#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace ChugGate::DSP {

// Integer main-path delay used to give the detector a bounded lookahead.
// Storage is fixed for the product's mono/stereo layouts and maximum supported
// sample rate, so prepare(), reset(), and process() never resize it.
class LookaheadDelay {
public:
  enum class Lookahead {
    Off,
    Ms0_25,
    Ms0_5,
    Ms1,
    Ms2,
    Ms5,
    Ms10
  };
  using LookaheadEnum = Lookahead;

  static constexpr int maximumChannels = 2;
  static constexpr double maximumSupportedSampleRate = 192000.0;
  static constexpr float maximumLookaheadMs = 10.0f;
  static constexpr float transitionTimeMs = 5.0f;

  LookaheadDelay() noexcept = default;

  void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;
  void reset() noexcept;

  // Non-finite values select Off. Other values snap to the nearest member of
  // { 0, 0.25, 0.5, 1, 2, 5, 10 } ms.
  void setLookaheadMs(float lookaheadMs) noexcept;
  void setLookaheadEnum(Lookahead lookahead) noexcept;

  [[nodiscard]] float getLookaheadMs() const noexcept;
  [[nodiscard]] Lookahead getLookaheadEnum() const noexcept;

  // This is the exact N_L = round(tau_L * fs) for the selected setting. Host
  // latency reporting is intentionally owned by the processor, not this unit.
  [[nodiscard]] int getLatencySamples() const noexcept;

  // Planar caller-owned buffers. Input and output may alias.
  void process(const float *const *input, float *const *output, int numChannels,
               int numSamples) noexcept;
  void process(float *const *channels, int numChannels,
               int numSamples) noexcept;
  void processBlock(const float *const *input, float *const *output,
                    int numChannels, int numSamples) noexcept {
    process(input, output, numChannels, numSamples);
  }
  void processBlock(float *const *channels, int numChannels,
                    int numSamples) noexcept {
    process(channels, numChannels, numSamples);
  }

  template <typename AudioBufferType>
  void processBlock(AudioBufferType &buffer) noexcept {
    process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
            buffer.getNumSamples());
  }

  [[nodiscard]] bool isPrepared() const noexcept {
    return sampleRate_ > 0.0 && numChannels_ > 0;
  }

private:
  static constexpr std::size_t maximumDelaySamples =
      static_cast<std::size_t>(maximumSupportedSampleRate *
                               maximumLookaheadMs * 0.001);
  static constexpr std::size_t bufferCapacity = maximumDelaySamples + 1U;

  [[nodiscard]] static Lookahead snapToLookahead(float lookaheadMs) noexcept;
  [[nodiscard]] static float millisecondsFor(Lookahead lookahead) noexcept;
  [[nodiscard]] int samplesFor(Lookahead lookahead) const noexcept;
  [[nodiscard]] float sampleAtDelay(int channel, int delaySamples) const
      noexcept;
  void beginTransitionIfNeeded() noexcept;

  std::array<std::array<float, bufferCapacity>, maximumChannels> buffer_{};
  std::size_t writeIndex_ = 0;
  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  int numChannels_ = 0;

  std::atomic<Lookahead> requestedLookahead_{Lookahead::Off};
  int sourceDelaySamples_ = 0;
  int destinationDelaySamples_ = 0;
  int transitionLengthSamples_ = 1;
  int transitionPosition_ = 1;
};

} // namespace ChugGate::DSP
