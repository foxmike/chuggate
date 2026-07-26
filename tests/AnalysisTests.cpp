// =============================================================================
// ChugGate - Analysis Data Pipeline Unit Tests (Phase H)
// =============================================================================
// Focused juce::UnitTest cases covering the Phase-H analysis pipeline per
// .abracapocus/CHUGGATE_SPEC.md §20 (Visual Diagnostic System), §28.2
// (Analysis Transport: lock-free SPSC, drop-never-block), and Appendix A.4
// (Attack/Tail Integrity Analyzers, A_r = sum(y^2)/sum(x^2)).
//
// Coverage:
//   1) AnalysisFifo            — SPSC non-blocking full/drop + ordered pop
//   2) PerformanceHistory      — duration bounds + copyRecent ring fidelity
//   3) NoiseFingerprint        — fixed-bin spectrum ingest + readback
//   4) AlignmentScopeData     — fixed-buffer onset/correlation ingest
//   5) AttackIntegrityAnalyzer — A_r preserved / partial / missed synthetics
//   6) TailIntegrityAnalyzer   — choke detection vs matched-decay baseline
//
// All fixtures are deterministic: fixed sample rates, fixed block lengths,
// and explicit float tolerances. No audio hardware, no real-time threads.
//
// NOTE ON DEPENDENCIES: This file exercises the public APIs documented in
// phase-H task specs h.1–h.4. The AnalysisFrame field set used below is the
// exact set consumed by PerformanceHistory::pushFromFrame (see
// Source/Analysis/PerformanceHistory.cpp) and so matches the h.1 contract.
// The AttackIntegrityAnalyzer / TailIntegrityAnalyzer call sites follow the
// allowed_symbols list of task h.4. Adjust only here if a later task finalises
// a different signature for those two analyzers.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Source/Analysis/AnalysisFifo.h"
#include "../Source/Analysis/PerformanceHistory.h"
#include "../Source/Analysis/NoiseFingerprint.h"
#include "../Source/Analysis/AlignmentScopeData.h"
#include "../Source/Analysis/AttackIntegrityAnalyzer.h"
#include "../Source/Analysis/TailIntegrityAnalyzer.h"

namespace {

using namespace ChugGate::Analysis;

// ---------------------------------------------------------------------------
// Fixed test constants
// ---------------------------------------------------------------------------

constexpr double kSampleRate = 48000.0;
constexpr double kControlRateHz = 100.0; // PerformanceHistory slot rate

// Tolerances — explicit per acceptance criteria.
constexpr float kExactTol = 1.0e-4f;   // bit-exact paths (memcpy / scalar echo)
constexpr float kArTol = 1.0e-2f;     // A_r ratio over finite-energy windows
constexpr float kChokeTol = 1.0e-2f;  // choke score thresholds

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a monotonically labelled AnalysisFrame. mainEnvelope carries the
// sequence index so FIFO ordering can be verified on pop.
AnalysisFrame makeSequenceFrame(int index)
{
    AnalysisFrame frame{};
    const float v = static_cast<float>(index);
    frame.keyEnvelope = 0.10f * v;
    frame.mainEnvelope = v; // sequence marker
    frame.thresholdOpen = -20.0f + 0.01f * v;
    frame.thresholdClose = -40.0f + 0.01f * v;
    frame.noiseFloor = -60.0f;
    frame.gateState = 1; // open-ish lane; plain int per spec "gate state enum/int"
    frame.outputGain = 0.5f;
    frame.transientMarker = (index % 4 == 0) ? 1.0f : 0.0f;
    frame.sustainConfidence = 0.25f * static_cast<float>(index % 4);
    frame.hasAlignmentOffset = false;
    frame.alignmentOffset = 0.0f;
    frame.hasLookaheadRegion = false;
    frame.lookaheadRegion = false;
    return frame;
}

// Linear ramp from a to b over count samples.
std::vector<float> makeRamp(int count, float a, float b)
{
    std::vector<float> buf(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
        buf[static_cast<std::size_t>(i)] = a + (b - a) * t;
    }
    return buf;
}

// Exponential decay starting at amplitude, time-constant in samples.
std::vector<float> makeDecay(int count, float amplitude, double tauSamples)
{
    std::vector<float> buf(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        buf[static_cast<std::size_t>(i)] =
            amplitude * static_cast<float>(std::exp(-static_cast<double>(i) / tauSamples));
    return buf;
}

} // namespace

// =============================================================================
// Test suite
// =============================================================================

class AnalysisTests final : public juce::UnitTest
{
public:
    AnalysisTests() : juce::UnitTest("ChugGate Analysis Tests", "Analysis") {}

