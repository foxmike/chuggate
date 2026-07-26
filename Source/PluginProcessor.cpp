#include "PluginProcessor.h"

#include "ParameterIDs.h"
#include "StateManager.h"

#if __has_include("PluginEditor.h")
#include "PluginEditor.h"
#define CHUGGATE_HAS_CUSTOM_EDITOR 1
#else
#define CHUGGATE_HAS_CUSTOM_EDITOR 0
#endif

#include <memory>
#include <utility>

namespace {
constexpr auto stateTreeType = "ChugGateState";
constexpr auto schemaVersionProperty = "schema_version";

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::ParameterID parameterID(const char *id) {
  return {id, ChugGate::ParameterIDs::schemaVersion};
}

void addBool(Layout &layout, const char *id, const char *name,
             bool defaultValue) {
  layout.add(std::make_unique<juce::AudioParameterBool>(parameterID(id), name,
                                                        defaultValue));
}

void addChoice(Layout &layout, const char *id, const char *name,
               juce::StringArray choices, int defaultIndex) {
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      parameterID(id), name, std::move(choices), defaultIndex));
}

void addFloat(Layout &layout, const char *id, const char *name, float minimum,
              float maximum, float interval, float defaultValue,
              const char *unit = "", float skewCentre = 0.0f) {
  juce::NormalisableRange<float> range(minimum, maximum, interval);

  if (skewCentre > minimum && skewCentre < maximum)
    range.setSkewForCentre(skewCentre);

  layout.add(std::make_unique<juce::AudioParameterFloat>(
      parameterID(id), name, range, defaultValue, unit));
}

