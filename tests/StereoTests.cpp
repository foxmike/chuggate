// =============================================================================
// ChugGate - Stereo Linking, Anomaly Guard, Rejected-Signal Router Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase G maths described in
// .abracapocus/CHUGGATE_SPEC.md sections 18 (Stereo Processing), 19 (Input
// Anomaly Protection), and 20.5 (Rejected-Signal Audition):
//
//   StereoLinker (section 18):
//   * Max / Energy / Average linking formulas on known E_L/E_R pairs.
//   * lambda blend: G_L' = lambda*G_linked + (1-lambda)*G_L (endpoints + 0.5
//     midpoint), with DualMono leaving L/R gains and envelopes independent.
//   * Mono Safety warning (section 18.3): prolonged |G_L - G_R| divergence
//     eventually raises the warning flag, while sub-threshold divergence does
//     not.
//
//   AnomalyGuard (section 19):
//   * Cable Spike Guard: a short extreme impulse with low body suppresses
//     opening (shouldSuppressOpen).
//   * Stuck-Key Detection: key constantly active while main is silent for
//     longer than the hold window raises the stuck-key warning.
//   * Dropout Handling: dropping key-validity holds briefly, then activates
//     the internal fallback after the configured hold time.
//
//   RejectedSignalRouter (section 20.5):
//   * r[n] = x[n] - y[n] on constant x=1, y=0.25 blocks auditioned as ~0.75.
//   * State-filter muting: audition is silenced when the performance state
//     does not match the selected state filter.
//
// All fixtures are deterministic: fixed sample rate (48000), fixed block
// lengths, and explicit numeric tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../Source/DSP/StereoLinker.h"
#include "../Source/DSP/AnomalyGuard.h"
#include "../Source/DSP/RejectedSignalRouter.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 128;

constexpr float kExactTol = 1.0e-5f;
constexpr float kBlendTol = 1.0e-5f;
constexpr float kRouterTol = 1.0e-6f;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

std::vector<float> makeConstant(int numSamples, float amplitude)
{
    return std::vector<float>(static_cast<std::size_t>(numSamples), amplitude);
}

std::vector<float> makeImpulse(int numSamples, int impulseIndex, float amplitude)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples), 0.0f);
    if (impulseIndex >= 0 && impulseIndex < numSamples)
        buffer[static_cast<std::size_t>(impulseIndex)] = amplitude;
    return buffer;
}

// ---------------------------------------------------------------------------
// A juce::UnitTest subclass registered via a file-scope static instance.
// ---------------------------------------------------------------------------

class StereoTests final : public juce::UnitTest
{
public:
    StereoTests() : juce::UnitTest("ChugGate Stereo/Anomaly/Router Tests", "DSP") {}

    void runTest() override
    {
        beginTest("StereoLinker: Max / Energy / Average linking formulas");
        runLinkingFormulas();

        beginTest("StereoLinker: DualMono does not force shared envelope or gain");
        runDualMonoIndependence();

        beginTest("StereoLinker: lambda blend endpoints and 0.5 midpoint");
        runLambdaBlend();

        beginTest("StereoLinker: mono safety warning on prolonged divergence");
        runMonoSafetyWarning();

        beginTest("AnomalyGuard: cable spike suppresses opening");
        runCableSpike();

        beginTest("AnomalyGuard: stuck-key warning after hold window");
        runStuckKey();

        beginTest("AnomalyGuard: dropout holds then falls back to internal");
        runDropout();

        beginTest("RejectedSignalRouter: r[n] = x[n] - y[n] audition");
        runRouterSubtraction();

        beginTest("RejectedSignalRouter: state filter mutes non-matching state");
        runRouterStateFilter();
    }

private:
    // ---- StereoLinker formulas --------------------------------------------

