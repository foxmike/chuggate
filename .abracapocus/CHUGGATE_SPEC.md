# ChugGate — Product and DSP Architecture Specification

**Product:** Performance-aware adaptive gate, downward expander, and multiband noise controller for electric guitar and bass
**Formats:** VST3, Standalone (CLAP and LV2 deferred)
**Stack:** C++20, JUCE, no mandatory third-party DSP framework
**Processing:** Real-time safe, deterministic, zero-latency default, optional correctly reported lookahead, fully local

---

## 1. Product Definition

ChugGate is a guitar-specific gating and noise-control environment designed around the conflicting demands of high-gain playing. The central question it answers: *Is this energy part of the performance, or is it noise that should disappear?*

It must close tightly between rapid palm-muted notes, preserve pick attack, avoid chatter around the threshold, distinguish a sustained musical note from amplifier noise, preserve bends/vibrato/legato/intentional feedback, reduce high-frequency hiss before cutting the tonal body, use a clean DI signal to control post-amplifier audio, compensate for latency between DI key and processed path, reject mains hum and pickup noise in the detector, remain usable while tracking at very low latency, and provide visual evidence of why it is opening and closing.

ChugGate is not a generic studio gate with a metal preset. Its detector, timing model, frequency topology, visual system, and factory profiles are designed around electric guitar and bass.

### 1.1 Core Philosophy

**Performance first.** The gate reacts to playing technique, not merely amplitude. The same level may represent a decaying musical note, amplifier hiss, pickup hum, a soft legato note, a muted string scrape, or intentional feedback. Deterministic signal analysis separates these cases.

**Fast must not mean crude.** Low latency does not justify hard discontinuities, clicks, arbitrary threshold switching, or unexplained adaptive behavior. Fast operation is professionally smoothed and reproducible.

**Noise disappears in layers.** A guitar note does not transition directly from full output to silence. A better decay: full note attack → normal note body → high-frequency hiss attenuation → gradual reduction of noisy sustain → broadband floor attenuation → silence. This layered process is a defining ChugGate capability.

**Automatic features remain editable.** When ChugGate recommends or controls threshold, hysteresis, key alignment, noise floor, release extension, or multiband depth, the user can always see and override these values.

**No cloud dependency.** All detection, learning, alignment, and noise analysis runs locally.

### 1.2 Suite Overlap Policy

- ChugGate handles gating, expansion, and noise suppression
- ShredBoost handles pre-amp tightening and nonlinear coloration — ChugGate does not distort
- ShredEQ handles equalization and spectral resonance — ChugGate does not shape tone
- Some overlap in palm-mute low-frequency control is acceptable for routing flexibility

### 1.3 Deliberate Non-Goals

ChugGate does not include: upward expansion, distortion, amp modeling, cabinet convolution, EQ-style tone design, stereo widening, artificial doubling, mastering limiting, drum de-bleeding, vocal presets, surround/Atmos, cloud classification, full feedback suppression, or conventional compression.

---

## 2. Architecture: Behavior × Topology

Rather than mutually exclusive modes, ChugGate separates two independent choices that combine freely.

### 2.1 Performance Behavior

- **Chug** — fast attack, short hold, cadence-aware release, strong transient guard, deep floor, quick high-band closure, reliable reopen
- **Lead** — sensitive legato reopen, harmonic sustain analysis, adaptive release, gentle high-band reduction, soft floor, feedback preservation
- **Hybrid** — automatically moves between rhythm-like and lead-like behavior based on onset density, harmonic stability, decay slope, note duration. Exposes current classification (rhythm/transition/sustain/feedback)
- **Manual** — disables adaptive timing. All attack/hold/release/floor operate directly. Retains hysteresis, detector conditioning, alignment, lookahead, multiband

### 2.2 Spectral Topology

- **Full Band** — one gain curve for entire signal. Light CPU, simple, complete stops
- **Dual Band** — Body + Hiss. Crossover 1.5–8 kHz. Hiss band can close earlier, faster, deeper while Body retains natural decay
- **Tri Band** — Weight + Voice + Hiss. Each band: threshold offset, attack offset, hold offset, release multiplier, floor, solo, mute, detector weighting
- **Hiss Curtain** — progressive high-frequency reduction based on SNR. Moving low-pass cutoff between f_min and f_max:

```
Q_H = clamp((SNR_H - S_min) / (S_max - S_min), 0, 1)
ln(f_c) = ln(f_min) + Q_H * (ln(f_max) - ln(f_min))
```

When high-frequency content is musically strong, cutoff stays high. As tail becomes hiss-dominated, cutoff moves down. Uses modulation-stable TPT filter with log-frequency smoothing.

