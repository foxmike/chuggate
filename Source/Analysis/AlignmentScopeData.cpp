#include "AlignmentScopeData.h"

#include <algorithm>
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

// Copy a contiguous (or wrapped) source segment of srcLen samples from a
// logical ring of srcCapacity into a flat destination of dstCapacity.
// Used to extract "the most recent srcLen samples" from a ring when the
// caller has supplied capacity equal to or greater than length.
inline void copyLinear(const float *src, int count, float *dst,
                       int dstCapacity) noexcept {
  if (!src || !dst || dstCapacity <= 0 || count <= 0)
    return;
  const int n = sanitiseCount(count, 0, dstCapacity);
  if (n > 0) {
    std::memcpy(dst, src, static_cast<std::size_t>(n) * sizeof(float));
  }
}

} // namespace

void AlignmentScopeData::prepare(int keyWindowSamples, int mainWindowSamples,
                                 int correlationLength) noexcept {
  keyOnsetCapacity_ =
      sanitiseCount(keyWindowSamples, minWindowSamples, maxWindowSamples);
  mainOnsetCapacity_ =
      sanitiseCount(mainWindowSamples, minWindowSamples, maxWindowSamples);
  correlationCapacity_ = sanitiseCount(
      correlationLength, minCorrelationLength, maxCorrelationLength);

  keyOnset_.assign(static_cast<std::size_t>(keyOnsetCapacity_), 0.0f);
  mainOnset_.assign(static_cast<std::size_t>(mainOnsetCapacity_), 0.0f);
  correlation_.assign(static_cast<std::size_t>(correlationCapacity_), 0.0f);

  reset();
}

void AlignmentScopeData::reset() noexcept {
  if (!keyOnset_.empty()) {
    std::fill(keyOnset_.begin(), keyOnset_.end(), 0.0f);
  }
  if (!mainOnset_.empty()) {
    std::fill(mainOnset_.begin(), mainOnset_.end(), 0.0f);
  }
  if (!correlation_.empty()) {
    std::fill(correlation_.begin(), correlation_.end(), 0.0f);
  }
  keyOnsetLength_.store(0, std::memory_order_relaxed);
  mainOnsetLength_.store(0, std::memory_order_relaxed);
  correlationLength_.store(0, std::memory_order_relaxed);

  offsetMs_.store(0.0f, std::memory_order_relaxed);
  confidence_.store(0.0f, std::memory_order_relaxed);
  offsetVariationMs_.store(0.0f, std::memory_order_relaxed);
  recommendedKeyDelayMs_.store(0.0f, std::memory_order_relaxed);
  mainLookaheadMs_.store(0.0f, std::memory_order_relaxed);
  needsMainLookahead_.store(false, std::memory_order_relaxed);
}

void AlignmentScopeData::setQualityMode(QualityMode mode) noexcept {
  qualityMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
  if (mode == QualityMode::Off) {
    reset();
  }
}

QualityMode AlignmentScopeData::getQualityMode() const noexcept {
  return static_cast<QualityMode>(
      qualityMode_.load(std::memory_order_relaxed));
}

void AlignmentScopeData::setKeyOnsetWaveform(const float *samples,
                                             int count) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  if (keyOnsetCapacity_ <= 0) {
    return;
  }
  const int n = sanitiseCount(count, 0, keyOnsetCapacity_);
  copyLinear(samples, n, keyOnset_.data(), keyOnsetCapacity_);
  keyOnsetLength_.store(n, std::memory_order_relaxed);
}

void AlignmentScopeData::setMainOnsetWaveform(const float *samples,
                                              int count) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  if (mainOnsetCapacity_ <= 0) {
    return;
  }
  const int n = sanitiseCount(count, 0, mainOnsetCapacity_);
  copyLinear(samples, n, mainOnset_.data(), mainOnsetCapacity_);
  mainOnsetLength_.store(n, std::memory_order_relaxed);
}

void AlignmentScopeData::setOnsetWaveforms(const float *key, int keyCount,
                                          const float *main,
                                          int mainCount) noexcept {
  setKeyOnsetWaveform(key, keyCount);
  setMainOnsetWaveform(main, mainCount);
}

