#pragma once

#include <atomic>

namespace ChugGate::DSP {

/**
 * A pair of stereo values (envelopes or gains) for Left and Right channels.
 */
struct StereoPair {
    float left = 0.0f;
    float right = 0.0f;
};

/**
 * StereoLinker implements a control-rate stereo link matrix for detector envelopes
 * and per-channel gains per CHUGGATE_SPEC §18.
 *
 * It supports four modes:
 * - Max: E_link = max(E_L, E_R)
 * - Energy: E_link = sqrt((E_L^2 + E_R^2) / 2)
 * - Average: E_link = (E_L + E_R) / 2
 * - DualMono: independent L/R gains and envelopes (no linking)
 *
 * For modes other than DualMono, per-channel gains can be blended with a shared gain:
 * G_L' = lambda * G_linked + (1-lambda) * G_L
 * G_R' = lambda * G_linked + (1-lambda) * G_R
 * where lambda is the link amount.
 *
 * Under DualMono, lambda has no effect on gain or envelope processing.
 *
 * Mono Safety Warning (§18.3): Exposes a warning flag if the L/R gains or envelopes
 * diverge above a threshold (default 0.15) for a significant portion of a 1.0 second window.
 */
class StereoLinker {
public:
    enum class LinkMode {
        Max = 0,
        Energy = 1,
        Average = 2,
        DualMono = 3
    };

    // Constants for Mono Safety tracking
    static constexpr float divergenceThreshold = 0.15f;          // Gain divergence threshold (~1.5 dB)
    static constexpr float warningProportionThreshold = 0.20f;   // Warning trigger if >20% of window is diverged

    StereoLinker() noexcept;
    ~StereoLinker() = default;

    /**
     * Prepares the stereo linker with sample rate, block size, and number of channels.
     * This method must be called before processing starts.
     */
    void prepare(double sampleRate, int maxBlockSize, int numChannels) noexcept;

    /**
     * Resets the internal state and the Mono Safety tracking history.
     */
    void reset() noexcept;

    /**
     * Sets the stereo linking mode.
     */
    void setMode(LinkMode mode) noexcept;
    void setMode(int modeIndex) noexcept;

    /**
     * Gets the current stereo linking mode.
     */
    [[nodiscard]] LinkMode getMode() const noexcept { return mode_.load(std::memory_order_relaxed); }

    /**
     * Sets the link percentage (lambda) in the range [0.0, 1.0].
     */
    void setLinkAmount(float amount) noexcept;

    /**
     * Gets the current link percentage.
     */
    [[nodiscard]] float getLinkAmount() const noexcept { return linkAmount_.load(std::memory_order_relaxed); }

    /**
     * Process L/R detector envelopes.
     * - Max/Energy/Average: returns { E_link, E_link }
     * - DualMono: returns { envL, envR } and tracks envelope divergence.
     */
    [[nodiscard]] StereoPair linkEnvelopes(float envL, float envR) noexcept;

    /**
     * Process L/R gains by blending independent gains with the shared/linked gain.
     * - Max/Energy/Average: G_channel' = lambda * G_linked + (1-lambda) * G_channel
     * - DualMono: returns { gainL, gainR } unchanged.
     * Updates the Mono Safety tracking using the absolute difference of the output gains.
     */
    [[nodiscard]] StereoPair linkGains(float gainL, float gainR, float gainLinked) noexcept;

    /**
     * Returns true if the L/R channels have frequently diverged in DualMono or during partial linking.
     */
    [[nodiscard]] bool isMonoSafetyWarning() const noexcept;

    /**
     * Returns the smoothed divergence metric (running average of absolute difference).
     */
    [[nodiscard]] float getDivergenceMetric() const noexcept;

private:
    void updateDivergence(float diffValue) noexcept;

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    int numChannels_ = 2;

    std::atomic<LinkMode> mode_{LinkMode::Max};
    std::atomic<float> linkAmount_{1.0f};

    // Mono Safety tracking state (EMA smoothing filters)
    float alpha_ = 0.99f;
    std::atomic<float> divergenceMetric_{0.0f};
    std::atomic<float> divergenceAboveThresholdSmoothed_{0.0f};
};

} // namespace ChugGate::DSP
