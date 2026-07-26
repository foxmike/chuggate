#include "FractionalDelay.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

void FractionalDelay::prepare(double sampleRate, float maxDelayMs) {
  sampleRate_ =
      std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 0.0;
  maximumDelayMs_ =
      std::isfinite(maxDelayMs)
          ? std::clamp(std::abs(maxDelayMs), 0.0f, maximumExtendedDelayMs)
          : defaultOperationalDelayMs;
  maximumDelaySamples_ =
      sampleRate_ * static_cast<double>(maximumDelayMs_) * 0.001;

  // Three older samples are required by the causal four-point interpolator.
  const auto capacity =
      static_cast<std::size_t>(std::ceil(maximumDelaySamples_)) + 4U;
  buffer_.assign(sampleRate_ > 0.0 ? capacity : 0U, 0.0f);
  writeIndex_ = 0;
  setDelaySamples(requestedDelaySamples_);
  reset();
}

void FractionalDelay::reset() noexcept {
  std::fill(buffer_.begin(), buffer_.end(), 0.0f);
  writeIndex_ = 0;
}

void FractionalDelay::setDelayMs(float delayMs) noexcept {
  if (!std::isfinite(delayMs) || sampleRate_ <= 0.0) {
    setDelaySamples(0.0);
    return;
  }

  // Double precision retains substantially finer than the required 0.01 ms
  // setting resolution at all normal audio sample rates.
  setDelaySamples(static_cast<double>(delayMs) * sampleRate_ * 0.001);
}

void FractionalDelay::setDelaySamples(double fractionalSamples) noexcept {
  if (!std::isfinite(fractionalSamples))
    fractionalSamples = 0.0;

  requestedDelaySamples_ = std::clamp(fractionalSamples, -maximumDelaySamples_,
                                      maximumDelaySamples_);
  appliedDelaySamples_ = std::max(0.0, requestedDelaySamples_);
}

float FractionalDelay::getDelayMs() const noexcept {
  return sampleRate_ > 0.0
             ? static_cast<float>(requestedDelaySamples_ * 1000.0 / sampleRate_)
             : 0.0f;
}

float FractionalDelay::sampleAtDelay(std::size_t delay) const noexcept {
  const std::size_t size = buffer_.size();
  return buffer_[(writeIndex_ + size - (delay % size)) % size];
}

float FractionalDelay::processSample(float input) noexcept {
  if (buffer_.empty())
    return input;

  buffer_[writeIndex_] = std::isfinite(input) ? input : 0.0f;

  const double delay = appliedDelaySamples_;
  const auto integerDelay = static_cast<std::size_t>(std::floor(delay));
  const double fraction = delay - static_cast<double>(integerDelay);

  float output = 0.0f;
  if (fraction <= 1.0e-9) {
    output = sampleAtDelay(integerDelay);
  } else {
    // Third-order Lagrange interpolation through causal samples at t=0..3,
    // evaluated at the fractional delay t in [0, 1).
    const double t = fraction;
    const double c0 = -((t - 1.0) * (t - 2.0) * (t - 3.0)) / 6.0;
    const double c1 = (t * (t - 2.0) * (t - 3.0)) / 2.0;
    const double c2 = -(t * (t - 1.0) * (t - 3.0)) / 2.0;
    const double c3 = (t * (t - 1.0) * (t - 2.0)) / 6.0;

    output = static_cast<float>(c0 * sampleAtDelay(integerDelay) +
                                c1 * sampleAtDelay(integerDelay + 1U) +
                                c2 * sampleAtDelay(integerDelay + 2U) +
                                c3 * sampleAtDelay(integerDelay + 3U));
  }

  writeIndex_ = (writeIndex_ + 1U) % buffer_.size();
  return output;
}

void FractionalDelay::process(const float *input, float *output,
                              int numSamples) noexcept {
  if (input == nullptr || output == nullptr || numSamples <= 0)
    return;

  for (int sample = 0; sample < numSamples; ++sample)
    output[sample] = processSample(input[sample]);
}

} // namespace ChugGate::DSP
