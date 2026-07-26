#pragma once

#include <atomic>

namespace ChugGate::DSP {

enum class FilterSlope {
  Db12,
  Db24,
  dB12 = Db12,
  dB24 = Db24,
  Slope12 = Db12,
  Slope24 = Db24
};
enum class EmphasisMode { Off, Bell, BandPass, Bandpass = BandPass };
enum class KeyListenMode {
  RawKey,
  ConditionedKey,
  AlignedKey,
  TransientKey,
  SustainKey,
  Raw = RawKey,
  Conditioned = ConditionedKey,
  Aligned = AlignedKey,
  Transient = TransientKey,
  Sustain = SustainKey
};

class KeyConditioner {
public:
  using FilterSlope = ChugGate::DSP::FilterSlope;
  using EmphasisMode = ChugGate::DSP::EmphasisMode;
  using KeyListenMode = ChugGate::DSP::KeyListenMode;
  using Slope = ChugGate::DSP::FilterSlope;
  using Emphasis = ChugGate::DSP::EmphasisMode;
  using ListenMode = ChugGate::DSP::KeyListenMode;

  void prepare(double sampleRate, int maxBlockSize = 0) noexcept;
  void reset() noexcept;

  void setHighPassEnabled(bool enabled) noexcept;
  void setHighPassFrequency(float frequencyHz) noexcept;
  void setHighPassSlope(FilterSlope slope) noexcept;
  void setLowPassEnabled(bool enabled) noexcept;
  void setLowPassFrequency(float frequencyHz) noexcept;
  void setLowPassSlope(FilterSlope slope) noexcept;
  void setHpf(bool enabled, float frequencyHz, FilterSlope slope) noexcept;
  void setLpf(bool enabled, float frequencyHz, FilterSlope slope) noexcept;

  // Short aliases match the persisted HPF/LPF parameter terminology.
  void setHPFEnabled(bool enabled) noexcept { setHighPassEnabled(enabled); }
  void setHPFFrequency(float frequencyHz) noexcept {
    setHighPassFrequency(frequencyHz);
  }
  void setHPFSlope(FilterSlope slope) noexcept { setHighPassSlope(slope); }
  void setLPFEnabled(bool enabled) noexcept { setLowPassEnabled(enabled); }
  void setLPFFrequency(float frequencyHz) noexcept {
    setLowPassFrequency(frequencyHz);
  }
  void setLPFSlope(FilterSlope slope) noexcept { setLowPassSlope(slope); }

  void setEmphasisMode(EmphasisMode mode) noexcept;
  void setEmphasisEnabled(bool enabled) noexcept;
  void setEmphasisFrequency(float frequencyHz) noexcept;
  void setEmphasisQ(float q) noexcept;
  void setEmphasisBandwidth(float q) noexcept { setEmphasisQ(q); }
  void setEmphasisGain(float gainDb) noexcept;
  void setEmphasis(EmphasisMode mode, float frequencyHz, float q, float gainDb,
                   bool soloKeyPath = false) noexcept;

  void setSoloKeyPath(bool solo) noexcept {
    soloKeyPath_.store(solo, std::memory_order_relaxed);
  }
  void setKeyListenMode(KeyListenMode mode) noexcept {
    listenMode_.store(mode, std::memory_order_relaxed);
  }
  void setListenMode(KeyListenMode mode) noexcept { setKeyListenMode(mode); }

  [[nodiscard]] bool isSoloKeyPath() const noexcept {
    return soloKeyPath_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] KeyListenMode getKeyListenMode() const noexcept {
    return listenMode_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] KeyListenMode getListenMode() const noexcept {
    return getKeyListenMode();
  }

  // The input and output may alias. Buffers are caller-owned.
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

  void markCoefficientsDirty() noexcept {
    coefficientsDirty_.store(true, std::memory_order_release);
  }
  void updateCoefficients() noexcept;
  static void makeHighPass(Biquad &filter, double sampleRate,
                           float frequencyHz) noexcept;
  static void makeLowPass(Biquad &filter, double sampleRate,
                          float frequencyHz) noexcept;
  static void makeBell(Biquad &filter, double sampleRate, float frequencyHz,
                       float q, float gainDb) noexcept;
  static void makeBandPass(Biquad &filter, double sampleRate,
                           float frequencyHz, float q,
                           float gainDb) noexcept;
  static void normalise(Biquad &filter, double b0, double b1, double b2,
                        double a0, double a1, double a2) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  Biquad highPass_[2]{};
  Biquad lowPass_[2]{};
  Biquad emphasis_{};

  std::atomic<bool> highPassEnabled_{true};
  std::atomic<float> highPassFrequency_{60.0f};
  std::atomic<FilterSlope> highPassSlope_{FilterSlope::Db12};
  std::atomic<bool> lowPassEnabled_{false};
  std::atomic<float> lowPassFrequency_{20000.0f};
  std::atomic<FilterSlope> lowPassSlope_{FilterSlope::Db12};
  std::atomic<EmphasisMode> emphasisMode_{EmphasisMode::Off};
  std::atomic<float> emphasisFrequency_{1000.0f};
  std::atomic<float> emphasisQ_{0.70710678f};
  std::atomic<float> emphasisGain_{0.0f};
  std::atomic<bool> soloKeyPath_{false};
  std::atomic<KeyListenMode> listenMode_{KeyListenMode::ConditionedKey};
  std::atomic<bool> coefficientsDirty_{true};
};

} // namespace ChugGate::DSP