void addBandParameters(Layout &layout, const ChugGate::ParameterIDs::Band &ids,
                       const juce::String &displayName) {
  addFloat(layout, ids.floor, (displayName + " Floor").toRawUTF8(), -80.0f,
           0.0f, 0.1f, -80.0f, "dB");
  addFloat(layout, ids.thresholdOffset,
           (displayName + " Threshold Offset").toRawUTF8(), -24.0f, 24.0f, 0.1f,
           0.0f, "dB");
  addFloat(layout, ids.attackOffset,
           (displayName + " Attack Offset").toRawUTF8(), -50.0f, 50.0f, 0.01f,
           0.0f, "ms");
  addFloat(layout, ids.holdOffset, (displayName + " Hold Offset").toRawUTF8(),
           -1000.0f, 1000.0f, 0.1f, 0.0f, "ms");
  addFloat(layout, ids.releaseMultiplier,
           (displayName + " Release Multiplier").toRawUTF8(), 0.1f, 4.0f, 0.01f,
           1.0f, "x");
  addBool(layout, ids.solo, (displayName + " Solo").toRawUTF8(), false);
  addBool(layout, ids.mute, (displayName + " Mute").toRawUTF8(), false);
  addFloat(layout, ids.detectorWeighting,
           (displayName + " Detector Weighting").toRawUTF8(), 0.0f, 1.0f, 0.01f,
           1.0f);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout
ChugGate::createParameterLayout() {
  namespace IDs = ParameterIDs;

  Layout layout;

  addBool(layout, IDs::Global::enabled, "Enabled", true);
  addChoice(layout, IDs::Global::mode, "Mode",
            {"Chug", "Lead", "Hybrid", "Manual"}, 0);
  addChoice(layout, IDs::Global::topology, "Topology",
            {"FullBand", "DualBand", "TriBand", "HissCurtain"}, 0);
  addChoice(layout, IDs::Global::gainMode, "Gain Mode",
            {"HardGate", "SoftGate", "DownwardExpander"}, 0);
  addFloat(layout, IDs::Global::thresholdOpen, "Open Threshold", -100.0f, 0.0f,
           0.1f, -45.0f, "dBFS");
  addFloat(layout, IDs::Global::hysteresis, "Hysteresis", 0.5f, 24.0f, 0.1f,
           6.0f, "dB");
  addFloat(layout, IDs::Global::attack, "Attack", 0.01f, 50.0f, 0.01f, 1.0f,
           "ms", 5.0f);
  addFloat(layout, IDs::Global::hold, "Hold", 0.0f, 1000.0f, 0.1f, 40.0f, "ms",
           100.0f);
  addFloat(layout, IDs::Global::release, "Release", 2.0f, 5000.0f, 0.1f, 100.0f,
           "ms", 250.0f);
  addFloat(layout, IDs::Global::maxAdaptiveRelease, "Maximum Adaptive Release",
           10.0f, 10000.0f, 0.1f, 2000.0f, "ms", 1000.0f);
  addFloat(layout, IDs::Global::floor, "Floor", -80.0f, 0.0f, 0.1f, -80.0f,
           "dB");
  addFloat(layout, IDs::Global::ratio, "Ratio", 1.0f, 100.0f, 0.1f, 4.0f, ":1",
           4.0f);
  addFloat(layout, IDs::Global::knee, "Knee", 0.0f, 24.0f, 0.1f, 6.0f, "dB");
  addFloat(layout, IDs::Global::curve, "Curve", 0.25f, 4.0f, 0.01f, 1.0f);
  addChoice(layout, IDs::Global::lookahead, "Lookahead",
            {"Off", "0.25 ms", "0.5 ms", "1 ms", "2 ms", "5 ms", "10 ms"}, 0);
  addFloat(layout, IDs::Global::outputTrim, "Output Trim", -24.0f, 12.0f, 0.1f,
           0.0f, "dB");
  addBool(layout, IDs::Global::bypass, "Bypass", false);

  addChoice(layout, IDs::Key::source, "Key Source",
            {"Main Input", "External Sidechain", "Blended", "Left", "Right",
             "Stereo Max", "Stereo Average", "Mid", "Side"},
            0);
  addFloat(layout, IDs::Key::externalBlend, "External Key Blend", 0.0f, 1.0f,
           0.01f, 1.0f);
  addBool(layout, IDs::Key::hpfEnabled, "Key HPF Enabled", true);
  addFloat(layout, IDs::Key::hpfFrequency, "Key HPF Frequency", 20.0f, 250.0f,
           0.1f, 70.0f, "Hz", 70.0f);
  addChoice(layout, IDs::Key::hpfSlope, "Key HPF Slope",
            {"12 dB/oct", "24 dB/oct"}, 0);
  addBool(layout, IDs::Key::lpfEnabled, "Key LPF Enabled", false);
  addFloat(layout, IDs::Key::lpfFrequency, "Key LPF Frequency", 1500.0f,
           20000.0f, 1.0f, 12000.0f, "Hz", 6000.0f);
  addChoice(layout, IDs::Key::lpfSlope, "Key LPF Slope",
            {"12 dB/oct", "24 dB/oct"}, 0);
  addFloat(layout, IDs::Key::emphasisFreq, "Key Emphasis Frequency", 30.0f,
           12000.0f, 0.1f, 1200.0f, "Hz", 1200.0f);
  addFloat(layout, IDs::Key::emphasisGain, "Key Emphasis Gain", -18.0f, 18.0f,
           0.1f, 0.0f, "dB");
  addFloat(layout, IDs::Key::emphasisQ, "Key Emphasis Q", 0.1f, 20.0f, 0.01f,
           1.0f, "", 1.0f);
  addChoice(layout, IDs::Key::humMode, "Hum Mode",
            {"Off", "50Hz", "60Hz", "Auto"}, 0);
  addChoice(layout, IDs::Key::humHarmonics, "Hum Harmonics",
            {"Fundamental", "Fundamental + 2nd", "Fundamental + 2nd + 3rd"}, 0);
  addFloat(layout, IDs::Key::humNotchQ, "Hum Notch Q", 1.0f, 50.0f, 0.1f, 20.0f,
           "", 10.0f);
  addFloat(layout, IDs::Key::humImmunityAmount, "Hum Immunity", 0.0f, 1.0f,
           0.01f, 0.5f);
  addFloat(layout, IDs::Key::alignmentOffset, "Key Alignment Offset", -100.0f,
           100.0f, 0.01f, 0.0f, "ms");
  addChoice(layout, IDs::Key::fallbackMode, "Missing Sidechain Fallback",
            {"Internal Key", "Hold State", "Close Gate"}, 0);
  addChoice(layout, IDs::Key::listenMode, "Key Listen Mode",
            {"Off", "Raw Key", "Conditioned Key", "Aligned Key",
             "Transient Key", "Sustain Key"},
            0);

  addBool(layout, IDs::AttackGuard::enabled, "Attack Guard Enabled", true);
  addFloat(layout, IDs::AttackGuard::amount, "Attack Guard Amount", 0.0f, 1.0f,
           0.01f, 0.75f);
  addFloat(layout, IDs::AttackGuard::sensitivity, "Attack Guard Sensitivity",
           0.0f, 1.0f, 0.01f, 0.5f);
  addFloat(layout, IDs::AttackGuard::frequency, "Attack Guard Frequency", 30.0f,
           12000.0f, 0.1f, 1800.0f, "Hz", 1800.0f);
  addFloat(layout, IDs::AttackGuard::decay, "Attack Guard Decay", 1.0f, 500.0f,
           0.1f, 40.0f, "ms", 50.0f);
  addFloat(layout, IDs::AttackGuard::softNoteAssist, "Soft Note Assist", 0.0f,
           1.0f, 0.01f, 0.5f);
  addFloat(layout, IDs::AttackGuard::maxThresholdReduction,
           "Maximum Threshold Reduction", 0.0f, 36.0f, 0.1f, 12.0f, "dB");
  addFloat(layout, IDs::AttackGuard::confirmationWindow,
           "Attack Confirmation Window", 0.1f, 50.0f, 0.1f, 5.0f, "ms", 5.0f);

  addBool(layout, IDs::Sustain::enabled, "Sustain Enabled", true);
  addFloat(layout, IDs::Sustain::amount, "Sustain Amount", 0.0f, 1.0f, 0.01f,
           0.5f);
  addFloat(layout, IDs::Sustain::releaseExtension, "Sustain Release Extension",
           0.0f, 10.0f, 0.01f, 2.0f, "x");
  addFloat(layout, IDs::Sustain::harmonicWeight, "Sustain Harmonic Weight",
           0.0f, 1.0f, 0.01f, 0.5f);
  addFloat(layout, IDs::Sustain::decayWeight, "Sustain Decay Weight", 0.0f,
           1.0f, 0.01f, 0.3f);
  addFloat(layout, IDs::Sustain::noiseWeight, "Sustain Noise Weight", 0.0f,
           1.0f, 0.01f, 0.5f);
  addBool(layout, IDs::Sustain::feedbackEnabled, "Feedback Friend Enabled",
          false);
  addFloat(layout, IDs::Sustain::feedbackSensitivity, "Feedback Sensitivity",
           0.0f, 1.0f, 0.01f, 0.5f);
  addFloat(layout, IDs::Sustain::feedbackMinDuration,
           "Feedback Minimum Duration", 0.0f, 10000.0f, 1.0f, 500.0f, "ms",
           1000.0f);
  addFloat(layout, IDs::Sustain::feedbackMaxHold, "Feedback Maximum Hold", 0.0f,
           30000.0f, 1.0f, 5000.0f, "ms", 5000.0f);
  addFloat(layout, IDs::Sustain::feedbackFreqMin, "Feedback Minimum Frequency",
           20.0f, 5000.0f, 0.1f, 80.0f, "Hz", 200.0f);
  addFloat(layout, IDs::Sustain::feedbackFreqMax, "Feedback Maximum Frequency",
           100.0f, 20000.0f, 1.0f, 8000.0f, "Hz", 4000.0f);
  addFloat(layout, IDs::Sustain::feedbackFloor, "Feedback Floor", -80.0f, 0.0f,
           0.1f, -12.0f, "dB");
  addBool(layout, IDs::Sustain::feedbackRequireExtKey,
          "Feedback Requires External Key", false);
  addFloat(layout, IDs::Sustain::feedbackSafetyTimeout,
           "Feedback Safety Timeout", 100.0f, 60000.0f, 1.0f, 10000.0f, "ms",
           10000.0f);

  addBool(layout, IDs::Cadence::enabled, "Cadence Enabled", true);
  addFloat(layout, IDs::Cadence::amount, "Cadence Amount", 0.0f, 1.0f, 0.01f,
           0.5f);
  addFloat(layout, IDs::Cadence::releaseFraction, "Cadence Release Fraction",
           0.15f, 0.85f, 0.01f, 0.5f);
  addChoice(layout, IDs::Cadence::sourceMode, "Cadence Source",
            {"Audio", "HostTempo", "Combined", "Off"}, 0);

  addFloat(layout, IDs::NoiseLearn::snrMargin, "Noise Learn SNR Margin", 0.0f,
           36.0f, 0.1f, 12.0f, "dB");
  addBool(layout, IDs::NoiseLearn::adaptiveEnabled, "Adaptive Noise Enabled",
          false);
  addFloat(layout, IDs::NoiseLearn::adaptiveRate, "Adaptive Noise Rate", 0.0f,
           1.0f, 0.001f, 0.05f);

  addBandParameters(layout, IDs::bandLow, "Low Band");
  addBandParameters(layout, IDs::bandMid, "Mid Band");
  addBandParameters(layout, IDs::bandHigh, "High Band");

  addFloat(layout, IDs::HissCurtain::minFreq, "Hiss Curtain Minimum Frequency",
           500.0f, 8000.0f, 1.0f, 1500.0f, "Hz", 2000.0f);
  addFloat(layout, IDs::HissCurtain::maxFreq, "Hiss Curtain Maximum Frequency",
           3000.0f, 20000.0f, 1.0f, 16000.0f, "Hz", 10000.0f);
  addFloat(layout, IDs::HissCurtain::depth, "Hiss Curtain Depth", 0.0f, 1.0f,
           0.01f, 0.75f);

  addChoice(layout, IDs::Stereo::linkMode, "Stereo Link Mode",
            {"Max", "Energy", "Average", "DualMono"}, 0);
  addFloat(layout, IDs::Stereo::linkAmount, "Stereo Link Amount", 0.0f, 1.0f,
           0.01f, 1.0f);

  addFloat(layout, IDs::Crossovers::dualFreq, "Dual Band Crossover", 1500.0f,
           8000.0f, 1.0f, 4000.0f, "Hz", 4000.0f);
  addFloat(layout, IDs::Crossovers::triLowFreq, "Tri Band Low Crossover", 80.0f,
           900.0f, 0.1f, 250.0f, "Hz", 250.0f);
  addFloat(layout, IDs::Crossovers::triHighFreq, "Tri Band High Crossover",
           1500.0f, 10000.0f, 1.0f, 4500.0f, "Hz", 4500.0f);

  addChoice(layout, IDs::Audition::rejectedMode, "Rejected Signal Audition",
            {"Off", "All", "FullBand", "Weight", "Voice", "Hiss", "HissCurtain",
             "NoiseModel", "Key"},
            0);
  addChoice(layout, IDs::Audition::stateFilterMode, "Audition State Filter",
            {"Off", "Hold", "Sustain", "Release", "Closed"}, 0);

  addChoice(layout, IDs::profileIndex, "Profile",
            {"Tight Rhythm", "Low-Tuned Rhythm", "Fast Alternate Picking",
             "Lead Sustain", "Rhythm + Lead Hybrid", "Controlled Feedback Lead",
             "Pre-Amp DI Gate", "Post-Amp Hiss Control", "DI-Keyed Reamp",
             "Stereo Rhythm Bus", "Active Bass", "Distorted Bass"},
            0);
  addChoice(layout, IDs::tuningIndex, "Tuning",
            {"E Standard", "Eb Standard", "D Standard", "C# Standard",
             "C Standard", "B Standard", "Drop D", "Drop C#", "Drop C",
             "Drop B", "Drop A", "7-String Standard", "7-String Drop A",
             "8-String Standard", "8-String Drop E", "4-String Bass",
             "5-String Bass", "6-String Bass", "Custom"},
            0);
  addChoice(layout, IDs::abState, "A/B State", {"A", "B"}, 0);

  return layout;
}

ChugGateAudioProcessor::ChugGateAudioProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, stateTreeType, ChugGate::createParameterLayout())
{
    stateManager = std::make_unique<ChugGate::StateManager>(*this);

    // Initialize parameter pointers
    params.enabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::enabled);
    params.bypass = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::bypass);
    params.outputTrim = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::outputTrim);
    params.listenMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::listenMode);
    params.thresholdOpen = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::thresholdOpen);
    params.hysteresis = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::hysteresis);
    params.attack = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::attack);
    params.hold = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::hold);
    params.release = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::release);
    params.floor = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::floor);
    params.ratio = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::ratio);
    params.knee = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::knee);
    params.curve = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::curve);
    params.gainMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::gainMode);
    
    params.keySource = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::source);
    params.externalBlend = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::externalBlend);
    params.fallbackMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::fallbackMode);
    params.hpfEnabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::hpfEnabled);
    params.hpfFrequency = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::hpfFrequency);
    params.hpfSlope = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::hpfSlope);
    params.lpfEnabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::lpfEnabled);
    params.lpfFrequency = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::lpfFrequency);
    params.lpfSlope = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::lpfSlope);
    params.emphasisFreq = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::emphasisFreq);
    params.emphasisGain = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::emphasisGain);
    params.emphasisQ = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::emphasisQ);
    params.humMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::humMode);
    params.humHarmonics = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::humHarmonics);
    params.humNotchQ = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::humNotchQ);
    params.humImmunityAmount = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::humImmunityAmount);
    params.maxAdaptiveRelease = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::maxAdaptiveRelease);

    // Attack Guard parameters
    params.attackGuardEnabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::enabled);
    params.attackGuardAmount = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::amount);
    params.attackGuardSensitivity = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::sensitivity);
    params.attackGuardFrequency = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::frequency);
    params.attackGuardDecay = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::decay);
    params.attackGuardSoftNoteAssist = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::softNoteAssist);
    params.attackGuardMaxThresholdReduction = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::maxThresholdReduction);
    params.attackGuardConfirmationWindow = apvts.getRawParameterValue(ChugGate::ParameterIDs::AttackGuard::confirmationWindow);

    // Sustain Analyzer parameters
    params.sustainEnabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::enabled);
    params.sustainAmount = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::amount);
    params.sustainReleaseExtension = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::releaseExtension);
    params.sustainHarmonicWeight = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::harmonicWeight);
    params.sustainDecayWeight = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::decayWeight);
    params.sustainNoiseWeight = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::noiseWeight);

    // Feedback Friend parameters
    params.feedbackEnabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackEnabled);
    params.feedbackSensitivity = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackSensitivity);
    params.feedbackMinDuration = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackMinDuration);
    params.feedbackMaxHold = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackMaxHold);
    params.feedbackFreqMin = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackFreqMin);
    params.feedbackFreqMax = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackFreqMax);
    params.feedbackFloor = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackFloor);
    params.feedbackRequireExtKey = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackRequireExtKey);
    params.feedbackSafetyTimeout = apvts.getRawParameterValue(ChugGate::ParameterIDs::Sustain::feedbackSafetyTimeout);

    // Phase E parameters
    params.cadenceEnabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::Cadence::enabled);
    params.cadenceAmount = apvts.getRawParameterValue(ChugGate::ParameterIDs::Cadence::amount);
    params.cadenceReleaseFraction = apvts.getRawParameterValue(ChugGate::ParameterIDs::Cadence::releaseFraction);
    params.cadenceSourceMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Cadence::sourceMode);

    params.noiseSnrMargin = apvts.getRawParameterValue(ChugGate::ParameterIDs::NoiseLearn::snrMargin);
    params.noiseAdaptiveEnabled = apvts.getRawParameterValue(ChugGate::ParameterIDs::NoiseLearn::adaptiveEnabled);
    params.noiseAdaptiveRate = apvts.getRawParameterValue(ChugGate::ParameterIDs::NoiseLearn::adaptiveRate);

    params.alignmentOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::Key::alignmentOffset);
    params.lookahead = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::lookahead);

    // Phase F parameter bindings
    params.topology = apvts.getRawParameterValue(ChugGate::ParameterIDs::Global::topology);
    params.dualCrossoverFreq = apvts.getRawParameterValue(ChugGate::ParameterIDs::Crossovers::dualFreq);
    params.triLowCrossoverFreq = apvts.getRawParameterValue(ChugGate::ParameterIDs::Crossovers::triLowFreq);
    params.triHighCrossoverFreq = apvts.getRawParameterValue(ChugGate::ParameterIDs::Crossovers::triHighFreq);

    params.hissCurtainMinFreq = apvts.getRawParameterValue(ChugGate::ParameterIDs::HissCurtain::minFreq);
    params.hissCurtainMaxFreq = apvts.getRawParameterValue(ChugGate::ParameterIDs::HissCurtain::maxFreq);
    params.hissCurtainDepth = apvts.getRawParameterValue(ChugGate::ParameterIDs::HissCurtain::depth);

    // Phase G parameter bindings
    params.linkMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Stereo::linkMode);
    params.linkAmount = apvts.getRawParameterValue(ChugGate::ParameterIDs::Stereo::linkAmount);
    params.rejectedMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Audition::rejectedMode);
    params.stateFilterMode = apvts.getRawParameterValue(ChugGate::ParameterIDs::Audition::stateFilterMode);

    // Band Low
    params.bandLow.floor = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.floor);
    params.bandLow.thresholdOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.thresholdOffset);
    params.bandLow.attackOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.attackOffset);
    params.bandLow.holdOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.holdOffset);
    params.bandLow.releaseMultiplier = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.releaseMultiplier);
    params.bandLow.solo = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.solo);
    params.bandLow.mute = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.mute);
    params.bandLow.detectorWeighting = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandLow.detectorWeighting);

    // Band Mid
    params.bandMid.floor = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.floor);
    params.bandMid.thresholdOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.thresholdOffset);
    params.bandMid.attackOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.attackOffset);
    params.bandMid.holdOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.holdOffset);
    params.bandMid.releaseMultiplier = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.releaseMultiplier);
    params.bandMid.solo = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.solo);
    params.bandMid.mute = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.mute);
    params.bandMid.detectorWeighting = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandMid.detectorWeighting);

    // Band High
    params.bandHigh.floor = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.floor);
    params.bandHigh.thresholdOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.thresholdOffset);
    params.bandHigh.attackOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.attackOffset);
    params.bandHigh.holdOffset = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.holdOffset);
    params.bandHigh.releaseMultiplier = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.releaseMultiplier);
    params.bandHigh.solo = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.solo);
    params.bandHigh.mute = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.mute);
    params.bandHigh.detectorWeighting = apvts.getRawParameterValue(ChugGate::ParameterIDs::bandHigh.detectorWeighting);
}

