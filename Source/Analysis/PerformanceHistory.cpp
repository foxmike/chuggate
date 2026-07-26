#include "PerformanceHistory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ChugGate::Analysis {

namespace {

constexpr int kMinCapacitySlots = 8; // guard against pathological rates
constexpr int kMaxCapacitySlots = 16384; // 30 s @ ~545 Hz upper bound

inline int sanitiseInt(int v, int lo, int hi) noexcept {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

inline double sanitiseDouble(double v, double lo, double hi) noexcept {
  if (!(v >= lo))
    return lo; // also catches NaN
  if (v > hi)
    return hi;
  return v;
}

} // namespace

void PerformanceHistory::prepare(double controlRateHz, double maxDurationSeconds,
                                 int numChannels) noexcept {
  Config cfg;
  cfg.controlRateHz = controlRateHz;
  cfg.maxDurationSeconds = maxDurationSeconds;
  cfg.numChannels = numChannels;
  prepare(cfg);
}

void PerformanceHistory::prepare(const Config &cfg) noexcept {
  controlRateHz_ = sanitiseDouble(cfg.controlRateHz, 1.0, 4096.0);
  minDurationSeconds_ = sanitiseDouble(cfg.minDurationSeconds, 0.1, 30.0);
  maxDurationSeconds_ =
      sanitiseDouble(cfg.maxDurationSeconds, minDurationSeconds_, 30.0);

  const double initial = sanitiseDouble(
      cfg.initialDurationSeconds, minDurationSeconds_, maxDurationSeconds_);
  durationSeconds_.store(initial, std::memory_order_relaxed);

  numChannels_ = sanitiseInt(cfg.numChannels, 0, 64);

  const int maxCapacity = capacityForSeconds(maxDurationSeconds_);
  reallocateStorage(maxCapacity);

  // Initialise the live window to the requested duration — trim the
  // "valid" sample count to the smaller of (current valid) and
  // (initial-duration capacity). On a fresh prepare, numValid_ is zero so
  // the trim is a no-op.
  const int initialCap = std::min(maxCapacity, capacityForSeconds(initial));
  if (numValid_.load(std::memory_order_relaxed) > initialCap) {
    // Snap the window back to the most recent initialCap samples.
    const int excess =
        numValid_.load(std::memory_order_relaxed) - initialCap;
    int w = writeIndex_.load(std::memory_order_relaxed) - excess;
    w = ((w % capacity_) + capacity_) % capacity_;
    writeIndex_.store(w, std::memory_order_relaxed);
    numValid_.store(initialCap, std::memory_order_relaxed);
  }

  // Reset the Live decimation counter and counters so a fresh prepare
  // starts from a known state. We do not clear storage contents — they
  // are immediately overwritten by the next push.
  liveDecimCounter_ = 0;
  totalPushes_.store(0, std::memory_order_relaxed);
  storedPushes_.store(0, std::memory_order_relaxed);
  droppedByMode_.store(0, std::memory_order_relaxed);
  overwriteCount_.store(0, std::memory_order_relaxed);
}

void PerformanceHistory::reset() noexcept {
  writeIndex_.store(0, std::memory_order_relaxed);
  numValid_.store(0, std::memory_order_relaxed);
  liveDecimCounter_ = 0;
  totalPushes_.store(0, std::memory_order_relaxed);
  storedPushes_.store(0, std::memory_order_relaxed);
  droppedByMode_.store(0, std::memory_order_relaxed);
  overwriteCount_.store(0, std::memory_order_relaxed);
}

void PerformanceHistory::setQualityMode(QualityMode mode) noexcept {
  qualityMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
  // Reset the decimation counter so that switching into Live mode does
  // not consume a phantom slot from the previous mode.
  liveDecimCounter_ = 0;
}

QualityMode PerformanceHistory::getQualityMode() const noexcept {
  return static_cast<QualityMode>(
      qualityMode_.load(std::memory_order_relaxed));
}

void PerformanceHistory::setDurationSeconds(double seconds) noexcept {
  const double clamped =
      sanitiseDouble(seconds, minDurationSeconds_, maxDurationSeconds_);
  const int requestedCap = capacityForSeconds(clamped);

  // Grow storage only if the request exceeds the currently allocated
  // capacity. Grow-to-max was preallocated in prepare(), so this branch
  // is only hit when maxDurationSeconds_ was later increased or a
  // previous setDurationSeconds() shrank the storage.
  if (requestedCap > capacity_) {
    reallocateStorage(requestedCap);
  } else if (requestedCap < capacity_) {
    // Shrinking: trim the valid window to fit, but keep the storage
    // allocated at the max so future grows are free. The "trim" path
    // preserves the most recent requestedCap samples.
    const int currentValid = numValid_.load(std::memory_order_relaxed);
    if (currentValid > requestedCap) {
      const int excess = currentValid - requestedCap;
      int w = writeIndex_.load(std::memory_order_relaxed) - excess;
      w = ((w % capacity_) + capacity_) % capacity_;
      writeIndex_.store(w, std::memory_order_relaxed);
      numValid_.store(requestedCap, std::memory_order_relaxed);
    }
  }

  durationSeconds_.store(clamped, std::memory_order_relaxed);
}

int PerformanceHistory::pushFromFrame(const AnalysisFrame &frame) noexcept {
  totalPushes_.fetch_add(1, std::memory_order_relaxed);

  if (!shouldStoreNextPush()) {
    droppedByMode_.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }

  // Persist the optional-field flags the first time we see a frame in
  // Diagnose mode (the GUI uses these to decide whether to render the
  // alignment/lookahead lanes). Subsequent frames simply read them.
  const QualityMode mode = getQualityMode();
  if (mode == QualityMode::Diagnose) {
    hasAlignment_ = frame.hasAlignmentOffset;
    hasLookahead_ = frame.hasLookaheadRegion;
  }

  const int w = writeIndex_.load(std::memory_order_relaxed);
  if (capacity_ <= 0) {
    // Not prepared — treat as a no-op (the GUI will display nothing).
    return 0;
  }

  keyEnv_[w] = frame.keyEnvelope;
  mainEnv_[w] = frame.mainEnvelope;
  threshOpen_[w] = frame.thresholdOpen;
  threshClose_[w] = frame.thresholdClose;
  noiseFloor_[w] = frame.noiseFloor;
  gateState_[w] = static_cast<std::uint8_t>(frame.gateState);
  outputGain_[w] = frame.outputGain;
  transient_[w] = frame.transientMarker;
  sustainConf_[w] = frame.sustainConfidence;

  if (hasAlignment_ && frame.hasAlignmentOffset) {
    alignment_[w] = frame.alignmentOffset;
  }
  if (hasLookahead_ && frame.hasLookaheadRegion) {
    lookahead_[w] = frame.lookaheadRegion ? 1 : 0;
  }

  const int prevNum = numValid_.load(std::memory_order_relaxed);
  const int prevWrite = w;
  const int nextWrite = (w + 1) % capacity_;
  writeIndex_.store(nextWrite, std::memory_order_relaxed);
  if (prevNum < capacity_) {
    numValid_.store(prevNum + 1, std::memory_order_relaxed);
  } else {
    // Wrapped — the oldest sample is about to be overwritten.
    overwriteCount_.fetch_add(1, std::memory_order_relaxed);
  }
  (void)prevWrite;

  storedPushes_.fetch_add(1, std::memory_order_relaxed);
  return 1;
}

int PerformanceHistory::push(float keyEnvelope, float mainEnvelope,
                             float thresholdOpen, float thresholdClose,
                             float noiseFloor, std::uint8_t gateState,
                             float outputGain, float transientMarker,
                             float sustainConfidence, float alignmentOffset,
                             bool lookaheadRegion) noexcept {
  totalPushes_.fetch_add(1, std::memory_order_relaxed);

  if (!shouldStoreNextPush()) {
    droppedByMode_.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }

  if (capacity_ <= 0) {
    return 0;
  }

  const int w = writeIndex_.load(std::memory_order_relaxed);

  keyEnv_[w] = keyEnvelope;
  mainEnv_[w] = mainEnvelope;
  threshOpen_[w] = thresholdOpen;
  threshClose_[w] = thresholdClose;
  noiseFloor_[w] = noiseFloor;
  gateState_[w] = gateState;
  outputGain_[w] = outputGain;
  transient_[w] = transientMarker;
  sustainConf_[w] = sustainConfidence;

  if (hasAlignment_) {
    alignment_[w] = alignmentOffset;
  }
  if (hasLookahead_) {
    lookahead_[w] = lookaheadRegion ? 1 : 0;
  }

  const int prevNum = numValid_.load(std::memory_order_relaxed);
  const int nextWrite = (w + 1) % capacity_;
  writeIndex_.store(nextWrite, std::memory_order_relaxed);
  if (prevNum < capacity_) {
    numValid_.store(prevNum + 1, std::memory_order_relaxed);
  } else {
    overwriteCount_.fetch_add(1, std::memory_order_relaxed);
  }

  storedPushes_.fetch_add(1, std::memory_order_relaxed);
  return 1;
}

bool PerformanceHistory::shouldStoreNextPush() noexcept {
  const QualityMode mode = getQualityMode();
  switch (mode) {
  case QualityMode::Off:
    return false;
  case QualityMode::Live:
    // 2:1 decimation — store the first of every pair of pushes.
    liveDecimCounter_ = (liveDecimCounter_ + 1) & 1;
    return liveDecimCounter_ == 0;
  case QualityMode::Studio:
  case QualityMode::Diagnose:
  default:
    return true;
  }
}

PerformanceHistory::TimelineView PerformanceHistory::getView() const noexcept {
  TimelineView v;
  v.capacity = capacity_;
  v.numSamples = numValid_.load(std::memory_order_relaxed);
  if (capacity_ <= 0 || v.numSamples <= 0) {
    v.numSamples = 0;
    v.startIndex = 0;
    return v;
  }

  // The oldest valid sample is the one immediately after writeIndex
  // when the buffer is full; otherwise it's index 0.
  const int w = writeIndex_.load(std::memory_order_relaxed);
  if (v.numSamples < capacity_) {
    v.startIndex = 0;
  } else {
    v.startIndex = w; // next write position is the oldest valid sample
  }

  v.keyEnvelope = std::span<const float>(keyEnv_.data(), capacity_);
  v.mainEnvelope = std::span<const float>(mainEnv_.data(), capacity_);
  v.thresholdOpen = std::span<const float>(threshOpen_.data(), capacity_);
  v.thresholdClose = std::span<const float>(threshClose_.data(), capacity_);
  v.noiseFloor = std::span<const float>(noiseFloor_.data(), capacity_);
  v.outputGain = std::span<const float>(outputGain_.data(), capacity_);
  v.transientMarker = std::span<const float>(transient_.data(), capacity_);
  v.sustainConfidence = std::span<const float>(sustainConf_.data(), capacity_);
  v.gateState = std::span<const std::uint8_t>(gateState_.data(), capacity_);
  if (hasAlignment_) {
    v.alignmentOffset =
        std::span<const float>(alignment_.data(), capacity_);
  }
  if (hasLookahead_) {
    v.lookaheadRegion =
        std::span<const std::uint8_t>(lookahead_.data(), capacity_);
  }
  return v;
}

int PerformanceHistory::copyRecent(CopyBuffers &buffers,
                                   int dstCapacity) const noexcept {
  if (dstCapacity <= 0 || capacity_ <= 0) {
    return 0;
  }
  const int valid = numValid_.load(std::memory_order_relaxed);
  if (valid <= 0) {
    return 0;
  }
  const int toCopy = std::min(dstCapacity, valid);

  // The most recent toCopy samples are at indices (writeIndex - toCopy
  // ... writeIndex - 1) in the ring. Compute the starting physical index.
  const int w = writeIndex_.load(std::memory_order_relaxed);
  int start = w - toCopy;
  // Normalise start into [0, capacity_) using a single wrap.
  if (start < 0) {
    start = ((start % capacity_) + capacity_) % capacity_;
  }

  // If the window does not wrap, copy contiguously. Otherwise copy the
  // tail to the end of the destination and the head from offset 0.
  auto copySegmented = [](const float *src, float *dst, int cap, int startIdx,
                          int count) {
    if (!dst)
      return;
    const int first = std::min(count, cap - startIdx);
    if (first > 0) {
      std::memcpy(dst, src + startIdx,
                  static_cast<std::size_t>(first) * sizeof(float));
    }
    const int rest = count - first;
    if (rest > 0) {
      std::memcpy(dst + first, src,
                  static_cast<std::size_t>(rest) * sizeof(float));
    }
  };
  auto copySegmentedU8 = [](const std::uint8_t *src, std::uint8_t *dst,
                            int cap, int startIdx, int count) {
    if (!dst)
      return;
    const int first = std::min(count, cap - startIdx);
    if (first > 0) {
      std::memcpy(dst, src + startIdx,
                  static_cast<std::size_t>(first) * sizeof(std::uint8_t));
    }
    const int rest = count - first;
    if (rest > 0) {
      std::memcpy(dst + first, src,
                  static_cast<std::size_t>(rest) * sizeof(std::uint8_t));
    }
  };

  copySegmented(keyEnv_.data(), buffers.keyEnvelope, capacity_, start, toCopy);
  copySegmented(mainEnv_.data(), buffers.mainEnvelope, capacity_, start,
                toCopy);
  copySegmented(threshOpen_.data(), buffers.thresholdOpen, capacity_, start,
                toCopy);
  copySegmented(threshClose_.data(), buffers.thresholdClose, capacity_, start,
                toCopy);
  copySegmented(noiseFloor_.data(), buffers.noiseFloor, capacity_, start,
                toCopy);
  copySegmented(outputGain_.data(), buffers.outputGain, capacity_, start,
                toCopy);
  copySegmented(transient_.data(), buffers.transientMarker, capacity_, start,
                toCopy);
  copySegmented(sustainConf_.data(), buffers.sustainConfidence, capacity_,
                start, toCopy);
  if (hasAlignment_ && buffers.alignmentOffset) {
    copySegmented(alignment_.data(), buffers.alignmentOffset, capacity_, start,
                  toCopy);
  }
  copySegmentedU8(gateState_.data(), buffers.gateState, capacity_, start,
                  toCopy);
  if (hasLookahead_ && buffers.lookaheadRegion) {
    copySegmentedU8(lookahead_.data(), buffers.lookaheadRegion, capacity_,
                    start, toCopy);
  }
  return toCopy;
}

int PerformanceHistory::capacityForSeconds(double seconds) const noexcept {
  if (!(seconds > 0.0) || !(controlRateHz_ > 0.0)) {
    return kMinCapacitySlots;
  }
  // Round up so a 30 s window at 100 Hz yields 3000 slots (not 2999).
  double raw = std::ceil(seconds * controlRateHz_);
  if (!(raw >= static_cast<double>(kMinCapacitySlots))) {
    return kMinCapacitySlots;
  }
  if (raw > static_cast<double>(kMaxCapacitySlots)) {
    return kMaxCapacitySlots;
  }
  return static_cast<int>(raw);
}

void PerformanceHistory::reallocateStorage(int newCapacity) noexcept {
  if (newCapacity < kMinCapacitySlots) {
    newCapacity = kMinCapacitySlots;
  }
  if (newCapacity > kMaxCapacitySlots) {
    newCapacity = kMaxCapacitySlots;
  }
  if (newCapacity == capacity_) {
    return;
  }

  // When growing, copy the in-order valid window into the new front so
  // the most recent samples are preserved. When shrinking, the valid
  // window is trimmed by the caller.
  std::vector<float> newKey(newCapacity, 0.0f);
  std::vector<float> newMain(newCapacity, 0.0f);
  std::vector<float> newOpen(newCapacity, 0.0f);
  std::vector<float> newClose(newCapacity, 0.0f);
  std::vector<float> newNoise(newCapacity, 0.0f);
  std::vector<float> newGain(newCapacity, 0.0f);
  std::vector<float> newTransient(newCapacity, 0.0f);
  std::vector<float> newSustain(newCapacity, 0.0f);
  std::vector<float> newAlign(newCapacity, 0.0f);
  std::vector<std::uint8_t> newGate(newCapacity, 0);
  std::vector<std::uint8_t> newLook(newCapacity, 0);

  if (capacity_ > 0) {
    const int valid = numValid_.load(std::memory_order_relaxed);
    if (valid > 0) {
      const int w = writeIndex_.load(std::memory_order_relaxed);
      // Physical index of the oldest valid sample.
      int start;
      if (valid < capacity_) {
        start = 0;
      } else {
        start = w;
      }
      const int keep = std::min(valid, newCapacity);
      // Source layout in chronological (oldest→newest) order.
      const int first = std::min(keep, capacity_ - start);
      const int rest = keep - first;
      auto copyChronological = [](const float *src, int srcCap, int from,
                                  int count, float *dst) {
        if (count <= 0)
          return;
        const int tail = std::min(count, srcCap - from);
        if (tail > 0) {
          std::memcpy(dst, src + from,
                      static_cast<std::size_t>(tail) * sizeof(float));
        }
        if (count - tail > 0) {
          std::memcpy(dst + tail, src,
                      static_cast<std::size_t>(count - tail) * sizeof(float));
        }
      };
      auto copyChronologicalU8 = [](const std::uint8_t *src, int srcCap,
                                    int from, int count, std::uint8_t *dst) {
        if (count <= 0)
          return;
        const int tail = std::min(count, srcCap - from);
        if (tail > 0) {
          std::memcpy(dst, src + from,
                      static_cast<std::size_t>(tail) * sizeof(std::uint8_t));
        }
        if (count - tail > 0) {
          std::memcpy(dst + tail, src,
                      static_cast<std::size_t>(count - tail) *
                          sizeof(std::uint8_t));
        }
      };
      copyChronological(keyEnv_.data(), capacity_, start, first, newKey.data());
      if (rest > 0) {
        copyChronological(keyEnv_.data(), capacity_, 0, rest,
                          newKey.data() + first);
      }
      copyChronological(mainEnv_.data(), capacity_, start, first,
                        newMain.data());
      if (rest > 0) {
        copyChronological(mainEnv_.data(), capacity_, 0, rest,
                          newMain.data() + first);
      }
      copyChronological(threshOpen_.data(), capacity_, start, first,
                        newOpen.data());
      if (rest > 0) {
        copyChronological(threshOpen_.data(), capacity_, 0, rest,
                          newOpen.data() + first);
      }
      copyChronological(threshClose_.data(), capacity_, start, first,
                        newClose.data());
      if (rest > 0) {
        copyChronological(threshClose_.data(), capacity_, 0, rest,
                          newClose.data() + first);
      }
      copyChronological(noiseFloor_.data(), capacity_, start, first,
                        newNoise.data());
      if (rest > 0) {
        copyChronological(noiseFloor_.data(), capacity_, 0, rest,
                          newNoise.data() + first);
      }
      copyChronological(outputGain_.data(), capacity_, start, first,
                        newGain.data());
      if (rest > 0) {
        copyChronological(outputGain_.data(), capacity_, 0, rest,
                          newGain.data() + first);
      }
      copyChronological(transient_.data(), capacity_, start, first,
                        newTransient.data());
      if (rest > 0) {
        copyChronological(transient_.data(), capacity_, 0, rest,
                          newTransient.data() + first);
      }
      copyChronological(sustainConf_.data(), capacity_, start, first,
                        newSustain.data());
      if (rest > 0) {
        copyChronological(sustainConf_.data(), capacity_, 0, rest,
                          newSustain.data() + first);
      }
      copyChronological(alignment_.data(), capacity_, start, first,
                        newAlign.data());
      if (rest > 0) {
        copyChronological(alignment_.data(), capacity_, 0, rest,
                          newAlign.data() + first);
      }
      copyChronologicalU8(gateState_.data(), capacity_, start, first,
                          newGate.data());
      if (rest > 0) {
        copyChronologicalU8(gateState_.data(), capacity_, 0, rest,
                            newGate.data() + first);
      }
      copyChronologicalU8(lookahead_.data(), capacity_, start, first,
                          newLook.data());
      if (rest > 0) {
        copyChronologicalU8(lookahead_.data(), capacity_, 0, rest,
                            newLook.data() + first);
      }
      // Adjust indices: the kept window now lives at the front of the
      // new storage, so writeIndex = keep % newCapacity and numValid = keep.
      writeIndex_.store(keep % newCapacity, std::memory_order_relaxed);
      numValid_.store(keep, std::memory_order_relaxed);
    }
  }

  keyEnv_.swap(newKey);
  mainEnv_.swap(newMain);
  threshOpen_.swap(newOpen);
  threshClose_.swap(newClose);
  noiseFloor_.swap(newNoise);
  outputGain_.swap(newGain);
  transient_.swap(newTransient);
  sustainConf_.swap(newSustain);
  alignment_.swap(newAlign);
  gateState_.swap(newGate);
  lookahead_.swap(newLook);
  capacity_ = newCapacity;
}

} // namespace ChugGate::Analysis
