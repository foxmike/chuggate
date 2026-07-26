#include "AlignmentAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {

float sanitiseAudio(float sample) noexcept {
  return std::isfinite(sample) ? std::clamp(sample, -16.0f, 16.0f) : 0.0f;
}

} // namespace

void AlignmentAnalyzer::prepare(double sampleRate, int maxBlockSize) {
  sampleRate_ =
      std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);

  powerAlpha_ = sampleRate_ > 0.0
                    ? static_cast<float>(std::exp(
                          -1.0 / (sampleRate_ * powerSmoothingMs * 0.001)))
                    : 0.0f;
  onsetDelaySamples_ =
      sampleRate_ > 0.0
          ? std::max(1, static_cast<int>(std::lround(
                            sampleRate_ * onsetDifferenceMs * 0.001)))
          : 1;
  onsetDecimation_ = sampleRate_ > 0.0
                         ? std::max(1, static_cast<int>(std::lround(
                                           sampleRate_ / onsetAnalysisRate)))
                         : 1;
  correlationSampleRate_ =
      sampleRate_ > 0.0 ? sampleRate_ / onsetDecimation_ : onsetAnalysisRate;

  const auto historySize = static_cast<std::size_t>(onsetDelaySamples_ + 1);
  keyEnvelope_.energyHistory.assign(historySize, 0.0f);
  audioEnvelope_.energyHistory.assign(historySize, 0.0f);

  const auto captureCapacity =
      static_cast<std::size_t>(
          std::ceil(maximumCalibrationSeconds * correlationSampleRate_)) +
      2U;
  // One slot remains empty to distinguish a full SPSC ring from an empty one.
  onsetRing_.assign(captureCapacity + 1U, OnsetPair{});
  pendingSamples_.assign(captureCapacity, OnsetPair{});

  const int maximumLag = static_cast<int>(
      std::ceil(maximumSearchRangeMs * 0.001 * correlationSampleRate_));
  correlationScratch_.assign(static_cast<std::size_t>(2 * maximumLag + 1),
                             0.0f);
  reset();
}

void AlignmentAnalyzer::reset() noexcept {
  state_.store(CalibrationState::Idle, std::memory_order_release);
  clearCaptureData();
  acceptedResult_ = Result{};
}

void AlignmentAnalyzer::setSearchRangeMs(float rangeMs) noexcept {
  if (std::isfinite(rangeMs))
    searchRangeMs_.store(
        std::clamp(std::abs(rangeMs), 0.01f, maximumSearchRangeMs),
        std::memory_order_relaxed);
}

void AlignmentAnalyzer::startAlign() noexcept {
  if (sampleRate_ <= 0.0 || onsetRing_.empty())
    return;

  // Ring indices remain monotonic across sessions. Generation tags let the
  // consumer discard a late sample from an earlier audio-thread session.
  state_.store(CalibrationState::Idle, std::memory_order_release);
  clearAnalysisData();
  captureGeneration_.fetch_add(1U, std::memory_order_acq_rel);
  state_.store(CalibrationState::Capturing, std::memory_order_release);
}

void AlignmentAnalyzer::cancel() noexcept {
  if (getState() != CalibrationState::Capturing)
    return;

  state_.store(CalibrationState::Idle, std::memory_order_release);
  clearAnalysisData();
}

bool AlignmentAnalyzer::accept() noexcept {
  if (getState() != CalibrationState::Ready ||
      !candidateValid_.load(std::memory_order_acquire))
    return false;

  acceptedResult_ = getResult();
  state_.store(CalibrationState::Accepted, std::memory_order_release);
  return true;
}

float AlignmentAnalyzer::calculateOnset(float rawSample,
                                        EnvelopeState &state) noexcept {
  const float sample = sanitiseAudio(rawSample);
  state.power =
      (1.0f - powerAlpha_) * sample * sample + powerAlpha_ * state.power;
  const float energy = std::sqrt(state.power + 1.0e-20f);

  auto &history = state.energyHistory;
  if (history.empty())
    return 0.0f;

  const std::size_t delayedIndex =
      (state.writeIndex + history.size() -
       (static_cast<std::size_t>(onsetDelaySamples_) % history.size())) %
      history.size();
  const float delayedEnergy = history[delayedIndex];
  history[state.writeIndex] = energy;
  state.writeIndex = (state.writeIndex + 1U) % history.size();
  return std::max(0.0f, energy - delayedEnergy);
}

