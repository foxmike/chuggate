// =============================================================================
// ChugGate - Pick Attack Guard Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase D AttackGuard maths described
// in .abracapocus/CHUGGATE_SPEC.md section 8:
//   * 8.2 Band-limited transient detection: T[n] = max(|d[n]|, alpha_t*T[n-1])
//     and the clamped transient strength G_t = clamp((T-T0)/(T1-T0), 0, 1).
//   * 8.3 False-trigger protection: a valid attack requires transient evidence
//     AND minimum body energy within a short confirmation window. Single-sample
//     spikes / digital clicks must be rejected.
//   * 8.4 Open-assist polarity: O[n] = L[n] + G_t raises the opening score
//     (positive threshold reduction) only while a validated attack assist is
//     active, and stays identity/disabled when the guard is disabled.
//
// All fixtures are deterministic: fixed sample rate (48000), fixed block
// lengths, explicit tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Source/DSP/AttackGuard.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

// Tolerances.
constexpr float kExactTol = 1.0e-4f;
constexpr float kLevelTolDb = 1.0e-3f;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Coherent sine with selectable phase so an onset can be made band-limited or
// full-amplitude depending on the test.
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

// Deterministic low-amplitude noise floor (LCG, no <random> dependency).
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

// Raised-cosine band-limited rising edge from 0 to amplitude over rampSamples,
// held at the final DC level afterwards. This is the section 8.2 "short rising
// band-limited edge" fixture.
std::vector<float> makeRisingEdge(int prefixSamples, int rampSamples, int holdSamples,
                                  float amplitude)
{
    std::vector<float> buffer;
    buffer.reserve(static_cast<std::size_t>(prefixSamples + rampSamples + holdSamples));
    buffer.insert(buffer.end(), static_cast<std::size_t>(prefixSamples), 0.0f);
    const double pi = 3.14159265358979323846;
    for (int i = 0; i < rampSamples; ++i)
    {
        const double a = 0.5 * (1.0 - std::cos(pi * static_cast<double>(i) /
                                               static_cast<double>(rampSamples)));
        buffer.push_back(static_cast<float>(amplitude * a));
    }
    buffer.insert(buffer.end(), static_cast<std::size_t>(holdSamples), amplitude);
    return buffer;
}

// Pick-like toneburst: silence prefix, then a cosine-started sine (full
// amplitude discontinuity from silence -> strong transient) sustained long
// enough to fill the confirmation window with AC body energy.
std::vector<float> makePickBurst(int prefixSamples, int sustainSamples,
                                 double frequency, float amplitude)
{
    std::vector<float> buffer;
    buffer.reserve(static_cast<std::size_t>(prefixSamples + sustainSamples));
    buffer.insert(buffer.end(), static_cast<std::size_t>(prefixSamples), 0.0f);
    // Phase pi/2 -> cosine: first sustained sample is full amplitude.
    const auto sine = makeSine(sustainSamples, kSampleRate, frequency,
                               static_cast<double>(amplitude), 1.57079632679489661923);
    buffer.insert(buffer.end(), sine.begin(), sine.end());
    return buffer;
}

// Constant detector-level companion buffer.
std::vector<float> makeConstantDetector(int numSamples, float levelDb)
{
    return std::vector<float>(static_cast<std::size_t>(numSamples), levelDb);
}

// Run a mono key + detector-level pair through the guard and capture outputs.
std::vector<AttackGuard::Output>
runGuard(AttackGuard &guard, const std::vector<float> &monoKey,
         const std::vector<float> &detectorLevelDb)
{
    const int n = static_cast<int>(monoKey.size());
    std::vector<AttackGuard::Output> out(static_cast<std::size_t>(n));
    guard.process(monoKey.data(), detectorLevelDb.data(), out.data(), n);
    return out;
}

