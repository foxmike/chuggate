// =============================================================================
// ChugGate - Performance State Machine / Gain Computer / Gain Smoother Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase C maths described in
// .abracapocus/CHUGGATE_SPEC.md sections 7, 15 and 16:
//   * PerformanceStateMachine dual-threshold hysteresis (T_close = T_open - H)
//   * Hold honoring the configured hold time at a fixed sample rate
//   * Reopen path activating from Release on a valid transient stimulus
//   * Section 7.3 smoothstep soft-decision midpoint behaviour
//   * GainComputer hard/soft floor/unity targets and expander G_exp_dB law
//   * Section 15.5 curve shape gamma behaviour
//   * Section 16 GainSmoother separate open/close time constants
//
// All fixtures are deterministic: fixed sample rate, fixed block lengths and
// explicit tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Source/DSP/PerformanceStateMachine.h"
#include "../Source/DSP/GainComputer.h"
#include "../Source/DSP/GainSmoother.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;

// Tolerances.
constexpr float kExactTol = 1.0e-4f;
constexpr float kSoftTol = 1.0e-5f;
constexpr float kGainTol = 1.0e-3f;

// Helper: feed a constant detector level (with no extra evidence) and return
// the final output. Useful for driving the state machine through timing-only
// transitions.
PerformanceStateMachine::Output
feedConstant(PerformanceStateMachine &machine, float levelDb, int numSamples)
{
    PerformanceStateMachine::Output out{};
    for (int i = 0; i < numSamples; ++i)
        out = machine.processSample(levelDb);
    return out;
}

// Helper: feed a constant detector level but supply an evidence record on
// every sample (used for transient / reopen stimuli).
PerformanceStateMachine::Output
feedConstantWithEvidence(PerformanceStateMachine &machine, float levelDb,
                         const PerformanceStateMachine::Evidence &evidence,
                         int numSamples)
{
    PerformanceStateMachine::Output out{};
    for (int i = 0; i < numSamples; ++i)
        out = machine.processSample(levelDb, evidence);
    return out;
}

class StateMachineTests final : public juce::UnitTest
{
public:
    StateMachineTests() : juce::UnitTest("ChugGate State Machine Tests", "DSP") {}

    void runTest() override
    {
        beginTest("Hysteresis: T_close == T_open - H and dual-threshold behaviour");
        runHysteresis();

        beginTest("Hold: holds below open threshold for configured hold time");
        runHold();

        beginTest("Reopen: valid transient during Release enters Reopen");
        runReopen();

        beginTest("Soft decision: smoothstep midpoint ~0.5");
        runSoftDecisionMidpoint();

        beginTest("GainComputer: hard/soft floor/unity targets");
        runGainComputerHardSoft();

        beginTest("GainComputer: expander G_exp_dB = max(G_floor, (R-1)*(L-T))");
        runGainComputerExpander();

        beginTest("Curve shape: gamma>1 pulls mid-S lower than gamma=1");
        runCurveShape();

        beginTest("GainSmoother: separate open/close time constants");
        runGainSmootherTimeConstants();
    }

private:
    // ---- PerformanceStateMachine -------------------------------------------

