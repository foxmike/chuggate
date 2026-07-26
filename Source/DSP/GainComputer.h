#pragma once

namespace ChugGate::DSP {

class GainComputer {
public:
  enum class Mode {
    HardGate,
    SoftGate,
    DownwardExpander,
    Hard = HardGate,
    Soft = SoftGate,
    Expander = DownwardExpander
  };

  struct Input {
    float detectorLevelDb = -100.0f;
    float thresholdDb = -40.0f;
    float ratio = 2.0f;
    float floorDb = -80.0f;
    float gamma = 1.0f;

    // Width below thresholdDb used when no performance-engine certainty is
    // supplied. This is the T_close..T_open region from section 7.3.
    float softKneeDb = 6.0f;

    // A negative value means "not supplied". PerformanceStateMachine::Output
    // can provide openCertainty directly here.
    float openCertainty = -1.0f;

    // Hard gating should normally follow the performance engine's discrete
    // decision rather than make another threshold decision.
    bool stateHintIsOpen = false;
    bool hasStateHint = false;
  };

  GainComputer() noexcept = default;
  explicit GainComputer(Mode mode) noexcept : mode_(mode) {}

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  void setMode(Mode mode) noexcept { mode_ = mode; }
  [[nodiscard]] Mode getMode() const noexcept { return mode_; }

  [[nodiscard]] float processSample(const Input &input) const noexcept;
  [[nodiscard]] float compute(const Input &input) const noexcept {
    return processSample(input);
  }

  // All buffers are caller-owned and may alias.
  void process(const Input *input, float *targetGain,
               int numSamples) const noexcept;
  void processBlock(const Input *input, float *targetGain,
                    int numSamples) const noexcept {
    process(input, targetGain, numSamples);
  }

  [[nodiscard]] static float floorGain(float floorDb) noexcept;
  [[nodiscard]] static float shape(float amount, float floorLinear,
                                   float gamma) noexcept;
  [[nodiscard]] static float
  softDecision(float detectorLevelDb, float thresholdDb, float kneeDb) noexcept;
  [[nodiscard]] static float downwardExpanderGainDb(float detectorLevelDb,
                                                    float thresholdDb,
                                                    float ratio,
                                                    float floorDb) noexcept;

private:
  [[nodiscard]] static float finiteOr(float value, float fallback) noexcept;

  Mode mode_ = Mode::HardGate;
};

} // namespace ChugGate::DSP
