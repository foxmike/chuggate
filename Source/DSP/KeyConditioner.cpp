#include "KeyConditioner.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr float butterworthQ = 0.70710678f;

float finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

float clampFrequency(float value, float minimum, float maximum,
                     double sampleRate) noexcept {
  const float nyquistLimit =
      sampleRate > 0.0 ? static_cast<float>(sampleRate * 0.495) : maximum;
  return std::clamp(finiteOr(value, minimum), minimum,
                    std::max(minimum, std::min(maximum, nyquistLimit)));
}

} // namespace

float KeyConditioner::Biquad::process(float input) noexcept {
  const float output = b0 * input + z1;
  z1 = b1 * input - a1 * output + z2;
  z2 = b2 * input - a2 * output;
  return output;
}

void KeyConditioner::Biquad::reset() noexcept {
  z1 = 0.0f;
  z2 = 0.0f;
}

void KeyConditioner::Biquad::setBypass() noexcept {
  b0 = 1.0f;
  b1 = b2 = a1 = a2 = 0.0f;
}

void KeyConditioner::prepare(double sampleRate, int maxBlockSize) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  coefficientsDirty_.store(true, std::memory_order_release);
  updateCoefficients();
  reset();
}

void KeyConditioner::reset() noexcept {
  for (auto &filter : highPass_)
    filter.reset();
  for (auto &filter : lowPass_)
    filter.reset();
  emphasis_.reset();
}

