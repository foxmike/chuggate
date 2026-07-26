# h

## Goal

Analyzer system: Performance Timeline data, Noise Fingerprint, DI Alignment Scope, Attack Integrity, Tail Integrity, and analysis FIFO.

AnalysisFifo (Source/Analysis/AnalysisFifo.h/.cpp): lock-free SPSC via juce::AbstractFifo. Carries all analysis data. Skip frames, fixed memory.

PerformanceHistory (Source/Analysis/PerformanceHistory.h/.cpp): ring buffer for timeline — envelopes, thresholds, noise floor, state lane, gain, transients, sustain confidence. History 2-30s.

NoiseFingerprint (Source/Analysis/NoiseFingerprint.h/.cpp): time-averaged spectral display of learned noise.

AlignmentScopeData (Source/Analysis/AlignmentScopeData.h/.cpp): key/main onset waveforms, correlation curve, offset, confidence.

AttackIntegrityAnalyzer (Source/Analysis/AttackIntegrityAnalyzer.h/.cpp): per-onset attack preservation ratio A_r = sum(y²)/sum(x²) over attack window. Summarizes preserved/partial/missed attacks. Uses data already collected by Performance Timeline.

TailIntegrityAnalyzer (Source/Analysis/TailIntegrityAnalyzer.h/.cpp): per-tail comparison of main decay vs output decay vs sustain confidence vs closure point. Makes lead-note choking visible.

All on background/message thread. Quality modes: Off, Live, Studio, Diagnose.

## Notes

- Async: Wave 1: h.1 (AnalysisFifo) standalone. Wave 2: h.2 (PerformanceHistory) and h.3 (NoiseFingerprint + AlignmentScopeData) depend on h.1 but independent. Wave 3: h.4 (AttackIntegrity + TailIntegrity) depends on h.2. Wave 4: h.5 (Processor FIFO push + CMakeLists) depends on all — sole Processor/CMakeLists owner. Wave 5: h.6 (tests) depends on all.
- Risk: PluginProcessor modified for FIFO push — serialize after phase G. AbstractFifo sizing must handle all channels.
