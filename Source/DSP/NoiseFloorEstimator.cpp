#include "NoiseFloorEstimator.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

NoiseFloorEstimator::NoiseFloorEstimator() noexcept {
  snrMarginDb_.fill(12.0f);
  floorDb_.fill(minimumFloorDb);
  suggestions_.noiseFloorDb.fill(minimumFloorDb);
  suggestions_.thresholdDb.fill(minimumFloorDb);
  suggestions_.openThresholdByBandDb.fill(minimumFloorDb);
  suggestions_.closeThresholdByBandDb.fill(minimumFloorDb);
  appliedSuggestions_ = suggestions_;
}

void NoiseFloorEstimator::prepare(double sampleRate, int maxBlockSize) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  bucketSamples_ = std::max(
      1, static_cast<int>(std::llround(sampleRate_ * bucketDurationSeconds)));
  bucketCapacity_ = static_cast<std::size_t>(
      std::ceil(maximumCaptureSeconds / bucketDurationSeconds) + 2.0f);

  observations_.assign(bucketCapacity_ *
                           static_cast<std::size_t>(maxFloorBands),
                       minimumFloorDb);
  percentileScratch_.assign(bucketCapacity_, minimumFloorDb);
  reset();
}

void NoiseFloorEstimator::reset() noexcept {
  clearCapture();
  state_ = LearnState::Idle;
  stateBeforeCapture_ = LearnState::Idle;
  captureDurationSeconds_ = 0.0f;
}

void NoiseFloorEstimator::startLearn() noexcept {
  if (bucketCapacity_ == 0 || sampleRate_ <= 0.0)
    return;

  stateBeforeCapture_ =
      state_ == LearnState::Capturing ? LearnState::Idle : state_;
  clearCapture();
  state_ = LearnState::Capturing;
}

void NoiseFloorEstimator::cancelLearn() noexcept {
  if (state_ != LearnState::Capturing)
    return;

  clearCapture();
  state_ = stateBeforeCapture_;
}

bool NoiseFloorEstimator::applySuggestions() noexcept {
  if (state_ != LearnState::Ready || !suggestions_.valid)
    return false;

  // This explicit copy is the only point at which the applied/live-facing
  // profile changes.
  appliedSuggestions_ = suggestions_;
  state_ = LearnState::Applied;
  return true;
}

void NoiseFloorEstimator::freeze() noexcept {
  if (state_ == LearnState::Capturing)
    clearCapture();
  state_ = LearnState::Frozen;
}

void NoiseFloorEstimator::resetProfile() noexcept {
  clearCapture();
  state_ = LearnState::Idle;
  stateBeforeCapture_ = LearnState::Idle;
  numFloorBands_ = 1;
  floorDb_.fill(minimumFloorDb);
  confidence_ = 0.0f;
  captureDurationSeconds_ = 0.0f;
  idleSpikes_ = false;
  intermittentNoise_ = false;
  idleSpikeLevelDb_ = minimumFloorDb;
  suggestions_ = Suggestions{};
  suggestions_.noiseFloorDb.fill(minimumFloorDb);
  suggestions_.thresholdDb.fill(minimumFloorDb);
  suggestions_.openThresholdByBandDb.fill(minimumFloorDb);
  suggestions_.closeThresholdByBandDb.fill(minimumFloorDb);
  appliedSuggestions_ = suggestions_;
}

void NoiseFloorEstimator::setPercentile(float percentile) noexcept {
  if (std::isfinite(percentile))
    percentile_ = std::clamp(percentile, 0.20f, 0.50f);
}

void NoiseFloorEstimator::setSnrMarginDb(float marginDb) noexcept {
  snrMarginDb_.fill(sanitiseMargin(marginDb));
}

void NoiseFloorEstimator::setSnrMarginDb(int floorBand,
                                         float marginDb) noexcept {
  if (floorBand >= 0 && floorBand < maxFloorBands)
    snrMarginDb_[static_cast<std::size_t>(floorBand)] =
        sanitiseMargin(marginDb);
}

void NoiseFloorEstimator::setCaptureLengthSeconds(float seconds) noexcept {
  if (std::isfinite(seconds))
    requestedCaptureSeconds_ = std::clamp(seconds, 1.0f, maximumCaptureSeconds);
}

void NoiseFloorEstimator::setSlowAdaptationEnabled(bool enabled) noexcept {
  slowAdaptationEnabled_ = enabled;
}