void AlignmentAnalyzer::processSample(float conditionedKey,
                                      float mainAudio) noexcept {
  if (state_.load(std::memory_order_acquire) != CalibrationState::Capturing)
    return;

  const auto generation = captureGeneration_.load(std::memory_order_acquire);
  if (producerGeneration_ != generation) {
    clearProducerData();
    producerGeneration_ = generation;
  }

  const float keyOnset = calculateOnset(conditionedKey, keyEnvelope_);
  const float audioOnset = calculateOnset(mainAudio, audioEnvelope_);
  decimatedKeyMaximum_ = std::max(decimatedKeyMaximum_, keyOnset);
  decimatedAudioMaximum_ = std::max(decimatedAudioMaximum_, audioOnset);

  if (++decimationCounter_ >= onsetDecimation_) {
    pushOnsetPair(decimatedKeyMaximum_, decimatedAudioMaximum_);
    decimationCounter_ = 0;
    decimatedKeyMaximum_ = 0.0f;
    decimatedAudioMaximum_ = 0.0f;
  }
}

void AlignmentAnalyzer::process(const float *conditionedKey,
                                const float *mainAudio,
                                int numSamples) noexcept {
  if (conditionedKey == nullptr || mainAudio == nullptr || numSamples <= 0)
    return;

  for (int sample = 0; sample < numSamples; ++sample)
    processSample(conditionedKey[sample], mainAudio[sample]);
}

