# i

## Goal

GUI: LookAndFeel, all pages, Performance Timeline, state indicator, editor layout.

See CHUGGATE_SPEC.md section 21 for complete GUI spec. Correct LookAndFeel_V4 overrides. Default 1280x760, min 920x580.

ChugLookAndFeel: fast/mechanical/precise feel, gate-state accents.
PerformanceTimeline: central visualization rendering from PerformanceHistory.
StateIndicator: state + duration + gain + reason.
PerformPage + ShapePage: fast controls, macros, multiband panel.
DetectPage: key conditioning, hum, threshold, transient, sustain.
AlignPage: alignment scope, calibrate button.
DiagnosePage: noise fingerprint, rejected signal, attack/tail integrity display.
MainHeader: preset, profile, A/B, key, sidechain status, latency, CPU.
PluginEditor: tabbed views, resizing, wiring.

## Notes

- Async: Wave 1: i.1 (LookAndFeel) standalone. Wave 2: i.2 (PerformanceTimeline + StateIndicator) and i.3 (PerformPage + ShapePage) depend on i.1 but independent. Wave 3: i.4 (DetectPage) and i.5 (AlignPage + DiagnosePage) depend on i.1 but independent. Wave 4: i.6 (MainHeader + PluginEditor assembly) fan-in — sole Editor/CMakeLists owner.
- Risk: Antigravity-heavy phase. PluginEditor fan-in in i.6. Known JUCE API: LookAndFeel_V4 signatures.