void ChugGateAudioProcessor::prepareToPlay(double sampleRate,
                                           int samplesPerBlock) {
  const int numMainChannels = getMainBusNumInputChannels();
  const int numSidechainChannels = getBusCount(true) > 1 ? getBus(true, 1)->getNumberOfChannels() : 0;

  monoKeyBuffer.setSize(1, samplesPerBlock);
  smoothedGainBuffer.setSize(2, samplesPerBlock);
  monoMainBuffer.setSize(1, samplesPerBlock);

  // Phase G buffer allocations
  monoSidechainBuffer.setSize(1, samplesPerBlock);
  dryMainCopyBuffer.setSize(numMainChannels, samplesPerBlock);
  monoWetBuffer.setSize(1, samplesPerBlock);
  hissCurtainRejectBuffer.setSize(1, samplesPerBlock);
  auditionOutputBuffer.setSize(1, samplesPerBlock);

  wetBandBuffer0.setSize(numMainChannels, samplesPerBlock);
  wetBandBuffer1.setSize(numMainChannels, samplesPerBlock);
  wetBandBuffer2.setSize(numMainChannels, samplesPerBlock);

  monoDryLow.setSize(1, samplesPerBlock);
  monoDryMid.setSize(1, samplesPerBlock);
  monoDryHigh.setSize(1, samplesPerBlock);
  monoWetLow.setSize(1, samplesPerBlock);
  monoWetMid.setSize(1, samplesPerBlock);
  monoWetHigh.setSize(1, samplesPerBlock);

  keySourceRouter.prepare(sampleRate, samplesPerBlock, numMainChannels, numSidechainChannels);
  keyConditioner.prepare(sampleRate, samplesPerBlock);
  powerlineShield.prepare(sampleRate, samplesPerBlock);
  detectorEngine.prepare(sampleRate, samplesPerBlock);

  stateMachine.prepare(sampleRate);
  gainComputer.prepare(sampleRate);
  gainSmoother.prepare(sampleRate, 1);

  attackGuard.prepare(sampleRate, samplesPerBlock);
  fundamentalEstimator.prepare(sampleRate, samplesPerBlock);
  fundamentalEstimator.setControlHop(static_cast<int>(std::max(1.0, 0.02 * sampleRate)));
  sustainAnalyzer.prepare(sampleRate, samplesPerBlock);
  feedbackDetector.prepare(sampleRate, samplesPerBlock);

  // Phase E prepare
  cadenceTracker.prepare(sampleRate);
  noiseFloorEstimator.prepare(sampleRate, samplesPerBlock);
  alignmentAnalyzer.prepare(sampleRate, samplesPerBlock);
  // size FractionalDelay for extended capacity +/- 100 ms
  fractionalDelay.prepare(sampleRate, ChugGate::DSP::FractionalDelay::maximumExtendedDelayMs);

  // Phase F prepare
  crossoverSplitter.prepare(sampleRate, samplesPerBlock, numMainChannels);
  multibandGate.prepare(sampleRate, samplesPerBlock, numMainChannels, 3);
  hissCurtain.prepare(sampleRate, samplesPerBlock, numMainChannels);

  // Phase G prepare
  lookaheadDelay.prepare(sampleRate, samplesPerBlock, numMainChannels);
  stereoLinker.prepare(sampleRate, samplesPerBlock, numMainChannels);
  anomalyGuard.prepare(sampleRate);
  rejectedSignalRouter.prepare(sampleRate);

  prevTopologyBuffer.setSize(numMainChannels, samplesPerBlock);
  bandBuffer0.setSize(numMainChannels, samplesPerBlock);
  bandBuffer1.setSize(numMainChannels, samplesPerBlock);
  bandBuffer2.setSize(numMainChannels, samplesPerBlock);
  detectorLevels.resize(static_cast<size_t>(samplesPerBlock), -100.0f);

  crossfadeDurationSamples = static_cast<int>(std::round(sampleRate * 0.02)); // 20 ms
  isCrossfading = false;
  crossfadeProgressSamples = 0;
  activeTopology = std::clamp(static_cast<int>(getParamValue(params.topology, 0.0f)), 0, 3);
  prevTopology = activeTopology;

  // Initialize main-path delay
  currentLatency = 0;
  setLatencySamples(0);

  const double maxMainDelayMs = 100.0f + 10.0f; // 100 ms skew + 10 ms lookahead
  const int maxMainDelaySamples = static_cast<int>(std::ceil(sampleRate * maxMainDelayMs * 0.001)) + 4;
  mainDelayBuffers.setSize(numMainChannels, maxMainDelaySamples);
  mainDelayBuffers.clear();
  mainDelayWriteIndex = 0;

  keySourceRouter.reset();
  keyConditioner.reset();
  powerlineShield.reset();
  detectorEngine.reset();
  attackGuard.reset();
  fundamentalEstimator.reset();
  sustainAnalyzer.reset();
  feedbackDetector.reset();
  stateMachine.reset();
  gainComputer.reset();
  gainSmoother.reset(1.0f);

  cadenceTracker.reset();
  noiseFloorEstimator.reset();
  alignmentAnalyzer.reset();
  fractionalDelay.reset();

  crossoverSplitter.reset();
  multibandGate.reset();
  hissCurtain.reset();

  lookaheadDelay.reset();
  stereoLinker.reset();
  anomalyGuard.reset();
  rejectedSignalRouter.reset();
}

void ChugGateAudioProcessor::releaseResources() {
  monoKeyBuffer.setSize(0, 0);
  smoothedGainBuffer.setSize(0, 0);
  monoMainBuffer.setSize(0, 0);
  mainDelayBuffers.setSize(0, 0);
  prevTopologyBuffer.setSize(0, 0);
  bandBuffer0.setSize(0, 0);
  bandBuffer1.setSize(0, 0);
  bandBuffer2.setSize(0, 0);
  detectorLevels.clear();

  monoSidechainBuffer.setSize(0, 0);
  dryMainCopyBuffer.setSize(0, 0);
  monoWetBuffer.setSize(0, 0);
  hissCurtainRejectBuffer.setSize(0, 0);
  auditionOutputBuffer.setSize(0, 0);

  wetBandBuffer0.setSize(0, 0);
  wetBandBuffer1.setSize(0, 0);
  wetBandBuffer2.setSize(0, 0);

  monoDryLow.setSize(0, 0);
  monoDryMid.setSize(0, 0);
  monoDryHigh.setSize(0, 0);
  monoWetLow.setSize(0, 0);
  monoWetMid.setSize(0, 0);
  monoWetHigh.setSize(0, 0);
}

