#include "HissCurtain.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr float butterworthDamping = 1.4142135623730950488f;
constexpr double cutoffSmoothingSeconds = 0.02;
constexpr double depthSmoothingSeconds = 0.01;
constexpr float frequencyOrderingGapHz = 1.0f;
constexpr float minimumAllowedSnrDb = -120.0f;
constexpr float maximumAllowedSnrDb = 120.0f;
constexpr float snrOrderingGapDb = 0.01f;
} // namespace

float HissCurtain::TptLowPass::process(float input, float g) noexcept {
  // Cytomic/Simper TPT state-variable filter. integrator1/2 are equivalent
  // current states, so coefficient modulation does not invalidate the state.
  const float a1 = 1.0f / (1.0f + g * (g + butterworthDamping));
  const float a2 = g * a1;
  const float a3 = g * a2;
  const float v3 = input - integrator2;
  const float v1 = a1 * integrator1 + a2 * v3;
  const float lowPass = integrator2 + a2 * integrator1 + a3 * v3;
  integrator1 = 2.0f * v1 - integrator1;
  integrator2 = 2.0f * lowPass - integrator2;
  return lowPass;
}

void HissCurtain::TptLowPass::reset() noexcept {
  integrator1 = 0.0f;
  integrator2 = 0.0f;
}

void HissCurtain::ChannelState::reset() noexcept {
  stage1.reset();
  stage2.reset();
}

void HissCurtain::prepare(double sampleRate, int maxBlockSize,
                          int numChannels) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  numChannels_ = std::max(0, numChannels);
  channels_.resize(static_cast<std::size_t>(numChannels_));

  cutoffSmoothingFactor_ =
      sampleRate_ > 0.0
          ? static_cast<float>(
                1.0 - std::exp(-1.0 / (cutoffSmoothingSeconds * sampleRate_)))
          : 1.0f;
  depthSmoothingFactor_ =
      sampleRate_ > 0.0
          ? static_cast<float>(
                1.0 - std::exp(-1.0 / (depthSmoothingSeconds * sampleRate_)))
          : 1.0f;

  reset();
}

void HissCurtain::reset() noexcept {
  for (auto &channel : channels_)
    channel.reset();

  float minimumHz = minimumHz_.load(std::memory_order_relaxed);
  float maximumHz = maximumHz_.load(std::memory_order_relaxed);
  protectFrequencyOrdering(minimumHz, maximumHz);
  float minimumSnrDb = minimumSnrDb_.load(std::memory_order_relaxed);
  float maximumSnrDb = maximumSnrDb_.load(std::memory_order_relaxed);
  protectSnrOrdering(minimumSnrDb, maximumSnrDb);

  const float cutoff =
      mapCutoffHz(snrDb_.load(std::memory_order_relaxed), minimumSnrDb,
                  maximumSnrDb, minimumHz, maximumHz);
  smoothedLogCutoff_ = std::log(cutoff);
  smoothedDepth_ =
      std::clamp(depth_.load(std::memory_order_relaxed), 0.0f, 1.0f);
  currentCutoffHz_.store(cutoff, std::memory_order_relaxed);
}

void HissCurtain::setMinFreqHz(float frequencyHz) noexcept {
  float minimumHz = std::clamp(finiteOr(frequencyHz, 2000.0f), minimumMinFreqHz,
                               maximumMinFreqHz);
  float maximumHz =
      std::clamp(finiteOr(maximumHz_.load(std::memory_order_relaxed), 16000.0f),
                 minimumMaxFreqHz, maximumMaxFreqHz);
  protectFrequencyOrdering(minimumHz, maximumHz);

  // Write the adjusted partner first so the audio thread cannot observe the
  // new minimum paired with an old, lower maximum.
  maximumHz_.store(maximumHz, std::memory_order_relaxed);
  minimumHz_.store(minimumHz, std::memory_order_relaxed);
}

void HissCurtain::setMaxFreqHz(float frequencyHz) noexcept {
  float minimumHz =
      std::clamp(finiteOr(minimumHz_.load(std::memory_order_relaxed), 2000.0f),
                 minimumMinFreqHz, maximumMinFreqHz);
  float maximumHz = std::clamp(finiteOr(frequencyHz, 16000.0f),
                               minimumMaxFreqHz, maximumMaxFreqHz);
  protectFrequencyOrdering(minimumHz, maximumHz);

  // Write the adjusted partner first so the audio thread cannot observe the
  // new maximum paired with an old, higher minimum.
  minimumHz_.store(minimumHz, std::memory_order_relaxed);
  maximumHz_.store(maximumHz, std::memory_order_relaxed);
}

void HissCurtain::setDepth(float depth) noexcept {
  depth_.store(std::clamp(finiteOr(depth, 1.0f), 0.0f, 1.0f),
               std::memory_order_relaxed);
}

void HissCurtain::setSnrRange(float minimumSnrDb, float maximumSnrDb) noexcept {
  minimumSnrDb = std::clamp(finiteOr(minimumSnrDb, 0.0f), minimumAllowedSnrDb,
                            maximumAllowedSnrDb);
  maximumSnrDb = std::clamp(finiteOr(maximumSnrDb, 30.0f), minimumAllowedSnrDb,
                            maximumAllowedSnrDb);
  protectSnrOrdering(minimumSnrDb, maximumSnrDb);
  minimumSnrDb_.store(minimumSnrDb, std::memory_order_relaxed);
  maximumSnrDb_.store(maximumSnrDb, std::memory_order_relaxed);
}