    void runHysteresis()
    {
        PerformanceStateMachine machine;
        machine.prepare(kSampleRate);

        constexpr float kOpen = -40.0f;
        constexpr float kHyst = 6.0f;
        constexpr float kClose = kOpen - kHyst; // -46 dB

        machine.setThresholdOpenDb(kOpen);
        machine.setHysteresisDb(kHyst);
        // Instant attack so we can reach Open in two samples; non-trivial hold
        // and release so timing is observable.
        machine.setAttackTimeMs(0.0f);
        machine.setHoldTimeMs(50.0f);
        machine.setReleaseTimeMs(100.0f);
        machine.reset();

        // Section 7.2: T_close is derived, never stored independently.
        expectEquals(machine.getThresholdOpenDb(), kOpen,
                     "Threshold open must round-trip");
        expectEquals(machine.getHysteresisDb(), kHyst,
                     "Hysteresis must round-trip");
        expectEquals(machine.getThresholdCloseDb(), kClose,
                     "T_close must equal T_open - H");

        // Level above T_open enters the open family.
        feedConstant(machine, kOpen + 10.0f, 4); // well above open
        expect(machine.getState() == PerformanceStateMachine::State::Open ||
                   machine.getState() == PerformanceStateMachine::State::Attack,
               "Level above T_open must enter an open-family state");
        expect(PerformanceStateMachine::isOpenFamily(machine.getState()),
               "Level above T_open must be in open family");

        // Level between T_close and T_open must NOT immediately force Closed
        // after the open phase: it should drop into Hold (open family).
        feedConstant(machine, kClose + 3.0f, 2); // between close and open
        expect(machine.getState() != PerformanceStateMachine::State::Closed,
               "Mid-hysteresis level must not immediately force Closed after open");
        expect(PerformanceStateMachine::isOpenFamily(machine.getState()),
               "Mid-hysteresis level after open must remain in open family (Hold)");

        // Level below T_close after hold expiry allows Release then Closed.
        const std::uint64_t holdSamples =
            static_cast<std::uint64_t>(kSampleRate * 0.050 + 0.5);
        // Feed well past the hold window so the Hold -> Release transition fires.
        feedConstant(machine, kClose - 4.0f, static_cast<int>(holdSamples + 4));
        expect(machine.getState() == PerformanceStateMachine::State::Release ||
                   machine.getState() == PerformanceStateMachine::State::Closed,
               "Below-T_close after hold expiry must reach Release/Closed path");

        // Continue feeding below close to drain the release window and reach
        // Closed without any reopen stimulus.
        const std::uint64_t releaseSamples =
            static_cast<std::uint64_t>(kSampleRate * 0.100 + 0.5);
        feedConstant(machine, kClose - 4.0f, static_cast<int>(releaseSamples + 4));
        expect(machine.getState() == PerformanceStateMachine::State::Closed,
               "Sustained below-T_close level must eventually return to Closed");
    }

    void runHold()
    {
        PerformanceStateMachine machine;
        machine.prepare(kSampleRate);

        constexpr float kOpen = -40.0f;
        constexpr float kHyst = 6.0f;
        constexpr float kClose = kOpen - kHyst;

        machine.setThresholdOpenDb(kOpen);
        machine.setHysteresisDb(kHyst);
        machine.setAttackTimeMs(0.0f);
        machine.setHoldTimeMs(50.0f); // 50 ms -> 2400 samples at 48 kHz
        machine.setReleaseTimeMs(100.0f);
        machine.reset();

        // Reach Open.
        feedConstant(machine, kOpen + 10.0f, 4);
        expect(machine.getState() == PerformanceStateMachine::State::Open,
               "Must reach Open before testing hold");

        // Drop to a level below open but above close: should enter Hold.
        feedConstant(machine, kClose + 2.0f, 1);
        expect(machine.getState() == PerformanceStateMachine::State::Hold,
               "Below-open, above-close level must enter Hold");

        // The configured hold time in samples (mirrors the engine's own
        // ms->samples conversion).
        const std::uint64_t holdSamples =
            static_cast<std::uint64_t>(kSampleRate * 0.050 + 0.5);

        // Feeding just shy of the hold window must keep us in Hold.
        feedConstant(machine, kClose + 2.0f, static_cast<int>(holdSamples) - 1);
        expect(machine.getState() == PerformanceStateMachine::State::Hold,
               "Hold must persist until the configured hold time elapses");
        expect(machine.getLastOutput().hasFlag(PerformanceStateMachine::HoldActive),
               "HoldActive flag must be set while in Hold");

        // One more sample past the window transitions out of Hold. Above-close
        // with no sustain protection routes to Sustain (HysteresisProtection),
        // which is still open-family and not Release/Closed.
        feedConstant(machine, kClose + 2.0f, 2);
        expect(machine.getState() != PerformanceStateMachine::State::Hold,
               "Hold must release after the configured hold time elapses");
        expect(PerformanceStateMachine::isOpenFamily(machine.getState()),
               "Above-close post-hold transition must remain open family");

        // A below-close drop after hold expiry routes to Release (closing).
        machine.reset();
        feedConstant(machine, kOpen + 10.0f, 4);
        feedConstant(machine, kClose + 2.0f, 1); // -> Hold
        feedConstant(machine, kClose - 4.0f,
                     static_cast<int>(holdSamples) + 2);
        expect(machine.getState() == PerformanceStateMachine::State::Release,
               "Below-close after hold expiry must enter Release");
    }

