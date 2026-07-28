#pragma once

#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {

/**
 * @class MultibandGate
 * @brief Applies per-band target gains with independent open/close smoothing
 *        and recombines bands to a single output.
 * 
 * API Choice:
 * - We take externally split band buffers (band0, band1, band2) rather than owning
 *   a CrossoverSplitter member, allowing the splitter to be shared across topologies
 *   or processed externally.
 * - Supports up to 3 bands (Weight / Voice / Hiss or Low / Mid / High).
 * - Realtime safe (no heap allocations or locking in the process path after prepare).
 */
class MultibandGate {
public:
    MultibandGate() noexcept;
    ~MultibandGate() = default;

    /**
     * @brief Preallocates internal buffers and sets processing parameters.
     * @param sampleRate The audio sample rate in Hz.
     * @param maxBlockSize The maximum expected block size in samples.
     * @param numChannels The number of audio channels (e.g. 1 for mono, 2 for stereo).
     * @param maxBands The maximum number of bands supported (clamped to [1, 3]).
     */
    void prepare(double sampleRate, int maxBlockSize, int numChannels, int maxBands = 3);
    
    /**
     * @brief Resets the smoothed gains to 1.0 (fully open) and clears the target buffers.
     *        Realtime-safe.
     */
    void reset() noexcept;

    // --- Configuration API ---
    
    /**
     * @brief Sets the active number of bands (1, 2, or 3).
     */
    void setNumBands(int numBands) noexcept;
    
    /**
     * @brief Gets the active number of bands.
     */
    [[nodiscard]] int getNumBands() const noexcept { return numBands_; }

    /**
     * @brief Sets the linear raw target gain S_b for a specific band.
     */
    void setBandTargetGain(int band, float targetGainLinear) noexcept;

    /**
     * @brief Sets the gain floor G_floor for a band in decibels (0 to -80 dB).
     */
    void setBandFloorDb(int band, float floorDb) noexcept;

    /**
     * @brief Sets the open time constant (attack time) tau_open_b in milliseconds.
     */
    void setBandOpenTime(int band, float openTimeMs) noexcept;

    /**
     * @brief Sets the close time constant (release time) tau_close_b in milliseconds.
     */
    void setBandCloseTime(int band, float closeTimeMs) noexcept;
    void setBandHoldTime(int band, float holdTimeMs) noexcept;

    /**
     * @brief Sets whether a specific band is muted (forces its target gain to 0.0).
     */
    void setBandMute(int band, bool shouldMute) noexcept;

    /**
     * @brief Sets whether a specific band is soloed (mutes all other bands).
     */
    void setBandSolo(int band, bool shouldSolo) noexcept;

    /**
     * @brief Sets the global curve shape gamma parameter (Natural/Firm/Clamp).
     *        Natural: gamma < 1.0 (e.g. 0.5)
     *        Firm: gamma = 1.0 (linear)
     *        Clamp: gamma > 1.0 (e.g. 2.0)
     */
    void setCurveGamma(float gamma) noexcept;
    
    /**
     * @brief Sets the curve shape gamma parameter for a specific band.
     */
    void setCurveGamma(int band, float gamma) noexcept;

    /**
     * @brief Sets the threshold offset in dB for a band (used in processTargets).
     */
    void setBandThresholdOffset(int band, float offsetDb) noexcept;
    void setBandDetectorWeighting(int band, float weighting) noexcept;

    /**
     * @brief Returns the current smoothed gain for a band (channel 0).
     */
    [[nodiscard]] float getBandGain(int band) const noexcept;

    /**
     * @brief Returns the current smoothed gain for a band and channel.
     */
    [[nodiscard]] float getBandGain(int band, int channel) const noexcept;

    // --- Process API ---

    /**
     * @brief Computes sample-wise target gains from log-energy and thresholds,
     *        storing them internally for the next process() call.
     */
    void processTargets(const float* logEnergy, float globalThresholdOpen, float globalHysteresis, int numSamples) noexcept;

    /**
     * @brief Recombines pre-split band buffers into a single output buffer.
     *        Uses targets set via setBandTargetGain or computed in processTargets.
     */
    void process(const float* const* band0,
                 const float* const* band1,
                 const float* const* band2,
                 float* const* output,
                 int numChannels,
                 int numSamples) noexcept;

    /**
     * @brief Recombines pre-split band buffers with explicit sample-wise external targets.
     */
    void process(const float* const* band0,
                 const float* const* band1,
                 const float* const* band2,
                 float* const* output,
                 const float* const* externalTargets,
                 int numChannels,
                 int numSamples) noexcept;

private:
    void updateAlpha(int band) noexcept;
    [[nodiscard]] float calculateAlpha(float timeMs) const noexcept;
    [[nodiscard]] float getTarget(int band, int sampleIndex, const float* const* externalTargets) const noexcept;

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    int numChannels_ = 0;
    int maxBands_ = 3;
    int numBands_ = 1;

    // Parameters per band
    std::array<float, 3> rawTargetGain_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> floorDb_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> openTimeMs_{5.0f, 5.0f, 5.0f};
    std::array<float, 3> closeTimeMs_{100.0f, 100.0f, 100.0f};
    std::array<float, 3> holdTimeMs_{0.0f, 0.0f, 0.0f};
    std::array<bool, 3> mute_{false, false, false};
    std::array<bool, 3> solo_{false, false, false};
    std::array<float, 3> gamma_{1.0f, 1.0f, 1.0f};
    std::array<float, 3> thresholdOffsetDb_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> detectorWeighting_{1.0f, 1.0f, 1.0f};

    // Precalculated smoothing coefficients
    std::array<float, 3> alphaOpen_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> alphaClose_{0.0f, 0.0f, 0.0f};

    // Smoothing states: gainChannels_[channel][band]
    std::vector<std::array<float, 3>> gainChannels_;
    std::vector<std::array<int, 3>> holdSamplesRemaining_;

    // Sample-wise target buffers
    std::vector<float> targetBuffer_[3];
    bool useSampleWiseTargets_ = false;

    static constexpr float eps = 1e-10f;
};

} // namespace ChugGate::DSP
