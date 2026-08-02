#include "TuningProfiles.h"

#include "ParameterIDs.h"

#include <array>
#include <cstring>

namespace ChugGate::TuningProfiles {
namespace {

// ------------------------------------------------------------------------- //
// Catalog table                                                              //
// ------------------------------------------------------------------------- //

const TuningInfo kCatalog[kNumTunings] = {
    {0, "e_standard", "E Standard",
     "E A D G B E -- standard guitar tuning"},
    {1, "eb_standard", "Eb Standard",
     "Eb Ab Db Gb Bb Eb -- half-step down"},
    {2, "d_standard", "D Standard",
     "D G C F A D -- whole-step down"},
    {3, "csharp_standard", "C# Standard",
     "C# F# B E G# C#"},
    {4, "c_standard", "C Standard",
     "C F Bb Eb G C -- two whole steps down"},
    {5, "b_standard", "B Standard",
     "B E A D F# B -- baritone territory"},
    {6, "drop_d", "Drop D",
     "D A D G B E -- drop-D power-chord tuning"},
    {7, "drop_csharp", "Drop C#",
     "C# G# C# F# A# D#"},
    {8, "drop_c", "Drop C",
     "C G C F A D"},
    {9, "drop_b", "Drop B",
     "B F# B E G# C#"},
    {10, "drop_a", "Drop A",
     "A E A D F# B -- 7-string/Drop-A territory"},
    {11, "7_string_standard", "7-String Standard",
     "B E A D G B E"},
    {12, "7_string_drop_a", "7-String Drop A",
     "A E A D G B E"},
    {13, "8_string_standard", "8-String Standard",
     "E B E A D G B E"},
    {14, "8_string_drop_e", "8-String Drop E",
     "E A E A D G B E"},
    {15, "4_string_bass", "4-String Bass",
     "E A D G -- standard electric bass"},
    {16, "5_string_bass", "5-String Bass",
     "B E A D G -- low-B extension"},
    {17, "6_string_bass", "6-String Bass",
     "B E A D G C -- low-B + high-C extension"},
    {18, "custom", "Custom",
     "User-defined tuning; neutral adjustments"},
};

// ------------------------------------------------------------------------- //
// Tuning adjustments table                                                   //
// ------------------------------------------------------------------------- //
// Open-string fundamental ranges (lowest string f0 → highest open-string f0)
// are advisory detector bounds, NOT open/close rules. Key HPF defaults sit
// below the lowest open-string f0 so the key does not attenuate the
// fundamental. Detector weighting de-emphasizes the high band for low-tuned
// instruments where most useful energy lives in the low/mid bands.

const TuningAdjustments kAdjustments[kNumTunings] = {
    // E Standard: E2 82.41 – E4 329.63
    {70.0f, 250.0f, 1.0f, 0.9f, 0.85f, 80.0f, 4000.0f, 3, 0.5f, 82.41f, 329.63f},
    // Eb Standard: Eb2 77.78 – Eb4 311.13
    {65.0f, 250.0f, 1.0f, 0.9f, 0.85f, 78.0f, 4000.0f, 3, 0.5f, 77.78f, 311.13f},
    // D Standard: D2 73.42 – D4 293.66
    {60.0f, 240.0f, 1.0f, 0.9f, 0.8f, 75.0f, 3800.0f, 3, 0.5f, 73.42f, 293.66f},
    // C# Standard: C#2 69.30 – C#4 277.18
    {55.0f, 240.0f, 1.0f, 0.9f, 0.8f, 70.0f, 3600.0f, 3, 0.55f, 69.30f, 277.18f},
    // C Standard: C2 65.41 – C4 261.63
    {50.0f, 230.0f, 1.0f, 0.85f, 0.75f, 65.0f, 3400.0f, 3, 0.6f, 65.41f, 261.63f},
    // B Standard: B1 61.74 – B3 246.94
    {48.0f, 220.0f, 1.0f, 0.85f, 0.7f, 60.0f, 3200.0f, 3, 0.6f, 61.74f, 246.94f},
    // Drop D: D2 73.42 – E4 329.63
    {55.0f, 240.0f, 1.0f, 0.9f, 0.8f, 75.0f, 3800.0f, 3, 0.5f, 73.42f, 329.63f},
    // Drop C#: C#2 69.30 – Eb4 311.13
    {52.0f, 230.0f, 1.0f, 0.85f, 0.75f, 70.0f, 3600.0f, 3, 0.55f, 69.30f, 311.13f},
    // Drop C: C2 65.41 – D4 293.66
    {48.0f, 220.0f, 1.0f, 0.85f, 0.7f, 65.0f, 3400.0f, 3, 0.6f, 65.41f, 293.66f},
    // Drop B: B1 61.74 – Eb4 311.13
    {45.0f, 220.0f, 1.0f, 0.85f, 0.7f, 60.0f, 3200.0f, 3, 0.6f, 61.74f, 311.13f},
    // Drop A: A1 55.00 – E4 329.63
    {40.0f, 210.0f, 1.0f, 0.8f, 0.65f, 55.0f, 3000.0f, 3, 0.65f, 55.00f, 329.63f},
    // 7-String Standard: B1 61.74 – E4 329.63
    {48.0f, 220.0f, 1.0f, 0.85f, 0.7f, 60.0f, 3200.0f, 3, 0.6f, 61.74f, 329.63f},
    // 7-String Drop A: A1 55.00 – E4 329.63
    {40.0f, 210.0f, 1.0f, 0.8f, 0.65f, 55.0f, 3000.0f, 3, 0.65f, 55.00f, 329.63f},
    // 8-String Standard: E1 41.20 – E4 329.63
    {35.0f, 200.0f, 1.0f, 0.75f, 0.6f, 42.0f, 2800.0f, 3, 0.7f, 41.20f, 329.63f},
    // 8-String Drop E: E1 41.20 – E4 329.63
    {35.0f, 200.0f, 1.0f, 0.75f, 0.6f, 42.0f, 2800.0f, 3, 0.7f, 41.20f, 329.63f},
    // 4-String Bass: E1 41.20 – G2 98.00
    {30.0f, 300.0f, 1.0f, 0.6f, 0.4f, 42.0f, 1500.0f, 3, 0.6f, 41.20f, 98.00f},
    // 5-String Bass: B0 30.87 – G2 98.00
    {25.0f, 320.0f, 1.0f, 0.6f, 0.4f, 31.0f, 1500.0f, 3, 0.7f, 30.87f, 98.00f},
    // 6-String Bass: B0 30.87 – C3 130.81
    {25.0f, 320.0f, 1.0f, 0.6f, 0.4f, 31.0f, 1600.0f, 3, 0.7f, 30.87f, 130.81f},
    // Custom: neutral
    {20.0f, 250.0f, 1.0f, 1.0f, 1.0f, 30.0f, 5000.0f, 3, 0.5f, 30.0f, 400.0f},
};

// ------------------------------------------------------------------------- //
// APVTS write helper                                                         //
// ------------------------------------------------------------------------- //

void writeApvtsValue(juce::AudioProcessorValueTreeState &apvts,
                     const char *id, float denormalized) {
  if (auto *param = apvts.getParameter(id)) {
    const auto &range = param->getNormalisableRange();
    const float clamped = juce::jlimit(range.start, range.end, denormalized);
    const float normalized = range.convertTo0to1(clamped);
    param->setValueNotifyingHost(normalized);
  }
}

void writeApvtsChoice(juce::AudioProcessorValueTreeState &apvts,
                      const char *id, int index) {
  if (auto *param = apvts.getParameter(id)) {
    const auto &range = param->getNormalisableRange();
    const float clamped =
        juce::jlimit(range.start, range.end, static_cast<float>(index));
    const float normalized = range.convertTo0to1(clamped);
    param->setValueNotifyingHost(normalized);
  }
}

} // namespace

// ------------------------------------------------------------------------- //
// Catalog API                                                                //
// ------------------------------------------------------------------------- //

const TuningInfo *infoByIndex(int index) noexcept {
  if (index < 0 || index >= kNumTunings)
    return nullptr;
  return &kCatalog[index];
}

const TuningInfo *infoByStableId(const char *stableId) noexcept {
  if (stableId == nullptr)
    return nullptr;
  for (const auto &t : kCatalog)
    if (std::strcmp(t.stableId, stableId) == 0)
      return &t;
  return nullptr;
}

const TuningInfo &infoFor(TuningIndex t) noexcept {
  const auto i = static_cast<int>(t);
  return kCatalog[juce::jlimit(0, kNumTunings - 1, i)];
}

juce::StringArray listTuningNames() noexcept {
  juce::StringArray names;
  names.ensureStorageAllocated(kNumTunings);
  for (const auto &t : kCatalog)
    names.add(juce::String(t.name));
  return names;
}

// ------------------------------------------------------------------------- //
// Adjustments                                                                //
// ------------------------------------------------------------------------- //

TuningAdjustments adjustmentsFor(TuningIndex t) noexcept {
  const auto i = static_cast<int>(t);
  if (i < 0 || i >= kNumTunings)
    return kAdjustments[static_cast<int>(TuningIndex::Custom)];
  return kAdjustments[i];
}

TuningAdjustments adjustmentsFor(int index) noexcept {
  if (index < 0 || index >= kNumTunings)
    return kAdjustments[static_cast<int>(TuningIndex::Custom)];
  return kAdjustments[index];
}

// ------------------------------------------------------------------------- //
// Application                                                                //
// ------------------------------------------------------------------------- //
// Only the tuning-sensitive subset is written. No other parameters are
// touched; the open/close decision is never tied to a note name or MIDI
// pitch. Pitch range stays advisory (consumed by the detector outside this
// translation unit).

juce::StringArray applyTuningAdjustments(
    TuningIndex t, juce::AudioProcessorValueTreeState &apvts) {
  const auto a = adjustmentsFor(t);
  namespace ID = ParameterIDs;
  juce::StringArray written;

  writeApvtsValue(apvts, ID::Key::hpfFrequency, a.keyHpfDefaultHz);
  written.add(juce::String(ID::Key::hpfFrequency));

  writeApvtsValue(apvts, ID::Crossovers::triLowFreq, a.lowBandCrossoverHz);
  written.add(juce::String(ID::Crossovers::triLowFreq));

  writeApvtsValue(apvts, ID::bandLow.detectorWeighting, a.detectorLowWeight);
  written.add(juce::String(ID::bandLow.detectorWeighting));

  writeApvtsValue(apvts, ID::bandMid.detectorWeighting, a.detectorMidWeight);
  written.add(juce::String(ID::bandMid.detectorWeighting));

  writeApvtsValue(apvts, ID::bandHigh.detectorWeighting, a.detectorHighWeight);
  written.add(juce::String(ID::bandHigh.detectorWeighting));

  writeApvtsValue(apvts, ID::Sustain::feedbackFreqMin, a.sustainMinHz);
  written.add(juce::String(ID::Sustain::feedbackFreqMin));

  writeApvtsValue(apvts, ID::Sustain::feedbackFreqMax, a.sustainMaxHz);
  written.add(juce::String(ID::Sustain::feedbackFreqMax));

  writeApvtsChoice(apvts, ID::Key::humMode, a.humMode);
  written.add(juce::String(ID::Key::humMode));

  writeApvtsValue(apvts, ID::Key::humImmunityAmount, a.humImmunityAmount);
  written.add(juce::String(ID::Key::humImmunityAmount));

  return written;
}

juce::StringArray applyTuningAdjustments(TuningIndex t,
                                         Profiles::ParameterMap &out) {
  const auto a = adjustmentsFor(t);
  namespace ID = ParameterIDs;
  juce::StringArray written;

  out[juce::String(ID::Key::hpfFrequency)] = a.keyHpfDefaultHz;
  written.add(juce::String(ID::Key::hpfFrequency));

  out[juce::String(ID::Crossovers::triLowFreq)] = a.lowBandCrossoverHz;
  written.add(juce::String(ID::Crossovers::triLowFreq));

  out[juce::String(ID::bandLow.detectorWeighting)] = a.detectorLowWeight;
  written.add(juce::String(ID::bandLow.detectorWeighting));

  out[juce::String(ID::bandMid.detectorWeighting)] = a.detectorMidWeight;
  written.add(juce::String(ID::bandMid.detectorWeighting));

  out[juce::String(ID::bandHigh.detectorWeighting)] = a.detectorHighWeight;
  written.add(juce::String(ID::bandHigh.detectorWeighting));

  out[juce::String(ID::Sustain::feedbackFreqMin)] = a.sustainMinHz;
  written.add(juce::String(ID::Sustain::feedbackFreqMin));

  out[juce::String(ID::Sustain::feedbackFreqMax)] = a.sustainMaxHz;
  written.add(juce::String(ID::Sustain::feedbackFreqMax));

  out[juce::String(ID::Key::humMode)] = static_cast<float>(a.humMode);
  written.add(juce::String(ID::Key::humMode));

  out[juce::String(ID::Key::humImmunityAmount)] = a.humImmunityAmount;
  written.add(juce::String(ID::Key::humImmunityAmount));

  return written;
}

juce::StringArray applyTuningAdjustments(
    int index, juce::AudioProcessorValueTreeState &apvts) {
  if (index < 0 || index >= kNumTunings)
    return {};
  return applyTuningAdjustments(static_cast<TuningIndex>(index), apvts);
}

juce::StringArray applyTuningAdjustments(int index,
                                         Profiles::ParameterMap &out) {
  if (index < 0 || index >= kNumTunings)
    return {};
  return applyTuningAdjustments(static_cast<TuningIndex>(index), out);
}

} // namespace ChugGate::TuningProfiles