    void runReopen()
    {
        PerformanceStateMachine machine;
        machine.prepare(kSampleRate);

        constexpr float kOpen = -40.0f;
        constexpr float kHyst = 6.0f;
        constexpr float kClose = kOpen - kHyst;

        machine.setThresholdOpenDb(kOpen);
        machine.setHysteresisDb(kHyst);
        machine.setAttackTimeMs(0.0f);
        machine.setHoldTimeMs(10.0f);
        machine.setReleaseTimeMs(1000.0f); // long so Release is sticky
        machine.reset();

        // Reach Open, then drive into Release via a below-close hold-out.
        feedConstant(machine, kOpen + 10.0f, 4);
        const std::uint64_t holdSamples =
            static_cast<std::uint64_t>(kSampleRate * 0.010 + 0.5);
        feedConstant(machine, kClose - 4.0f,
                     static_cast<int>(holdSamples) + 2);
        expect(machine.getState() == PerformanceStateMachine::State::Release,
               "Fixture must reach Release before testing reopen");

        // A valid transient during Release must transition directly to Reopen
        // without passing back through Closed first.
        PerformanceStateMachine::Evidence reopenEvidence;
        reopenEvidence.validTransient = true;
        const auto out = feedConstantWithEvidence(machine, kClose - 4.0f,
                                                  reopenEvidence, 1);
        expect(machine.getState() == PerformanceStateMachine::State::Reopen,
               "Valid transient during Release must enter Reopen");
        expect(machine.getLastTransitionReason() ==
                   PerformanceStateMachine::TransitionReason::ReopenTransient,
               "Reopen must record the ReopenTransient reason");
        expect(out.hasFlag(PerformanceStateMachine::ReopenActive),
               "ReopenActive flag must be set during Reopen");
        expect(out.previousState == PerformanceStateMachine::State::Release,
               "Reopen must transition directly from Release, not via Closed");

        // Continuing with a rising level returns through Attack to Open
        // (attack samples == 0 here, so Reopen -> Open on the next sample).
        feedConstant(machine, kOpen + 10.0f, 2);
        expect(machine.getState() == PerformanceStateMachine::State::Open,
               "Reopen must progress back to Open on a recovered level");
    }

    void runSoftDecisionMidpoint()
    {
        constexpr float kOpen = -40.0f;
        constexpr float kHyst = 6.0f;
        constexpr float kClose = kOpen - kHyst;
        constexpr float kMid = 0.5f * (kClose + kOpen); // -43 dB

        // Section 7.3: at the midpoint u = 0.5 and S(u) = 3u^2 - 2u^3 = 0.5.
        const float s = PerformanceStateMachine::softDecision(kMid, kOpen, kHyst);
        expect(std::fabs(s - 0.5f) < kSoftTol,
               "Smoothstep at the threshold midpoint must be ~0.5");

        // Endpoints of the soft region.
        const float sLo = PerformanceStateMachine::softDecision(kClose, kOpen, kHyst);
        const float sHi = PerformanceStateMachine::softDecision(kOpen, kOpen, kHyst);
        expect(std::fabs(sLo - 0.0f) < kSoftTol,
               "Smoothstep at T_close must be ~0.0");
        expect(std::fabs(sHi - 1.0f) < kSoftTol,
               "Smoothstep at T_open must be ~1.0");

        // Monotonicity across the knee: low < mid < high.
        const float sQuarter = PerformanceStateMachine::softDecision(
            kClose + 0.25f * kHyst, kOpen, kHyst);
        const float sThreeQuarter = PerformanceStateMachine::softDecision(
            kClose + 0.75f * kHyst, kOpen, kHyst);
        expect(sLo < sQuarter && sQuarter < s && s < sThreeQuarter && sThreeQuarter < sHi,
               "Smoothstep must be monotonically increasing across the knee");
    }

    // ---- GainComputer ------------------------------------------------------

