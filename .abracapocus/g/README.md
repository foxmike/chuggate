# g

## Goal

Lookahead delay, stereo linking, anomaly protection, and rejected-signal routing.

See CHUGGATE_SPEC.md sections 17-19, 20.5.

LookaheadDelay: circular buffer, off/0.25/0.5/1/2/5/10ms, setLatencySamples, zero default.
StereoLinker: Max/Energy/Average/DualMono, link percentage, mono safety warning.
AnomalyGuard: Cable Spike Guard, Stuck-Key detection, Dropout handling.
RejectedSignalRouter: r[n] = x[n]-y[n], per-band, state-triggered audition.

## Notes

- Async: Wave 1: g.1 (LookaheadDelay) and g.2 (StereoLinker) and g.3 (AnomalyGuard + RejectedSignalRouter) all independent. Wave 2: g.4 (processBlock wiring + CMakeLists) depends on all — sole Processor/CMakeLists owner. Wave 3: g.5 (tests) depends on g.1, g.2, g.3.
- Risk: PluginProcessor heavily modified — fan-in in g.4. Lookahead must call setLatencySamples.