void ChugGateAudioProcessor::reset() {
  keySourceRouter.reset();
  keyConditioner.reset();
  powerlineShield.reset();
  detectorEngine.reset();
  attackGuard.reset();
  fundamentalEstimator.reset();
  sustainAnalyzer.reset();
  feedbackDetector.reset();
  stateMachine.reset();
  gainComputer.reset();
  gainSmoother.reset(1.0f);

  cadenceTracker.reset();
  noiseFloorEstimator.reset();
  alignmentAnalyzer.reset();
  fractionalDelay.reset();
  mainDelayBuffers.clear();
  mainDelayWriteIndex = 0;

  crossoverSplitter.reset();
  multibandGate.reset();
  hissCurtain.reset();
  isCrossfading = false;
  crossfadeProgressSamples = 0;

  lookaheadDelay.reset();
  stereoLinker.reset();
  anomalyGuard.reset();
  rejectedSignalRouter.reset();
}

float ChugGateAudioProcessor::getParamValue(const std::atomic<float>* paramPtr, float defaultValue) const noexcept {
  return paramPtr != nullptr ? paramPtr->load(std::memory_order_relaxed) : defaultValue;
}

void ChugGateAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                          juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  midiMessages.clear();

  const int numSamples = buffer.getNumSamples();
  if (numSamples <= 0)
    return;

  // Ensure auxiliary buffers are large enough
  if (monoKeyBuffer.getNumSamples() < numSamples) {
    monoKeyBuffer.setSize(1, numSamples, false, true, true);
  }
  if (smoothedGainBuffer.getNumSamples() < numSamples) {
    smoothedGainBuffer.setSize(2, numSamples, false, true, true);
  }

  // Load parameters
  const bool enabled = getParamValue(params.enabled, 1.0f) > 0.5f;
  const bool bypassed = getParamValue(params.bypass, 0.0f) > 0.5f;
  const float trimDb = getParamValue(params.outputTrim, 0.0f);
  const float trimLinear = juce::Decibels::decibelsToGain(trimDb);

  const int listenModeVal = static_cast<int>(getParamValue(params.listenMode, 0.0f));

  // Gate parameters
  const float thresholdOpenVal = getParamValue(params.thresholdOpen, -45.0f);
  const float hysteresisVal = getParamValue(params.hysteresis, 6.0f);
  const float attackVal = getParamValue(params.attack, 1.0f);
  const float holdVal = getParamValue(params.hold, 40.0f);
  const float releaseVal = getParamValue(params.release, 100.0f);

  // Gain computer parameters
  const float floorVal = getParamValue(params.floor, -80.0f);
  const float ratioVal = getParamValue(params.ratio, 4.0f);
  const float kneeVal = getParamValue(params.knee, 6.0f);
  const float curveVal = getParamValue(params.curve, 1.0f);
  const auto gainModeIdx = static_cast<int>(getParamValue(params.gainMode, 0.0f));

  // Phase E parameters
  const bool cadenceEnabled = getParamValue(params.cadenceEnabled, 1.0f) > 0.5f;
  const float cadenceAmount = getParamValue(params.cadenceAmount, 0.5f);
  const float cadenceReleaseFraction = getParamValue(params.cadenceReleaseFraction, 0.5f);
  const int cadenceSourceMode = static_cast<int>(getParamValue(params.cadenceSourceMode, 0.0f));

  const float noiseSnrMarginVal = getParamValue(params.noiseSnrMargin, 12.0f);
  const bool noiseAdaptiveEnabledVal = getParamValue(params.noiseAdaptiveEnabled, 0.0f) > 0.5f;
  const float noiseAdaptiveRateVal = getParamValue(params.noiseAdaptiveRate, 0.05f);

  const float alignmentOffsetVal = getParamValue(params.alignmentOffset, 0.0f);
  const int lookaheadChoice = static_cast<int>(getParamValue(params.lookahead, 0.0f));

  float lookaheadMs = 0.0f;
  switch (lookaheadChoice) {
      case 1: lookaheadMs = 0.25f; break;
      case 2: lookaheadMs = 0.5f; break;
      case 3: lookaheadMs = 1.0f; break;
      case 4: lookaheadMs = 2.0f; break;
      case 5: lookaheadMs = 5.0f; break;
      case 6: lookaheadMs = 10.0f; break;
      default: lookaheadMs = 0.0f; break;
  }

  // Extract main channels and sidechain channels safely
  auto mainInputBuffer = getBusBuffer(buffer, true, 0);
  auto mainChannels = mainInputBuffer.getArrayOfReadPointers();
  auto numMainChannels = mainInputBuffer.getNumChannels();

  const float* const* sidechainChannels = nullptr;
  int numSidechainChannels = 0;
  juce::AudioBuffer<float> sidechainInputBuffer;
  if (getBusCount(true) > 1) {
    auto* sidechainBus = getBus(true, 1);
    if (sidechainBus->isEnabled()) {
      sidechainInputBuffer = getBusBuffer(buffer, true, 1);
      sidechainChannels = sidechainInputBuffer.getArrayOfReadPointers();
      numSidechainChannels = sidechainInputBuffer.getNumChannels();
    }
  }

  const bool sidechainActive = (numSidechainChannels > 0);
  float* monoKey = monoKeyBuffer.getWritePointer(0);

  // Pre-mix mono main input for AnomalyGuard and AlignmentAnalyzer
  if (monoMainBuffer.getNumSamples() < numSamples) {
    monoMainBuffer.setSize(1, numSamples, false, true, true);
  }
  float* monoMain = monoMainBuffer.getWritePointer(0);
  if (numMainChannels > 0) {
      std::copy(mainChannels[0], mainChannels[0] + numSamples, monoMain);
      for (int c = 1; c < numMainChannels; ++c) {
          for (int i = 0; i < numSamples; ++i) {
              monoMain[i] += mainChannels[c][i];
          }
      }
      const float invChannels = 1.0f / static_cast<float>(numMainChannels);
      for (int i = 0; i < numSamples; ++i) {
          monoMain[i] *= invChannels;
      }
  } else {
      std::fill(monoMain, monoMain + numSamples, 0.0f);
  }

  // Pre-mix mono sidechain input for AnomalyGuard
  const int keySourceIdx = static_cast<int>(getParamValue(params.keySource, 0.0f));
  const bool needsExternal = (keySourceIdx == 1 || keySourceIdx == 2); // ExternalSidechain or BlendedMainExternal

  float* monoSidechain = nullptr;
  if (needsExternal && numSidechainChannels > 0 && sidechainChannels != nullptr) {
      if (monoSidechainBuffer.getNumSamples() < numSamples) {
          monoSidechainBuffer.setSize(1, numSamples, false, true, true);
      }
      monoSidechain = monoSidechainBuffer.getWritePointer(0);
      std::copy(sidechainChannels[0], sidechainChannels[0] + numSamples, monoSidechain);
      for (int c = 1; c < numSidechainChannels; ++c) {
          for (int i = 0; i < numSamples; ++i) {
              monoSidechain[i] += sidechainChannels[c][i];
          }
      }
      float invSidechainChannels = 1.0f / static_cast<float>(numSidechainChannels);
      for (int i = 0; i < numSamples; ++i) {
          monoSidechain[i] *= invSidechainChannels;
      }
  }

  // Run AnomalyGuard before routing to check for spikes, stuck keys, and dropouts
  const bool isKeyValid = needsExternal ? (numSidechainChannels > 0 && sidechainChannels != nullptr) : true;
  anomalyGuard.process(monoMain, needsExternal ? monoSidechain : monoMain, numSamples, isKeyValid);

  // Apply AnomalyGuard dropout fallback logic to override KeySourceRouter's fallback mode
  auto userFallbackMode = static_cast<ChugGate::DSP::KeySourceRouter::FallbackMode>(
      static_cast<int>(getParamValue(params.fallbackMode, 0.0f)));
  ChugGate::DSP::KeySourceRouter::FallbackMode activeFallbackMode = userFallbackMode;
  if (anomalyGuard.isDropoutActive()) {
      if (!anomalyGuard.shouldUseInternalFallback()) {
          // During the dropout hold period, hold the state
          activeFallbackMode = ChugGate::DSP::KeySourceRouter::FallbackMode::HoldState;
      } else {
          // After the hold period, switch to internal main key if fallback is enabled
          if (userFallbackMode == ChugGate::DSP::KeySourceRouter::FallbackMode::InternalMain) {
              activeFallbackMode = ChugGate::DSP::KeySourceRouter::FallbackMode::InternalMain;
          } else {
              activeFallbackMode = userFallbackMode;
          }
      }
  }

  // Update key source router parameters
  keySourceRouter.setSource(static_cast<ChugGate::DSP::KeySourceRouter::Source>(keySourceIdx));
  keySourceRouter.setExternalBlend(getParamValue(params.externalBlend, 1.0f));
  keySourceRouter.setFallbackMode(activeFallbackMode);

  // 1) Route key source to monoKey
  keySourceRouter.process(mainChannels, numMainChannels,
                          sidechainChannels, numSidechainChannels,
                          monoKey, numSamples);

  // If Raw Key Audition is active, copy monoKey to output now
  if (enabled && !bypassed && listenModeVal == 1) {
      for (int channel = 0; channel < numMainChannels; ++channel) {
          if (float* channelData = buffer.getWritePointer(channel)) {
              std::copy(monoKey, monoKey + numSamples, channelData);
          }
      }
  }

  // 2) Condition the key
  keyConditioner.process(monoKey, numSamples);

  // 3) Apply Powerline Shield
  powerlineShield.process(monoKey, numSamples);

  // If Conditioned Key Audition (or later stages fallback) is active, copy monoKey to output now
  if (enabled && !bypassed && listenModeVal > 1) {
      for (int channel = 0; channel < numMainChannels; ++channel) {
          if (float* channelData = buffer.getWritePointer(channel)) {
              std::copy(monoKey, monoKey + numSamples, channelData);
          }
      }
  }

  // Update CadenceTracker host tempo/BPM if playhead is available
  if (auto* ph = getPlayHead()) {
      auto positionInfo = ph->getPosition();
      if (positionInfo.hasValue()) {
          if (auto bpm = positionInfo->getBpm()) {
              cadenceTracker.setHostTempoBpm(static_cast<float>(*bpm));
          }
      }
  }

  // Update CadenceTracker and NoiseFloorEstimator controls
  cadenceTracker.setMode(cadenceEnabled ? cadenceSourceMode : static_cast<int>(ChugGate::DSP::CadenceTracker::Mode::Off));
  cadenceTracker.setConstraintFactor(cadenceReleaseFraction);

  noiseFloorEstimator.setSlowAdaptationEnabled(noiseAdaptiveEnabledVal);
  noiseFloorEstimator.setSlowAdaptationLambda(1.0f - noiseAdaptiveRateVal);
  noiseFloorEstimator.setSnrMarginDb(noiseSnrMarginVal);

  // Compute alignment delay (calibration or manual parameter)
  float alignmentDelayMs = 0.0f;
  auto acceptedResult = alignmentAnalyzer.getAcceptedResult();
  if (acceptedResult.valid) {
      alignmentDelayMs = acceptedResult.offsetMs;
  } else {
      alignmentDelayMs = alignmentOffsetVal;
      if (std::abs(alignmentOffsetVal) > 25.0f) {
          alignmentDelayMs = juce::jlimit(-100.0f, 100.0f, alignmentOffsetVal);
      } else {
          alignmentDelayMs = juce::jlimit(-25.0f, 25.0f, alignmentOffsetVal);
      }
  }

  // Apply alignment offset to FractionalDelay
  fractionalDelay.setDelayMs(alignmentDelayMs);

  // Push key + main onset audio into AlignmentAnalyzer before FractionalDelay
  alignmentAnalyzer.process(monoKey, monoMain, numSamples);

  // Apply FractionalDelay on key path
  fractionalDelay.process(monoKey, numSamples);

  // 1. Process Lookahead Delay on the input buffer (dry main)
  lookaheadDelay.setLookaheadMs(lookaheadMs);
  lookaheadDelay.processBlock(buffer);

  // 2. Process residual main-path alignment delay (skew) on the buffer
  const float negativeSkewMs = (alignmentDelayMs < 0.0f) ? -alignmentDelayMs : 0.0f;
  const double residualMainDelaySamples = negativeSkewMs * getSampleRate() * 0.001;
  const int residualDelaySamples = static_cast<int>(std::round(residualMainDelaySamples));

  const int totalLatency = lookaheadDelay.getLatencySamples() + residualDelaySamples;
  if (totalLatency != currentLatency) {
      currentLatency = totalLatency;
      setLatencySamples(currentLatency);
  }

  const int maxDelaySamples = mainDelayBuffers.getNumSamples();
  if (maxDelaySamples > 0 && residualDelaySamples > 0) {
      for (int channel = 0; channel < numMainChannels; ++channel) {
          float* channelData = buffer.getWritePointer(channel);
          float* delayData = mainDelayBuffers.getWritePointer(channel);
          int writeIdx = mainDelayWriteIndex;
          for (int i = 0; i < numSamples; ++i) {
              float inputSample = channelData[i];
              delayData[writeIdx] = inputSample;
              
              int readIdx = (writeIdx - residualDelaySamples + maxDelaySamples) % maxDelaySamples;
              channelData[i] = delayData[readIdx];
              
              writeIdx = (writeIdx + 1) % maxDelaySamples;
          }
      }
      mainDelayWriteIndex = (mainDelayWriteIndex + numSamples) % maxDelaySamples;
  }

  // Keep a copy of the delayed dry main signal in dryMainCopyBuffer for audition routing
  if (dryMainCopyBuffer.getNumSamples() < numSamples || dryMainCopyBuffer.getNumChannels() < numMainChannels) {
      dryMainCopyBuffer.setSize(numMainChannels, numSamples, false, true, true);
  }
  for (int c = 0; c < numMainChannels; ++c) {
      dryMainCopyBuffer.copyFrom(c, 0, buffer.getReadPointer(c), numSamples);
  }

  // Update StereoLinker parameters
  stereoLinker.setMode(static_cast<ChugGate::DSP::StereoLinker::LinkMode>(
      static_cast<int>(getParamValue(params.linkMode, 0.0f))));
  stereoLinker.setLinkAmount(getParamValue(params.linkAmount, 1.0f));

  // Update gate state machine and gain parameters
  stateMachine.setThresholdOpen(thresholdOpenVal);
  stateMachine.setHysteresis(hysteresisVal);
  stateMachine.setAttack(attackVal);
  stateMachine.setHold(holdVal);
  stateMachine.setRelease(releaseVal);

  auto gainMode = ChugGate::DSP::GainComputer::Mode::HardGate;
  if (gainModeIdx == 1) {
      gainMode = ChugGate::DSP::GainComputer::Mode::SoftGate;
  } else if (gainModeIdx == 2) {
      gainMode = ChugGate::DSP::GainComputer::Mode::DownwardExpander;
  }
  gainComputer.setMode(gainMode);

  gainSmoother.setOpenTimeMs(0, attackVal);
  gainSmoother.setCloseTimeMs(0, releaseVal);

  // Update Attack Guard parameters
  attackGuard.setEnabled(getParamValue(params.attackGuardEnabled, 1.0f) > 0.5f);
  attackGuard.setAmount(getParamValue(params.attackGuardAmount, 0.75f));
  attackGuard.setSensitivity(getParamValue(params.attackGuardSensitivity, 0.5f));
  attackGuard.setFrequencyEmphasis(getParamValue(params.attackGuardFrequency, 1800.0f));
  attackGuard.setDecay(getParamValue(params.attackGuardDecay, 40.0f));
  attackGuard.setSoftNoteAssist(getParamValue(params.attackGuardSoftNoteAssist, 0.5f));
  attackGuard.setMaxThresholdReduction(getParamValue(params.attackGuardMaxThresholdReduction, 12.0f));
  attackGuard.setConfirmationWindow(getParamValue(params.attackGuardConfirmationWindow, 5.0f));

  // Update Sustain Analyzer parameters
  sustainAnalyzer.setEnabled(getParamValue(params.sustainEnabled, 1.0f) > 0.5f);
  sustainAnalyzer.setBaseRelease(getParamValue(params.release, 100.0f));
  sustainAnalyzer.setAdaptiveReleaseAmount(getParamValue(params.sustainReleaseExtension, 2.0f));
  sustainAnalyzer.setWeights(
      getParamValue(params.sustainHarmonicWeight, 0.5f),
      getParamValue(params.sustainDecayWeight, 0.3f),
      0.0f,
      getParamValue(params.sustainNoiseWeight, 0.5f)
  );
  sustainAnalyzer.setSustainThreshold(getParamValue(params.sustainAmount, 0.5f));
  sustainAnalyzer.setMaximumAdaptiveRelease(getParamValue(params.maxAdaptiveRelease, 2000.0f));

  // Update Feedback Detector parameters
  feedbackDetector.setEnabled(getParamValue(params.feedbackEnabled, 0.0f) > 0.5f);
  feedbackDetector.setSensitivity(getParamValue(params.feedbackSensitivity, 0.5f));
  feedbackDetector.setMinDuration(getParamValue(params.feedbackMinDuration, 500.0f));
  feedbackDetector.setMaxHold(getParamValue(params.feedbackMaxHold, 5000.0f));
  feedbackDetector.setFrequencyRange(
      getParamValue(params.feedbackFreqMin, 80.0f),
      getParamValue(params.feedbackFreqMax, 4000.0f)
  );
  feedbackDetector.setFeedbackFloor(getParamValue(params.feedbackFloor, -12.0f));
  feedbackDetector.setRequireExternalDI(getParamValue(params.feedbackRequireExtKey, 0.0f) > 0.5f);
  feedbackDetector.setSafetyTimeout(getParamValue(params.feedbackSafetyTimeout, 10000.0f));

  // Ensure Phase F auxiliary buffers are large enough
  if (prevTopologyBuffer.getNumSamples() < numSamples || prevTopologyBuffer.getNumChannels() < numMainChannels) {
    prevTopologyBuffer.setSize(numMainChannels, numSamples, false, true, true);
  }
  if (bandBuffer0.getNumSamples() < numSamples || bandBuffer0.getNumChannels() < numMainChannels) {
    bandBuffer0.setSize(numMainChannels, numSamples, false, true, true);
  }
  if (bandBuffer1.getNumSamples() < numSamples || bandBuffer1.getNumChannels() < numMainChannels) {
    bandBuffer1.setSize(numMainChannels, numSamples, false, true, true);
  }
  if (bandBuffer2.getNumSamples() < numSamples || bandBuffer2.getNumChannels() < numMainChannels) {
    bandBuffer2.setSize(numMainChannels, numSamples, false, true, true);
  }
  if (detectorLevels.size() < static_cast<size_t>(numSamples)) {
    detectorLevels.resize(static_cast<size_t>(numSamples), -100.0f);
  }

  // Run the sample-by-sample DSP loop
  float* smoothedGainsL = smoothedGainBuffer.getWritePointer(0);
  float* smoothedGainsR = smoothedGainBuffer.getWritePointer(1);
  bool anyTransient = false;
  bool anySustain = false;
  float sumL = 0.0f;
  float lastActiveRelease = releaseVal;

  for (int i = 0; i < numSamples; ++i) {
      float keySample = monoKey[i];

      // A) Detector
      detectorEngine.processSample(keySample);
      float L = detectorEngine.getLevelDb();

      // Feed to StereoLinker for envelopes (using mono L for both left and right since detector is mono)
      auto linkedEnv = stereoLinker.linkEnvelopes(L, L);
      float linkedL = linkedEnv.left;

      sumL += linkedL;
      detectorLevels[static_cast<size_t>(i)] = linkedL;

      // B) AttackGuard
      auto attackGuardOutput = attackGuard.processSample(keySample, linkedL);
      if (anomalyGuard.shouldSuppressOpen()) {
          // Suppress false opens into AttackGuard/state path
          attackGuardOutput.validAttack = false;
          attackGuardOutput.thresholdReductionDb = 0.0f;
      }

      if (attackGuardOutput.validAttack) {
          anyTransient = true;
          cadenceTracker.noteOnset();
      }
      cadenceTracker.tick(1);

      // C) FundamentalEstimator
      fundamentalEstimator.processSample(keySample);
      auto f0Estimate = fundamentalEstimator.getEstimate();

      // D) SustainAnalyzer + FeedbackDetector
      auto sustainOutput = sustainAnalyzer.processSample(keySample, linkedL, f0Estimate, 0.0f);
      if (sustainOutput.sustainProtection) {
          anySustain = true;
      }
      auto feedbackOutput = feedbackDetector.processSample(keySample, linkedL, f0Estimate, attackGuardOutput.transientStrength, sidechainActive);

      // E) State machine
      stateMachine.setAttackAssist(attackGuardOutput.validAttack, attackGuardOutput.thresholdReductionDb);
      stateMachine.setSustainProtection(sustainOutput.sustainProtection);
      stateMachine.setFeedbackProtection(feedbackOutput.protectOpen);

      ChugGate::DSP::PerformanceStateMachine::Evidence evidence;
      evidence.validAttack = attackGuardOutput.validAttack;
      evidence.thresholdReduction = attackGuardOutput.thresholdReductionDb;
      evidence.sustainScore = sustainOutput.sustainScore;
      evidence.sustainProtection = sustainOutput.sustainProtection;
      evidence.feedbackActive = feedbackOutput.feedbackActive;
      evidence.feedbackProtection = feedbackOutput.protectOpen;
      evidence.adaptiveReleaseMs = sustainOutput.adaptiveReleaseMs;

      auto stateOutput = stateMachine.processSample(linkedL, linkedL, evidence);

      // F) Gain computer
      float activeFloor = floorVal;
      if (feedbackOutput.feedbackActive) {
          activeFloor = feedbackOutput.suggestedFloorDb;
      }

      ChugGate::DSP::GainComputer::Input gainInput;
      gainInput.detectorLevelDb = linkedL;
      gainInput.thresholdDb = thresholdOpenVal;
      gainInput.ratio = ratioVal;
      gainInput.floorDb = activeFloor;
      gainInput.gamma = curveVal;
      gainInput.softKneeDb = kneeVal;
      gainInput.openCertainty = stateOutput.openCertainty;
      gainInput.stateHintIsOpen = ChugGate::DSP::PerformanceStateMachine::isOpenFamily(stateOutput.state);
      gainInput.hasStateHint = true;

      float targetGain = gainComputer.processSample(gainInput);

      // G) Gain smoother (combine cadence and sustain adaptive release via min)
      float sustainRelease = (stateOutput.adaptiveReleaseMs > 0.0f) ? stateOutput.adaptiveReleaseMs : releaseVal;
      float activeRelease = cadenceTracker.getEffectiveRelease(sustainRelease);
      lastActiveRelease = activeRelease;
      gainSmoother.setCloseTimeMs(0, activeRelease);

      float smoothedGain = gainSmoother.processSample(targetGain, 0);

      // Link gains with StereoLinker
      auto linkedGainPair = stereoLinker.linkGains(smoothedGain, smoothedGain, smoothedGain);
      smoothedGainsL[i] = linkedGainPair.left;
      smoothedGainsR[i] = linkedGainPair.right;

      // Update real-time state for UI/timeline with the last sample of the block
      if (i == numSamples - 1) {
          realTimeState.currentState.store(static_cast<int>(stateOutput.state));
          realTimeState.lastGain.store(0.5f * (linkedGainPair.left + linkedGainPair.right));
      }
  }

  // Observe Noise Floor
  float avgL = sumL / static_cast<float>(numSamples);
  float logEnergyDb[1] = { avgL };
  ChugGate::DSP::NoiseFloorEstimator::ProcessFlags noiseFlags;
  noiseFlags.confidentlyClosed = (getCurrentState() == ChugGate::DSP::PerformanceStateMachine::State::Closed);
  noiseFlags.transientActivity = anyTransient;
  noiseFlags.signalAboveNoiseModel = (avgL > noiseFloorEstimator.getBroadbandFloorDb() + noiseSnrMarginVal);
  noiseFlags.harmonicOrSustainedNote = anySustain;
  noiseFlags.externalKeyActive = sidechainActive;
  noiseFlags.decayingNote = (getCurrentState() == ChugGate::DSP::PerformanceStateMachine::State::Release);

  noiseFloorEstimator.process(logEnergyDb, 1, numSamples, noiseFlags);

  // Update real-time state for noise floor and alignment
  realTimeState.noiseBroadbandFloor.store(noiseFloorEstimator.getBroadbandFloorDb());
  realTimeState.noiseLowBandFloor.store(noiseFloorEstimator.getBandFloorDb(1));
  realTimeState.noiseMidBandFloor.store(noiseFloorEstimator.getBandFloorDb(2));
  realTimeState.noiseHighBandFloor.store(noiseFloorEstimator.getBandFloorDb(3));
  realTimeState.alignmentOffset.store(alignmentDelayMs);

  // 5) Apply topology selector, crossfade, and trim to main output channels (in-place)
  if (enabled && !bypassed && listenModeVal == 0) {
      // Check for topology change
      int targetTopology = static_cast<int>(getParamValue(params.topology, 0.0f));
      targetTopology = std::clamp(targetTopology, 0, 3);
      
      if (targetTopology != activeTopology) {
          if (!isCrossfading) {
              prevTopology = activeTopology;
              isCrossfading = true;
              crossfadeProgressSamples = 0;
          } else {
              prevTopology = activeTopology;
              crossfadeProgressSamples = 0;
          }
          activeTopology = targetTopology;
      }

      if (isCrossfading) {
          // Copy delayed main signal to prevTopologyBuffer before processing
          for (int c = 0; c < numMainChannels; ++c) {
              prevTopologyBuffer.copyFrom(c, 0, buffer.getReadPointer(c), numSamples);
          }
          
          // Process current topology on buffer
          processTopology(activeTopology, buffer, smoothedGainsL, smoothedGainsR, lastActiveRelease, avgL, numSamples);
          
          // Process previous topology on prevTopologyBuffer
          processTopology(prevTopology, prevTopologyBuffer, smoothedGainsL, smoothedGainsR, lastActiveRelease, avgL, numSamples);
          
          // Crossfade blend: buffer = (1-alpha)*prevTopologyBuffer + alpha*buffer
          int progress = crossfadeProgressSamples;
          for (int i = 0; i < numSamples; ++i) {
              float alpha = static_cast<float>(progress) / static_cast<float>(crossfadeDurationSamples);
              alpha = std::clamp(alpha, 0.0f, 1.0f);
              progress++;
              if (progress >= crossfadeDurationSamples) {
                  isCrossfading = false;
              }
              
              for (int c = 0; c < numMainChannels; ++c) {
                  float newSample = buffer.getSample(c, i);
                  float prevSample = prevTopologyBuffer.getSample(c, i);
                  buffer.setSample(c, i, (1.0f - alpha) * prevSample + alpha * newSample);
              }
          }
          crossfadeProgressSamples = progress;
      } else {
          // No crossfade, process current topology directly in-place
          processTopology(activeTopology, buffer, smoothedGainsL, smoothedGainsR, lastActiveRelease, avgL, numSamples);
      }

      // Apply output trim to the final processed signal
      for (int channel = 0; channel < numMainChannels; ++channel) {
          if (float* channelData = buffer.getWritePointer(channel)) {
              for (int i = 0; i < numSamples; ++i) {
                  channelData[i] *= trimLinear;
              }
          }
      }
  } else if (!enabled || bypassed) {
      // If bypassed/disabled, we don't apply the gain, but we still ran the DSP state tracking.
      // We don't modify the buffer (it remains dry).
  }

  // Compute monoDry and monoWet downmixes for audition routing
  float* monoDry = monoMainBuffer.getWritePointer(0); // reuse monoMainBuffer as monoDry
  if (numMainChannels > 0) {
      std::copy(dryMainCopyBuffer.getReadPointer(0), dryMainCopyBuffer.getReadPointer(0) + numSamples, monoDry);
      for (int c = 1; c < numMainChannels; ++c) {
          for (int i = 0; i < numSamples; ++i) {
              monoDry[i] += dryMainCopyBuffer.getSample(c, i);
          }
      }
      const float invChannels = 1.0f / static_cast<float>(numMainChannels);
      for (int i = 0; i < numSamples; ++i) {
          monoDry[i] *= invChannels;
      }
  } else {
      std::fill_n(monoDry, numSamples, 0.0f);
  }

  float* monoWet = monoWetBuffer.getWritePointer(0);
  if (numMainChannels > 0) {
      std::copy(buffer.getReadPointer(0), buffer.getReadPointer(0) + numSamples, monoWet);
      for (int c = 1; c < numMainChannels; ++c) {
          for (int i = 0; i < numSamples; ++i) {
              monoWet[i] += buffer.getSample(c, i);
          }
      }
      const float invChannels = 1.0f / static_cast<float>(numMainChannels);
      for (int i = 0; i < numSamples; ++i) {
          monoWet[i] *= invChannels;
      }
  } else {
      std::fill_n(monoWet, numSamples, 0.0f);
  }

  // 6) RejectedSignalRouter Audition Routing
  auto auditionMode = static_cast<ChugGate::DSP::RejectedSignalRouter::Mode>(
      static_cast<int>(getParamValue(params.rejectedMode, 0.0f)));

  if (enabled && !bypassed && auditionMode != ChugGate::DSP::RejectedSignalRouter::Mode::Off) {
      rejectedSignalRouter.setRejectedMode(auditionMode);
      rejectedSignalRouter.setStateFilterMode(static_cast<ChugGate::DSP::RejectedSignalRouter::StateFilterMode>(
          static_cast<int>(getParamValue(params.stateFilterMode, 0.0f))));
      rejectedSignalRouter.setPerformanceState(getCurrentState());

      ChugGate::DSP::RejectedSignalRouter::ProcessArgs args;
      args.dryMain = monoDry;
      args.wetMain = monoWet;
      args.numSamples = numSamples;
      args.detectorKey = monoKey;

      if (activeTopology == 3) {
          args.hissCurtainReject = hissCurtainRejectBuffer.getReadPointer(0);
      }

      auto downmixToMono = [](const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& dest, int nSamples) {
          int nChannels = source.getNumChannels();
          float* destData = dest.getWritePointer(0);
          if (nChannels > 0) {
              std::copy(source.getReadPointer(0), source.getReadPointer(0) + nSamples, destData);
              for (int c = 1; c < nChannels; ++c) {
                  for (int j = 0; j < nSamples; ++j) {
                      destData[j] += source.getSample(c, j);
                  }
              }
              float inv = 1.0f / static_cast<float>(nChannels);
              for (int j = 0; j < nSamples; ++j) {
                  destData[j] *= inv;
              }
          } else {
              std::fill_n(destData, nSamples, 0.0f);
          }
      };

      if ((activeTopology == 1 || activeTopology == 2) &&
          (auditionMode == ChugGate::DSP::RejectedSignalRouter::Mode::All ||
           auditionMode == ChugGate::DSP::RejectedSignalRouter::Mode::Weight ||
           auditionMode == ChugGate::DSP::RejectedSignalRouter::Mode::Voice ||
           auditionMode == ChugGate::DSP::RejectedSignalRouter::Mode::Hiss))
      {
          for (int c = 0; c < numMainChannels; ++c) {
              prevTopologyBuffer.copyFrom(c, 0, buffer.getReadPointer(c), numSamples);
          }

          if (activeTopology == 1) { // Dual Band
              const float dualCrossoverFreqVal = getParamValue(params.dualCrossoverFreq, 4000.0f);
              crossoverSplitter.setMode(ChugGate::DSP::CrossoverSplitter::Mode::TwoWay);
              crossoverSplitter.setDualCrossoverHz(dualCrossoverFreqVal);
              crossoverSplitter.processBlock(prevTopologyBuffer, wetBandBuffer0, wetBandBuffer1, wetBandBuffer2);

              downmixToMono(bandBuffer0, monoDryLow, numSamples);
              downmixToMono(bandBuffer1, monoDryHigh, numSamples);
              downmixToMono(wetBandBuffer0, monoWetLow, numSamples);
              downmixToMono(wetBandBuffer1, monoWetHigh, numSamples);

              args.dryLow = monoDryLow.getReadPointer(0);
              args.wetLow = monoWetLow.getReadPointer(0);
              args.dryHigh = monoDryHigh.getReadPointer(0);
              args.wetHigh = monoWetHigh.getReadPointer(0);
          }
          else if (activeTopology == 2) { // Tri Band
              const float triLowCrossoverFreqVal = getParamValue(params.triLowCrossoverFreq, 250.0f);
              const float triHighCrossoverFreqVal = getParamValue(params.triHighCrossoverFreq, 4500.0f);
              crossoverSplitter.setMode(ChugGate::DSP::CrossoverSplitter::Mode::ThreeWay);
              crossoverSplitter.setTriCrossoversHz(triLowCrossoverFreqVal, triHighCrossoverFreqVal);
              crossoverSplitter.processBlock(prevTopologyBuffer, wetBandBuffer0, wetBandBuffer1, wetBandBuffer2);

              downmixToMono(bandBuffer0, monoDryLow, numSamples);
              downmixToMono(bandBuffer1, monoDryMid, numSamples);
              downmixToMono(bandBuffer2, monoDryHigh, numSamples);
              downmixToMono(wetBandBuffer0, monoWetLow, numSamples);
              downmixToMono(wetBandBuffer1, monoWetMid, numSamples);
              downmixToMono(wetBandBuffer2, monoWetHigh, numSamples);

              args.dryLow = monoDryLow.getReadPointer(0);
              args.wetLow = monoWetLow.getReadPointer(0);
              args.dryMid = monoDryMid.getReadPointer(0);
              args.wetMid = monoWetMid.getReadPointer(0);
              args.dryHigh = monoDryHigh.getReadPointer(0);
              args.wetHigh = monoWetHigh.getReadPointer(0);
          }
      }

      if (auditionOutputBuffer.getNumSamples() < numSamples) {
          auditionOutputBuffer.setSize(1, numSamples, false, true, true);
      }
      float* auditionOut = auditionOutputBuffer.getWritePointer(0);

      rejectedSignalRouter.process(args, auditionOut);

      for (int channel = 0; channel < numMainChannels; ++channel) {
          if (float* channelData = buffer.getWritePointer(channel)) {
              std::copy(auditionOut, auditionOut + numSamples, channelData);
          }
      }
  }
}

