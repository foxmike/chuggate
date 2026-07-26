#include "FeedbackDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ChugGate::DSP {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float minimumLevelDb = -100.0f;
constexpr float spectrumEpsilon = 1.0e-12f;

} // namespace

void FeedbackDetector::prepare(double sampleRate, int maxBlockSize) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  for (int n = 0; n < analysisWindowSize_; ++n) {
    window_[static_cast<std::size_t>(n)] =
        0.5f - 0.5f * std::cos(2.0f * pi * static_cast<float>(n) /
                               static_cast<float>(analysisWindowSize_ - 1));
  }
  controlHopSamples_ =
      sampleRate_ > 0.0
          ? std::max(1, static_cast<int>(std::lround(sampleRate_ * 0.01)))
          : 1;
  reset();
}

void FeedbackDetector::reset() noexcept {
  sampleRing_.fill(0.0f);
  spectrum_.fill(0.0f);
  previousNormalizedSpectrum_.fill(0.0f);
  samplesUntilAnalysis_ = controlHopSamples_;
  writeIndex_ = 0;
  samplesInWindow_ = 0;
  havePreviousSpectrum_ = false;
  previousAnalysisLevelDb_ = minimumLevelDb;
  havePreviousLevel_ = false;
  previousInput_ = 0.0f;
  transientAccumulator_ = 0.0f;
  transientSampleCount_ = 0;
  transientDensity_ = 0.0f;
  qualifyingSamples_ = 0;
  protectionSamples_ = 0;
  safetyLatched_ = false;
  lastOutput_ = {};
  lastOutput_.spectralChange = 1.0f;
  lastOutput_.suggestedFloorDb =
      feedbackFloorDb_.load(std::memory_order_relaxed);
}

FeedbackDetector::Output
FeedbackDetector::processSample(float rawMonoKey, float rawDetectorLevelDb,
                                const FundamentalEstimate &fundamental,
                                float rawTransientStrength,
                                bool externalDIActive) noexcept {
  const float monoKey = std::clamp(finiteOr(rawMonoKey, 0.0f), -16.0f, 16.0f);
  const float detectorLevelDb = std::clamp(
      finiteOr(rawDetectorLevelDb, minimumLevelDb), minimumLevelDb, 24.0f);

  sampleRing_[static_cast<std::size_t>(writeIndex_)] = monoKey;
  writeIndex_ = (writeIndex_ + 1) % analysisWindowSize_;
  samplesInWindow_ = std::min(samplesInWindow_ + 1, analysisWindowSize_);

  float transient = finiteOr(rawTransientStrength, -1.0f);
  if (transient < 0.0f)
    transient =
        std::clamp(std::abs(monoKey - previousInput_) * 4.0f, 0.0f, 1.0f);
  else
    transient = std::clamp(transient, 0.0f, 1.0f);
  previousInput_ = monoKey;
  transientAccumulator_ += transient;
  ++transientSampleCount_;

  if (--samplesUntilAnalysis_ <= 0) {
    samplesUntilAnalysis_ = controlHopSamples_;
    updateAnalysis(detectorLevelDb, fundamental, externalDIActive);
  }

  if (lastOutput_.protectOpen) {
    if (protectionSamples_ != std::numeric_limits<std::uint64_t>::max())
      ++protectionSamples_;
    const std::uint64_t maximumProtection = std::min(
        millisecondsToSamples(maximumHoldMs_.load(std::memory_order_relaxed)),
        millisecondsToSamples(
            safetyTimeoutMs_.load(std::memory_order_relaxed)));
    if (maximumProtection == 0 || protectionSamples_ >= maximumProtection)
      clearProtection(true);
    else
      updateRemainingTime();
  }

  return lastOutput_;
}

void FeedbackDetector::process(const float *monoKey,
                               const float *detectorLevelDb, int numSamples,
                               const FundamentalEstimate &fundamental,
                               const float *transientStrength,
                               bool externalDIActive, Output *output) noexcept {
  if (monoKey == nullptr || detectorLevelDb == nullptr || numSamples <= 0)
    return;

  for (int i = 0; i < numSamples; ++i) {
    const float transient =
        transientStrength != nullptr ? transientStrength[i] : -1.0f;
    const Output result =
        processSample(monoKey[i], detectorLevelDb[i], fundamental, transient,
                      externalDIActive);
    if (output != nullptr)
      output[i] = result;
  }
}