    void runTest() override
    {
        beginTest("AnalysisFifo: SPSC non-blocking full-drop + ordered pop");
        runAnalysisFifoSpsc();

        beginTest("AnalysisFifo: QualityMode::Off never retains frames");
        runAnalysisFifoQualityOff();

        beginTest("PerformanceHistory: duration bounds within [2, 30] s");
        runPerformanceHistoryDuration();

        beginTest("PerformanceHistory: copyRecent fidelity + wrap retention");
        runPerformanceHistoryCopyRecent();

        beginTest("NoiseFingerprint: fixed-bin spectrum ingest + readback");
        runNoiseFingerprintBuffers();

        beginTest("AlignmentScopeData: fixed-buffer onset/correlation readback");
        runAlignmentScopeBuffers();

        beginTest("AttackIntegrityAnalyzer: A_r preserved / partial / missed");
        runAttackIntegrity();

        beginTest("TailIntegrityAnalyzer: choke vs matched-decay baseline");
        runTailIntegrity();
    }

private:
    // ---- 1) AnalysisFifo SPSC ------------------------------------------------

    void runAnalysisFifoSpsc()
    {
        constexpr int kCapacity = 4;

        AnalysisFifo fifo;
        fifo.prepare(kCapacity, 2);
        fifo.reset();
        fifo.setQualityMode(QualityMode::Studio);
        expect(fifo.getQualityMode() == QualityMode::Studio,
               "Studio mode must be selectable on AnalysisFifo");

        // Fill exactly to capacity; every push must succeed.
        for (int i = 0; i < kCapacity; ++i)
        {
            const bool ok = fifo.tryPush(makeSequenceFrame(i));
            expect(ok, "tryPush must succeed while the FIFO has free slots");
        }

        // The next push must be rejected (drop-never-block): it must not
        // spin/wait, and it must return false immediately.
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const bool overflow = fifo.tryPush(makeSequenceFrame(kCapacity));
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;
        expect(!overflow,
               "tryPush on a full FIFO must return false (drop-never-block)");
        expect(elapsedMs < 50.0,
               "tryPush on a full FIFO must not block the producer");

        // The drop counter must reflect the rejected frame.
        expect(fifo.getDropCount() >= static_cast<std::uint64_t>(1),
               "Drop count must increment when a frame is dropped on full FIFO");

        // Drain in FIFO order and verify the sequence marker survives.
        for (int i = 0; i < kCapacity; ++i)
        {
            AnalysisFrame out{};
            const bool ok = fifo.tryPop(out);
            expect(ok, "tryPop must succeed while the FIFO is non-empty");
            expect(std::fabs(out.mainEnvelope - static_cast<float>(i)) < kExactTol,
                   "tryPop must drain frames in push order");
        }

        // Empty FIFO: tryPop returns false, non-blocking.
        AnalysisFrame out2{};
        expect(!fifo.tryPop(out2),
               "tryPop on an empty FIFO must return false without blocking");
    }

    // ---- 1b) AnalysisFifo QualityMode::Off ---------------------------------

    void runAnalysisFifoQualityOff()
    {
        constexpr int kCapacity = 4;

        AnalysisFifo fifo;
        fifo.prepare(kCapacity, 2);
        fifo.reset();
        fifo.setQualityMode(QualityMode::Off);
        expect(fifo.getQualityMode() == QualityMode::Off,
               "Off mode must be selectable on AnalysisFifo");

        // Off path must never retain frames: tryPush is a no-op / rejection.
        for (int i = 0; i < kCapacity + 2; ++i)
        {
            const bool ok = fifo.tryPush(makeSequenceFrame(i));
            expect(!ok,
                   "tryPush under QualityMode::Off must not retain frames");
        }

        // Switching back to Studio must resume from an empty FIFO.
        fifo.setQualityMode(QualityMode::Studio);
        AnalysisFrame out{};
        expect(!fifo.tryPop(out),
               "No frames may survive an Off-mode window when the UUT gates "
               "push by mode");
    }

