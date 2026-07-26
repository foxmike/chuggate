#pragma once

// ============================================================================
// Tuning Profiles (CHUGGATE_SPEC.md §25)
// ----------------------------------------------------------------------------
// A *tuning* selects a documented set of defaults for the subset of parameters
// that depend on the instrument's open-string tuning. Tuning adjustments
// overlay a profile and may modify ONLY the following values:
//
//   * key HPF default                 (ParameterIDs::Key::hpfFrequency)
//   * low-band crossover default      (ParameterIDs::Crossovers::triLowFreq)
//   * detector weighting             (ParameterIDs::band*.detectorWeighting)
//   * expected pitch range            (TuningAdjustments::pitchMin/MaxHz —
//                                      a read-only descriptor for detector
//                                      modules; not an open/close rule)
//   * sustain analysis bounds          (ParameterIDs::Sustain::feedbackFreqMin
//                                      / feedbackFreqMax)
//   * hum-vs-fundamental discrimination
//                                     (ParameterIDs::Key::humMode /
//                                      Key::humImmunityAmount /
//                                      Key::humHarmonics)
//
// A tuning MUST NOT hard-code the gate to specific note names or MIDI pitches
// as open/close rules. Pitch range is supplied as advisory bounds for the
// detector (e.g. fundamental-estimator clamps and harmonic-verification
// windows); the open/close decision is always made by the threshold/envelope
// DSP from signal evidence.
//
// Composition order (documented):
//
//     1. Profiles::applyProfileDefaults()      scenario defaults
//     2. TuningProfiles::applyTuningAdjustments()  narrows the
//        tuning-sensitive subset listed above. A tuning never overrides
//        scenario-specific parameters (gain mode, topology, attack-guard
//        amount, etc.); it only re-narrows the tuning-sensitive defaults.
//
// Stable indices
// --------------
// The integer index is persisted in `ParameterIDs::tuningIndex` and in
// presets. The order below MUST NOT change after release.
//
//   0  E Standard         1  Eb Standard       2  D Standard
//   3  C# Standard        4  C Standard        5  B Standard
//   6  Drop D             7  Drop C#            8  Drop C
//   9  Drop B            10  Drop A            11 7-String Standard
//  12  7-String Drop A   13  8-String Standard 14  8-String Drop E
//  15  4-String Bass     16  5-String Bass     17  6-String Bass
//  18  Custom
// ============================================================================

#include "Profiles.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ChugGate::TuningProfiles {

// ------------------------------------------------------------------------- //
// Stable indices                                                            //
// ------------------------------------------------------------------------- //

enum class TuningIndex : int {
  EStandard = 0,
  EbStandard = 1,
  DStandard = 2,
  CSharpStandard = 3,
  CStandard = 4,
  BStandard = 5,
  DropD = 6,
  DropCSharp = 7,
  DropC = 8,
  DropB = 9,
  DropA = 10,
  SevenStringStandard = 11,
  SevenStringDropA = 12,
  EightStringStandard = 13,
  EightStringDropE = 14,
  FourStringBass = 15,
  FiveStringBass = 16,
  SixStringBass = 17,
  Custom = 18,
};

inline constexpr int kNumTunings = 19;

// ------------------------------------------------------------------------- //
// Catalog                                                                   //
// ------------------------------------------------------------------------- //

struct TuningInfo {
  int index;
  const char *stableId;
  const char *name;       // matches ParameterIDs::tuningIndex choice text
  const char *description;
};

const TuningInfo *infoByIndex(int index) noexcept;
const TuningInfo *infoByStableId(const char *stableId) noexcept;
const TuningInfo &infoFor(TuningIndex t) noexcept;
juce::StringArray listTuningNames() noexcept;
constexpr int tuningCount() noexcept { return kNumTunings; }

// ------------------------------------------------------------------------- //
// Adjustments                                                                //
// ------------------------------------------------------------------------- //
// `TuningAdjustments` carries both the values written to APVTS parameters
// and advisory detector descriptors. Advisory fields (pitchMin/MaxHz) are NOT
// used as open/close rules; they guide fundamental estimation and harmonic
// verification windows in the detector.
struct TuningAdjustments {
  // --- Written to APVTS parameters ----------------------------------------
  float keyHpfDefaultHz = 70.0f;        // Key::hpfFrequency
  float lowBandCrossoverHz = 250.0f;    // Crossovers::triLowFreq
  float detectorLowWeight = 1.0f;       // bandLow.detectorWeighting
  float detectorMidWeight = 1.0f;       // bandMid.detectorWeighting
  float detectorHighWeight = 1.0f;      // bandHigh.detectorWeighting
  float sustainMinHz = 80.0f;           // Sustain::feedbackFreqMin
  float sustainMaxHz = 4000.0f;         // Sustain::feedbackFreqMax
  int humMode = 3;                       // Key::humMode (0 Off,1 50Hz,
                                         //                  2 60Hz,3 Auto)
  float humImmunityAmount = 0.5f;       // Key::humImmunityAmount

  // --- Advisory detector descriptors (NOT open/close rules) --------------
  float pitchMinHz = 80.0f;            // expected lowest open-string f0
  float pitchMaxHz = 330.0f;            // expected highest open-string f0
};

// Returns the adjustments descriptor for a tuning index. Out-of-range input
// returns the "Custom" (neutral) adjustments.
TuningAdjustments adjustmentsFor(TuningIndex t) noexcept;
TuningAdjustments adjustmentsFor(int index) noexcept;

// Writes the tuning-sensitive subset of APVTS parameters from the
// adjustments descriptor for `t`. Returns the list of parameter IDs written.
juce::StringArray applyTuningAdjustments(
    TuningIndex t, juce::AudioProcessorValueTreeState &apvts);

// Writes the tuning-sensitive subset into a Profiles::ParameterMap (used when
// constructing presets without a live APVTS instance).
juce::StringArray applyTuningAdjustments(TuningIndex t,
                                         Profiles::ParameterMap &out);

// Integer-index overloads.
juce::StringArray applyTuningAdjustments(
    int index, juce::AudioProcessorValueTreeState &apvts);
juce::StringArray applyTuningAdjustments(int index,
                                         Profiles::ParameterMap &out);

} // namespace ChugGate::TuningProfiles
