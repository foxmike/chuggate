#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ChugGate::DSP {

struct FundamentalEstimate {
  float estimatedFundamental = 0.0f;
  float confidence = 0.0f;

  // Number of input samples elapsed at the end of the control hop which
  // produced this estimate, counted since prepare()/reset().
  std::uint64_t timestamp = 0;
  bool validity = false;
};

// Lightweight monophonic f0 evidence for harmonicity analysis. This is not a
// note tracker: estimates come from bounded normalized autocorrelation over a
// fixed window, and are published only at control-hop boundaries.
class FundamentalEstimator {
public:
  using Estimate = FundamentalEstimate;

  static constexpr float minimumFundamentalHz = 40.0f;
  static constexpr float maximumFundamentalHz = 800.0f;
  static constexpr float defaultControlHopMs = 20.0f;

  FundamentalEstimator() noexcept = default;

  void prepare(double sampleRate, int maxBlockSize = 0) noexcept;
  void reset() noexcept;

  // Caller-owned mono input. No allocation, locking, or logging occurs here.
  void process(const float *monoInput, int numSamples) noexcept;
  void processBlock(const float *monoInput, int numSamples) noexcept {
    process(monoInput, numSamples);
  }
  void processSample(float monoSample) noexcept;

  // Sets the update interval in input-rate samples. The value is clamped to
  // at least one sample and takes effect after the current hop.
  void setControlHop(int hopSamples) noexcept;
  void setControlHopSamples(int hopSamples) noexcept {
    setControlHop(hopSamples);
  }

  [[nodiscard]] FundamentalEstimate getEstimate() const noexcept;
  [[nodiscard]] FundamentalEstimate getLatestSnapshot() const noexcept {
    return getEstimate();
  }
  [[nodiscard]] float getEstimatedFundamental() const noexcept;
  [[nodiscard]] float getConfidence() const noexcept;
  [[nodiscard]] std::uint64_t getTimestamp() const noexcept;
  [[nodiscard]] bool isValid() const noexcept;

private:
  static constexpr int analysisWindowSize = 1024;
  static constexpr int maximumCorrelationLag = analysisWindowSize / 2 - 1;
  static constexpr double targetAnalysisRate = 8000.0;
  static constexpr float minimumRms = 1.0e-5f;
  static constexpr float minimumCorrelation = 0.45f;

  void appendAnalysisSample(float sample) noexcept;
  void estimate() noexcept;
  [[nodiscard]] float correlationAtLag(int lag, float mean) const noexcept;
  [[nodiscard]] float sampleAt(int chronologicalIndex) const noexcept;
  void publish(float fundamentalHz, float confidence, bool validity) noexcept;

  double sampleRate_ = 0.0;
  double analysisRate_ = 0.0;
  int maxBlockSize_ = 0;
  int decimationFactor_ = 1;
  int minimumLag_ = 1;
  int maximumLag_ = 1;
  bool prepared_ = false;

  std::array<float, analysisWindowSize> analysisBuffer_{};
  std::array<float, maximumCorrelationLag + 1> correlations_{};
  int writeIndex_ = 0;
  int samplesInWindow_ = 0;

  float decimationSum_ = 0.0f;
  int decimationCount_ = 0;
  std::atomic<int> controlHopSamples_{1};
  int samplesUntilEstimate_ = 1;
  std::uint64_t inputSampleCount_ = 0;

  // Atomic fields plus a sequence counter provide a coherent, lock-free
  // snapshot to non-audio readers while the audio thread publishes.
  mutable std::atomic<std::uint32_t> snapshotSequence_{0};
  std::atomic<float> publishedFundamental_{0.0f};
  std::atomic<float> publishedConfidence_{0.0f};
  std::atomic<std::uint64_t> publishedTimestamp_{0};
  std::atomic<bool> publishedValidity_{false};
};

} // namespace ChugGate::DSP