    // ---- 2) PerformanceHistory ---------------------------------------------

    void runPerformanceHistoryDuration()
    {
        PerformanceHistory history;
        PerformanceHistory::Config cfg;
        cfg.controlRateHz = kControlRateHz;
        cfg.minDurationSeconds = 2.0;
        cfg.maxDurationSeconds = 30.0;
        cfg.initialDurationSeconds = 10.0;
        cfg.numChannels = 0;
        history.prepare(cfg);

        // In-range durations are accepted verbatim.
        history.setDurationSeconds(2.0);
        expect(std::fabs(history.getDurationSeconds() - 2.0) < 1.0e-6,
               "setDurationSeconds must accept the lower bound (2 s)");
        expect(std::fabs(history.getMinDurationSeconds() - 2.0) < 1.0e-6,
               "Min duration must be 2 s");

        history.setDurationSeconds(30.0);
        expect(std::fabs(history.getDurationSeconds() - 30.0) < 1.0e-6,
               "setDurationSeconds must accept the upper bound (30 s)");
        expect(std::fabs(history.getMaxDurationSeconds() - 30.0) < 1.0e-6,
               "Max duration must be 30 s");

        history.setDurationSeconds(15.0);
        expect(std::fabs(history.getDurationSeconds() - 15.0) < 1.0e-6,
               "Mid-range duration must be accepted verbatim");

        // Out-of-range values must clamp to the documented [2, 30] s window.
        history.setDurationSeconds(0.5);
        expect(std::fabs(history.getDurationSeconds() - 2.0) < 1.0e-6,
               "Sub-minimum duration must clamp to 2 s");
        history.setDurationSeconds(1000.0);
        expect(std::fabs(history.getDurationSeconds() - 30.0) < 1.0e-6,
               "Over-maximum duration must clamp to 30 s");
    }

