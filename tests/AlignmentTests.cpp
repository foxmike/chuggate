// =============================================================================
// ChugGate - DI Key Alignment + Fractional Delay Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase E maths described in
// .abracapocus/CHUGGATE_SPEC.md section 13 (and the supporting FractionalDelay
// implementation of section 13.4):
//
//   FractionalDelay (section 13.4):
//   * Integer sample delay places an impulse at the expected output index.
//   * Fractional delay (e.g. 1.3 samples) produces interpolated energy
//     spanning neighboring taps via the four-point causal Lagrange kernel,
//     not pure zeros, and the Lagrange partition of unity sums to ~1.
//   * setDelayMs within the +/-25 ms operational range at fs=48000 is stable
//     and silence stays near zero after reset.
//
//   AlignmentAnalyzer (sections 13.2/13.3/13.5):
//   * A synthetic key onset train leading the audio onset train by a known
//     sample offset yields a recommended key delay near that offset (positive
//     key delay when DI leads audio) within a documented tolerance after the
//     calibration/analysis call.
//   * Correlation confidence is higher for clearly aligned repeated onsets
//     than for an uncorrelated noise-pair fixture.
//   * Calibration gate: a recommended delay is not committed until accept()
//     is called; before Ready the accept call is a no-op.
//
// All fixtures are deterministic: fixed sample rate (48000), fixed lengths,
// explicit tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Source/DSP/FractionalDelay.h"
#include "../Source/DSP/AlignmentAnalyzer.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

// Tolerances.
constexpr float kExactTol = 1.0e-4f;
constexpr float kLagrangeTol = 1.0e-3f;
constexpr float kDelayMsTol = 2.0f; // ms, includes decimation + fractional slack

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Cosine-started sine burst: sample 0 is full amplitude, giving a strong
// band-limited transient for the onset detector.
std::vector<float> makeBurst(int length, double frequency, float amplitude)
{
    std::vector<float> b(static_cast<std::size_t>(length));
    const double twoPi = 6.28318530717958647692;
    for (int j = 0; j < length; ++j)
        b[static_cast<std::size_t>(j)] =
            static_cast<float>(amplitude * std::cos(twoPi * frequency * j / kSampleRate));
    return b;
}

// Build a deterministic onset train: cosine-started bursts at the given
// positions, silence everywhere else.
std::vector<float> buildOnsetTrain(const std::vector<int> &onsetPositions,
                                   int totalLength, int burstLen,
                                   double frequency, float amplitude)
{
    std::vector<float> buf(static_cast<std::size_t>(totalLength), 0.0f);
    const auto burst = makeBurst(burstLen, frequency, amplitude);
    for (int pos : onsetPositions)
    {
        for (int j = 0; j < burstLen && pos + j < totalLength; ++j)
            buf[static_cast<std::size_t>(pos + j)] = burst[static_cast<std::size_t>(j)];
    }
    return buf;
}

// Deterministic LCG noise (no <random> dependency).
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

// Eight onsets spaced 0.5 s starting at sample 1000, total ~4.17 s.
std::vector<int> makeRegularOnsets()
{
    std::vector<int> positions;
    constexpr int spacing = 24000; // 0.5 s at 48 k
    constexpr int start = 1000;
    for (int k = 0; k < 8; ++k)
        positions.push_back(start + k * spacing);
    return positions;
}

// Run a full single-shot calibration on an analyzer prepared at kSampleRate.
// Returns the analyzer's recommended offset (ms) and confidence via out params.
void runCalibration(AlignmentAnalyzer &analyzer,
                    const std::vector<float> &key,
                    const std::vector<float> &audio)
{
    analyzer.prepare(kSampleRate, kBlockSize);
    analyzer.setSearchRangeMs(25.0f);
    analyzer.startAlign();
    analyzer.process(key.data(), audio.data(), static_cast<int>(key.size()));
    (void) analyzer.finishCalibrationChunk();
}

// ===========================================================================
// FractionalDelay suite
// ===========================================================================

class FractionalDelayTests final : public juce::UnitTest
{
public:
    FractionalDelayTests() : juce::UnitTest("ChugGate FractionalDelay Tests", "DSP") {}

