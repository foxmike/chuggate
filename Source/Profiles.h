#pragma once

// ============================================================================
// Profiles (CHUGGATE_SPEC.md §23)
// ----------------------------------------------------------------------------
// A *profile* selects a documented set of parameter defaults and detector
// behavior weights for a common playing scenario. Profiles NEVER introduce
// hidden processing paths: every value a profile changes is an exposed
// APVTS parameter (see Source/ParameterIDs.h). Detector behavior is shaped
// only through the exposed detector-weighting / cadence / sustain / attack
// guard parameters and the read-only DetectorWeights descriptor below.
//
// Stable indices
// --------------
// The integer index of each profile is the value persisted in the
// `ParameterIDs::profileIndex` choice parameter and in presets. The order
// below MUST NOT change after release; new profiles append at the end.
//
//   0  TightRhythm              palm-muted, high gain, rapid stops,
//                               deep floor, strong transient guard
//   1  LowTunedRhythm           7/8-string, baritone, Drop A and lower,
//                               slower LF envelopes, false-trigger prevention
//   2  FastAlternatePicking     high onset density, fast reopen, cadence-aware
//   3  LeadSustain              bends, vibrato, legato, harmonics, soft floor
//   4  RhythmLeadHybrid         full live or recorded performances
//   5  ControlledFeedbackLead   held feedback, harmonic verification,
//                               safety timeout
//   6  PreAmpDI                 before distortion
//   7  PostAmpHiss              after amp and cabinet
//   8  DIKeyedReamp             external sidechain, alignment, lookahead
//   9  StereoRhythmBus          linked stereo, hiss suppression, preserved image
//  10  ActiveBass               active electronics, hum rejection, moderate sustain
//  11  DistortedBass            clean DI key, multiband, preserved LF body
//
// Composition with tunings
// ------------------------
// Tuning adjustments (Source/TuningProfiles.h) overlay a profile. The
// documented application order is:
//
//     1. applyProfileDefaults()      sets the scenario defaults
//     2. applyTuningAdjustments()    narrows tuning-sensitive defaults
//                                    (key HPF, low-band crossover, detector
//                                     weighting, sustain bounds, hum
//                                     discrimination)
//
// A tuning never overrides scenario-specific parameters such as gain mode,
// topology, or attack-guard amount; it only re-narrows the tuning-sensitive
// subset listed above.
// ============================================================================

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

namespace ChugGate::Profiles {

// ------------------------------------------------------------------------- //
// Stable indices                                                            //
// ------------------------------------------------------------------------- //

enum class ProfileIndex : int {
  TightRhythm = 0,
  LowTunedRhythm = 1,
  FastAlternatePicking = 2,
  LeadSustain = 3,
  RhythmLeadHybrid = 4,
  ControlledFeedbackLead = 5,
  PreAmpDI = 6,
  PostAmpHiss = 7,
  DIKeyedReamp = 8,
  StereoRhythmBus = 9,
  ActiveBass = 10,
  DistortedBass = 11,
};

inline constexpr int kNumProfiles = 12;
inline constexpr int kInvalidProfile = -1;

// ------------------------------------------------------------------------- //
// Catalog                                                                   //
// ------------------------------------------------------------------------- //

struct ProfileInfo {
  int index;
  const char *stableId; // short, stable identifier for presets/reports
  const char *name;     // display name (matches ParameterIDs::profileIndex)
  const char *intent;   // one-line intent from CHUGGATE_SPEC.md §23
};

// Returns the info record for a valid profile index.
// `index` is clamped; out-of-range input returns nullptr.
const ProfileInfo *infoByIndex(int index) noexcept;

// Lookup by stable identifier (case-sensitive). Returns nullptr if unknown.
const ProfileInfo *infoByStableId(const char *stableId) noexcept;

// Convenience overload.
const ProfileInfo &infoFor(ProfileIndex p) noexcept;

// Display names in stable index order, suitable for a choice parameter or UI.
juce::StringArray listProfileNames() noexcept;

// Number of profiles in the catalog.
constexpr int profileCount() noexcept { return kNumProfiles; }

// ------------------------------------------------------------------------- //
// Detector behavior weights                                                 //
// ------------------------------------------------------------------------- //
// Read-only descriptor of the detector shaping a profile asks for. These
// values are also written to the corresponding APVTS parameters by
// applyProfileDefaults(); the struct is exposed for diagnostic / reporting
// use and for detector modules that read behavior weights directly.
struct DetectorWeights {
  // Per-band detector emphasis in [0,1] (mirrors band*.detectorWeighting).
  float low = 1.0f;
  float mid = 1.0f;
  float high = 1.0f;

  // Influence amounts in [0,1] (mirror Cadence::amount / Sustain::amount).
  float cadenceInfluence = 0.5f;
  float sustainInfluence = 0.5f;

  // Attack Guard amount in [0,1] (mirrors AttackGuard::amount).
  float attackGuardAmount = 0.75f;
};

DetectorWeights detectorWeightsFor(ProfileIndex p) noexcept;
DetectorWeights detectorWeightsFor(int index) noexcept;

// ------------------------------------------------------------------------- //
// Applying profile defaults                                                  //
// ------------------------------------------------------------------------- //
// A `ParameterMap` maps ParameterIDs strings to denormalized parameter
// values (the same units a host sees). It is used when defaults must be
// computed without a live APVTS instance (e.g. preset construction).
using ParameterMap = std::map<juce::String, float>;

// Writes the documented default-parameter overrides for `p` to `apvts`.
// Only documented parameters are touched; no hidden processing paths are
// introduced. Returns the list of parameter IDs that were written, in the
// order they were applied.
juce::StringArray applyProfileDefaults(ProfileIndex p,
                                       juce::AudioProcessorValueTreeState &apvts);

// Writes the documented default-parameter overrides for `p` into `out`.
// Existing entries are overwritten; entries for parameters not owned by the
// profile are left untouched. Returns the list of parameter IDs written.
juce::StringArray applyProfileDefaults(ProfileIndex p, ParameterMap &out);

// Integer-index overloads (e.g. for binding to the choice parameter).
juce::StringArray applyProfileDefaults(int index,
                                       juce::AudioProcessorValueTreeState &apvts);
juce::StringArray applyProfileDefaults(int index, ParameterMap &out);

} // namespace ChugGate::Profiles
