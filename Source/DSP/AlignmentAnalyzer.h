#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ChugGate::DSP {

// Onset-based DI/main alignment estimator. process/processSample are bounded
// real-time producer operations. Correlation and robust aggregation are only
// performed by processOfflineAnalysis/finishCalibrationChunk on a non-audio
// thread.
class AlignmentAnalyzer {
public:
  static constexpr float defaultSearchRangeMs = 25.0f;
  static constexpr float maximumSearchRangeMs = 100.0f;
  static constexpr float maximumCalibrationSeconds = 12.0f;

  enum class CalibrationState { Idle, Capturing, Ready, Accepted };

  struct Result {
    bool valid = false;
    float offsetMs = 0.0f;
    float keyDelayMs = 0.0f;
    float confidence = 0.0f;
    float variationMs = 0.0f;
    bool needsMainLookahead = false;
    int estimateCount = 0;
  };

  AlignmentAnalyzer() noexcept = default;

  void prepare(double sampleRate, int maxBlockSize = 0);
  void reset() noexcept;

  void setSearchRangeMs(float rangeMs) noexcept;
  [[nodiscard]] float getSearchRangeMs() const noexcept {
    return searchRangeMs_.load(std::memory_order_relaxed);
  }

  void startAlign() noexcept;
  void cancel() noexcept;
  [[nodiscard]] bool accept() noexcept;

  // Conditioned mono key and main-path audio. No allocation, locking, or
  // correlation occurs on these calls.
  void processSample(float conditionedKey, float mainAudio) noexcept;
  void process(const float *conditionedKey, const float *mainAudio,
               int numSamples) noexcept;
  void processBlock(const float *conditionedKey, const float *mainAudio,
                    int numSamples) noexcept {
    process(conditionedKey, mainAudio, numSamples);
  }

  // Call from a worker/control thread. The first method drains the SPSC onset
  // ring and analyzes complete windows. finishCalibrationChunk also analyzes
  // the final partial window and changes Capturing to Ready when possible.
  [[nodiscard]] bool processOfflineAnalysis() noexcept;
  [[nodiscard]] bool finishCalibrationChunk() noexcept;

  [[nodiscard]] CalibrationState getState() const noexcept {
    return state_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool isCapturing() const noexcept {
    return getState() == CalibrationState::Capturing;
  }
  [[nodiscard]] bool hasRecommendation() const noexcept {
    return candidateValid_.load(std::memory_order_acquire);
  }
  [[nodiscard]] Result getResult() const noexcept;
  [[nodiscard]] Result getAcceptedResult() const noexcept;

  [[nodiscard]] float getRecommendedOffsetMs() const noexcept {
    return candidateOffsetMs_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getRecommendedDelayMs() const noexcept {
    return getRecommendedOffsetMs();
  }
  [[nodiscard]] float getRecommendedKeyDelayMs() const noexcept {
    return std::max(0.0f, getRecommendedOffsetMs());
  }
  [[nodiscard]] float getConfidence() const noexcept {
    return candidateConfidence_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getVariationMs() const noexcept {
    return candidateVariationMs_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool needsMainLookahead() const noexcept {
    return candidateNeedsLookahead_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getDroppedOnsetSamples() const noexcept {
    return droppedSamples_.load(std::memory_order_relaxed);
  }

private:
  struct OnsetPair {
    float key = 0.0f;
    float audio = 0.0f;
    std::uint32_t generation = 0;
  };

  struct EnvelopeState {
    float power = 0.0f;
    std::vector<float> energyHistory;
    std::size_t writeIndex = 0;
  };

  struct Estimate {
    float lagSamples = 0.0f;
    float confidence = 0.0f;
  };

  [[nodiscard]] float calculateOnset(float sample,
                                     EnvelopeState &state) noexcept;
  void pushOnsetPair(float keyOnset, float audioOnset) noexcept;
  void drainRing() noexcept;
  [[nodiscard]] bool analyzeAvailableWindows(bool finalize) noexcept;
  [[nodiscard]] bool analyzeWindow(const OnsetPair *samples,
                                   std::size_t count) noexcept;
  void addEstimate(float lagSamples, float confidence) noexcept;
  void updateCandidate() noexcept;
  void clearProducerData() noexcept;
  void clearAnalysisData() noexcept;
  void clearCaptureData() noexcept;
  [[nodiscard]] static float median(float *values, int count) noexcept;

  static constexpr int maximumEstimates = 64;
  static constexpr double onsetAnalysisRate = 4000.0;
  static constexpr float powerSmoothingMs = 2.0f;
  static constexpr float onsetDifferenceMs = 2.0f;
  static constexpr float analysisWindowSeconds = 0.5f;
  static constexpr float minimumWindowSeconds = 0.12f;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  float powerAlpha_ = 0.0f;
  int onsetDecimation_ = 1;
  double correlationSampleRate_ = onsetAnalysisRate;
  int onsetDelaySamples_ = 1;
  int decimationCounter_ = 0;
  float decimatedKeyMaximum_ = 0.0f;
  float decimatedAudioMaximum_ = 0.0f;
  EnvelopeState keyEnvelope_;
  EnvelopeState audioEnvelope_;

  std::vector<OnsetPair> onsetRing_;
  std::atomic<std::uint32_t> ringWrite_{0};
  std::atomic<std::uint32_t> ringRead_{0};
  std::atomic<std::uint64_t> droppedSamples_{0};
  std::atomic<std::uint32_t> captureGeneration_{0};
  std::uint32_t producerGeneration_ = 0;

  // Consumer-only, pre-sized work areas. Heavy use is explicitly off-thread.
  std::vector<OnsetPair> pendingSamples_;
  std::size_t pendingCount_ = 0;
  std::vector<float> correlationScratch_;
  std::array<Estimate, maximumEstimates> estimates_{};
  int estimateCount_ = 0;
  std::array<float, maximumEstimates> medianScratch_{};

  std::atomic<CalibrationState> state_{CalibrationState::Idle};
  std::atomic<float> searchRangeMs_{defaultSearchRangeMs};
  std::atomic<bool> candidateValid_{false};
  std::atomic<float> candidateOffsetMs_{0.0f};
  std::atomic<float> candidateConfidence_{0.0f};
  std::atomic<float> candidateVariationMs_{0.0f};
  std::atomic<bool> candidateNeedsLookahead_{false};
  std::atomic<int> candidateEstimateCount_{0};

  Result acceptedResult_{};
};

} // namespace ChugGate::DSP
