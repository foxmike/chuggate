// =============================================================================
// ChugGate - Multiband / Crossover / Hiss Curtain Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase F multiband DSP described
// in .abracapocus/CHUGGATE_SPEC.md sections 2.2, 14, 15, 16, and 30.5:
//   * CrossoverSplitter two-way LR4 complementary reconstruction (magnitude,
//     not sample-identical — the band sum is an all-pass, phase-shifted)
//   * CrossoverSplitter three-way complementary reconstruction
//   * CrossoverSplitter crossover ordering protection (inverted edges leave
//     sorted, gap-respecting edges)
//   * Click-free crossover movement smoke (finite output, bounded deltas)
//   * MultibandGate per-band open/close smoothing ballistics and sum
//     recombination y = sum_b G_b * x_b
//   * MultibandGate mute/solo behaviour
//   * HissCurtain SNR -> log-domain cutoff mapping (high/low/mid) and
//     depth=0 bypass
//   * HissCurtain stability under SNR modulation (finite, no NaNs)
//
// All fixtures are deterministic: fixed sample rate (48 kHz), fixed block
// lengths, and explicit numeric tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "../Source/DSP/CrossoverSplitter.h"
#include "../Source/DSP/MultibandGate.h"
#include "../Source/DSP/HissCurtain.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

// Reconstruction magnitude tolerances. The band sum of a Linkwitz-Riley
// crossover is an all-pass with unity magnitude, so the RMS of the summed
// output must match the RMS of the dry input after IIR settling. We do NOT
// require sample-identical equality (spec 14 / 30.5).
constexpr float kReconRelativeRmsError = 0.01f; // -40 dB
constexpr float kExactTol = 1.0e-4f;
constexpr float kCutoffTolFraction = 0.03f; // 3 % log-domain cutoff tolerance

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Coherent sine: numCycles must be integral so the RMS over the window is the
// theoretical A / sqrt(2).
std::vector<float> makeSine(int numSamples, double sampleRate, double frequency,
                            double amplitude, double phase = 0.0)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples));
    const double twoPi = 6.28318530717958647692;
    for (int i = 0; i < numSamples; ++i)
        buffer[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * std::sin(twoPi * frequency * i / sampleRate + phase));
    return buffer;
}

// Broadband multi-sine: incoherent sum of several sines spanning low to high
// frequencies, normalised to a target peak amplitude.
std::vector<float> makeMultiSine(int numSamples, double sampleRate,
                                 float amplitude)
{
    const double freqs[] = { 110.0, 440.0, 1500.0, 3500.0, 7000.0, 12000.0 };
    std::vector<float> buffer(static_cast<std::size_t>(numSamples), 0.0f);
    const double twoPi = 6.28318530717958647692;
    for (int i = 0; i < numSamples; ++i)
    {
        double acc = 0.0;
        for (const double f : freqs)
            acc += std::sin(twoPi * f * i / sampleRate);
        buffer[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * acc / static_cast<double>(sizeof(freqs) / sizeof(freqs[0])));
    }
    return buffer;
}

// Deterministic white-ish noise via a simple LCG (no <random> seeding needed).
std::vector<float> makeNoise(int numSamples, float amplitude, uint32_t seed)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples));
    uint32_t state = seed ? seed : 0x12345678u;
    for (int i = 0; i < numSamples; ++i)
    {
        state = state * 1103515245u + 12345u;
        const float r = static_cast<float>(static_cast<int32_t>(state >> 8))
                        / static_cast<float>(0x00800000);
        buffer[static_cast<std::size_t>(i)] = amplitude * r;
    }
    return buffer;
}

float rms(const std::vector<float> &buffer, std::size_t start = 0)
{
    if (start >= buffer.size())
        return 0.0f;
    double sumSq = 0.0;
    std::size_t count = 0;
    for (std::size_t i = start; i < buffer.size(); ++i)
    {
        sumSq += static_cast<double>(buffer[i]) * buffer[i];
        ++count;
    }
    return count > 0 ? static_cast<float>(std::sqrt(sumSq / count)) : 0.0f;
}

float trailingRms(const std::vector<float> &buffer, double skipRatio)
{
    return rms(buffer, static_cast<std::size_t>(buffer.size() * skipRatio));
}