---

## 3. Signal Architecture

### 3.1 High-Level Signal Flow

```
Main Input
  → Optional Main Lookahead Delay
  → Spectral Split (Full/Dual/Tri/Hiss Curtain)
  → Per-Band Gain Application
  → Stereo Link Matrix
  → Output Trim
  → Output

External DI / Internal Key
  → Source Selection and Blend
  → Key Pre-Conditioning (HPF, LPF, Emphasis)
  → Powerline Shield (Hum Rejection)
  → Key Delay / Alignment
  → Transient Detector
  → Level Detector (Peak + RMS blend)
  → Sustain Intelligence (Harmonicity, Decay, Spectral Stability)
  → Noise-Floor Comparison
  → Performance State Engine
  → Per-Band Gain Targets
  → Gain Smoothing
```

### 3.2 Operating Configurations

**Pre-Amp Gate:** Guitar DI → ChugGate → ShredBoost → amp sim. Main audio = detector source.

**Post-Amp Internal Gate:** Guitar → amp → cab → ChugGate. Processed audio feeds detector.

**DI-Keyed Post-Amp Gate:** Clean DI → sidechain detector. Processed amp/cab → main input. Cleanest detection, preserves attack after high-latency processing.

**Blended Key:** Detector combines internal audio + external DI + transient + sustain detectors. DI provides reliable attack, processed audio provides tail information.

### 3.3 Channel Layouts

Required: mono→mono, stereo→stereo, stereo main with mono/stereo external sidechain.

---

## 4. Detector Source System

### 4.1 Sources

Main input, external sidechain, blended main+external, left, right, stereo max, stereo average, mid, side.

### 4.2 Key Blending

Envelope blending (default for unrelated paths):

```
E_K[n] = (1 - mu) * E_I[n] + mu * E_E[n]     mu in [0, 1]
```

### 4.3 Source Confidence

UI indicates: external sidechain present/silent, channel mismatch, low confidence, internal fallback active, alignment not calibrated. Never silently treat a missing sidechain as valid.

---

## 5. Key-Path Pre-Conditioning

Independent processing that never changes audible tone.

### 5.1 High-Pass Filter

Adjustable 20–250 Hz, 12 or 24 dB/oct, profile-dependent default. Examples: 6-string 60–90 Hz, 7-string 40–70 Hz, 8-string 30–60 Hz, 5-string bass 20–40 Hz.

### 5.2 Low-Pass Filter

Optional 1.5–20 kHz, 12 or 24 dB/oct. Rejects interface hiss, pickup noise, digital artifacts, excessive pick scratch.

### 5.3 Detector Emphasis

Broad key bell or bandpass to emphasize the most reliable performance region. Controls: frequency, bandwidth, gain, solo key path.

### 5.4 Powerline Shield

Hum rejection notch filters: 50 Hz or 60 Hz mode (auto-detect), first harmonic + optional 2nd/3rd harmonic notches, adjustable Q, confidence display.

Notch coefficients (standard biquad):
```
w0 = 2*pi * f0/fs
alpha = sin(w0) / (2*Q)
b0 = 1,  b1 = -2*cos(w0),  b2 = 1
a0 = 1 + alpha,  a1 = -2*cos(w0),  a2 = 1 - alpha
```

Auto detector compares energy around 50 Hz and 60 Hz, checks harmonic consistency.

### 5.5 Hum Immunity

Subtracts hum-energy estimate from detector confidence:
```
E_musical = max(0, E_key - eta * E_hum)
```

Prevents steady low-frequency tone from holding gate open.

### 5.6 Key Listen Mode

Audition: raw key, conditioned key, aligned key, transient key, sustain key. Clearly indicated bypass of output gain.

---

## 6. Detector Engine

### 6.1 Peak and RMS Detection

```
Peak:  P[n] = max(|x[n]|, alpha_p * P[n-1])
Power: R²[n] = alpha_r * R²[n-1] + (1-alpha_r) * x²[n]
       R[n] = sqrt(R²[n] + eps)
Combined: E[n] = w_p * P[n] + (1-w_p) * R[n]
```

### 6.2 Attack and Release Ballistics

```
E[n] = alpha_a * E[n-1] + (1-alpha_a) * |x[n]|   when |x[n]| > E[n-1]
E[n] = alpha_r * E[n-1] + (1-alpha_r) * |x[n]|   when |x[n]| <= E[n-1]

alpha_a = exp(-1 / (fs * tau_a))
alpha_r = exp(-1 / (fs * tau_r))
```

