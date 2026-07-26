// =============================================================================
// ChugGate - Riff Cadence Engine + Noise Learn Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase E maths described in
// .abracapocus/CHUGGATE_SPEC.md sections 11 and 12:
//
//   CadenceTracker (section 11):
//   * 11.2 Onset interval estimate I = median of recent deltas, with outlier
//     rejection. A regularly spaced synthetic onset train yields I near the
//     true spacing, and a single outlier interval must not pull the robust
//     median away from the regular spacing.
//   * 11.3 Release constraint tau_release_max = c*I and
//     tau_release_eff = min(tau_manual, tau_release_max). Verified for
//     tau_manual both above and below the cap.
//   * 11.4 Mode Off leaves the manual release unconstrained and reports a
//     zero/inactive constraint.
//   * Insufficient history: isEstimateValid is false until the minimum onset
//     count has been collected.
//
//   NoiseFloorEstimator (section 12):
//   * 12.2 N_b = percentile_p({L_b[t]}), T_b = N_b + M_b. Idle noise capture
//     produces a floor below a louder tone; the percentile floor rises when
//     the observation buffer is elevated.
//   * 12.1 applySuggestions does not run until explicitly called: hasSuggestions
//     is only true after learn, getters are populated, and applied suggestions
//     remain default until accept.
//   * 12.3 Slow adaptation is rejected when the gate is not confidently closed
//     or the transient flag is set; it only updates the model downward while
//     confidently closed.
//
// NoiseFloorEstimator.cpp is compiled directly into this translation unit (the
// ChugGateTests target links CadenceTracker.cpp but not NoiseFloorEstimator.cpp;
// including the implementation here avoids any CMake edits and produces no
// duplicate symbols in the test executable).
//
// All fixtures are deterministic: fixed sample rate (48000), fixed block
// lengths, explicit tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Source/DSP/CadenceTracker.h"
#include "../Source/DSP/NoiseFloorEstimator.h"
#include "../Source/DSP/NoiseFloorEstimator.cpp"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

// Tolerances.
constexpr float kExactTol = 1.0e-4f;
constexpr float kIntervalTol = 5.0e-4f; // 0.5 ms
constexpr float kMsTol = 1.0e-3f;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// Feed a constant log-energy level (dB) into a NoiseFloorEstimator for a given
// total duration in samples, in fixed blocks. Returns once the estimator has
// transitioned out of Capturing (i.e. capture finished) or all samples have
// been consumed.
void feedConstantLevel(NoiseFloorEstimator &est, float levelDb, int totalSamples,
                       int blockSize = kBlockSize)
{
    std::vector<float> level(static_cast<std::size_t>(blockSize), levelDb);
    int remaining = totalSamples;
    while (remaining > 0)
    {
        const int n = std::min(blockSize, remaining);
        est.observe(level.data(), 1, n);
        remaining -= n;
        if (!est.isCapturing())
            break;
    }
}

// Feed a constant level with explicit per-block process flags.
void feedConstantLevelFlags(NoiseFloorEstimator &est, float levelDb,
                            int totalSamples, int blockSize,
                            const NoiseFloorEstimator::ProcessFlags &flags)
{
    std::vector<float> level(static_cast<std::size_t>(blockSize), levelDb);
    int remaining = totalSamples;
    while (remaining > 0)
    {
        const int n = std::min(blockSize, remaining);
        est.observe(level.data(), 1, n, flags);
        remaining -= n;
    }
}

// ===========================================================================
// CadenceTracker suite
// ===========================================================================

class CadenceTests final : public juce::UnitTest
{
public:
    CadenceTests() : juce::UnitTest("ChugGate Cadence Tests", "DSP") {}

    void runTest() override
    {
        beginTest("Interval median: regular onsets yield I near true spacing");
        runIntervalMedianRegular();

        beginTest("Interval median: single outlier interval does not pull robust median");
        runIntervalMedianOutlier();

        beginTest("Release constraint: tau_release_eff = min(tau_manual, c*I)");
        runReleaseConstraint();

        beginTest("Mode Off: manual release unconstrained, constraint inactive");
        runModeOff();

        beginTest("Insufficient history: isEstimateValid false until minimum onsets");
        runInsufficientHistory();
    }

private:
    // ---- 11.2 Interval median --------------------------------------------

