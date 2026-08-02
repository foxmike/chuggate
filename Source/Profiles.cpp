#include "Profiles.h"

#include "ParameterIDs.h"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace ChugGate::Profiles {
namespace {

// ------------------------------------------------------------------------- //
// Catalog table                                                              //
// ------------------------------------------------------------------------- //

const ProfileInfo kCatalog[kNumProfiles] = {
    {0, "tight_rhythm", "Tight Rhythm",
     "Palm-muted, high gain, rapid stops, deep floor, strong transient guard"},
    {1, "low_tuned_rhythm", "Low-Tuned Rhythm",
     "7/8-string, baritone, Drop A and lower; slower LF envelopes, "
     "false-trigger prevention"},
    {2, "fast_alternate_picking", "Fast Alternate Picking",
     "High onset density, fast reopen, cadence-aware"},
    {3, "lead_sustain", "Lead Sustain",
     "Bends, vibrato, legato, harmonics, soft floor"},
    {4, "rhythm_lead_hybrid", "Rhythm + Lead Hybrid",
     "Full live or recorded performances"},
    {5, "controlled_feedback_lead", "Controlled Feedback Lead",
     "Held feedback, harmonic verification, safety timeout"},
    {6, "pre_amp_di", "Pre-Amp DI Gate",
     "Before distortion"},
    {7, "post_amp_hiss", "Post-Amp Hiss Control",
     "After amp and cabinet"},
    {8, "di_keyed_reamp", "DI-Keyed Reamp",
     "External sidechain, alignment, lookahead"},
    {9, "stereo_rhythm_bus", "Stereo Rhythm Bus",
     "Linked stereo, hiss suppression, preserved image"},
    {10, "active_bass", "Active Bass",
     "Active electronics, hum rejection, moderate sustain"},
    {11, "distorted_bass", "Distorted Bass",
     "Clean DI key, multiband, preserved LF body"},
};

// ------------------------------------------------------------------------- //
// Per-profile documented default overrides                                   //
// ------------------------------------------------------------------------- //
// Each entry is {ParameterID, denormalized value}. Values are the defaults a
// profile requests; they are written through the normal APVTS parameter
// interface so the runtime detector picks them up identically to a user
// edit. No hidden processing path is introduced.
//
// Indices used for choice parameters:
//   Global::mode        0=Chug 1=Lead 2=Hybrid 3=Manual
//   Global::topology    0=FullBand 1=DualBand 2=TriBand 3=HissCurtain
//   Global::gainMode    0=HardGate 1=SoftGate 2=DownwardExpander
//   Global::lookahead   0=Off 1=0.25ms 2=0.5ms 3=1ms 4=2ms 5=5ms 6=10ms
//   Key::source         0=Main 1=External 2=Blended 3=Left 4=Right
//                       5=StereoMax 6=StereoAverage 7=Mid 8=Side
//   Key::humMode        0=Off 1=50Hz 2=60Hz 3=Auto
//   Key::humHarmonics   0=Fund 1=Fund+2nd 2=Fund+2nd+3rd
//   Key::fallbackMode   0=Internal 1=Hold 2=Close
//   Stereo::linkMode    0=Max 1=Energy 2=Average 3=DualMono
using Override = std::pair<const char *, float>;

std::vector<Override> overridesFor(ProfileIndex p) {
  namespace ID = ParameterIDs;
  std::vector<Override> o;
  o.reserve(28);

  switch (p) {
  case ProfileIndex::TightRhythm:
    o = {
        {ID::Global::mode, 0.0f},         // Chug
        {ID::Global::topology, 0.0f},     // FullBand
        {ID::Global::gainMode, 0.0f},     // HardGate
        {ID::Global::thresholdOpen, -45.0f},
        {ID::Global::hysteresis, 8.0f},
        {ID::Global::attack, 0.5f},
        {ID::Global::hold, 20.0f},
        {ID::Global::release, 60.0f},
        {ID::Global::floor, -80.0f},
        {ID::Global::ratio, 100.0f},
        {ID::Global::knee, 0.0f},
        {ID::Global::lookahead, 3.0f}, // 1 ms
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 70.0f},
        {ID::Key::lpfEnabled, 1.0f},
        {ID::Key::lpfFrequency, 8000.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.85f},
        {ID::AttackGuard::sensitivity, 0.6f},
        {ID::AttackGuard::maxThreshReduction, 15.0f},
        {ID::Cadence::enabled, 1.0f},
        {ID::Cadence::amount, 0.6f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.3f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.8f},
        {ID::bandHigh.detectorWeighting, 0.7f},
    };
    break;

  case ProfileIndex::LowTunedRhythm:
    o = {
        {ID::Global::mode, 0.0f},
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 0.0f},
        {ID::Global::thresholdOpen, -48.0f},
        {ID::Global::hysteresis, 6.0f},
        {ID::Global::attack, 1.0f},
        {ID::Global::hold, 50.0f},
        {ID::Global::release, 150.0f},
        {ID::Global::floor, -75.0f},
        {ID::Global::ratio, 50.0f},
        {ID::Global::knee, 2.0f},
        {ID::Global::lookahead, 4.0f}, // 2 ms
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 50.0f},
        {ID::Key::lpfEnabled, 1.0f},
        {ID::Key::lpfFrequency, 6000.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.7f},
        {ID::AttackGuard::sensitivity, 0.5f},
        {ID::AttackGuard::frequency, 1200.0f},
        {ID::Cadence::enabled, 1.0f},
        {ID::Cadence::amount, 0.5f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.4f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.7f},
        {ID::bandHigh.detectorWeighting, 0.5f},
    };
    break;

  case ProfileIndex::FastAlternatePicking:
    o = {
        {ID::Global::mode, 0.0f},
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 0.0f},
        {ID::Global::thresholdOpen, -45.0f},
        {ID::Global::hysteresis, 4.0f},
        {ID::Global::attack, 0.2f},
        {ID::Global::hold, 5.0f},
        {ID::Global::release, 40.0f},
        {ID::Global::floor, -70.0f},
        {ID::Global::lookahead, 2.0f}, // 0.5 ms
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 70.0f},
        {ID::Key::lpfEnabled, 1.0f},
        {ID::Key::lpfFrequency, 10000.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.8f},
        {ID::AttackGuard::sensitivity, 0.7f},
        {ID::Cadence::enabled, 1.0f},
        {ID::Cadence::amount, 0.8f},
        {ID::Cadence::releaseFraction, 0.4f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.2f},
        {ID::bandLow.detectorWeighting, 0.9f},
        {ID::bandMid.detectorWeighting, 1.0f},
        {ID::bandHigh.detectorWeighting, 0.9f},
    };
    break;

  case ProfileIndex::LeadSustain:
    o = {
        {ID::Global::mode, 1.0f},         // Lead
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 1.0f},     // SoftGate
        {ID::Global::thresholdOpen, -50.0f},
        {ID::Global::hysteresis, 3.0f},
        {ID::Global::attack, 2.0f},
        {ID::Global::hold, 80.0f},
        {ID::Global::release, 400.0f},
        {ID::Global::floor, -30.0f},     // soft floor
        {ID::Global::ratio, 6.0f},
        {ID::Global::knee, 6.0f},
        {ID::Global::lookahead, 0.0f},
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 80.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.5f},
        {ID::AttackGuard::softNoteAssist, 0.7f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.8f},
        {ID::Sustain::releaseExtension, 1.5f},
        {ID::Sustain::harmonicWeight, 0.7f},
        {ID::Cadence::enabled, 1.0f},
        {ID::Cadence::amount, 0.2f},
        {ID::bandLow.detectorWeighting, 0.7f},
        {ID::bandMid.detectorWeighting, 0.9f},
        {ID::bandHigh.detectorWeighting, 1.0f},
    };
    break;

  case ProfileIndex::RhythmLeadHybrid:
    o = {
        {ID::Global::mode, 2.0f},         // Hybrid
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 2.0f},      // DownwardExpander
        {ID::Global::thresholdOpen, -46.0f},
        {ID::Global::hysteresis, 6.0f},
        {ID::Global::attack, 1.0f},
        {ID::Global::hold, 40.0f},
        {ID::Global::release, 180.0f},
        {ID::Global::floor, -50.0f},
        {ID::Global::ratio, 10.0f},
        {ID::Global::knee, 4.0f},
        {ID::Global::lookahead, 3.0f}, // 1 ms
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 70.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.7f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.6f},
        {ID::Cadence::enabled, 1.0f},
        {ID::Cadence::amount, 0.5f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.9f},
        {ID::bandHigh.detectorWeighting, 0.8f},
    };
    break;

  case ProfileIndex::ControlledFeedbackLead:
    o = {
        {ID::Global::mode, 1.0f},
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 1.0f},     // SoftGate
        {ID::Global::thresholdOpen, -52.0f},
        {ID::Global::hysteresis, 2.5f},
        {ID::Global::attack, 5.0f},
        {ID::Global::hold, 200.0f},
        {ID::Global::release, 600.0f},
        {ID::Global::floor, -20.0f},
        {ID::Global::ratio, 4.0f},
        {ID::Global::knee, 8.0f},
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 90.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.3f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 1.0f},
        {ID::Sustain::harmonicWeight, 0.9f},
        {ID::Sustain::feedbackEnabled, 1.0f},
        {ID::Sustain::feedbackSensitivity, 0.7f},
        {ID::Sustain::feedbackMinDuration, 800.0f},
        {ID::Sustain::feedbackSafetyTimeout, 8000.0f},
        {ID::bandLow.detectorWeighting, 0.6f},
        {ID::bandMid.detectorWeighting, 0.9f},
        {ID::bandHigh.detectorWeighting, 1.0f},
    };
    break;

  case ProfileIndex::PreAmpDI:
    o = {
        {ID::Global::mode, 0.0f},
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 0.0f},     // HardGate
        {ID::Global::thresholdOpen, -55.0f},
        {ID::Global::hysteresis, 10.0f},
        {ID::Global::attack, 0.5f},
        {ID::Global::hold, 15.0f},
        {ID::Global::release, 50.0f},
        {ID::Global::floor, -80.0f},
        {ID::Global::ratio, 100.0f},
        {ID::Global::knee, 0.0f},
        {ID::Global::lookahead, 1.0f}, // 0.25 ms
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 70.0f},
        {ID::Key::lpfEnabled, 1.0f},
        {ID::Key::lpfFrequency, 7000.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.6f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.2f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.85f},
        {ID::bandHigh.detectorWeighting, 0.7f},
    };
    break;

  case ProfileIndex::PostAmpHiss:
    o = {
        {ID::Global::mode, 1.0f},          // Lead
        {ID::Global::topology, 3.0f},      // HissCurtain
        {ID::Global::gainMode, 1.0f},      // SoftGate
        {ID::Global::thresholdOpen, -60.0f},
        {ID::Global::hysteresis, 4.0f},
        {ID::Global::attack, 1.0f},
        {ID::Global::hold, 30.0f},
        {ID::Global::release, 200.0f},
        {ID::Global::floor, -40.0f},
        {ID::Global::ratio, 8.0f},
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 100.0f},
        {ID::HissCurtain::depth, 0.9f},
        {ID::HissCurtain::minFreq, 2000.0f},
        {ID::HissCurtain::maxFreq, 12000.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.4f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.4f},
        {ID::bandLow.detectorWeighting, 0.7f},
        {ID::bandMid.detectorWeighting, 0.85f},
        {ID::bandHigh.detectorWeighting, 1.0f},
    };
    break;

  case ProfileIndex::DIKeyedReamp:
    o = {
        {ID::Global::mode, 0.0f},
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 0.0f},
        {ID::Global::thresholdOpen, -50.0f},
        {ID::Global::hysteresis, 6.0f},
        {ID::Global::attack, 0.5f},
        {ID::Global::hold, 20.0f},
        {ID::Global::release, 80.0f},
        {ID::Global::floor, -70.0f},
        {ID::Global::lookahead, 4.0f}, // 2 ms
        {ID::Key::source, 1.0f},        // External Sidechain
        {ID::Key::externalBlend, 1.0f},
        {ID::Key::alignmentOffset, 0.0f},
        {ID::Key::fallbackMode, 0.0f},   // Internal Key
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 70.0f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.7f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::feedbackRequireExtKey, 1.0f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.85f},
        {ID::bandHigh.detectorWeighting, 0.7f},
    };
    break;

  case ProfileIndex::StereoRhythmBus:
    o = {
        {ID::Global::mode, 2.0f},         // Hybrid
        {ID::Global::topology, 3.0f},     // HissCurtain
        {ID::Global::gainMode, 1.0f},     // SoftGate
        {ID::Global::thresholdOpen, -48.0f},
        {ID::Global::hysteresis, 6.0f},
        {ID::Global::attack, 1.0f},
        {ID::Global::hold, 30.0f},
        {ID::Global::release, 120.0f},
        {ID::Global::floor, -55.0f},
        {ID::Stereo::linkMode, 1.0f},     // Energy
        {ID::Stereo::linkAmount, 1.0f},
        {ID::Key::source, 5.0f},          // Stereo Max
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 70.0f},
        {ID::HissCurtain::depth, 0.6f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.3f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.85f},
        {ID::bandHigh.detectorWeighting, 0.8f},
    };
    break;

  case ProfileIndex::ActiveBass:
    o = {
        {ID::Global::mode, 0.0f},
        {ID::Global::topology, 0.0f},
        {ID::Global::gainMode, 2.0f},      // DownwardExpander
        {ID::Global::thresholdOpen, -50.0f},
        {ID::Global::hysteresis, 5.0f},
        {ID::Global::attack, 1.5f},
        {ID::Global::hold, 40.0f},
        {ID::Global::release, 250.0f},
        {ID::Global::floor, -45.0f},
        {ID::Global::ratio, 8.0f},
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 40.0f},
        {ID::Key::humMode, 3.0f},           // Auto
        {ID::Key::humHarmonics, 2.0f},
        {ID::Key::humNotchQ, 25.0f},
        {ID::Key::humImmunityAmount, 0.7f},
        {ID::AttackGuard::enabled, 1.0f},
        {ID::AttackGuard::amount, 0.6f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.5f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.6f},
        {ID::bandHigh.detectorWeighting, 0.4f},
    };
    break;

  case ProfileIndex::DistortedBass:
    o = {
        {ID::Global::mode, 0.0f},
        {ID::Global::topology, 2.0f},      // TriBand
        {ID::Global::gainMode, 1.0f},       // SoftGate
        {ID::Global::thresholdOpen, -48.0f},
        {ID::Global::hysteresis, 6.0f},
        {ID::Global::attack, 1.5f},
        {ID::Global::hold, 60.0f},
        {ID::Global::release, 300.0f},
        {ID::Global::floor, -35.0f},
        {ID::Global::ratio, 6.0f},
        {ID::Key::source, 1.0f},            // External (clean DI key)
        {ID::Key::externalBlend, 1.0f},
        {ID::Key::hpfEnabled, 1.0f},
        {ID::Key::hpfFrequency, 30.0f},
        {ID::Key::humMode, 3.0f},
        {ID::Key::humImmunityAmount, 0.5f},
        {ID::Crossovers::triLowFreq, 200.0f},
        {ID::Crossovers::triHighFreq, 3000.0f},
        {ID::Sustain::enabled, 1.0f},
        {ID::Sustain::amount, 0.6f},
        {ID::bandLow.detectorWeighting, 1.0f},
        {ID::bandMid.detectorWeighting, 0.7f},
        {ID::bandHigh.detectorWeighting, 0.5f},
    };
    break;
  }

  return o;
}