Detector and audible gain ballistics are separate. A fast detector does not require an equally abrupt gain transition.

### 6.3 Log Level

```
L[n] = 20 * log10(E[n] + eps)
```

All threshold decisions in stable dB domain.

---

## 7. Performance State Engine

### 7.1 States

1. **Closed** — output at configured floor. Watching for threshold crossing, transient evidence, or trigger
2. **Primed** — likely onset detected, not yet validated. With lookahead, can begin opening before audible audio arrives
3. **Attack** — gain moving toward unity using configured attack law
4. **Open** — performance confidently active
5. **Hold** — detector below open threshold, minimum hold time not elapsed
6. **Sustain** — level may be low, but harmonic/decay evidence indicates continuing musical note
7. **Release** — gain moving toward floor
8. **Reopen** — new transient during release. Gain path reverses smoothly without resetting to zero or producing discontinuity. Essential for fast repeated notes

### 7.2 Dual-Threshold Hysteresis

```
T_close = T_open - H     where H in [0.5, 24] dB
```

Gate opens when `L_open_score >= T_open`. Remains open while `L_sustain_score > T_close`. Closing begins only when: hold expired, sustain protection inactive, no valid reopen transient, detector below closing condition.

### 7.3 Soft Decision Region

Smoothstep target between thresholds:
```
u = clamp((L - T_close) / (T_open - T_close), 0, 1)
S(u) = 3u² - 2u³
```

Hysteresis decides state; smooth curve avoids abrupt gain changes near threshold.

---

## 8. Pick Attack Guard

### 8.1 Purpose

Ensures soft picking opens reliably, sweep-picked notes aren't lost, legato transitions reopen, fast alternate picking doesn't lose transients, gate opens before audible attack when lookahead is enabled.

### 8.2 Band-Limited Transient Detection

```
d[n] = x_HP[n] - x_HP[n-1]
T[n] = max(|d[n]|, alpha_t * T[n-1])
O[n] = L[n] + G_t * clamp((T[n] - T0) / (T1 - T0), 0, 1)
```

Temporarily lowers effective opening threshold for detected pick events.

### 8.3 False-Trigger Protection

Valid attack requires both transient evidence AND minimum body energy within a short confirmation window. Rejects cable clicks, single-sample spikes, digital discontinuities.

### 8.4 Controls

Amount, sensitivity, frequency emphasis, decay, soft-note assist, maximum threshold reduction.

---

## 9. Sustain Intelligence

### 9.1 Purpose

Prevents quiet musical tails from being mistaken for noise. Pitch detection alone is insufficient for distorted guitar (harmonically dense, polyphonic chords, bends, vibrato, feedback). Combines multiple indicators.

### 9.2 Harmonicity

Given estimated fundamental f0:
```
H = sum_m(sum_{f near m*f0} P(f)) / (sum_f P(f) + eps)
```

Strong harmonic ratio supports musical sustain.

### 9.3 Spectral Stability

```
D_s[t] = sum_k |P_hat_t[k] - P_hat_{t-1}[k]|
```

Stable decaying note has lower spectral change than intermittent noise.

### 9.4 Decay Slope

```
m_E = dL/dt
```

Gradual negative slope consistent with note tail. Constant low-level floor with low harmonicity is more likely noise.

### 9.5 Composite Sustain Score

```
S_sustain = w_h*H + w_d*D + w_e*E - w_n*N
```

Where H=harmonicity, D=valid decay, E=external-key evidence, N=noise-likeness.

### 9.6 Adaptive Release

```
tau_release_eff = tau_base * [1 + A_s * S_sustain^gamma]
```

Clamped to configured maximum.

### 9.7 Bend and Vibrato Tolerance

Frequency smoothing in log space. System does not require exact pitch constancy — tolerates slow drift, periodic vibrato, semitone/whole-tone bends, glissando.

---

## 10. Feedback Friend

### 10.1 Purpose

Intentional controlled feedback is musically important. Detects stable/slowly moving harmonic energy, long duration, low onset rate, non-random spectral structure, flat or slowly rising level. Prevents closure or applies softer floor.

### 10.2 Confidence Score

```
F = w_h*H + w_s*(1-D_s) + w_g*G - w_t*T
```

H=harmonicity, D_s=spectral change, G=flat/positive gain slope, T=transient density.

### 10.3 Controls

Enable, sensitivity, minimum duration, maximum hold, allowed frequency range, floor during feedback, require external DI activity, safety timeout.

### 10.4 Safety

Maximum protection duration and minimum harmonicity are mandatory. Feedback Friend must not hold gate open indefinitely due to continuous hum, steady noise, stuck sidechain, or frozen detector.

