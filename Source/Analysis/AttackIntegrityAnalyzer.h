#pragma once

#include "AnalysisFifo.h"

#include <atomic>
#include <cstdint>

namespace ChugGate::Analysis {

class PerformanceHistory;

// Message/analysis-thread model for per-onset attack integrity. This class is
// never called from the audio thread. All event storage is allocated by
// prepare(); observe paths perform no allocation.
class AttackIntegrityAnalyzer {
public:
  enum class Classification : std::uint8_t {
    Preserved,
    Partial,
    Missed
  };

  struct Event {
    float preservationRatio = 0.0f;
    float inputEnergy = 0.0f;
    float outputEnergy = 0.0f;
    float transientStrength = 0.0f;
    Classification classification = Classification::Missed;
  };

  // A_r >= 0.9 is preserved, 0.5 <= A_r < 0.9 is partial, and A_r <
  // 0.5 is missed.
  static constexpr float preservedThreshold = 0.9f;
  static constexpr float partialThreshold = 0.5f;
  static constexpr double energyEpsilon = 1.0e-12;
  static constexpr int defaultEventCapacity = 256;
  static constexpr int defaultAttackWindowSamples = 8;

  AttackIntegrityAnalyzer() noexcept = default;

  void prepare(int maxEvents = defaultEventCapacity,
               int attackWindowSamples = defaultAttackWindowSamples) noexcept;
  void reset() noexcept;

  void setQualityMode(QualityMode mode) noexcept;
  [[nodiscard]] QualityMode getQualityMode() const noexcept;

  // Finds newly-added transient markers and evaluates a bounded timeline
  // window around each marker. mainEnvelope is treated as dBFS and outputGain
  // as a linear gain.
  void observe(const PerformanceHistory &history) noexcept;

  // Explicit amplitude-window path. Computes
  // A_r = sum(output[i]^2) / max(sum(input[i]^2), energyEpsilon).
  void observeOnset(const float *input, const float *output,
                    int numSamples, float transientStrength = 1.0f) noexcept;

  [[nodiscard]] float getAttackPreservationRatio() const noexcept {
    return latestRatio_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getLatestRatio() const noexcept {
    return getAttackPreservationRatio();
  }
  [[nodiscard]] float getMeanAttackPreservationRatio() const noexcept {
    return meanRatio_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getMeanRatio() const noexcept {
    return getMeanAttackPreservationRatio();
  }
  [[nodiscard]] std::uint64_t getPreservedCount() const noexcept {
    return preservedCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getPartialCount() const noexcept {
    return partialCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getMissedCount() const noexcept {
    return missedCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getEventCount() const noexcept {
    return eventCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getEventCapacity() const noexcept { return eventCapacity_; }
  [[nodiscard]] int getAttackWindowSamples() const noexcept {
    return attackWindowSamples_;
  }
  [[nodiscard]] Event getLatestEvent() const noexcept;
  int copyRecentEvents(Event *destination, int destinationCapacity) const
      noexcept;

private:
  [[nodiscard]] bool shouldAnalyzeNextEvent() noexcept;
  void addEvent(double inputEnergy, double outputEnergy,
                float transientStrength) noexcept;
  void removeFromSummary(const Event &event) noexcept;

  juce::HeapBlock<Event> events_;
  int eventCapacity_ = 0;
  int writeIndex_ = 0;
  std::atomic<int> eventCount_{0};
  int attackWindowSamples_ = defaultAttackWindowSamples;
  int liveDecimationCounter_ = 0;
  std::uint64_t lastObservedHistoryPushes_ = 0;
  double retainedRatioSum_ = 0.0;

  std::atomic<int> qualityMode_{static_cast<int>(QualityMode::Studio)};
  std::atomic<float> latestRatio_{0.0f};
  std::atomic<float> meanRatio_{0.0f};
  std::atomic<std::uint64_t> preservedCount_{0};
  std::atomic<std::uint64_t> partialCount_{0};
  std::atomic<std::uint64_t> missedCount_{0};
};

} // namespace ChugGate::Analysis
