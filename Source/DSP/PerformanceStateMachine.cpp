#include "PerformanceStateMachine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ChugGate::DSP {

PerformanceStateMachine::PerformanceStateMachine() noexcept { reset(); }

void PerformanceStateMachine::prepare(double sampleRate) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  timingDirty_.store(true, std::memory_order_release);
  updateTimingIfNeeded();
  reset();
}

void PerformanceStateMachine::reset() noexcept {
  state_ = State::Closed;
  lastReason_ = TransitionReason::Reset;
  samplesInState_ = 0;
  lastOutput_ = {};
  lastOutput_.reason = TransitionReason::Reset;
  lastOutput_.thresholdOpenDb = getThresholdOpenDb();
  lastOutput_.thresholdCloseDb = getThresholdCloseDb();
}

void PerformanceStateMachine::setThresholdOpenDb(float thresholdDb) noexcept {
  thresholdOpenDb_.store(
      std::clamp(finiteOr(thresholdDb, -40.0f), -100.0f, 0.0f),
      std::memory_order_relaxed);
}

void PerformanceStateMachine::setHysteresisDb(float hysteresisDb) noexcept {
  hysteresisDb_.store(std::clamp(finiteOr(hysteresisDb, minHysteresisDb_),
                                 minHysteresisDb_, maxHysteresisDb_),
                      std::memory_order_relaxed);
}

void PerformanceStateMachine::setAttackTimeMs(float attackMs) noexcept {
  attackTimeMs_.store(std::clamp(finiteOr(attackMs, 0.0f), 0.0f, 5000.0f),
                      std::memory_order_relaxed);
  timingDirty_.store(true, std::memory_order_release);
}

void PerformanceStateMachine::setHoldTimeMs(float holdMs) noexcept {
  holdTimeMs_.store(std::clamp(finiteOr(holdMs, 0.0f), 0.0f, 1000.0f),
                    std::memory_order_relaxed);
  timingDirty_.store(true, std::memory_order_release);
}

void PerformanceStateMachine::setReleaseTimeMs(float releaseMs) noexcept {
  releaseTimeMs_.store(std::clamp(finiteOr(releaseMs, 0.0f), 0.0f, 10000.0f),
                       std::memory_order_relaxed);
  timingDirty_.store(true, std::memory_order_release);
}

float PerformanceStateMachine::getThresholdOpenDb() const noexcept {
  return thresholdOpenDb_.load(std::memory_order_relaxed);
}

float PerformanceStateMachine::getThresholdCloseDb() const noexcept {
  return getThresholdOpenDb() - getHysteresisDb();
}

float PerformanceStateMachine::getHysteresisDb() const noexcept {
  return std::clamp(hysteresisDb_.load(std::memory_order_relaxed),
                    minHysteresisDb_, maxHysteresisDb_);
}

PerformanceStateMachine::Output
PerformanceStateMachine::processSample(float detectorLevelDb) noexcept {
  return processSample(detectorLevelDb, detectorLevelDb, Evidence{});
}

PerformanceStateMachine::Output
PerformanceStateMachine::processSample(float detectorLevelDb,
                                       const Evidence &evidence) noexcept {
  return processSample(detectorLevelDb, detectorLevelDb, evidence);
}

PerformanceStateMachine::Output
PerformanceStateMachine::processSample(float openScoreDb,
                                       float sustainScoreDb) noexcept {
  return processSample(openScoreDb, sustainScoreDb, Evidence{});
}

PerformanceStateMachine::Output
PerformanceStateMachine::processSample(float openScoreDb, float sustainScoreDb,
                                       const Evidence &evidence) noexcept {
  return processSample(Input{openScoreDb, sustainScoreDb, evidence});
}