---

## 11. Riff Cadence Engine

### 11.1 Purpose

Estimates spacing between recent attacks. Helps gate close cleanly before next note without rigid tempo sync. Useful for rubato riffs, changing subdivisions, unquantized performances, fast picking, varying rests.

### 11.2 Onset Interval Estimate

```
Intervals: delta_i = t_i - t_{i-1}
Robust estimate: I = median(delta_{N-k+1}, ..., delta_N)
```

Outliers rejected.

### 11.3 Release Constraint

```
tau_release_max = c * I     where c in [0.15, 0.85]
tau_release_eff = min(tau_manual, tau_release_max)
```

Does not quantize playing. Only prevents adaptive release from spilling into next expected gap.

### 11.4 Controls

Audio cadence, host tempo, combined, off.

---

## 12. Noise Learn

### 12.1 Workflow

1. User stops playing
2. Presses **Learn Noise**
3. ChugGate records several seconds
4. Measures broadband and per-band noise
5. Identifies hum, hiss, intermittent disturbances
6. Suggests: open/close thresholds, hysteresis, filter topology, high-band floor, hum rejection settings
7. User reviews and applies

### 12.2 Noise-Floor Estimation

For band b, log-energy observations L_b[t]:
```
N_b = percentile_p({L_b[t]})     p between 20-50%
T_b = N_b + M_b                   M_b = desired SNR margin
```

### 12.3 Optional Slow Adaptation

Updates only while gate is confidently closed:
```
N_b[t] = lambda * N_b[t-1] + (1-lambda) * L_b[t]
```

Rejected when transient activity, signal exceeds noise model, harmonicity suggests musical note, or external key active. Must not chase decaying notes.

### 12.4 Noise Profile Display

Shows: broadband floor, low hum, midrange buzz, HF hiss, intermittent spikes, confidence, capture duration. User can freeze, relearn, or reset.

---

## 13. DI Key Alignment

### 13.1 Purpose

Compensates timing differences between clean key and processed signal. Supports manual offset, auto calibration workflow, key delay or main-path lookahead, exact latency reporting.

### 13.2 Onset-Based Cross-Correlation

Onset envelope from conditioned energy:
```
p[n] = (1-alpha_p) * x²[n] + alpha_p * p[n-1]
E[n] = sqrt(p[n] + eps)
O[n] = max(0, E[n] - E[n-d])
```

Normalized cross-correlation at lag k:
```
R_KA[k] = (sum O_K[m] * O_A[m+k]) / (sqrt(sum O_K²[m] * sum O_A²[m+k]) + eps)
k* = argmax_k R_KA[k]
```

### 13.3 Delay Direction

DI arrives before processed → delay key path. DI arrives after → add main-path lookahead if enabled, otherwise warn.

### 13.4 Implementation

Integer delay via preallocated circular buffer. Range: ±25 ms default, ±100 ms extended. Manual trim resolution: 0.01 ms.

### 13.5 Calibration Workflow

Select key → press Align → play muted and open notes → ChugGate collects onset pairs → calculates robust median offset → displays confidence and variation → user accepts, retries, or manually trims → value becomes saved parameter.

### 13.6 Latency Reporting

Processor reports exact latency including any main-path delay for lookahead or negative DI skew compensation. Default mode reports zero samples. No hidden latency.

---

## 14. Crossover Design

Dual and tri-band modes use fourth-order Linkwitz-Riley crossovers (two cascaded 2nd-order Butterworth, Q = 1/sqrt(2)). Complementary reconstruction — unity-gain bands reproduce input. Click-free crossover movement, ordering protection, no hidden level shift.

Crossover ranges:

| Crossover     | Range        |
|---------------|-------------|
| Dual Band     | 1.5–8 kHz   |
| Tri Low       | 80–900 Hz   |
| Tri High      | 1.5–10 kHz  |
| Hiss Curtain min | 500 Hz–8 kHz |
| Hiss Curtain max | 3–20 kHz    |

---

## 15. Gate and Expansion Laws

### 15.1 Hard Gate

Target moves between unity and floor. For complete rhythm stops, extremely noisy rigs.

### 15.2 Soft Gate

Transition spread across threshold knee. For lead, bass, natural decay.

### 15.3 Downward Expander

```
G_exp_dB = max(G_floor_dB, (R-1) * (L-T))     where R > 1
```

1.5:1 gentle cleanup, 2-4:1 natural gating, high ratios for gate-like behavior.

### 15.4 Floor Range

0 dB (no attenuation) through −80 dB (practical mute):
```
g_floor = 10^(G_floor_dB / 20)
```