    void runPerformanceHistoryCopyRecent()
    {
        PerformanceHistory history;
        PerformanceHistory::Config cfg;
        cfg.controlRateHz = kControlRateHz;
        cfg.minDurationSeconds = 2.0;
        cfg.maxDurationSeconds = 30.0;
        cfg.initialDurationSeconds = 2.0; // small window for determinism
        cfg.numChannels = 0;
        history.prepare(cfg);
        history.setQualityMode(QualityMode::Studio);

        // Push a known sequence; mainEnvelope = i so order can be checked.
        constexpr int kNumPush = 5;
        for (int i = 0; i < kNumPush; ++i)
        {
            const int written = history.push(
                /*keyEnvelope*/ 0.10f * static_cast<float>(i),
                /*mainEnvelope*/ static_cast<float>(i),
                /*thresholdOpen*/ -20.0f + static_cast<float>(i),
                /*thresholdClose*/ -40.0f + static_cast<float>(i),
                /*noiseFloor*/ -60.0f,
                /*gateState*/ static_cast<std::uint8_t>(i % 4),
                /*outputGain*/ 0.5f,
                /*transientMarker*/ (i % 2 == 0) ? 1.0f : 0.0f,
                /*sustainConfidence*/ 0.1f * static_cast<float>(i));
            expect(written == 1, "Studio-mode push must store every slot");
        }
        expect(history.getNumSamples() == kNumPush,
               "getNumSamples must equal the number of stored pushes");

        // copyRecent must return the most recent samples in oldest-first
        // order, matching every series within tolerance.
        std::vector<float> keyEnv(static_cast<std::size_t>(kNumPush));
        std::vector<float> mainEnv(static_cast<std::size_t>(kNumPush));
        std::vector<float> threshOpen(static_cast<std::size_t>(kNumPush));
        std::vector<float> threshClose(static_cast<std::size_t>(kNumPush));
        std::vector<float> noiseFloor(static_cast<std::size_t>(kNumPush));
        std::vector<float> outputGain(static_cast<std::size_t>(kNumPush));
        std::vector<float> transient(static_cast<std::size_t>(kNumPush));
        std::vector<float> sustain(static_cast<std::size_t>(kNumPush));
        std::vector<std::uint8_t> gateState(static_cast<std::size_t>(kNumPush));

        PerformanceHistory::CopyBuffers bufs;
        bufs.keyEnvelope = keyEnv.data();
        bufs.mainEnvelope = mainEnv.data();
        bufs.thresholdOpen = threshOpen.data();
        bufs.thresholdClose = threshClose.data();
        bufs.noiseFloor = noiseFloor.data();
        bufs.outputGain = outputGain.data();
        bufs.transientMarker = transient.data();
        bufs.sustainConfidence = sustain.data();
        bufs.gateState = gateState.data();

        const int copied = history.copyRecent(bufs, kNumPush);
        expect(copied == kNumPush,
               "copyRecent must copy the full valid window when dst >= valid");

        for (int i = 0; i < kNumPush; ++i)
        {
            expect(std::fabs(mainEnv[static_cast<std::size_t>(i)] - static_cast<float>(i)) < kExactTol,
                   "copyRecent mainEnvelope must match push order");
            expect(std::fabs(keyEnv[static_cast<std::size_t>(i)] - 0.10f * static_cast<float>(i)) < kExactTol,
                   "copyRecent keyEnvelope must match push values");
            expect(std::fabs(threshOpen[static_cast<std::size_t>(i)] - (-20.0f + static_cast<float>(i))) < kExactTol,
                   "copyRecent thresholdOpen must match push values");
            expect(std::fabs(threshClose[static_cast<std::size_t>(i)] - (-40.0f + static_cast<float>(i))) < kExactTol,
                   "copyRecent thresholdClose must match push values");
            expect(std::fabs(noiseFloor[static_cast<std::size_t>(i)] - (-60.0f)) < kExactTol,
                   "copyRecent noiseFloor must match push values");
            expect(std::fabs(outputGain[static_cast<std::size_t>(i)] - 0.5f) < kExactTol,
                   "copyRecent outputGain must match push values");
            expect(std::fabs(transient[static_cast<std::size_t>(i)] - ((i % 2 == 0) ? 1.0f : 0.0f)) < kExactTol,
                   "copyRecent transientMarker must match push values");
            expect(std::fabs(sustain[static_cast<std::size_t>(i)] - 0.1f * static_cast<float>(i)) < kExactTol,
                   "copyRecent sustainConfidence must match push values");
            expect(gateState[static_cast<std::size_t>(i)] == static_cast<std::uint8_t>(i % 4),
                   "copyRecent gateState must match push values");
        }

        // Wrapping beyond capacity must retain the most recent samples.
        const int cap = history.getCapacity();
        const int overflow = cap + 10;
        for (int i = 0; i < overflow; ++i)
            history.push(0.0f, static_cast<float>(1000 + i), -20.0f, -40.0f,
                         -60.0f, 0, 0.5f, 0.0f, 0.0f);

        std::vector<float> wrapMain(static_cast<std::size_t>(cap));
        PerformanceHistory::CopyBuffers wbufs;
        wbufs.mainEnvelope = wrapMain.data();
        const int wcopied = history.copyRecent(wbufs, cap);
        expect(wcopied == cap,
               "copyRecent after overflow must return capacity samples");
        // The most recent `cap` samples have mainEnvelope = 1000 + overflow - cap .. 1000 + overflow - 1.
        for (int i = 0; i < cap; ++i)
        {
            const float expected = static_cast<float>(1000 + overflow - cap + i);
            expect(std::fabs(wrapMain[static_cast<std::size_t>(i)] - expected) < kExactTol,
                   "Wrapping must retain the most recent samples in order");
        }

        // QualityMode::Off must freeze history (push returns 0, no growth).
        const std::uint64_t beforeOff = history.getTotalPushes();
        history.setQualityMode(QualityMode::Off);
        const int offWritten = history.push(0.0f, 7.0f, -20.0f, -40.0f, -60.0f,
                                            0, 0.5f, 0.0f, 0.0f);
        expect(offWritten == 0, "Off-mode push must not store a slot");
        expect(history.getTotalPushes() > beforeOff,
               "Off push must still be counted as a (dropped) push attempt");
    }

    // ---- 3) NoiseFingerprint ------------------------------------------------

