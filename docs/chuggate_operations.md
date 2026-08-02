# ChugGate — Operations Manual

## Plugin Overview
ChugGate is a performance-aware adaptive gate, downward expander, and multiband noise controller designed for electric guitar and bass. It is optimized to suppress pickup hum, high-gain amp hiss, and cabinet resonance tails while preserving note attacks, fast alternate-picked chugs, and singing lead sustain.

## Intended Use
The plugin is intended to be inserted on high-gain guitar or bass tracks (pre- or post-amplifier sim) or buses. It utilizes a flexible sidechain key path to allow a clean DI signal to drive the gating of noisy post-amplifier channels.

## Signal Flow
The internal audio signal path runs from input to output through the following sequential stages:
1. **Input Stage & Lookahead Delay (`global.lookahead`)**: Incoming audio can be delayed by a lookahead buffer (0.25 to 10 ms) to align with gate transitions.
2. **Key Path Conditioning**: Taps the key source (`key.source`), blending external sidechain and main input if configured. The sidechain path is pre-conditioned using a high-pass filter (`key.hpfFrequency`), low-pass filter (`key.lpfFrequency`), emphasis boost, and a powerline hum notch filter (`key.humMode`). It is offset by a temporal alignment delay (`key.alignmentOffset`).
3. **Behavior Classifier (`global.mode`)**:
   - *Chug*: Fast-attack, short-hold, cadence-aligned gating.
   - *Lead*: Legs/bends preservation with harmonic sustain tracking.
   - *Hybrid*: Dynamic tracking that morphs between chug and lead classifications.
   - *Manual*: Simple static threshold gate.
4. **Adaptive Controls**:
   - *Attack Guard*: Suppresses pop/click transition transients.
   - *Sustain Friend*: Extends gate release automatically on held notes or feedback.
   - *Cadence Tracker*: Adjusts release timing based on host tempo or performance density.
   - *Noise Learn*: Tracks long-term noise floor and adjusts thresholds dynamically.
5. **Spectral Topology Splitter (`global.topology`)**: Splits signal according to topology:
   - *FullBand*: Processes signal as a single frequency band.
   - *DualBand*: Body and Hiss bands separated by a crossover (`crossovers.dualFreq`, 1.5–8 kHz).
   - *TriBand*: Low, Mid, and High bands separated by low crossover (`crossovers.triLowFreq`) and high crossover (`crossovers.triHighFreq`).
   - *HissCurtain*: Dynamic SNR-driven low-pass filter sliding between f_min (`hissCurtain.minFreq`) and f_max (`hissCurtain.maxFreq`).
6. **Gain & Expansion Application (`global.gainMode`)**: Applies gain reduction using hard-gate, soft-gate, or downward expansion.
7. **Stereo Link Matrix (`stereo.linkMode`)**: Links gain reduction between left and right channels.
8. **Output Trim (`global.outputTrim`)**: Applies master output level adjustment.

## Controls and Parameters
Exposed parameters registered with the APVTS:

### Performance Tab
- **Enabled (`global.enabled`)**: bool, default On. Enables gate processing.
- **Mode (`global.mode`)**: choice, Chug (0), Lead (1), Hybrid (2), Manual (3), default Chug.
- **Topology (`global.topology`)**: choice, FullBand (0), DualBand (1), TriBand (2), HissCurtain (3), default FullBand.
- **Gain Mode (`global.gainMode`)**: choice, HardGate (0), SoftGate (1), DownwardExpander (2), default HardGate.
- **Open Threshold (`global.thresholdOpen`)**: float, -100.0 to 0.0 dBFS, default -45.0 dBFS. Sets the gate open threshold.
- **Hysteresis (`global.hysteresis`)**: float, 0.5 to 24.0 dB, default 6.0 dB. Gate close threshold is Open Threshold minus Hysteresis.
- **Attack (`global.attack`)**: float, 0.01 to 50.0 ms, default 1.0 ms. Sets gate open speed.
- **Hold (`global.hold`)**: float, 0.0 to 1000.0 ms, default 40.0 ms. Sets gate open hold time.
- **Release (`global.release`)**: float, 2.0 to 5000.0 ms, default 100.0 ms. Sets gate close speed.
- **Floor (`global.floor`)**: float, -80.0 to 0.0 dB, default -80.0 dB. Sets the maximum attenuation depth.
- **Ratio (`global.ratio`)**: float, 1.0 to 100.0:1, default 4.0:1. Sets downward expansion ratio.
- **Knee (`global.knee`)**: float, 0.0 to 24.0 dB, default 6.0 dB. Sets expansion knee.
- **Output Trim (`global.outputTrim`)**: float, -24.0 to +12.0 dB, default 0.0 dB.
- **Bypass (`global.bypass`)**: bool, default Off. Bypasses audio processing.
- **Profile (`profile.index`)**: choice, 12 options (e.g. Tight Rhythm, Lead Sustain, Pre-Amp DI Gate), default Tight Rhythm.
- **Tuning (`tuning.index`)**: choice, 18 guitar/bass tunings, default E Standard.
- **A/B State (`ab.state`)**: choice, A or B, default A. Switches active setting slot.

### Detection Tab
- **Key Source (`key.source`)**: choice, 9 options (e.g. Main Input, External Sidechain, Blended, Mid, Side), default Main Input.
- **External Key Blend (`key.externalBlend`)**: float, 0.0 to 1.0, default 1.0. Blends input and sidechain envelopes.
- **Key HPF Enabled (`key.hpfEnabled`)**: bool, default On.
- **Key HPF Frequency (`key.hpfFrequency`)**: float, 20.0 to 250.0 Hz, default 70.0 Hz.
- **Key HPF Slope (`key.hpfSlope`)**: choice, 12 dB/oct (0), 24 dB/oct (1), default 12 dB/oct.
- **Key LPF Enabled (`key.lpfEnabled`)**: bool, default Off.
- **Key LPF Frequency (`key.lpfFrequency`)**: float, 1.5 to 20.0 kHz, default 12.0 kHz.
- **Key LPF Slope (`key.lpfSlope`)**: choice, 12 dB/oct (0), 24 dB/oct (1), default 12 dB/oct.
- **Key Emphasis Frequency (`key.emphasisFreq`)**: float, 30.0 Hz to 12.0 kHz, default 1200.0 Hz.
- **Key Emphasis Gain (`key.emphasisGain`)**: float, -18.0 to +18.0 dB, default 0.0 dB.
- **Key Emphasis Q (`key.emphasisQ`)**: float, 0.1 to 20.0, default 1.0.
- **Hum Mode (`key.humMode`)**: choice, Off (0), 50Hz (1), 60Hz (2), Auto (3), default Off.
- **Hum Harmonics (`key.humHarmonics`)**: choice, Fundamental (0), Fundamental+2nd (1), Fundamental+2nd+3rd (2), default Fundamental.
- **Hum Notch Q (`key.humNotchQ`)**: float, 1.0 to 50.0, default 20.0.
- **Hum Immunity (`key.humImmunityAmount`)**: float, 0.0 to 1.0, default 0.5.
- **Alignment Offset (`key.alignmentOffset`)**: float, -100.0 to +100.0 ms, default 0.0 ms. Shifts detector trigger relative to main audio.
- **Missing Sidechain Fallback (`key.fallbackMode`)**: choice, Internal Key (0), Hold State (1), Close Gate (2), default Internal Key.
- **Key Listen Mode (`key.listenMode`)**: choice, Off (0), Raw Key (1), Conditioned Key (2), Aligned Key (3), Transient Key (4), Sustain Key (5), default Off. Auditions detector path.