float peakAbs(const std::vector<float> &buffer)
{
    float peak = 0.0f;
    for (const auto v : buffer)
        peak = std::max(peak, std::fabs(v));
    return peak;
}

bool allFinite(const std::vector<float> &buffer)
{
    for (const auto v : buffer)
        if (! std::isfinite(v))
            return false;
    return true;
}

// Maximum absolute sample-to-sample delta of a buffer.
float maxSampleDelta(const std::vector<float> &buffer)
{
    float maxDelta = 0.0f;
    for (std::size_t i = 1; i < buffer.size(); ++i)
        maxDelta = std::max(maxDelta, std::fabs(buffer[i] - buffer[i - 1]));
    return maxDelta;
}

// ---------------------------------------------------------------------------
// Test class registered via a file-scope static instance.
// ---------------------------------------------------------------------------

class MultibandGateTests final : public juce::UnitTest
{
public:
    MultibandGateTests() : juce::UnitTest("ChugGate Multiband Tests", "DSP") {}

    void runTest() override
    {
        beginTest("CrossoverSplitter: two-way LR4 complementary reconstruction");
        runTwoWayReconstruction();

        beginTest("CrossoverSplitter: three-way complementary reconstruction");
        runThreeWayReconstruction();

        beginTest("CrossoverSplitter: ordering protection rejects inverted edges");
        runOrderingProtection();

        beginTest("CrossoverSplitter: click-free crossover movement smoke");
        runClickFreeMovement();

        beginTest("MultibandGate: per-band open/close smoothing and sum");
        runSmoothingBallistics();

        beginTest("MultibandGate: mute/solo forces band contributions to zero");
        runMuteSolo();

        beginTest("HissCurtain: SNR -> log-domain cutoff mapping");
        runSnrMapping();

        beginTest("HissCurtain: depth=0 bypass leaves output ~= input");
        runDepthBypass();

        beginTest("HissCurtain: stability under SNR modulation");
        runStability();
    }

private:
    // ---- CrossoverSplitter ------------------------------------------------