void ChugGateAudioProcessor::processTopology(int topology, juce::AudioBuffer<float>& targetBuffer, const float* smoothedGainsL, const float* smoothedGainsR, float lastActiveRelease, float avgL, int numSamples)
{
    const int numChannels = targetBuffer.getNumChannels();
    
    // Note on Stereo Linker / DualMono Gating Limitation:
    // Since the detection, state machine, and gain smoothing paths are processed as a single shared mono signal (monoKey),
    // true independent L/R state machines and independent multiband gates are not run. For FullBand and HissCurtain topologies,
    // the L/R channels are gated with separate linked gains smoothed from a shared state, but for DualBand and TriBand multiband
    // gate modes, the same gain is applied to both channels. True DualMono would require duplicating the detector engine,
    // state machine, gain computer, and gain smoothers for both left and right channels.
    
    if (topology == 0) // Full Band
    {
        // Full Band: bypass splitter; apply L/R gains per channel
        for (int channel = 0; channel < numChannels; ++channel)
        {
            if (float* channelData = targetBuffer.getWritePointer(channel))
            {
                const float* gains = (channel == 0) ? smoothedGainsL : ((channel == 1) ? smoothedGainsR : smoothedGainsL);
                for (int i = 0; i < numSamples; ++i)
                {
                    channelData[i] *= gains[i];
                }
            }
        }
    }
    else if (topology == 1) // Dual Band
    {
        // Dual: TwoWay splitter + MultibandGate 2 bands
        const float dualCrossoverFreqVal = getParamValue(params.dualCrossoverFreq, 4000.0f);
        
        crossoverSplitter.setMode(ChugGate::DSP::CrossoverSplitter::Mode::TwoWay);
        crossoverSplitter.setDualCrossoverHz(dualCrossoverFreqVal);
        crossoverSplitter.processBlock(targetBuffer, bandBuffer0, bandBuffer1, bandBuffer2);
        
        const float attackVal = getParamValue(params.attack, 1.0f);
        
        multibandGate.setNumBands(2);
        
        // Band 0 (Low/Body) uses bandLow params
        multibandGate.setBandFloorDb(0, getParamValue(params.bandLow.floor, -80.0f));
        multibandGate.setBandOpenTime(0, std::max(0.01f, attackVal + getParamValue(params.bandLow.attackOffset, 0.0f)));
        multibandGate.setBandCloseTime(0, std::max(2.0f, lastActiveRelease * getParamValue(params.bandLow.releaseMultiplier, 1.0f)));
        multibandGate.setBandMute(0, getParamValue(params.bandLow.mute, 0.0f) > 0.5f);
        multibandGate.setBandSolo(0, getParamValue(params.bandLow.solo, 0.0f) > 0.5f);
        multibandGate.setBandThresholdOffset(0, getParamValue(params.bandLow.thresholdOffset, 0.0f));
        
        // Band 1 (High/Hiss) uses bandHigh params
        multibandGate.setBandFloorDb(1, getParamValue(params.bandHigh.floor, -80.0f));
        multibandGate.setBandOpenTime(1, std::max(0.01f, attackVal + getParamValue(params.bandHigh.attackOffset, 0.0f)));
        multibandGate.setBandCloseTime(1, std::max(2.0f, lastActiveRelease * getParamValue(params.bandHigh.releaseMultiplier, 1.0f)));
        multibandGate.setBandMute(1, getParamValue(params.bandHigh.mute, 0.0f) > 0.5f);
        multibandGate.setBandSolo(1, getParamValue(params.bandHigh.solo, 0.0f) > 0.5f);
        multibandGate.setBandThresholdOffset(1, getParamValue(params.bandHigh.thresholdOffset, 0.0f));
        
        // Set curve gamma
        multibandGate.setCurveGamma(getParamValue(params.curve, 1.0f));
        
        // Process target gains and recombine
        multibandGate.processTargets(detectorLevels.data(), getParamValue(params.thresholdOpen, -45.0f), getParamValue(params.hysteresis, 6.0f), numSamples);
        multibandGate.process(bandBuffer0.getArrayOfReadPointers(),
                              bandBuffer1.getArrayOfReadPointers(),
                              nullptr,
                              targetBuffer.getArrayOfWritePointers(),
                              numChannels,
                              numSamples);
    }
    else if (topology == 2) // Tri Band
    {
        // Tri: ThreeWay splitter + MultibandGate 3 bands
        const float triLowCrossoverFreqVal = getParamValue(params.triLowCrossoverFreq, 250.0f);
        const float triHighCrossoverFreqVal = getParamValue(params.triHighCrossoverFreq, 4500.0f);
        
        crossoverSplitter.setMode(ChugGate::DSP::CrossoverSplitter::Mode::ThreeWay);
        crossoverSplitter.setTriCrossoversHz(triLowCrossoverFreqVal, triHighCrossoverFreqVal);
        crossoverSplitter.processBlock(targetBuffer, bandBuffer0, bandBuffer1, bandBuffer2);
        
        const float attackVal = getParamValue(params.attack, 1.0f);
        
        multibandGate.setNumBands(3);
        
        // Band 0 (Low/Weight) uses bandLow params
        multibandGate.setBandFloorDb(0, getParamValue(params.bandLow.floor, -80.0f));
        multibandGate.setBandOpenTime(0, std::max(0.01f, attackVal + getParamValue(params.bandLow.attackOffset, 0.0f)));
        multibandGate.setBandCloseTime(0, std::max(2.0f, lastActiveRelease * getParamValue(params.bandLow.releaseMultiplier, 1.0f)));
        multibandGate.setBandMute(0, getParamValue(params.bandLow.mute, 0.0f) > 0.5f);
        multibandGate.setBandSolo(0, getParamValue(params.bandLow.solo, 0.0f) > 0.5f);
        multibandGate.setBandThresholdOffset(0, getParamValue(params.bandLow.thresholdOffset, 0.0f));
        
        // Band 1 (Mid/Voice) uses bandMid params
        multibandGate.setBandFloorDb(1, getParamValue(params.bandMid.floor, -80.0f));
        multibandGate.setBandOpenTime(1, std::max(0.01f, attackVal + getParamValue(params.bandMid.attackOffset, 0.0f)));
        multibandGate.setBandCloseTime(1, std::max(2.0f, lastActiveRelease * getParamValue(params.bandMid.releaseMultiplier, 1.0f)));
        multibandGate.setBandMute(1, getParamValue(params.bandMid.mute, 0.0f) > 0.5f);
        multibandGate.setBandSolo(1, getParamValue(params.bandMid.solo, 0.0f) > 0.5f);
        multibandGate.setBandThresholdOffset(1, getParamValue(params.bandMid.thresholdOffset, 0.0f));
 
        // Band 2 (High/Hiss) uses bandHigh params
        multibandGate.setBandFloorDb(2, getParamValue(params.bandHigh.floor, -80.0f));
        multibandGate.setBandOpenTime(2, std::max(0.01f, attackVal + getParamValue(params.bandHigh.attackOffset, 0.0f)));
        multibandGate.setBandCloseTime(2, std::max(2.0f, lastActiveRelease * getParamValue(params.bandHigh.releaseMultiplier, 1.0f)));
        multibandGate.setBandMute(2, getParamValue(params.bandHigh.mute, 0.0f) > 0.5f);
        multibandGate.setBandSolo(2, getParamValue(params.bandHigh.solo, 0.0f) > 0.5f);
        multibandGate.setBandThresholdOffset(2, getParamValue(params.bandHigh.thresholdOffset, 0.0f));
        
        // Set curve gamma
        multibandGate.setCurveGamma(getParamValue(params.curve, 1.0f));
        
        // Process target gains and recombine
        multibandGate.processTargets(detectorLevels.data(), getParamValue(params.thresholdOpen, -45.0f), getParamValue(params.hysteresis, 6.0f), numSamples);
        multibandGate.process(bandBuffer0.getArrayOfReadPointers(),
                              bandBuffer1.getArrayOfReadPointers(),
                              bandBuffer2.getArrayOfReadPointers(),
                              targetBuffer.getArrayOfWritePointers(),
                              numChannels,
                              numSamples);
    }
    else if (topology == 3) // Hiss Curtain
    {
        // Apply curtain on open/sustaining audio after main gain law:
        // First apply broadband L/R gains in-place
        for (int channel = 0; channel < numChannels; ++channel)
        {
            if (float* channelData = targetBuffer.getWritePointer(channel))
            {
                const float* gains = (channel == 0) ? smoothedGainsL : ((channel == 1) ? smoothedGainsR : smoothedGainsL);
                for (int i = 0; i < numSamples; ++i)
                {
                    channelData[i] *= gains[i];
                }
            }
        }
        
        // Configure HissCurtain
        hissCurtain.setMinFreqHz(getParamValue(params.hissCurtainMinFreq, 2000.0f));
        hissCurtain.setMaxFreqHz(getParamValue(params.hissCurtainMaxFreq, 10000.0f));
        hissCurtain.setDepth(getParamValue(params.hissCurtainDepth, 0.75f));
        
        // Feed SNR_H: L - noise floor (broadband envelope residual heuristic)
        float highBandFloor = noiseFloorEstimator.getBandFloorDb(3);
        HighBandFloorCheck:
        if (highBandFloor > 0.0f || highBandFloor < -120.0f) {
            highBandFloor = noiseFloorEstimator.getBroadbandFloorDb();
        }
        float snrH = avgL - highBandFloor;
        hissCurtain.setSnr(snrH);
        
        // Process HissCurtain
        hissCurtain.processBlock(targetBuffer);
    }
}