### Shape Tab
- **Attack Guard Enabled (`attackGuard.enabled`)**: bool, default On.
- **Attack Guard Amount (`attackGuard.amount`)**: float, 0.0 to 1.0, default 0.75.
- **Attack Guard Sensitivity (`attackGuard.sensitivity`)**: float, 0.0 to 1.0, default 0.5.
- **Attack Guard Frequency (`attackGuard.frequency`)**: float, 30.0 Hz to 12.0 kHz, default 1800.0 Hz.
- **Attack Guard Decay (`attackGuard.decay`)**: float, 1.0 to 500.0 ms, default 40.0 ms.
- **Soft Note Assist (`attackGuard.softNoteAssist`)**: float, 0.0 to 1.0, default 0.5.
- **Maximum Threshold Reduction (`attackGuard.maxThresholdReduction`)**: float, 0.0 to 36.0 dB, default 12.0 dB.
- **Attack Confirmation Window (`attackGuard.confirmationWindow`)**: float, 0.1 to 50.0 ms, default 5.0 ms.

### Alignment Tab
- **Sustain Enabled (`sustain.enabled`)**: bool, default On.
- **Sustain Amount (`sustain.amount`)**: float, 0.0 to 1.0, default 0.5.
- **Sustain Release Extension (`sustain.releaseExtension`)**: float, 0.0 to 10.0x, default 2.0x.
- **Sustain Harmonic Weight (`sustain.harmonicWeight`)**: float, 0.0 to 1.0, default 0.5.
- **Sustain Decay Weight (`sustain.decayWeight`)**: float, 0.0 to 1.0, default 0.3.
- **Sustain Noise Weight (`sustain.noiseWeight`)**: float, 0.0 to 1.0, default 0.5.
- **Feedback Friend Enabled (`sustain.feedbackEnabled`)**: bool, default Off.
- **Feedback Sensitivity (`sustain.feedbackSensitivity`)**: float, 0.0 to 1.0, default 0.5.
- **Feedback Minimum Duration (`sustain.feedbackMinDuration`)**: float, 0.0 to 10000.0 ms, default 500.0 ms.
- **Feedback Maximum Hold (`sustain.feedbackMaxHold`)**: float, 0.0 to 30000.0 ms, default 5000.0 ms.
- **Feedback Minimum Frequency (`sustain.feedbackFreqMin`)**: float, 20.0 Hz to 5.0 kHz, default 80.0 Hz.
- **Feedback Maximum Frequency (`sustain.feedbackFreqMax`)**: float, 100.0 Hz to 20.0 kHz, default 8000.0 Hz.
- **Feedback Floor (`sustain.feedbackFloor`)**: float, -80.0 to 0.0 dB, default -12.0 dB.
- **Feedback Requires External Key (`sustain.feedbackRequireExtKey`)**: bool, default Off.
- **Feedback Safety Timeout (`sustain.feedbackSafetyTimeout`)**: float, 100.0 to 60000.0 ms, default 10000.0 ms.
- **Cadence Enabled (`cadence.enabled`)**: bool, default On.
- **Cadence Amount (`cadence.amount`)**: float, 0.0 to 1.0, default 0.5.
- **Cadence Release Fraction (`cadence.releaseFraction`)**: float, 0.15 to 0.85, default 0.5.
- **Cadence Source (`cadence.sourceMode`)**: choice, Audio (0), HostTempo (1), Combined (2), Off (3), default Audio.
- **Noise Learn SNR Margin (`noiseLearn.snrMargin`)**: float, 0.0 to 36.0 dB, default 12.0 dB.
- **Adaptive Noise Enabled (`noiseLearn.adaptiveEnabled`)**: bool, default Off.
- **Adaptive Noise Rate (`noiseLearn.adaptiveRate`)**: float, 0.0 to 1.0, default 0.05.

