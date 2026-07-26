# d

## Goal

Pick Attack Guard, Sustain Intelligence with lightweight fundamental estimator, and Feedback Friend.

See CHUGGATE_SPEC.md sections 8-10 for complete DSP math.

AttackGuard: band-limited transient detection with false-trigger protection.

Lightweight fundamental estimator (Source/DSP/FundamentalEstimator.h/.cpp): required by SustainAnalyzer and FeedbackDetector for harmonicity calculation. Use bounded autocorrelation or harmonic product spectrum — does NOT need full YIN/note tracking (that's ShredEQ territory). Must produce: estimatedFundamental, confidence, timestamp, validity. Runs at decimated control rate.

SustainAnalyzer: harmonicity using FundamentalEstimator f0, spectral stability, decay slope. Composite sustain score. Adaptive release.

FeedbackDetector: harmonic confidence, safety bounds mandatory.

Integrate all into PerformanceStateMachine.

## Notes

- Async: Wave 1: d.1 (AttackGuard) and d.2 (FundamentalEstimator) independent. Wave 2: d.3 (SustainAnalyzer + FeedbackDetector) depends on d.2. Wave 3: d.4 (state machine integration + CMakeLists) depends on all — sole Processor/CMakeLists owner. Wave 4: d.5 (tests) depends on d.1, d.2, d.3.
- Risk: PerformanceStateMachine and PluginProcessor modified in d.4 — serialize after phase C. FundamentalEstimator may need small fixed DFT.