void AlignmentScopeData::setCorrelationCurve(const float *samples,
                                             int count) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  if (correlationCapacity_ <= 0) {
    return;
  }
  const int n = sanitiseCount(count, 0, correlationCapacity_);
  copyLinear(samples, n, correlation_.data(), correlationCapacity_);
  correlationLength_.store(n, std::memory_order_relaxed);
}

void AlignmentScopeData::setResult(const Snapshot &snapshot) noexcept {
  setResult(snapshot.offsetMs, snapshot.confidence, snapshot.offsetVariationMs,
            snapshot.recommendedKeyDelayMs, snapshot.mainLookaheadMs,
            snapshot.needsMainLookahead);
}

void AlignmentScopeData::setResult(float offsetMs, float confidence,
                                   float offsetVariationMs,
                                   float recommendedKeyDelayMs,
                                   float mainLookaheadMs,
                                   bool needsMainLookahead) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return;
  }
  offsetMs_.store(offsetMs, std::memory_order_relaxed);
  confidence_.store(confidence, std::memory_order_relaxed);
  offsetVariationMs_.store(offsetVariationMs, std::memory_order_relaxed);
  recommendedKeyDelayMs_.store(recommendedKeyDelayMs,
                               std::memory_order_relaxed);
  mainLookaheadMs_.store(mainLookaheadMs, std::memory_order_relaxed);
  needsMainLookahead_.store(needsMainLookahead, std::memory_order_relaxed);
}

const float *AlignmentScopeData::getKeyOnsetBuffer() const noexcept {
  return keyOnsetCapacity_ > 0 ? keyOnset_.data() : nullptr;
}

const float *AlignmentScopeData::getMainOnsetBuffer() const noexcept {
  return mainOnsetCapacity_ > 0 ? mainOnset_.data() : nullptr;
}

const float *AlignmentScopeData::getCorrelationBuffer() const noexcept {
  return correlationCapacity_ > 0 ? correlation_.data() : nullptr;
}

std::span<const float> AlignmentScopeData::getKeyOnsetSpan() const noexcept {
  return std::span<const float>(keyOnset_.data(),
                                static_cast<std::size_t>(keyOnsetCapacity_));
}

std::span<const float> AlignmentScopeData::getMainOnsetSpan() const noexcept {
  return std::span<const float>(mainOnset_.data(),
                                static_cast<std::size_t>(mainOnsetCapacity_));
}

std::span<const float>
AlignmentScopeData::getCorrelationSpan() const noexcept {
  return std::span<const float>(
      correlation_.data(), static_cast<std::size_t>(correlationCapacity_));
}

int AlignmentScopeData::copyKeyOnset(float *dst, int dstCapacity) const
    noexcept {
  if (!dst || dstCapacity <= 0 || keyOnsetCapacity_ <= 0) {
    return 0;
  }
  const int len = keyOnsetLength_.load(std::memory_order_relaxed);
  if (len <= 0) {
    return 0;
  }
  const int n = sanitiseCount(len, 0, std::min(dstCapacity, keyOnsetCapacity_));
  std::memcpy(dst, keyOnset_.data(),
              static_cast<std::size_t>(n) * sizeof(float));
  return n;
}

int AlignmentScopeData::copyMainOnset(float *dst, int dstCapacity) const
    noexcept {
  if (!dst || dstCapacity <= 0 || mainOnsetCapacity_ <= 0) {
    return 0;
  }
  const int len = mainOnsetLength_.load(std::memory_order_relaxed);
  if (len <= 0) {
    return 0;
  }
  const int n = sanitiseCount(len, 0, std::min(dstCapacity, mainOnsetCapacity_));
  std::memcpy(dst, mainOnset_.data(),
              static_cast<std::size_t>(n) * sizeof(float));
  return n;
}

int AlignmentScopeData::copyCorrelationCurve(float *dst,
                                             int dstCapacity) const noexcept {
  if (!dst || dstCapacity <= 0 || correlationCapacity_ <= 0) {
    return 0;
  }
  const int len = correlationLength_.load(std::memory_order_relaxed);
  if (len <= 0) {
    return 0;
  }
  const int n =
      sanitiseCount(len, 0, std::min(dstCapacity, correlationCapacity_));
  std::memcpy(dst, correlation_.data(),
              static_cast<std::size_t>(n) * sizeof(float));
  return n;
}

} // namespace ChugGate::Analysis