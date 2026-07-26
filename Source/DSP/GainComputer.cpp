#include "GainComputer.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

void GainComputer::prepare(double) noexcept {}

void GainComputer::reset() noexcept {}

float GainComputer::processSample(const Input &rawInput) const noexcept {
  const float levelDb = finiteOr(rawInput.detectorLevelDb, -100.0f);
  const float thresholdDb = finiteOr(rawInput.thresholdDb, -40.0f);
  const float floorDb =
      std::clamp(finiteOr(rawInput.floorDb, -80.0f), -80.0f, 0.0f);
  const float floorLinear = floorGain(floorDb);
  const float gamma = std::max(finiteOr(rawInput.gamma, 1.0f), 0.001f);

  float amount = 0.0f;
  switch (mode_) {
  case Mode::HardGate:
    if (rawInput.hasStateHint) {
      amount = rawInput.stateHintIsOpen ? 1.0f : 0.0f;
    } else if (std::isfinite(rawInput.openCertainty) &&
               rawInput.openCertainty >= 0.0f) {
      amount = rawInput.openCertainty >= 0.5f ? 1.0f : 0.0f;
    } else {
      amount = levelDb >= thresholdDb ? 1.0f : 0.0f;
    }
    break;

  case Mode::SoftGate:
    if (std::isfinite(rawInput.openCertainty) &&
        rawInput.openCertainty >= 0.0f) {
      amount = std::clamp(rawInput.openCertainty, 0.0f, 1.0f);
    } else {
      amount = softDecision(levelDb, thresholdDb, rawInput.softKneeDb);
    }
    break;

  case Mode::DownwardExpander: {
    const float gainDb =
        downwardExpanderGainDb(levelDb, thresholdDb, rawInput.ratio, floorDb);
    const float unshapedGain = std::pow(10.0f, gainDb * 0.05f);
    if (floorLinear < 1.0f)
      amount = (unshapedGain - floorLinear) / (1.0f - floorLinear);
    else
      amount = 1.0f;
    break;
  }
  }

  return shape(amount, floorLinear, gamma);
}

void GainComputer::process(const Input *input, float *targetGain,
                           int numSamples) const noexcept {
  if (input == nullptr || targetGain == nullptr || numSamples <= 0)
    return;

  for (int sample = 0; sample < numSamples; ++sample)
    targetGain[sample] = processSample(input[sample]);
}

float GainComputer::floorGain(float floorDb) noexcept {
  const float boundedDb = std::clamp(finiteOr(floorDb, -80.0f), -80.0f, 0.0f);
  return std::pow(10.0f, boundedDb * 0.05f);
}

float GainComputer::shape(float amount, float floorLinear,
                          float gamma) noexcept {
  const float floor = std::clamp(finiteOr(floorLinear, 0.0001f), 0.0f, 1.0f);
  const float boundedAmount = std::clamp(finiteOr(amount, 0.0f), 0.0f, 1.0f);
  const float boundedGamma = std::max(finiteOr(gamma, 1.0f), 0.001f);
  return floor + (1.0f - floor) * std::pow(boundedAmount, boundedGamma);
}

float GainComputer::softDecision(float detectorLevelDb, float thresholdDb,
                                 float kneeDb) noexcept {
  const float threshold = finiteOr(thresholdDb, -40.0f);
  const float width = std::max(finiteOr(kneeDb, 0.0f), 0.0f);
  const float level = finiteOr(detectorLevelDb, threshold - width);
  if (width <= 0.0f)
    return level >= threshold ? 1.0f : 0.0f;

  const float u = std::clamp((level - (threshold - width)) / width, 0.0f, 1.0f);
  return u * u * (3.0f - 2.0f * u);
}

float GainComputer::downwardExpanderGainDb(float detectorLevelDb,
                                           float thresholdDb, float ratio,
                                           float floorDb) noexcept {
  const float floor = std::clamp(finiteOr(floorDb, -80.0f), -80.0f, 0.0f);
  const float level = finiteOr(detectorLevelDb, floor);
  const float threshold = finiteOr(thresholdDb, -40.0f);
  const float boundedRatio = std::max(finiteOr(ratio, 2.0f), 1.000001f);

  // Above threshold an expander must remain at unity. Below threshold this is
  // exactly section 15.3: max(G_floor_dB, (R - 1) * (L - T)).
  if (level >= threshold)
    return 0.0f;
  return std::max(floor, (boundedRatio - 1.0f) * (level - threshold));
}

float GainComputer::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

} // namespace ChugGate::DSP
