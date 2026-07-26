#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ChugGate::DSP {

enum class HumMode {
  Off,
  Hz50,
  Hz60,
  Auto,
  FiftyHz = Hz50,
  SixtyHz = Hz60
};

class PowerlineShield {
public:
  using Mode = ChugGate::DSP::HumMode;
  using HumMode = ChugGate::DSP::HumMode;

  void prepare(double sampleRate, int maxBlockSize = 0) noexcept;
  void reset() noexcept;

  void setHumMode(HumMode mode) noexcept;
  void setMode(HumMode mode) noexcept { setHumMode(mode); }
  void setHarmonics(int harmonicCount) noexcept;
  void setHarmonicCount(int harmonicCount) noexcept {
    setHarmonics(harmonicCount);
  }
  void setHumHarmonics(int harmonicCount) noexcept {
    setHarmonics(harmonicCount);
  }
  void setSecondHarmonicEnabled(bool enabled) noexcept;
  void setThirdHarmonicEnabled(bool enabled) noexcept;
  void setNotchQ(float q) noexcept;
  void setHumNotchQ(float q) noexcept { setNotchQ(q); }
  void setHumImmunityAmount(float eta) noexcept;

  [[nodiscard]] HumMode getHumMode() const noexcept {
    return requestedMode_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] HumMode getChosenMode() const noexcept {
    return chosenMode_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] HumMode getDetectedMode() const noexcept {
    return getChosenMode();
  }
  [[nodiscard]] float getAutoDetectConfidence() const noexcept {
    return confidence_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getConfidence() const noexcept {
    return getAutoDetectConfidence();
  }
  [[nodiscard]] float getHumConfidence() const noexcept {
    return getAutoDetectConfidence();
  }
  [[nodiscard]] float getDetectedHumHz() const noexcept {
    const HumMode mode = getChosenMode();
    return mode == HumMode::Hz50 ? 50.0f
                                : (mode == HumMode::Hz60 ? 60.0f : 0.0f);
  }
  [[nodiscard]] float getHumEnergy() const noexcept {
    return humEnergy_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getHumImmunityAmount() const noexcept {
    return humImmunityAmount_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] float estimateMusicalEnergy(float keyEnergy) const noexcept;
  [[nodiscard]] float applyHumImmunity(float keyEnergy,
                                       float humEnergy) const noexcept;
  [[nodiscard]] float computeMusicalEnergy(float keyEnergy,
                                           float humEnergy) const noexcept {
    return applyHumImmunity(keyEnergy, humEnergy);
  }
  [[nodiscard]] static float computeMusicalEnergy(float keyEnergy,
                                                  float humEnergy,
                                                  float eta) noexcept {
    return calculateMusicalEnergy(keyEnergy, humEnergy, eta);
  }
  [[nodiscard]] static float calculateMusicalEnergy(float keyEnergy,
                                                    float humEnergy,
                                                    float eta) noexcept;

  // Auto detection observes input before the key-only notch cascade.
  // The input and output may alias; all buffers are caller-owned.
  void process(float *monoKey, int numSamples) noexcept;
  void process(const float *input, float *output, int numSamples) noexcept;
  void processBlock(float *monoKey, int numSamples) noexcept {
    process(monoKey, numSamples);
  }
  void processBlock(const float *input, float *output, int numSamples) noexcept {
    process(input, output, numSamples);
  }

private:
  struct Biquad {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;

    float process(float input) noexcept;
    void reset() noexcept;
    void setBypass() noexcept;
  };

  struct EnergyTracker {
    double oscillatorReal = 1.0;
    double oscillatorImag = 0.0;
    double rotationReal = 1.0;
    double rotationImag = 0.0;
    double correlationReal = 0.0;
    double correlationImag = 0.0;

    void prepare(double frequency, double sampleRate) noexcept;
    void reset() noexcept;
    void push(float sample, double smoothing) noexcept;
    [[nodiscard]] float energy() const noexcept;
  };

  void markCoefficientsDirty() noexcept {
    coefficientsDirty_.store(true, std::memory_order_release);
  }
  void updateNotches(HumMode mode) noexcept;
  void updateDetection(const float *input, int numSamples) noexcept;
  static void makeNotch(Biquad &filter, double sampleRate, double frequency,
                        float q) noexcept;
  static float finiteOr(float value, float fallback) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  std::array<Biquad, 3> notches_{};
  // Track 50/100/150 Hz followed by 60/120/180 Hz.
  std::array<EnergyTracker, 6> trackers_{};
  std::uint64_t analysedSamples_ = 0;
  HumMode activeNotchMode_ = HumMode::Off;

  std::atomic<HumMode> requestedMode_{HumMode::Off};
  std::atomic<HumMode> chosenMode_{HumMode::Off};
  std::atomic<int> harmonicCount_{1};
  std::atomic<float> notchQ_{20.0f};
  std::atomic<float> humImmunityAmount_{1.0f};
  std::atomic<float> confidence_{0.0f};
  std::atomic<float> humEnergy_{0.0f};
  std::atomic<bool> coefficientsDirty_{true};
};

} // namespace ChugGate::DSP
