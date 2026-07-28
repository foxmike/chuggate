#include "PowerlineShield.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double analysisTimeSeconds = 0.2;
constexpr double minimumDecisionSeconds = 0.05;
} // namespace

float PowerlineShield::Biquad::process(float input) noexcept {
  const float output = b0 * input + z1;
  z1 = b1 * input - a1 * output + z2;
  z2 = b2 * input - a2 * output;
  return output;
}

void PowerlineShield::Biquad::reset() noexcept {
  z1 = 0.0f;
  z2 = 0.0f;
}

void PowerlineShield::Biquad::setBypass() noexcept {
  b0 = 1.0f;
  b1 = b2 = a1 = a2 = 0.0f;
}

void PowerlineShield::EnergyTracker::prepare(double frequency,
                                             double sampleRate) noexcept {
  const double angle = 2.0 * pi * frequency / sampleRate;
  rotationReal = std::cos(angle);
  rotationImag = std::sin(angle);
  reset();
}

void PowerlineShield::EnergyTracker::reset() noexcept {
  oscillatorReal = 1.0;
  oscillatorImag = 0.0;
  correlationReal = 0.0;
  correlationImag = 0.0;
}

void PowerlineShield::EnergyTracker::push(float sample,
                                          double smoothing) noexcept {
  correlationReal += smoothing * (sample * oscillatorReal - correlationReal);
  correlationImag += smoothing * (sample * oscillatorImag - correlationImag);

  const double nextReal =
      oscillatorReal * rotationReal - oscillatorImag * rotationImag;
  oscillatorImag =
      oscillatorReal * rotationImag + oscillatorImag * rotationReal;
  oscillatorReal = nextReal;
}

float PowerlineShield::EnergyTracker::energy() const noexcept {
  return static_cast<float>(
      2.0 * (correlationReal * correlationReal +
             correlationImag * correlationImag));
}

void PowerlineShield::prepare(double sampleRate, int maxBlockSize) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  if (sampleRate_ > 0.0) {
    constexpr double frequencies[] = {50.0, 100.0, 150.0,
                                      60.0, 120.0, 180.0};
    for (std::size_t index = 0; index < trackers_.size(); ++index)
      trackers_[index].prepare(frequencies[index], sampleRate_);
  }
  coefficientsDirty_.store(true, std::memory_order_release);
  reset();
}

void PowerlineShield::reset() noexcept {
  for (auto &notch : notches_)
    notch.reset();
  for (auto &tracker : trackers_)
    tracker.reset();
  analysedSamples_ = 0;
  activeNotchMode_ = HumMode::Off;
  const HumMode requested = requestedMode_.load(std::memory_order_relaxed);
  chosenMode_.store(requested == HumMode::Auto ? HumMode::Off : requested,
                    std::memory_order_relaxed);
  confidence_.store(0.0f, std::memory_order_relaxed);
  humEnergy_.store(0.0f, std::memory_order_relaxed);
  coefficientsDirty_.store(true, std::memory_order_release);
}

void PowerlineShield::setHumMode(HumMode mode) noexcept {
  const bool changed =
      requestedMode_.exchange(mode, std::memory_order_relaxed) != mode;
  if (mode != HumMode::Auto)
    chosenMode_.store(mode, std::memory_order_relaxed);
  if (changed)
    markCoefficientsDirty();
}

void PowerlineShield::setHarmonics(int harmonicCount) noexcept {
  const int harmonics = std::clamp(harmonicCount, 1, 3);
  if (harmonicCount_.exchange(harmonics, std::memory_order_relaxed) != harmonics)
    markCoefficientsDirty();
}

void PowerlineShield::setSecondHarmonicEnabled(bool enabled) noexcept {
  const int current = harmonicCount_.load(std::memory_order_relaxed);
  setHarmonics(enabled ? std::max(2, current) : 1);
}

void PowerlineShield::setThirdHarmonicEnabled(bool enabled) noexcept {
  const int current = harmonicCount_.load(std::memory_order_relaxed);
  setHarmonics(enabled ? 3 : std::min(current, 2));
}

void PowerlineShield::setNotchQ(float q) noexcept {
  const float notchQ = std::clamp(finiteOr(q, 20.0f), 1.0f, 100.0f);
  if (std::abs(notchQ_.exchange(notchQ, std::memory_order_relaxed) - notchQ) >
      1.0e-7f)
    markCoefficientsDirty();
}

void PowerlineShield::setHumImmunityAmount(float eta) noexcept {
  humImmunityAmount_.store(std::clamp(finiteOr(eta, 1.0f), 0.0f, 2.0f),
                           std::memory_order_relaxed);
}

float PowerlineShield::estimateMusicalEnergy(float keyEnergy) const noexcept {
  return calculateMusicalEnergy(
      keyEnergy, humEnergy_.load(std::memory_order_relaxed),
      humImmunityAmount_.load(std::memory_order_relaxed));
}

float PowerlineShield::applyHumImmunity(float keyEnergy,
                                        float humEnergy) const noexcept {
  return calculateMusicalEnergy(
      keyEnergy, humEnergy,
      humImmunityAmount_.load(std::memory_order_relaxed));
}

float PowerlineShield::calculateMusicalEnergy(float keyEnergy, float humEnergy,
                                              float eta) noexcept {
  const float cleanKey = std::max(0.0f, finiteOr(keyEnergy, 0.0f));
  const float cleanHum = std::max(0.0f, finiteOr(humEnergy, 0.0f));
  const float cleanEta = std::max(0.0f, finiteOr(eta, 0.0f));
  return std::max(0.0f, cleanKey - cleanEta * cleanHum);
}