    void runNoiseFingerprintBuffers()
    {
        constexpr int kBins = 64;
        NoiseFingerprint nf;
        nf.prepare(kSampleRate, kBins);

        expect(nf.getNumBins() == kBins,
               "getNumBins must reflect the prepared bin count");
        expect(nf.getMedianFloorSpan().size() == static_cast<std::size_t>(kBins),
               "Median-floor buffer length must equal numBins");
        expect(nf.getUpperPercentileSpan().size() == static_cast<std::size_t>(kBins),
               "Upper-percentile buffer length must equal numBins");

        // setSpectrum must fill exactly numBins magnitudes.
        std::vector<float> median(static_cast<std::size_t>(kBins));
        std::vector<float> upper(static_cast<std::size_t>(kBins));
        for (int b = 0; b < kBins; ++b)
        {
            median[static_cast<std::size_t>(b)] = -60.0f + 0.1f * static_cast<float>(b);
            upper[static_cast<std::size_t>(b)] = -50.0f + 0.1f * static_cast<float>(b);
        }
        nf.setSpectrum(median.data(), upper.data(), kBins);

        for (int b = 0; b < kBins; ++b)
        {
            expect(std::fabs(nf.getMedianFloor()[b] - median[static_cast<std::size_t>(b)]) < kExactTol,
                   "getMedianFloor must echo setSpectrum values");
            expect(std::fabs(nf.getUpperPercentile()[b] - upper[static_cast<std::size_t>(b)]) < kExactTol,
                   "getUpperPercentile must echo setSpectrum values");
        }

        // updateFromLearnedProfile must populate the magnitude buffers and
        // echo feature flags / peak bins.
        std::vector<float> learned(static_cast<std::size_t>(kBins));
        for (int b = 0; b < kBins; ++b)
            learned[static_cast<std::size_t>(b)] = -55.0f + 0.05f * static_cast<float>(b);

        NoiseFingerprint::LearnedProfile prof;
        prof.medianFloor = learned.data();
        prof.numBins = kBins;
        prof.sampleRate = kSampleRate;
        prof.humFiftyHz = true;
        prof.hasHumHarmonics = true;
        prof.hasHighFrequencyHiss = true;
        prof.humPeakBin = 3;
        prof.hissPeakBin = 60;
        nf.updateFromLearnedProfile(prof);

        expect(nf.hasFiftyHzHum(), "Fifty-Hz flag must echo the learned profile");
        expect(nf.hasHumHarmonics(), "Harmonic-hum flag must echo the learned profile");
        expect(nf.hasHighFrequencyHiss(), "HF-hiss flag must echo the learned profile");
        expect(nf.getHumPeakBin() == 3, "Hum peak bin must echo the learned profile");
        expect(nf.getHissPeakBin() == 60, "Hiss peak bin must echo the learned profile");
        for (int b = 0; b < kBins; ++b)
        {
            expect(std::fabs(nf.getMedianFloor()[b] - learned[static_cast<std::size_t>(b)]) < kExactTol,
                   "updateFromLearnedProfile must fill getNumBins magnitudes");
        }

        // Buffer length must remain fixed (== numBins) after ingest.
        expect(nf.getMedianFloorSpan().size() == static_cast<std::size_t>(nf.getNumBins()),
               "Magnitude length must match bin count after ingest");

        // QualityMode::Off must turn subsequent setSpectrum into a no-op.
        nf.setQualityMode(QualityMode::Off);
        std::vector<float> zeros(static_cast<std::size_t>(kBins), 0.0f);
        nf.setSpectrum(zeros.data(), zeros.data(), kBins);
        // Off clears existing data, so the floor is zeroed and stays so.
        for (int b = 0; b < kBins; ++b)
        {
            expect(std::fabs(nf.getMedianFloor()[b]) < kExactTol,
                   "Off-mode setSpectrum must not repopulate buffers");
        }
        expect(nf.getNumBins() == kBins,
               "Off mode must preserve the prepared bin count");
    }

    // ---- 4) AlignmentScopeData ---------------------------------------------