    void runGainComputerHardSoft()
    {
        constexpr float kFloorDb = -80.0f;
        const float kFloorLinear = GainComputer::floorGain(kFloorDb);
        constexpr float kThreshold = -40.0f;

        // Hard gate, high level, no state hint / certainty: target ~ unity.
        GainComputer hard(GainComputer::Mode::HardGate);
        GainComputer::Input highIn;
        highIn.detectorLevelDb = kThreshold + 20.0f;
        highIn.thresholdDb = kThreshold;
        highIn.floorDb = kFloorDb;
        highIn.gamma = 1.0f;
        const float hardHigh = hard.processSample(highIn);
        expect(std::fabs(hardHigh - 1.0f) < kGainTol,
               "Hard gate above threshold must target near unity");

        // Hard gate, deep noise-floor level: target ~ g_floor.
        GainComputer::Input lowIn;
        lowIn.detectorLevelDb = kThreshold - 60.0f;
        lowIn.thresholdDb = kThreshold;
        lowIn.floorDb = kFloorDb;
        lowIn.gamma = 1.0f;
        const float hardLow = hard.processSample(lowIn);
        expect(std::fabs(hardLow - kFloorLinear) < kGainTol,
               "Hard gate below threshold must target near g_floor");

        // Soft gate at the knee midpoint (openCertainty = 0.5) with gamma = 1
        // yields an intermediate gain strictly between floor and unity, and
        // for gamma = 1 exactly floor + (1 - floor) * 0.5.
        GainComputer soft(GainComputer::Mode::SoftGate);
        GainComputer::Input softIn;
        softIn.detectorLevelDb = kThreshold; // level inside knee
        softIn.thresholdDb = kThreshold;
        softIn.softKneeDb = 6.0f;
        softIn.floorDb = kFloorDb;
        softIn.gamma = 1.0f;
        softIn.openCertainty = 0.5f;
        const float softMid = soft.processSample(softIn);
        const float expectedSoftMid = kFloorLinear + (1.0f - kFloorLinear) * 0.5f;
        expect(std::fabs(softMid - expectedSoftMid) < kGainTol,
               "Soft gate at openCertainty 0.5 must match floor + (1-floor)*0.5");
        expect(softMid > hardLow + kGainTol && softMid < hardHigh - kGainTol,
               "Soft midpoint gain must be intermediate vs hard corners");

        // Soft gate with a low openCertainty collapses toward the floor.
        GainComputer::Input softLowIn = softIn;
        softLowIn.openCertainty = 0.0f;
        const float softLow = soft.processSample(softLowIn);
        expect(std::fabs(softLow - kFloorLinear) < kGainTol,
               "Soft gate at openCertainty 0 must target near g_floor");

        // Soft gate with a high openCertainty collapses toward unity.
        GainComputer::Input softHighIn = softIn;
        softHighIn.openCertainty = 1.0f;
        const float softHigh = soft.processSample(softHighIn);
        expect(std::fabs(softHigh - 1.0f) < kGainTol,
               "Soft gate at openCertainty 1 must target near unity");
    }

    void runGainComputerExpander()
    {
        constexpr float kThreshold = -40.0f;
        constexpr float kRatio = 2.0f;
        constexpr float kFloorDb = -80.0f;
        const float kFloorLinear = GainComputer::floorGain(kFloorDb);

        // Section 15.3: G_exp_dB = max(G_floor_dB, (R-1)*(L-T)).
        // For L = T - 6 dB, R = 2: (R-1)*(L-T) = 1 * -6 = -6 dB, above floor.
        constexpr float kLevel = kThreshold - 6.0f;
        const float gExpDb = GainComputer::downwardExpanderGainDb(
            kLevel, kThreshold, kRatio, kFloorDb);
        expect(std::fabs(gExpDb - (-6.0f)) < kExactTol,
               "Expander G_exp_dB must equal (R-1)*(L-T) when above floor");

        // Linear gain matches 10^(G_dB / 20).
        const float expectedLinear = std::pow(10.0f, gExpDb * 0.05f);
        expect(std::fabs(expectedLinear - std::pow(10.0f, -6.0f * 0.05f)) < kGainTol,
               "Linear gain must match 10^(G_dB/20)");

        // Full GainComputer path with gamma = 1 reduces to the unshaped expander
        // gain (shaped = floor + (1-floor) * amount, amount maps linearly).
        GainComputer expander(GainComputer::Mode::DownwardExpander);
        GainComputer::Input in;
        in.detectorLevelDb = kLevel;
        in.thresholdDb = kThreshold;
        in.ratio = kRatio;
        in.floorDb = kFloorDb;
        in.gamma = 1.0f;
        const float gain = expander.processSample(in);
        expect(std::fabs(gain - expectedLinear) < kGainTol,
               "Expander processSample with gamma=1 must return 10^(G_exp_dB/20)");

        // Floor clamp: a very deep level is clamped to G_floor_dB.
        const float gDeep = GainComputer::downwardExpanderGainDb(
            kThreshold - 200.0f, kThreshold, kRatio, kFloorDb);
        expect(std::fabs(gDeep - kFloorDb) < kExactTol,
               "Deep expander level must clamp to G_floor_dB");
        expect(std::fabs(GainComputer::floorGain(kFloorDb) - kFloorLinear) < kGainTol,
               "floorGain must equal 10^(G_floor_dB/20)");

        // Above threshold the expander must remain at unity (0 dB).
        const float gAbove = GainComputer::downwardExpanderGainDb(
            kThreshold + 6.0f, kThreshold, kRatio, kFloorDb);
        expect(std::fabs(gAbove - 0.0f) < kExactTol,
               "Expander above threshold must return 0 dB (unity)");
    }

