#include "MultibandGate.h"

namespace ChugGate::DSP {

MultibandGate::MultibandGate() noexcept {
    reset();
}

void MultibandGate::prepare(double sampleRate, int maxBlockSize, int numChannels, int maxBands) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlockSize_ = std::max(1, maxBlockSize);
    numChannels_ = std::max(1, numChannels);
    maxBands_ = std::clamp(maxBands, 1, 3);

    // Preallocate states for each channel
    gainChannels_.resize(static_cast<size_t>(numChannels_));
    for (auto& channelGains : gainChannels_) {
        channelGains.fill(1.0f);
    }

    // Preallocate target buffers
    for (int b = 0; b < 3; ++b) {
        targetBuffer_[b].resize(static_cast<size_t>(maxBlockSize_), 1.0f);
    }

    // Update all coefficients
    for (int b = 0; b < 3; ++b) {
        updateAlpha(b);
    }

    useSampleWiseTargets_ = false;
}

void MultibandGate::reset() noexcept {
    for (auto& channelGains : gainChannels_) {
        channelGains.fill(1.0f);
    }
    useSampleWiseTargets_ = false;
    for (int b = 0; b < 3; ++b) {
        std::fill(targetBuffer_[b].begin(), targetBuffer_[b].end(), 1.0f);
    }
}

void MultibandGate::setNumBands(int numBands) noexcept {
    numBands_ = std::clamp(numBands, 1, 3);
}

void MultibandGate::setBandTargetGain(int band, float targetGainLinear) noexcept {
    if (band >= 0 && band < 3) {
        rawTargetGain_[static_cast<size_t>(band)] = std::clamp(targetGainLinear, 0.0f, 1.0f);
    }
}

void MultibandGate::setBandFloorDb(int band, float floorDb) noexcept {
    if (band >= 0 && band < 3) {
        floorDb_[static_cast<size_t>(band)] = std::clamp(floorDb, -80.0f, 0.0f);
    }
}

void MultibandGate::setBandOpenTime(int band, float openTimeMs) noexcept {
    if (band >= 0 && band < 3) {
        openTimeMs_[static_cast<size_t>(band)] = std::clamp(openTimeMs, 0.0f, 10000.0f);
        updateAlpha(band);
    }
}

void MultibandGate::setBandCloseTime(int band, float closeTimeMs) noexcept {
    if (band >= 0 && band < 3) {
        closeTimeMs_[static_cast<size_t>(band)] = std::clamp(closeTimeMs, 0.0f, 10000.0f);
        updateAlpha(band);
    }
}

void MultibandGate::setBandMute(int band, bool shouldMute) noexcept {
    if (band >= 0 && band < 3) {
        mute_[static_cast<size_t>(band)] = shouldMute;
    }
}

void MultibandGate::setBandSolo(int band, bool shouldSolo) noexcept {
    if (band >= 0 && band < 3) {
        solo_[static_cast<size_t>(band)] = shouldSolo;
    }
}

void MultibandGate::setCurveGamma(float gamma) noexcept {
    const float clampedGamma = std::clamp(gamma, 0.01f, 100.0f);
    for (int b = 0; b < 3; ++b) {
        gamma_[static_cast<size_t>(b)] = clampedGamma;
    }
}

void MultibandGate::setCurveGamma(int band, float gamma) noexcept {
    if (band >= 0 && band < 3) {
        gamma_[static_cast<size_t>(band)] = std::clamp(gamma, 0.01f, 100.0f);
    }
}

void MultibandGate::setBandThresholdOffset(int band, float offsetDb) noexcept {
    if (band >= 0 && band < 3) {
        thresholdOffsetDb_[static_cast<size_t>(band)] = offsetDb;
    }
}

float MultibandGate::getBandGain(int band) const noexcept {
    if (band < 0 || band >= 3)
        return 1.0f;
    if (gainChannels_.empty())
        return 1.0f;
    return gainChannels_[0][static_cast<size_t>(band)];
}

float MultibandGate::getBandGain(int band, int channel) const noexcept {
    if (band < 0 || band >= 3)
        return 1.0f;
    if (channel < 0 || channel >= static_cast<int>(gainChannels_.size()))
        return 1.0f;
    return gainChannels_[static_cast<size_t>(channel)][static_cast<size_t>(band)];
}

void MultibandGate::processTargets(const float* logEnergy, float globalThresholdOpen, float globalHysteresis, int numSamples) noexcept {
    if (logEnergy == nullptr || numSamples <= 0) {
        useSampleWiseTargets_ = false;
        return;
    }

    useSampleWiseTargets_ = true;
    const int limit = std::min(numSamples, maxBlockSize_);

    for (int b = 0; b < numBands_; ++b) {
        const float T_open = globalThresholdOpen + thresholdOffsetDb_[static_cast<size_t>(b)];
        const float T_close = T_open - globalHysteresis;
        float range = T_open - T_close;
        if (range <= 0.0f) {
            range = 0.001f;
        }

        auto& buf = targetBuffer_[static_cast<size_t>(b)];
        for (int i = 0; i < limit; ++i) {
            const float L = logEnergy[i];
            const float u = std::clamp((L - T_close) / range, 0.0f, 1.0f);
            // Smoothstep curve S(u) = 3u^2 - 2u^3
            const float S = u * u * (3.0f - 2.0f * u);
            buf[static_cast<size_t>(i)] = S;
        }
    }
}