void NoiseFloorEstimator::setSlowAdaptationLambda(float lambda) noexcept {
  if (std::isfinite(lambda))
    slowAdaptationLambda_ = std::clamp(lambda, 0.9f, 0.999999f);
}

void NoiseFloorEstimator::observe(const float *logEnergyDb, int numFloorBands,
                                  int numSamples,
                                  const ProcessFlags &flags) noexcept {
  if (logEnergyDb == nullptr || numSamples <= 0 || sampleRate_ <= 0.0)
    return;

  const int bands = std::clamp(numFloorBands, 1, maxFloorBands);

  if (state_ == LearnState::Capturing) {
    numFloorBands_ = bands;
    int remaining = numSamples;

    while (remaining > 0 && state_ == LearnState::Capturing) {
      const int needed = bucketSamples_ - accumulatedBucketSamples_;
      const int consumed = std::min(remaining, needed);
      for (int band = 0; band < bands; ++band)
        bucketSums_[static_cast<std::size_t>(band)] +=
            static_cast<double>(sanitiseLevel(logEnergyDb[band])) * consumed;

      accumulatedBucketSamples_ += consumed;
      captureSamples_ += consumed;
      remaining -= consumed;

      if (accumulatedBucketSamples_ >= bucketSamples_)
        pushBucket();

      const auto targetSamples = static_cast<long long>(
          requestedCaptureSeconds_ * static_cast<float>(sampleRate_));
      if (captureSamples_ >= targetSamples) {
        if (accumulatedBucketSamples_ > 0)
          pushBucket();
        finishCapture();
      }
    }
    return;
  }

  updateSlowModel(logEnergyDb, bands, flags);
}

float NoiseFloorEstimator::getBandFloorDb(int floorBand) const noexcept {
  if (floorBand < 0 || floorBand >= numFloorBands_)
    return minimumFloorDb;
  return floorDb_[static_cast<std::size_t>(floorBand)];
}

void NoiseFloorEstimator::clearCapture() noexcept {
  observationCount_ = 0;
  observationWrite_ = 0;
  accumulatedBucketSamples_ = 0;
  bucketSums_.fill(0.0);
  captureSamples_ = 0;
}

void NoiseFloorEstimator::pushBucket() noexcept {
  if (bucketCapacity_ == 0 || accumulatedBucketSamples_ <= 0)
    return;

  for (int band = 0; band < numFloorBands_; ++band) {
    const auto offset =
        static_cast<std::size_t>(band) * bucketCapacity_ + observationWrite_;
    observations_[offset] = sanitiseLevel(
        static_cast<float>(bucketSums_[static_cast<std::size_t>(band)] /
                           static_cast<double>(accumulatedBucketSamples_)));
  }

  observationWrite_ = (observationWrite_ + 1) % bucketCapacity_;
  observationCount_ = std::min(observationCount_ + 1, bucketCapacity_);
  accumulatedBucketSamples_ = 0;
  bucketSums_.fill(0.0);
}

