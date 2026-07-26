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
#include "DSP/CrossoverSplitter.h"
#include "DSP/MultibandGate.h"
#include "DSP/HissCurtain.h"
#include "DSP/LookaheadDelay.h"
#include "DSP/StereoLinker.h"
#include "DSP/AnomalyGuard.h"
#include "DSP/RejectedSignalRouter.h"
#include "Analysis/AnalysisFifo.h"
#include "Analysis/PerformanceHistory.h"
#include "Analysis/NoiseFingerprint.h"
#include "Analysis/AlignmentScopeData.h"
#include "Analysis/AttackIntegrityAnalyzer.h"
#include "Analysis/TailIntegrityAnalyzer.h"

class ChugGateAudioProcessorEditor;

namespace ChugGate {
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
class StateManager;
}

class ChugGateAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer {
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

  // Phase H analysis accessors (consumed by phase I GUI; message-thread only).
  ChugGate::Analysis::AnalysisFifo &getAnalysisFifo() noexcept {
    return analysisFifo;
  }
  const ChugGate::Analysis::AnalysisFifo &getAnalysisFifo() const noexcept {
    return analysisFifo;
  }
  ChugGate::Analysis::PerformanceHistory &getPerformanceHistory() noexcept {
    return performanceHistory;
  }
  const ChugGate::Analysis::PerformanceHistory &getPerformanceHistory()
      const noexcept {
    return performanceHistory;
  }
  ChugGate::Analysis::NoiseFingerprint &getNoiseFingerprint() noexcept {
    return noiseFingerprint;
  }
  const ChugGate::Analysis::NoiseFingerprint &getNoiseFingerprint()
      const noexcept {
    return noiseFingerprint;
  }
  ChugGate::Analysis::AlignmentScopeData &getAlignmentScopeData() noexcept {
    return alignmentScopeData;
  }
  const ChugGate::Analysis::AlignmentScopeData &getAlignmentScopeData()
      const noexcept {
    return alignmentScopeData;
  }
  ChugGate::Analysis::AttackIntegrityAnalyzer &getAttackIntegrityAnalyzer()
      noexcept {
    return attackIntegrityAnalyzer;
  }
  const ChugGate::Analysis::AttackIntegrityAnalyzer &
  getAttackIntegrityAnalyzer() const noexcept {
    return attackIntegrityAnalyzer;
  }
  ChugGate::Analysis::TailIntegrityAnalyzer &getTailIntegrityAnalyzer()
      noexcept {
    return tailIntegrityAnalyzer;
  }
  const ChugGate::Analysis::TailIntegrityAnalyzer &getTailIntegrityAnalyzer()
      const noexcept {
    return tailIntegrityAnalyzer;
  }

  // Active quality mode (message-thread read). Defaults to Studio when the
  // analyzer is not driven by a parameter yet.
  ChugGate::Analysis::QualityMode getAnalysisQualityMode() const noexcept {
    return analysisQualityMode_.load(std::memory_order_relaxed);
  }
  // Update quality mode on every owned analysis unit. Message thread.
  void setAnalysisQualityMode(ChugGate::Analysis::QualityMode mode) noexcept;

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

    // Phase F additions
    std::atomic<float>* topology = nullptr;
    std::atomic<float>* dualCrossoverFreq = nullptr;
    std::atomic<float>* triLowCrossoverFreq = nullptr;
    std::atomic<float>* triHighCrossoverFreq = nullptr;

    std::atomic<float>* hissCurtainMinFreq = nullptr;
    std::atomic<float>* hissCurtainMaxFreq = nullptr;
    std::atomic<float>* hissCurtainDepth = nullptr;

    // Phase G additions
    std::atomic<float>* linkMode = nullptr;
    std::atomic<float>* linkAmount = nullptr;
    std::atomic<float>* rejectedMode = nullptr;
    std::atomic<float>* stateFilterMode = nullptr;