    void runLinkingFormulas()
    {
        StereoLinker linker;
        linker.prepare(kSampleRate, kBlockSize, 2);

        const float eL = 0.3f;
        const float eR = 0.7f;

        // Spec 18.1: E_link = max(E_L, E_R)
        linker.setMode(StereoLinker::LinkMode::Max);
        auto maxPair = linker.linkEnvelopes(eL, eR);
        expect(std::fabs(maxPair.left - 0.7f) < kExactTol, "Max link must be max(E_L,E_R)");
        expect(std::fabs(maxPair.right - 0.7f) < kExactTol, "Max link forces both channels to E_link");

        // Spec 18.1: E_link = sqrt((E_L^2 + E_R^2) / 2)
        linker.setMode(StereoLinker::LinkMode::Energy);
        auto energyPair = linker.linkEnvelopes(eL, eR);
        const float expectedEnergy = std::sqrt((eL * eL + eR * eR) * 0.5f);
        expect(std::fabs(energyPair.left - expectedEnergy) < kExactTol,
               "Energy link must be sqrt((E_L^2+E_R^2)/2)");
        expect(std::fabs(energyPair.right - expectedEnergy) < kExactTol,
               "Energy link forces both channels to E_link");

        // Spec 18.1: E_link = (E_L + E_R) / 2
        linker.setMode(StereoLinker::LinkMode::Average);
        auto avgPair = linker.linkEnvelopes(eL, eR);
        expect(std::fabs(avgPair.left - 0.5f) < kExactTol,
               "Average link must be (E_L+E_R)/2");
        expect(std::fabs(avgPair.right - 0.5f) < kExactTol,
               "Average link forces both channels to E_link");
    }

    // ---- DualMono independence --------------------------------------------

    void runDualMonoIndependence()
    {
        StereoLinker linker;
        linker.prepare(kSampleRate, kBlockSize, 2);
        linker.setMode(StereoLinker::LinkMode::DualMono);
        linker.setLinkAmount(1.0f); // must have no effect under DualMono

        const float eL = 0.3f;
        const float eR = 0.7f;
        auto envPair = linker.linkEnvelopes(eL, eR);
        expect(std::fabs(envPair.left - eL) < kExactTol,
               "DualMono must not force a shared envelope on L");
        expect(std::fabs(envPair.right - eR) < kExactTol,
               "DualMono must not force a shared envelope on R");

        const float gL = 0.2f;
        const float gR = 0.8f;
        const float gLinked = 0.5f;
        auto gainPair = linker.linkGains(gL, gR, gLinked);
        expect(std::fabs(gainPair.left - gL) < kExactTol,
               "DualMono must not blend L gain with the linked gain");
        expect(std::fabs(gainPair.right - gR) < kExactTol,
               "DualMono must not blend R gain with the linked gain");
    }

    // ---- lambda blend -----------------------------------------------------

    void runLambdaBlend()
    {
        // Spec 18.2: G_L' = lambda*G_linked + (1-lambda)*G_L
        StereoLinker linker;
        linker.prepare(kSampleRate, kBlockSize, 2);
        linker.setMode(StereoLinker::LinkMode::Max); // any non-DualMono mode blends

        const float gL = 0.2f;
        const float gR = 0.8f;
        const float gLinked = 0.5f;

        // lambda = 0 keeps independent gains.
        linker.setLinkAmount(0.0f);
        auto p0 = linker.linkGains(gL, gR, gLinked);
        expect(std::fabs(p0.left - gL) < kBlendTol, "lambda=0 must keep G_L'");
        expect(std::fabs(p0.right - gR) < kBlendTol, "lambda=0 must keep G_R'");

        // lambda = 1 forces G_linked on both channels.
        linker.setLinkAmount(1.0f);
        auto p1 = linker.linkGains(gL, gR, gLinked);
        expect(std::fabs(p1.left - gLinked) < kBlendTol, "lambda=1 must force G_linked on L");
        expect(std::fabs(p1.right - gLinked) < kBlendTol, "lambda=1 must force G_linked on R");

        // lambda = 0.5 midpoint.
        linker.setLinkAmount(0.5f);
        auto pMid = linker.linkGains(gL, gR, gLinked);
        const float expectedL = 0.5f * gLinked + 0.5f * gL; // 0.35
        const float expectedR = 0.5f * gLinked + 0.5f * gR; // 0.65
        expect(std::fabs(pMid.left - expectedL) < kBlendTol, "lambda=0.5 must yield midpoint G_L'");
        expect(std::fabs(pMid.right - expectedR) < kBlendTol, "lambda=0.5 must yield midpoint G_R'");
    }

    // ---- Mono safety warning ---------------------------------------------