void NoiseFloorEstimator::finishCapture() noexcept {
  if (observationCount_ == 0) {
    state_ = LearnState::Idle;
    return;
  }

  captureDurationSeconds_ =
      static_cast<float>(static_cast<double>(captureSamples_) / sampleRate_);

  for (int band = 0; band < numFloorBands_; ++band)
    floorDb_[static_cast<std::size_t>(band)] =
        percentileForBand(band, percentile_);

  const float broadbandP75 = percentileForBand(0, 0.75f);
  const float broadbandP95 = percentileForBand(0, 0.95f);
  const float spreadDb = std::max(0.0f, broadbandP75 - floorDb_[0]);
  const float hysteresisDb = std::clamp(3.0f + 0.5f * spreadDb, 3.0f, 12.0f);

  std::size_t spikeCount = 0;
  for (std::size_t index = 0; index < observationCount_; ++index) {
    const float value = observations_[index];
    if (value > floorDb_[0] + 12.0f)
      ++spikeCount;
  }
  const float spikeFraction =
      static_cast<float>(spikeCount) / static_cast<float>(observationCount_);
  idleSpikes_ = spikeCount > 0;
  intermittentNoise_ = spikeFraction >= 0.02f && spikeFraction <= 0.35f;
  idleSpikeLevelDb_ = broadbandP95;

  const float durationConfidence = std::clamp(
      captureDurationSeconds_ / requestedCaptureSeconds_, 0.0f, 1.0f);
  const float stabilityConfidence =
      std::clamp(1.0f - spreadDb / 24.0f, 0.0f, 1.0f);
  confidence_ = durationConfidence * stabilityConfidence;

  Suggestions next{};
  next.valid = true;
  next.numBands = numFloorBands_;
  next.noiseFloorDb.fill(minimumFloorDb);
  next.thresholdDb.fill(minimumFloorDb);
  next.openThresholdByBandDb.fill(minimumFloorDb);
  next.closeThresholdByBandDb.fill(minimumFloorDb);
  next.hysteresisDb = hysteresisDb;

  for (int band = 0; band < numFloorBands_; ++band) {
    const auto index = static_cast<std::size_t>(band);
    next.noiseFloorDb[index] = floorDb_[index];
    next.thresholdDb[index] =
        sanitiseLevel(floorDb_[index] + snrMarginDb_[index]);
    next.openThresholdByBandDb[index] = next.thresholdDb[index];
    next.closeThresholdByBandDb[index] =
        sanitiseLevel(next.thresholdDb[index] - hysteresisDb);
  }

  next.openThresholdDb = next.openThresholdByBandDb[0];
  next.closeThresholdDb = next.closeThresholdByBandDb[0];
  const int highBand = numFloorBands_ - 1;
  next.highBandFloorDb = floorDb_[static_cast<std::size_t>(highBand)];
  const float highBandReference =
      numFloorBands_ >= 3 ? floorDb_[static_cast<std::size_t>(highBand - 1)]
                          : floorDb_[0];
  next.highBandFloorRecommended =
      numFloorBands_ >= maxFloorBands &&
      next.highBandFloorDb > highBandReference + 3.0f;
  next.humRejectionEnabledHint =
      numFloorBands_ >= 3 && floorDb_[1] > floorDb_[2] + 6.0f;
  suggestions_ = next;
  state_ = LearnState::Ready;
}

void NoiseFloorEstimator::updateSlowModel(const float *logEnergyDb,
                                          int numFloorBands,
                                          const ProcessFlags &flags) noexcept {
  if (!slowAdaptationEnabled_ || state_ == LearnState::Frozen ||
      !flags.confidentlyClosed || flags.transientActivity ||
      flags.signalAboveNoiseModel || flags.harmonicOrSustainedNote ||
      flags.externalKeyActive || flags.decayingNote ||
      (state_ != LearnState::Ready && state_ != LearnState::Applied))
    return;

  const int bands = std::min(numFloorBands_, numFloorBands);
  for (int band = 0; band < bands; ++band) {
    const auto index = static_cast<std::size_t>(band);
    const float observed = sanitiseLevel(logEnergyDb[band]);

    // An independent guard protects callers that have not preclassified a
    // level excursion. Downward observations may adapt; upward updates are
    // limited to the immediate neighbourhood of the learned model.
    if (observed > floorDb_[index] + 3.0f)
      continue;

    floorDb_[index] = slowAdaptationLambda_ * floorDb_[index] +
                      (1.0f - slowAdaptationLambda_) * observed;
  }
}

float NoiseFloorEstimator::percentileForBand(int band,
                                             float percentile) noexcept {
  if (band < 0 || band >= numFloorBands_ || observationCount_ == 0)
    return minimumFloorDb;

  const auto bandOffset = static_cast<std::size_t>(band) * bucketCapacity_;
  for (std::size_t index = 0; index < observationCount_; ++index)
    percentileScratch_[index] = observations_[bandOffset + index];

  const float clamped = std::clamp(percentile, 0.0f, 1.0f);
  const auto rank = static_cast<std::size_t>(
      clamped * static_cast<float>(observationCount_ - 1));
  auto begin = percentileScratch_.begin();
  auto nth = begin + static_cast<std::ptrdiff_t>(rank);
  auto end = begin + static_cast<std::ptrdiff_t>(observationCount_);
  std::nth_element(begin, nth, end);
  return *nth;
}

float NoiseFloorEstimator::sanitiseLevel(float levelDb) noexcept {
  if (!std::isfinite(levelDb))
    return minimumFloorDb;
  return std::clamp(levelDb, minimumFloorDb, maximumFloorDb);
}

float NoiseFloorEstimator::sanitiseMargin(float marginDb) noexcept {
  if (!std::isfinite(marginDb))
    return 0.0f;
  return std::clamp(marginDb, 0.0f, 48.0f);
}

} // namespace ChugGate::DSP