PerformanceStateMachine::Output
PerformanceStateMachine::processSample(const Input &rawInput) noexcept {
  updateTimingIfNeeded();

  Input input = rawInput;
  input.openScoreDb =
      finiteOr(input.openScoreDb, -std::numeric_limits<float>::max());
  input.sustainScoreDb =
      finiteOr(input.sustainScoreDb, -std::numeric_limits<float>::max());
  input.evidence.sustainScore =
      std::clamp(finiteOr(input.evidence.sustainScore, 0.0f), 0.0f, 1.0f);
  input.evidence.lookaheadSamples =
      std::max(0, input.evidence.lookaheadSamples);

  const float thresholdOpen = getThresholdOpenDb();
  // Section 7.2: derive T_close on every update. It is never an independent
  // persisted or runtime parameter.
  const float thresholdClose = thresholdOpen - getHysteresisDb();
  const bool aboveOpen = input.openScoreDb >= thresholdOpen;
  const bool aboveClose = input.sustainScoreDb > thresholdClose;
  const bool sustainProtected = input.evidence.sustainProtection;
  const bool validTransient = input.evidence.validTransient;
  const State previousState = state_;

  switch (state_) {
  case State::Closed:
    if (aboveOpen) {
      transitionTo(State::Attack, TransitionReason::ThresholdReached);
    } else if (validTransient) {
      transitionTo(State::Attack, TransitionReason::TransientValidated);
    } else if (input.evidence.onset) {
      transitionTo(State::Primed, TransitionReason::OnsetEvidence);
    }
    break;

  case State::Primed:
    if (aboveOpen) {
      transitionTo(State::Attack, TransitionReason::ThresholdReached);
    } else if (validTransient) {
      transitionTo(State::Attack, TransitionReason::TransientValidated);
    } else if (!input.evidence.onset) {
      transitionTo(State::Closed, TransitionReason::PrimingCancelled);
    }
    break;

  case State::Attack:
    if (input.evidence.attackComplete || samplesInState_ >= attackSamples_) {
      transitionTo(State::Open, TransitionReason::AttackComplete);
    }
    break;

  case State::Open:
    if (validTransient) {
      transitionTo(State::Attack, TransitionReason::TransientValidated);
    } else if (!aboveOpen && holdSamples_ > 0) {
      transitionTo(State::Hold, TransitionReason::BelowOpenThreshold);
    } else if (!aboveClose && sustainProtected) {
      transitionTo(State::Sustain, TransitionReason::SustainProtection);
    } else if (!aboveClose) {
      transitionTo(State::Release, TransitionReason::HoldExpired);
    }
    break;

  case State::Hold:
    if (validTransient || aboveOpen) {
      transitionTo(State::Attack, validTransient
                                      ? TransitionReason::TransientValidated
                                      : TransitionReason::ThresholdReached);
    } else if (samplesInState_ >= holdSamples_) {
      if (aboveClose) {
        transitionTo(State::Sustain, TransitionReason::HysteresisProtection);
      } else if (sustainProtected) {
        transitionTo(State::Sustain, TransitionReason::SustainProtection);
      } else {
        transitionTo(State::Release, TransitionReason::HoldExpired);
      }
    }
    break;

  case State::Sustain:
    if (validTransient) {
      transitionTo(State::Attack, TransitionReason::TransientValidated);
    } else if (aboveOpen) {
      transitionTo(State::Open, TransitionReason::LevelRecovered);
    } else if (!aboveClose && !sustainProtected) {
      transitionTo(State::Release, TransitionReason::SustainEnded);
    }
    break;

  case State::Release:
    // Reopen is a direction change only. No gain value is owned or reset here;
    // GainComputer/GainSmoother continue from their current gain.
    if (validTransient) {
      transitionTo(State::Reopen, TransitionReason::ReopenTransient);
    } else if (aboveOpen) {
      transitionTo(State::Attack, TransitionReason::ThresholdReached);
    } else if (aboveClose) {
      transitionTo(State::Sustain, TransitionReason::HysteresisProtection);
    } else if (sustainProtected) {
      transitionTo(State::Sustain, TransitionReason::SustainProtection);
    } else if (input.evidence.releaseComplete ||
               samplesInState_ >= releaseSamples_) {
      transitionTo(State::Closed, TransitionReason::ReleaseComplete);
    }
    break;

  case State::Reopen:
    if (input.evidence.attackComplete || samplesInState_ >= attackSamples_) {
      transitionTo(State::Open, TransitionReason::AttackComplete);
    }
    break;
  }

  lastOutput_ = makeOutput(input, previousState, thresholdOpen, thresholdClose);
  if (samplesInState_ != std::numeric_limits<std::uint64_t>::max())
    ++samplesInState_;
  return lastOutput_;
}

void PerformanceStateMachine::process(const float *detectorLevelDb,
                                      Output *output, int numSamples) noexcept {
  if (detectorLevelDb == nullptr || numSamples <= 0)
    return;

  for (int i = 0; i < numSamples; ++i) {
    const Output result = processSample(detectorLevelDb[i]);
    if (output != nullptr)
      output[i] = result;
  }
}

void PerformanceStateMachine::process(const Input *input, Output *output,
                                      int numSamples) noexcept {
  if (input == nullptr || numSamples <= 0)
    return;

  for (int i = 0; i < numSamples; ++i) {
    const Output result = processSample(input[i]);
    if (output != nullptr)
      output[i] = result;
  }
}