void PowerlineShield::process(float *monoKey, int numSamples) noexcept {
  process(monoKey, monoKey, numSamples);
}

void PowerlineShield::process(const float *input, float *output,
                              int numSamples) noexcept {
  if (input == nullptr || output == nullptr || numSamples <= 0)
    return;

  if (sampleRate_ > 0.0)
    updateDetection(input, numSamples);

  const HumMode requested = requestedMode_.load(std::memory_order_relaxed);
  const HumMode desired =
      requested == HumMode::Auto
          ? chosenMode_.load(std::memory_order_relaxed)
          : requested;
  if (coefficientsDirty_.exchange(false, std::memory_order_acquire) ||
      desired != activeNotchMode_)
    updateNotches(desired);

  const int harmonicCount =
      harmonicCount_.load(std::memory_order_relaxed);
  const bool enabled = desired == HumMode::Hz50 || desired == HumMode::Hz60;
  for (int sample = 0; sample < numSamples; ++sample) {
    float value = std::isfinite(input[sample]) ? input[sample] : 0.0f;
    if (enabled) {
      for (int harmonic = 0; harmonic < harmonicCount; ++harmonic)
        value = notches_[static_cast<std::size_t>(harmonic)].process(value);
    }
    output[sample] = value;
  }
}

void PowerlineShield::updateDetection(const float *input,
                                      int numSamples) noexcept {
  const double smoothing =
      1.0 - std::exp(-1.0 / (analysisTimeSeconds * sampleRate_));
  for (int sample = 0; sample < numSamples; ++sample) {
    const float value = std::isfinite(input[sample]) ? input[sample] : 0.0f;
    for (auto &tracker : trackers_)
      tracker.push(value, smoothing);
  }
  analysedSamples_ += static_cast<std::uint64_t>(numSamples);

  const float energy50 = trackers_[0].energy();
  const float energy100 = trackers_[1].energy();
  const float energy150 = trackers_[2].energy();
  const float energy60 = trackers_[3].energy();
  const float energy120 = trackers_[4].energy();
  const float energy180 = trackers_[5].energy();
  constexpr float epsilon = 1.0e-20f;

  // Harmonics add evidence only when they are consistent with their
  // fundamental, preventing an unrelated strong overtone from dominating.
  const float consistency50 =
      0.25f * std::min(energy100 / (energy50 + epsilon), 1.0f) +
      0.15f * std::min(energy150 / (energy50 + epsilon), 1.0f);
  const float consistency60 =
      0.25f * std::min(energy120 / (energy60 + epsilon), 1.0f) +
      0.15f * std::min(energy180 / (energy60 + epsilon), 1.0f);
  const float score50 =
      energy50 * (1.0f + consistency50) + 0.1f * energy100 +
      0.05f * energy150;
  const float score60 =
      energy60 * (1.0f + consistency60) + 0.1f * energy120 +
      0.05f * energy180;
  const float total = score50 + score60;
  float confidence =
      total > epsilon ? std::abs(score50 - score60) / total : 0.0f;

  const double analysedSeconds =
      static_cast<double>(analysedSamples_) / sampleRate_;
  confidence *= static_cast<float>(
      std::clamp(analysedSeconds / analysisTimeSeconds, 0.0, 1.0));
  confidence_.store(confidence, std::memory_order_relaxed);

  const bool choose50 = score50 >= score60;
  const HumMode requested = requestedMode_.load(std::memory_order_relaxed);
  const float selectedHumEnergy =
      requested == HumMode::Hz50
          ? score50
          : (requested == HumMode::Hz60 ? score60
                                        : (choose50 ? score50 : score60));
  humEnergy_.store(selectedHumEnergy, std::memory_order_relaxed);
  if (requested == HumMode::Auto &&
      analysedSeconds >= minimumDecisionSeconds && total > epsilon) {
    const HumMode detected = choose50 ? HumMode::Hz50 : HumMode::Hz60;
    if (chosenMode_.exchange(detected, std::memory_order_relaxed) != detected)
      markCoefficientsDirty();
  }
}

void PowerlineShield::updateNotches(HumMode mode) noexcept {
  activeNotchMode_ = mode;
  if (sampleRate_ <= 0.0 ||
      (mode != HumMode::Hz50 && mode != HumMode::Hz60)) {
    for (auto &notch : notches_)
      notch.setBypass();
    return;
  }

  const double fundamental = mode == HumMode::Hz50 ? 50.0 : 60.0;
  const float q = notchQ_.load(std::memory_order_relaxed);
  for (std::size_t index = 0; index < notches_.size(); ++index)
    makeNotch(notches_[index], sampleRate_,
              fundamental * static_cast<double>(index + 1), q);
}

void PowerlineShield::makeNotch(Biquad &filter, double sampleRate,
                                double frequency, float q) noexcept {
  if (frequency >= sampleRate * 0.5) {
    filter.setBypass();
    return;
  }

  const double omega = 2.0 * pi * frequency / sampleRate;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * q);
  const double a0 = 1.0 + alpha;
  filter.b0 = static_cast<float>(1.0 / a0);
  filter.b1 = static_cast<float>(-2.0 * cosine / a0);
  filter.b2 = static_cast<float>(1.0 / a0);
  filter.a1 = static_cast<float>(-2.0 * cosine / a0);
  filter.a2 = static_cast<float>((1.0 - alpha) / a0);
}

float PowerlineShield::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

} // namespace ChugGate::DSP
