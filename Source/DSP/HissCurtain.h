#pragma once

#include <atomic>
#include <vector>

namespace ChugGate::DSP {

/**
 * SNR-driven, modulation-stable low-pass for progressively removing hiss.
 *
 * The externally supplied high-band SNR and the SNR range are expressed in
 * decibels. The target cutoff follows the specification exactly:
 *
 *   Q_H = clamp((SNR_H - S_min) / (S_max - S_min), 0, 1)
 *   ln(f_c) = ln(f_min) + Q_H * (ln(f_max) - ln(f_min))
 *
 * Two cascaded 12 dB/oct Butterworth TPT-SVF sections provide a 24 dB/oct
 * low-pass. The cutoff is smoothed in log-frequency once per sample and is
 * shared by every channel, preventing stereo image modulation. Depth is a
 * smoothed dry/filtered blend: zero is dry and one is fully filtered.
 */
class HissCurtain {
public:
  static constexpr float minimumMinFreqHz = 500.0f;
  static constexpr float maximumMinFreqHz = 8000.0f;
  static constexpr float minimumMaxFreqHz = 3000.0f;
  static constexpr float maximumMaxFreqHz = 20000.0f;

  // Allocates all per-channel state. process() allocates nothing.
  void prepare(double sampleRate, int maxBlockSize, int numChannels);
  void reset() noexcept;

  void setMinFreqHz(float frequencyHz) noexcept;
  void setMaxFreqHz(float frequencyHz) noexcept;
  void setDepth(float depth) noexcept;

  // S_min and S_max are high-band signal-to-noise ratios in decibels.
  void setSnrRange(float minimumSnrDb, float maximumSnrDb) noexcept;

  // Sets the externally measured high-band signal-to-noise ratio in decibels.
  void setSnr(float snrDb) noexcept;
  void setSnrDb(float snrDb) noexcept { setSnr(snrDb); }

  // Returns the current (smoothed) cutoff used for coefficient generation.
  [[nodiscard]] float getCutoffHz() const noexcept;

  // Planar caller-owned buffers. Input and output channel pointers may alias.
  // Processing is limited to the channel count supplied to prepare().
  void process(const float *const *input, float *const *output, int numChannels,
               int numSamples) noexcept;

  template <typename AudioBufferType>
  void processBlock(AudioBufferType &buffer) noexcept {
    process(buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(),
            buffer.getNumChannels(), buffer.getNumSamples());
  }

  template <typename InputBufferType, typename OutputBufferType>
  void processBlock(const InputBufferType &input,
                    OutputBufferType &output) noexcept {
    process(input.getArrayOfReadPointers(), output.getArrayOfWritePointers(),
            input.getNumChannels(), input.getNumSamples());
  }

private:
  struct TptLowPass {
    float integrator1 = 0.0f;
    float integrator2 = 0.0f;

    float process(float input, float g) noexcept;
    void reset() noexcept;
  };

  struct ChannelState {
    TptLowPass stage1{};
    TptLowPass stage2{};

    void reset() noexcept;
  };

  static float finiteOr(float value, float fallback) noexcept;
  static void protectFrequencyOrdering(float &minimumHz,
                                       float &maximumHz) noexcept;
  static void protectSnrOrdering(float &minimumDb, float &maximumDb) noexcept;
  static float mapCutoffHz(float snrDb, float minimumSnrDb, float maximumSnrDb,
                           float minimumHz, float maximumHz) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  int numChannels_ = 0;
  std::vector<ChannelState> channels_{};

  float smoothedLogCutoff_ = 0.0f;
  float cutoffSmoothingFactor_ = 1.0f;
  float smoothedDepth_ = 1.0f;
  float depthSmoothingFactor_ = 1.0f;

  std::atomic<float> minimumHz_{2000.0f};
  std::atomic<float> maximumHz_{16000.0f};
  std::atomic<float> depth_{1.0f};
  std::atomic<float> minimumSnrDb_{0.0f};
  std::atomic<float> maximumSnrDb_{30.0f};
  std::atomic<float> snrDb_{30.0f};
  std::atomic<float> currentCutoffHz_{16000.0f};
};

} // namespace ChugGate::DSP