    void runCurveShape()
    {
        constexpr float kFloorDb = -80.0f;
        const float kFloorLinear = GainComputer::floorGain(kFloorDb);
        constexpr float kS = 0.5f; // mid-region fixture

        // Section 15.5: g_shaped = g_floor + (1 - g_floor) * S^gamma.
        // gamma = 1 is linear in S between floor and unity.
        const float shapedLinear =
            GainComputer::shape(kS, kFloorLinear, 1.0f);
        const float expectedLinear = kFloorLinear + (1.0f - kFloorLinear) * kS;
        expect(std::fabs(shapedLinear - expectedLinear) < kGainTol,
               "gamma=1 must be linear in S between floor and unity");

        // gamma > 1 pulls the shaped gain lower at the same mid-S fixture.
        const float shapedGamma2 = GainComputer::shape(kS, kFloorLinear, 2.0f);
        expect(shapedGamma2 < shapedLinear - kGainTol,
               "gamma>1 must pull shaped gain below the gamma=1 curve at mid S");
        const float expectedGamma2 = kFloorLinear + (1.0f - kFloorLinear) * (kS * kS);
        expect(std::fabs(shapedGamma2 - expectedGamma2) < kGainTol,
               "gamma=2 shaped gain must equal floor + (1-floor)*S^2");

        // Endpoint sanity: S=0 -> floor, S=1 -> unity for any gamma.
        const float s0 = GainComputer::shape(0.0f, kFloorLinear, 2.0f);
        const float s1 = GainComputer::shape(1.0f, kFloorLinear, 2.0f);
        expect(std::fabs(s0 - kFloorLinear) < kGainTol,
               "shape(S=0) must equal g_floor for any gamma");
        expect(std::fabs(s1 - 1.0f) < kGainTol,
               "shape(S=1) must equal unity for any gamma");
    }

    // ---- GainSmoother -----------------------------------------------------

    void runGainSmootherTimeConstants()
    {
        constexpr int kNumSamples = 100;
        constexpr float kOpenMs = 2.0f;   // fast open
        constexpr float kCloseMs = 200.0f; // slow close (tau_close >> tau_open)

        GainSmoother smoother;
        smoother.prepare(kSampleRate, 1);
        smoother.setOpenTimeMs(kOpenMs);
        smoother.setCloseTimeMs(kCloseMs);

        // --- Upward step: floor (0) -> unity (1) uses the open time constant.
        smoother.reset(0.0f);
        float upGain = 0.0f;
        for (int i = 0; i < kNumSamples; ++i)
            upGain = smoother.processSample(1.0f, 0);
        const float upResidual = 1.0f - upGain; // distance remaining to unity
        expect(upGain > 0.0f && upGain < 1.0f,
               "Upward step must be partially settled after a finite window");
        expect(upResidual > 0.0f,
               "Upward step must not fully reach unity in the fixture window");

        // --- Downward step: unity (1) -> floor (0) uses the close time constant.
        smoother.reset(1.0f);
        float downGain = 1.0f;
        for (int i = 0; i < kNumSamples; ++i)
            downGain = smoother.processSample(0.0f, 0);
        const float downResidual = downGain - 0.0f; // distance remaining to floor
        expect(downGain > 0.0f && downGain < 1.0f,
               "Downward step must be partially settled after a finite window");
        expect(downResidual > 0.0f,
               "Downward step must not fully reach floor in the fixture window");

        // Section 16: with tau_close >> tau_open the downward (close) residual
        // must be larger (slower) than the upward (open) approach residual after
        // an equal number of samples.
        expect(downResidual > upResidual,
               "Downward (close) residual must exceed upward (open) residual "
               "when tau_close >> tau_open");
    }
};

// Static instance registers the test with juce::UnitTestRunner.
static StateMachineTests s_stateMachineTestsInstance;

} // namespace
