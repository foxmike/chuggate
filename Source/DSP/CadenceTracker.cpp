#include "CadenceTracker.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

CadenceTracker::CadenceTracker() noexcept {
  reset();
}

void CadenceTracker::prepare(double sampleRate) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  reset();
}

void CadenceTracker::reset() noexcept {
  sampleCounter_ = 0;
  lastOnsetTimestamp_ = 0;
  hasLastOnsetTimestamp_ = false;
  historyCount_ = 0;
  writeIndex_ = 0;
  consecutiveOutliers_ = 0;
  audioIntervalSeconds_ = 0.0f;
  history_.fill(0.0f);
  updateHostInterval();
}

void CadenceTracker::noteOnset() noexcept {
  noteOnset(sampleCounter_);
}

void CadenceTracker::noteOnset(std::uint64_t sampleTimestamp) noexcept {
  if (sampleRate_ <= 0.0) {
    if (sampleTimestamp > sampleCounter_) {
      sampleCounter_ = sampleTimestamp;
    }
    return;
  }

  if (hasLastOnsetTimestamp_) {
    if (sampleTimestamp > lastOnsetTimestamp_) {
      const std::uint64_t deltaSamples = sampleTimestamp - lastOnsetTimestamp_;
      const float deltaSeconds = static_cast<float>(deltaSamples) / static_cast<float>(sampleRate_);

      if (deltaSeconds >= minIntervalLimit_ && deltaSeconds <= maxIntervalLimit_) {
        addInterval(deltaSeconds);
        lastOnsetTimestamp_ = sampleTimestamp;
      } else if (deltaSeconds > maxIntervalLimit_) {
        // Gap is too long to be a consecutive pick. Do not add to history,
        // but update timestamp so the next note is measured from here.
        lastOnsetTimestamp_ = sampleTimestamp;
      }
      // If deltaSeconds < minIntervalLimit_, it is likely a double-trigger/glitch.
      // We ignore it and do NOT update lastOnsetTimestamp_, so the next note
      // is still measured from the previous valid onset.
    }
  } else {
    lastOnsetTimestamp_ = sampleTimestamp;
    hasLastOnsetTimestamp_ = true;
  }

  if (sampleTimestamp > sampleCounter_) {
    sampleCounter_ = sampleTimestamp;
  }
}

void CadenceTracker::tick(int numSamples) noexcept {
  if (numSamples > 0) {
    sampleCounter_ += static_cast<std::uint64_t>(numSamples);
  }
}

void CadenceTracker::process(const bool* onsetFlags, int numSamples) noexcept {
  if (numSamples <= 0) {
    return;
  }

  if (onsetFlags != nullptr) {
    for (int i = 0; i < numSamples; ++i) {
      if (onsetFlags[i]) {
        noteOnset(sampleCounter_ + static_cast<std::uint64_t>(i));
      }
    }
  }
  sampleCounter_ += static_cast<std::uint64_t>(numSamples);
}

void CadenceTracker::process(bool hasOnsetInBlock, int numSamples) noexcept {
  if (numSamples <= 0) {
    return;
  }

  if (hasOnsetInBlock) {
    noteOnset(sampleCounter_);
  }
  sampleCounter_ += static_cast<std::uint64_t>(numSamples);
}

void CadenceTracker::setMode(Mode mode) noexcept {
  mode_.store(mode, std::memory_order_relaxed);
}

void CadenceTracker::setMode(int modeIndex) noexcept {
  switch (modeIndex) {
    case 0: setMode(Mode::Audio); break;
    case 1: setMode(Mode::HostTempo); break;
    case 2: setMode(Mode::Combined); break;
    case 3: setMode(Mode::Off); break;
    default: break;
  }
}

void CadenceTracker::setConstraintFactor(float factor) noexcept {
  constraintFactor_.store(std::clamp(factor, 0.15f, 0.85f), std::memory_order_relaxed);
}

void CadenceTracker::setHostTempoBpm(float bpm) noexcept {
  hostBpm_.store(bpm, std::memory_order_relaxed);
  updateHostInterval();
}

void CadenceTracker::setHostSubdivision(float subdivision) noexcept {
  hostSubdivision_.store(subdivision, std::memory_order_relaxed);
  updateHostInterval();
}

void CadenceTracker::updateHostInterval() noexcept {
  const float bpm = hostBpm_.load(std::memory_order_relaxed);
  const float sub = hostSubdivision_.load(std::memory_order_relaxed);
  if (bpm > 0.0f) {
    hostIntervalSeconds_ = (60.0f / bpm) * sub;
  } else {
    hostIntervalSeconds_ = 0.25f; // Fallback to 120 bpm eighth notes
  }
}