    void runTwoWayReconstruction()
    {
        // Spec 14 / 30.5: complementary LR4 reconstruction. The two-way band
        // sum (low + high) is an all-pass with unity magnitude, so the RMS of
        // the summed output must match the dry input RMS after IIR settling.
        CrossoverSplitter splitter;
        splitter.prepare(kSampleRate, static_cast<int>(kSampleRate), 1);
        splitter.setMode(CrossoverSplitter::Mode::TwoWay);
        splitter.setDualCrossoverHz(2500.0f);

        const int numSamples = static_cast<int>(kSampleRate); // 1 s
        auto input = makeMultiSine(numSamples, kSampleRate, 0.5f);

        std::vector<float> band0(static_cast<std::size_t>(numSamples), 0.0f);
        std::vector<float> band1(static_cast<std::size_t>(numSamples), 0.0f);
        float *inPtr = input.data();
        float *b0Ptr = band0.data();
        float *b1Ptr = band1.data();

        splitter.process(&inPtr, &b0Ptr, &b1Ptr, nullptr, 1, numSamples);

        // Sum the two bands and compare RMS to the dry input (trailing half to
        // skip the IIR transient). This is a magnitude check, deliberately not
        // sample-identical.
        std::vector<float> sum(static_cast<std::size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
            sum[static_cast<std::size_t>(i)] = band0[static_cast<std::size_t>(i)]
                                             + band1[static_cast<std::size_t>(i)];

        const std::size_t skip = static_cast<std::size_t>(numSamples * 0.5);
        const float inputRms = rms(input, skip);
        const float sumRms = rms(sum, skip);
        expect(inputRms > 0.0f, "Input RMS must be non-zero for the fixture");

        // The band sum is an all-pass with unity magnitude but a different
        // phase response than the dry input, so a sample-wise difference is
        // meaningless. The correct complementary-reconstruction check is a
        // magnitude (RMS) comparison, not sample-identical equality.
        const float relativeError = std::fabs(sumRms - inputRms) / inputRms;
        expect(relativeError < kReconRelativeRmsError,
               "Two-way sum RMS must match input RMS within -40 dB "
               "(all-pass unity magnitude, not sample-identical)");
    }

    void runThreeWayReconstruction()
    {
        // Spec 14 / 30.5: three-way complementary tree. The three band outputs
        // sum to a cascade of all-pass responses with unity magnitude.
        CrossoverSplitter splitter;
        splitter.prepare(kSampleRate, static_cast<int>(kSampleRate), 1);
        splitter.setMode(CrossoverSplitter::Mode::ThreeWay);
        splitter.setTriCrossoversHz(300.0f, 4000.0f);

        const int numSamples = static_cast<int>(kSampleRate);
        auto input = makeMultiSine(numSamples, kSampleRate, 0.5f);

        std::vector<float> band0(static_cast<std::size_t>(numSamples), 0.0f);
        std::vector<float> band1(static_cast<std::size_t>(numSamples), 0.0f);
        std::vector<float> band2(static_cast<std::size_t>(numSamples), 0.0f);
        float *inPtr = input.data();
        float *b0Ptr = band0.data();
        float *b1Ptr = band1.data();
        float *b2Ptr = band2.data();

        splitter.process(&inPtr, &b0Ptr, &b1Ptr, &b2Ptr, 1, numSamples);

        std::vector<float> sum(static_cast<std::size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
            sum[static_cast<std::size_t>(i)] = band0[static_cast<std::size_t>(i)]
                                             + band1[static_cast<std::size_t>(i)]
                                             + band2[static_cast<std::size_t>(i)];

        const std::size_t skip = static_cast<std::size_t>(numSamples * 0.5);
        const float inputRms = rms(input, skip);
        const float sumRms = rms(sum, skip);
        expect(inputRms > 0.0f, "Input RMS must be non-zero for the fixture");

        const float relativeError = std::fabs(sumRms - inputRms) / inputRms;
        expect(relativeError < kReconRelativeRmsError,
               "Three-way sum RMS must match input RMS within -40 dB "
               "(complementary all-pass reconstruction)");
    }

    void runOrderingProtection()
    {
        // Spec 14: ordering protection. Inverted edges (low > high) must be
        // rejected, leaving sorted, gap-respecting crossovers.
        CrossoverSplitter splitter;
        splitter.prepare(kSampleRate, kBlockSize, 1);
        splitter.setMode(CrossoverSplitter::Mode::ThreeWay);

        // Deliberately inverted: low edge above high edge.
        splitter.setTriCrossoversHz(5000.0f, 200.0f);

        const float low = splitter.getTriLowCrossoverHz();
        const float high = splitter.getTriHighCrossoverHz();

        expect(low < high, "Tri low crossover must be below tri high after ordering protection");
        expect(high - low >= CrossoverSplitter::minimumCrossoverGapHz,
               "Tri crossovers must respect the minimum gap after protection");
        expect(low <= CrossoverSplitter::triLowMaximumHz,
               "Tri low must remain within its valid range");
        expect(high >= CrossoverSplitter::triHighMinimumHz,
               "Tri high must remain within its valid range");

        // A second, valid call must remain stable.
        splitter.setTriCrossoversHz(250.0f, 6000.0f);
        expectEquals(splitter.getTriLowCrossoverHz(), 250.0f,
                     "Valid tri low crossover must be stored verbatim");
        expectEquals(splitter.getTriHighCrossoverHz(), 6000.0f,
                     "Valid tri high crossover must be stored verbatim");
    }

    void runClickFreeMovement()
    {
        // Spec 14 / 30.5: click-free crossover movement. Sweep the dual
        // crossover from 2 kHz to 6 kHz over a block and assert the summed
        // output stays finite with bounded sample-to-sample deltas. This is a
        // generous smoke check, not a step-discontinuity equality test.
        CrossoverSplitter splitter;
        splitter.prepare(kSampleRate, static_cast<int>(kSampleRate), 1);
        splitter.setMode(CrossoverSplitter::Mode::TwoWay);

        const int numSamples = static_cast<int>(kSampleRate * 0.2); // 0.2 s
        auto input = makeSine(numSamples, kSampleRate, 1000.0, 0.5);

        std::vector<float> band0(static_cast<std::size_t>(numSamples), 0.0f);
        std::vector<float> band1(static_cast<std::size_t>(numSamples), 0.0f);
        std::vector<float> sum(static_cast<std::size_t>(numSamples), 0.0f);

        // Process sample-by-sample so we can move the crossover each sample.
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(numSamples - 1);
            const float crossed = 2000.0f + t * (6000.0f - 2000.0f);
            splitter.setDualCrossoverHz(crossed);

            float inS = input[static_cast<std::size_t>(i)];
            float b0 = 0.0f;
            float b1 = 0.0f;
            float *inPtr = &inS;
            float *b0Ptr = &b0;
            float *b1Ptr = &b1;
            splitter.process(&inPtr, &b0Ptr, &b1Ptr, nullptr, 1, 1);

            sum[static_cast<std::size_t>(i)] = b0 + b1;
        }

        expect(allFinite(sum), "Summed output must remain finite during crossover sweep");

        // Peak input amplitude is 0.5, so a generous bound of 4x the input
        // peak accommodates legitimate filter transient ringing without
        // accepting step discontinuities.
        const float delta = maxSampleDelta(sum);
        expect(delta < 4.0f * peakAbs(input),
               "Sample-to-sample delta must stay bounded during crossover movement");

        const float energy = rms(sum);
        expect(energy > 0.0f && std::isfinite(energy),
               "Summed output must carry finite energy during crossover movement");
    }

    // ---- MultibandGate ----------------------------------------------------

    // Helper: drive a single band with a constant input and a freshly set
    // target, returning the gain after processing numSamples.
    float processSingleBandGain(MultibandGate &gate, float target,
                                int numSamples)
    {
        gate.setBandTargetGain(0, target);
        std::vector<float> bandIn(static_cast<std::size_t>(numSamples), 1.0f);
        std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);
        float *b0 = bandIn.data();
        float *outPtr = out.data();
        gate.process(&b0, nullptr, nullptr, &outPtr, 1, numSamples);
        return gate.getBandGain(0);
    }

