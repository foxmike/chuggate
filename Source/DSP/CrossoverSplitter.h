#pragma once

#include <atomic>
#include <vector>

namespace ChugGate::DSP {

enum class CrossoverMode { Bypass, TwoWay, ThreeWay };

class CrossoverSplitter {
public:
  using Mode = CrossoverMode;

  static constexpr float dualMinimumHz = 1500.0f;
  static constexpr float dualMaximumHz = 8000.0f;
  static constexpr float triLowMinimumHz = 80.0f;
  static constexpr float triLowMaximumHz = 900.0f;
  static constexpr float triHighMinimumHz = 1500.0f;
  static constexpr float triHighMaximumHz = 10000.0f;
  static constexpr float minimumCrossoverGapHz = 100.0f;

  // Allocates all per-channel state. process()/processBlock() allocate nothing.
  void prepare(double sampleRate, int maxBlockSize, int numChannels);
  void reset() noexcept;

  void setMode(Mode mode) noexcept;
  [[nodiscard]] Mode getMode() const noexcept;
  [[nodiscard]] int getNumBands() const noexcept;

  void setCrossoverHz(float dualHz) noexcept;
  void setCrossoverHz(float lowHz, float highHz) noexcept;
  void setDualCrossoverHz(float frequencyHz) noexcept;
  void setTriCrossoversHz(float lowHz, float highHz) noexcept;
  void setTriLowCrossoverHz(float frequencyHz) noexcept;
  void setTriHighCrossoverHz(float frequencyHz) noexcept;

  [[nodiscard]] float getDualCrossoverHz() const noexcept;
  [[nodiscard]] float getTriLowCrossoverHz() const noexcept;
  [[nodiscard]] float getTriHighCrossoverHz() const noexcept;

  // Planar caller-owned buffers. Each array contains one pointer per channel.
  // Any output array or channel pointer may be null when that band is unused.
  // Input may alias an output because each input sample is read before writing.
  //
  // ThreeWay uses a complementary tree: split at tri-low, split that high
  // branch at tri-high, and pass the low branch through a matching tri-high
  // LR4 all-pass (LP4 + HP4). Consequently the unity-gain sum is a cascade of
  // two all-pass responses. The fixed-cutoff reconstruction target is within
  // +/-0.01 dB after settling at normal audio sample rates; it is phase-shifted
  // and deliberately not sample-identical to the dry input.
  void process(const float *const *input, float *const *band0,
               float *const *band1, float *const *band2, int numChannels,
               int numSamples) noexcept;
  void processBlock(const float *const *input, float *const *band0,
                    float *const *band1, float *const *band2, int numChannels,
                    int numSamples) noexcept {
    process(input, band0, band1, band2, numChannels, numSamples);
  }

  // Convenience overload for pre-sized AudioBuffer-like types (including
  // juce::AudioBuffer<float>) that expose JUCE's pointer-array accessors.
  template <typename AudioBufferType>
  void processBlock(const AudioBufferType &input, AudioBufferType &band0,
                    AudioBufferType &band1,
                    AudioBufferType &band2) noexcept {
    process(input.getArrayOfReadPointers(), band0.getArrayOfWritePointers(),
            band1.getArrayOfWritePointers(), band2.getArrayOfWritePointers(),
            input.getNumChannels(), input.getNumSamples());
  }

private:
  struct Coefficients {
    float lowB0 = 1.0f;
    float lowB1 = 0.0f;
    float lowB2 = 0.0f;
    float highB0 = 1.0f;
    float highB1 = 0.0f;
    float highB2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
  };

  struct BiquadState {
    float z1 = 0.0f;
    float z2 = 0.0f;

    float process(float input, float b0, float b1, float b2,
                  const Coefficients &coefficients) noexcept;
    void reset() noexcept;
  };

  struct LR4Pair {
    BiquadState low[2]{};
    BiquadState high[2]{};

    void process(float input, const Coefficients &coefficients, float &lowOut,
                 float &highOut) noexcept;
    void reset() noexcept;
  };

  struct ChannelState {
    LR4Pair dual{};
    LR4Pair triLow{};
    LR4Pair triHighUpper{};
    LR4Pair triHighLowCompensation{};

    void reset() noexcept;
  };

  struct SmoothedCutoff {
    float currentHz = 0.0f;
    float smoothingFactor = 1.0f;

    void prepare(double sampleRate, float initialHz) noexcept;
    float next(float targetHz) noexcept;
  };

  static float finiteOr(float value, float fallback) noexcept;
  static float clampDual(float frequencyHz) noexcept;
  static void clampTri(float &lowHz, float &highHz) noexcept;
  static Coefficients makeButterworth(double sampleRate,
                                      float frequencyHz) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  int numChannels_ = 0;
  std::vector<ChannelState> channels_{};

  SmoothedCutoff dualCutoff_{};
  SmoothedCutoff triLowCutoff_{};
  SmoothedCutoff triHighCutoff_{};

  std::atomic<Mode> mode_{Mode::Bypass};
  std::atomic<float> dualHz_{4000.0f};
  std::atomic<float> triLowHz_{350.0f};
  std::atomic<float> triHighHz_{4000.0f};
};

} // namespace ChugGate::DSP
