#include "FundamentalEstimator.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {

float sanitiseSample(float sample) noexcept {
  return std::isfinite(sample) ? std::clamp(sample, -16.0f, 16.0f) : 0.0f;
}

bool harmonicallyRelated(int firstLag, int secondLag) noexcept {
  const float smaller = static_cast<float>(std::min(firstLag, secondLag));
  const float larger = static_cast<float>(std::max(firstLag, secondLag));
  const float ratio = larger / smaller;
  const float nearestInteger = std::round(ratio);
  return nearestInteger >= 1.0f &&
         std::abs(ratio - nearestInteger) <= 0.08f * nearestInteger;
}

} // namespace

void FundamentalEstimator::prepare(double sampleRate,
                                   int maxBlockSize) noexcept {
  sampleRate_ =
      std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);

  decimationFactor_ = sampleRate_ > 0.0
                          ? std::max(1, static_cast<int>(std::ceil(
                                            sampleRate_ / targetAnalysisRate)))
                          : 1;
  analysisRate_ = sampleRate_ > 0.0 ? sampleRate_ / decimationFactor_ : 0.0;

  // Period bounds are the direct inverse of the documented 40--800 Hz band.
  minimumLag_ =
      analysisRate_ > 0.0
          ? static_cast<int>(std::floor(analysisRate_ / maximumFundamentalHz))
          : 1;
  maximumLag_ =
      analysisRate_ > 0.0
          ? static_cast<int>(std::ceil(analysisRate_ / minimumFundamentalHz))
          : 1;
  minimumLag_ = std::clamp(minimumLag_, 1, maximumCorrelationLag - 2);
  maximumLag_ =
      std::clamp(maximumLag_, minimumLag_ + 2, maximumCorrelationLag - 1);

  prepared_ =
      sampleRate_ >= 2.0 * maximumFundamentalHz && maximumLag_ > minimumLag_;

  const int defaultHop =
      sampleRate_ > 0.0
          ? std::max(1, static_cast<int>(std::lround(
                            sampleRate_ * defaultControlHopMs * 0.001)))
          : 1;
  controlHopSamples_.store(defaultHop, std::memory_order_relaxed);
  reset();
}

void FundamentalEstimator::reset() noexcept {
  analysisBuffer_.fill(0.0f);
  correlations_.fill(0.0f);
  writeIndex_ = 0;
  samplesInWindow_ = 0;
  decimationSum_ = 0.0f;
  decimationCount_ = 0;
  samplesUntilEstimate_ =
      std::max(1, controlHopSamples_.load(std::memory_order_relaxed));
  inputSampleCount_ = 0;

  snapshotSequence_.store(1, std::memory_order_release);
  publishedFundamental_.store(0.0f, std::memory_order_relaxed);
  publishedConfidence_.store(0.0f, std::memory_order_relaxed);
  publishedTimestamp_.store(0, std::memory_order_relaxed);
  publishedValidity_.store(false, std::memory_order_relaxed);
  snapshotSequence_.store(2, std::memory_order_release);
}

void FundamentalEstimator::process(const float *monoInput,
                                   int numSamples) noexcept {
  if (monoInput == nullptr || numSamples <= 0)
    return;

  for (int i = 0; i < numSamples; ++i)
    processSample(monoInput[i]);
}

void FundamentalEstimator::processSample(float monoSample) noexcept {
  ++inputSampleCount_;
  decimationSum_ += sanitiseSample(monoSample);
  ++decimationCount_;

  if (decimationCount_ >= decimationFactor_) {
    appendAnalysisSample(decimationSum_ / static_cast<float>(decimationCount_));
    decimationSum_ = 0.0f;
    decimationCount_ = 0;
  }

  --samplesUntilEstimate_;
  if (samplesUntilEstimate_ <= 0) {
    if (prepared_ && samplesInWindow_ == analysisWindowSize)
      estimate();
    else
      publish(0.0f, 0.0f, false);

    samplesUntilEstimate_ =
        std::max(1, controlHopSamples_.load(std::memory_order_relaxed));
  }
}

