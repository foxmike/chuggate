#include "GainSmoother.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

void GainSmoother::prepare(double sampleRate, int numBands) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  numBands_ = std::clamp(numBands, 1, maxBands);
  for (int band = 0; band < maxBands; ++band)
    updateAlpha(band);
  reset();
}

void GainSmoother::reset() noexcept { reset(1.0f); }

void GainSmoother::reset(float gain) noexcept {
  gain_.fill(sanitiseGain(gain));
}

float GainSmoother::getGain(int band) const noexcept {
  if (band < 0 || band >= numBands_)
    return 1.0f;
  return gain_[static_cast<std::size_t>(band)];
}

void GainSmoother::setOpenTimeMs(float timeMs) noexcept {
  for (int band = 0; band < maxBands; ++band)
    setOpenTimeMs(band, timeMs);
}

void GainSmoother::setCloseTimeMs(float timeMs) noexcept {
  for (int band = 0; band < maxBands; ++band)
    setCloseTimeMs(band, timeMs);
}

void GainSmoother::setOpenTimeMs(int band, float timeMs) noexcept {
  if (band < 0 || band >= maxBands)
    return;
  const auto index = static_cast<std::size_t>(band);
  const float time = sanitiseTime(timeMs);
  if (std::abs(openTimeMs_[index] - time) <= 1.0e-7f)
    return;
  openTimeMs_[index] = time;
  updateAlpha(band);
}

void GainSmoother::setCloseTimeMs(int band, float timeMs) noexcept {
  if (band < 0 || band >= maxBands)
    return;
  const auto index = static_cast<std::size_t>(band);
  const float time = sanitiseTime(timeMs);
  if (std::abs(closeTimeMs_[index] - time) <= 1.0e-7f)
    return;
  closeTimeMs_[index] = time;
  updateAlpha(band);
}

void GainSmoother::setBandTimesMs(int band, float openTimeMs,
                                  float closeTimeMs) noexcept {
  if (band < 0 || band >= maxBands)
    return;

  const auto index = static_cast<std::size_t>(band);
  openTimeMs_[index] = sanitiseTime(openTimeMs);
  closeTimeMs_[index] = sanitiseTime(closeTimeMs);
  updateAlpha(band);
}

float GainSmoother::processSample(float targetGain, int band) noexcept {
  if (band < 0 || band >= numBands_)
    return sanitiseGain(targetGain);

  const auto index = static_cast<std::size_t>(band);
  const float target = sanitiseGain(targetGain);
  const float alpha =
      target > gain_[index] ? openAlpha_[index] : closeAlpha_[index];

  // A reversal merely selects the other coefficient and continues from the
  // current value. In particular, reopen never resets the gain to the floor.
  gain_[index] = alpha * gain_[index] + (1.0f - alpha) * target;
  return gain_[index];
}

void GainSmoother::processFrame(const float *targetGains,
                                float *smoothedGains) noexcept {
  if (targetGains == nullptr || smoothedGains == nullptr)
    return;

  for (int band = 0; band < numBands_; ++band)
    smoothedGains[band] = processSample(targetGains[band], band);
}

void GainSmoother::process(const float *targetGains, float *smoothedGains,
                           int numSamples) noexcept {
  if (targetGains == nullptr || smoothedGains == nullptr || numSamples <= 0)
    return;

  for (int sample = 0; sample < numSamples; ++sample) {
    const int frameOffset = sample * numBands_;
    processFrame(targetGains + frameOffset, smoothedGains + frameOffset);
  }
}

float GainSmoother::calculateAlpha(float timeMs) const noexcept {
  if (sampleRate_ <= 0.0 || timeMs <= 0.0f)
    return 0.0f;

  const double tauSeconds = static_cast<double>(timeMs) * 0.001;
  return static_cast<float>(std::exp(-1.0 / (sampleRate_ * tauSeconds)));
}

void GainSmoother::updateAlpha(int band) noexcept {
  const auto index = static_cast<std::size_t>(band);
  openAlpha_[index] = calculateAlpha(openTimeMs_[index]);
  closeAlpha_[index] = calculateAlpha(closeTimeMs_[index]);
}

float GainSmoother::sanitiseTime(float timeMs) noexcept {
  if (!std::isfinite(timeMs))
    return 0.0f;
  return std::clamp(timeMs, 0.0f, 10000.0f);
}

float GainSmoother::sanitiseGain(float gain) noexcept {
  if (!std::isfinite(gain))
    return 0.0f;
  return std::clamp(gain, 0.0f, 1.0f);
}

} // namespace ChugGate::DSP
