# c

## Goal

Performance state machine, dual-threshold hysteresis, gain computation and smoothing.

See CHUGGATE_SPEC.md sections 7, 15, 16 for complete DSP math.

8-state engine: Closed, Primed, Attack, Open, Hold, Sustain, Release, Reopen. Hysteresis: T_close = T_open - H (computed, not stored). Reopen reverses gain path smoothly.

GainComputer: hard gate, soft gate, downward expander laws. Curve shape with gamma.
GainSmoother: separate open/close constants per band.

Wire into processBlock.

## Notes

- Async: Wave 1: c.1 (StateMachine) and c.2 (GainComputer + GainSmoother) independent. Wave 2: c.3 (processBlock + CMakeLists) depends on c.1, c.2 — sole Processor/CMakeLists owner. Wave 3: c.4 (tests) depends on c.1, c.2.
- Risk: PluginProcessor fan-in in c.3. State machine transitions must be exhaustively tested.
