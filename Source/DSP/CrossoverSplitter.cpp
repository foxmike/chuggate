#include "CrossoverSplitter.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double butterworthQ = 0.70710678118654752440;
constexpr double cutoffSmoothingSeconds = 0.02;
} // namespace

float CrossoverSplitter::BiquadState::process(
    float input, float b0, float b1, float b2,
    const Coefficients &coefficients) noexcept {
  const float output = b0 * input + z1;
  z1 = b1 * input - coefficients.a1 * output + z2;
  z2 = b2 * input - coefficients.a2 * output;
  return output;
}

void CrossoverSplitter::BiquadState::reset() noexcept {
  z1 = 0.0f;
  z2 = 0.0f;
}

void CrossoverSplitter::LR4Pair::process(
    float input, const Coefficients &coefficients, float &lowOut,
    float &highOut) noexcept {
  lowOut = low[0].process(input, coefficients.lowB0, coefficients.lowB1,
                          coefficients.lowB2, coefficients);
  lowOut = low[1].process(lowOut, coefficients.lowB0, coefficients.lowB1,
                          coefficients.lowB2, coefficients);

  highOut = high[0].process(input, coefficients.highB0, coefficients.highB1,
                            coefficients.highB2, coefficients);
  highOut =
      high[1].process(highOut, coefficients.highB0, coefficients.highB1,
                      coefficients.highB2, coefficients);
}

void CrossoverSplitter::LR4Pair::reset() noexcept {
  for (auto &section : low)
    section.reset();
  for (auto &section : high)
    section.reset();
}

void CrossoverSplitter::ChannelState::reset() noexcept {
  dual.reset();
  triLow.reset();
  triHighUpper.reset();
  triHighLowCompensation.reset();
}

void CrossoverSplitter::SmoothedCutoff::prepare(double sampleRate,
                                                float initialHz) noexcept {
  currentHz = initialHz;
  smoothingFactor =
      sampleRate > 0.0
          ? static_cast<float>(
                1.0 - std::exp(-1.0 / (cutoffSmoothingSeconds * sampleRate)))
          : 1.0f;
}

float CrossoverSplitter::SmoothedCutoff::next(float targetHz) noexcept {
  currentHz += smoothingFactor * (targetHz - currentHz);
  return currentHz;
}

void CrossoverSplitter::prepare(double sampleRate, int maxBlockSize,
                                int numChannels) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  numChannels_ = std::max(0, numChannels);
  channels_.resize(static_cast<std::size_t>(numChannels_));

  float lowHz = triLowHz_.load(std::memory_order_relaxed);
  float highHz = triHighHz_.load(std::memory_order_relaxed);
  clampTri(lowHz, highHz);
  triLowHz_.store(lowHz, std::memory_order_relaxed);
  triHighHz_.store(highHz, std::memory_order_relaxed);

  const float dualHz =
      clampDual(dualHz_.load(std::memory_order_relaxed));
  dualHz_.store(dualHz, std::memory_order_relaxed);
  dualCutoff_.prepare(sampleRate_, dualHz);
  triLowCutoff_.prepare(sampleRate_, lowHz);
  triHighCutoff_.prepare(sampleRate_, highHz);
  reset();
}

void CrossoverSplitter::reset() noexcept {
  for (auto &channel : channels_)
    channel.reset();
}

void CrossoverSplitter::setMode(Mode mode) noexcept {
  mode_.store(mode, std::memory_order_relaxed);
}

CrossoverSplitter::Mode CrossoverSplitter::getMode() const noexcept {
  return mode_.load(std::memory_order_relaxed);
}

int CrossoverSplitter::getNumBands() const noexcept {
  switch (getMode()) {
  case Mode::Bypass:
    return 1;
  case Mode::TwoWay:
    return 2;
  case Mode::ThreeWay:
    return 3;
  }
  return 1;
}

void CrossoverSplitter::setCrossoverHz(float dualHz) noexcept {
  setDualCrossoverHz(dualHz);
}

void CrossoverSplitter::setCrossoverHz(float lowHz, float highHz) noexcept {
  setTriCrossoversHz(lowHz, highHz);
}

void CrossoverSplitter::setDualCrossoverHz(float frequencyHz) noexcept {
  dualHz_.store(clampDual(frequencyHz), std::memory_order_relaxed);
}

void CrossoverSplitter::setTriCrossoversHz(float lowHz,
                                           float highHz) noexcept {
  clampTri(lowHz, highHz);
  lowHz = std::min(lowHz, highHz - minimumCrossoverGapHz);
  highHz = std::max(highHz, lowHz + minimumCrossoverGapHz);
  triLowHz_.store(lowHz, std::memory_order_relaxed);
  triHighHz_.store(highHz, std::memory_order_relaxed);
}

void CrossoverSplitter::setTriLowCrossoverHz(float frequencyHz) noexcept {
  setTriCrossoversHz(frequencyHz,
                     triHighHz_.load(std::memory_order_relaxed));
}

void CrossoverSplitter::setTriHighCrossoverHz(float frequencyHz) noexcept {
  setTriCrossoversHz(triLowHz_.load(std::memory_order_relaxed), frequencyHz);
}

float CrossoverSplitter::getDualCrossoverHz() const noexcept {
  return dualHz_.load(std::memory_order_relaxed);
}

float CrossoverSplitter::getTriLowCrossoverHz() const noexcept {
  return triLowHz_.load(std::memory_order_relaxed);
}

