#pragma once

#include <atomic>
#include <cstdint>

namespace ChugGate::DSP {

class PerformanceStateMachine {
public:
  enum class State : std::uint8_t {
    Closed,
    Primed,
    Attack,
    Open,
    Hold,
    Sustain,
    Release,
    Reopen
  };

  enum class TransitionReason : std::uint8_t {
    None,
    Reset,
    OnsetEvidence,
    ThresholdReached,
    TransientValidated,
    PrimingCancelled,
    AttackComplete,
    LevelRecovered,
    BelowOpenThreshold,
    HoldExpired,
    HysteresisProtection,
    SustainProtection,
    SustainEnded,
    ReleaseComplete,
    ReopenTransient
  };

  enum Flag : std::uint32_t {
    NoFlags = 0,
    StateChanged = 1u << 0,
    AboveOpenThreshold = 1u << 1,
    AboveCloseThreshold = 1u << 2,
    OnsetDetected = 1u << 3,
    ValidTransient = 1u << 4,
    SustainProtected = 1u << 5,
    LookaheadPreOpen = 1u << 6,
    ShouldOpen = 1u << 7,
    OpenFamily = 1u << 8,
    OpeningPhase = 1u << 9,
    ClosingPhase = 1u << 10,
    HoldActive = 1u << 11,
    ReopenActive = 1u << 12
  };

  struct Evidence {
    // An unvalidated onset enters Primed. A valid transient can enter Attack,
    // or Reopen if the machine is already releasing.
    bool onset = false;
    bool validTransient = false;

    // Sustain Intelligence may supply a continuous confidence for consumers
    // and/or a discrete protection decision. The score does not replace the
    // sustain detector level used by hysteresis.
    float sustainScore = 0.0f;
    bool sustainProtection = false;

    // Phase D: Attack and Feedback integration
    bool validAttack = false;
    float thresholdReduction = 0.0f;
    bool feedbackActive = false;
    bool feedbackProtection = false;
    float adaptiveReleaseMs = 0.0f;

    // Allows Primed to request opening before audible audio reaches the gain
    // path. lookaheadSamples is informational until lookahead is wired.
    bool allowLookaheadPreOpen = false;
    int lookaheadSamples = 0;

    // Optional completion hooks let the eventual gain path finish a phase
    // earlier than its configured nominal duration.
    bool attackComplete = false;
    bool releaseComplete = false;
  };

  struct Input {
    float openScoreDb = -100.0f;
    float sustainScoreDb = -100.0f;
    Evidence evidence{};
  };

  struct Output {
    State state = State::Closed;
    State previousState = State::Closed;
    TransitionReason reason = TransitionReason::None;
    std::uint32_t flags = NoFlags;
    float openCertainty = 0.0f;
    float openScoreDb = -100.0f;
    float sustainScoreDb = -100.0f;
    float sustainScore = 0.0f;
    float thresholdOpenDb = -40.0f;
    float thresholdCloseDb = -46.0f;
    std::uint64_t samplesInState = 0;
    float adaptiveReleaseMs = 0.0f;
    bool shouldOpenEvidence = false;
    bool stateChanged = false;

    [[nodiscard]] bool hasFlag(Flag flag) const noexcept {
      return (flags & static_cast<std::uint32_t>(flag)) != 0;
    }
  };

  PerformanceStateMachine() noexcept;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  void setThresholdOpenDb(float thresholdDb) noexcept;
  void setHysteresisDb(float hysteresisDb) noexcept;
  void setAttackTimeMs(float attackMs) noexcept;
  void setHoldTimeMs(float holdMs) noexcept;
  void setReleaseTimeMs(float releaseMs) noexcept;

  // Aliases match the persisted parameter terminology.
  void setThresholdOpen(float thresholdDb) noexcept {
    setThresholdOpenDb(thresholdDb);
  }
  void setHysteresis(float hysteresisDb) noexcept {
    setHysteresisDb(hysteresisDb);
  }
  void setAttack(float attackMs) noexcept { setAttackTimeMs(attackMs); }
  void setHold(float holdMs) noexcept { setHoldTimeMs(holdMs); }
  void setRelease(float releaseMs) noexcept { setReleaseTimeMs(releaseMs); }

