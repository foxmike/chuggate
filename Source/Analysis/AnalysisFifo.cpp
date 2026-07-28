#include "AnalysisFifo.h"

#include <algorithm>
#include <limits>

namespace ChugGate::Analysis {

void AnalysisFifo::prepare(int maxFrames, int numChannels) noexcept {
  const int requestedCapacity =
      std::clamp(maxFrames, 0, std::numeric_limits<int>::max() - 1);
  const int storageSize = requestedCapacity + 1;

  storage_.allocate(storageSize, false);

  // HeapBlock is configured not to throw. If allocation fails, leave the
  // transport in a valid empty state whose pushes are reported as drops.
  capacity_ = storage_.getData() != nullptr ? requestedCapacity : 0;
  numChannels_ = std::max(0, numChannels);
  fifo_.setTotalSize(capacity_ + 1);
  reset();
}

void AnalysisFifo::reset() noexcept {
  fifo_.reset();
  dropCount_.store(0, std::memory_order_relaxed);
}

bool AnalysisFifo::tryPush(const AnalysisFrame &frame) noexcept {
  if (getQualityMode() == QualityMode::Off) {
    return false;
  }

  int startIndex1 = 0;
  int blockSize1 = 0;
  int startIndex2 = 0;
  int blockSize2 = 0;
  fifo_.prepareToWrite(1, startIndex1, blockSize1, startIndex2, blockSize2);

  if (blockSize1 == 0) {
    dropCount_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  storage_[startIndex1] = frame;
  fifo_.finishedWrite(1);
  return true;
}

bool AnalysisFifo::tryPop(AnalysisFrame &frame) noexcept {
  int startIndex1 = 0;
  int blockSize1 = 0;
  int startIndex2 = 0;
  int blockSize2 = 0;
  fifo_.prepareToRead(1, startIndex1, blockSize1, startIndex2, blockSize2);

  if (blockSize1 == 0) {
    return false;
  }

  frame = storage_[startIndex1];
  fifo_.finishedRead(1);
  return true;
}

void AnalysisFifo::setQualityMode(QualityMode mode) noexcept {
  qualityMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
}

QualityMode AnalysisFifo::getQualityMode() const noexcept {
  return static_cast<QualityMode>(
      qualityMode_.load(std::memory_order_relaxed));
}

std::uint64_t AnalysisFifo::getDropCount() const noexcept {
  return dropCount_.load(std::memory_order_relaxed);
}

} // namespace ChugGate::Analysis