bool ChugGateAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *ChugGateAudioProcessor::createEditor() {
#if CHUGGATE_HAS_CUSTOM_EDITOR
  return new ChugGateAudioProcessorEditor(*this);
#else
  return new juce::GenericAudioProcessorEditor(*this);
#endif
}

const juce::String ChugGateAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool ChugGateAudioProcessor::acceptsMidi() const { return false; }

bool ChugGateAudioProcessor::producesMidi() const { return false; }

bool ChugGateAudioProcessor::isMidiEffect() const { return false; }

double ChugGateAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ChugGateAudioProcessor::getNumPrograms() { return 1; }

int ChugGateAudioProcessor::getCurrentProgram() { return 0; }

void ChugGateAudioProcessor::setCurrentProgram(int index) {
  juce::ignoreUnused(index);
}

const juce::String ChugGateAudioProcessor::getProgramName(int index) {
  juce::ignoreUnused(index);
  return {};
}

void ChugGateAudioProcessor::changeProgramName(int index,
                                               const juce::String &newName) {
  juce::ignoreUnused(index, newName);
}

void ChugGateAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
  if (stateManager != nullptr)
  {
    auto state = stateManager->createFullState();
    if (const auto xml = state.createXml())
      copyXmlToBinary(*xml, destData);
  }
}

