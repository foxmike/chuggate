// =============================================================================
// ChugGate - Sustain Intelligence / Feedback Friend Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase D maths described in
// .abracapocus/CHUGGATE_SPEC.md sections 9 and 10:
//   * 9.2 Harmonicity H = sum_m(sum_{f near m*f0} P(f)) / (sum P + eps)
//   * 9.5 Composite sustain score S = w_h*H + w_d*D + w_e*E - w_n*N
//   * 9.6 Adaptive release tau = tau_base * [1 + A_s * S^gamma], clamped to max
//   * 10.2 Feedback confidence F = w_h*H + w_s*(1-D_s) + w_g*G - w_t*T
//   * 10.4 Safety: max hold / safety timeout clears protection; minimum
//     harmonicity is mandatory and prevents engagement / holds.
//
// Also covers FundamentalEstimator (section 9 in-band f0 evidence):
//   * Synthetic sine near a known f0 yields validity=true and an estimate
//     within a stated relative tolerance.
//   * Near-silence yields validity=false (low confidence).
//
// All fixtures are deterministic: fixed sample rate (48000), fixed lengths,
// explicit tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Source/DSP/FundamentalEstimator.h"
#include "../Source/DSP/SustainAnalyzer.h"
#include "../Source/DSP/FeedbackDetector.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

// Tolerances.
constexpr float kExactTol = 1.0e-4f;
constexpr float kF0RelativeTol = 0.02f; // 2 % relative tolerance for f0 estimate

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

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

// Deterministic white-noise fixture (LCG, no <random> dependency).
std::vector<float> makeNoise(int numSamples, float amplitude, std::uint32_t seed)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples));
    std::uint32_t state = seed;
    for (int i = 0; i < numSamples; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const float u = static_cast<float>((state >> 8) & 0xFFFFFFu) /
                        static_cast<float>(0x1000000u); // [0,1)
        buffer[static_cast<std::size_t>(i)] = amplitude * (2.0f * u - 1.0f);
    }
    return buffer;
}

// Harmonic stack: fundamental + integer partials, each at the given amplitude,
// starting at phase 0 for coherent addition. f0 is chosen to land on an exact
// Goertzel bin centre of the SustainAnalyzer/FeedbackDetector spectrum
// (binWidth = sr / 512 = 93.75 Hz; f0 = 2 * binWidth = 187.5 Hz).
std::vector<float> makeHarmonicStack(int numSamples, double sampleRate,
                                     double fundamentalHz, int partials,
                                     double perPartialAmplitude)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples), 0.0f);
    const double twoPi = 6.28318530717958647692;
    for (int m = 1; m <= partials; ++m)
    {
        const double freq = fundamentalHz * static_cast<double>(m);
        for (int i = 0; i < numSamples; ++i)
            buffer[static_cast<std::size_t>(i)] +=
                static_cast<float>(perPartialAmplitude *
                                   std::sin(twoPi * freq * i / sampleRate));
    }
    return buffer;
}

// RMS of a buffer (full window).
float rms(const std::vector<float> &buffer)
{
    if (buffer.empty())
        return 0.0f;
    double sumSq = 0.0;
    for (const auto v : buffer)
        sumSq += static_cast<double>(v) * v;
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(buffer.size())));
}

// Scale a buffer to a target RMS (in-place) so harmonic vs noise fixtures can
// be compared at similar power.
void normalizeToRms(std::vector<float> &buffer, float targetRms)
{
    const float current = rms(buffer);
    if (current <= 1.0e-12f)
        return;
    const float gain = targetRms / current;
    for (auto &v : buffer)
        v *= gain;
}

// Constant detector-level companion buffer.
std::vector<float> makeConstantDetector(int numSamples, float levelDb)
{
    return std::vector<float>(static_cast<std::size_t>(numSamples), levelDb);
}

