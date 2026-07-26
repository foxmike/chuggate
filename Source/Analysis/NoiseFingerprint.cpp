#include "NoiseFingerprint.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ChugGate::Analysis {

namespace {

inline int sanitiseCount(int v, int lo, int hi) noexcept {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

inline void copyClipped(const float *src, int srcCount, float *dst,
                        int dstCapacity) noexcept {
  if (!src || !dst || dstCapacity <= 0)
    return;
  const int n = sanitiseCount(srcCount, 0, dstCapacity);
  if (n > 0) {
    std::memcpy(dst, src, static_cast<std::size_t>(n) * sizeof(float));
  }
}

} // namespace

void NoiseFingerprint::prepare(double sampleRate,
                               int numBins) noexcept {
  sampleRate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate
                                                             : 0.0;
  numBins_ = sanitiseCount(numBins, minNumBins, maxNumBins);
  numMultibandThresholds_ = 0;

  medianFloor_.assign(static_cast<std::size_t>(numBins_), 0.0f);
  upperPercentile_.assign(static_cast<std::size_t>(numBins_), 0.0f);
  detectorFilterResponse_.assign(static_cast<std::size_t>(numBins_), 0.0f);
  multibandThresholds_.assign(maxMultibandThresholds, 0.0f);

  reset();
}

void NoiseFingerprint::reset() noexcept {
  if (!medianFloor_.empty()) {
    std::fill(medianFloor_.begin(), medianFloor_.end(), 0.0f);
  }
  if (!upperPercentile_.empty()) {
    std::fill(upperPercentile_.begin(), upperPercentile_.end(), 0.0f);
  }
  if (!detectorFilterResponse_.empty()) {
    std::fill(detectorFilterResponse_.begin(), detectorFilterResponse_.end(),
              0.0f);
  }
  if (!multibandThresholds_.empty()) {
    std::fill(multibandThresholds_.begin(), multibandThresholds_.end(), 0.0f);
  }
  numMultibandThresholds_ = 0;

  humFiftyHz_.store(false, std::memory_order_relaxed);
  humSixtyHz_.store(false, std::memory_order_relaxed);
  humHarmonics_.store(false, std::memory_order_relaxed);
  hfHiss_.store(false, std::memory_order_relaxed);
  intermittentBursts_.store(false, std::memory_order_relaxed);
  humPeakBin_.store(-1, std::memory_order_relaxed);
  harmonicPeakBin_.store(-1, std::memory_order_relaxed);
  hissPeakBin_.store(-1, std::memory_order_relaxed);
}

void NoiseFingerprint::setQualityMode(QualityMode mode) noexcept {
  qualityMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
  if (mode == QualityMode::Off) {
    // Off freezes further updates AND clears existing data so the next
    // paint shows an empty background. Subsequent set* calls become
    // no-ops until the mode is changed back.
    reset();
  }
}

QualityMode NoiseFingerprint::getQualityMode() const noexcept {
  return static_cast<QualityMode>(
      qualityMode_.load(std::memory_order_relaxed));
}

void NoiseFingerprint::updateFromLearnedProfile(
    const LearnedProfile &profile) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  if (numBins_ <= 0) {
    return;
  }

  if (profile.medianFloor && profile.numBins > 0) {
    copyClipped(profile.medianFloor, profile.numBins, medianFloor_.data(),
                numBins_);
  }
  if (profile.upperPercentile && profile.numBins > 0) {
    copyClipped(profile.upperPercentile, profile.numBins,
                upperPercentile_.data(), numBins_);
  }

  humFiftyHz_.store(profile.humFiftyHz, std::memory_order_relaxed);
  humSixtyHz_.store(profile.humSixtyHz, std::memory_order_relaxed);
  humHarmonics_.store(profile.hasHumHarmonics, std::memory_order_relaxed);
  hfHiss_.store(profile.hasHighFrequencyHiss, std::memory_order_relaxed);
  intermittentBursts_.store(profile.hasIntermittentBursts,
                             std::memory_order_relaxed);

  // Caller-supplied peak bins are stored verbatim. -1 means "no marker".
  humPeakBin_.store(profile.humPeakBin, std::memory_order_relaxed);
  harmonicPeakBin_.store(profile.harmonicPeakBin, std::memory_order_relaxed);
  hissPeakBin_.store(profile.hissPeakBin, std::memory_order_relaxed);

  if (profile.detectorFilterResponse && profile.detectorFilterLength > 0) {
    copyClipped(profile.detectorFilterResponse, profile.detectorFilterLength,
                detectorFilterResponse_.data(), numBins_);
  }
  if (profile.multibandThresholds && profile.numMultibandThresholds > 0) {
    const int n = sanitiseCount(profile.numMultibandThresholds, 0,
                                maxMultibandThresholds);
    std::memcpy(multibandThresholds_.data(), profile.multibandThresholds,
                static_cast<std::size_t>(n) * sizeof(float));
    numMultibandThresholds_ = n;
  }

  // A profile may carry a different sample rate than the one we were
  // prepared with. The caller has already mapped Hz→bin in that case, so
  // we leave the bin indices alone; only update the axis sample rate
  // when the profile supplies a positive value (used by the GUI for the
  // bin→Hz tick labels).
  if (profile.sampleRate > 0.0 && std::isfinite(profile.sampleRate)) {
    sampleRate_ = profile.sampleRate;
  }
}