    void runTest() override
    {
        beginTest("Integer delay: impulse appears at expected index");
        runIntegerDelay();

        beginTest("Fractional delay: Lagrange interpolation spans neighboring taps");
        runFractionalDelay();

        beginTest("setDelayMs within +/-25 ms: stable, silence stays near zero");
        runStability();
    }

private:
    void runIntegerDelay()
    {
        FractionalDelay fd;
        fd.prepare(kSampleRate, 25.0f);
        fd.setDelaySamples(5.0); // exactly five samples

        std::vector<float> in(40, 0.0f);
        in[0] = 1.0f; // unit impulse
        std::vector<float> out(40, 0.0f);
        fd.process(in.data(), out.data(), 40);

        // The impulse must appear exactly at index 5.
        expect(std::fabs(out[5] - 1.0f) < kExactTol,
               "Integer delay of 5 must place the impulse at index 5");
        for (int i = 0; i < 5; ++i)
            expect(std::fabs(out[static_cast<std::size_t>(i)]) < kExactTol,
                   "Output before the delay tap must be zero");
        for (int i = 6; i < 40; ++i)
            expect(std::fabs(out[static_cast<std::size_t>(i)]) < kExactTol,
                   "Output after the delay tap must be zero (no ringing)");
    }

    void runFractionalDelay()
    {
        FractionalDelay fd;
        fd.prepare(kSampleRate, 25.0f);
        fd.setDelaySamples(1.3); // fractional delay of 1.3 samples

        std::vector<float> in(32, 0.0f);
        in[0] = 1.0f;
        std::vector<float> out(32, 0.0f);
        fd.process(in.data(), out.data(), 32);

        // No output before the first delayed tap.
        expect(std::fabs(out[0]) < kExactTol,
               "Output at index 0 must be zero (no lookahead)");

        // Four-point Lagrange coefficients at t=0.3 (integerDelay=1):
        //   c0 = -((t-1)(t-2)(t-3))/6          ~= 0.53550
        //   c1 =  (t (t-2)(t-3))/2             ~= 0.68850
        //   c2 = -(t (t-1)(t-3))/2             ~= -0.28350
        //   c3 =  (t (t-1)(t-2))/6             ~= 0.05950
        // The impulse energy is distributed across taps 1..4 (not pure zeros),
        // and the partition of unity sums to ~1.0.
        expect(std::fabs(out[1] - 0.5355f) < kLagrangeTol,
               "Lagrange tap 0 weight at t=0.3 ~= 0.5355");
        expect(std::fabs(out[2] - 0.6885f) < kLagrangeTol,
               "Lagrange tap 1 weight at t=0.3 ~= 0.6885");
        expect(std::fabs(out[3] - (-0.2835f)) < kLagrangeTol,
               "Lagrange tap 2 weight at t=0.3 ~= -0.2835");
        expect(std::fabs(out[4] - 0.0595f) < kLagrangeTol,
               "Lagrange tap 3 weight at t=0.3 ~= 0.0595");

        // Energy spans multiple neighboring taps (interpolated, not pure zeros).
        int nonZeroTaps = 0;
        for (int i = 1; i <= 4; ++i)
            if (std::fabs(out[static_cast<std::size_t>(i)]) > 1.0e-4f)
                ++nonZeroTaps;
        expect(nonZeroTaps >= 3,
               "Fractional delay must spread energy across multiple taps");
        expect(std::fabs(out[2]) > std::fabs(out[4]),
               "Largest fractional contribution must be near the fractional index");

        // Lagrange four-point kernel partitions unity at DC.
        float sum = out[1] + out[2] + out[3] + out[4];
        expect(std::fabs(sum - 1.0f) < kLagrangeTol,
               "Lagrange weights must sum to ~1.0 (partition of unity)");

        // Pure zeros would make every tap except one vanish; confirm at least
        // one negative lobe (ringing) is present, which pure integer placement
        // could never produce.
        bool hasNegativeLobe = false;
        for (int i = 1; i <= 4; ++i)
            if (out[static_cast<std::size_t>(i)] < -1.0e-4f)
                hasNegativeLobe = true;
        expect(hasNegativeLobe,
               "Lagrange interpolation must produce a negative lobe (non-trivial)");
    }

