# j

## Goal

Profiles, tuning, factory presets, A/B comparison, and rejected-signal audition wiring.

12 profiles: Tight Rhythm, Low-Tuned Rhythm, Fast Alternate Picking, Lead Sustain, Rhythm+Lead Hybrid, Controlled Feedback Lead, Pre-Amp DI, Post-Amp Hiss, DI-Keyed Reamp, Stereo Rhythm Bus, Active Bass, Distorted Bass.

Tuning integration: adjusts key HPF, crossover, detector weighting, pitch range, sustain bounds.

A/B: extend StateManager, click-safe crossfade, copy A→B.

Factory presets: categorized, gain-conscious. Preset browser in header.

Rejected-signal audition GUI wiring.

## Notes

- Async: Wave 1: j.1 (Profiles + Tuning) and j.2 (A/B state) independent. Wave 2: j.3 (factory preset assets) depends on j.1. Wave 3: j.4 (preset browser + audition GUI wiring + Processor/Editor/CMakeLists) depends on all — sole Processor/Editor/CMakeLists owner. Wave 4: j.5 (tests) depends on all.
- Risk: StateManager, PluginProcessor, PluginEditor modified — j.4 is fan-in. Preset JSON files are independent.