### 15.5 Curve Shape

```
g_shaped = g_floor + (1-g_floor) * S^gamma
```

gamma < 1: slower initial closure (Natural). gamma = 1: linear (Firm). gamma > 1: faster initial closure (Clamp).

---

## 16. Per-Band Gate Behavior

Each band receives target gain G_b*[n], smoothed with separate open/close constants:

```
alpha_b = exp(-1/(fs * tau_open_b))   when G_b*[n] > G_b[n-1]
alpha_b = exp(-1/(fs * tau_close_b))  when G_b*[n] <= G_b[n-1]

G_b[n] = alpha_b * G_b[n-1] + (1-alpha_b) * G_b*[n]
```

Output: `y[n] = sum_b G_b[n] * x_b[n]`

---

## 17. Lookahead

Available values: off, 0.25, 0.5, 1, 2, 5, 10 ms. Default off (zero reported latency).

```
N_L = round(tau_L * fs)
x_D[n] = x[n - N_L]
```

Reported latency includes main-path delay for lookahead and negative DI skew compensation. No hidden latency.

---

## 18. Stereo Processing

### 18.1 Linking Modes

- **Max**: `E_link = max(E_L, E_R)` — preserves stereo image, hard-panned rhythm buses
- **Energy**: `E_link = sqrt((E_L² + E_R²) / 2)`
- **Average**: `E_link = (E_L + E_R) / 2`
- **Dual Mono**: independent state and gain per channel

### 18.2 Link Percentage

```
G_L' = lambda * G_linked + (1-lambda) * G_L
G_R' = lambda * G_linked + (1-lambda) * G_R     lambda in [0, 1]
```

### 18.3 Mono Safety

Warns when dual-mono gating produces frequent L/R gain divergence on stereo bus.

---

## 19. Input Anomaly Protection

### 19.1 Cable Spike Guard

Identifies impulses with extreme peak, insufficient body, sub-minimum duration. Prevents false opening. Not a limiter.

### 19.2 Stuck-Key Detection

Warns when external key constantly active while main path silent/noise-like.

### 19.3 Dropout Handling

If sidechain disappears: hold current state briefly, crossfade to internal detection if fallback enabled, display failure clearly.

---

## 20. Visual Diagnostic System

### 20.1 Performance Timeline (Central Visualization)

Horizontal: recent time. Shows: conditioned key envelope, main audio envelope, open threshold, close threshold, noise-floor estimate, transient events, sustain confidence, gate state lane, output gain, lookahead region, alignment offset. Attenuation shaded between original and processed gain. Makes visible: missed attacks, excessive hold, chopped tails, false openings, late closure, threshold chatter, key/audio misalignment.

### 20.2 DI Alignment Scope

Key onset waveform, main-audio onset waveform, correlation curve, selected offset, confidence, offset variation, required key delay or main lookahead.

### 20.3 Noise Fingerprint

Time-averaged frequency display of learned idle noise: floor median, upper percentile, 50/60 Hz hum, harmonic mains, HF hiss, intermittent bursts, detector filter response, multiband thresholds.

### 20.4 State Indicator

More than a lamp. Shows current state, duration, current gain, reason, active protection:
```
OPEN — Pick transient
SUSTAIN — Harmonic tail (18 ms remaining)
RELEASE — Hiss band closing
CLOSED — Below learned noise margin
```

### 20.5 Rejected-Signal Audition

```
r[n] = x[n] - y[n]
```

Choices: all rejected, full-band, Weight/Voice/Hiss band, Hiss Curtain removal, threshold-transition region, noise model, detector key. State-triggered audition: hear only material removed during Hold, Sustain, Release, or Closed.

### 20.6 Analyzer Controls

Every visual layer individually switchable. Modes: Off, Live, Studio, Diagnose. Disabling reduces CPU materially.

---

## 21. GUI

### 21.1 Window

Default ~1280×760, min ~920×580, freely resizable, high-DPI aware, keyboard accessible, no hidden controls at minimum size.

### 21.2 Visual Identity

Fast, mechanical, precise, controlled — aggressive without being visually childish. Suite-consistent with ShredBoost/ShredEQ/ShredSpace. Dark neutral base, bright gate-state accents, separate key/audio traces, restrained animation, clear state transitions, high contrast around thresholds, color-blind-safe state symbols.

### 21.3 Main Layout