int firstValidAttackIndex(const std::vector<AttackGuard::Output> &out)
{
    for (std::size_t i = 0; i < out.size(); ++i)
        if (out[i].validAttack)
            return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

class AttackGuardTests final : public juce::UnitTest
{
public:
    AttackGuardTests() : juce::UnitTest("ChugGate AttackGuard Tests", "DSP") {}

    void runTest() override
    {
        beginTest("Transient response: band-limited edge raises T[n] above noise floor");
        runTransientResponse();

        beginTest("Open assist: valid transient + body energy yields non-zero "
                  "threshold reduction that aids opening");
        runOpenAssistDirection();

        beginTest("False-trigger rejection: isolated spike without body energy "
                  "does not set validAttack");
        runFalseTriggerRejection();

        beginTest("Valid attack: transient + body-energy toneburst sets validAttack");
        runValidAttack();

        beginTest("Disabled path: assist / threshold reduction stays identity");
        runDisabledPath();
    }

private:
    // ---- 8.2 Transient response -------------------------------------------

    void runTransientResponse()
    {
        AttackGuard guard;
        guard.prepare(kSampleRate, kBlockSize);
        // Small confirmation window keeps the fixture compact; transient
        // detection itself is independent of it.
        guard.setConfirmationWindowMs(1.0);
        guard.setEnabled(true);

        // Quiet deterministic noise floor (well below the onset floor T0).
        auto noise = makeNoise(300, 8.0e-4f, 0x1234u);
        // Band-limited raised-cosine edge into a held level. A 10-sample ramp
        // keeps the edge spectral content above the 1 kHz emphasis cutoff so
        // the HP path passes it near unity while remaining band-limited.
        auto edge = makeRisingEdge(0, 10, 120, 0.4f);

        std::vector<float> mono;
        mono.insert(mono.end(), noise.begin(), noise.end());
        mono.insert(mono.end(), edge.begin(), edge.end());
        const auto det = makeConstantDetector(static_cast<int>(mono.size()), -100.0f);

        const auto out = runGuard(guard, mono, det);

        float noiseMaxStrength = 0.0f;
        for (std::size_t i = 0; i < 300u; ++i)
            noiseMaxStrength = std::max(noiseMaxStrength, out[i].transientStrength);

        float edgeMaxStrength = 0.0f;
        for (std::size_t i = 300u; i < out.size(); ++i)
            edgeMaxStrength = std::max(edgeMaxStrength, out[i].transientStrength);

        // The noise floor sits below T0, so its strength must be ~0.
        expect(noiseMaxStrength < kExactTol,
               "Quiet noise floor must not register transient strength");
        // The band-limited edge must lift T[n] / strength well clear of the
        // noise floor (the clamped strength saturates near 1.0 on this edge).
        expect(edgeMaxStrength > 0.5f,
               "Band-limited rising edge must raise transient strength above floor");
        expect(edgeMaxStrength > noiseMaxStrength + 0.5f,
               "Edge transient strength must exceed noise floor by a clear margin");
    }

    // ---- 8.4 Open-assist polarity -----------------------------------------

    void runOpenAssistDirection()
    {
        AttackGuard guard;
        guard.prepare(kSampleRate, kBlockSize);
        guard.setConfirmationWindowMs(1.0); // ~48 samples
        guard.setEnabled(true);
        // Defaults: amount=0.5, sensitivity=0.5, maxReduction=12 dB.

        const int prefix = 50;
        const int sustain = 150;
        auto mono = makePickBurst(prefix, sustain, 220.0, 0.3f);
        constexpr float kDetectorDb = -30.0f;
        const auto det = makeConstantDetector(static_cast<int>(mono.size()), kDetectorDb);

        const auto out = runGuard(guard, mono, det);

        const int attackIdx = firstValidAttackIndex(out);
        expect(attackIdx >= 0, "Valid pick fixture must produce a validAttack event");

        if (attackIdx >= 0)
        {
            const auto &a = out[static_cast<std::size_t>(attackIdx)];
            // Section 8.2/8.4: threshold reduction is a positive dB delta that
            // is subtracted from the opening threshold (aids opening).
            expect(a.thresholdReductionDb > 0.0f,
                   "Valid attack must yield positive threshold reduction");
            expect(a.thresholdReductionDb <= 12.0f + kLevelTolDb,
                   "Threshold reduction must not exceed configured maximum");
            // openAssistScore = L[n] + reduction must exceed the bare detector
            // level, i.e. the assist raises the opening score.
            expect(a.openAssistScore > kDetectorDb,
                   "Open-assist score must exceed detector level (aids opening)");
        }

        // The assist must remain active for several samples after confirmation
        // while T[n] is still decaying.
        float maxReduction = 0.0f;
        for (const auto &o : out)
            maxReduction = std::max(maxReduction, o.thresholdReductionDb);
        expect(maxReduction > 1.0f,
               "Sustained assist must produce non-trivial threshold reduction");
    }

    // ---- 8.3 False-trigger rejection --------------------------------------

    void runFalseTriggerRejection()
    {
        AttackGuard guard;
        guard.prepare(kSampleRate, kBlockSize);
        guard.setConfirmationWindowMs(1.0);
        guard.setEnabled(true);

        // Silence prefix, a single full-scale sample, then silence. This is
        // the canonical cable-click / single-sample spike fixture.
        std::vector<float> mono(static_cast<std::size_t>(300), 0.0f);
        mono[50] = 1.0f; // isolated spike
        const auto det = makeConstantDetector(static_cast<int>(mono.size()), -30.0f);

        const auto out = runGuard(guard, mono, det);

        bool anyValid = false;
        bool anyReduction = false;
        for (const auto &o : out)
        {
            anyValid = anyValid || o.validAttack;
            anyReduction = anyReduction || (o.thresholdReductionDb > 0.0f);
        }

        expect(! anyValid,
               "Isolated spike without body energy must not set validAttack");
        expect(! anyReduction,
               "Isolated spike must not produce any threshold reduction / assist");
    }

    // ---- 8.3 Valid attack -------------------------------------------------

    void runValidAttack()
    {
        AttackGuard guard;
        guard.prepare(kSampleRate, kBlockSize);
        guard.setConfirmationWindowMs(1.0);
        guard.setEnabled(true);

        auto mono = makePickBurst(50, 150, 220.0, 0.3f);
        const auto det = makeConstantDetector(static_cast<int>(mono.size()), -30.0f);

        const auto out = runGuard(guard, mono, det);

        const int attackIdx = firstValidAttackIndex(out);
        expect(attackIdx >= 0, "Transient + body-energy toneburst must set validAttack");
        // The confirmation window is ~48 samples after the onset at index 50.
        expect(attackIdx >= 50 && attackIdx <= 110,
               "validAttack must fire inside the confirmation window after the onset");
    }

    // ---- Disabled path ----------------------------------------------------

    void runDisabledPath()
    {
        AttackGuard guard;
        guard.prepare(kSampleRate, kBlockSize);
        guard.setConfirmationWindowMs(1.0);
        guard.setEnabled(false); // disabled

        auto mono = makePickBurst(50, 150, 220.0, 0.3f);
        constexpr float kDetectorDb = -30.0f;
        const auto det = makeConstantDetector(static_cast<int>(mono.size()), kDetectorDb);

        const auto out = runGuard(guard, mono, det);

        bool anyValid = false;
        for (const auto &o : out)
        {
            anyValid = anyValid || o.validAttack;
            expect(std::fabs(o.thresholdReductionDb) < kExactTol,
                   "Disabled guard must keep threshold reduction at zero");
            // openAssistScore is initialised to the detector level when the
            // assist is inactive (identity passthrough).
            expect(std::fabs(o.openAssistScore - kDetectorDb) < kLevelTolDb,
                   "Disabled guard must leave openAssistScore == detector level");
        }
        expect(! anyValid, "Disabled guard must never report validAttack");
    }
};

// Static instance registers the suite with juce::UnitTestRunner.
static AttackGuardTests s_attackGuardTestsInstance;

} // namespace