// Run a mono key + detector-level pair through the analyzer with a single
// coherent FundamentalEstimate snapshot for the whole block.
std::vector<SustainAnalyzer::Output>
runSustain(SustainAnalyzer &analyzer, const std::vector<float> &monoKey,
           const std::vector<float> &detectorLevelDb,
           const FundamentalEstimate &fundamental)
{
    const int n = static_cast<int>(monoKey.size());
    std::vector<SustainAnalyzer::Output> out(static_cast<std::size_t>(n));
    analyzer.process(monoKey.data(), detectorLevelDb.data(), n, fundamental,
                     out.data());
    return out;
}

std::vector<FeedbackDetector::Output>
runFeedback(FeedbackDetector &detector, const std::vector<float> &monoKey,
            const std::vector<float> &detectorLevelDb,
            const FundamentalEstimate &fundamental)
{
    const int n = static_cast<int>(monoKey.size());
    std::vector<FeedbackDetector::Output> out(static_cast<std::size_t>(n));
    detector.process(monoKey.data(), detectorLevelDb.data(), n, fundamental,
                     nullptr, true, out.data());
    return out;
}

// A synthetic, valid fundamental estimate at a chosen f0.
FundamentalEstimate makeFundamental(float f0, float confidence = 1.0f)
{
    FundamentalEstimate f;
    f.estimatedFundamental = f0;
    f.confidence = confidence;
    f.timestamp = 0;
    f.validity = true;
    return f;
}

// Configure a SustainAnalyzer with the adaptive-release defaults used by the
// harmonicity / adaptive-release tests. SustainAnalyzer holds atomics and is
// non-movable, so it must be configured in place rather than returned.
void configureSustain(SustainAnalyzer &a)
{
    a.prepare(kSampleRate, kBlockSize);
    a.setEnabled(true);
    a.setBaseReleaseMs(100.0f);
    a.setAdaptiveAmount(3.0f);
    a.setGamma(1.5f);
    a.setMaxAdaptiveRelease(500.0f);
}

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

class SustainTests final : public juce::UnitTest
{
public:
    SustainTests() : juce::UnitTest("ChugGate Sustain/Feedback Tests", "DSP") {}

    void runTest() override
    {
        beginTest("FundamentalEstimator: in-band sine yields valid f0 within tolerance");
        runFundamentalEstimatorInBand();

        beginTest("FundamentalEstimator: near-silence is invalid / low confidence");
        runFundamentalEstimatorSilence();

        beginTest("SustainAnalyzer: harmonic stack scores higher H/S than "
                  "equal-RMS noise");
        runHarmonicityVsNoise();

        beginTest("SustainAnalyzer: adaptive release lengthens with S_sustain "
                  "and clamps to max");
        runAdaptiveRelease();

        beginTest("FeedbackDetector: safety timeout / max hold clears protection");
        runFeedbackSafetyClear();

        beginTest("FeedbackDetector: minimum harmonicity prevents engage and hold");
        runFeedbackMinHarmonicity();

        beginTest("FeedbackDetector: stable harmonic tone scores higher F than "
                  "transient/noise chatter");
        runFeedbackConfidencePolarity();
    }

private:
    // ---- FundamentalEstimator --------------------------------------------

    void runFundamentalEstimatorInBand()
    {
        // 200 Hz at the 8000 Hz analysis rate is exactly 40 decimated samples
        // per period, so the ACF peak lands on an integer lag with zero
        // fractional parabolic offset.
        constexpr double kF0 = 200.0;
        const int numSamples = 8000;
        auto sine = makeSine(numSamples, kSampleRate, kF0, 0.3);

        FundamentalEstimator est;
        est.prepare(kSampleRate, kBlockSize);
        est.reset();
        est.process(sine.data(), numSamples);

        const auto snap = est.getEstimate();
        expect(snap.validity,
               "In-band sine must produce a valid fundamental estimate");
        expect(snap.confidence > 0.3f,
               "In-band sine must produce non-trivial confidence");

        if (snap.validity)
        {
            const float relError =
                std::fabs(snap.estimatedFundamental - static_cast<float>(kF0)) /
                static_cast<float>(kF0);
            expect(relError <= kF0RelativeTol,
                   "Estimated f0 must be within stated relative tolerance of 200 Hz");
        }
    }