DetectorWeights weightsFor(ProfileIndex p) noexcept {
  switch (p) {
  case ProfileIndex::TightRhythm:
    return {1.0f, 0.8f, 0.7f, 0.6f, 0.3f, 0.85f};
  case ProfileIndex::LowTunedRhythm:
    return {1.0f, 0.7f, 0.5f, 0.5f, 0.4f, 0.7f};
  case ProfileIndex::FastAlternatePicking:
    return {0.9f, 1.0f, 0.9f, 0.8f, 0.2f, 0.8f};
  case ProfileIndex::LeadSustain:
    return {0.7f, 0.9f, 1.0f, 0.2f, 0.8f, 0.5f};
  case ProfileIndex::RhythmLeadHybrid:
    return {1.0f, 0.9f, 0.8f, 0.5f, 0.6f, 0.7f};
  case ProfileIndex::ControlledFeedbackLead:
    return {0.6f, 0.9f, 1.0f, 0.1f, 1.0f, 0.3f};
  case ProfileIndex::PreAmpDI:
    return {1.0f, 0.85f, 0.7f, 0.4f, 0.2f, 0.6f};
  case ProfileIndex::PostAmpHiss:
    return {0.7f, 0.85f, 1.0f, 0.3f, 0.4f, 0.4f};
  case ProfileIndex::DIKeyedReamp:
    return {1.0f, 0.85f, 0.7f, 0.4f, 0.3f, 0.7f};
  case ProfileIndex::StereoRhythmBus:
    return {1.0f, 0.85f, 0.8f, 0.4f, 0.3f, 0.6f};
  case ProfileIndex::ActiveBass:
    return {1.0f, 0.6f, 0.4f, 0.3f, 0.5f, 0.6f};
  case ProfileIndex::DistortedBass:
    return {1.0f, 0.7f, 0.5f, 0.3f, 0.6f, 0.5f};
  }
  return {};
}

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

} // namespace