### Diagnostics Tab (Expert & Band parameters)
- **Maximum Adaptive Release (`global.maxAdaptiveRelease`)**: float, 10.0 to 10000.0 ms, default 2000.0 ms.
- **Curve (`global.curve`)**: float, 0.25 to 4.0, default 1.0.
- **Stereo Link Mode (`stereo.linkMode`)**: choice, Max (0), Energy (1), Average (2), DualMono (3), default Max.
- **Stereo Link Amount (`stereo.linkAmount`)**: float, 0.0 to 1.0, default 1.0.
- **Dual Band Crossover (`crossovers.dualFreq`)**: float, 1500.0 to 8000.0 Hz, default 4000.0 Hz.
- **Tri Band Low Crossover (`crossovers.triLowFreq`)**: float, 80.0 to 900.0 Hz, default 250.0 Hz.
- **Tri Band High Crossover (`crossovers.triHighFreq`)**: float, 1500.0 to 10000.0 Hz, default 4500.0 Hz.
- **Hiss Curtain Minimum Frequency (`hissCurtain.minFreq`)**: float, 500.0 to 8000.0 Hz, default 1500.0 Hz.
- **Hiss Curtain Maximum Frequency (`hissCurtain.maxFreq`)**: float, 3000.0 to 20000.0 Hz, default 16000.0 Hz.
- **Hiss Curtain Depth (`hissCurtain.depth`)**: float, 0.0 to 1.0, default 0.75.
- **Rejected Signal Audition (`audition.rejectedMode`)**: choice, Off (0) to Key (8), default Off.
- **Audition State Filter (`audition.stateFilterMode`)**: choice, Off (0) to Closed (4), default Off.
- **Low/Mid/High Band parameters**: Each band exposes: `Floor` (-80.0 to 0.0 dB), `Threshold Offset` (-24.0 to +24.0 dB), `Attack Offset` (-50.0 to +50.0 ms), `Hold Offset` (-1000.0 to +1000.0 ms), `Release Multiplier` (0.1 to 4.0x), `Solo` (bool), `Mute` (bool), and `Detector Weighting` (0.0 to 1.0).

## Stereo and Mono Behavior
Stereo link matrix behavior is determined by `stereo.linkMode` and `stereo.linkAmount`. Modes include Max, Energy, Average, and DualMono, allowing customization of dynamic attenuation sharing between Left and Right channels.

## Latency and Lookahead
- The default lookahead mode is Off (`0` samples of reported latency).
- Selecting a lookahead value (0.25 ms, 0.5 ms, 1 ms, 2 ms, 5 ms, 10 ms) activates the internal lookahead delay buffer and correctly reports the latency to the host via `setLatencySamples()`.

## Bypass Behavior
Controlled via the `global.bypass` boolean parameter. When enabled, audio processing is bypassed, passing the input signal transparently to the output.

## Presets and State Recall
The plugin features program-based A/B memory state registers (`ab.state`) and separate factory and user preset workflows.

- Six factory presets are supplied as JSON resources: `Bass_ActiveNoiseControl`, `DIKeyed_Rhythm`, `Diag_LearnMyRig`, `Lead_SingingSustain`, `PostAmp_HissSuppression`, and `Rhythm_ModernTight`. Select one in the Factory Preset control and use **Load Preset** to apply it.
- To save the current settings as a user preset, use **Save** in the header preset control. If a preset name is selected, that name is used; otherwise the default name is `User Preset`. Select a saved user preset and use **Load** to recall it.
- User presets are stored as XML below the operating system's user application-data directory at `ChugGate/Presets/`. The directory is created when the first user preset is saved.
- Factory and user preset recall updates the complete plugin state. The host session also preserves the current state through normal plugin state serialization.

## Metering and Visual Feedback
- **State Indicator (`StateIndicator`)**: Renders a vertical colored state column representing the active state machine status: OPEN (green), HOLD (yellow), SUSTAIN (blue), RELEASE (purple), or CLOSED (red).
- **Performance Timeline (`PerformanceTimeline`)**: Scrolling GUI trace displaying the sidechain key envelope, gain attenuation curves across bands, and onset ticks over a time history.
- **Level Meters**: Traditional input, output, and sidechain level indicators.

## Supported Formats
- VST3, Standalone.
