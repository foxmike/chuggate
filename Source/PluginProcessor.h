#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/KeySourceRouter.h"
#include "DSP/KeyConditioner.h"
#include "DSP/PowerlineShield.h"
#include "DSP/DetectorEngine.h"
#include "DSP/AttackGuard.h"
#include "DSP/FundamentalEstimator.h"
#include "DSP/SustainAnalyzer.h"
#include "DSP/FeedbackDetector.h"
#include "DSP/PerformanceStateMachine.h"
#include "DSP/GainComputer.h"
#include "DSP/GainSmoother.h"
#include "DSP/CadenceTracker.h"
#include "DSP/NoiseFloorEstimator.h"
#include "DSP/AlignmentAnalyzer.h"
#include "DSP/FractionalDelay.h"

class ChugGateAudioProcessorEditor;

namespace ChugGate {
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
class StateManager;
}

class ChugGateAudioProcessor : public juce::AudioProcessor {
public:
  ChugGateAudioProcessor();
  ~ChugGateAudioProcessor() override = default;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void reset() override;
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;
  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  juce::AudioProcessorValueTreeState &getAPVTS() noexcept { return apvts; }
  const juce::AudioProcessorValueTreeState &getAPVTS() const noexcept {
    return apvts;
  }

  struct RealTimeState {
    std::atomic<float> noiseBroadbandFloor{-80.0f};
    std::atomic<float> noiseLowBandFloor{-80.0f};
    std::atomic<float> noiseMidBandFloor{-80.0f};
    std::atomic<float> noiseHighBandFloor{-80.0f};
    std::atomic<double> alignmentOffset{0.0};
    
    // Phase C additions:
    std::atomic<int> currentState{0};
    std::atomic<float> lastGain{1.0f};
  };

  RealTimeState realTimeState;

  ChugGate::StateManager* getStateManager() noexcept { return stateManager.get(); }

  // Expose state for GUI/timeline endpoints
  ChugGate::DSP::PerformanceStateMachine::State getCurrentState() const noexcept {
    return static_cast<ChugGate::DSP::PerformanceStateMachine::State>(realTimeState.currentState.load());
  }
  float getLastGain() const noexcept {
    return realTimeState.lastGain.load();
  }

  // Phase E non-realtime hooks
  void startNoiseLearn() noexcept { noiseFloorEstimator.startLearn(); }
  bool applyNoiseLearn() noexcept;
  void startAlignment() noexcept { alignmentAnalyzer.startAlign(); }
  bool acceptAlignment() noexcept;
  bool processOfflineAlignment() noexcept { return alignmentAnalyzer.processOfflineAnalysis(); }
  bool finishAlignmentCalibration() noexcept { return alignmentAnalyzer.finishCalibrationChunk(); }

private:
  float getParamValue(const std::atomic<float>* paramPtr, float defaultValue = 0.0f) const noexcept;

  juce::AudioProcessorValueTreeState apvts;
  std::unique_ptr<ChugGate::StateManager> stateManager;

  // DSP components
  ChugGate::DSP::KeySourceRouter keySourceRouter;
  ChugGate::DSP::KeyConditioner keyConditioner;
  ChugGate::DSP::PowerlineShield powerlineShield;
  ChugGate::DSP::DetectorEngine detectorEngine;
  ChugGate::DSP::AttackGuard attackGuard;
  ChugGate::DSP::FundamentalEstimator fundamentalEstimator;
  ChugGate::DSP::SustainAnalyzer sustainAnalyzer;
  ChugGate::DSP::FeedbackDetector feedbackDetector;
  ChugGate::DSP::PerformanceStateMachine stateMachine;
  ChugGate::DSP::GainComputer gainComputer;
  ChugGate::DSP::GainSmoother gainSmoother;
  ChugGate::DSP::CadenceTracker cadenceTracker;
  ChugGate::DSP::NoiseFloorEstimator noiseFloorEstimator;
  ChugGate::DSP::AlignmentAnalyzer alignmentAnalyzer;
  ChugGate::DSP::FractionalDelay fractionalDelay;

