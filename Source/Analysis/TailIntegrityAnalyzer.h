#pragma once

#include "AnalysisFifo.h"

#include <atomic>
#include <cstdint>

namespace ChugGate::Analysis {

class PerformanceHistory;

// Message/analysis-thread model comparing input and gain-shaped tail decay.
// Storage is fixed by prepare(); neither observation path allocates.
class TailIntegrityAnalyzer {
public:
  struct Event {
    float mainDecayRate = 0.0f;
    float outputDecayRate = 0.0f;
    float sustainConfidence = 0.0f;
    float chokeScore = 0.0f;
    int closurePoint = -1;
    bool choked = false;
  };

  static constexpr float minimumSustainConfidence = 0.65f;
  static constexpr float minimumDecayRateDifference = 0.002f;
  static constexpr float chokeDecayRateRatio = 1.5f;
  static constexpr float chokeScoreThreshold = 0.25f;
  static constexpr float amplitudeEpsilon = 1.0e-9f;
  static constexpr int defaultEventCapacity = 256;
  static constexpr int defaultTailWindowSamples = 32;

  // PerformanceStateMachine state values used by the timeline lane.
  static constexpr std::uint8_t closedGateState = 0;
  static constexpr std::uint8_t releaseGateState = 6;

  TailIntegrityAnalyzer() noexcept = default;

  void prepare(int maxEvents = defaultEventCapacity,
               int tailWindowSamples = defaultTailWindowSamples) noexcept;
  void reset() noexcept;

  void setQualityMode(QualityMode mode) noexcept;
  [[nodiscard]] QualityMode getQualityMode() const noexcept;

  // Observes newly-added history and emits a tail event when the gate enters
  // Release or Closed. mainEnvelope is dBFS; output is reconstructed from its
  // linear outputGain series.
  void observe(const PerformanceHistory &history) noexcept;

  // Explicit amplitude-window path. closurePoint is optional metadata for the
  // sample at which Release/Closed began.
  void observeTail(const float *main, const float *output, int numSamples,
                   float sustainConfidence,
                   int closurePoint = -1) noexcept;

  [[nodiscard]] float getChokeScore() const noexcept {
    return latestChokeScore_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool isLatestTailChoked() const noexcept {
    return latestChoked_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getChokeCount() const noexcept {
    return chokeCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getCleanTailCount() const noexcept {
    return cleanTailCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getEventCount() const noexcept {
    return eventCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getEventCapacity() const noexcept { return eventCapacity_; }
  [[nodiscard]] float getMeanChokeScore() const noexcept {
    return meanChokeScore_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getMeanMainDecayRate() const noexcept {
    return meanMainDecayRate_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getMeanOutputDecayRate() const noexcept {
    return meanOutputDecayRate_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] Event getLatestEvent() const noexcept;
  int copyRecentEvents(Event *destination, int destinationCapacity) const
      noexcept;

private:
  [[nodiscard]] bool shouldAnalyzeNextEvent() noexcept;
  void addEvent(float mainDecayRate, float outputDecayRate,
                float sustainConfidence, int closurePoint) noexcept;
  void removeFromSummary(const Event &event) noexcept;

  juce::HeapBlock<Event> events_;
  int eventCapacity_ = 0;
  int writeIndex_ = 0;
  std::atomic<int> eventCount_{0};
  int tailWindowSamples_ = defaultTailWindowSamples;
  int liveDecimationCounter_ = 0;
  std::uint64_t lastObservedHistoryPushes_ = 0;
  std::uint8_t previousGateState_ = closedGateState;
  bool hasPreviousGateState_ = false;

  double retainedChokeScoreSum_ = 0.0;
  double retainedMainDecaySum_ = 0.0;
  double retainedOutputDecaySum_ = 0.0;

  std::atomic<int> qualityMode_{static_cast<int>(QualityMode::Studio)};
  std::atomic<float> latestChokeScore_{0.0f};
  std::atomic<bool> latestChoked_{false};
  std::atomic<float> meanChokeScore_{0.0f};
  std::atomic<float> meanMainDecayRate_{0.0f};
  std::atomic<float> meanOutputDecayRate_{0.0f};
  std::atomic<std::uint64_t> chokeCount_{0};
  std::atomic<std::uint64_t> cleanTailCount_{0};
};

} // namespace ChugGate::Analysis
