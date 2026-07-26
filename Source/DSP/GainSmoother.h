#pragma once

#include <array>

namespace ChugGate::DSP {

class GainSmoother {
public:
  static constexpr int maxBands = 3;

  GainSmoother() noexcept = default;

  void prepare(double sampleRate, int numBands = 1) noexcept;
  void reset() noexcept;
  void reset(float gain) noexcept;

  [[nodiscard]] int getNumBands() const noexcept { return numBands_; }
  [[nodiscard]] float getGain(int band = 0) const noexcept;

  void setOpenTimeMs(float timeMs) noexcept;
  void setCloseTimeMs(float timeMs) noexcept;
  void setOpenTimeMs(int band, float timeMs) noexcept;
  void setCloseTimeMs(int band, float timeMs) noexcept;
  void setBandTimesMs(int band, float openTimeMs, float closeTimeMs) noexcept;

  [[nodiscard]] float processSample(float targetGain, int band = 0) noexcept;

  // Processes one gain target per configured band.
  void processFrame(const float *targetGains, float *smoothedGains) noexcept;

  // Buffers use sample-major interleaving:
  // [sample0-band0, sample0-band1, ..., sample1-band0, ...].
  // Input and output may alias and all storage is caller-owned.
  void process(const float *targetGains, float *smoothedGains,
               int numSamples) noexcept;
  void processBlock(const float *targetGains, float *smoothedGains,
                    int numSamples) noexcept {
    process(targetGains, smoothedGains, numSamples);
  }

private:
  [[nodiscard]] float calculateAlpha(float timeMs) const noexcept;
  void updateAlpha(int band) noexcept;
  [[nodiscard]] static float sanitiseTime(float timeMs) noexcept;
  [[nodiscard]] static float sanitiseGain(float gain) noexcept;

  double sampleRate_ = 0.0;
  int numBands_ = 1;
  std::array<float, maxBands> gain_{1.0f, 1.0f, 1.0f};
  std::array<float, maxBands> openTimeMs_{5.0f, 5.0f, 5.0f};
  std::array<float, maxBands> closeTimeMs_{100.0f, 100.0f, 100.0f};
  std::array<float, maxBands> openAlpha_{0.0f, 0.0f, 0.0f};
  std::array<float, maxBands> closeAlpha_{0.0f, 0.0f, 0.0f};
};

} // namespace ChugGate::DSP
