#include "AttackGuard.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ChugGate::DSP {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float minimumLevelDb = -100.0f;
constexpr float maximumAvailableThresholdReductionDb = 36.0f;

} // namespace

void AttackGuard::prepare(double sampleRate, int maxBlockSize) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  coefficientsDirty_.store(true, std::memory_order_release);
  updateCoefficientsIfNeeded();
  reset();
}

void AttackGuard::reset() noexcept {
  previousInput_ = 0.0f;
  previousHighPass_ = 0.0f;
  previousEmphasised_ = 0.0f;
  transientEnvelope_ = 0.0f;

  candidateActive_ = false;
  candidateArmed_ = true;
  attackAssistActive_ = false;
  candidateAge_ = 0;
  samplesSinceCandidate_ = confirmationSamples_;
  bodyEnergy_ = 0.0f;
  bodyMean_ = 0.0f;
  bodyM2_ = 0.0f;
  bodySampleCount_ = 0;
  bodyActiveSampleCount_ = 0;
  lastOutput_ = {};
}

AttackGuard::Output
AttackGuard::processSample(float rawMonoKey, float rawDetectorLevelDb) noexcept {
  updateCoefficientsIfNeeded();

  const float detectorLevelDb =
      finiteOr(rawDetectorLevelDb, minimumLevelDb);
  Output result;
  result.openAssistScore = detectorLevelDb;

  const float monoKey = std::clamp(finiteOr(rawMonoKey, 0.0f), -16.0f, 16.0f);
  if (!enabled_.load(std::memory_order_relaxed)) {
    // Keep the filter warm so enabling the guard cannot manufacture a
    // discontinuity from stale state.
    const float emphasised =
        highPassCoefficient_ *
        (previousHighPass_ + monoKey - previousInput_);
    previousInput_ = monoKey;
    previousHighPass_ = emphasised;
    previousEmphasised_ = emphasised;
    transientEnvelope_ = 0.0f;
    candidateActive_ = false;
    candidateArmed_ = true;
    attackAssistActive_ = false;
    lastOutput_ = result;
    return result;
  }

  // First-order high-pass emphasis followed by the finite difference from
  // section 8.2.
  const float emphasised =
      highPassCoefficient_ *
      (previousHighPass_ + monoKey - previousInput_);
  previousInput_ = monoKey;
  previousHighPass_ = emphasised;

  const float difference = emphasised - previousEmphasised_;
  previousEmphasised_ = emphasised;
  const float instantaneousTransient = std::abs(difference);
  const float previousTransientEnvelope = transientEnvelope_;
  transientEnvelope_ =
      std::max(instantaneousTransient,
               transientDecayCoefficient_ * transientEnvelope_);

  const float strength = calculateTransientStrength(transientEnvelope_);
  result.transientStrength = strength;

  // A candidate starts from instantaneous evidence. Using the decaying
  // envelope here would repeatedly start candidates from a single impulse.
  const float instantaneousStrength =
      calculateTransientStrength(instantaneousTransient);
  if (samplesSinceCandidate_ != std::numeric_limits<std::uint32_t>::max())
    ++samplesSinceCandidate_;

  if (!candidateActive_ && strength <= 0.0f)
    candidateArmed_ = true;

  // A substantially stronger derivative can represent a rapid repick before
  // T[n] has fully decayed. The ratio condition prevents a periodic waveform
  // from manufacturing a new attack every cycle.
  const bool rapidRepick =
      samplesSinceCandidate_ >= confirmationSamples_ &&
      instantaneousTransient > 1.4f * previousTransientEnvelope;
  if (!candidateActive_ && instantaneousStrength > 0.0f &&
      (candidateArmed_ || rapidRepick))
    beginCandidate();

  if (candidateActive_) {
    // The trigger sample itself is deliberately excluded: an isolated impulse
    // must contribute no "body" merely by being large.
    if (candidateAge_ > 0)
      accumulateBody(monoKey);

    ++candidateAge_;
    if (candidateAge_ >= confirmationSamples_) {
      if (candidateHasBody()) {
        result.validAttack = true;
        attackAssistActive_ = true;
      }
      candidateActive_ = false;
    }
  }

  // T[n] naturally releases the temporary assist. Validation gates G_t so
  // click-only candidates cannot bias the opening score while pending.
  if (attackAssistActive_ && strength <= 0.0f)
    attackAssistActive_ = false;

  if (attackAssistActive_) {
    const float amount =
        amount_.load(std::memory_order_relaxed);
    const float sensitivity =
        sensitivity_.load(std::memory_order_relaxed);
    const float maximum =
        maxThresholdReductionDb_.load(std::memory_order_relaxed);
    // Amount controls the depth of the guard contribution. The user maximum
    // is deliberately applied only after that contribution is computed: using
    // it as the multiplier as well as the clamp made it a second amount
    // control and prevented it from behaving as a true safety ceiling.
    const float requestedReductionDb =
        maximumAvailableThresholdReductionDb * amount * sensitivity * strength;
    result.thresholdReductionDb =
        std::min(requestedReductionDb, maximum);
    result.openAssistScore = detectorLevelDb + result.thresholdReductionDb;
  }

  lastOutput_ = result;
  return result;
}