  [[nodiscard]] float getThresholdOpenDb() const noexcept;
  // Always derived; there is deliberately no independently stored close
  // threshold parameter.
  [[nodiscard]] float getThresholdCloseDb() const noexcept;
  [[nodiscard]] float getHysteresisDb() const noexcept;
  [[nodiscard]] State getState() const noexcept { return state_; }
  [[nodiscard]] TransitionReason getLastTransitionReason() const noexcept {
    return lastReason_;
  }
  [[nodiscard]] const Output &getLastOutput() const noexcept {
    return lastOutput_;
  }
  [[nodiscard]] float getAdaptiveRelease() const noexcept {
    return lastOutput_.adaptiveReleaseMs;
  }

  void setSustainProtection(bool active) noexcept;
  void setAttackAssist(bool active, float reductionDb = 0.0f) noexcept;
  void setFeedbackProtection(bool active) noexcept;

  [[nodiscard]] Output processSample(float detectorLevelDb) noexcept;
  [[nodiscard]] Output processSample(float detectorLevelDb,
                                     const Evidence &evidence) noexcept;
  [[nodiscard]] Output processSample(float openScoreDb,
                                     float sustainScoreDb) noexcept;
  [[nodiscard]] Output processSample(float openScoreDb, float sustainScoreDb,
                                     const Evidence &evidence) noexcept;
  [[nodiscard]] Output processSample(const Input &input) noexcept;

  // All buffers are caller-owned. Null output buffers are allowed.
  void process(const float *detectorLevelDb, Output *output,
               int numSamples) noexcept;
  void process(const Input *input, Output *output, int numSamples) noexcept;
  void processBlock(const float *detectorLevelDb, Output *output,
                    int numSamples) noexcept {
    process(detectorLevelDb, output, numSamples);
  }
  void processBlock(const Input *input, Output *output,
                    int numSamples) noexcept {
    process(input, output, numSamples);
  }

  // Section 7.3 smoothstep. Discrete state remains controlled by hysteresis.
  [[nodiscard]] static float softDecision(float levelDb, float thresholdOpenDb,
                                          float hysteresisDb) noexcept;
  [[nodiscard]] float softDecision(float levelDb) const noexcept;

  [[nodiscard]] static bool isOpenFamily(State state) noexcept;

private:
  static constexpr float minHysteresisDb_ = 0.5f;
  static constexpr float maxHysteresisDb_ = 24.0f;

  void updateTimingIfNeeded() noexcept;
  [[nodiscard]] std::uint64_t
  millisecondsToSamples(float timeMs) const noexcept;
  void transitionTo(State state, TransitionReason reason) noexcept;
  [[nodiscard]] Output makeOutput(const Input &input, State previousState,
                                  float thresholdOpenDb,
                                  float thresholdCloseDb) const noexcept;
  [[nodiscard]] static float finiteOr(float value, float fallback) noexcept;

  double sampleRate_ = 0.0;
  State state_ = State::Closed;
  TransitionReason lastReason_ = TransitionReason::Reset;
  std::uint64_t samplesInState_ = 0;
  std::uint64_t attackSamples_ = 0;
  std::uint64_t holdSamples_ = 0;
  std::uint64_t releaseSamples_ = 0;
  Output lastOutput_{};

  std::atomic<float> thresholdOpenDb_{-40.0f};
  std::atomic<float> hysteresisDb_{6.0f};
  std::atomic<float> attackTimeMs_{5.0f};
  std::atomic<float> holdTimeMs_{50.0f};
  std::atomic<float> releaseTimeMs_{100.0f};
  std::atomic<bool> timingDirty_{true};

  std::atomic<bool> attackAssistActive_{false};
  std::atomic<float> attackAssistReduction_{0.0f};
  std::atomic<bool> sustainProtectionActive_{false};
  std::atomic<bool> feedbackProtectionActive_{false};
};

} // namespace ChugGate::DSP