```
┌─────────────────────────────────────────────────────────┐
│ Preset | Profile | A/B | Key | Align | Learn | Bypass   │
├─────────────────────────────────────────────────────────┤
│            Performance Timeline / Analysis               │
├───────────────────────────┬─────────────────────────────┤
│ Threshold / Tightness     │ Sustain / Hiss / Depth      │
│ Attack Guard / Lookahead  │ Mode / Topology / State     │
├───────────────────────────┴─────────────────────────────┤
│ Expert Inspector / Multiband / Detector / Alignment      │
└─────────────────────────────────────────────────────────┘
```

### 21.4 Views

- **Perform** — fast controls + Performance Timeline
- **Shape** — full/dual/tri/hiss curtain controls, crossover, per-band settings
- **Detect** — key conditioning, hum rejection, threshold, transient, sustain analysis
- **Align** — DI alignment scope and lookahead
- **Diagnose** — noise fingerprint, rejected signal, statistics

### 21.5 Performance Page Controls

Threshold, Tightness (macro: hold + release + cadence + close curve + high-band timing), Sustain (macro: sustain score + max release extension + feedback + legato + body floor), Hiss (macro: high-band threshold offset + high-band floor + hiss curtain depth + high-band release + learned noise weighting), Depth (broadband floor), Attack Guard, mode selector, topology selector, key source, lookahead, Learn Noise, Align DI.

### 21.6 Expert Inspector

Expands to show all mapped parameter values beneath macros. No hidden parameters.

---

## 22. Gain Matching and Comparison

### 22.1 A/B States

Two complete configurations. Instant switch with click-safe crossfade, copy A→B, compare behavior modes and topologies.

### 22.2 Bypass

Click-free, latency-correct, state-preserving, automation-safe.

---

## 23. Profiles

Profiles change defaults and detector behavior. They do not impose hidden processing.

- **Tight Rhythm** — palm-muted, high gain, rapid stops, deep floor, strong transient guard
- **Low-Tuned Rhythm** — 7/8-string, baritone, Drop A and lower, slower LF envelopes, false-trigger prevention
- **Fast Alternate Picking** — high onset density, fast reopen, cadence-aware
- **Lead Sustain** — bends, vibrato, legato, harmonics, soft floor
- **Rhythm + Lead Hybrid** — full live or recorded performances
- **Controlled Feedback Lead** — held feedback, harmonic verification, safety timeout
- **Pre-Amp DI Gate** — before distortion
- **Post-Amp Hiss Control** — after amp and cabinet
- **DI-Keyed Reamp** — external sidechain, alignment, lookahead
- **Stereo Rhythm Bus** — linked stereo, hiss suppression, preserved image
- **Active Bass** — active electronics, hum rejection, moderate sustain
- **Distorted Bass** — clean DI key, multiband, preserved LF body

---

## 24. Factory Presets

Categories: Rhythm (tight, staccato, fast 16ths, palm-mute, low-tuned, 8-string, thrash, progressive, live safe), Lead (singing sustain, bend/vibrato safe, legato, feedback, shred, hiss curtain), Post-Amp (hiss suppression, cab fizz tail, post-NAM/TONEX, natural decay), DI-Keyed (rhythm, lead, reamp auto-align, high-latency chain), Stereo (linked double-track, stereo hiss, hard stop, natural decay), Bass (active, 5-string, distorted, picked, sustained, DI-keyed), Diagnostic (learn rig, find DI skew, inspect attack/tail damage, hear rejected, hum analysis).

Specific names and values designed after DSP is testable.

---

## 25. Tuning Integration

Factory tunings: E/Eb/D/C#/C/B Standard, Drop D/C#/C/B/A, 7-string standard+Drop A, 8-string standard+Drop E, 4/5/6-string bass, custom. Tuning adjusts: key HPF default, low-band crossover default, detector weighting, expected pitch range, sustain analysis bounds, hum-vs-fundamental discrimination. Does not hard-code gate to specific notes.

---

## 26. Parameter Ranges

| Parameter                | Range              |
|--------------------------|--------------------|
| Open threshold           | −100 to 0 dBFS    |
| Hysteresis               | 0.5–24 dB         |
| Attack                   | 0.01–50 ms        |
| Hold                     | 0–1000 ms         |
| Release                  | 2–5000 ms         |
| Max adaptive release     | 10–10000 ms       |
| Floor                    | −80 to 0 dB       |
| Downward ratio           | 1:1 to 100:1      |
| Knee                     | 0–24 dB           |
| Lookahead                | 0–10 ms           |
| Key alignment            | ±100 ms           |
| Key HPF                  | 20–250 Hz         |
| Key LPF                  | 1.5–20 kHz        |
| Detector emphasis        | 30 Hz–12 kHz      |
| Dual-band crossover      | 1.5–8 kHz         |
| Tri-band low crossover   | 80–900 Hz         |
| Tri-band high crossover  | 1.5–10 kHz        |
| Hiss Curtain min/max     | 500Hz–8k / 3–20k  |
| Stereo link              | 0–100%            |
| Cadence influence        | 0–100%            |
| Sustain influence        | 0–100%            |
| Attack Guard             | 0–100%            |