void AlignmentAnalyzer::pushOnsetPair(float keyOnset,
                                      float audioOnset) noexcept {
  if (onsetRing_.empty())
    return;

  const auto write = ringWrite_.load(std::memory_order_relaxed);
  const auto next =
      static_cast<std::uint32_t>((write + 1U) % onsetRing_.size());
  if (next == ringRead_.load(std::memory_order_acquire)) {
    droppedSamples_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  onsetRing_[write] = OnsetPair{keyOnset, audioOnset, producerGeneration_};
  ringWrite_.store(next, std::memory_order_release);
}

void AlignmentAnalyzer::drainRing() noexcept {
  if (onsetRing_.empty())
    return;

  auto read = ringRead_.load(std::memory_order_relaxed);
  const auto write = ringWrite_.load(std::memory_order_acquire);
  const auto generation = captureGeneration_.load(std::memory_order_acquire);
  while (read != write) {
    const OnsetPair pair = onsetRing_[read];
    if (pair.generation == generation && pendingCount_ < pendingSamples_.size())
      pendingSamples_[pendingCount_++] = pair;
    read = static_cast<std::uint32_t>((read + 1U) % onsetRing_.size());
  }
  ringRead_.store(read, std::memory_order_release);
}

bool AlignmentAnalyzer::processOfflineAnalysis() noexcept {
  if (getState() != CalibrationState::Capturing)
    return false;

  drainRing();
  return analyzeAvailableWindows(false);
}

bool AlignmentAnalyzer::finishCalibrationChunk() noexcept {
  if (getState() != CalibrationState::Capturing)
    return false;

  // Stop writes before taking the final producer snapshot.
  state_.store(CalibrationState::Idle, std::memory_order_release);
  drainRing();
  const bool analyzed = analyzeAvailableWindows(true);
  if (estimateCount_ > 0) {
    updateCandidate();
    state_.store(CalibrationState::Ready, std::memory_order_release);
    return true;
  }

  clearAnalysisData();
  state_.store(CalibrationState::Idle, std::memory_order_release);
  return analyzed;
}

bool AlignmentAnalyzer::analyzeAvailableWindows(bool finalize) noexcept {
  const auto fullWindow = static_cast<std::size_t>(std::max(
      1.0, std::round(analysisWindowSeconds * correlationSampleRate_)));
  const auto minimumWindow = static_cast<std::size_t>(
      std::max(1.0, std::round(minimumWindowSeconds * correlationSampleRate_)));
  bool analyzed = false;
  std::size_t consumed = 0;

  while (pendingCount_ - consumed >= fullWindow) {
    analyzed |= analyzeWindow(pendingSamples_.data() + consumed, fullWindow);
    consumed += fullWindow;
  }

  if (finalize && pendingCount_ - consumed >= minimumWindow) {
    analyzed |= analyzeWindow(pendingSamples_.data() + consumed,
                              pendingCount_ - consumed);
    consumed = pendingCount_;
  }

  if (consumed > 0) {
    const std::size_t remaining = pendingCount_ - consumed;
    std::move(pendingSamples_.begin() + static_cast<std::ptrdiff_t>(consumed),
              pendingSamples_.begin() +
                  static_cast<std::ptrdiff_t>(pendingCount_),
              pendingSamples_.begin());
    pendingCount_ = remaining;
  }

  if (analyzed)
    updateCandidate();
  return analyzed;
}

bool AlignmentAnalyzer::analyzeWindow(const OnsetPair *samples,
                                      std::size_t count) noexcept {
  if (samples == nullptr || count < 2)
    return false;

  const int requestedLag = static_cast<int>(
      std::lround(searchRangeMs_.load(std::memory_order_relaxed) * 0.001 *
                  correlationSampleRate_));
  const int maxLag = std::min(requestedLag, static_cast<int>(count / 3U));
  if (maxLag < 1 ||
      correlationScratch_.size() < static_cast<std::size_t>(2 * maxLag + 1))
    return false;

  float peak = -1.0f;
  int peakLag = 0;
  for (int lag = -maxLag; lag <= maxLag; ++lag) {
    const std::size_t keyStart = lag < 0 ? static_cast<std::size_t>(-lag) : 0U;
    const std::size_t audioStart = lag > 0 ? static_cast<std::size_t>(lag) : 0U;
    const std::size_t overlap = count - static_cast<std::size_t>(std::abs(lag));

    double cross = 0.0;
    double keySquare = 0.0;
    double audioSquare = 0.0;
    for (std::size_t index = 0; index < overlap; ++index) {
      const float key = samples[keyStart + index].key;
      const float audio = samples[audioStart + index].audio;
      cross += static_cast<double>(key) * audio;
      keySquare += static_cast<double>(key) * key;
      audioSquare += static_cast<double>(audio) * audio;
    }

    const double denominator = std::sqrt(keySquare * audioSquare) + 1.0e-20;
    const float correlation =
        static_cast<float>(std::clamp(cross / denominator, 0.0, 1.0));
    correlationScratch_[static_cast<std::size_t>(lag + maxLag)] = correlation;
    if (correlation > peak) {
      peak = correlation;
      peakLag = lag;
    }
  }

  if (peak <= 1.0e-5f)
    return false;

  float fractionalLag = static_cast<float>(peakLag);
  if (peakLag > -maxLag && peakLag < maxLag) {
    const auto center = static_cast<std::size_t>(peakLag + maxLag);
    const float left = correlationScratch_[center - 1U];
    const float middle = correlationScratch_[center];
    const float right = correlationScratch_[center + 1U];
    const float curvature = left - 2.0f * middle + right;
    if (std::abs(curvature) > 1.0e-9f)
      fractionalLag +=
          std::clamp(0.5f * (left - right) / curvature, -0.5f, 0.5f);
  }

  const int exclusion = std::max(
      1, static_cast<int>(std::lround(0.001 * correlationSampleRate_)));
  float secondPeak = 0.0f;
  for (int lag = -maxLag; lag <= maxLag; ++lag) {
    if (std::abs(lag - peakLag) > exclusion)
      secondPeak =
          std::max(secondPeak,
                   correlationScratch_[static_cast<std::size_t>(lag + maxLag)]);
  }
  const float prominence = std::clamp(
      (peak - secondPeak) / std::max(1.0e-6f, 1.0f - secondPeak), 0.0f, 1.0f);
  const float confidence = peak * (0.5f + 0.5f * prominence);
  addEstimate(fractionalLag, confidence);
  return true;
}

void AlignmentAnalyzer::addEstimate(float lagSamples,
                                    float confidence) noexcept {
  if (estimateCount_ < maximumEstimates) {
    estimates_[static_cast<std::size_t>(estimateCount_++)] =
        Estimate{lagSamples, confidence};
    return;
  }

  std::move(estimates_.begin() + 1, estimates_.end(), estimates_.begin());
  estimates_.back() = Estimate{lagSamples, confidence};
}

float AlignmentAnalyzer::median(float *values, int count) noexcept {
  if (values == nullptr || count <= 0)
    return 0.0f;

  auto begin = values;
  auto middle = values + count / 2;
  auto end = values + count;
  std::nth_element(begin, middle, end);
  const float upper = *middle;
  if ((count & 1) != 0)
    return upper;

  const float lower = *std::max_element(begin, middle);
  return 0.5f * (lower + upper);
}

void AlignmentAnalyzer::updateCandidate() noexcept {
  if (estimateCount_ <= 0)
    return;

  for (int index = 0; index < estimateCount_; ++index)
    medianScratch_[static_cast<std::size_t>(index)] =
        estimates_[static_cast<std::size_t>(index)].lagSamples;
  const float medianLag = median(medianScratch_.data(), estimateCount_);

  for (int index = 0; index < estimateCount_; ++index)
    medianScratch_[static_cast<std::size_t>(index)] = std::abs(
        estimates_[static_cast<std::size_t>(index)].lagSamples - medianLag);
  const float medianDeviation = median(medianScratch_.data(), estimateCount_);

  for (int index = 0; index < estimateCount_; ++index)
    medianScratch_[static_cast<std::size_t>(index)] =
        estimates_[static_cast<std::size_t>(index)].confidence;
  const float medianConfidence = median(medianScratch_.data(), estimateCount_);

  const float offsetMs =
      static_cast<float>(medianLag * 1000.0 / correlationSampleRate_);
  const float variationMs =
      static_cast<float>(medianDeviation * 1000.0 / correlationSampleRate_);
  const float rangeMs = searchRangeMs_.load(std::memory_order_relaxed);
  const float consistency =
      std::clamp(1.0f - variationMs / std::max(0.01f, rangeMs), 0.0f, 1.0f);

  candidateOffsetMs_.store(offsetMs, std::memory_order_relaxed);
  candidateConfidence_.store(medianConfidence * consistency,
                             std::memory_order_relaxed);
  candidateVariationMs_.store(variationMs, std::memory_order_relaxed);
  candidateNeedsLookahead_.store(offsetMs < 0.0f, std::memory_order_relaxed);
  candidateEstimateCount_.store(estimateCount_, std::memory_order_relaxed);
  candidateValid_.store(true, std::memory_order_release);
}

AlignmentAnalyzer::Result AlignmentAnalyzer::getResult() const noexcept {
  Result result;
  result.valid = candidateValid_.load(std::memory_order_acquire);
  result.offsetMs = candidateOffsetMs_.load(std::memory_order_relaxed);
  result.keyDelayMs = std::max(0.0f, result.offsetMs);
  result.confidence = candidateConfidence_.load(std::memory_order_relaxed);
  result.variationMs = candidateVariationMs_.load(std::memory_order_relaxed);
  result.needsMainLookahead =
      candidateNeedsLookahead_.load(std::memory_order_relaxed);
  result.estimateCount =
      candidateEstimateCount_.load(std::memory_order_relaxed);
  return result;
}

AlignmentAnalyzer::Result
AlignmentAnalyzer::getAcceptedResult() const noexcept {
  return acceptedResult_;
}

void AlignmentAnalyzer::clearProducerData() noexcept {
  keyEnvelope_.power = 0.0f;
  keyEnvelope_.writeIndex = 0;
  std::fill(keyEnvelope_.energyHistory.begin(),
            keyEnvelope_.energyHistory.end(), 0.0f);
  audioEnvelope_.power = 0.0f;
  audioEnvelope_.writeIndex = 0;
  std::fill(audioEnvelope_.energyHistory.begin(),
            audioEnvelope_.energyHistory.end(), 0.0f);

  decimationCounter_ = 0;
  decimatedKeyMaximum_ = 0.0f;
  decimatedAudioMaximum_ = 0.0f;
}

void AlignmentAnalyzer::clearAnalysisData() noexcept {
  pendingCount_ = 0;
  estimateCount_ = 0;

  candidateValid_.store(false, std::memory_order_relaxed);
  candidateOffsetMs_.store(0.0f, std::memory_order_relaxed);
  candidateConfidence_.store(0.0f, std::memory_order_relaxed);
  candidateVariationMs_.store(0.0f, std::memory_order_relaxed);
  candidateNeedsLookahead_.store(false, std::memory_order_relaxed);
  candidateEstimateCount_.store(0, std::memory_order_relaxed);
}

void AlignmentAnalyzer::clearCaptureData() noexcept {
  clearProducerData();
  producerGeneration_ = captureGeneration_.load(std::memory_order_relaxed);
  ringWrite_.store(0, std::memory_order_relaxed);
  ringRead_.store(0, std::memory_order_relaxed);
  droppedSamples_.store(0, std::memory_order_relaxed);
  clearAnalysisData();
}

} // namespace ChugGate::DSP
