# ChugGate Release Verification Checklist

This document defines the verification requirements and procedures for releasing ChugGate, mapped directly to the release criteria in `CHUGGATE_SPEC.md §31`.

## Purpose
To ensure that the plugin is sonically transparent, stable across target hosts, and functionally complete according to the technical specification before shipping.

## Automated Validation
The primary automated check is performed by `scripts/validate.sh`. This script handles build verification, unit/integration tests, and real-time safety analysis.

### How to Run
1. Ensure all build dependencies are installed.
2. Execute the validation script from the project root:
   ```bash
   ./scripts/validate.sh
   ```

### Expected Results
The script must exit with code 0. The summary should show:
- **Build**: VST3 and Standalone binaries created successfully.
- **CTest**: All tests in `ChugGateTests` passed (covers State Machine, Attack, Sustain, Cadence, Lookahead, Multiband, Stereo, Alignment, Analysis, and State Restoration).
- **RT-Safety**: No prohibited calls (allocations, I/O) detected in the audio thread.
- **Latency**: Zero-latency mode reports 0 samples; lookahead modes report exact sample counts.
- **Multiband**: Reconstruction verified as flat.
- **State**: Parameter and state round-trip verified.

---

## Verification Checklist

### 1. Automated / CI-Capable
These items are verified by `scripts/validate.sh` and are suitable for CI pipelines.

| Requirement (§31) | Verification Method | Pass Criteria |
| :--- | :--- | :--- |
| VST3 + Standalone Build | `validate.sh` (Build) | Binaries generate without errors |
| Zero-Latency Report | `validate.sh` (Latency) | `getLatencySamples()` returns 0 when lookahead is off |
| Lookahead Latency | `validate.sh` (Latency) | Reported latency matches `round(tau_L * fs)` |
| RT-Safety | `validate.sh` (RT-Scan) | Zero allocations/locks/IO in `processBlock` |
| Attack Preservation | `ctest` (AttackTests) | Fixtures (palm mutes, etc.) open reliably |
| Adaptive Release Bounds | `ctest` (SustainTests) | Release extension does not exceed `maxAdaptiveRelease` |
| Powerline Shield | `ctest` (DetectorTests) | 50/60 Hz hum rejected; musicality preserved |
| Noise Learn Logic | `ctest` (NoiseLearnTests) | Parameters only update upon explicit "Apply" |
| Multiband Reconstruction | `ctest` (MultibandGateTests) | Sum of bands = Input (Unity Gain / Flat) |
| Stereo Link Image | `ctest` (StereoTests) | L/R gain coherence maintained in linked modes |
| State Restoration | `ctest` (StateTests) | Save $\rightarrow$ Load results in identical parameters/profiles |
| Validation Scripts | Project State | `scripts/validate.sh` exists and passes |

### 2. Manual Host Validation
These items require human audition and interaction within a DAW. They cannot be fully automated in CI.

**Host Matrix:**
- Bitwig Studio (Linux)
- REAPER (Linux)
- REAPER (Windows)
- Standalone App

| Requirement (§31) | Pass Criteria | Host(s) |
| :--- | :--- | :--- |
| DI Alignment | Onset markers align perfectly; no perceived "pre-echo" or lag | All |
| Missing Sidechain Safety | Plugin falls back to internal detection or warns; no crashes/silence | All |
| Opening/Closing Clicks | No audible discontinuities during fast state transitions | All |
| Lead Tails | Natural decay under "Lead" profiles; no abrupt chopping | All |
| Rhythm Gaps | Clean silence between dense notes; no "leakage" or chatter | All |
| Rejected-Signal Audition | `x - y` signal is audible and matches expected rejected content | All |
| Analyzer Audio Impact | Audio remains glitch-free while Performance Timeline is active | All |
| Analyzer CPU | Measurable CPU drop when visuals are disabled | All |
| GUI Usability | No controls cut off at min size (~920x580); HiDPI scaling correct | All |
| Visuals Disableable | All analyzer layers can be toggled off | All |
| Factory Presets | All categories (Rhythm, Lead, Post-Amp, etc.) load and sound as intended | All |
| Offline Render | Gating behavior is consistent between real-time and offline bounce | Bitwig, REAPER |
| Project Reopen | State is preserved exactly after DAW project reload | Bitwig, REAPER |

---

## Final Declarations
- **Online Services**: This plugin is fully local. No internet connection, cloud classification, or online activation is required for any feature.
- **Dependencies**: All required components are bundled or standard system libraries.

## References
- Validation Script: [`scripts/validate.sh`](../scripts/validate.sh)
- Tech Spec: [`.abracapocus/CHUGGATE_SPEC.md`](../../.abracapocus/CHUGGATE_SPEC.md)