void FeedbackDetector::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

void FeedbackDetector::setSensitivity(float sensitivity) noexcept {
  sensitivity_.store(std::clamp(finiteOr(sensitivity, 0.5f), 0.0f, 1.0f),
                     std::memory_order_relaxed);
}

void FeedbackDetector::setMinimumDuration(float durationMs) noexcept {
  minimumDurationMs_.store(
      std::clamp(finiteOr(durationMs, 300.0f), 0.0f, 30000.0f),
      std::memory_order_relaxed);
}

void FeedbackDetector::setMaximumHold(float holdMs) noexcept {
  // A non-zero mandatory bound cannot be disabled through parameter input.
  maximumHoldMs_.store(std::clamp(finiteOr(holdMs, 5000.0f), 1.0f, 60000.0f),
                       std::memory_order_relaxed);
}

void FeedbackDetector::setAllowedFrequencyRange(float minimumHz,
                                                float maximumHz) noexcept {
  const float minimum = std::clamp(finiteOr(minimumHz, 70.0f), 20.0f, 10000.0f);
  const float maximum =
      std::clamp(finiteOr(maximumHz, 2000.0f), minimum, 20000.0f);
  minimumFrequencyHz_.store(minimum, std::memory_order_relaxed);
  maximumFrequencyHz_.store(maximum, std::memory_order_relaxed);
}

void FeedbackDetector::setFloorDuringFeedback(float floorDb) noexcept {
  feedbackFloorDb_.store(std::clamp(finiteOr(floorDb, -18.0f), -100.0f, 0.0f),
                         std::memory_order_relaxed);
}

void FeedbackDetector::setRequireExternalDIActivity(bool required) noexcept {
  requireExternalDIActivity_.store(required, std::memory_order_relaxed);
}

void FeedbackDetector::setSafetyTimeout(float timeoutMs) noexcept {
  // Safety timeout is mandatory and always finite/non-zero.
  safetyTimeoutMs_.store(
      std::clamp(finiteOr(timeoutMs, 8000.0f), 1.0f, 60000.0f),
      std::memory_order_relaxed);
}

void FeedbackDetector::setMinimumHarmonicity(
    float minimumHarmonicity) noexcept {
  // Retaining a small positive lower bound prevents callers from turning the
  // mandatory noise safety gate into an unconditional pass.
  minimumHarmonicity_.store(
      std::clamp(finiteOr(minimumHarmonicity, 0.45f), 0.05f, 0.95f),
      std::memory_order_relaxed);
}

void FeedbackDetector::setWeights(float harmonicWeight, float stabilityWeight,
                                  float gainSlopeWeight,
                                  float transientWeight) noexcept {
  harmonicWeight_.store(std::clamp(finiteOr(harmonicWeight, 0.0f), 0.0f, 2.0f),
                        std::memory_order_relaxed);
  stabilityWeight_.store(
      std::clamp(finiteOr(stabilityWeight, 0.0f), 0.0f, 2.0f),
      std::memory_order_relaxed);
  gainSlopeWeight_.store(
      std::clamp(finiteOr(gainSlopeWeight, 0.0f), 0.0f, 2.0f),
      std::memory_order_relaxed);
  transientWeight_.store(
      std::clamp(finiteOr(transientWeight, 0.0f), 0.0f, 2.0f),
      std::memory_order_relaxed);
}