    void runIntervalMedianRegular()
    {
        CadenceTracker tracker;
        tracker.prepare(kSampleRate);
        tracker.setMode(CadenceTracker::Mode::Audio);

        // True spacing 0.25 s (120 bpm eighth notes) -> 12000 samples at 48k.
        constexpr double iTrue = 0.25;
        const std::uint64_t step = 12000ull;

        // Six onsets -> five intervals of 0.25 s each.
        for (int i = 0; i < 6; ++i)
            tracker.noteOnset(static_cast<std::uint64_t>(i) * step);

        expect(tracker.isEstimateValid(),
               "After >=3 intervals the Audio-mode estimate must be valid");
        const float I = tracker.getIntervalSeconds();
        expect(std::fabs(I - static_cast<float>(iTrue)) < kIntervalTol,
               "Regular onset train must yield interval near 0.25 s");
    }

    void runIntervalMedianOutlier()
    {
        CadenceTracker tracker;
        tracker.prepare(kSampleRate);
        tracker.setMode(CadenceTracker::Mode::Audio);

        constexpr double iTrue = 0.25;
        const std::uint64_t step = 12000ull;

        // Build five regular intervals (six onsets).
        for (int i = 0; i < 6; ++i)
            tracker.noteOnset(static_cast<std::uint64_t>(i) * step);

        const float Ibefore = tracker.getIntervalSeconds();
        expect(std::fabs(Ibefore - static_cast<float>(iTrue)) < kIntervalTol,
               "Pre-outlier interval must be near 0.25 s");

        // One outlier interval: 0.60 s (ratio 2.4 > 2.0 -> rejected as outlier).
        tracker.noteOnset(6ull * step + 28800ull); // +0.60 s after last onset
        expect(std::fabs(tracker.getIntervalSeconds() - Ibefore) < kExactTol,
               "Rejected outlier must not change the interval estimate");

        // Resume regular spacing; the median must remain near 0.25 s.
        tracker.noteOnset(6ull * step + 28800ull + step); // 0.25 s after outlier
        const float Iafter = tracker.getIntervalSeconds();
        expect(std::fabs(Iafter - static_cast<float>(iTrue)) < kIntervalTol,
               "Robust median must stay near regular spacing after an outlier");
    }

    // ---- 11.3 Release constraint -----------------------------------------

    void runReleaseConstraint()
    {
        CadenceTracker tracker;
        tracker.prepare(kSampleRate);
        tracker.setMode(CadenceTracker::Mode::Audio);

        // Establish I = 0.25 s.
        const std::uint64_t step = 12000ull;
        for (int i = 0; i < 6; ++i)
            tracker.noteOnset(static_cast<std::uint64_t>(i) * step);
        const float I = tracker.getIntervalSeconds();
        expect(std::fabs(I - 0.25f) < kIntervalTol, "I must be ~0.25 s");

        constexpr float c = 0.5f;
        tracker.setConstraintFactor(c);
        expect(std::fabs(tracker.getConstraintFactor() - c) < kExactTol,
               "Constraint factor must round-trip");

        // tau_release_max = c * I = 0.5 * 0.25 s = 0.125 s = 125 ms.
        const float constraintSec = tracker.getReleaseConstraintSeconds();
        expect(std::fabs(constraintSec - (c * 0.25f)) < kExactTol,
               "Release constraint seconds must equal c*I");

        // tau_manual above the cap -> capped to 125 ms.
        const float above = tracker.getEffectiveRelease(200.0f);
        expect(std::fabs(above - 125.0f) < kMsTol,
               "tau_manual above cap must clamp to c*I (125 ms)");

        // tau_manual below the cap -> unchanged.
        const float below = tracker.getEffectiveRelease(50.0f);
        expect(std::fabs(below - 50.0f) < kMsTol,
               "tau_manual below cap must pass through unchanged");

        expect(above > below,
               "Capped release must exceed the below-cap manual release");
    }

    // ---- 11.4 Mode Off ---------------------------------------------------

    void runModeOff()
    {
        CadenceTracker tracker;
        tracker.prepare(kSampleRate);
        tracker.setMode(CadenceTracker::Mode::Audio);
        const std::uint64_t step = 12000ull;
        for (int i = 0; i < 6; ++i)
            tracker.noteOnset(static_cast<std::uint64_t>(i) * step);

        tracker.setMode(CadenceTracker::Mode::Off);
        expect(!tracker.isEstimateValid(),
               "Off mode must report an invalid estimate");
        expect(std::fabs(tracker.getReleaseConstraintSeconds()) < kExactTol,
               "Off mode must report zero release constraint");

        constexpr float tauManual = 123.0f;
        const float eff = tracker.getEffectiveRelease(tauManual);
        expect(std::fabs(eff - tauManual) < kExactTol,
               "Off mode must return tau_manual unchanged");
        expect(std::fabs(tracker.getIntervalSeconds()) < kExactTol,
               "Off mode must report a zero interval");
    }