    void runAlignmentScopeBuffers()
    {
        constexpr int kKeyCap = 64;
        constexpr int kMainCap = 64;
        constexpr int kCorrCap = 129;

        AlignmentScopeData scope;
        scope.prepare(kKeyCap, kMainCap, kCorrCap);
        scope.setQualityMode(QualityMode::Diagnose);

        // Fixed capacities after prepare.
        expect(scope.getKeyOnsetCapacity() == kKeyCap,
               "Key onset capacity must be fixed after prepare");
        expect(scope.getMainOnsetCapacity() == kMainCap,
               "Main onset capacity must be fixed after prepare");
        expect(scope.getCorrelationCapacity() == kCorrCap,
               "Correlation capacity must be fixed after prepare");

        // Ingest onset waveforms and correlation curve at full length.
        auto keyWave = makeRamp(kKeyCap, -0.5f, 0.5f);
        auto mainWave = makeRamp(kMainCap, -1.0f, 1.0f);
        auto corr = makeRamp(kCorrCap, -1.0f, 1.0f);
        scope.setOnsetWaveforms(keyWave.data(), kKeyCap,
                                mainWave.data(), kMainCap);
        scope.setCorrelationCurve(corr.data(), kCorrCap);

        // Buffer lengths track the most recent valid write and equal the
        // ingest count (clamped to capacity).
        expect(scope.getKeyOnsetLength() == kKeyCap,
               "Key onset length must equal ingest count after prepare");
        expect(scope.getMainOnsetLength() == kMainCap,
               "Main onset length must equal ingest count after prepare");
        expect(scope.getCorrelationLength() == kCorrCap,
               "Correlation length must equal ingest count after prepare");

        // Readback fidelity — zero-copy spans echo the ingested samples.
        const auto keySpan = scope.getKeyOnsetSpan();
        const auto mainSpan = scope.getMainOnsetSpan();
        const auto corrSpan = scope.getCorrelationSpan();
        expect(static_cast<int>(keySpan.size()) >= kKeyCap,
               "Key onset span must cover the ingested length");
        expect(static_cast<int>(mainSpan.size()) >= kMainCap,
               "Main onset span must cover the ingested length");
        expect(static_cast<int>(corrSpan.size()) >= kCorrCap,
               "Correlation span must cover the ingested length");
        for (int i = 0; i < kKeyCap; ++i)
            expect(std::fabs(keySpan[static_cast<std::size_t>(i)] - keyWave[static_cast<std::size_t>(i)]) < kExactTol,
                   "Key onset readback must match ingest");
        for (int i = 0; i < kMainCap; ++i)
            expect(std::fabs(mainSpan[static_cast<std::size_t>(i)] - mainWave[static_cast<std::size_t>(i)]) < kExactTol,
                   "Main onset readback must match ingest");
        for (int i = 0; i < kCorrCap; ++i)
            expect(std::fabs(corrSpan[static_cast<std::size_t>(i)] - corr[static_cast<std::size_t>(i)]) < kExactTol,
                   "Correlation readback must match ingest");

        // Scalar recommendation: setResult(offset, confidence, ...) must be
        // echoed verbatim by the getters.
        scope.setResult(/*offsetMs*/ 1.5f, /*confidence*/ 0.8f,
                        /*offsetVariationMs*/ 0.2f,
                        /*recommendedKeyDelayMs*/ 1.5f,
                        /*mainLookaheadMs*/ 0.0f,
                        /*needsMainLookahead*/ false);
        expect(std::fabs(scope.getOffsetMs() - 1.5f) < kExactTol,
               "getOffsetMs must echo setResult");
        expect(std::fabs(scope.getConfidence() - 0.8f) < kExactTol,
               "getConfidence must echo setResult");
        expect(std::fabs(scope.getOffsetVariationMs() - 0.2f) < kExactTol,
               "getOffsetVariationMs must echo setResult");
        expect(std::fabs(scope.getRecommendedKeyDelayMs() - 1.5f) < kExactTol,
               "getRecommendedKeyDelayMs must echo setResult");
        expect(!scope.needsMainLookahead(),
               "needsMainLookahead must echo setResult");

        // QualityMode::Off must freeze further updates and reset scalars.
        scope.setQualityMode(QualityMode::Off);
        scope.setResult(9.0f, 0.1f, 0.0f, 0.0f, 0.0f, true);
        expect(std::fabs(scope.getOffsetMs()) < kExactTol,
               "Off-mode setResult must not update the scalar recommendation");
        expect(std::fabs(scope.getConfidence()) < kExactTol,
               "Off-mode setResult must not update confidence");
    }

    // ---- 5) AttackIntegrityAnalyzer -----------------------------------------