void MultibandGate::process(const float* const* band0,
                            const float* const* band1,
                            const float* const* band2,
                            float* const* output,
                            int numChannels,
                            int numSamples) noexcept {
    process(band0, band1, band2, output, nullptr, numChannels, numSamples);
}

void MultibandGate::process(const float* const* band0,
                            const float* const* band1,
                            const float* const* band2,
                            float* const* output,
                            const float* const* externalTargets,
                            int numChannels,
                            int numSamples) noexcept {
    if (output == nullptr || numSamples <= 0 || numChannels <= 0) {
        return;
    }

    // Limit active channels to prepared size
    const int channelsToProcess = std::min(numChannels, static_cast<int>(gainChannels_.size()));

    for (int c = 0; c < channelsToProcess; ++c) {
        float* outChannel = output[c];
        if (outChannel == nullptr) {
            continue;
        }

        const float* b0 = (numBands_ >= 1 && band0 != nullptr) ? band0[c] : nullptr;
        const float* b1 = (numBands_ >= 2 && band1 != nullptr) ? band1[c] : nullptr;
        const float* b2 = (numBands_ >= 3 && band2 != nullptr) ? band2[c] : nullptr;

        for (int i = 0; i < numSamples; ++i) {
            float sum = 0.0f;

            if (b0 != nullptr) {
                const float target = getTarget(0, i, externalTargets);
                float current = gainChannels_[static_cast<size_t>(c)][0];
                const float alpha = (target > current) ? alphaOpen_[0] : alphaClose_[0];
                current = alpha * current + (1.0f - alpha) * target;
                gainChannels_[static_cast<size_t>(c)][0] = current;
                sum += current * b0[i];
            }

            if (b1 != nullptr) {
                const float target = getTarget(1, i, externalTargets);
                float current = gainChannels_[static_cast<size_t>(c)][1];
                const float alpha = (target > current) ? alphaOpen_[1] : alphaClose_[1];
                current = alpha * current + (1.0f - alpha) * target;
                gainChannels_[static_cast<size_t>(c)][1] = current;
                sum += current * b1[i];
            }

            if (b2 != nullptr) {
                const float target = getTarget(2, i, externalTargets);
                float current = gainChannels_[static_cast<size_t>(c)][2];
                const float alpha = (target > current) ? alphaOpen_[2] : alphaClose_[2];
                current = alpha * current + (1.0f - alpha) * target;
                gainChannels_[static_cast<size_t>(c)][2] = current;
                sum += current * b2[i];
            }

            outChannel[i] = sum;
        }
    }
}

void MultibandGate::updateAlpha(int band) noexcept {
    const auto idx = static_cast<size_t>(band);
    alphaOpen_[idx] = calculateAlpha(openTimeMs_[idx]);
    alphaClose_[idx] = calculateAlpha(closeTimeMs_[idx]);
}

float MultibandGate::calculateAlpha(float timeMs) const noexcept {
    if (sampleRate_ <= 0.0 || timeMs <= 0.0f) {
        return 0.0f;
    }
    const double tauSeconds = static_cast<double>(timeMs) * 0.001;
    return static_cast<float>(std::exp(-1.0 / (sampleRate_ * tauSeconds)));
}

float MultibandGate::getTarget(int band, int sampleIndex, const float* const* externalTargets) const noexcept {
    float S = 1.0f;
    const auto idx = static_cast<size_t>(band);

    if (externalTargets != nullptr && externalTargets[band] != nullptr) {
        S = externalTargets[band][sampleIndex];
    } else if (useSampleWiseTargets_) {
        S = targetBuffer_[idx][static_cast<size_t>(sampleIndex)];
    } else {
        S = rawTargetGain_[idx];
    }

    // Check solo rules
    bool anySolo = false;
    for (int b = 0; b < numBands_; ++b) {
        if (solo_[static_cast<size_t>(b)]) {
            anySolo = true;
            break;
        }
    }

    if (anySolo && !solo_[idx]) {
        return 0.0f; // Mute this band because another is soloed
    }

    if (mute_[idx]) {
        return 0.0f; // Explicit mute
    }

    // Apply curve shape: g_shaped = g_floor + (1 - g_floor) * S^gamma
    const float floorDb = floorDb_[idx];
    const float g_floor = std::pow(10.0f, floorDb * 0.05f); // 10^(floorDb/20)
    const float gamma = gamma_[idx];
    const float S_shaped = std::pow(std::max(0.0f, S), gamma);
    const float target = g_floor + (1.0f - g_floor) * S_shaped;

    return std::clamp(target, 0.0f, 1.0f);
}

} // namespace ChugGate::DSP
