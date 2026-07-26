# a

## Goal

Project scaffold, build system, APVTS parameter infrastructure, and state engine.

Create the ChugGate JUCE plugin project. Inspect ShredBoost and ShredSpace CMakeLists.txt for known-working structure and follow same conventions for plugin code, manufacturer code, company name, bundle ID, version, copy-after-build.

CMakeLists.txt: juce_add_plugin targeting VST3 + Standalone. CMakePresets.json, scripts/build_all.sh, JUCE as git submodule.

PluginProcessor: configure main input bus, optional sidechain input bus, and output through AudioProcessor::BusesProperties. Implement isBusesLayoutSupported for mono, stereo, and stereo+sidechain layouts. Standalone must operate safely without external sidechain. processBlock stub, state save/restore with schema versioning.

ParameterIDs.h: complete stable v1.0 parameter inventory. Global: enabled, mode (Chug/Lead/Hybrid/Manual), topology (FullBand/DualBand/TriBand/HissCurtain), gainMode (HardGate/SoftGate/DownwardExpander), thresholdOpen (-100 to 0dBFS), hysteresis (0.5-24dB — thresholdClose is DERIVED as open-hysteresis, not stored), attack, hold, release, maxAdaptiveRelease, floor, ratio, knee, curve, lookahead, outputTrim, bypass. Key: source, externalBlend, hpfEnabled, hpfFrequency, hpfSlope, lpfEnabled, lpfFrequency, lpfSlope, emphasisFreq, emphasisGain, emphasisQ, humMode (off/50Hz/60Hz/auto), humHarmonics, humNotchQ, humImmunityAmount, alignmentOffset, fallbackMode, listenMode. AttackGuard: enabled, amount, sensitivity, frequency, decay, softNoteAssist, maxThresholdReduction, confirmationWindow. Sustain: enabled, amount, releaseExtension, harmonicWeight, decayWeight, noiseWeight, feedbackEnabled, feedbackSensitivity, feedbackMinDuration, feedbackMaxHold, feedbackFreqMin, feedbackFreqMax, feedbackFloor, feedbackRequireExtKey, feedbackSafetyTimeout. Cadence: enabled, amount, releaseFraction, sourceMode (audio/hostTempo/combined/off). Noise learn: snrMargin, adaptiveEnabled, adaptiveRate. Per-band (low/mid/high): floor, thresholdOffset, attackOffset, holdOffset, releaseMultiplier, solo, mute, detectorWeighting. HissCurtain: minFreq, maxFreq, depth. Stereo: linkMode, linkAmount. Crossovers: dualFreq, triLowFreq, triHighFreq. Audition: rejectedMode (off/all/fullBand/weight/voice/hiss/hissCurtain/noiseModel/key), stateFilterMode (off/hold/sustain/release/closed). Profile and tuning index. A/B state.

State engine: XML state with schema_version, A/B slots, learned noise profile storage, DI alignment result storage, factory presets from Resources/Presets/ (read-only), user presets via per-user app data.

.gitignore: build/, cmake-build-*, state/, .abracapocus/state/, .abracapocus/logs/, .abracapocus/reports/.

## Notes

- Async: Wave 1: a.1 (CMakeLists + build + JUCE submodule) and a.4 (.gitignore + README + TestMain) independent. Wave 2: a.2 (ParameterIDs + APVTS + PluginProcessor) depends on a.1 — sole owner of CMakeLists, Processor, ParameterIDs. Wave 3: a.3 (StateManager + PluginEditor) depends on a.2.
- Risk: a.2 sole CMakeLists/Processor owner. Sidechain bus requires correct isBusesLayoutSupported. thresholdClose is derived display value, not stored parameter.