    void runSmoothingBallistics()
    {
        // Spec 16: per-band open/close smoothing.
        //   G_b[n] = alpha * G_b[n-1] + (1-alpha) * G_b*[n]
        //   alpha = exp(-1/(fs*tau_open))  when target > current
        //   alpha = exp(-1/(fs*tau_close)) when target <= current
        // With a deep floor (g_floor ~ 0), a target step 1 -> 0 closes along
        // the close time constant; a step 0 -> 1 opens along the open time
        // constant. y = sum_b G_b * x_b, so with x_b = 1 the output tracks G_b.

        constexpr float kFloorDb = -80.0f; // g_floor ~ 1e-4 ~ 0
        constexpr float kOpenMs = 5.0f;
        constexpr float kCloseMs = 50.0f;

        // --- Closing ballistics ---
        {
            MultibandGate gate;
            gate.prepare(kSampleRate, static_cast<int>(kSampleRate), 1, 3);
            gate.setNumBands(1);
            gate.setBandFloorDb(0, kFloorDb);
            gate.setCurveGamma(0, 1.0f);
            gate.setBandOpenTime(0, kOpenMs);
            gate.setBandCloseTime(0, kCloseMs);
            gate.reset();

            // Settle at fully open (target = 1).
            processSingleBandGain(gate, 1.0f, static_cast<int>(kSampleRate * 0.1));
            expect(gate.getBandGain(0) > 0.99f,
                   "Gain must settle near unity before the closing step");

            // Step target 1 -> 0 and process one close time constant.
            // G ~ exp(-1) ~= 0.368 after one tau (not yet settled).
            const int oneTau = static_cast<int>(kSampleRate * kCloseMs * 0.001);
            const float gAfterOneTau = processSingleBandGain(gate, 0.0f, oneTau);
            expect(gAfterOneTau > 0.2f && gAfterOneTau < 0.5f,
                   "After ~1 close tau the gain must be near exp(-1) ~ 0.37, "
                   "not yet settled");

            // After many tau, gain must approach the floor (~0).
            const int manyTau = static_cast<int>(kSampleRate * kCloseMs * 0.001 * 7.0);
            const float gAfterManyTau = processSingleBandGain(gate, 0.0f, manyTau);
            expect(gAfterManyTau < 0.01f,
                   "After many close taus the gain must approach the floor");

            // y = G_b * x_b with x_b = 1, so the last output sample tracks G_b.
            // Re-check the recombination directly: feed ones and read output.
            std::vector<float> bandIn(static_cast<std::size_t>(kBlockSize), 1.0f);
            std::vector<float> out(static_cast<std::size_t>(kBlockSize), 0.0f);
            float *b0 = bandIn.data();
            float *outPtr = out.data();
            gate.setBandTargetGain(0, 0.0f);
            gate.process(&b0, nullptr, nullptr, &outPtr, 1, kBlockSize);
            const float gainNow = gate.getBandGain(0);
            expect(std::fabs(out.back() - gainNow) < kExactTol,
                   "Output sample must equal G_b * x_b with x_b = 1");
        }

        // --- Opening ballistics ---
        {
            MultibandGate gate;
            gate.prepare(kSampleRate, static_cast<int>(kSampleRate), 1, 3);
            gate.setNumBands(1);
            gate.setBandFloorDb(0, kFloorDb);
            gate.setCurveGamma(0, 1.0f);
            gate.setBandOpenTime(0, kOpenMs);
            gate.setBandCloseTime(0, kCloseMs);
            gate.reset();

            // Force the gain to ~0 by closing for a long time.
            processSingleBandGain(gate, 0.0f, static_cast<int>(kSampleRate * 0.5));
            expect(gate.getBandGain(0) < 0.01f,
                   "Gain must be near floor before the opening step");

            // Step target 0 -> 1 and process one open time constant.
            // G ~ 1 - exp(-1) ~= 0.632 after one tau (not yet settled).
            const int oneTau = static_cast<int>(kSampleRate * kOpenMs * 0.001);
            const float gAfterOneTau = processSingleBandGain(gate, 1.0f, oneTau);
            expect(gAfterOneTau > 0.5f && gAfterOneTau < 0.75f,
                   "After ~1 open tau the gain must be near 1-exp(-1) ~ 0.63, "
                   "not yet settled");

            // After many open taus, gain must approach unity.
            const int manyTau = static_cast<int>(kSampleRate * kOpenMs * 0.001 * 7.0);
            const float gAfterManyTau = processSingleBandGain(gate, 1.0f, manyTau);
            expect(gAfterManyTau > 0.99f,
                   "After many open taus the gain must approach unity");
        }

        // --- Close tau slower/faster as set ---
        {
            // After a fixed wall-clock close duration, a slower close time
            // constant must retain more gain than a faster one.
            const float fixedMs = 50.0f;
            const int fixedSamples = static_cast<int>(kSampleRate * fixedMs * 0.001);

            auto closeFor = [&](float closeMs) {
                MultibandGate gate;
                gate.prepare(kSampleRate, static_cast<int>(kSampleRate), 1, 3);
                gate.setNumBands(1);
                gate.setBandFloorDb(0, kFloorDb);
                gate.setCurveGamma(0, 1.0f);
                gate.setBandOpenTime(0, kOpenMs);
                gate.setBandCloseTime(0, closeMs);
                gate.reset();
                processSingleBandGain(gate, 1.0f, static_cast<int>(kSampleRate * 0.1));
                return processSingleBandGain(gate, 0.0f, fixedSamples);
            };

            const float slow = closeFor(50.0f); // 1 tau in 50 ms
            const float fast = closeFor(5.0f);  // 10 tau in 50 ms
            expect(slow > fast,
                   "Slower close tau must retain more gain than faster close "
                   "tau over the same wall-clock duration");
            expect(slow > 0.2f,
                   "Slow close (~1 tau) must still be far from settled");
            expect(fast < 0.01f,
                   "Fast close (~10 tau) must be near the floor");
        }
    }