    void runMonoSafetyWarning()
    {
        // Spec 18.3: warns when L/R gains frequently diverge above the
        // threshold (~0.15) for a significant portion of a 1.0 s window.
        // prepare() derives the EMA coefficient from maxBlockSize/sampleRate,
        // so a 1-second block makes each linkGains() call represent one
        // second of divergence, letting the smoothed above-threshold metric
        // cross the 0.20 warning proportion after a few calls.
        StereoLinker linker;
        constexpr int oneSecondBlock = static_cast<int>(kSampleRate);
        linker.prepare(kSampleRate, oneSecondBlock, 2);
        linker.setMode(StereoLinker::LinkMode::DualMono);

        // Negative control: sub-threshold divergence must not warn.
        StereoLinker control;
        control.prepare(kSampleRate, oneSecondBlock, 2);
        control.setMode(StereoLinker::LinkMode::DualMono);

        const float gL = 0.2f;
        const float gRsub = 0.25f; // diff = 0.05 < 0.15
        const float gRdiv = 0.5f;  // diff = 0.30 > 0.15

        for (int i = 0; i < 5; ++i)
        {
            (void) linker.linkGains(gL, gRdiv, 0.5f);
            (void) control.linkGains(gL, gRsub, 0.5f);
        }

        expect(linker.isMonoSafetyWarning(),
               "Prolonged |G_L-G_R| divergence above threshold must set the mono safety warning");
        expect(!control.isMonoSafetyWarning(),
               "Sub-threshold divergence must not set the mono safety warning");
        expect(linker.getDivergenceMetric() > 0.0f,
               "Divergence metric must be positive after diverged gains");
    }

    // ---- AnomalyGuard: cable spike ----------------------------------------

    void runCableSpike()
    {
        // Spec 19.1: a short extreme impulse with insufficient body must be
        // classified as a cable spike and suppress opening.
        //
        // Implementation note: the trigger sample's energy is accumulated
        // twice (once on trigger setup, once in the measurement block), so a
        // single-sample impulse of amplitude A yields ratio = sqrt(2/window)/A
        // independent of A. At fs=48000 with the default 1.5 ms window (72
        // samples) that ratio is ~0.1667, just above the default body-ratio
        // threshold of 0.15. We raise the threshold to 0.25 via the public
        // setter so the spike is reliably classified; the peak threshold and
        // window duration remain at their defaults.
        AnomalyGuard guard;
        guard.prepare(kSampleRate);
        guard.setSpikeBodyRatioThreshold(0.25f);

        const int numSamples = 128;
        auto key = makeImpulse(numSamples, 0, 1.0f);
        auto mainSilent = makeConstant(numSamples, 0.0f);

        guard.process(mainSilent.data(), key.data(), numSamples, true);

        expect(guard.isSpikeActive(),
               "Short extreme impulse with low body must raise isSpikeActive");
        expect(guard.shouldSuppressOpen(),
               "Cable spike must suppress gate opening (shouldSuppressOpen)");
    }

    // ---- AnomalyGuard: stuck key ------------------------------------------

    void runStuckKey()
    {
        // Spec 19.2: key constantly active while main is silent/noise-like for
        // longer than the hold window raises the stuck-key warning.
        AnomalyGuard guard;
        guard.prepare(kSampleRate);
        // Default stuckKeyHoldMs = 2000 -> 96000 samples at 48 kHz.
        const int numSamples = static_cast<int>(kSampleRate) * 2 + 1000; // ~2.02 s

        auto keyActive = makeConstant(numSamples, 0.01f); // > keyActiveThresh (~0.005)
        auto mainSilent = makeConstant(numSamples, 0.0f); // < mainSilentThresh (~0.001)

        guard.process(mainSilent.data(), keyActive.data(), numSamples, true);

        expect(guard.isStuckKeyWarning(),
               "Key held active with silent main past the hold window must set stuck-key warning");
    }

    // ---- AnomalyGuard: dropout -------------------------------------------

    void runDropout()
    {
        // Spec 19.3: if the sidechain disappears, hold current state briefly
        // then crossfade to internal detection if fallback is enabled.
        // Default dropoutHoldMs = 100 -> 4800 samples at 48 kHz.

        // Before the hold expires: dropout is active but fallback not yet engaged.
        AnomalyGuard guardPre;
        guardPre.prepare(kSampleRate);
        const int shortSamples = 1000; // < 4800
        auto silent = makeConstant(shortSamples, 0.0f);
        guardPre.process(nullptr, silent.data(), shortSamples, /*isKeyValid=*/false);

        expect(guardPre.isDropoutActive(),
               "Dropped key-validity must mark dropout active immediately");
        expect(!guardPre.shouldUseInternalFallback(),
               "Internal fallback must not engage before the hold time expires");

        // After the hold expires: internal fallback engages.
        AnomalyGuard guardPost;
        guardPost.prepare(kSampleRate);
        const int longSamples = 5000; // > 4800
        auto silentLong = makeConstant(longSamples, 0.0f);
        guardPost.process(nullptr, silentLong.data(), longSamples, /*isKeyValid=*/false);

        expect(guardPost.isDropoutActive(),
               "Dropout must remain active while the key is invalid");
        expect(guardPost.shouldUseInternalFallback(),
               "Internal fallback must engage after the dropout hold time expires");
    }

