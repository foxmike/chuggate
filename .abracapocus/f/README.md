# f

## Goal

Multiband: LR4 crossovers, dual/tri band gate, Hiss Curtain, per-band gain.

See CHUGGATE_SPEC.md sections 14-16, 20 for complete DSP math.

CrossoverSplitter: LR4 2-way and 3-way, complementary tree, magnitude reconstruction tolerance-based (not sample-identical). MultibandGate: per-band gain targets and smoothing. HissCurtain: SNR-driven TPT LP filter.

Topology selector: Full Band bypasses splitter, Dual/Tri activate splitter+gate, Hiss Curtain activates curtain module. Crossfade transitions.

## Notes

- Async: Wave 1: f.1 (CrossoverSplitter) standalone. Wave 2: f.2 (MultibandGate) and f.3 (HissCurtain) depend on f.1 but independent of each other. Wave 3: f.4 (topology selector + processBlock + CMakeLists) depends on all — sole Processor/CMakeLists owner. Wave 4: f.5 (tests) depends on all.
- Risk: PluginProcessor fan-in in f.4. Topology crossfade must not click.