    void runStability()
    {
        FractionalDelay fd;
        fd.prepare(kSampleRate, 25.0f);

        // +15 ms and -15 ms both within the +/-25 ms operational range.
        fd.setDelayMs(15.0f);
        expect(std::fabs(fd.getDelayMs() - 15.0f) < kExactTol,
               "+15 ms must round-trip within the operational range");
        fd.setDelayMs(-15.0f);
        expect(std::fabs(fd.getDelayMs() - (-15.0f)) < kExactTol,
               "-15 ms must round-trip within the operational range");

        // Reset then process silence: output must stay near zero.
        fd.reset();
        fd.setDelayMs(10.0f); // 480 samples
        std::vector<float> silence(2000, 0.0f);
        std::vector<float> out(2000, 0.0f);
        fd.process(silence.data(), out.data(), 2000);

        float maxAbs = 0.0f;
        for (float v : out)
            maxAbs = std::max(maxAbs, std::fabs(v));
        expect(maxAbs < 1.0e-6f,
               "Silence through a reset fractional delay must stay near zero");
    }
};

// ===========================================================================
// AlignmentAnalyzer suite
// ===========================================================================

class AlignmentTests final : public juce::UnitTest
{
public:
    AlignmentTests() : juce::UnitTest("ChugGate Alignment Tests", "DSP") {}

    void runTest() override
    {
        beginTest("Alignment: key-leads-audio offset yields recommended key delay");
        runKeyLeadsAudio();

        beginTest("Confidence: aligned onsets higher than uncorrelated noise pair");
        runConfidenceComparison();

        beginTest("Calibration gate: recommendation not committed until accept()");
        runAcceptGate();
    }

private:
    // ---- 13.2/13.3 Key-leads-audio offset --------------------------------

    void runKeyLeadsAudio()
    {
        // Key onsets lead the audio onsets by D = 240 samples (5.0 ms at 48 k).
        // Per section 13.3 the analyzer must recommend a positive key delay.
        constexpr int D = 240;
        constexpr float expectedDelayMs = 5.0f;

        const auto keyPositions = makeRegularOnsets();
        std::vector<int> audioPositions;
        audioPositions.reserve(keyPositions.size());
        for (int p : keyPositions)
            audioPositions.push_back(p + D);

        constexpr int totalLength = 200000;
        const auto key = buildOnsetTrain(keyPositions, totalLength, 1500, 200.0, 0.6f);
        const auto audio = buildOnsetTrain(audioPositions, totalLength, 1500, 200.0, 0.6f);

        AlignmentAnalyzer analyzer;
        runCalibration(analyzer, key, audio);

        expect(analyzer.getState() == AlignmentAnalyzer::CalibrationState::Ready,
               "Calibration must finish into Ready");
        expect(analyzer.hasRecommendation(),
               "A recommendation must be available after calibration");

        const float recMs = analyzer.getRecommendedDelayMs();
        juce::String msg;
        msg << "Recommended delay " << recMs << " ms, expected ~" << expectedDelayMs << " ms";
        expect(std::fabs(recMs - expectedDelayMs) < kDelayMsTol, msg);

        // Per section 13.3, key-leads-audio => delay key (positive offset).
        expect(recMs > 0.0f,
               "Key leading audio must recommend a positive key delay");
        expect(!analyzer.needsMainLookahead(),
               "Positive offset must not require main-path lookahead");

        const auto result = analyzer.getResult();
        expect(result.valid, "Result must be valid");
        expect(std::fabs(result.keyDelayMs - recMs) < kExactTol,
               "Result keyDelayMs must match the recommended offset");
        expect(result.estimateCount > 0,
               "At least one window estimate must contribute");
    }

    // ---- Confidence comparison ------------------------------------------