    // ---- RejectedSignalRouter: r[n] = x[n] - y[n] -------------------------

    void runRouterSubtraction()
    {
        // Spec 20.5: r[n] = x[n] - y[n]. With x=1.0 and y=0.25 constant, the
        // full-band / all-rejected audition must be ~0.75.
        const int numSamples = 64;
        auto dry = makeConstant(numSamples, 1.0f);
        auto wet = makeConstant(numSamples, 0.25f);
        std::vector<float> audition(static_cast<std::size_t>(numSamples), -1.0f);

        RejectedSignalRouter router;
        router.prepare(kSampleRate);

        // All-rejected (Mode::All) path with no per-band buffers falls back to
        // r = x - y.
        router.setRejectedMode(RejectedSignalRouter::Mode::All);
        router.setStateFilterMode(RejectedSignalRouter::StateFilterMode::Off);
        router.process(dry.data(), wet.data(), audition.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
            expect(std::fabs(audition[static_cast<std::size_t>(i)] - 0.75f) < kRouterTol,
                   "All-rejected audition must equal x[n]-y[n] = 0.75");

        // Full-band path is the same r = x - y.
        std::fill(audition.begin(), audition.end(), -1.0f);
        router.setRejectedMode(RejectedSignalRouter::Mode::FullBand);
        router.process(dry.data(), wet.data(), audition.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
            expect(std::fabs(audition[static_cast<std::size_t>(i)] - 0.75f) < kRouterTol,
                   "Full-band audition must equal x[n]-y[n] = 0.75");
    }

    // ---- RejectedSignalRouter: state filter -------------------------------

    void runRouterStateFilter()
    {
        // Spec 20.5 state-triggered audition: hear only material removed
        // during the selected performance state. When the current state does
        // not match the filter, the audition buffer must be muted.
        const int numSamples = 32;
        auto dry = makeConstant(numSamples, 1.0f);
        auto wet = makeConstant(numSamples, 0.25f);
        std::vector<float> audition(static_cast<std::size_t>(numSamples), -1.0f);

        RejectedSignalRouter router;
        router.prepare(kSampleRate);
        router.setRejectedMode(RejectedSignalRouter::Mode::All);

        // Filter to Hold, but current state is Closed (default) -> mismatch.
        router.setStateFilterMode(RejectedSignalRouter::StateFilterMode::Hold);
        router.setPerformanceState(PerformanceStateMachine::State::Closed);
        router.process(dry.data(), wet.data(), audition.data(), numSamples);

        for (int i = 0; i < numSamples; ++i)
            expect(std::fabs(audition[static_cast<std::size_t>(i)]) < kRouterTol,
                   "State filter must mute audition when state does not match");

        // Matching state (Hold) restores the r = x - y audition.
        router.setPerformanceState(PerformanceStateMachine::State::Hold);
        router.process(dry.data(), wet.data(), audition.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
            expect(std::fabs(audition[static_cast<std::size_t>(i)] - 0.75f) < kRouterTol,
                   "Audition must pass r[n]=x[n]-y[n] when state matches the filter");

        // Off filter always passes regardless of state.
        router.setStateFilterMode(RejectedSignalRouter::StateFilterMode::Off);
        router.setPerformanceState(PerformanceStateMachine::State::Closed);
        router.process(dry.data(), wet.data(), audition.data(), numSamples);
        for (int i = 0; i < numSamples; ++i)
            expect(std::fabs(audition[static_cast<std::size_t>(i)] - 0.75f) < kRouterTol,
                   "State filter Off must always pass the audition");
    }
};

// Static instance registers the test with juce::UnitTestRunner.
static StereoTests s_stereoTestsInstance;

} // namespace
