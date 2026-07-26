#pragma once

#include <atomic>
#include <cstdint>

namespace ChugGate::DSP {

class AnomalyGuard {
public:
  AnomalyGuard() noexcept;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /**
   * Analyzes the main audio and key signal streams to detect anomalies.
   *
   * @param mainAudio Pointer to main signal buffer (can be null, assumes silence)
   * @param keySignal Pointer to key/sidechain signal buffer (can be null, assumes silence)
   * @param numSamples Number of samples to process
   * @param isKeyValid Flag representing whether the key stream is currently valid
   */
  void analyze(const float* mainAudio, const float* keySignal, int numSamples, bool isKeyValid = true) noexcept;

  /**
   * Alias for analyze to match processing conventions.
   */
  void process(const float* mainAudio, const float* keySignal, int numSamples, bool isKeyValid = true) noexcept {
    analyze(mainAudio, keySignal, numSamples, isKeyValid);
  }

  // Cable Spike Guard Getters
  [[nodiscard]] bool shouldSuppressOpen() const noexcept { return shouldSuppressOpen_; }
  [[nodiscard]] bool isSpikeActive() const noexcept { return isSpikeActive_; }

  // Stuck-Key Detection Getter
  [[nodiscard]] bool isStuckKeyWarning() const noexcept { return isStuckKeyWarning_; }

  // Dropout Handling Getters
  [[nodiscard]] bool isDropoutActive() const noexcept { return isDropoutActive_; }
  [[nodiscard]] bool shouldUseInternalFallback() const noexcept { return shouldUseInternalFallback_; }

  // Cable Spike Guard Setters
  void setSpikePeakThresholdDb(float db) noexcept;
  void setSpikeBodyRatioThreshold(float ratio) noexcept;
  void setSpikeDurationMs(float ms) noexcept;
  void setSpikeHoldMs(float ms) noexcept;

  // Stuck-Key Detection Setters
  void setStuckKeyHoldTimeMs(float ms) noexcept;
  void setKeyActiveThresholdDb(float db) noexcept;
  void setMainSilentThresholdDb(float db) noexcept;

  // Dropout Handling Setters
  void setDropoutHoldTimeMs(float ms) noexcept;
  void setDropoutThresholdDb(float db) noexcept;

private:
  double sampleRate_ = 48000.0;
  float envelopeAlpha_ = 0.99f;

  // Cable Spike Guard State
  std::atomic<float> spikePeakThresholdDb_{-20.0f};
  std::atomic<float> spikeBodyRatioThreshold_{0.15f};
  std::atomic<float> spikeDurationMs_{1.5f};
  std::atomic<float> spikeHoldMs_{10.0f};

  bool shouldSuppressOpen_ = false;
  bool isSpikeActive_ = false;

  bool measuringSpike_ = false;
  std::uint32_t spikeWindowCounter_ = 0;
  float spikeWindowPeak_ = 0.0f;
  float spikeWindowEnergy_ = 0.0f;
  std::uint32_t spikeWindowActiveCount_ = 0;
  std::uint32_t spikeHoldCounter_ = 0;

  // Stuck-Key Detection State
  std::atomic<float> stuckKeyHoldMs_{2000.0f};
  std::atomic<float> keyActiveThresholdDb_{-46.0f};
  std::atomic<float> mainSilentThresholdDb_{-60.0f};

  bool isStuckKeyWarning_ = false;
  std::uint32_t stuckKeyCounter_ = 0;

  // Dropout Handling State
  std::atomic<float> dropoutHoldMs_{100.0f};
  std::atomic<float> dropoutThresholdDb_{-90.0f};

  bool isDropoutActive_ = false;
  bool shouldUseInternalFallback_ = false;
  std::uint32_t dropoutCounter_ = 0;

  float keyEnvelope_ = 0.0f;
};

} // namespace ChugGate::DSP