void ChugGateAudioProcessor::setStateInformation(const void *data,
                                                 int sizeInBytes) {
  const auto xml = getXmlFromBinary(data, sizeInBytes);

  if (xml == nullptr)
    return;

  auto state = juce::ValueTree::fromXml(*xml);
  if (stateManager != nullptr)
  {
    stateManager->loadFullState(state);
  }
}

bool ChugGateAudioProcessor::applyNoiseLearn() noexcept {
    if (noiseFloorEstimator.applySuggestions()) {
        const auto& sugg = noiseFloorEstimator.getAppliedSuggestions();
        if (sugg.valid) {
            if (auto* paramOpen = apvts.getParameter(ChugGate::ParameterIDs::Global::thresholdOpen)) {
                paramOpen->setValueNotifyingHost(paramOpen->convertTo0to1(sugg.openThresholdDb));
            }
            if (auto* paramHyst = apvts.getParameter(ChugGate::ParameterIDs::Global::hysteresis)) {
                paramHyst->setValueNotifyingHost(paramHyst->convertTo0to1(sugg.hysteresisDb));
            }
            if (sugg.humRejectionEnabledHint) {
                if (auto* paramHum = apvts.getParameter(ChugGate::ParameterIDs::Key::humMode)) {
                    paramHum->setValueNotifyingHost(paramHum->convertTo0to1(3.0f)); // Auto
                }
            }
            return true;
        }
    }
    return false;
}

bool ChugGateAudioProcessor::acceptAlignment() noexcept {
    if (alignmentAnalyzer.accept()) {
        auto result = alignmentAnalyzer.getAcceptedResult();
        if (result.valid) {
            if (auto* param = apvts.getParameter(ChugGate::ParameterIDs::Key::alignmentOffset)) {
                param->setValueNotifyingHost(param->convertTo0to1(result.offsetMs));
            }
            return true;
        }
    }
    return false;
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new ChugGateAudioProcessor();
}
