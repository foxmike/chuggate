#include "DetectorEngine.h"
#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

DetectorEngine::DetectorEngine() noexcept
{
    reset();
}

void DetectorEngine::prepare(double sampleRate, int maxBlockSize) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
    maxBlockSize_ = std::max(0, maxBlockSize);
    coefficientsDirty_.store(true, std::memory_order_release);
    reset();
}

void DetectorEngine::reset() noexcept
{
    lastPeak_ = 0.0f;
    lastRmsSquared_ = 0.0f;
    lastEnvelope_ = 0.0f;
    lastLevelDb_ = -100.0f;
}

void DetectorEngine::process(const float* monoKey, float* envelopeOut, float* levelDbOut, int numSamples) noexcept
{
    if (monoKey == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float env = processSample(monoKey[i]);
        if (envelopeOut != nullptr)
            envelopeOut[i] = env;
        if (levelDbOut != nullptr)
            levelDbOut[i] = lastLevelDb_;
    }
}

void DetectorEngine::process(float* monoKey, int numSamples) noexcept
{
    if (monoKey == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        monoKey[i] = processSample(monoKey[i]);
    }
}

float DetectorEngine::processSample(float sample, float overrideMagnitude) noexcept
{
    if (coefficientsDirty_.load(std::memory_order_acquire))
    {
        updateTimeConstants();
        coefficientsDirty_.store(false, std::memory_order_release);
    }

    const float magnitude = (overrideMagnitude >= 0.0f) ? overrideMagnitude : std::abs(sample);
    const float magnitudeSquared = magnitude * magnitude;

    // Peak detector: P[n] = max(|x[n]|, alpha_p * P[n-1])
    lastPeak_ = std::max(magnitude, alphaP_ * lastPeak_);

    // RMS/Power: R^2[n] = alpha_r * R^2[n-1] + (1 - alpha_r) * x^2[n]
    lastRmsSquared_ = alphaR_rms_ * lastRmsSquared_ + (1.0f - alphaR_rms_) * magnitudeSquared;
    const float rms = std::sqrt(lastRmsSquared_ + eps);

    // Combined envelope: E[n] = w_p * P[n] + (1 - w_p) * R[n]
    const float weight = peakWeight_.load(std::memory_order_relaxed);
    const float blendedEnvelope = weight * lastPeak_ + (1.0f - weight) * rms;

    // Attack/release ballistics on the detector envelope:
    // when |x| > Eprev: alpha_a; else alpha_r
    const float alpha = (magnitude > lastEnvelope_) ? alphaA_ : alphaR_;
    lastEnvelope_ = alpha * lastEnvelope_ + (1.0f - alpha) * blendedEnvelope;

    // Log level: L[n] = 20 * log10(E[n] + eps)
    lastLevelDb_ = 20.0f * std::log10(lastEnvelope_ + eps);

    return lastEnvelope_;
}

void DetectorEngine::setPeakWeight(float weight) noexcept
{
    peakWeight_.store(std::clamp(weight, 0.0f, 1.0f), std::memory_order_relaxed);
}

void DetectorEngine::setAttackTime(float attackMs) noexcept
{
    attackTimeMs_.store(std::max(0.0f, attackMs), std::memory_order_relaxed);
    coefficientsDirty_.store(true, std::memory_order_release);
}

void DetectorEngine::setReleaseTime(float releaseMs) noexcept
{
    releaseTimeMs_.store(std::max(0.0f, releaseMs), std::memory_order_relaxed);
    coefficientsDirty_.store(true, std::memory_order_release);
}

void DetectorEngine::setPeakRelease(float peakReleaseMs) noexcept
{
    peakReleaseMs_.store(std::max(0.0f, peakReleaseMs), std::memory_order_relaxed);
    coefficientsDirty_.store(true, std::memory_order_release);
}

void DetectorEngine::setRmsWindow(float rmsWindowMs) noexcept
{
    rmsWindowMs_.store(std::max(0.0f, rmsWindowMs), std::memory_order_relaxed);
    coefficientsDirty_.store(true, std::memory_order_release);
}

float DetectorEngine::getEnvelope() const noexcept
{
    return lastEnvelope_;
}

float DetectorEngine::getLevelDb() const noexcept
{
    return lastLevelDb_;
}

void DetectorEngine::updateTimeConstants() noexcept
{
    if (sampleRate_ <= 0.0)
    {
        alphaP_ = 0.0f;
        alphaR_rms_ = 0.0f;
        alphaA_ = 0.0f;
        alphaR_ = 0.0f;
        return;
    }

    alphaP_ = calculateAlpha(peakReleaseMs_.load(std::memory_order_relaxed));
    alphaR_rms_ = calculateAlpha(rmsWindowMs_.load(std::memory_order_relaxed));
    alphaA_ = calculateAlpha(attackTimeMs_.load(std::memory_order_relaxed));
    alphaR_ = calculateAlpha(releaseTimeMs_.load(std::memory_order_relaxed));
}

float DetectorEngine::calculateAlpha(float timeMs) const noexcept
{
    if (sampleRate_ <= 0.0 || timeMs <= 0.0f)
        return 0.0f;

    const float tau = timeMs * 0.001f;
    return std::exp(-1.0f / (static_cast<float>(sampleRate_) * tau));
}

} // namespace ChugGate::DSP