void AttackGuard::process(const float *monoKey, const float *detectorLevelDb,
                          Output *output, int numSamples) noexcept {
  if (monoKey == nullptr || detectorLevelDb == nullptr || numSamples <= 0)
    return;

  for (int i = 0; i < numSamples; ++i) {
    const Output result = processSample(monoKey[i], detectorLevelDb[i]);
    if (output != nullptr)
      output[i] = result;
  }
}

void AttackGuard::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

void AttackGuard::setAmount(float amount) noexcept {
  amount_.store(std::clamp(finiteOr(amount, 0.0f), 0.0f, 1.0f),
                std::memory_order_relaxed);
}

void AttackGuard::setSensitivity(float sensitivity) noexcept {
  sensitivity_.store(
      std::clamp(finiteOr(sensitivity, 0.0f), 0.0f, 1.0f),
      std::memory_order_relaxed);
}

void AttackGuard::setFrequencyEmphasis(float frequencyHz) noexcept {
  frequencyHz_.store(
      std::clamp(finiteOr(frequencyHz, 1000.0f), 30.0f, 12000.0f),
      std::memory_order_relaxed);
  coefficientsDirty_.store(true, std::memory_order_release);
}

void AttackGuard::setDecay(float decayMs) noexcept {
  decayMs_.store(std::clamp(finiteOr(decayMs, 8.0f), 0.1f, 250.0f),
                 std::memory_order_relaxed);
  coefficientsDirty_.store(true, std::memory_order_release);
}

void AttackGuard::setSoftNoteAssist(float assist) noexcept {
  softNoteAssist_.store(std::clamp(finiteOr(assist, 0.0f), 0.0f, 1.0f),
                        std::memory_order_relaxed);
}

void AttackGuard::setMaxThresholdReduction(float reductionDb) noexcept {
  maxThresholdReductionDb_.store(
      std::clamp(finiteOr(reductionDb, 0.0f), 0.0f, 36.0f),
      std::memory_order_relaxed);
}

void AttackGuard::setConfirmationWindow(float windowMs) noexcept {
  confirmationWindowMs_.store(
      std::clamp(finiteOr(windowMs, 4.0f), 0.1f, 30.0f),
      std::memory_order_relaxed);
  coefficientsDirty_.store(true, std::memory_order_release);
}