    // ---- Insufficient history --------------------------------------------

    void runInsufficientHistory()
    {
        CadenceTracker tracker;
        tracker.prepare(kSampleRate);
        tracker.setMode(CadenceTracker::Mode::Audio);

        expect(!tracker.isEstimateValid(),
               "Fresh tracker must not have a valid estimate");

        const std::uint64_t step = 12000ull;
        // 1 interval (2 onsets) -> still below minimum of 3.
        tracker.noteOnset(0ull);
        tracker.noteOnset(step);
        expect(!tracker.isEstimateValid(),
               "Two onsets (1 interval) must not be a valid estimate");

        // 2 intervals (3 onsets) -> still below minimum of 3 intervals.
        tracker.noteOnset(2ull * step);
        expect(!tracker.isEstimateValid(),
               "Three onsets (2 intervals) must not be a valid estimate");

        // 3 intervals (4 onsets) -> valid.
        tracker.noteOnset(3ull * step);
        expect(tracker.isEstimateValid(),
               "Four onsets (3 intervals) must produce a valid estimate");
    }
};

// ===========================================================================
// NoiseFloorEstimator suite
// ===========================================================================

class NoiseFloorTests final : public juce::UnitTest
{
public:
    NoiseFloorTests() : juce::UnitTest("ChugGate NoiseFloor Tests", "DSP") {}

    void runTest() override
    {
        beginTest("NoiseFloor: idle noise capture floor below louder tone; "
                  "percentile floor responds to observation level");
        runFloorRespondsToLevel();

        beginTest("NoiseFloor: applySuggestions only commits after explicit call");
        runApplySuggestionsGate();

        beginTest("NoiseFloor: slow adapt rejected when not closed / transient flag set");
        runSlowAdaptGuards();
    }

private:
    // ---- 12.2 Percentile floor responds to level -------------------------

    void runFloorRespondsToLevel()
    {
        NoiseFloorEstimator quiet;
        quiet.prepare(kSampleRate, kBlockSize);
        quiet.setCaptureLengthSeconds(1.0f);
        quiet.setPercentile(0.35f);
        quiet.setSnrMarginDb(12.0f);
        quiet.startLearn();
        // Feed slightly more than 1.0 s so the auto-finish triggers.
        feedConstantLevel(quiet, -90.0f, static_cast<int>(kSampleRate) + 1000);
        expect(quiet.getState() == NoiseFloorEstimator::LearnState::Ready,
               "Quiet capture must finish into Ready");
        expect(quiet.hasSuggestions(),
               "Ready state must expose suggestions");
        const float quietFloor = quiet.getBroadbandFloorDb();

        NoiseFloorEstimator loud;
        loud.prepare(kSampleRate, kBlockSize);
        loud.setCaptureLengthSeconds(1.0f);
        loud.setPercentile(0.35f);
        loud.setSnrMarginDb(12.0f);
        loud.startLearn();
        feedConstantLevel(loud, -40.0f, static_cast<int>(kSampleRate) + 1000);
        expect(loud.getState() == NoiseFloorEstimator::LearnState::Ready,
               "Loud capture must finish into Ready");
        const float loudFloor = loud.getBroadbandFloorDb();

        // Constant-level buffers produce a percentile exactly equal to the
        // input level.
        expect(std::fabs(quietFloor - (-90.0f)) < 1.0e-2f,
               "Quiet constant-level percentile floor must equal the level");
        expect(std::fabs(loudFloor - (-40.0f)) < 1.0e-2f,
               "Loud constant-level percentile floor must equal the level");
        expect(quietFloor < loudFloor,
               "Idle noise floor must sit below the louder tone floor");

        // Suggestions thresholds = floor + SNR margin.
        const auto &qs = quiet.getSuggestions();
        expect(qs.valid, "Suggestions must be valid after capture");
        expect(std::fabs(qs.openThresholdDb - (-90.0f + 12.0f)) < 1.0e-2f,
               "Open threshold must equal floor + SNR margin");
        expect(std::fabs(qs.closeThresholdDb - (qs.openThresholdDb - qs.hysteresisDb)) < 1.0e-2f,
               "Close threshold must equal open threshold minus hysteresis");
    }

    // ---- 12.1 applySuggestions gate -------------------------------------