    void runConfidenceComparison()
    {
        // Aligned fixture (same as the offset test).
        constexpr int D = 240;
        const auto keyPositions = makeRegularOnsets();
        std::vector<int> audioPositions;
        for (int p : keyPositions)
            audioPositions.push_back(p + D);
        constexpr int totalLength = 200000;
        const auto keyAligned = buildOnsetTrain(keyPositions, totalLength, 1500, 200.0, 0.6f);
        const auto audioAligned = buildOnsetTrain(audioPositions, totalLength, 1500, 200.0, 0.6f);

        AlignmentAnalyzer alignedAnalyzer;
        runCalibration(alignedAnalyzer, keyAligned, audioAligned);
        const float alignedConfidence = alignedAnalyzer.getConfidence();
        expect(alignedAnalyzer.hasRecommendation(),
               "Aligned fixture must produce a recommendation");
        expect(alignedConfidence > 0.5f,
               "Clearly aligned onsets must yield high confidence");

        // Uncorrelated fixture: two independent noise streams (no shared onset
        // structure). The cross-correlation peak must be much weaker.
        const auto keyNoise = makeNoise(totalLength, 0.5f, 0xABCDEF01u);
        const auto audioNoise = makeNoise(totalLength, 0.5f, 0x13579BDFu);

        AlignmentAnalyzer noiseAnalyzer;
        runCalibration(noiseAnalyzer, keyNoise, audioNoise);
        const float noiseConfidence = noiseAnalyzer.getConfidence();

        expect(alignedConfidence > noiseConfidence,
               "Aligned onset confidence must exceed uncorrelated noise confidence");
        expect(alignedConfidence > 0.5f && noiseConfidence < 0.5f,
               "Aligned confidence must be high and noise confidence low");
    }

    // ---- 13.5 Calibration gate (recommend vs accepted) -------------------

    void runAcceptGate()
    {
        // Before any calibration, accept() must be a no-op.
        AlignmentAnalyzer fresh;
        fresh.prepare(kSampleRate, kBlockSize);
        fresh.startAlign();
        expect(!fresh.accept(),
               "accept() must fail while still Capturing (no recommendation yet)");
        expect(!fresh.getAcceptedResult().valid,
               "Accepted result must remain invalid before any accept");

        // Run a real calibration.
        constexpr int D = 240;
        const auto keyPositions = makeRegularOnsets();
        std::vector<int> audioPositions;
        for (int p : keyPositions)
            audioPositions.push_back(p + D);
        constexpr int totalLength = 200000;
        const auto key = buildOnsetTrain(keyPositions, totalLength, 1500, 200.0, 0.6f);
        const auto audio = buildOnsetTrain(audioPositions, totalLength, 1500, 200.0, 0.6f);

        AlignmentAnalyzer analyzer;
        runCalibration(analyzer, key, audio);
        expect(analyzer.getState() == AlignmentAnalyzer::CalibrationState::Ready,
               "Calibration must finish into Ready");
        expect(analyzer.hasRecommendation(),
               "A recommendation must be available");

        // Recommendation exists but must NOT be committed yet.
        expect(!analyzer.getAcceptedResult().valid,
               "Accepted result must remain invalid until accept() is called");

        const float recommendedMs = analyzer.getRecommendedDelayMs();

        // Commit.
        expect(analyzer.accept(),
               "accept() must succeed in Ready state");
        expect(analyzer.getState() == AlignmentAnalyzer::CalibrationState::Accepted,
               "State must become Accepted after accept()");
        const auto accepted = analyzer.getAcceptedResult();
        expect(accepted.valid, "Accepted result must be valid after accept()");
        expect(std::fabs(accepted.offsetMs - recommendedMs) < kExactTol,
               "Accepted offset must match the recommended offset");
        expect(std::fabs(accepted.keyDelayMs - recommendedMs) < kExactTol,
               "Accepted keyDelayMs must match the recommended key delay");

        // A second accept must fail (no longer Ready).
        expect(!analyzer.accept(),
               "accept() must fail once already Accepted");
    }
};

// Static instances register the suites with juce::UnitTestRunner.
static FractionalDelayTests s_fractionalDelayTestsInstance;
static AlignmentTests s_alignmentTestsInstance;

} // namespace
