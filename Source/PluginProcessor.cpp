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
}

void ChugGateAudioProcessor::prepareToPlay(double sampleRate,
                                           int samplesPerBlock) {
  monoKeyBuffer.setSize(1, samplesPerBlock);
  smoothedGainBuffer.setSize(1, samplesPerBlock);

  const int numMainChannels = getMainBusNumInputChannels();
  const int numSidechainChannels = getBusCount(true) > 1 ? getBus(true, 1)->getNumberOfChannels() : 0;

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
}

void ChugGateAudioProcessor::releaseResources() {
  monoKeyBuffer.setSize(0, 0);
  smoothedGainBuffer.setSize(0, 0);
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
    smoothedGainBuffer.setSize(1, numSamples, false, true, true);
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

  // Update key source router parameters
  keySourceRouter.setSource(static_cast<ChugGate::DSP::KeySourceRouter::Source>(
      static_cast<int>(getParamValue(params.keySource, 0.0f))));
  keySourceRouter.setExternalBlend(getParamValue(params.externalBlend, 1.0f));
  keySourceRouter.setFallbackMode(static_cast<ChugGate::DSP::KeySourceRouter::FallbackMode>(
      static_cast<int>(getParamValue(params.fallbackMode, 0.0f))));

  // Update key conditioner parameters
  keyConditioner.setHighPassEnabled(getParamValue(params.hpfEnabled, 1.0f) > 0.5f);
  keyConditioner.setHighPassFrequency(getParamValue(params.hpfFrequency, 70.0f));
  keyConditioner.setHighPassSlope(static_cast<ChugGate::DSP::FilterSlope>(
      static_cast<int>(getParamValue(params.hpfSlope, 0.0f))));

  keyConditioner.setLowPassEnabled(getParamValue(params.lpfEnabled, 0.0f) > 0.5f);
  keyConditioner.setLowPassFrequency(getParamValue(params.lpfFrequency, 12000.0f));
  keyConditioner.setLowPassSlope(static_cast<ChugGate::DSP::FilterSlope>(
      static_cast<int>(getParamValue(params.lpfSlope, 0.0f))));

  const float emphasisFreq = getParamValue(params.emphasisFreq, 1200.0f);
  const float emphasisGain = getParamValue(params.emphasisGain, 0.0f);
  const float emphasisQ = getParamValue(params.emphasisQ, 1.0f);
  keyConditioner.setEmphasisEnabled(emphasisGain != 0.0f);
  keyConditioner.setEmphasisFrequency(emphasisFreq);
  keyConditioner.setEmphasisQ(emphasisQ);
  keyConditioner.setEmphasisGain(emphasisGain);

  if (listenModeVal > 0) {
      keyConditioner.setListenMode(static_cast<ChugGate::DSP::KeyConditioner::ListenMode>(listenModeVal - 1));
  }

  // Update powerline shield parameters
  powerlineShield.setHumMode(static_cast<ChugGate::DSP::PowerlineShield::HumMode>(
      static_cast<int>(getParamValue(params.humMode, 0.0f))));
  powerlineShield.setHarmonics(static_cast<int>(getParamValue(params.humHarmonics, 0.0f)) + 1);
  powerlineShield.setNotchQ(getParamValue(params.humNotchQ, 20.0f));
  powerlineShield.setHumImmunityAmount(getParamValue(params.humImmunityAmount, 0.5f));

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

  // Run the sample-by-sample DSP loop
  float* smoothedGains = smoothedGainBuffer.getWritePointer(0);
  for (int i = 0; i < numSamples; ++i) {
      float keySample = monoKey[i];

      // A) Detector
      detectorEngine.processSample(keySample);
      float L = detectorEngine.getLevelDb();

      // B) AttackGuard
      auto attackGuardOutput = attackGuard.processSample(keySample, L);

      // C) FundamentalEstimator
      fundamentalEstimator.processSample(keySample);
      auto f0Estimate = fundamentalEstimator.getEstimate();

      // D) SustainAnalyzer + FeedbackDetector
      auto sustainOutput = sustainAnalyzer.processSample(keySample, L, f0Estimate, 0.0f);
      auto feedbackOutput = feedbackDetector.processSample(keySample, L, f0Estimate, attackGuardOutput.transientStrength, sidechainActive);

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

      auto stateOutput = stateMachine.processSample(L, L, evidence);

      // F) Gain computer
      float activeFloor = floorVal;
      if (feedbackOutput.feedbackActive) {
          activeFloor = feedbackOutput.suggestedFloorDb;
      }

      ChugGate::DSP::GainComputer::Input gainInput;
      gainInput.detectorLevelDb = L;
      gainInput.thresholdDb = thresholdOpenVal;
      gainInput.ratio = ratioVal;
      gainInput.floorDb = activeFloor;
      gainInput.gamma = curveVal;
      gainInput.softKneeDb = kneeVal;
      gainInput.openCertainty = stateOutput.openCertainty;
      gainInput.stateHintIsOpen = ChugGate::DSP::PerformanceStateMachine::isOpenFamily(stateOutput.state);
      gainInput.hasStateHint = true;

      float targetGain = gainComputer.processSample(gainInput);

      // G) Gain smoother
      float activeRelease = (stateOutput.adaptiveReleaseMs > 0.0f) ? stateOutput.adaptiveReleaseMs : releaseVal;
      gainSmoother.setCloseTimeMs(0, activeRelease);

      float smoothedGain = gainSmoother.processSample(targetGain, 0);

      smoothedGains[i] = smoothedGain;

      // Update real-time state for UI/timeline with the last sample of the block
      if (i == numSamples - 1) {
          realTimeState.currentState.store(static_cast<int>(stateOutput.state));
          realTimeState.lastGain.store(smoothedGain);
      }
  }

  // 5) Apply smoothed broadband gain and trim to main output channels (in-place)
  if (enabled && !bypassed && listenModeVal == 0) {
      for (int channel = 0; channel < numMainChannels; ++channel) {
          if (float* channelData = buffer.getWritePointer(channel)) {
              for (int i = 0; i < numSamples; ++i) {
                  channelData[i] *= smoothedGains[i] * trimLinear;
              }
          }
      }
  } else if (!enabled || bypassed) {
      // If bypassed/disabled, we don't apply the gain, but we still ran the DSP state tracking.
      // We don't modify the buffer (it remains dry).
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

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new ChugGateAudioProcessor();
}