    void runMuteSolo()
    {
        // Spec 16: solo band 0 forces other band contributions near zero;
        // mute forces a single band's contribution near zero.
        constexpr float kFloorDb = -80.0f;
        const int settle = static_cast<int>(kSampleRate * 0.2);

        // Three bands, each fed a constant 1.0, targets fully open.
        auto runGate = [&](int soloBand, int muteBand) {
            MultibandGate gate;
            gate.prepare(kSampleRate, static_cast<int>(kSampleRate), 1, 3);
            gate.setNumBands(3);
            for (int b = 0; b < 3; ++b)
            {
                gate.setBandFloorDb(b, kFloorDb);
                gate.setCurveGamma(b, 1.0f);
                gate.setBandOpenTime(b, 5.0f);
                gate.setBandCloseTime(b, 50.0f);
                gate.setBandTargetGain(b, 1.0f);
            }
            gate.reset();

            if (soloBand >= 0)
                gate.setBandSolo(soloBand, true);
            if (muteBand >= 0)
                gate.setBandMute(muteBand, true);

            std::vector<float> b0(static_cast<std::size_t>(settle), 1.0f);
            std::vector<float> b1(static_cast<std::size_t>(settle), 1.0f);
            std::vector<float> b2(static_cast<std::size_t>(settle), 1.0f);
            std::vector<float> out(static_cast<std::size_t>(settle), 0.0f);
            float *p0 = b0.data();
            float *p1 = b1.data();
            float *p2 = b2.data();
            float *outPtr = out.data();
            gate.process(&p0, &p1, &p2, &outPtr, 1, settle);
            return out;
        };

        // Solo band 0: bands 1 and 2 must collapse to ~0, band 0 stays open.
        {
            const auto out = runGate(0, -1);
            // Output y = sum_b G_b x_b; with x_b = 1 the trailing output equals
            // the sum of the smoothed gains. After settling, only band 0
            // contributes, so y ~= G_0 ~= 1.
            expect(std::fabs(out.back() - 1.0f) < 0.05f,
                   "Solo band 0 must leave the summed output near unity "
                   "(other bands forced to zero)");
        }

        // Mute band 0 (no solo): band 0 collapses, bands 1 and 2 stay open.
        {
            const auto out = runGate(-1, 0);
            // With band 0 muted, y ~= G_1 + G_2 ~= 2.
            expect(std::fabs(out.back() - 2.0f) < 0.05f,
                   "Mute band 0 must remove only band 0's contribution "
                   "(other bands remain open)");
        }
    }

