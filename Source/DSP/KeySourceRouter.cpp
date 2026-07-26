#include "KeySourceRouter.h"

#include <cmath>

namespace ChugGate::DSP {
namespace {
constexpr float silenceThreshold = 1.0e-8f;

float absolute(float value) noexcept { return value < 0.0f ? -value : value; }
} // namespace

void KeySourceRouter::prepare(double sampleRate, int maxBlockSize,
                              int numMainChannels,
                              int numSidechainChannels) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = maxBlockSize > 0 ? maxBlockSize : 0;
  preparedMainChannels_ = numMainChannels > 0 ? numMainChannels : 0;
  preparedSidechainChannels_ =
      numSidechainChannels > 0 ? numSidechainChannels : 0;
  reset();
}

void KeySourceRouter::reset() noexcept {
  lastKeySample_ = 0.0f;
  alignmentCalibrated_ = false;
  status_ = {};
}

void KeySourceRouter::setExternalBlend(float blend) noexcept {
  externalBlend_ = clampBlend(blend);
}

void KeySourceRouter::process(const float *const *mainChannels,
                              int numMainChannels,
                              const float *const *sidechainChannels,
                              int numSidechainChannels, float *monoKey,
                              int numSamples) noexcept {
  process(mainChannels, numMainChannels, sidechainChannels,
          numSidechainChannels, monoKey, numSamples, source_, externalBlend_,
          fallbackMode_);
}

void KeySourceRouter::process(const float *const *mainChannels,
                              int numMainChannels,
                              const float *const *sidechainChannels,
                              int numSidechainChannels, float *monoKey,
                              int numSamples, Source source,
                              float externalBlend,
                              FallbackMode fallbackMode) noexcept {
  status_ = {};
  status_.alignmentNotCalibrated = !alignmentCalibrated_;

  if (numSamples <= 0 || monoKey == nullptr)
    return;

  const int usableMainChannels =
      countUsableChannels(mainChannels, numMainChannels);
  const int usableSidechainChannels =
      countUsableChannels(sidechainChannels, numSidechainChannels);
  const bool mainPresent = usableMainChannels > 0;
  status_.externalPresent = usableSidechainChannels > 0;
  status_.externalSilent =
      status_.externalPresent &&
      isSilent(sidechainChannels, numSidechainChannels, numSamples);

  status_.channelMismatch =
      numMainChannels != preparedMainChannels_ ||
      (status_.externalPresent &&
       numSidechainChannels != preparedSidechainChannels_) ||
      usableMainChannels != numMainChannels ||
      usableSidechainChannels != numSidechainChannels ||
      numSamples > maxBlockSize_;

  const bool needsExternal = source == Source::ExternalSidechain ||
                             source == Source::BlendedMainExternal;
  const bool externalUsable =
      status_.externalPresent && !status_.externalSilent;

  if (needsExternal && !externalUsable) {
    status_.lowConfidence = true;
    status_.internalFallbackActive = true;
    applyUnavailableExternalFallback(mainChannels, numMainChannels, monoKey,
                                     numSamples, fallbackMode);
    return;
  }

  if (!mainPresent && source != Source::ExternalSidechain) {
    status_.lowConfidence = true;
    for (int sample = 0; sample < numSamples; ++sample)
      monoKey[sample] = 0.0f;
    lastKeySample_ = 0.0f;
    return;
  }

  if (source == Source::ExternalSidechain) {
    for (int sample = 0; sample < numSamples; ++sample)
      monoKey[sample] =
          averageSample(sidechainChannels, numSidechainChannels, sample);
  } else if (source == Source::BlendedMainExternal) {
    const float mu = clampBlend(externalBlend);
    const float internalAmount = 1.0f - mu;

    for (int sample = 0; sample < numSamples; ++sample) {
      const float internalEnvelope =
          averageEnvelope(mainChannels, numMainChannels, sample);
      const float externalEnvelope =
          averageEnvelope(sidechainChannels, numSidechainChannels, sample);
      monoKey[sample] =
          internalAmount * internalEnvelope + mu * externalEnvelope;
    }
  } else {
    writeMainSource(mainChannels, numMainChannels, monoKey, numSamples, source);
  }

  if (source == Source::Right && numMainChannels < 2) {
    status_.channelMismatch = true;
    status_.lowConfidence = true;
  } else if ((source == Source::StereoMax || source == Source::StereoAverage ||
              source == Source::Side) &&
             numMainChannels < 2) {
    status_.channelMismatch = true;
    status_.lowConfidence = true;
  }

  if (status_.channelMismatch)
    status_.lowConfidence = true;

  lastKeySample_ = monoKey[numSamples - 1];
}

void KeySourceRouter::blendEnvelopes(const float *internalEnvelope,
                                     const float *externalEnvelope,
                                     float *keyEnvelope, int numSamples,
                                     float externalBlend) noexcept {
  if (internalEnvelope == nullptr || externalEnvelope == nullptr ||
      keyEnvelope == nullptr || numSamples <= 0)
    return;

  const float mu = clampBlend(externalBlend);
  const float internalAmount = 1.0f - mu;
  for (int sample = 0; sample < numSamples; ++sample)
    keyEnvelope[sample] = internalAmount * internalEnvelope[sample] +
                          mu * externalEnvelope[sample];
}

