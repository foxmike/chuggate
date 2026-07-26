#pragma once

#include <atomic>
#include <cstdint>
#include <array>

namespace ChugGate::DSP {

class CadenceTracker {
public:
  enum class Mode : std::uint8_t {
    Audio,
    HostTempo,
    Combined,
    Off
  };

  CadenceTracker() noexcept;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  // Audio thread inputs:
  // noteOnset is called when a valid onset or attack is detected.
  void noteOnset() noexcept;
  void noteOnset(std::uint64_t sampleTimestamp) noexcept;
  
  // Advanced sample counter
  void tick(int numSamples) noexcept;
  
  // Scan onset/attack flags array
  void process(const bool* onsetFlags, int numSamples) noexcept;
  void process(bool hasOnsetInBlock, int numSamples) noexcept;

  // Parameter/Control settings:
  void setMode(Mode mode) noexcept;
  void setMode(int modeIndex) noexcept;
  void setConstraintFactor(float factor) noexcept;
  void setHostTempoBpm(float bpm) noexcept;
  void setHostSubdivision(float subdivision) noexcept;

  // Queries:
  [[nodiscard]] Mode getMode() const noexcept { return mode_.load(std::memory_order_relaxed); }
  [[nodiscard]] float getConstraintFactor() const noexcept { return constraintFactor_.load(std::memory_order_relaxed); }
  [[nodiscard]] float getHostTempoBpm() const noexcept { return hostBpm_.load(std::memory_order_relaxed); }
  [[nodiscard]] float getHostSubdivision() const noexcept { return hostSubdivision_.load(std::memory_order_relaxed); }
  
  [[nodiscard]] float getIntervalSeconds() const noexcept;
  [[nodiscard]] float getReleaseConstraintSeconds() const noexcept;
  [[nodiscard]] float getEffectiveRelease(float tauManual) const noexcept;
  [[nodiscard]] bool isEstimateValid() const noexcept;

private:
  void addInterval(float deltaSeconds) noexcept;
  [[nodiscard]] float calculateMedianOfHistory() const noexcept;
  void updateHostInterval() noexcept;

  static constexpr int kMaxHistory = 16;
  static constexpr int kDefaultHistoryLength = 8;
  static constexpr int kMinRequiredIntervals = 3;

  double sampleRate_ = 0.0;
  
  // Real-time safe state:
  std::uint64_t sampleCounter_ = 0;
  std::uint64_t lastOnsetTimestamp_ = 0;
  bool hasLastOnsetTimestamp_ = false;

  std::array<float, kMaxHistory> history_{};
  int historyCount_ = 0;
  int writeIndex_ = 0;
  int consecutiveOutliers_ = 0;
  
  float audioIntervalSeconds_ = 0.0f;
  float hostIntervalSeconds_ = 0.25f; // Default 120 bpm, 8th notes (0.25s)

  // Thread-safe parameters:
  std::atomic<Mode> mode_{Mode::Audio};
  std::atomic<float> constraintFactor_{0.5f};
  std::atomic<float> hostBpm_{120.0f};
  std::atomic<float> hostSubdivision_{0.5f};
  
  // Configuration limits:
  int minRequiredIntervals_ = kMinRequiredIntervals;
  float minIntervalLimit_ = 0.03f; // 30 ms (approx 33 picks/sec)
  float maxIntervalLimit_ = 3.0f;  // 3.0 seconds (20 bpm)
};

} // namespace ChugGate::DSP