    // ---- HissCurtain ------------------------------------------------------

    void runSnrMapping()
    {
        // Spec 2.2: ln(f_c) = ln(f_min) + Q_H * (ln(f_max) - ln(f_min)),
        // Q_H = clamp((SNR_H - S_min) / (S_max - S_min), 0, 1).
        // High SNR -> f_c near f_max; low SNR -> f_c near f_min; mid SNR ->
        // geometric midpoint on a log scale.
        constexpr float kMinHz = 1000.0f;
        constexpr float kMaxHz = 16000.0f;
        constexpr float kSMin = 0.0f;
        constexpr float kSMax = 30.0f;

        auto cutoffForSnr = [&](float snrDb) {
            HissCurtain curtain;
            curtain.prepare(kSampleRate, static_cast<int>(kSampleRate), 1);
            curtain.setMinFreqHz(kMinHz);
            curtain.setMaxFreqHz(kMaxHz);
            curtain.setSnrRange(kSMin, kSMax);
            curtain.setDepth(1.0f);
            curtain.setSnr(snrDb);

            // Process enough samples for the log-cutoff smoothing to converge
            // (cutoffSmoothingSeconds ~ 0.02 s -> 0.2 s is plenty).
            const int numSamples = static_cast<int>(kSampleRate * 0.3);
            std::vector<float> in(static_cast<std::size_t>(numSamples), 0.0f);
            std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);
            float *inPtr = in.data();
            float *outPtr = out.data();
            curtain.process(&inPtr, &outPtr, 1, numSamples);
            return curtain.getCutoffHz();
        };

        const float tol = kCutoffTolFraction; // 3 % relative

        const float high = cutoffForSnr(kSMax); // Q=1 -> f_max
        expect(std::fabs(high - kMaxHz) / kMaxHz < tol,
               "High SNR must map cutoff near f_max");

        const float low = cutoffForSnr(kSMin); // Q=0 -> f_min
        expect(std::fabs(low - kMinHz) / kMinHz < tol,
               "Low SNR must map cutoff near f_min");