float PerformanceStateMachine::softDecision(float levelDb,
                                            float thresholdOpenDb,
                                            float hysteresisDb) noexcept {
  const float hysteresis = std::clamp(finiteOr(hysteresisDb, minHysteresisDb_),
                                      minHysteresisDb_, maxHysteresisDb_);
  const float thresholdOpen = finiteOr(thresholdOpenDb, -40.0f);
  const float thresholdClose = thresholdOpen - hysteresis;
  const float level = finiteOr(levelDb, thresholdClose);
  const float u = std::clamp((level - thresholdClose) / hysteresis, 0.0f, 1.0f);
  return u * u * (3.0f - 2.0f * u);
}

float PerformanceStateMachine::softDecision(float levelDb) const noexcept {
  return softDecision(levelDb, getThresholdOpenDb(), getHysteresisDb());
}

bool PerformanceStateMachine::isOpenFamily(State state) noexcept {
  switch (state) {
  case State::Attack:
  case State::Open:
  case State::Hold:
  case State::Sustain:
  case State::Reopen:
    return true;
  case State::Primed:
  case State::Closed:
  case State::Release:
    return false;
  }
  return false;
}

void PerformanceStateMachine::updateTimingIfNeeded() noexcept {
  if (!timingDirty_.exchange(false, std::memory_order_acq_rel))
    return;

  attackSamples_ =
      millisecondsToSamples(attackTimeMs_.load(std::memory_order_relaxed));
  holdSamples_ =
      millisecondsToSamples(holdTimeMs_.load(std::memory_order_relaxed));
  releaseSamples_ =
      millisecondsToSamples(releaseTimeMs_.load(std::memory_order_relaxed));
}

std::uint64_t
PerformanceStateMachine::millisecondsToSamples(float timeMs) const noexcept {
  if (sampleRate_ <= 0.0 || timeMs <= 0.0f)
    return 0;

  const double samples = sampleRate_ * static_cast<double>(timeMs) * 0.001;
  return static_cast<std::uint64_t>(std::llround(samples));
}

void PerformanceStateMachine::transitionTo(State state,
                                           TransitionReason reason) noexcept {
  if (state_ == state)
    return;
  state_ = state;
  lastReason_ = reason;
  samplesInState_ = 0;
}

PerformanceStateMachine::Output
PerformanceStateMachine::makeOutput(const Input &input, State previousState,
                                    float thresholdOpenDb,
                                    float thresholdCloseDb) const noexcept {
  Output output;
  output.state = state_;
  output.previousState = previousState;
  output.reason = lastReason_;
  output.openCertainty = softDecision(input.openScoreDb, thresholdOpenDb,
                                      thresholdOpenDb - thresholdCloseDb);
  output.openScoreDb = input.openScoreDb;
  output.sustainScoreDb = input.sustainScoreDb;
  output.sustainScore = input.evidence.sustainScore;
  output.thresholdOpenDb = thresholdOpenDb;
  output.thresholdCloseDb = thresholdCloseDb;
  output.samplesInState = samplesInState_;

  output.stateChanged = state_ != previousState;
  if (output.stateChanged)
    output.flags |= StateChanged;
  if (input.openScoreDb >= thresholdOpenDb)
    output.flags |= AboveOpenThreshold;
  if (input.sustainScoreDb > thresholdCloseDb)
    output.flags |= AboveCloseThreshold;
  if (input.evidence.onset)
    output.flags |= OnsetDetected;
  if (input.evidence.validTransient)
    output.flags |= ValidTransient;
  if (input.evidence.sustainProtection)
    output.flags |= SustainProtected;

  const bool lookaheadPreOpen = state_ == State::Primed &&
                                input.evidence.allowLookaheadPreOpen &&
                                input.evidence.lookaheadSamples > 0;
  const bool shouldOpen = input.openScoreDb >= thresholdOpenDb ||
                          input.evidence.validTransient || lookaheadPreOpen;
  output.shouldOpenEvidence = shouldOpen;
  if (lookaheadPreOpen)
    output.flags |= LookaheadPreOpen;
  if (shouldOpen)
    output.flags |= ShouldOpen;
  if (isOpenFamily(state_) || lookaheadPreOpen)
    output.flags |= OpenFamily;
  if (state_ == State::Attack || state_ == State::Reopen || lookaheadPreOpen)
    output.flags |= OpeningPhase;
  if (state_ == State::Release)
    output.flags |= ClosingPhase;
  if (state_ == State::Hold)
    output.flags |= HoldActive;
  if (state_ == State::Reopen)
    output.flags |= ReopenActive;

  return output;
}

float PerformanceStateMachine::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

} // namespace ChugGate::DSP
