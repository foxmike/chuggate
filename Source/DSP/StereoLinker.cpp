#include "StereoLinker.h"
#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

StereoLinker::StereoLinker() noexcept
{
    reset();
}

void StereoLinker::prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    numChannels_ = numChannels;

    // Calculate EMA smoothing coefficient based on block size & sample rate.
    // We target a time constant of 1.0 second.
    const double samplesPerBlock = (maxBlockSize > 0) ? maxBlockSize : 256;
    const double secondsPerBlock = (sampleRate > 0.0) ? (samplesPerBlock / sampleRate) : (256.0 / 44100.0);
    
    constexpr double timeConstantSeconds = 1.0;
    alpha_ = static_cast<float>(std::exp(-secondsPerBlock / timeConstantSeconds));

    reset();
}

void StereoLinker::reset() noexcept
{
    divergenceMetric_.store(0.0f, std::memory_order_relaxed);
    divergenceAboveThresholdSmoothed_.store(0.0f, std::memory_order_relaxed);
}

void StereoLinker::setMode(LinkMode mode) noexcept
{
    mode_.store(mode, std::memory_order_relaxed);
}

void StereoLinker::setMode(int modeIndex) noexcept
{
    switch (modeIndex)
    {
        case 0:  setMode(LinkMode::Max); break;
        case 1:  setMode(LinkMode::Energy); break;
        case 2:  setMode(LinkMode::Average); break;
        case 3:  setMode(LinkMode::DualMono); break;
        default: setMode(LinkMode::Max); break;
    }
}

void StereoLinker::setLinkAmount(float amount) noexcept
{
    linkAmount_.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
}

StereoPair StereoLinker::linkEnvelopes(float envL, float envR) noexcept
{
    if (numChannels_ <= 1)
    {
        return StereoPair{ envL, envL };
    }

    float linkedEnv = 0.0f;
    const LinkMode mode = mode_.load(std::memory_order_relaxed);

    switch (mode)
    {
        case LinkMode::Max:
            linkedEnv = std::max(envL, envR);
            break;
        case LinkMode::Energy:
            linkedEnv = std::sqrt(std::max(0.0f, (envL * envL + envR * envR) * 0.5f));
            break;
        case LinkMode::Average:
            linkedEnv = (envL + envR) * 0.5f;
            break;
        case LinkMode::DualMono:
            // DualMono returns the pair unchanged and tracks envelope divergence
            updateDivergence(std::abs(envL - envR));
            return StereoPair{ envL, envR };
    }

    const float lambda = linkAmount_.load(std::memory_order_relaxed);
    const float linkedLeft = lambda * linkedEnv + (1.0f - lambda) * envL;
    const float linkedRight = lambda * linkedEnv + (1.0f - lambda) * envR;
    updateDivergence(std::abs(linkedLeft - linkedRight));
    return StereoPair{ linkedLeft, linkedRight };
}

StereoPair StereoLinker::linkGains(float gainL, float gainR, float gainLinked) noexcept
{
    if (numChannels_ <= 1)
    {
        return StereoPair{ gainL, gainL };
    }

    const LinkMode mode = mode_.load(std::memory_order_relaxed);
    
    if (mode == LinkMode::DualMono)
    {
        // DualMono leaves L/R gains completely independent and tracks gain divergence.
        updateDivergence(std::abs(gainL - gainR));
        return StereoPair{ gainL, gainR };
    }

    // Blend per-channel gains with linked gain based on lambda.
    const float lambda = linkAmount_.load(std::memory_order_relaxed);
    const float gL_prime = lambda * gainLinked + (1.0f - lambda) * gainL;
    const float gR_prime = lambda * gainLinked + (1.0f - lambda) * gainR;

    updateDivergence(std::abs(gL_prime - gR_prime));

    return StereoPair{ gL_prime, gR_prime };
}

bool StereoLinker::isMonoSafetyWarning() const noexcept
{
    if (numChannels_ <= 1)
        return false;

    // Trigger warning if L/R divergence frequently exceeds threshold
    return (divergenceAboveThresholdSmoothed_.load(std::memory_order_relaxed) > warningProportionThreshold);
}

float StereoLinker::getDivergenceMetric() const noexcept
{
    if (numChannels_ <= 1)
        return 0.0f;
    return divergenceMetric_.load(std::memory_order_relaxed);
}

void StereoLinker::updateDivergence(float diffValue) noexcept
{
    if (numChannels_ <= 1)
    {
        divergenceMetric_.store(0.0f, std::memory_order_relaxed);
        divergenceAboveThresholdSmoothed_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const float alpha = alpha_;
    
    // Smooth the absolute divergence metric
    const float currentMetric = divergenceMetric_.load(std::memory_order_relaxed);
    const float newMetric = alpha * currentMetric + (1.0f - alpha) * diffValue;
    divergenceMetric_.store(newMetric, std::memory_order_relaxed);

    // Smooth the binary state of whether divergence exceeds our warning threshold
    const bool isDiverging = (diffValue > divergenceThreshold);
    const float currentAbove = divergenceAboveThresholdSmoothed_.load(std::memory_order_relaxed);
    const float newAbove = alpha * currentAbove + (1.0f - alpha) * (isDiverging ? 1.0f : 0.0f);
    divergenceAboveThresholdSmoothed_.store(newAbove, std::memory_order_relaxed);
}

} // namespace ChugGate::DSP
