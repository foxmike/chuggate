# b

## Goal

Detector engine: key source routing, key conditioning, Powerline Shield, and envelope detection.

See CHUGGATE_SPEC.md sections 4-6 for complete DSP math.

KeySourceRouter: source selection, envelope blending, confidence, internal fallback.
KeyConditioner: HPF/LPF/emphasis biquads, key listen mode.
PowerlineShield: 50/60Hz notch with harmonics, auto-detect, hum immunity.
DetectorEngine: peak/RMS blend, attack/release ballistics, log level. Detector and audible gain ballistics are SEPARATE.

## Notes

- Async: Wave 1: b.1 (KeySourceRouter) and b.2 (KeyConditioner + PowerlineShield) independent. Wave 2: b.3 (DetectorEngine + CMakeLists) depends on b.1, b.2 — sole CMakeLists owner. Wave 3: b.4 (tests) depends on all.
- Risk: Sidechain channel handling must match JUCE bus layout from phase A.