  // Real-time parameter pointers
  struct ParameterPointers {
    std::atomic<float>* enabled = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* outputTrim = nullptr;
    std::atomic<float>* listenMode = nullptr;
    std::atomic<float>* thresholdOpen = nullptr;
    std::atomic<float>* hysteresis = nullptr;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* hold = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* floor = nullptr;
    std::atomic<float>* ratio = nullptr;
    std::atomic<float>* knee = nullptr;
    std::atomic<float>* curve = nullptr;
    std::atomic<float>* gainMode = nullptr;
    
    std::atomic<float>* keySource = nullptr;
    std::atomic<float>* externalBlend = nullptr;
    std::atomic<float>* fallbackMode = nullptr;
    std::atomic<float>* hpfEnabled = nullptr;
    std::atomic<float>* hpfFrequency = nullptr;
    std::atomic<float>* hpfSlope = nullptr;
    std::atomic<float>* lpfEnabled = nullptr;
    std::atomic<float>* lpfFrequency = nullptr;
    std::atomic<float>* lpfSlope = nullptr;
    std::atomic<float>* emphasisFreq = nullptr;
    std::atomic<float>* emphasisGain = nullptr;
    std::atomic<float>* emphasisQ = nullptr;
    std::atomic<float>* humMode = nullptr;
    std::atomic<float>* humHarmonics = nullptr;
    std::atomic<float>* humNotchQ = nullptr;
    std::atomic<float>* humImmunityAmount = nullptr;
    std::atomic<float>* maxAdaptiveRelease = nullptr;

    // Attack Guard
    std::atomic<float>* attackGuardEnabled = nullptr;
    std::atomic<float>* attackGuardAmount = nullptr;
    std::atomic<float>* attackGuardSensitivity = nullptr;
    std::atomic<float>* attackGuardFrequency = nullptr;
    std::atomic<float>* attackGuardDecay = nullptr;
    std::atomic<float>* attackGuardSoftNoteAssist = nullptr;
    std::atomic<float>* attackGuardMaxThresholdReduction = nullptr;
    std::atomic<float>* attackGuardConfirmationWindow = nullptr;

    // Sustain Analyzer & Feedback Friend
    std::atomic<float>* sustainEnabled = nullptr;
    std::atomic<float>* sustainAmount = nullptr;
    std::atomic<float>* sustainReleaseExtension = nullptr;
    std::atomic<float>* sustainHarmonicWeight = nullptr;
    std::atomic<float>* sustainDecayWeight = nullptr;
    std::atomic<float>* sustainNoiseWeight = nullptr;
    std::atomic<float>* feedbackEnabled = nullptr;
    std::atomic<float>* feedbackSensitivity = nullptr;
    std::atomic<float>* feedbackMinDuration = nullptr;
    std::atomic<float>* feedbackMaxHold = nullptr;
    std::atomic<float>* feedbackFreqMin = nullptr;
    std::atomic<float>* feedbackFreqMax = nullptr;
    std::atomic<float>* feedbackFloor = nullptr;
    std::atomic<float>* feedbackRequireExtKey = nullptr;
    std::atomic<float>* feedbackSafetyTimeout = nullptr;

    // Cadence
    std::atomic<float>* cadenceEnabled = nullptr;
    std::atomic<float>* cadenceAmount = nullptr;
    std::atomic<float>* cadenceReleaseFraction = nullptr;
    std::atomic<float>* cadenceSourceMode = nullptr;

    // Noise Learn
    std::atomic<float>* noiseSnrMargin = nullptr;
    std::atomic<float>* noiseAdaptiveEnabled = nullptr;
    std::atomic<float>* noiseAdaptiveRate = nullptr;

    // Alignment
    std::atomic<float>* alignmentOffset = nullptr;
    std::atomic<float>* lookahead = nullptr;
  };
  ParameterPointers params;

  // Auxiliary buffers for block processing
  juce::AudioBuffer<float> monoKeyBuffer;
  juce::AudioBuffer<float> smoothedGainBuffer;
  juce::AudioBuffer<float> monoMainBuffer;
  juce::AudioBuffer<float> mainDelayBuffers;
  int mainDelayWriteIndex = 0;
  int currentLatency = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChugGateAudioProcessor)
};