void FundamentalEstimator::setControlHop(int hopSamples) noexcept {
  controlHopSamples_.store(std::max(1, hopSamples), std::memory_order_relaxed);
}

FundamentalEstimate FundamentalEstimator::getEstimate() const noexcept {
  FundamentalEstimate result;

  for (;;) {
    const std::uint32_t before =
        snapshotSequence_.load(std::memory_order_acquire);
    if ((before & 1U) != 0U)
      continue;

    result.estimatedFundamental =
        publishedFundamental_.load(std::memory_order_relaxed);
    result.confidence = publishedConfidence_.load(std::memory_order_relaxed);
    result.timestamp = publishedTimestamp_.load(std::memory_order_relaxed);
    result.validity = publishedValidity_.load(std::memory_order_relaxed);

    const std::uint32_t after =
        snapshotSequence_.load(std::memory_order_acquire);
    if (before == after)
      return result;
  }
}

float FundamentalEstimator::getEstimatedFundamental() const noexcept {
  return getEstimate().estimatedFundamental;
}

float FundamentalEstimator::getConfidence() const noexcept {
  return getEstimate().confidence;
}

std::uint64_t FundamentalEstimator::getTimestamp() const noexcept {
  return getEstimate().timestamp;
}

bool FundamentalEstimator::isValid() const noexcept {
  return getEstimate().validity;
}

void FundamentalEstimator::appendAnalysisSample(float sample) noexcept {
  analysisBuffer_[static_cast<std::size_t>(writeIndex_)] = sample;
  writeIndex_ = (writeIndex_ + 1) % analysisWindowSize;
  samplesInWindow_ = std::min(samplesInWindow_ + 1, analysisWindowSize);
}