float CrossoverSplitter::getTriHighCrossoverHz() const noexcept {
  return triHighHz_.load(std::memory_order_relaxed);
}

void CrossoverSplitter::process(const float *const *input,
                                float *const *band0, float *const *band1,
                                float *const *band2, int numChannels,
                                int numSamples) noexcept {
  if (input == nullptr || numSamples <= 0 || sampleRate_ <= 0.0 ||
      channels_.empty())
    return;

  const int channelsToProcess =
      std::clamp(numChannels, 0, numChannels_);
  if (channelsToProcess == 0)
    return;

  const Mode mode = mode_.load(std::memory_order_relaxed);
  const float dualTarget = dualHz_.load(std::memory_order_relaxed);
  const float triLowTarget = triLowHz_.load(std::memory_order_relaxed);
  const float triHighTarget = triHighHz_.load(std::memory_order_relaxed);

  for (int sample = 0; sample < numSamples; ++sample) {
    const Coefficients dualCoefficients =
        makeButterworth(sampleRate_, dualCutoff_.next(dualTarget));
    const Coefficients triLowCoefficients =
        makeButterworth(sampleRate_, triLowCutoff_.next(triLowTarget));
    const Coefficients triHighCoefficients =
        makeButterworth(sampleRate_, triHighCutoff_.next(triHighTarget));

    for (int channel = 0; channel < channelsToProcess; ++channel) {
      const float *inputChannel = input[channel];
      const float value =
          inputChannel != nullptr && std::isfinite(inputChannel[sample])
              ? inputChannel[sample]
              : 0.0f;
      ChannelState &state = channels_[static_cast<std::size_t>(channel)];

      float dualLow = 0.0f;
      float dualHigh = 0.0f;
      state.dual.process(value, dualCoefficients, dualLow, dualHigh);

      float triLow = 0.0f;
      float triUpper = 0.0f;
      state.triLow.process(value, triLowCoefficients, triLow, triUpper);

      float triMid = 0.0f;
      float triHigh = 0.0f;
      state.triHighUpper.process(triUpper, triHighCoefficients, triMid,
                                 triHigh);

      float compensatedLowPart = 0.0f;
      float compensatedHighPart = 0.0f;
      state.triHighLowCompensation.process(
          triLow, triHighCoefficients, compensatedLowPart,
          compensatedHighPart);
      const float triWeight = compensatedLowPart + compensatedHighPart;

      float output0 = value;
      float output1 = 0.0f;
      float output2 = 0.0f;
      if (mode == Mode::TwoWay) {
        output0 = dualLow;
        output1 = dualHigh;
      } else if (mode == Mode::ThreeWay) {
        output0 = triWeight;
        output1 = triMid;
        output2 = triHigh;
      }

      if (band0 != nullptr && band0[channel] != nullptr)
        band0[channel][sample] = output0;
      if (band1 != nullptr && band1[channel] != nullptr)
        band1[channel][sample] = output1;
      if (band2 != nullptr && band2[channel] != nullptr)
        band2[channel][sample] = output2;
    }
  }
}

float CrossoverSplitter::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

float CrossoverSplitter::clampDual(float frequencyHz) noexcept {
  return std::clamp(finiteOr(frequencyHz, 4000.0f), dualMinimumHz,
                    dualMaximumHz);
}

void CrossoverSplitter::clampTri(float &lowHz, float &highHz) noexcept {
  lowHz = std::clamp(finiteOr(lowHz, 350.0f), triLowMinimumHz,
                     triLowMaximumHz);
  highHz = std::clamp(finiteOr(highHz, 4000.0f), triHighMinimumHz,
                      triHighMaximumHz);

  if (lowHz + minimumCrossoverGapHz >= highHz) {
    lowHz = std::min(lowHz, triLowMaximumHz);
    highHz =
        std::max(highHz, lowHz + minimumCrossoverGapHz);
    highHz = std::min(highHz, triHighMaximumHz);
    lowHz = std::min(lowHz, highHz - minimumCrossoverGapHz);
  }
}

CrossoverSplitter::Coefficients
CrossoverSplitter::makeButterworth(double sampleRate,
                                   float frequencyHz) noexcept {
  Coefficients result{};
  if (sampleRate <= 0.0)
    return result;

  const double nyquistSafe = sampleRate * 0.495;
  const double cutoff =
      std::clamp(static_cast<double>(frequencyHz), 1.0, nyquistSafe);
  const double omega = 2.0 * pi * cutoff / sampleRate;
  const double cosine = std::cos(omega);
  const double alpha = std::sin(omega) / (2.0 * butterworthQ);
  const double inverseA0 = 1.0 / (1.0 + alpha);

  result.lowB0 =
      static_cast<float>(0.5 * (1.0 - cosine) * inverseA0);
  result.lowB1 = static_cast<float>((1.0 - cosine) * inverseA0);
  result.lowB2 = result.lowB0;
  result.highB0 =
      static_cast<float>(0.5 * (1.0 + cosine) * inverseA0);
  result.highB1 = static_cast<float>(-(1.0 + cosine) * inverseA0);
  result.highB2 = result.highB0;
  result.a1 = static_cast<float>(-2.0 * cosine * inverseA0);
  result.a2 = static_cast<float>((1.0 - alpha) * inverseA0);
  return result;
}

} // namespace ChugGate::DSP