    void runFundamentalEstimatorSilence()
    {
        // Amplitude well below minimumRms (1e-5) -> the estimator must publish
        // invalid / zero-confidence evidence rather than a spurious f0.
        const int numSamples = 8000;
        auto silence = makeSine(numSamples, kSampleRate, 200.0, 1.0e-7);

        FundamentalEstimator est;
        est.prepare(kSampleRate, kBlockSize);
        est.reset();
        est.process(silence.data(), numSamples);

        const auto snap = est.getEstimate();
        expect(! snap.validity,
               "Near-silence must not produce a valid fundamental estimate");
        expect(snap.confidence <= 0.05f,
               "Near-silence must yield low confidence");
        expect(std::fabs(snap.estimatedFundamental) < kExactTol,
               "Near-silence must publish zero estimated fundamental");
    }

    // ---- SustainAnalyzer harmonicity -------------------------------------

    void runHarmonicityVsNoise()
    {
        // f0 = 187.5 Hz lands on an exact Goertzel bin centre; its 8 partials
        // also land on exact bin centres, maximising harmonic-bin power.
        constexpr float kF0 = 187.5f;
        const auto fundamental = makeFundamental(kF0);

        const int numSamples = 2000;
        auto harmonic = makeHarmonicStack(numSamples, kSampleRate, kF0, 8, 0.08);
        auto noise = makeNoise(numSamples, 0.2f, 0xBEEFu);
        // Equalise RMS so the comparison is at similar power.
        normalizeToRms(harmonic, 0.2f);
        normalizeToRms(noise, 0.2f);

        const auto det = makeConstantDetector(numSamples, -12.0f);

        SustainAnalyzer harmonicAnalyzer;
        harmonicAnalyzer.prepare(kSampleRate, kBlockSize);
        harmonicAnalyzer.setEnabled(true);
        const auto hOut = runSustain(harmonicAnalyzer, harmonic, det, fundamental);

        SustainAnalyzer noiseAnalyzer;
        noiseAnalyzer.prepare(kSampleRate, kBlockSize);
        noiseAnalyzer.setEnabled(true);
        const auto nOut = runSustain(noiseAnalyzer, noise, det, fundamental);

        const float hH = harmonicAnalyzer.getHarmonicity();
        const float nH = noiseAnalyzer.getHarmonicity();
        const float hScore = harmonicAnalyzer.getSustainScore();
        const float nScore = noiseAnalyzer.getSustainScore();

        expect(hH > nH + 0.5f,
               "Harmonic stack must yield much higher harmonicity than noise");
        expect(hScore > nScore + 0.2f,
               "Harmonic stack must yield higher composite sustain score than noise");
        expect(hH > 0.5f, "Harmonic stack harmonicity should be high");
        expect(nH < 0.5f, "Noise fixture harmonicity should be below half");
    }

    // ---- 9.6 Adaptive release --------------------------------------------