// ------------------------------------------------------------------------- //
// Catalog API                                                                //
// ------------------------------------------------------------------------- //

const ProfileInfo *infoByIndex(int index) noexcept {
  if (index < 0 || index >= kNumProfiles)
    return nullptr;
  return &kCatalog[index];
}

const ProfileInfo *infoByStableId(const char *stableId) noexcept {
  if (stableId == nullptr)
    return nullptr;
  for (const auto &p : kCatalog)
    if (std::strcmp(p.stableId, stableId) == 0)
      return &p;
  return nullptr;
}

const ProfileInfo &infoFor(ProfileIndex p) noexcept {
  const auto i = static_cast<int>(p);
  return kCatalog[juce::jlimit(0, kNumProfiles - 1, i)];
}

juce::StringArray listProfileNames() noexcept {
  juce::StringArray names;
  names.ensureStorageAllocated(kNumProfiles);
  for (const auto &p : kCatalog)
    names.add(juce::String(p.name));
  return names;
}

// ------------------------------------------------------------------------- //
// Detector weights                                                           //
// ------------------------------------------------------------------------- //

DetectorWeights detectorWeightsFor(ProfileIndex p) noexcept {
  return weightsFor(p);
}

DetectorWeights detectorWeightsFor(int index) noexcept {
  if (index < 0 || index >= kNumProfiles)
    return {};
  return weightsFor(static_cast<ProfileIndex>(index));
}

// ------------------------------------------------------------------------- //
// Default application                                                         //
// ------------------------------------------------------------------------- //

juce::StringArray applyProfileDefaults(ProfileIndex p,
                                       juce::AudioProcessorValueTreeState &apvts) {
  juce::StringArray written;
  for (const auto &ov : overridesFor(p)) {
    writeApvtsValue(apvts, ov.first, ov.second);
    written.add(juce::String(ov.first));
  }
  return written;
}

juce::StringArray applyProfileDefaults(ProfileIndex p, ParameterMap &out) {
  juce::StringArray written;
  for (const auto &ov : overridesFor(p)) {
    out[juce::String(ov.first)] = ov.second;
    written.add(juce::String(ov.first));
  }
  return written;
}

juce::StringArray applyProfileDefaults(int index,
                                       juce::AudioProcessorValueTreeState &apvts) {
  if (index < 0 || index >= kNumProfiles)
    return {};
  return applyProfileDefaults(static_cast<ProfileIndex>(index), apvts);
}

juce::StringArray applyProfileDefaults(int index, ParameterMap &out) {
  if (index < 0 || index >= kNumProfiles)
    return {};
  return applyProfileDefaults(static_cast<ProfileIndex>(index), out);
}

} // namespace ChugGate::Profiles
