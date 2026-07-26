#include "LookaheadDelay.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {

constexpr std::array<float, 7> legalLookaheadMs{
    0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};

} // namespace

void LookaheadDelay::prepare(double sampleRate, int maxBlockSize,
                             int numChannels) noexcept {
  sampleRate_ =
      std::isfinite(sampleRate) && sampleRate > 0.0
          ? std::min(sampleRate, maximumSupportedSampleRate)
          : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  numChannels_ = std::clamp(numChannels, 0, maximumChannels);
  transitionLengthSamples_ =
      sampleRate_ > 0.0
          ? std::max(1, static_cast<int>(std::lround(
                            transitionTimeMs * 0.001 * sampleRate_)))
          : 1;

  sourceDelaySamples_ = samplesFor(
      requestedLookahead_.load(std::memory_order_relaxed));
  destinationDelaySamples_ = sourceDelaySamples_;
  transitionPosition_ = transitionLengthSamples_;
  reset();
}

void LookaheadDelay::reset() noexcept {
  for (auto &channel : buffer_)
    channel.fill(0.0f);
  writeIndex_ = 0;

  sourceDelaySamples_ = samplesFor(
      requestedLookahead_.load(std::memory_order_relaxed));
  destinationDelaySamples_ = sourceDelaySamples_;
  transitionPosition_ = transitionLengthSamples_;
}

void LookaheadDelay::setLookaheadMs(float lookaheadMs) noexcept {
  setLookaheadEnum(snapToLookahead(lookaheadMs));
}

void LookaheadDelay::setLookaheadEnum(Lookahead lookahead) noexcept {
  const auto ordinal = static_cast<int>(lookahead);
  if (ordinal < static_cast<int>(Lookahead::Off) ||
      ordinal > static_cast<int>(Lookahead::Ms10))
    lookahead = Lookahead::Off;

  requestedLookahead_.store(lookahead, std::memory_order_relaxed);
}

float LookaheadDelay::getLookaheadMs() const noexcept {
  return millisecondsFor(getLookaheadEnum());
}

LookaheadDelay::Lookahead LookaheadDelay::getLookaheadEnum() const noexcept {
  return requestedLookahead_.load(std::memory_order_relaxed);
}

int LookaheadDelay::getLatencySamples() const noexcept {
  return samplesFor(getLookaheadEnum());
}

void LookaheadDelay::process(const float *const *input, float *const *output,
                             int numChannels, int numSamples) noexcept {
  if (input == nullptr || output == nullptr || numSamples <= 0 ||
      numChannels <= 0)
    return;

  const int channelsToDelay =
      std::clamp(numChannels, 0, numChannels_);

  for (int sample = 0; sample < numSamples; ++sample) {
    beginTransitionIfNeeded();

    const bool transitioning =
        transitionPosition_ < transitionLengthSamples_;
    const float mix =
        transitioning
            ? static_cast<float>(transitionPosition_) /
                  static_cast<float>(transitionLengthSamples_)
            : 1.0f;

    for (int channel = 0; channel < channelsToDelay; ++channel) {
      const float *inputChannel = input[channel];
      float *outputChannel = output[channel];
      if (inputChannel == nullptr || outputChannel == nullptr)
        continue;

      const float inputSample = inputChannel[sample];
      buffer_[static_cast<std::size_t>(channel)][writeIndex_] = inputSample;

      const float destination =
          sampleAtDelay(channel, destinationDelaySamples_);
      float delayed = destination;
      if (transitioning) {
        const float source = sampleAtDelay(channel, sourceDelaySamples_);
        delayed = source + mix * (destination - source);
      }

      outputChannel[sample] = delayed;
    }

    // Prepared channels absent from this call still advance through silence,
    // keeping every channel's tap aligned to the shared delay length.
    for (int channel = channelsToDelay; channel < numChannels_; ++channel)
      buffer_[static_cast<std::size_t>(channel)][writeIndex_] = 0.0f;

    writeIndex_ = (writeIndex_ + 1U) % bufferCapacity;
    if (transitioning) {
      ++transitionPosition_;
      if (transitionPosition_ >= transitionLengthSamples_) {
        sourceDelaySamples_ = destinationDelaySamples_;
      }
    }
  }

  // Gracefully pass through channels beyond the prepared mono/stereo layout.
  for (int channel = channelsToDelay; channel < numChannels; ++channel) {
    const float *inputChannel = input[channel];
    float *outputChannel = output[channel];
    if (inputChannel == nullptr || outputChannel == nullptr ||
        inputChannel == outputChannel)
      continue;
    std::copy_n(inputChannel, numSamples, outputChannel);
  }
}

void LookaheadDelay::process(float *const *channels, int numChannels,
                             int numSamples) noexcept {
  process(const_cast<const float *const *>(channels), channels, numChannels,
          numSamples);
}

LookaheadDelay::Lookahead
LookaheadDelay::snapToLookahead(float lookaheadMs) noexcept {
  if (!std::isfinite(lookaheadMs))
    return Lookahead::Off;

  auto nearest = std::size_t{0};
  float nearestDistance = std::abs(lookaheadMs - legalLookaheadMs[0]);
  for (std::size_t index = 1; index < legalLookaheadMs.size(); ++index) {
    const float distance = std::abs(lookaheadMs - legalLookaheadMs[index]);
    if (distance < nearestDistance) {
      nearest = index;
      nearestDistance = distance;
    }
  }

  return static_cast<Lookahead>(nearest);
}

float LookaheadDelay::millisecondsFor(Lookahead lookahead) noexcept {
  const auto ordinal = static_cast<std::size_t>(lookahead);
  return ordinal < legalLookaheadMs.size() ? legalLookaheadMs[ordinal] : 0.0f;
}

int LookaheadDelay::samplesFor(Lookahead lookahead) const noexcept {
  if (sampleRate_ <= 0.0)
    return 0;

  return static_cast<int>(std::lround(
      static_cast<double>(millisecondsFor(lookahead)) * 0.001 * sampleRate_));
}

float LookaheadDelay::sampleAtDelay(int channel, int delaySamples) const
    noexcept {
  const auto delay = static_cast<std::size_t>(
      std::clamp(delaySamples, 0, static_cast<int>(maximumDelaySamples)));
  const auto readIndex =
      (writeIndex_ + bufferCapacity - (delay % bufferCapacity)) %
      bufferCapacity;
  return buffer_[static_cast<std::size_t>(channel)][readIndex];
}

void LookaheadDelay::beginTransitionIfNeeded() noexcept {
  const Lookahead requested =
      requestedLookahead_.load(std::memory_order_relaxed);
  const int requestedSamples = samplesFor(requested);
  if (requestedSamples == destinationDelaySamples_)
    return;

  // A tap change is rendered as a short equal-time crossfade. If automation
  // retargets during that fade, continue from its currently dominant tap;
  // this bounds the discontinuity and starts another 5 ms fade immediately.
  if (transitionPosition_ < transitionLengthSamples_ &&
      transitionPosition_ * 2 < transitionLengthSamples_)
    destinationDelaySamples_ = sourceDelaySamples_;

  sourceDelaySamples_ = destinationDelaySamples_;
  destinationDelaySamples_ = requestedSamples;
  transitionPosition_ = 0;
}

} // namespace ChugGate::DSP