---

## 27. Parameter and State Model

### 27.1 Stable Parameter IDs

Stable from first release. Structure:

```
global.enabled, global.mode, global.topology
global.thresholdOpen, global.thresholdClose, global.hysteresis
global.attack, global.hold, global.release, global.maxAdaptiveRelease
global.floor, global.ratio, global.knee, global.curve, global.lookahead
key.source, key.externalBlend, key.hpfFrequency, key.lpfFrequency
key.emphasisFreq, key.emphasisGain, key.emphasisQ
key.humMode, key.humHarmonics, key.alignmentOffset
attackGuard.enabled, attackGuard.amount, attackGuard.frequency
sustain.enabled, sustain.amount, sustain.releaseExtension
sustain.harmonicWeight, sustain.decayWeight
sustain.feedbackEnabled, sustain.feedbackSensitivity
cadence.enabled, cadence.amount, cadence.releaseFraction
bandLow.floor, bandLow.thresholdOffset, bandLow.releaseMultiplier
bandMid.floor, bandMid.thresholdOffset, bandMid.releaseMultiplier
bandHigh.floor, bandHigh.thresholdOffset, bandHigh.releaseMultiplier
hissCurtain.minFreq, hissCurtain.maxFreq, hissCurtain.depth
stereo.linkMode, stereo.linkAmount
```

### 27.2 State Versioning

Saved state: schema version, all params, A/B states, learned noise profile, DI alignment, profile, tuning, analyzer settings, window layout. Future migration loads older presets.

### 27.3 Undo/Redo

All parameter edits, profile loads, learned setting application, alignment acceptance, mode changes.

---

## 28. Real-Time Implementation

### 28.1 Audio-Thread Rules

**No allocation, no deallocation, no file I/O, no mutex waits, no logging, no GUI access, no FFT planning, no container resizing, no state serialization, no network calls.**

Use `juce::ScopedNoDenormals` at top of processBlock. `juce::SmoothedValue` for all parameter reads. `juce::AbstractFifo` for audio→analysis transfer.

### 28.2 Analysis Transport

Lock-free SPSC FIFOs. Drop visual frames when overloaded, never block audio. Alignment cross-correlation runs outside audio thread — audio thread supplies bounded onset data, result applied via atomic/message-thread update.

### 28.3 Parameter Smoothing

Smooth: thresholds, gain floors, crossovers, alignment delay, output trim, sidechain blend, lookahead transitions, stereo link. Mode/topology changes: state-safe crossfade ~5–30 ms.

---

## 29. Latency

Default: `0 samples`. Covers all modes without lookahead. When lookahead enabled: exact reported samples, host PDC updated. No hidden latency.

---

## 30. Testing

### 30.1 State Machine

Every legal transition verified. No chatter, correct hold timing, threshold hysteresis, reopen continuity, floor behavior.

### 30.2 Attack Tests

Fixtures: hard palm mutes, soft picking, sweep picking, alternate picking, cable clicks, impulses. Valid attacks open, attack energy preserved, false triggers rejected, no clicks.

### 30.3 Rhythm Tests

Fixtures at 80/120/160/200/240 BPM: 8ths, 16ths, triplets, gallops, rubato. Gaps quiet, no first-transient loss, no release spill, cadence doesn't impose timing.

### 30.4 Lead Tests

Sustained notes, bends, vibrato, legato, harmonics, feedback, quiet notes after loud. Musical tails intact, feedback protection bounded, noise doesn't hold gate open.

### 30.5 Multiband

Flat reconstruction at unity, correct crossover magnitude/phase, click-free movement, independent floors, Hiss Curtain stability.

### 30.6 Alignment

Offsets −25 to +25 ms, noisy key, missing transients, polarity inversion. Accuracy, confidence, correct delay direction, exact host latency.

### 30.7 State Restoration

Preset save/load, A/B, noise profile, alignment, profile, topology, project reopen, schema migration.

### 30.8 Host Validation

Bitwig (Linux), REAPER (Linux), REAPER (Windows), pluginval, standalone. Mono/stereo, sidechain, automation, project reopen, offline render, bypass.

---

## 31. Release Criteria