void FeedbackDetector::updateAnalysis(float detectorLevelDb,
                                      const FundamentalEstimate &fundamental,
                                      bool externalDIActive) noexcept {
  const float averageTransient =
      transientSampleCount_ > 0
          ? transientAccumulator_ / static_cast<float>(transientSampleCount_)
          : 0.0f;
  transientDensity_ = std::clamp(
      0.8f * transientDensity_ + 0.2f * averageTransient, 0.0f, 1.0f);
  transientAccumulator_ = 0.0f;
  transientSampleCount_ = 0;

  const float hopSeconds =
      sampleRate_ > 0.0 ? static_cast<float>(controlHopSamples_ / sampleRate_)
                        : 0.01f;
  float slope = 0.0f;
  if (havePreviousLevel_ && hopSeconds > 0.0f)
    slope = (detectorLevelDb - previousAnalysisLevelDb_) / hopSeconds;
  previousAnalysisLevelDb_ = detectorLevelDb;
  havePreviousLevel_ = true;

  lastOutput_.suggestedFloorDb =
      feedbackFloorDb_.load(std::memory_order_relaxed);
  if (!enabled_.load(std::memory_order_relaxed)) {
    clearProtection(false);
    lastOutput_.confidence = 0.0f;
    return;
  }

  calculateSpectrum();
  float totalPower = 0.0f;
  for (float power : spectrum_)
    totalPower += power;

  float spectralChange = 1.0f;
  if (totalPower > spectrumEpsilon) {
    float distance = 0.0f;
    for (int bin = 0; bin < spectrumBins_; ++bin) {
      const float normalized =
          spectrum_[static_cast<std::size_t>(bin)] / totalPower;
      if (havePreviousSpectrum_)
        distance += std::abs(
            normalized -
            previousNormalizedSpectrum_[static_cast<std::size_t>(bin)]);
      previousNormalizedSpectrum_[static_cast<std::size_t>(bin)] = normalized;
    }
    if (havePreviousSpectrum_)
      spectralChange = std::clamp(0.5f * distance, 0.0f, 1.0f);
    havePreviousSpectrum_ = true;
  }

  const float frequency = finiteOr(fundamental.estimatedFundamental, 0.0f);
  float harmonicity = calculateHarmonicity(frequency);
  if (!fundamental.validity)
    harmonicity = 0.0f;
  else
    harmonicity *=
        std::clamp(finiteOr(fundamental.confidence, 0.0f), 0.0f, 1.0f);

  const float gainSlopeEvidence =
      std::clamp((slope + 20.0f) / 20.0f, 0.0f, 1.0f);
  const float confidence = std::clamp(
      harmonicWeight_.load(std::memory_order_relaxed) * harmonicity +
          stabilityWeight_.load(std::memory_order_relaxed) *
              (1.0f - spectralChange) +
          gainSlopeWeight_.load(std::memory_order_relaxed) * gainSlopeEvidence -
          transientWeight_.load(std::memory_order_relaxed) * transientDensity_,
      0.0f, 1.0f);

  lastOutput_.confidence = confidence;
  lastOutput_.harmonicity = harmonicity;
  lastOutput_.spectralChange = spectralChange;
  lastOutput_.levelSlopeEvidence = gainSlopeEvidence;
  lastOutput_.transientDensity = transientDensity_;

  const float minimumH = minimumHarmonicity_.load(std::memory_order_relaxed);
  const bool harmonicGate = harmonicity >= minimumH;
  const bool frequencyGate =
      fundamental.validity &&
      frequency >= minimumFrequencyHz_.load(std::memory_order_relaxed) &&
      frequency <= maximumFrequencyHz_.load(std::memory_order_relaxed);
  const bool externalGate =
      !requireExternalDIActivity_.load(std::memory_order_relaxed) ||
      externalDIActive;
  const float threshold =
      0.70f - 0.30f * sensitivity_.load(std::memory_order_relaxed);
  const bool qualified =
      harmonicGate && frequencyGate && externalGate && confidence >= threshold;

  // The mandatory harmonicity/frequency/external gates end protection
  // immediately. There is deliberately no "hold through" low-H noise.
  if (!harmonicGate || !frequencyGate || !externalGate) {
    clearProtection(false);
    safetyLatched_ = false;
    return;
  }

  if (!qualified) {
    clearProtection(false);
    safetyLatched_ = false;
    return;
  }

  if (safetyLatched_)
    return;

  if (!lastOutput_.protectOpen) {
    const std::uint64_t hop = static_cast<std::uint64_t>(controlHopSamples_);
    if (qualifyingSamples_ <= std::numeric_limits<std::uint64_t>::max() - hop)
      qualifyingSamples_ += hop;
    else
      qualifyingSamples_ = std::numeric_limits<std::uint64_t>::max();

    if (qualifyingSamples_ >= millisecondsToSamples(minimumDurationMs_.load(
                                  std::memory_order_relaxed))) {
      lastOutput_.feedbackActive = true;
      lastOutput_.protectOpen = true;
      protectionSamples_ = 0;
      updateRemainingTime();
    }
  } else {
    lastOutput_.feedbackActive = true;
  }
}

