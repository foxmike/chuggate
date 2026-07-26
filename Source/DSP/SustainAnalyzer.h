#pragma once

#include "FundamentalEstimator.h"

#include <array>
#include <atomic>

namespace ChugGate::DSP {

class SustainAnalyzer {
public:
  struct Output {
    float sustainScore = 0.0f;
    float harmonicity = 0.0f;
    float spectralChange = 1.0f;
    float validDecay = 0.0f;
    float externalEvidence = 0.0f;
    float noiseLikeness = 1.0f;
    float decaySlopeDbPerSecond = 0.0f;
    float smoothedFundamentalHz = 0.0f;
    float adaptiveReleaseMs = 100.0f;
    bool sustainActive = false;
    bool sustainProtection = false;
  };

  SustainAnalyzer() noexcept = default;

  void prepare(double sampleRate, int maxBlockSize = 0) noexcept;
  void reset() noexcept;

  [[nodiscard]] Output processSample(float monoKey, float detectorLevelDb,
                                     const FundamentalEstimate &fundamental,
                                     float externalKeyEvidence = 0.0f) noexcept;

  // All buffers are caller-owned. output may be null. A single coherent
  // FundamentalEstimator snapshot is intentionally accepted for the block.
  void process(const float *monoKey, const float *detectorLevelDb,
               int numSamples, const FundamentalEstimate &fundamental,
               Output *output = nullptr,
               float externalKeyEvidence = 0.0f) noexcept;
  void processBlock(const float *monoKey, const float *detectorLevelDb,
                    int numSamples, const FundamentalEstimate &fundamental,
                    Output *output = nullptr,
                    float externalKeyEvidence = 0.0f) noexcept {
    process(monoKey, detectorLevelDb, numSamples, fundamental, output,
            externalKeyEvidence);
  }

  void setEnabled(bool enabled) noexcept;
  void setWeights(float harmonicWeight, float decayWeight,
                  float externalEvidenceWeight, float noiseWeight) noexcept;
  void setBaseRelease(float releaseMs) noexcept;
  void setAdaptiveReleaseAmount(float amount) noexcept;
  void setAdaptiveReleaseExponent(float gamma) noexcept;
  void setMaximumAdaptiveRelease(float maximumMs) noexcept;
  void setSustainThreshold(float threshold) noexcept;

  void setBaseReleaseMs(float releaseMs) noexcept { setBaseRelease(releaseMs); }
  void setAdaptiveAmount(float amount) noexcept {
    setAdaptiveReleaseAmount(amount);
  }
  void setGamma(float gamma) noexcept { setAdaptiveReleaseExponent(gamma); }
  void setMaxAdaptiveRelease(float maximumMs) noexcept {
    setMaximumAdaptiveRelease(maximumMs);
  }
  void setMaximumAdaptiveReleaseMs(float maximumMs) noexcept {
    setMaximumAdaptiveRelease(maximumMs);
  }

  [[nodiscard]] const Output &getLastOutput() const noexcept {
    return lastOutput_;
  }
  [[nodiscard]] float getSustainScore() const noexcept {
    return lastOutput_.sustainScore;
  }
  [[nodiscard]] float getHarmonicity() const noexcept {
    return lastOutput_.harmonicity;
  }
  [[nodiscard]] float getSpectralChange() const noexcept {
    return lastOutput_.spectralChange;
  }
  [[nodiscard]] float getDecaySlope() const noexcept {
    return lastOutput_.decaySlopeDbPerSecond;
  }
  [[nodiscard]] float getValidDecay() const noexcept {
    return lastOutput_.validDecay;
  }
  [[nodiscard]] float getNoiseLikeness() const noexcept {
    return lastOutput_.noiseLikeness;
  }
  [[nodiscard]] float getSmoothedFundamental() const noexcept {
    return lastOutput_.smoothedFundamentalHz;
  }
  [[nodiscard]] float getAdaptiveRelease() const noexcept {
    return lastOutput_.adaptiveReleaseMs;
  }
  [[nodiscard]] float getTauReleaseEff() const noexcept {
    return getAdaptiveRelease();
  }
  [[nodiscard]] bool isSustainActive() const noexcept {
    return lastOutput_.sustainActive;
  }
  [[nodiscard]] bool isSustainProtectionActive() const noexcept {
    return lastOutput_.sustainProtection;
  }
  [[nodiscard]] bool isProtectionActive() const noexcept {
    return isSustainProtectionActive();
  }

private:
  static constexpr int analysisWindowSize_ = 512;
  static constexpr int spectrumBins_ = 64;

  void updateAnalysis(float detectorLevelDb,
                      const FundamentalEstimate &fundamental,
                      float externalKeyEvidence) noexcept;
  void
  updateSmoothedFundamental(const FundamentalEstimate &fundamental) noexcept;
  void calculateSpectrum() noexcept;
  [[nodiscard]] float calculateHarmonicity(float fundamentalHz) const noexcept;
  [[nodiscard]] float sampleAt(int chronologicalIndex) const noexcept;
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
  float smoothedLogFundamental_ = 0.0f;
  bool haveSmoothedFundamental_ = false;
  float previousAnalysisLevelDb_ = -100.0f;
  bool havePreviousLevel_ = false;

  std::atomic<bool> enabled_{true};
  std::atomic<float> harmonicWeight_{0.55f};
  std::atomic<float> decayWeight_{0.25f};
  std::atomic<float> externalWeight_{0.20f};
  std::atomic<float> noiseWeight_{0.25f};
  std::atomic<float> baseReleaseMs_{100.0f};
  std::atomic<float> adaptiveAmount_{3.0f};
  std::atomic<float> adaptiveExponent_{1.5f};
  std::atomic<float> maximumAdaptiveReleaseMs_{1000.0f};
  std::atomic<float> sustainThreshold_{0.35f};

  Output lastOutput_{};
};

} // namespace ChugGate::DSP