Ships when: VST3 + standalone Release builds succeed, zero-latency mode reports 0, all lookahead values report exact latency, DI alignment works in supported hosts, missing sidechain safe, no audio-thread allocations, no opening/closing clicks, attack preservation passes fixtures, lead tails natural under Lead profiles, Chug closes cleanly between dense notes, adaptive release bounded, Powerline Shield handles 50/60 Hz, Noise Learn doesn't modify until applied, multiband reconstruction verified, stereo link preserves image, rejected-signal audition works in every topology, analyzer doesn't interrupt audio, disabled analyzers reduce CPU, all state restores, factory presets organized, GUI usable at minimum size and high DPI, all visuals disableable, validation scripts complete, no online service required.

---

## 32. Implementation Phasing (abracapocus)

| Phase | Focus                                    | Key Deliverables                                           |
|-------|------------------------------------------|------------------------------------------------------------|
| A     | Project scaffold + APVTS                 | CMakeLists, build scripts, parameter IDs, state engine     |
| B     | Detector engine + key conditioning       | Key routing, HPF/LPF, emphasis, Powerline Shield, hum      |
| C     | State machine + hysteresis + gain        | 8-state engine, dual threshold, gain computation/smoothing |
| D     | Attack Guard + Sustain Intelligence      | Transient detection, harmonicity, decay, Feedback Friend   |
| E     | Cadence + Noise Learn + alignment        | Riff cadence, noise-floor estimation, DI cross-correlation |
| F     | Multiband + Hiss Curtain                 | LR4 crossovers, dual/tri band, Hiss Curtain TPT filter    |
| G     | Lookahead + stereo + anomaly protection  | Delay lines, stereo linking, cable spike guard             |
| H     | Analyzer system                          | Performance Timeline, Noise Fingerprint, Alignment Scope   |
| I     | GUI                                      | LookAndFeel, pages, controls, state indicator, views       |
| J     | Profiles, presets, A/B, comparison       | All profiles, factory presets, rejected-signal audition     |
| K     | Validation + release                     | Test fixtures, validation scripts, release documentation   |

---

## Appendix A: v1.1 Features (Deferred)

### A.1 Fractional-Sample Key Delay

Lagrange or Thiran all-pass interpolation for sub-sample alignment precision.

### A.2 Continuous Drift Monitoring

Post-calibration offset monitoring with drift/recalibration-recommended indicators. Does not auto-move delay during performance.

### A.3 Spectral Tail Map

Frequency × time-after-onset display showing how guitar notes decay — low-frequency body early, midrange sustaining, HF hiss remaining.

### A.4 Attack and Tail Integrity Analyzers

Per-onset attack preservation ratio and per-tail musical-decay comparison.

### A.5 Cadence View

Visual onset markers, median interval, estimated density, release window, expected closure.

### A.6 Gain-Reduction Distribution Histogram

Recent attenuation histogram showing time in open/transition/closed.

### A.7 Full Periodicity Detection

YIN/MPM/AMDF periodicity scoring as additional sustain intelligence indicator.

---

## 33. Product Identity

ChugGate is a performance-aware guitar and bass gate that aligns clean DI triggers with processed amp audio, preserves attacks and musical sustain, removes hiss in frequency layers, and shows exactly why every note is being opened, held, or attenuated.

Defining capabilities:

1. **DI Key Auto-Alignment** — corrects latency between clean performance and processed amp path
2. **Independent Behavior × Spectral Modes** — Chug/Lead/Hybrid/Manual × Full/Dual/Tri/Hiss Curtain
3. **Performance State Engine** — 8 states: attack, open, hold, sustain, release, reopen, primed, closed
4. **Pick Attack Guard** — opens reliably on fast or quiet articulation without triggering on electrical spikes
5. **Sustain Intelligence** — harmonicity, spectral stability, and decay analysis rather than level alone
6. **Feedback Friend** — preserves intentional feedback with safety bounds
7. **Noise Learn** — measures the actual rig and recommends thresholds, hysteresis, hum filtering, spectral behavior
8. **Riff Cadence Engine** — adapts closure to player's note spacing without quantizing
9. **Multiband Tail Control** — removes HF hiss before cutting musical body
10. **Hiss Curtain** — progressive SNR-driven high-frequency reduction
11. **Performance Timeline** — key, audio, thresholds, noise floor, state, sustain confidence, and attenuation together
12. **Rejected-Signal Inspection** — hear and see exactly what is being removed
13. **Powerline Shield** — 50/60 Hz hum rejection with harmonic notches
14. **Professional Stereo Linking** — cleans rhythm buses without stereo wobble
15. **Zero-Latency Default** — operates at zero added latency, optional correctly reported lookahead