void FeedbackDetector::calculateSpectrum() noexcept {
  spectrum_.fill(0.0f);
  if (samplesInWindow_ < analysisWindowSize_ || sampleRate_ <= 0.0)
    return;

  for (int bin = 0; bin < spectrumBins_; ++bin) {
    const float phaseStep = 2.0f * pi * static_cast<float>(bin + 1) /
                            static_cast<float>(analysisWindowSize_);
    const float stepReal = std::cos(phaseStep);
    const float stepImaginary = std::sin(phaseStep);
    float oscillatorReal = 1.0f;
    float oscillatorImaginary = 0.0f;
    float real = 0.0f;
    float imaginary = 0.0f;
    for (int n = 0; n < analysisWindowSize_; ++n) {
      const float sample = sampleAt(n) * window_[static_cast<std::size_t>(n)];
      real += sample * oscillatorReal;
      imaginary -= sample * oscillatorImaginary;
      const float nextReal =
          oscillatorReal * stepReal - oscillatorImaginary * stepImaginary;
      oscillatorImaginary =
          oscillatorImaginary * stepReal + oscillatorReal * stepImaginary;
      oscillatorReal = nextReal;
    }
    spectrum_[static_cast<std::size_t>(bin)] =
        real * real + imaginary * imaginary;
  }
}

float FeedbackDetector::calculateHarmonicity(
    float fundamentalHz) const noexcept {
  if (fundamentalHz <= 0.0f || sampleRate_ <= 0.0)
    return 0.0f;

  float totalPower = 0.0f;
  for (float power : spectrum_)
    totalPower += power;
  if (totalPower <= spectrumEpsilon)
    return 0.0f;

  const float binWidth =
      static_cast<float>(sampleRate_) / static_cast<float>(analysisWindowSize_);
  const float highestFrequency = binWidth * static_cast<float>(spectrumBins_);
  std::array<bool, spectrumBins_> harmonicBins{};
  for (int harmonic = 1; harmonic <= 8; ++harmonic) {
    const float frequency = fundamentalHz * static_cast<float>(harmonic);
    if (frequency > highestFrequency + binWidth)
      break;
    const int centre = static_cast<int>(std::lround(frequency / binWidth)) - 1;
    for (int offset = -1; offset <= 1; ++offset) {
      const int bin = centre + offset;
      if (bin >= 0 && bin < spectrumBins_)
        harmonicBins[static_cast<std::size_t>(bin)] = true;
    }
  }

  float harmonicPower = 0.0f;
  for (int bin = 0; bin < spectrumBins_; ++bin) {
    if (harmonicBins[static_cast<std::size_t>(bin)])
      harmonicPower += spectrum_[static_cast<std::size_t>(bin)];
  }
  return std::clamp(harmonicPower / (totalPower + spectrumEpsilon), 0.0f, 1.0f);
}

float FeedbackDetector::sampleAt(int chronologicalIndex) const noexcept {
  return sampleRing_[static_cast<std::size_t>(
      (writeIndex_ + chronologicalIndex) % analysisWindowSize_)];
}

std::uint64_t
FeedbackDetector::millisecondsToSamples(float timeMs) const noexcept {
  if (sampleRate_ <= 0.0)
    return 0;
  const double samples =
      sampleRate_ * static_cast<double>(std::max(0.0f, timeMs)) * 0.001;
  return static_cast<std::uint64_t>(std::clamp(
      samples, 0.0,
      static_cast<double>(std::numeric_limits<std::uint64_t>::max())));
}

void FeedbackDetector::clearProtection(bool safetyExpired) noexcept {
  lastOutput_.feedbackActive = false;
  lastOutput_.protectOpen = false;
  lastOutput_.remainingHoldMs = 0.0f;
  qualifyingSamples_ = 0;
  protectionSamples_ = 0;
  if (safetyExpired)
    safetyLatched_ = true;
}

void FeedbackDetector::updateRemainingTime() noexcept {
  const std::uint64_t maximumProtection = std::min(
      millisecondsToSamples(maximumHoldMs_.load(std::memory_order_relaxed)),
      millisecondsToSamples(safetyTimeoutMs_.load(std::memory_order_relaxed)));
  const std::uint64_t remaining = maximumProtection > protectionSamples_
                                      ? maximumProtection - protectionSamples_
                                      : 0;
  lastOutput_.remainingHoldMs =
      sampleRate_ > 0.0
          ? static_cast<float>(1000.0 * static_cast<double>(remaining) /
                               sampleRate_)
          : 0.0f;
}

float FeedbackDetector::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

} // namespace ChugGate::DSP