    void runAttackIntegrity()
    {
        AttackIntegrityAnalyzer ai;
        ai.prepare();
        ai.reset();
        ai.setQualityMode(QualityMode::Diagnose);

        constexpr int kWindow = 64;

        // (a) y == x  ->  A_r = sum(y^2)/sum(x^2) == 1  -> preserved.
        auto x = makeRamp(kWindow, 0.0f, 1.0f);
        auto yPreserved = x;
        ai.observeOnset(x.data(), yPreserved.data(), kWindow);
        const float arPreserved = ai.getAttackPreservationRatio();
        expect(std::fabs(arPreserved - 1.0f) < kArTol,
               "A_r must be ~1 when y == x (attack preserved)");
        const auto preservedBefore = ai.getPreservedCount();
        // Re-observe a preserved attack to bump the preserved counter.
        ai.observeOnset(x.data(), yPreserved.data(), kWindow);
        expect(ai.getPreservedCount() > preservedBefore,
               "Preserved count must increment for a preserved attack");

        // (b) y ~ 0 with nonzero x  ->  A_r ~ 0  -> missed.
        std::vector<float> yMissed(static_cast<std::size_t>(kWindow), 0.0f);
        const auto missedBefore = ai.getMissedCount();
        ai.observeOnset(x.data(), yMissed.data(), kWindow);
        const float arMissed = ai.getAttackPreservationRatio();
        expect(arMissed < 0.5f,
               "A_r must be ~0 (below partial threshold) when y ~ 0");
        expect(ai.getMissedCount() > missedBefore,
               "Missed count must increment for a missed attack");

        // (c) intermediate y = 0.8 x  ->  A_r = 0.64  -> partial.
        std::vector<float> yPartial(static_cast<std::size_t>(kWindow));
        for (int i = 0; i < kWindow; ++i)
            yPartial[static_cast<std::size_t>(i)] = 0.8f * x[static_cast<std::size_t>(i)];
        const auto partialBefore = ai.getPartialCount();
        ai.observeOnset(x.data(), yPartial.data(), kWindow);
        const float arPartial = ai.getAttackPreservationRatio();
        expect(arPartial > 0.5f && arPartial < 0.9f,
               "A_r must land in the partial band for an intermediate attack");
        expect(ai.getPartialCount() > partialBefore,
               "Partial count must increment for a partial attack");

        // Counts must be non-decreasing aggregates over the history window.
        expect(ai.getPreservedCount() >= static_cast<std::uint64_t>(1), "Preserved aggregate >= 1");
        expect(ai.getPartialCount() >= static_cast<std::uint64_t>(1), "Partial aggregate >= 1");
        expect(ai.getMissedCount() >= static_cast<std::uint64_t>(1), "Missed aggregate >= 1");
    }

    // ---- 6) TailIntegrityAnalyzer -------------------------------------------

    void runTailIntegrity()
    {
        TailIntegrityAnalyzer ti;
        ti.prepare();
        ti.reset();
        ti.setQualityMode(QualityMode::Diagnose);

        constexpr int kTailLen = 256;
        constexpr float kAmplitude = 1.0f;

        // (a) Choke: main decays slowly (long tau) while the output decays
        // fast (short tau) and sustain confidence is high — the lead note
        // is being choked prematurely.
        auto mainDecay = makeDecay(kTailLen, kAmplitude, /*tau*/ 200.0);
        auto chokedOut = makeDecay(kTailLen, kAmplitude, /*tau*/ 20.0); // much faster
        const auto chokeBefore = ti.getChokeCount();
        ti.observeTail(mainDecay.data(), chokedOut.data(), kTailLen,
                       /*sustainConfidence*/ 0.9f);
        const float chokeScoreChoked = ti.getChokeScore();
        expect(chokeScoreChoked > kChokeTol,
               "Choke score must be elevated when output decays far faster "
               "than main under high sustain confidence");
        expect(ti.getChokeCount() > chokeBefore,
               "Choke count must increment for a clearly choked tail");

        // (b) Matched decay baseline: main and output decay identically —
        // no choking. Score must be low.
        ti.reset();
        auto matchedOut = mainDecay; // identical decay
        ti.observeTail(mainDecay.data(), matchedOut.data(), kTailLen,
                       /*sustainConfidence*/ 0.9f);
        const float chokeScoreMatched = ti.getChokeScore();
        expect(chokeScoreMatched < kChokeTol,
               "Choke score must be low when main and output decays match");
        expect(ti.getChokeCount() == static_cast<std::uint64_t>(0),
               "No choke event must be recorded for a matched-decay tail");
    }
};

// Static instance registers the suite with juce::UnitTestRunner.
static AnalysisTests s_analysisTestsInstance;
