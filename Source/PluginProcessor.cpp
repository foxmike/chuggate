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
}

void ChugGateAudioProcessor::prepareToPlay(double sampleRate,
                                           int samplesPerBlock) {
  juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void ChugGateAudioProcessor::releaseResources() {}

bool ChugGateAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  const auto mainInput = layouts.getMainInputChannelSet();
  const auto mainOutput = layouts.getMainOutputChannelSet();

  if (mainInput != mainOutput || (mainInput != juce::AudioChannelSet::mono() &&
                                  mainInput != juce::AudioChannelSet::stereo()))
    return false;

  const auto sidechain = layouts.getChannelSet(true, 1);

  if (sidechain.isDisabled())
    return true;

  return mainInput == juce::AudioChannelSet::stereo() &&
         (sidechain == juce::AudioChannelSet::mono() ||
          sidechain == juce::AudioChannelSet::stereo());
}

void ChugGateAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                          juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  juce::ignoreUnused(buffer);
  midiMessages.clear();
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

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new ChugGateAudioProcessor();
}
