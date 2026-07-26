#include "AnomalyGuard.h"
#include <cmath>
#include <algorithm>

namespace ChugGate::DSP {

AnomalyGuard::AnomalyGuard() noexcept {
  reset();
}

void AnomalyGuard::prepare(double sampleRate) noexcept {
  if (sampleRate > 0.0) {
    sampleRate_ = sampleRate;
  }
  // calculate envelope follower alpha for 5ms time constant:
  // tc = 0.005 seconds
  // alpha = exp(-1.0 / (tc * sampleRate_))
  envelopeAlpha_ = std::exp(-1.0f / (0.005f * static_cast<float>(sampleRate_)));
  reset();
}

void AnomalyGuard::reset() noexcept {
  shouldSuppressOpen_ = false;
  isSpikeActive_ = false;
  measuringSpike_ = false;
  spikeWindowCounter_ = 0;
  spikeWindowPeak_ = 0.0f;
  spikeWindowEnergy_ = 0.0f;
  spikeWindowActiveCount_ = 0;
  spikeHoldCounter_ = 0;

  isStuckKeyWarning_ = false;
  stuckKeyCounter_ = 0;

  isDropoutActive_ = false;
  shouldUseInternalFallback_ = false;
  dropoutCounter_ = 0;
  keyEnvelope_ = 0.0f;
}

void AnomalyGuard::setSpikePeakThresholdDb(float db) noexcept {
  spikePeakThresholdDb_.store(db, std::memory_order_relaxed);
}

void AnomalyGuard::setSpikeBodyRatioThreshold(float ratio) noexcept {
  spikeBodyRatioThreshold_.store(ratio, std::memory_order_relaxed);
}

void AnomalyGuard::setSpikeDurationMs(float ms) noexcept {
  spikeDurationMs_.store(ms, std::memory_order_relaxed);
}

void AnomalyGuard::setSpikeHoldMs(float ms) noexcept {
  spikeHoldMs_.store(ms, std::memory_order_relaxed);
}

void AnomalyGuard::setStuckKeyHoldTimeMs(float ms) noexcept {
  stuckKeyHoldMs_.store(ms, std::memory_order_relaxed);
}

void AnomalyGuard::setKeyActiveThresholdDb(float db) noexcept {
  keyActiveThresholdDb_.store(db, std::memory_order_relaxed);
}

void AnomalyGuard::setMainSilentThresholdDb(float db) noexcept {
  mainSilentThresholdDb_.store(db, std::memory_order_relaxed);
}

void AnomalyGuard::setDropoutHoldTimeMs(float ms) noexcept {
  dropoutHoldMs_.store(ms, std::memory_order_relaxed);
}

void AnomalyGuard::setDropoutThresholdDb(float db) noexcept {
  dropoutThresholdDb_.store(db, std::memory_order_relaxed);
}

void AnomalyGuard::analyze(const float* mainAudio, const float* keySignal, int numSamples, bool isKeyValid) noexcept {
  if (numSamples <= 0) {
    return;
  }

  // Load volatile settings to local variables for speed
  const float spikePeakThresh = std::pow(10.0f, spikePeakThresholdDb_.load(std::memory_order_relaxed) / 20.0f);
  const float bodyRatioThresh = spikeBodyRatioThreshold_.load(std::memory_order_relaxed);
  const std::uint32_t spikeWindowSamples = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::round(spikeDurationMs_.load(std::memory_order_relaxed) * 0.001 * sampleRate_)));
  const std::uint32_t spikeHoldSamples = static_cast<std::uint32_t>(std::round(spikeHoldMs_.load(std::memory_order_relaxed) * 0.001 * sampleRate_));

  const std::uint32_t stuckKeyHoldSamples = static_cast<std::uint32_t>(std::round(stuckKeyHoldMs_.load(std::memory_order_relaxed) * 0.001 * sampleRate_));
  const float keyActiveThresh = std::pow(10.0f, keyActiveThresholdDb_.load(std::memory_order_relaxed) / 20.0f);
  const float mainSilentThresh = std::pow(10.0f, mainSilentThresholdDb_.load(std::memory_order_relaxed) / 20.0f);

  const std::uint32_t dropoutHoldSamples = static_cast<std::uint32_t>(std::round(dropoutHoldMs_.load(std::memory_order_relaxed) * 0.001 * sampleRate_));
  const float dropoutThresh = std::pow(10.0f, dropoutThresholdDb_.load(std::memory_order_relaxed) / 20.0f);

  for (int i = 0; i < numSamples; ++i) {
    const float mainSample = mainAudio ? mainAudio[i] : 0.0f;
    const float keySample = keySignal ? keySignal[i] : 0.0f;
    const float keyAbs = std::abs(keySample);

    // 1. Dropout Handling
    keyEnvelope_ = envelopeAlpha_ * keyEnvelope_ + (1.0f - envelopeAlpha_) * keyAbs;
    const bool keyDisappeared = (keyEnvelope_ < dropoutThresh);
    const bool hasDropout = (!isKeyValid || (keySignal == nullptr) || keyDisappeared);

    if (hasDropout) {
      isDropoutActive_ = true;
      if (dropoutCounter_ < dropoutHoldSamples) {
        dropoutCounter_++;
        shouldUseInternalFallback_ = false;
      } else {
        shouldUseInternalFallback_ = true;
      }
    } else {
      isDropoutActive_ = false;
      shouldUseInternalFallback_ = false;
      dropoutCounter_ = 0;
    }

    // 2. Stuck-Key Detection
    const bool keyIsActive = (keyAbs > keyActiveThresh);
    const bool mainIsSilent = (std::abs(mainSample) < mainSilentThresh);

    if (keyIsActive && mainIsSilent) {
      if (stuckKeyCounter_ < stuckKeyHoldSamples) {
        stuckKeyCounter_++;
      }
      if (stuckKeyCounter_ >= stuckKeyHoldSamples) {
        isStuckKeyWarning_ = true;
      }
    } else {
      stuckKeyCounter_ = 0;
      isStuckKeyWarning_ = false;
    }

    // 3. Cable Spike Guard
    if (spikeHoldCounter_ > 0) {
      spikeHoldCounter_--;
      isSpikeActive_ = true;
    } else {
      isSpikeActive_ = false;
    }

    // Start a new spike measurement window if we see a peak above threshold,
    // aren't already measuring, and aren't in a hold period.
    if (keyAbs > spikePeakThresh && !measuringSpike_ && spikeHoldCounter_ == 0) {
      measuringSpike_ = true;
      spikeWindowCounter_ = 0;
      spikeWindowPeak_ = keyAbs;
      spikeWindowEnergy_ = keyAbs * keyAbs;
      spikeWindowActiveCount_ = 1;
    }

    if (measuringSpike_) {
      spikeWindowCounter_++;
      spikeWindowPeak_ = std::max(spikeWindowPeak_, keyAbs);
      spikeWindowEnergy_ += keyAbs * keyAbs;
      if (keyAbs > spikePeakThresh * 0.1f) {
        spikeWindowActiveCount_++;
      }

      if (spikeWindowCounter_ >= spikeWindowSamples) {
        const float rms = std::sqrt(spikeWindowEnergy_ / static_cast<float>(spikeWindowSamples));
        const float ratio = rms / (spikeWindowPeak_ + 1e-6f);
        
        // Sub-minimum duration: did it stay active for too long?
        const std::uint32_t maxActiveSamples = std::max<std::uint32_t>(2, spikeWindowSamples / 4);
        
        if (ratio < bodyRatioThresh && spikeWindowActiveCount_ <= maxActiveSamples) {
          // Classified as spike!
          spikeHoldCounter_ = spikeHoldSamples;
          isSpikeActive_ = true;
        }
        measuringSpike_ = false;
      }
    }

    shouldSuppressOpen_ = isSpikeActive_ || measuringSpike_;
  }
}

} // namespace ChugGate::DSP