    void runAdaptiveRelease()
    {
        constexpr float kF0 = 187.5f;
        const auto fundamental = makeFundamental(kF0);
        const int numSamples = 2000;
        auto harmonic = makeHarmonicStack(numSamples, kSampleRate, kF0, 8, 0.08);
        auto noise = makeNoise(numSamples, 0.2f, 0xBEEFu);
        normalizeToRms(harmonic, 0.2f);
        normalizeToRms(noise, 0.2f);
        const auto det = makeConstantDetector(numSamples, -12.0f);

        SustainAnalyzer highAnalyzerInst;
        configureSustain(highAnalyzerInst);
        runSustain(highAnalyzerInst, harmonic, det, fundamental);
        const float highTau = highAnalyzerInst.getAdaptiveRelease();

        SustainAnalyzer lowAnalyzerInst;
        configureSustain(lowAnalyzerInst);
        runSustain(lowAnalyzerInst, noise, det, fundamental);
        const float lowTau = lowAnalyzerInst.getAdaptiveRelease();

        // Explicit clamp verification: aggressive amount + low cap clamps hard.
        SustainAnalyzer clampAnalyzerInst;
        configureSustain(clampAnalyzerInst);
        clampAnalyzerInst.setAdaptiveAmount(20.0f);
        clampAnalyzerInst.setGamma(1.0f);
        clampAnalyzerInst.setMaxAdaptiveRelease(150.0f);
        runSustain(clampAnalyzerInst, harmonic, det, fundamental);
        const float clampedTau = clampAnalyzerInst.getAdaptiveRelease();

        // Section 9.6: higher S_sustain lengthens tau_release_eff.
        expect(highTau > lowTau,
               "High sustain score must lengthen adaptive release vs low score");
        expect(highTau > 100.0f,
               "High sustain score must lengthen release beyond the base time");
        // Clamp to configured maximum.
        expect(highTau <= 500.0f + kExactTol,
               "Adaptive release must be clamped to the configured maximum");
        expect(lowTau >= 100.0f - kLevelTolDb,
               "Low-score release must sit near the base release");

        expect(std::fabs(clampedTau - 150.0f) < kExactTol,
               "Aggressive adaptive release must clamp exactly to the cap");
    }

    // ---- 10.4 Feedback safety --------------------------------------------

    void runFeedbackSafetyClear()
    {
        // Short mandatory durations so the engage/clear cycle is compact.
        constexpr float kF0 = 200.0f;
        const auto fundamental = makeFundamental(kF0);
        const int numSamples = 3000;
        auto sine = makeSine(numSamples, kSampleRate, kF0, 0.3);
        const auto det = makeConstantDetector(numSamples, -10.0f);

        FeedbackDetector detector;
        detector.prepare(kSampleRate, kBlockSize);
        detector.setEnabled(true);
        detector.setMinimumDurationMs(10.0f);  // ~480 samples
        detector.setMaximumHoldMs(20.0f);      // ~960 samples
        detector.setSafetyTimeoutMs(30.0f);    // ~1440 samples
        detector.setMinimumHarmonicity(0.45f);

        const auto out = runFeedback(detector, sine, det, fundamental);

        bool everActive = false;
        int firstActive = -1;
        int lastActive = -1;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            if (out[i].feedbackActive)
            {
                everActive = true;
                if (firstActive < 0)
                    firstActive = static_cast<int>(i);
                lastActive = static_cast<int>(i);
            }
        }

        expect(everActive, "Stable harmonic tone must engage feedback protection");
        expect(firstActive >= 0 && firstActive < 1500,
               "Protection must engage within a reasonable window");
        // By the end, max hold / safety timeout must have cleared protection.
        expect(! out.back().feedbackActive,
               "Protection must clear after exceeding max hold / safety timeout");
        expect(lastActive < static_cast<int>(out.size()) - 1,
               "Protection must not remain active through the end of the run");