void KeyConditioner::setHighPassEnabled(bool enabled) noexcept {
  highPassEnabled_.store(enabled, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setHighPassFrequency(float frequencyHz) noexcept {
  highPassFrequency_.store(std::clamp(finiteOr(frequencyHz, 60.0f), 20.0f,
                                      250.0f),
                           std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setHighPassSlope(FilterSlope slope) noexcept {
  highPassSlope_.store(slope, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setLowPassEnabled(bool enabled) noexcept {
  lowPassEnabled_.store(enabled, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setLowPassFrequency(float frequencyHz) noexcept {
  lowPassFrequency_.store(std::clamp(finiteOr(frequencyHz, 20000.0f), 1500.0f,
                                     20000.0f),
                          std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setLowPassSlope(FilterSlope slope) noexcept {
  lowPassSlope_.store(slope, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setHpf(bool enabled, float frequencyHz,
                            FilterSlope slope) noexcept {
  highPassEnabled_.store(enabled, std::memory_order_relaxed);
  highPassFrequency_.store(std::clamp(finiteOr(frequencyHz, 60.0f), 20.0f,
                                      250.0f),
                           std::memory_order_relaxed);
  highPassSlope_.store(slope, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setLpf(bool enabled, float frequencyHz,
                            FilterSlope slope) noexcept {
  lowPassEnabled_.store(enabled, std::memory_order_relaxed);
  lowPassFrequency_.store(std::clamp(finiteOr(frequencyHz, 20000.0f), 1500.0f,
                                     20000.0f),
                          std::memory_order_relaxed);
  lowPassSlope_.store(slope, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setEmphasisMode(EmphasisMode mode) noexcept {
  emphasisMode_.store(mode, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setEmphasisEnabled(bool enabled) noexcept {
  setEmphasisMode(enabled ? EmphasisMode::Bell : EmphasisMode::Off);
}

void KeyConditioner::setEmphasisFrequency(float frequencyHz) noexcept {
  emphasisFrequency_.store(
      std::clamp(finiteOr(frequencyHz, 1000.0f), 20.0f, 20000.0f),
      std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setEmphasisQ(float q) noexcept {
  emphasisQ_.store(std::clamp(finiteOr(q, butterworthQ), 0.1f, 20.0f),
                   std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setEmphasisGain(float gainDb) noexcept {
  emphasisGain_.store(std::clamp(finiteOr(gainDb, 0.0f), -24.0f, 24.0f),
                      std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::setEmphasis(EmphasisMode mode, float frequencyHz, float q,
                                 float gainDb, bool soloKeyPath) noexcept {
  emphasisMode_.store(mode, std::memory_order_relaxed);
  emphasisFrequency_.store(
      std::clamp(finiteOr(frequencyHz, 1000.0f), 20.0f, 20000.0f),
      std::memory_order_relaxed);
  emphasisQ_.store(std::clamp(finiteOr(q, butterworthQ), 0.1f, 20.0f),
                   std::memory_order_relaxed);
  emphasisGain_.store(std::clamp(finiteOr(gainDb, 0.0f), -24.0f, 24.0f),
                      std::memory_order_relaxed);
  soloKeyPath_.store(soloKeyPath, std::memory_order_relaxed);
  markCoefficientsDirty();
}

void KeyConditioner::process(float *monoKey, int numSamples) noexcept {
  process(monoKey, monoKey, numSamples);
}

void KeyConditioner::process(const float *input, float *output,
                             int numSamples) noexcept {
  if (input == nullptr || output == nullptr || numSamples <= 0)
    return;

  if (coefficientsDirty_.exchange(false, std::memory_order_acquire))
    updateCoefficients();

  const bool useHighPass =
      highPassEnabled_.load(std::memory_order_relaxed) && sampleRate_ > 0.0;
  const bool cascadeHighPass =
      highPassSlope_.load(std::memory_order_relaxed) == FilterSlope::Db24;
  const bool useLowPass =
      lowPassEnabled_.load(std::memory_order_relaxed) && sampleRate_ > 0.0;
  const bool cascadeLowPass =
      lowPassSlope_.load(std::memory_order_relaxed) == FilterSlope::Db24;
  const bool useEmphasis =
      emphasisMode_.load(std::memory_order_relaxed) != EmphasisMode::Off &&
      sampleRate_ > 0.0;

  for (int sample = 0; sample < numSamples; ++sample) {
    float value = std::isfinite(input[sample]) ? input[sample] : 0.0f;
    if (useHighPass) {
      value = highPass_[0].process(value);
      if (cascadeHighPass)
        value = highPass_[1].process(value);
    }
    if (useLowPass) {
      value = lowPass_[0].process(value);
      if (cascadeLowPass)
        value = lowPass_[1].process(value);
    }
    if (useEmphasis)
      value = emphasis_.process(value);
    output[sample] = value;
  }
}

void KeyConditioner::updateCoefficients() noexcept {
  if (sampleRate_ <= 0.0) {
    for (auto &filter : highPass_)
      filter.setBypass();
    for (auto &filter : lowPass_)
      filter.setBypass();
    emphasis_.setBypass();
    return;
  }

  const float highPassFrequency =
      clampFrequency(highPassFrequency_.load(std::memory_order_relaxed), 20.0f,
                     250.0f, sampleRate_);
  const float lowPassFrequency =
      clampFrequency(lowPassFrequency_.load(std::memory_order_relaxed), 1500.0f,
                     20000.0f, sampleRate_);
  for (auto &filter : highPass_)
    makeHighPass(filter, sampleRate_, highPassFrequency);
  for (auto &filter : lowPass_)
    makeLowPass(filter, sampleRate_, lowPassFrequency);

  const float emphasisFrequency =
      clampFrequency(emphasisFrequency_.load(std::memory_order_relaxed), 20.0f,
                     20000.0f, sampleRate_);
  const float q = emphasisQ_.load(std::memory_order_relaxed);
  const float gain = emphasisGain_.load(std::memory_order_relaxed);
  if (emphasisMode_.load(std::memory_order_relaxed) ==
      EmphasisMode::BandPass)
    makeBandPass(emphasis_, sampleRate_, emphasisFrequency, q, gain);
  else
    makeBell(emphasis_, sampleRate_, emphasisFrequency, q, gain);
}

void KeyConditioner::makeHighPass(Biquad &filter, double sampleRate,
                                  float frequencyHz) noexcept {
  const double omega = 2.0 * pi * frequencyHz / sampleRate;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * butterworthQ);
  normalise(filter, 0.5 * (1.0 + cosine), -(1.0 + cosine),
            0.5 * (1.0 + cosine), 1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
}

void KeyConditioner::makeLowPass(Biquad &filter, double sampleRate,
                                 float frequencyHz) noexcept {
  const double omega = 2.0 * pi * frequencyHz / sampleRate;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * butterworthQ);
  normalise(filter, 0.5 * (1.0 - cosine), 1.0 - cosine,
            0.5 * (1.0 - cosine), 1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
}

void KeyConditioner::makeBell(Biquad &filter, double sampleRate,
                              float frequencyHz, float q,
                              float gainDb) noexcept {
  const double omega = 2.0 * pi * frequencyHz / sampleRate;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * q);
  const double amplitude = std::pow(10.0, gainDb / 40.0);
  normalise(filter, 1.0 + alpha * amplitude, -2.0 * cosine,
            1.0 - alpha * amplitude, 1.0 + alpha / amplitude,
            -2.0 * cosine, 1.0 - alpha / amplitude);
}

void KeyConditioner::makeBandPass(Biquad &filter, double sampleRate,
                                  float frequencyHz, float q,
                                  float gainDb) noexcept {
  const double omega = 2.0 * pi * frequencyHz / sampleRate;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * q);
  const double gain = std::pow(10.0, gainDb / 20.0);
  normalise(filter, alpha * gain, 0.0, -alpha * gain, 1.0 + alpha,
            -2.0 * cosine, 1.0 - alpha);
}

void KeyConditioner::normalise(Biquad &filter, double b0, double b1, double b2,
                               double a0, double a1, double a2) noexcept {
  const double inverseA0 = 1.0 / a0;
  filter.b0 = static_cast<float>(b0 * inverseA0);
  filter.b1 = static_cast<float>(b1 * inverseA0);
  filter.b2 = static_cast<float>(b2 * inverseA0);
  filter.a1 = static_cast<float>(a1 * inverseA0);
  filter.a2 = static_cast<float>(a2 * inverseA0);
}

} // namespace ChugGate::DSP