float CadenceTracker::getIntervalSeconds() const noexcept {
  const Mode m = mode_.load(std::memory_order_relaxed);
  switch (m) {
    case Mode::Audio:
      return audioIntervalSeconds_;

    case Mode::HostTempo:
      return hostIntervalSeconds_;

    case Mode::Combined:
      if (isEstimateValid()) {
        return audioIntervalSeconds_;
      }
      return hostIntervalSeconds_;

    case Mode::Off:
    default:
      return 0.0f;
  }
}

float CadenceTracker::getReleaseConstraintSeconds() const noexcept {
  const Mode m = mode_.load(std::memory_order_relaxed);
  if (m == Mode::Off) {
    return 0.0f;
  }
  return constraintFactor_.load(std::memory_order_relaxed) * getIntervalSeconds();
}

float CadenceTracker::getEffectiveRelease(float tauManual) const noexcept {
  const Mode m = mode_.load(std::memory_order_relaxed);
  if (m == Mode::Off) {
    return tauManual;
  }

  if (!isEstimateValid()) {
    return tauManual;
  }

  const float constraintSec = getReleaseConstraintSeconds();
  
  // All timings in ChugGate (including manual release) are in milliseconds.
  // We scale the constraint in seconds to milliseconds.
  const float constraintMs = constraintSec * 1000.0f;
  
  return std::min(tauManual, constraintMs);
}

bool CadenceTracker::isEstimateValid() const noexcept {
  const Mode m = mode_.load(std::memory_order_relaxed);
  if (m == Mode::Off) {
    return false;
  }
  if (m == Mode::HostTempo || m == Mode::Combined) {
    return true;
  }
  // Mode::Audio requires minimum history
  return historyCount_ >= minRequiredIntervals_;
}

void CadenceTracker::addInterval(float deltaSeconds) noexcept {
  if (deltaSeconds < minIntervalLimit_ || deltaSeconds > maxIntervalLimit_) {
    return;
  }

  bool accept = true;
  if (historyCount_ >= minRequiredIntervals_) {
    const float currentMedian = calculateMedianOfHistory();
    if (currentMedian > 0.0f) {
      const float ratio = deltaSeconds / currentMedian;
      if (ratio < 0.5f || ratio > 2.0f) {
        if (consecutiveOutliers_ < 2) {
          accept = false;
          consecutiveOutliers_++;
        } else {
          // Reset outlier counter after accepting to adapt to new tempo/subdivision
          consecutiveOutliers_ = 0;
        }
      } else {
        consecutiveOutliers_ = 0;
      }
    }
  } else {
    consecutiveOutliers_ = 0;
  }

  if (accept) {
    history_[static_cast<std::size_t>(writeIndex_)] = deltaSeconds;
    writeIndex_ = (writeIndex_ + 1) % kMaxHistory;
    if (historyCount_ < kMaxHistory) {
      historyCount_++;
    }
    audioIntervalSeconds_ = calculateMedianOfHistory();
  }
}

float CadenceTracker::calculateMedianOfHistory() const noexcept {
  if (historyCount_ <= 0) {
    return 0.0f;
  }

  const int n = std::min(historyCount_, kDefaultHistoryLength);
  std::array<float, kMaxHistory> temp{};
  
  int readIdx = writeIndex_;
  for (int i = 0; i < n; ++i) {
    readIdx = (readIdx - 1 + kMaxHistory) % kMaxHistory;
    temp[static_cast<std::size_t>(i)] = history_[static_cast<std::size_t>(readIdx)];
  }

  // Insertion sort (allocation-free, very fast for small arrays)
  for (int i = 1; i < n; ++i) {
    const float key = temp[static_cast<std::size_t>(i)];
    int j = i - 1;
    while (j >= 0 && temp[static_cast<std::size_t>(j)] > key) {
      temp[static_cast<std::size_t>(j + 1)] = temp[static_cast<std::size_t>(j)];
      j--;
    }
    temp[static_cast<std::size_t>(j + 1)] = key;
  }

  if (n % 2 == 1) {
    return temp[static_cast<std::size_t>(n / 2)];
  }
  
  return 0.5f * (temp[static_cast<std::size_t>(n / 2 - 1)] + temp[static_cast<std::size_t>(n / 2)]);
}

} // namespace ChugGate::DSP
