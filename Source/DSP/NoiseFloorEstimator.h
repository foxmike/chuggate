#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace ChugGate::DSP {

class NoiseFloorEstimator {
public:
  // Floor channel zero is broadband. The remaining channels are LF/MID/HF.
  static constexpr int maxSpectralBands = 3;
  static constexpr int maxFloorBands = 1 + maxSpectralBands;

  enum class LearnState { Idle, Capturing, Ready, Applied, Frozen };

  struct ProcessFlags {
    bool confidentlyClosed = false;
    bool transientActivity = false;
    bool signalAboveNoiseModel = false;
    bool harmonicOrSustainedNote = false;
    bool externalKeyActive = false;
    bool decayingNote = false;
  };

  struct Suggestions {
    bool valid = false;
    int numBands = 1;
    float openThresholdDb = -60.0f;
    float closeThresholdDb = -66.0f;
    float hysteresisDb = 6.0f;
    float highBandFloorDb = -100.0f;
    bool highBandFloorRecommended = false;
    bool humRejectionEnabledHint = false;
    std::array<float, maxFloorBands> noiseFloorDb{};
    std::array<float, maxFloorBands> thresholdDb{};
    std::array<float, maxFloorBands> openThresholdByBandDb{};
    std::array<float, maxFloorBands> closeThresholdByBandDb{};
  };

  NoiseFloorEstimator() noexcept;

  // All storage used by observe/process is allocated here.
  void prepare(double sampleRate, int maxBlockSize);
  void reset() noexcept;

  void startLearn() noexcept;
  void cancelLearn() noexcept;
  [[nodiscard]] bool applySuggestions() noexcept;
  void freeze() noexcept;
  void resetProfile() noexcept;

  void setPercentile(float percentile) noexcept;
  [[nodiscard]] float getPercentile() const noexcept { return percentile_; }
  void setSnrMarginDb(float marginDb) noexcept;
  void setSnrMarginDb(int floorBand, float marginDb) noexcept;
  void setCaptureLengthSeconds(float seconds) noexcept;
  void setSlowAdaptationEnabled(bool enabled) noexcept;
  void setSlowAdaptationLambda(float lambda) noexcept;

  // logEnergyDb contains broadband followed by the configured spectral bands.
  // One value per channel describes numSamples of elapsed audio.
  void observe(const float *logEnergyDb, int numFloorBands, int numSamples,
               const ProcessFlags &flags) noexcept;
  void observe(const float *logEnergyDb, int numFloorBands,
               int numSamples) noexcept {
    observe(logEnergyDb, numFloorBands, numSamples, ProcessFlags{});
  }
  void process(const float *logEnergyDb, int numFloorBands, int numSamples,
               const ProcessFlags &flags) noexcept {
    observe(logEnergyDb, numFloorBands, numSamples, flags);
  }
  void process(const float *logEnergyDb, int numFloorBands,
               int numSamples) noexcept {
    observe(logEnergyDb, numFloorBands, numSamples, ProcessFlags{});
  }

  [[nodiscard]] LearnState getState() const noexcept { return state_; }
  [[nodiscard]] bool isCapturing() const noexcept {
    return state_ == LearnState::Capturing;
  }
  [[nodiscard]] bool hasSuggestions() const noexcept {
    return suggestions_.valid && state_ != LearnState::Capturing;
  }
  [[nodiscard]] const Suggestions &getSuggestions() const noexcept {
    return suggestions_;
  }
  [[nodiscard]] const Suggestions &getAppliedSuggestions() const noexcept {
    return appliedSuggestions_;
  }

  [[nodiscard]] int getNumFloorBands() const noexcept { return numFloorBands_; }
  [[nodiscard]] float getBroadbandFloorDb() const noexcept {
    return floorDb_[0];
  }
  [[nodiscard]] float getBandFloorDb(int floorBand) const noexcept;
  [[nodiscard]] const std::array<float, maxFloorBands> &
  getPerBandFloorsDb() const noexcept {
    return floorDb_;
  }
  [[nodiscard]] float getConfidence() const noexcept { return confidence_; }
  [[nodiscard]] float getCaptureDurationSeconds() const noexcept {
    return captureDurationSeconds_;
  }
  [[nodiscard]] bool hasIdleSpikes() const noexcept { return idleSpikes_; }
  [[nodiscard]] bool hasIntermittentNoise() const noexcept {
    return intermittentNoise_;
  }
  [[nodiscard]] float getIdleSpikeLevelDb() const noexcept {
    return idleSpikeLevelDb_;
  }

private:
  static constexpr float minimumFloorDb = -160.0f;
  static constexpr float maximumFloorDb = 24.0f;
  static constexpr float bucketDurationSeconds = 0.02f;
  static constexpr float maximumCaptureSeconds = 10.0f;

  void clearCapture() noexcept;
  void pushBucket() noexcept;
  void finishCapture() noexcept;
  void updateSlowModel(const float *logEnergyDb, int numFloorBands,
                       const ProcessFlags &flags) noexcept;
  [[nodiscard]] float percentileForBand(int band, float percentile) noexcept;
  [[nodiscard]] static float sanitiseLevel(float levelDb) noexcept;
  [[nodiscard]] static float sanitiseMargin(float marginDb) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  int bucketSamples_ = 1;
  std::size_t bucketCapacity_ = 0;

  // Band-major preallocated capture storage and one reusable percentile buffer.
  std::vector<float> observations_;
  std::vector<float> percentileScratch_;
  std::size_t observationCount_ = 0;
  std::size_t observationWrite_ = 0;
  int accumulatedBucketSamples_ = 0;
  std::array<double, maxFloorBands> bucketSums_{};

  LearnState state_ = LearnState::Idle;
  LearnState stateBeforeCapture_ = LearnState::Idle;
  int numFloorBands_ = 1;
  float percentile_ = 0.35f;
  std::array<float, maxFloorBands> snrMarginDb_{};
  float requestedCaptureSeconds_ = 5.0f;
  float captureDurationSeconds_ = 0.0f;
  long long captureSamples_ = 0;

  bool slowAdaptationEnabled_ = false;
  float slowAdaptationLambda_ = 0.9995f;
  std::array<float, maxFloorBands> floorDb_{};
  float confidence_ = 0.0f;
  bool idleSpikes_ = false;
  bool intermittentNoise_ = false;
  float idleSpikeLevelDb_ = minimumFloorDb;

  Suggestions suggestions_{};
  Suggestions appliedSuggestions_{};
};

} // namespace ChugGate::DSP