void FundamentalEstimator::estimate() noexcept {
  float mean = 0.0f;
  for (int i = 0; i < analysisWindowSize; ++i)
    mean += sampleAt(i);
  mean /= static_cast<float>(analysisWindowSize);

  float energy = 0.0f;
  for (int i = 0; i < analysisWindowSize; ++i) {
    const float centred = sampleAt(i) - mean;
    energy += centred * centred;
  }
  const float rms = std::sqrt(energy / static_cast<float>(analysisWindowSize));
  if (!std::isfinite(rms) || rms < minimumRms) {
    publish(0.0f, 0.0f, false);
    return;
  }

  correlations_.fill(0.0f);
  float strongestPeak = -1.0f;
  int strongestLag = minimumLag_;
  // Compute one guard value outside either search bound so frequencies at
  // exactly 40 Hz or 800 Hz can still be classified as local maxima.
  for (int lag = minimumLag_ - 1; lag <= maximumLag_ + 1; ++lag)
    correlations_[static_cast<std::size_t>(lag)] = correlationAtLag(lag, mean);

  for (int lag = minimumLag_; lag <= maximumLag_; ++lag) {
    const float value = correlations_[static_cast<std::size_t>(lag)];
    if (value >= correlations_[static_cast<std::size_t>(lag - 1)] &&
        value > correlations_[static_cast<std::size_t>(lag + 1)] &&
        value > strongestPeak) {
      strongestPeak = value;
      strongestLag = lag;
    }
  }

  if (strongestPeak < minimumCorrelation) {
    publish(0.0f, 0.0f, false);
    return;
  }

  // Prefer the shortest strong period. This prevents a clean periodic signal
  // from being reported one or more octaves low when its repeated ACF peaks
  // have nearly identical height.
  const float acceptablePeak =
      std::max(minimumCorrelation, strongestPeak * 0.90f);
  int selectedLag = strongestLag;
  for (int lag = minimumLag_; lag <= maximumLag_; ++lag) {
    const float value = correlations_[static_cast<std::size_t>(lag)];
    if (value >= acceptablePeak &&
        value >= correlations_[static_cast<std::size_t>(lag - 1)] &&
        value > correlations_[static_cast<std::size_t>(lag + 1)]) {
      selectedLag = lag;
      break;
    }
  }

  const float selectedPeak =
      correlations_[static_cast<std::size_t>(selectedLag)];
  float competingPeak = 0.0f;
  for (int lag = minimumLag_; lag <= maximumLag_; ++lag) {
    const float value = correlations_[static_cast<std::size_t>(lag)];
    if (value < correlations_[static_cast<std::size_t>(lag - 1)] ||
        value <= correlations_[static_cast<std::size_t>(lag + 1)] ||
        std::abs(lag - selectedLag) <= 2 ||
        harmonicallyRelated(lag, selectedLag))
      continue;
    competingPeak = std::max(competingPeak, value);
  }

  const bool ambiguous = competingPeak > selectedPeak * 0.92f;
  const float separation =
      std::clamp((selectedPeak - competingPeak) / 0.25f, 0.0f, 1.0f);
  const float periodicity =
      std::clamp((selectedPeak - 0.35f) / 0.65f, 0.0f, 1.0f);
  const float confidence =
      std::clamp(periodicity * (0.70f + 0.30f * separation), 0.0f, 1.0f);

  const float left = correlations_[static_cast<std::size_t>(selectedLag - 1)];
  const float centre = selectedPeak;
  const float right = correlations_[static_cast<std::size_t>(selectedLag + 1)];
  const float denominator = left - 2.0f * centre + right;
  float fractionalOffset = 0.0f;
  if (std::abs(denominator) > 1.0e-12f)
    fractionalOffset =
        std::clamp(0.5f * (left - right) / denominator, -0.5f, 0.5f);

  const float period = static_cast<float>(selectedLag) + fractionalOffset;
  const float rawFundamental =
      period > 0.0f ? static_cast<float>(analysisRate_) / period : 0.0f;
  // The outer integer lag can straddle a fractional band-edge period. Allow
  // that half-bin guard region, then constrain the published estimate to the
  // documented band.
  const float fundamental =
      std::clamp(rawFundamental, minimumFundamentalHz, maximumFundamentalHz);
  const bool valid = !ambiguous && confidence > 0.0f &&
                     std::isfinite(rawFundamental) &&
                     rawFundamental >= minimumFundamentalHz * 0.98f &&
                     rawFundamental <= maximumFundamentalHz * 1.02f;
  publish(valid ? fundamental : 0.0f, valid ? confidence : 0.0f, valid);
}

float FundamentalEstimator::correlationAtLag(int lag,
                                             float mean) const noexcept {
  float numerator = 0.0f;
  float firstEnergy = 0.0f;
  float secondEnergy = 0.0f;
  const int count = analysisWindowSize - lag;

  for (int i = 0; i < count; ++i) {
    const float first = sampleAt(i) - mean;
    const float second = sampleAt(i + lag) - mean;
    numerator += first * second;
    firstEnergy += first * first;
    secondEnergy += second * second;
  }

  const float denominator = std::sqrt(firstEnergy * secondEnergy);
  if (!(denominator > 1.0e-12f) || !std::isfinite(denominator))
    return 0.0f;
  return std::clamp(numerator / denominator, -1.0f, 1.0f);
}

float FundamentalEstimator::sampleAt(int chronologicalIndex) const noexcept {
  const int index = (writeIndex_ + chronologicalIndex) % analysisWindowSize;
  return analysisBuffer_[static_cast<std::size_t>(index)];
}

void FundamentalEstimator::publish(float fundamentalHz, float confidence,
                                   bool validity) noexcept {
  snapshotSequence_.fetch_add(1, std::memory_order_acq_rel);
  publishedFundamental_.store(fundamentalHz, std::memory_order_relaxed);
  publishedConfidence_.store(std::clamp(confidence, 0.0f, 1.0f),
                             std::memory_order_relaxed);
  publishedTimestamp_.store(inputSampleCount_, std::memory_order_relaxed);
  publishedValidity_.store(validity, std::memory_order_relaxed);
  snapshotSequence_.fetch_add(1, std::memory_order_release);
}

} // namespace ChugGate::DSP