    struct BandPointers {
      std::atomic<float>* floor = nullptr;
      std::atomic<float>* thresholdOffset = nullptr;
      std::atomic<float>* attackOffset = nullptr;
      std::atomic<float>* holdOffset = nullptr;
      std::atomic<float>* releaseMultiplier = nullptr;
      std::atomic<float>* solo = nullptr;
      std::atomic<float>* mute = nullptr;
      std::atomic<float>* detectorWeighting = nullptr;
    };
    BandPointers bandLow;
    BandPointers bandMid;
    BandPointers bandHigh;
  };
  ParameterPointers params;

  // Auxiliary buffers for block processing
  juce::AudioBuffer<float> monoKeyBuffer;
  juce::AudioBuffer<float> smoothedGainBuffer;
  juce::AudioBuffer<float> monoMainBuffer;
  juce::AudioBuffer<float> mainDelayBuffers;
  int mainDelayWriteIndex = 0;
  int currentLatency = 0;

  // Phase F components
  ChugGate::DSP::CrossoverSplitter crossoverSplitter;
  ChugGate::DSP::MultibandGate multibandGate;
  ChugGate::DSP::HissCurtain hissCurtain;

  // Phase F state & buffers
  int activeTopology = 0;
  int prevTopology = 0;
  bool isCrossfading = false;
  int crossfadeProgressSamples = 0;
  int crossfadeDurationSamples = 0;
  juce::AudioBuffer<float> prevTopologyBuffer;
  juce::AudioBuffer<float> bandBuffer0;
  juce::AudioBuffer<float> bandBuffer1;
  juce::AudioBuffer<float> bandBuffer2;
  std::vector<float> detectorLevels;

  // Phase G components
  ChugGate::DSP::LookaheadDelay lookaheadDelay;
  ChugGate::DSP::StereoLinker stereoLinker;
  ChugGate::DSP::AnomalyGuard anomalyGuard;
  ChugGate::DSP::RejectedSignalRouter rejectedSignalRouter;

  // Phase G state & buffers
  juce::AudioBuffer<float> monoSidechainBuffer;
  juce::AudioBuffer<float> dryMainCopyBuffer;
  juce::AudioBuffer<float> monoWetBuffer;
  juce::AudioBuffer<float> hissCurtainRejectBuffer;
  juce::AudioBuffer<float> auditionOutputBuffer;

  juce::AudioBuffer<float> wetBandBuffer0;
  juce::AudioBuffer<float> wetBandBuffer1;
  juce::AudioBuffer<float> wetBandBuffer2;

  juce::AudioBuffer<float> monoDryLow;
  juce::AudioBuffer<float> monoDryMid;
  juce::AudioBuffer<float> monoDryHigh;
  juce::AudioBuffer<float> monoWetLow;
  juce::AudioBuffer<float> monoWetMid;
  juce::AudioBuffer<float> monoWetHigh;

  // Phase H analysis units (lifetime owned here). The AnalysisFifo is the
  // only one touched on the audio thread (tryPush is lock-free and
  // allocation-free). PerformanceHistory, NoiseFingerprint,
  // AlignmentScopeData and the two integrity analyzers are message-thread
  // only; the timerCallback below drains the FIFO and feeds them.
  ChugGate::Analysis::AnalysisFifo analysisFifo;
  ChugGate::Analysis::PerformanceHistory performanceHistory;
  ChugGate::Analysis::NoiseFingerprint noiseFingerprint;
  ChugGate::Analysis::AlignmentScopeData alignmentScopeData;
  ChugGate::Analysis::AttackIntegrityAnalyzer attackIntegrityAnalyzer;
  ChugGate::Analysis::TailIntegrityAnalyzer tailIntegrityAnalyzer;

  // Quality mode shared by every phase-H analysis unit. Atomic so the
  // audio thread can early-out before building an AnalysisFrame and so
  // setters from the message/UI thread are race-free.
  std::atomic<int> analysisQualityMode_{
      static_cast<int>(ChugGate::Analysis::QualityMode::Studio)};

  // Live-mode decimation counter for the audio-thread push path. We push
  // roughly one out of every kLivePushDecimationN frames in Live mode;
  // Studio / Diagnose push every block (the consumer-side decimation in
  // PerformanceHistory handles additional thinning).
  int livePushCounter_ = 0;
  static constexpr int kLivePushDecimationN = 4;

  // Timer period for the FIFO drainer (ms). 30 Hz is fast enough to keep
  // up with default block sizes and slow enough to avoid pegging the
  // message thread when the host is busy.
  static constexpr int kAnalysisDrainPeriodMs = 33;

  // juce::Timer override — runs on the message thread, drains the FIFO
  // into the message-side consumers and refreshes fingerprint/scope
  // snapshots.
  void timerCallback() override;

  void processTopology(int topology, juce::AudioBuffer<float>& targetBuffer, const float* smoothedGainsL, const float* smoothedGainsR, float lastActiveRelease, float avgL, int numSamples);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChugGateAudioProcessor)
};

