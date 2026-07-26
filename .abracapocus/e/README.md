# e

## Goal

Riff Cadence Engine, Noise Learn system, and DI key alignment.

See CHUGGATE_SPEC.md sections 11-13 for complete DSP math.

CadenceTracker: onset intervals, robust median, release constraint.
NoiseFloorEstimator: noise learn workflow, per-band percentile, slow adaptation.
AlignmentAnalyzer: onset cross-correlation, calibration workflow.
FractionalDelay (Source/DSP/FractionalDelay.h/.cpp): preallocated circular buffer with third-order Lagrange interpolation for sub-sample alignment precision. Range ±25ms default, ±100ms extended.

Wire cadence into adaptive release, noise learn into threshold recommendations, alignment into key delay path.

## Notes

- Async: Wave 1: e.1 (CadenceTracker) and e.2 (NoiseFloorEstimator) and e.3 (AlignmentAnalyzer + FractionalDelay) all independent. Wave 2: e.4 (processBlock integration + CMakeLists) depends on all — sole Processor/CMakeLists owner. Wave 3: e.5 (tests) depends on e.1, e.2, e.3.
- Risk: AlignmentAnalyzer cross-correlation runs off audio thread. FractionalDelay uses Lagrange interpolation in v1.0.