void NoiseFingerprint::setSpectrum(const float *medianFloor,
                                   const float *upperPercentile,
                                   int count) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  if (numBins_ <= 0 || count <= 0) {
    return;
  }
  const int n = sanitiseCount(count, 0, numBins_);
  if (medianFloor) {
    std::memcpy(medianFloor_.data(), medianFloor,
                static_cast<std::size_t>(n) * sizeof(float));
  }
  if (upperPercentile) {
    std::memcpy(upperPercentile_.data(), upperPercentile,
                static_cast<std::size_t>(n) * sizeof(float));
  }
}

void NoiseFingerprint::setMultibandThresholds(const float *thresholds,
                                              int count) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  if (!thresholds || count <= 0) {
    return;
  }
  const int n = sanitiseCount(count, 0, maxMultibandThresholds);
  std::memcpy(multibandThresholds_.data(), thresholds,
              static_cast<std::size_t>(n) * sizeof(float));
  numMultibandThresholds_ = n;
}

void NoiseFingerprint::setDetectorFilterResponse(const float *response,
                                                 int count) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  if (numBins_ <= 0 || count <= 0) {
    return;
  }
  const int n = sanitiseCount(count, 0, numBins_);
  if (response) {
    std::memcpy(detectorFilterResponse_.data(), response,
                static_cast<std::size_t>(n) * sizeof(float));
  }
}

const float *NoiseFingerprint::getMedianFloor() const noexcept {
  return numBins_ > 0 ? medianFloor_.data() : nullptr;
}

const float *NoiseFingerprint::getUpperPercentile() const noexcept {
  return numBins_ > 0 ? upperPercentile_.data() : nullptr;
}

const float *NoiseFingerprint::getDetectorFilterResponse() const noexcept {
  return numBins_ > 0 ? detectorFilterResponse_.data() : nullptr;
}

const float *NoiseFingerprint::getMultibandThresholds() const noexcept {
  return numMultibandThresholds_ > 0 ? multibandThresholds_.data() : nullptr;
}

std::span<const float> NoiseFingerprint::getMedianFloorSpan() const noexcept {
  return std::span<const float>(medianFloor_.data(),
                                static_cast<std::size_t>(numBins_));
}

std::span<const float>
NoiseFingerprint::getUpperPercentileSpan() const noexcept {
  return std::span<const float>(upperPercentile_.data(),
                                static_cast<std::size_t>(numBins_));
}

std::span<const float>
NoiseFingerprint::getDetectorFilterResponseSpan() const noexcept {
  return std::span<const float>(detectorFilterResponse_.data(),
                                static_cast<std::size_t>(numBins_));
}

std::span<const float>
NoiseFingerprint::getMultibandThresholdsSpan() const noexcept {
  return std::span<const float>(
      multibandThresholds_.data(),
      static_cast<std::size_t>(numMultibandThresholds_));
}

float NoiseFingerprint::binCenterHz(int bin) const noexcept {
  if (sampleRate_ <= 0.0 || numBins_ <= 0) {
    return 0.0f;
  }
  const int b = sanitiseCount(bin, 0, numBins_ - 1);
  // Linear mid-bin: (b + 0.5) * (sampleRate / 2) / numBins.
  return (static_cast<float>(b) + 0.5f) *
         (static_cast<float>(sampleRate_) * 0.5f) /
         static_cast<float>(numBins_);
}

int NoiseFingerprint::binForFrequency(float hz) const noexcept {
  if (sampleRate_ <= 0.0 || numBins_ <= 0 || !(hz >= 0.0f)) {
    return -1;
  }
  const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
  if (nyquist <= 0.0f) {
    return -1;
  }
  const float scaled = hz * static_cast<float>(numBins_) / nyquist;
  const int b = static_cast<int>(std::floor(scaled));
  return sanitiseCount(b, 0, numBins_ - 1);
}

int NoiseFingerprint::copyMedianFloor(float *dst, int dstCapacity) const
    noexcept {
  if (!dst || dstCapacity <= 0 || numBins_ <= 0) {
    return 0;
  }
  const int n = sanitiseCount(numBins_, 0, dstCapacity);
  std::memcpy(dst, medianFloor_.data(),
              static_cast<std::size_t>(n) * sizeof(float));
  return n;
}

int NoiseFingerprint::copyUpperPercentile(float *dst,
                                          int dstCapacity) const noexcept {
  if (!dst || dstCapacity <= 0 || numBins_ <= 0) {
    return 0;
  }
  const int n = sanitiseCount(numBins_, 0, dstCapacity);
  std::memcpy(dst, upperPercentile_.data(),
              static_cast<std::size_t>(n) * sizeof(float));
  return n;
}

int NoiseFingerprint::copyDetectorFilterResponse(float *dst,
                                                 int dstCapacity) const
    noexcept {
  if (!dst || dstCapacity <= 0 || numBins_ <= 0) {
    return 0;
  }
  const int n = sanitiseCount(numBins_, 0, dstCapacity);
  std::memcpy(dst, detectorFilterResponse_.data(),
              static_cast<std::size_t>(n) * sizeof(float));
  return n;
}

int NoiseFingerprint::copyMultibandThresholds(float *dst,
                                              int dstCapacity) const noexcept {
  if (!dst || dstCapacity <= 0 || numMultibandThresholds_ <= 0) {
    return 0;
  }
  const int n = sanitiseCount(numMultibandThresholds_, 0, dstCapacity);
  std::memcpy(dst, multibandThresholds_.data(),
              static_cast<std::size_t>(n) * sizeof(float));
  return n;
}

} // namespace ChugGate::Analysis