void HissCurtain::setSnr(float snrDb) noexcept {
  snrDb_.store(std::clamp(finiteOr(snrDb, 0.0f), minimumAllowedSnrDb,
                          maximumAllowedSnrDb),
               std::memory_order_relaxed);
}

float HissCurtain::getCutoffHz() const noexcept {
  return currentCutoffHz_.load(std::memory_order_relaxed);
}

void HissCurtain::process(const float *const *input, float *const *output,
                          int numChannels, int numSamples) noexcept {
  if (input == nullptr || output == nullptr || numSamples <= 0 ||
      sampleRate_ <= 0.0 || channels_.empty())
    return;

  const int channelsToProcess = std::clamp(numChannels, 0, numChannels_);
  if (channelsToProcess == 0)
    return;

  float minimumHz = minimumHz_.load(std::memory_order_relaxed);
  float maximumHz = maximumHz_.load(std::memory_order_relaxed);
  protectFrequencyOrdering(minimumHz, maximumHz);
  float minimumSnrDb = minimumSnrDb_.load(std::memory_order_relaxed);
  float maximumSnrDb = maximumSnrDb_.load(std::memory_order_relaxed);
  protectSnrOrdering(minimumSnrDb, maximumSnrDb);

  const float targetCutoffHz =
      mapCutoffHz(snrDb_.load(std::memory_order_relaxed), minimumSnrDb,
                  maximumSnrDb, minimumHz, maximumHz);
  const float targetLogCutoff = std::log(targetCutoffHz);
  const float targetDepth =
      std::clamp(depth_.load(std::memory_order_relaxed), 0.0f, 1.0f);
  const float maximumFilterCutoff = static_cast<float>(sampleRate_ * 0.495);

  for (int sample = 0; sample < numSamples; ++sample) {
    smoothedLogCutoff_ +=
        cutoffSmoothingFactor_ * (targetLogCutoff - smoothedLogCutoff_);
    const float smoothedCutoff = std::exp(smoothedLogCutoff_);
    currentCutoffHz_.store(smoothedCutoff, std::memory_order_relaxed);

    smoothedDepth_ += depthSmoothingFactor_ * (targetDepth - smoothedDepth_);
    const float filterCutoff =
        std::clamp(smoothedCutoff, 1.0f, maximumFilterCutoff);
    const float g = static_cast<float>(
        std::tan(pi * static_cast<double>(filterCutoff) / sampleRate_));

    for (int channel = 0; channel < channelsToProcess; ++channel) {
      const float *inputChannel = input[channel];
      float *outputChannel = output[channel];
      if (outputChannel == nullptr)
        continue;

      const float dry =
          inputChannel != nullptr && std::isfinite(inputChannel[sample])
              ? inputChannel[sample]
              : 0.0f;
      ChannelState &state = channels_[static_cast<std::size_t>(channel)];
      const float filtered =
          state.stage2.process(state.stage1.process(dry, g), g);
      outputChannel[sample] = dry + smoothedDepth_ * (filtered - dry);
    }
  }
}

float HissCurtain::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

void HissCurtain::protectFrequencyOrdering(float &minimumHz,
                                           float &maximumHz) noexcept {
  minimumHz = std::clamp(finiteOr(minimumHz, 2000.0f), minimumMinFreqHz,
                         maximumMinFreqHz);
  maximumHz = std::clamp(finiteOr(maximumHz, 16000.0f), minimumMaxFreqHz,
                         maximumMaxFreqHz);

  if (minimumHz >= maximumHz) {
    maximumHz = std::min(maximumMaxFreqHz, minimumHz + frequencyOrderingGapHz);
    if (minimumHz >= maximumHz)
      minimumHz =
          std::max(minimumMinFreqHz, maximumHz - frequencyOrderingGapHz);
  }
}

void HissCurtain::protectSnrOrdering(float &minimumDb,
                                     float &maximumDb) noexcept {
  minimumDb = std::clamp(finiteOr(minimumDb, 0.0f), minimumAllowedSnrDb,
                         maximumAllowedSnrDb);
  maximumDb = std::clamp(finiteOr(maximumDb, 30.0f), minimumAllowedSnrDb,
                         maximumAllowedSnrDb);

  if (maximumDb <= minimumDb) {
    maximumDb = std::min(maximumAllowedSnrDb, minimumDb + snrOrderingGapDb);
    if (maximumDb <= minimumDb)
      minimumDb = std::max(minimumAllowedSnrDb, maximumDb - snrOrderingGapDb);
  }
}

float HissCurtain::mapCutoffHz(float snrDb, float minimumSnrDb,
                               float maximumSnrDb, float minimumHz,
                               float maximumHz) noexcept {
  const float qHigh =
      std::clamp((finiteOr(snrDb, minimumSnrDb) - minimumSnrDb) /
                     (maximumSnrDb - minimumSnrDb),
                 0.0f, 1.0f);
  const float logCutoff =
      std::log(minimumHz) + qHigh * (std::log(maximumHz) - std::log(minimumHz));
  return std::exp(logCutoff);
}

} // namespace ChugGate::DSP
