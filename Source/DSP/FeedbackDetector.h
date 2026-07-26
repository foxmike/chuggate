#pragma once

#include "FundamentalEstimator.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace ChugGate::DSP {

class FeedbackDetector {
public:
  struct Output {
    bool feedbackActive = false;
    float confidence = 0.0f;
    float harmonicity = 0.0f;
    float spectralChange = 1.0f;
    float levelSlopeEvidence = 0.0f;
    float transientDensity = 0.0f;
    float suggestedFloorDb = -24.0f;
    bool protectOpen = false;
    float remainingHoldMs = 0.0f;
  };

  FeedbackDetector() noexcept = default;

  void prepare(double sampleRate, int maxBlockSize = 0) noexcept;
  void reset() noexcept;

  // transientStrength may be supplied by AttackGuard. Pass a negative value
  // to use the bounded internal derivative estimate instead.
  [[nodiscard]] Output processSample(float monoKey, float detectorLevelDb,
                                     const FundamentalEstimate &fundamental,
                                     float transientStrength = -1.0f,
                                     bool externalDIActive = true) noexcept;

  // All buffers are caller-owned. transientStrength and output may be null.
  void process(const float *monoKey, const float *detectorLevelDb,
               int numSamples, const FundamentalEstimate &fundamental,
               const float *transientStrength = nullptr,
               bool externalDIActive = true, Output *output = nullptr) noexcept;
  void processBlock(const float *monoKey, const float *detectorLevelDb,
                    int numSamples, const FundamentalEstimate &fundamental,
                    const float *transientStrength = nullptr,
                    bool externalDIActive = true,
                    Output *output = nullptr) noexcept {
    process(monoKey, detectorLevelDb, numSamples, fundamental,
            transientStrength, externalDIActive, output);
  }

  void setEnabled(bool enabled) noexcept;
  void setSensitivity(float sensitivity) noexcept;
  void setMinimumDuration(float durationMs) noexcept;
  void setMaximumHold(float holdMs) noexcept;
  void setAllowedFrequencyRange(float minimumHz, float maximumHz) noexcept;
  void setFloorDuringFeedback(float floorDb) noexcept;
  void setRequireExternalDIActivity(bool required) noexcept;
  void setSafetyTimeout(float timeoutMs) noexcept;
  void setMinimumHarmonicity(float minimumHarmonicity) noexcept;
  void setWeights(float harmonicWeight, float stabilityWeight,
                  float gainSlopeWeight, float transientWeight) noexcept;

  void setMinimumDurationMs(float durationMs) noexcept {
    setMinimumDuration(durationMs);
  }
  void setMinDuration(float durationMs) noexcept {
    setMinimumDuration(durationMs);
  }
  void setMaximumHoldMs(float holdMs) noexcept { setMaximumHold(holdMs); }
  void setMaxHold(float holdMs) noexcept { setMaximumHold(holdMs); }
  void setFrequencyRange(float minimumHz, float maximumHz) noexcept {
    setAllowedFrequencyRange(minimumHz, maximumHz);
  }
  void setSafetyTimeoutMs(float timeoutMs) noexcept {
    setSafetyTimeout(timeoutMs);
  }
  void setFeedbackFloor(float floorDb) noexcept {
    setFloorDuringFeedback(floorDb);
  }
  void setRequireExternalDI(bool required) noexcept {
    setRequireExternalDIActivity(required);
  }

  [[nodiscard]] const Output &getLastOutput() const noexcept {
    return lastOutput_;
  }
  [[nodiscard]] float getFeedbackConfidence() const noexcept {
    return lastOutput_.confidence;
  }
  [[nodiscard]] bool isFeedbackActive() const noexcept {
    return lastOutput_.feedbackActive;
  }
  [[nodiscard]] float getFeedbackFloor() const noexcept {
    return lastOutput_.suggestedFloorDb;
  }
  [[nodiscard]] float getSuggestedFloor() const noexcept {
    return getFeedbackFloor();
  }
  [[nodiscard]] bool shouldProtectOpen() const noexcept {
    return lastOutput_.protectOpen;
  }
  [[nodiscard]] bool getProtectOpen() const noexcept {
    return shouldProtectOpen();
  }
  [[nodiscard]] float getRemainingHoldTime() const noexcept {
    return lastOutput_.remainingHoldMs;
  }
  [[nodiscard]] float getRemainingHoldTimeMs() const noexcept {
    return getRemainingHoldTime();
  }

private:
  static constexpr int analysisWindowSize_ = 512;
  static constexpr int spectrumBins_ = 64;

  void updateAnalysis(float detectorLevelDb,
                      const FundamentalEstimate &fundamental,
                      bool externalDIActive) noexcept;
  void calculateSpectrum() noexcept;
  [[nodiscard]] float calculateHarmonicity(float fundamentalHz) const noexcept;
  [[nodiscard]] float sampleAt(int chronologicalIndex) const noexcept;
  [[nodiscard]] std::uint64_t
  millisecondsToSamples(float timeMs) const noexcept;
  void clearProtection(bool safetyExpired) noexcept;
  void updateRemainingTime() noexcept;
  [[nodiscard]] static float finiteOr(float value, float fallback) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  int controlHopSamples_ = 1;
  int samplesUntilAnalysis_ = 1;
  int writeIndex_ = 0;
  int samplesInWindow_ = 0;
  std::array<float, analysisWindowSize_> sampleRing_{};
  std::array<float, analysisWindowSize_> window_{};
  std::array<float, spectrumBins_> spectrum_{};
  std::array<float, spectrumBins_> previousNormalizedSpectrum_{};
  bool havePreviousSpectrum_ = false;
  float previousAnalysisLevelDb_ = -100.0f;
  bool havePreviousLevel_ = false;
  float previousInput_ = 0.0f;
  float transientAccumulator_ = 0.0f;
  int transientSampleCount_ = 0;
  float transientDensity_ = 0.0f;

  std::uint64_t qualifyingSamples_ = 0;
  std::uint64_t protectionSamples_ = 0;
  bool safetyLatched_ = false;

  std::atomic<bool> enabled_{true};
  std::atomic<float> sensitivity_{0.5f};
  std::atomic<float> minimumDurationMs_{300.0f};
  std::atomic<float> maximumHoldMs_{5000.0f};
  std::atomic<float> minimumFrequencyHz_{70.0f};
  std::atomic<float> maximumFrequencyHz_{2000.0f};
  std::atomic<float> feedbackFloorDb_{-18.0f};
  std::atomic<bool> requireExternalDIActivity_{false};
  std::atomic<float> safetyTimeoutMs_{8000.0f};
  std::atomic<float> minimumHarmonicity_{0.45f};
  std::atomic<float> harmonicWeight_{0.40f};
  std::atomic<float> stabilityWeight_{0.25f};
  std::atomic<float> gainSlopeWeight_{0.25f};
  std::atomic<float> transientWeight_{0.15f};

  Output lastOutput_{};
};

} // namespace ChugGate::DSP