        const float mid = cutoffForSnr((kSMin + kSMax) * 0.5f); // Q=0.5
        const float geomMid =
            static_cast<float>(std::sqrt(static_cast<double>(kMinHz) * kMaxHz));
        expect(std::fabs(mid - geomMid) / geomMid < tol,
               "Mid SNR must map cutoff near the geometric midpoint of "
               "f_min and f_max on a log scale");

        // Monotonicity: higher SNR must never yield a lower cutoff.
        expect(high > mid && mid > low,
               "Cutoff must be monotonic in SNR");
    }

    void runDepthBypass()
    {
        // Spec 2.2 / 30.5: depth is a dry/filtered blend. depth = 0 leaves the
        // output ~= input (RMS), i.e. the curtain is bypassed.
        HissCurtain curtain;
        curtain.prepare(kSampleRate, static_cast<int>(kSampleRate), 1);
        curtain.setMinFreqHz(1000.0f);
        curtain.setMaxFreqHz(16000.0f);
        curtain.setSnrRange(0.0f, 30.0f);
        curtain.setSnr(0.0f); // aggressive cutoff
        curtain.setDepth(0.0f);

        const int numSamples = static_cast<int>(kSampleRate); // 1 s
        auto input = makeSine(numSamples, kSampleRate, 2000.0, 0.5);
        std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);
        float *inPtr = input.data();
        float *outPtr = out.data();
        curtain.process(&inPtr, &outPtr, 1, numSamples);

        expect(allFinite(out), "Output must be finite with depth=0");

        const std::size_t skip = static_cast<std::size_t>(numSamples * 0.3);
        const float inRms = rms(input, skip);
        const float outRms = rms(out, skip);
        expect(inRms > 0.0f, "Input RMS must be non-zero");
        // depth smoothing converges to 0 well within the trailing window, so
        // the output must match the dry input to within ~0.1 dB.
        const float ratioDb = 20.0f * std::log10(outRms / inRms);
        expect(std::fabs(ratioDb) < 0.1f,
               "depth=0 must leave output RMS ~= input RMS within 0.1 dB");
    }

    void runStability()
    {
        // Spec 2.2 / 30.5: modulation-stable TPT filter. Process a dial-tone
        // plus noise while modulating SNR; output must stay finite, no NaNs.
        HissCurtain curtain;
        curtain.prepare(kSampleRate, static_cast<int>(kSampleRate), 1);
        curtain.setMinFreqHz(1000.0f);
        curtain.setMaxFreqHz(16000.0f);
        curtain.setSnrRange(0.0f, 30.0f);
        curtain.setDepth(0.8f);

        const int numSamples = static_cast<int>(kSampleRate * 0.5);
        auto tone1 = makeSine(numSamples, kSampleRate, 440.0, 0.3);
        auto tone2 = makeSine(numSamples, kSampleRate, 660.0, 0.2);
        auto noise = makeNoise(numSamples, 0.05f, 0xABCDEF01u);

        std::vector<float> input(static_cast<std::size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
            input[static_cast<std::size_t>(i)] = tone1[static_cast<std::size_t>(i)]
                                               + tone2[static_cast<std::size_t>(i)]
                                               + noise[static_cast<std::size_t>(i)];

        std::vector<float> out(static_cast<std::size_t>(numSamples), 0.0f);

        // Process sample-by-sample, sweeping SNR across the full range.
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(numSamples - 1);
            curtain.setSnr(30.0f * t);
            float inS = input[static_cast<std::size_t>(i)];
            float outS = 0.0f;
            float *inSPtr = &inS;
            float *outSPtr = &outS;
            curtain.process(&inSPtr, &outSPtr, 1, 1);
            out[static_cast<std::size_t>(i)] = outS;
        }

        expect(allFinite(out), "Output must stay finite under SNR modulation");
        expect(peakAbs(out) < 10.0f,
               "Output must not run away under SNR modulation");
        const float energy = trailingRms(out, 0.1);
        expect(std::isfinite(energy) && energy > 0.0f,
               "Output must carry finite energy under SNR modulation");
    }
};

// Static instance registers the test with juce::UnitTestRunner.
static MultibandGateTests s_multibandGateTestsInstance;

} // namespace
