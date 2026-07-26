#pragma once

#include <atomic>
#include <cstdint>

namespace ChugGate::DSP {

class AttackGuard {
public:
  struct Output {
    // A one-sample event emitted only after the candidate transient has been
    // confirmed by body energy.
    bool validAttack = false;
    float transientStrength = 0.0f;
    // Positive dB delta which may be subtracted from an opening threshold.
    float thresholdReductionDb = 0.0f;
    // Detector level plus thresholdReductionDb, in dB.
    float openAssistScore = -100.0f;
  };

  AttackGuard() noexcept = default;

  void prepare(double sampleRate, int maxBlockSize = 0) noexcept;
  void reset() noexcept;

  [[nodiscard]] Output processSample(float monoKey,
                                     float detectorLevelDb) noexcept;

  // All buffers are caller-owned. output may be null.
  void process(const float *monoKey, const float *detectorLevelDb,
               Output *output, int numSamples) noexcept;
  void processBlock(const float *monoKey, const float *detectorLevelDb,
                    Output *output, int numSamples) noexcept {
    process(monoKey, detectorLevelDb, output, numSamples);
  }

  void setEnabled(bool enabled) noexcept;
  void setAmount(float amount) noexcept;
  void setSensitivity(float sensitivity) noexcept;
  void setFrequencyEmphasis(float frequencyHz) noexcept;
  void setDecay(float decayMs) noexcept;
  void setSoftNoteAssist(float assist) noexcept;
  void setMaxThresholdReduction(float reductionDb) noexcept;
  void setConfirmationWindow(float windowMs) noexcept;

  // Convenience aliases for parameter names which carry their unit.
  void setFrequency(float frequencyHz) noexcept {
    setFrequencyEmphasis(frequencyHz);
  }
  void setDecayMs(float decayMs) noexcept { setDecay(decayMs); }
  void setMaxThresholdReductionDb(float reductionDb) noexcept {
    setMaxThresholdReduction(reductionDb);
  }
  void setConfirmationWindowMs(float windowMs) noexcept {
    setConfirmationWindow(windowMs);
  }

  [[nodiscard]] bool isValidAttack() const noexcept {
    return lastOutput_.validAttack;
  }
  [[nodiscard]] float getTransientStrength() const noexcept {
    return lastOutput_.transientStrength;
  }
  [[nodiscard]] float getThresholdReductionDb() const noexcept {
    return lastOutput_.thresholdReductionDb;
  }
  [[nodiscard]] float getOpenAssistScore() const noexcept {
    return lastOutput_.openAssistScore;
  }

private:
  void updateCoefficientsIfNeeded() noexcept;
  void beginCandidate() noexcept;
  void accumulateBody(float sample) noexcept;
  [[nodiscard]] bool candidateHasBody() const noexcept;
  [[nodiscard]] float calculateTransientStrength(float envelope) const noexcept;
  [[nodiscard]] static float finiteOr(float value, float fallback) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;

  std::atomic<bool> enabled_{true};
  std::atomic<float> amount_{0.5f};
  std::atomic<float> sensitivity_{0.5f};
  std::atomic<float> frequencyHz_{1000.0f};
  std::atomic<float> decayMs_{8.0f};
  std::atomic<float> softNoteAssist_{0.5f};
  std::atomic<float> maxThresholdReductionDb_{12.0f};
  std::atomic<float> confirmationWindowMs_{4.0f};
  std::atomic<bool> coefficientsDirty_{true};

  float highPassCoefficient_ = 0.0f;
  float transientDecayCoefficient_ = 0.0f;
  std::uint32_t confirmationSamples_ = 1;

  float previousInput_ = 0.0f;
  float previousHighPass_ = 0.0f;
  float previousEmphasised_ = 0.0f;
  float transientEnvelope_ = 0.0f;

  bool candidateActive_ = false;
  bool candidateArmed_ = true;
  bool attackAssistActive_ = false;
  std::uint32_t candidateAge_ = 0;
  std::uint32_t samplesSinceCandidate_ = 0;
  float bodyEnergy_ = 0.0f;
  float bodyMean_ = 0.0f;
  float bodyM2_ = 0.0f;
  std::uint32_t bodySampleCount_ = 0;
  std::uint32_t bodyActiveSampleCount_ = 0;

  Output lastOutput_{};
};

} // namespace ChugGate::DSP