        // Safety latch: continuing the same stimulus must not re-engage.
        const int extra = 1000;
        auto more = makeSine(extra, kSampleRate, kF0, 0.3);
        const auto moreDet = makeConstantDetector(extra, -10.0f);
        const auto moreOut = runFeedback(detector, more, moreDet, fundamental);
        bool reengaged = false;
        for (const auto &o : moreOut)
            reengaged = reengaged || o.feedbackActive;
        expect(! reengaged,
               "Safety latch must prevent re-engagement after a safety expiry");
    }

    void runFeedbackMinHarmonicity()
    {
        constexpr float kF0 = 200.0f;
        const auto fundamental = makeFundamental(kF0);
        const int numSamples = 2500;
        const auto det = makeConstantDetector(numSamples, -10.0f);

        // (a) High minimum-harmonicity gate + noise: must never engage.
        {
            auto noise = makeNoise(numSamples, 0.2f, 0x5A5Au);
            normalizeToRms(noise, 0.2f);

            FeedbackDetector detector;
            detector.prepare(kSampleRate, kBlockSize);
            detector.setEnabled(true);
            detector.setMinimumDurationMs(10.0f);
            detector.setMinimumHarmonicity(0.9f); // above noise H

            const auto out = runFeedback(detector, noise, det, fundamental);
            bool everActive = false;
            for (const auto &o : out)
                everActive = everActive || o.feedbackActive;
            expect(! everActive,
                   "Below-minimum harmonicity must prevent feedback engagement");
        }

        // (b) Engage with harmonic sine, then switch to noise: low H must
        // clear protection immediately (no "hold through" low-H noise).
        {
            const int sineSamples = 1200;
            auto sine = makeSine(sineSamples, kSampleRate, kF0, 0.3);
            const int noiseSamples = 1300;
            auto noise = makeNoise(noiseSamples, 0.2f, 0x5A5Au);
            normalizeToRms(noise, 0.2f);

            std::vector<float> mono;
            mono.insert(mono.end(), sine.begin(), sine.end());
            mono.insert(mono.end(), noise.begin(), noise.end());
            const auto monoDet = makeConstantDetector(static_cast<int>(mono.size()), -10.0f);

            FeedbackDetector detector;
            detector.prepare(kSampleRate, kBlockSize);
            detector.setEnabled(true);
            detector.setMinimumDurationMs(10.0f);
            detector.setMaximumHoldMs(500.0f); // long enough that H-gate clears first
            detector.setMinimumHarmonicity(0.45f);

            const auto out = runFeedback(detector, mono, monoDet, fundamental);
            bool everActive = false;
            for (const auto &o : out)
                everActive = everActive || o.feedbackActive;
            expect(everActive, "Harmonic sine must engage protection before the switch");
            expect(! out.back().feedbackActive,
                   "Dropping below minimum harmonicity must clear protection");
        }
    }

    // ---- 10.2 Feedback confidence polarity -------------------------------

    void runFeedbackConfidencePolarity()
    {
        constexpr float kF0 = 200.0f;
        const auto fundamental = makeFundamental(kF0);
        const int numSamples = 2500;
        const auto det = makeConstantDetector(numSamples, -10.0f);

        // Stable harmonic long tone.
        auto sine = makeSine(numSamples, kSampleRate, kF0, 0.2f);
        FeedbackDetector stable;
        stable.prepare(kSampleRate, kBlockSize);
        stable.setEnabled(true);
        runFeedback(stable, sine, det, fundamental);
        const float stableConf = stable.getFeedbackConfidence();

        // Rapid transient / noise chatter fixture: equal-RMS white noise.
        auto noise = makeNoise(numSamples, 0.2f, 0xCAFEu);
        normalizeToRms(noise, 0.2f);
        FeedbackDetector chatter;
        chatter.prepare(kSampleRate, kBlockSize);
        chatter.setEnabled(true);
        runFeedback(chatter, noise, det, fundamental);
        const float chatterConf = chatter.getFeedbackConfidence();

        expect(stableConf > chatterConf + 0.3f,
               "Stable harmonic tone must score higher F than noise chatter");
        expect(stableConf > 0.5f,
               "Stable harmonic tone confidence should be clearly positive");
        expect(chatterConf < stableConf,
               "Noise chatter confidence must be below stable-tone confidence");
    }

private:
    static constexpr float kLevelTolDb = 1.0e-3f;
};

// Static instance registers the suite with juce::UnitTestRunner.
static SustainTests s_sustainTestsInstance;

} // namespace
