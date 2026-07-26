#pragma once

#include <cstddef>
#include <vector>

namespace ChugGate::DSP {

// Mono delay for key-path alignment. Positive values are applied to this
// stream. Negative values are retained as alignment requests but process as
// zero delay: the caller must realize them by delaying the main path and
// reporting that latency to the host.
class FractionalDelay {
public:
  static constexpr float defaultOperationalDelayMs = 25.0f;
  static constexpr float maximumExtendedDelayMs = 100.0f;

  FractionalDelay() noexcept = default;

  // This is the only allocating operation. Pass 100 ms to enable the full
  // extended alignment range; the default configures the normal +/-25 ms
  // operational range.
  void prepare(double sampleRate, float maxDelayMs = defaultOperationalDelayMs);
  void reset() noexcept;

  void setDelayMs(float delayMs) noexcept;
  void setDelaySamples(double fractionalSamples) noexcept;

  [[nodiscard]] float processSample(float input) noexcept;
  void process(const float *input, float *output, int numSamples) noexcept;
  void process(float *samples, int numSamples) noexcept {
    process(samples, samples, numSamples);
  }
  void processBlock(const float *input, float *output,
                    int numSamples) noexcept {
    process(input, output, numSamples);
  }
  void processBlock(float *samples, int numSamples) noexcept {
    process(samples, numSamples);
  }

  [[nodiscard]] double getDelaySamples() const noexcept {
    return requestedDelaySamples_;
  }
  [[nodiscard]] float getDelayMs() const noexcept;
  [[nodiscard]] double getAppliedDelaySamples() const noexcept {
    return appliedDelaySamples_;
  }
  [[nodiscard]] float getMaximumDelayMs() const noexcept {
    return maximumDelayMs_;
  }
  [[nodiscard]] bool needsMainLookahead() const noexcept {
    return requestedDelaySamples_ < 0.0;
  }
  [[nodiscard]] bool isPrepared() const noexcept {
    return sampleRate_ > 0.0 && !buffer_.empty();
  }

private:
  [[nodiscard]] float sampleAtDelay(std::size_t delay) const noexcept;

  std::vector<float> buffer_;
  std::size_t writeIndex_ = 0;
  double sampleRate_ = 0.0;
  float maximumDelayMs_ = 0.0f;
  double maximumDelaySamples_ = 0.0;
  double requestedDelaySamples_ = 0.0;
  double appliedDelaySamples_ = 0.0;
};

} // namespace ChugGate::DSP
