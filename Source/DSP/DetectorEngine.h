#pragma once

#include <atomic>

namespace ChugGate::DSP {

class DetectorEngine {
public:
    DetectorEngine() noexcept;
    ~DetectorEngine() = default;

    void prepare(double sampleRate, int maxBlockSize = 0) noexcept;
    void reset() noexcept;

    // Process a block of samples.
    // monoKey: input key samples
    // envelopeOut: outputs linear envelope (if not nullptr)
    // levelDbOut: outputs level in dB (if not nullptr)
    // numSamples: number of samples to process
    void process(const float* monoKey, float* envelopeOut, float* levelDbOut, int numSamples) noexcept;

    // Overload of process that processes mono key samples in-place
    void process(float* monoKey, int numSamples) noexcept;

    // Single sample processing. Returns the linear envelope E[n].
    // Optional parameter to pass in pre-conditioned key or hum-immunity adjusted energy magnitude
    float processSample(float sample, float overrideMagnitude = -1.0f) noexcept;

    // Parameter setters (realtime-safe)
    void setPeakWeight(float weight) noexcept;
    void setAttackTime(float attackMs) noexcept;
    void setReleaseTime(float releaseMs) noexcept;
    void setPeakRelease(float peakReleaseMs) noexcept;
    void setRmsWindow(float rmsWindowMs) noexcept;

    // Getters for current state
    [[nodiscard]] float getEnvelope() const noexcept;
    [[nodiscard]] float getLevelDb() const noexcept;

private:
    void updateTimeConstants() noexcept;
    [[nodiscard]] float calculateAlpha(float timeMs) const noexcept;

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;

    // Parameters
    std::atomic<float> peakWeight_{0.5f};      // w_p
    std::atomic<float> attackTimeMs_{10.0f};   // tau_a (detector attack)
    std::atomic<float> releaseTimeMs_{100.0f}; // tau_r (detector release)
    std::atomic<float> peakReleaseMs_{50.0f};  // tau_p (peak release)
    std::atomic<float> rmsWindowMs_{10.0f};    // tau_r_rms (RMS window)

    // Precomputed coefficients (updated in process/processSample if dirty)
    float alphaP_ = 0.0f;
    float alphaR_rms_ = 0.0f;
    float alphaA_ = 0.0f;
    float alphaR_ = 0.0f;
    std::atomic<bool> coefficientsDirty_{true};

    // State variables (all accessed on audio thread only)
    float lastPeak_ = 0.0f;
    float lastRmsSquared_ = 0.0f;
    float lastEnvelope_ = 0.0f;
    float lastLevelDb_ = -100.0f;

    static constexpr float eps = 1e-10f; // Prevent log10(0) and division by zero
};

} // namespace ChugGate::DSP