    void runApplySuggestionsGate()
    {
        NoiseFloorEstimator est;
        est.prepare(kSampleRate, kBlockSize);
        est.setCaptureLengthSeconds(1.0f);
        est.setSnrMarginDb(12.0f);

        // Before any learn, apply must fail and applied suggestions are default.
        expect(!est.applySuggestions(),
               "applySuggestions must fail before a capture is ready");
        expect(!est.getAppliedSuggestions().valid,
               "Applied suggestions must remain default before accept");

        est.startLearn();
        feedConstantLevel(est, -60.0f, static_cast<int>(kSampleRate) + 1000);
        expect(est.getState() == NoiseFloorEstimator::LearnState::Ready,
               "Capture must finish into Ready");
        expect(est.hasSuggestions(),
               "hasSuggestions must be true after learn");
        expect(est.getSuggestions().valid,
               "getSuggestions must be valid after learn");
        expect(std::fabs(est.getSuggestions().openThresholdDb - (-48.0f)) < 1.0e-2f,
               "Suggested open threshold must be populated (-60 + 12 = -48 dB)");

        // Recommended but not yet committed.
        expect(!est.getAppliedSuggestions().valid,
               "Applied suggestions must remain default until accept");

        // Explicit accept commits the applied profile.
        expect(est.applySuggestions(),
               "applySuggestions must succeed in Ready state");
        expect(est.getState() == NoiseFloorEstimator::LearnState::Applied,
               "State must become Applied after accept");
        expect(est.getAppliedSuggestions().valid,
               "Applied suggestions must be valid after accept");
        expect(std::fabs(est.getAppliedSuggestions().openThresholdDb - (-48.0f)) < 1.0e-2f,
               "Applied open threshold must match the suggested value");

        // A second accept now that state is Applied must fail (not Ready).
        expect(!est.applySuggestions(),
               "applySuggestions must fail when not in Ready state");
    }

    // ---- 12.3 Slow adaptation guards ------------------------------------

    void runSlowAdaptGuards()
    {
        NoiseFloorEstimator est;
        est.prepare(kSampleRate, kBlockSize);
        est.setCaptureLengthSeconds(1.0f);
        est.setSnrMarginDb(12.0f);
        est.startLearn();
        feedConstantLevel(est, -60.0f, static_cast<int>(kSampleRate) + 1000);
        expect(est.getState() == NoiseFloorEstimator::LearnState::Ready,
               "Capture must finish into Ready before slow-adapt test");
        const float learnedFloor = est.getBroadbandFloorDb();
        expect(std::fabs(learnedFloor - (-60.0f)) < 1.0e-2f,
               "Learned floor must equal the captured level");

        est.setSlowAdaptationEnabled(true);
        est.setSlowAdaptationLambda(0.9f); // fast, deterministic adaptation

        // Transient flag set -> adaptation rejected, floor unchanged.
        NoiseFloorEstimator::ProcessFlags transientFlags{};
        transientFlags.transientActivity = true;
        transientFlags.confidentlyClosed = true; // closed, but transient wins
        feedConstantLevelFlags(est, -70.0f, 20 * kBlockSize, kBlockSize, transientFlags);
        expect(std::fabs(est.getBroadbandFloorDb() - learnedFloor) < 1.0e-3f,
               "Transient flag must suppress slow adaptation");

        // Not confidently closed -> adaptation rejected, floor unchanged.
        NoiseFloorEstimator::ProcessFlags openFlags{};
        openFlags.confidentlyClosed = false;
        feedConstantLevelFlags(est, -70.0f, 20 * kBlockSize, kBlockSize, openFlags);
        expect(std::fabs(est.getBroadbandFloorDb() - learnedFloor) < 1.0e-3f,
               "Open gate must suppress slow adaptation");

        // Confidently closed with no veto flags -> floor adapts downward.
        NoiseFloorEstimator::ProcessFlags closedFlags{};
        closedFlags.confidentlyClosed = true;
        feedConstantLevelFlags(est, -70.0f, 200 * kBlockSize, kBlockSize, closedFlags);
        const float adaptedFloor = est.getBroadbandFloorDb();
        expect(adaptedFloor < learnedFloor - 5.0f,
               "Confidently-closed low observation must pull the floor downward");
        expect(adaptedFloor > -70.0f - 1.0f,
               "Floor must not overshoot the observed level (approaches -70 dB)");
    }
};

// Static instances register the suites with juce::UnitTestRunner.
static CadenceTests s_cadenceTestsInstance;
static NoiseFloorTests s_noiseFloorTestsInstance;

} // namespace