float KeySourceRouter::clampBlend(float blend) noexcept {
  if (!std::isfinite(blend))
    return 0.0f;
  if (blend < 0.0f)
    return 0.0f;
  if (blend > 1.0f)
    return 1.0f;
  return blend;
}

bool KeySourceRouter::hasUsableChannelArray(const float *const *channels,
                                            int numChannels) noexcept {
  return countUsableChannels(channels, numChannels) > 0;
}

int KeySourceRouter::countUsableChannels(const float *const *channels,
                                         int numChannels) noexcept {
  if (channels == nullptr || numChannels <= 0)
    return 0;

  int usableChannels = 0;
  for (int channel = 0; channel < numChannels; ++channel) {
    if (channels[channel] != nullptr)
      ++usableChannels;
  }
  return usableChannels;
}

bool KeySourceRouter::isSilent(const float *const *channels, int numChannels,
                               int numSamples) noexcept {
  for (int channel = 0; channel < numChannels; ++channel) {
    const float *data = channels[channel];
    if (data == nullptr)
      continue;

    for (int sample = 0; sample < numSamples; ++sample) {
      const float magnitude = absolute(data[sample]);
      if (std::isfinite(magnitude) && magnitude > silenceThreshold)
        return false;
    }
  }
  return true;
}

float KeySourceRouter::averageSample(const float *const *channels,
                                     int numChannels, int sample) noexcept {
  float sum = 0.0f;
  int validChannels = 0;
  for (int channel = 0; channel < numChannels; ++channel) {
    if (channels[channel] != nullptr) {
      sum += channels[channel][sample];
      ++validChannels;
    }
  }
  return validChannels > 0 ? sum / static_cast<float>(validChannels) : 0.0f;
}

float KeySourceRouter::averageEnvelope(const float *const *channels,
                                       int numChannels, int sample) noexcept {
  float sum = 0.0f;
  int validChannels = 0;
  for (int channel = 0; channel < numChannels; ++channel) {
    if (channels[channel] != nullptr) {
      sum += absolute(channels[channel][sample]);
      ++validChannels;
    }
  }
  return validChannels > 0 ? sum / static_cast<float>(validChannels) : 0.0f;
}

void KeySourceRouter::writeMainSource(const float *const *mainChannels,
                                      int numMainChannels, float *monoKey,
                                      int numSamples, Source source) noexcept {
  const float *left = numMainChannels > 0 && mainChannels[0] != nullptr
                          ? mainChannels[0]
                          : nullptr;
  const float *right = numMainChannels > 1 && mainChannels[1] != nullptr
                           ? mainChannels[1]
                           : nullptr;

  for (int sample = 0; sample < numSamples; ++sample) {
    const float leftSample = left != nullptr ? left[sample] : 0.0f;
    const float rightSample = right != nullptr ? right[sample] : leftSample;

    switch (source) {
    case Source::Left:
      monoKey[sample] = leftSample;
      break;
    case Source::Right:
      monoKey[sample] = rightSample;
      break;
    case Source::StereoMax:
      monoKey[sample] = absolute(leftSample) >= absolute(rightSample)
                            ? absolute(leftSample)
                            : absolute(rightSample);
      break;
    case Source::StereoAverage:
      monoKey[sample] = 0.5f * (absolute(leftSample) + absolute(rightSample));
      break;
    case Source::Mid:
      monoKey[sample] = 0.5f * (leftSample + rightSample);
      break;
    case Source::Side:
      monoKey[sample] = 0.5f * (leftSample - rightSample);
      break;
    case Source::Main:
    default:
      monoKey[sample] = averageSample(mainChannels, numMainChannels, sample);
      break;
    }
  }
}

void KeySourceRouter::applyUnavailableExternalFallback(
    const float *const *mainChannels, int numMainChannels, float *monoKey,
    int numSamples, FallbackMode fallbackMode) noexcept {
  switch (fallbackMode) {
  case FallbackMode::InternalMain:
    if (hasUsableChannelArray(mainChannels, numMainChannels)) {
      writeMainSource(mainChannels, numMainChannels, monoKey, numSamples,
                      Source::Main);
    } else {
      for (int sample = 0; sample < numSamples; ++sample)
        monoKey[sample] = 0.0f;
    }
    break;
  case FallbackMode::HoldState:
    for (int sample = 0; sample < numSamples; ++sample)
      monoKey[sample] = lastKeySample_;
    break;
  case FallbackMode::CloseGate:
    for (int sample = 0; sample < numSamples; ++sample)
      monoKey[sample] = 0.0f;
    break;
  }

  lastKeySample_ = monoKey[numSamples - 1];
}

} // namespace ChugGate::DSP