void AttackGuard::updateCoefficientsIfNeeded() noexcept {
  if (!coefficientsDirty_.exchange(false, std::memory_order_acq_rel))
    return;

  if (sampleRate_ <= 0.0) {
    highPassCoefficient_ = 0.0f;
    transientDecayCoefficient_ = 0.0f;
    confirmationSamples_ = 3;
    return;
  }

  const float sampleRate = static_cast<float>(sampleRate_);
  const float nyquistSafe = std::max(1.0f, 0.45f * sampleRate);
  const float frequency =
      std::min(frequencyHz_.load(std::memory_order_relaxed), nyquistSafe);
  highPassCoefficient_ =
      std::exp(-2.0f * pi * frequency / sampleRate);

  const float decaySeconds =
      decayMs_.load(std::memory_order_relaxed) * 0.001f;
  transientDecayCoefficient_ =
      decaySeconds > 0.0f
          ? std::exp(-1.0f / (sampleRate * decaySeconds))
          : 0.0f;

  const double requestedSamples =
      sampleRate_ *
      static_cast<double>(
          confirmationWindowMs_.load(std::memory_order_relaxed)) *
      0.001;
  const double boundedSamples =
      std::clamp(requestedSamples, 3.0,
                 static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
  confirmationSamples_ =
      static_cast<std::uint32_t>(std::llround(boundedSamples));
}

void AttackGuard::beginCandidate() noexcept {
  candidateActive_ = true;
  candidateArmed_ = false;
  candidateAge_ = 0;
  samplesSinceCandidate_ = 0;
  bodyEnergy_ = 0.0f;
  bodyMean_ = 0.0f;
  bodyM2_ = 0.0f;
  bodySampleCount_ = 0;
  bodyActiveSampleCount_ = 0;
}

void AttackGuard::accumulateBody(float sample) noexcept {
  const float sensitivity =
      sensitivity_.load(std::memory_order_relaxed);
  const float softAssist =
      softNoteAssist_.load(std::memory_order_relaxed);
  const float minimumBody =
      0.0015f * (1.0f - 0.75f * sensitivity) *
      (1.0f - 0.85f * softAssist);

  bodyEnergy_ += sample * sample;
  ++bodySampleCount_;
  if (std::abs(sample) >= minimumBody)
    ++bodyActiveSampleCount_;

  // Welford variance rejects a constant-valued digital step: it has level,
  // but no continuing AC body after the discontinuity.
  const float count = static_cast<float>(bodySampleCount_);
  const float delta = sample - bodyMean_;
  bodyMean_ += delta / count;
  bodyM2_ += delta * (sample - bodyMean_);
}

bool AttackGuard::candidateHasBody() const noexcept {
  if (bodySampleCount_ < 2)
    return false;

  const float sensitivity =
      sensitivity_.load(std::memory_order_relaxed);
  const float softAssist =
      softNoteAssist_.load(std::memory_order_relaxed);
  const float minimumBody =
      0.0015f * (1.0f - 0.75f * sensitivity) *
      (1.0f - 0.85f * softAssist);
  const float bodyRms =
      std::sqrt(bodyEnergy_ / static_cast<float>(bodySampleCount_));
  const float acRms =
      std::sqrt(std::max(0.0f, bodyM2_) /
                static_cast<float>(bodySampleCount_));
  const std::uint32_t minimumActiveSamples =
      std::max<std::uint32_t>(2, bodySampleCount_ / 4);

  return bodyRms >= minimumBody &&
         acRms >= minimumBody * 0.05f &&
         bodyActiveSampleCount_ >= minimumActiveSamples;
}

float AttackGuard::calculateTransientStrength(float envelope) const noexcept {
  const float sensitivity =
      sensitivity_.load(std::memory_order_relaxed);
  const float softAssist =
      softNoteAssist_.load(std::memory_order_relaxed);

  // Sensitivity and soft-note assist lower the onset floor while preserving a
  // useful span between T0 and T1.
  const float baseThreshold =
      0.01f + (0.00025f - 0.01f) * sensitivity;
  const float t0 = baseThreshold * (1.0f - 0.75f * softAssist);
  const float t1 = std::max(t0 + 1.0e-7f, t0 * 8.0f);
  return std::clamp((envelope - t0) / (t1 - t0), 0.0f, 1.0f);
}

float AttackGuard::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

} // namespace ChugGate::DSP
