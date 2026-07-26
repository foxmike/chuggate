// =============================================================================
// ChugGate - DSP Detector/Key-Path Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase B detector-engine and
// key-conditioning maths described in .abracapocus/CHUGGATE_SPEC.md sections
// 4-6:
//   * KeySourceRouter routing fallback + envelope blend endpoints/midpoint
//   * KeyConditioner HPF attenuation of sub-cutoff content
//   * PowerlineShield notch + 50/60 Hz auto-detect
//   * Hum immunity: E_musical = max(0, E_key - eta * E_hum)
//   * DetectorEngine attack/release ballistics, peak/rms weighting, log level
//
// All fixtures are deterministic: fixed sample rate, fixed block lengths, and
// explicit tolerances. No audio-thread assumptions are made inside the tests.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "../Source/DSP/KeySourceRouter.h"
#include "../Source/DSP/KeyConditioner.h"
#include "../Source/DSP/PowerlineShield.h"
#include "../Source/DSP/DetectorEngine.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 64;

// Tolerances are deliberately loose where IIR settling / cross-frequency
// leakage is involved, but tight where the maths is exact.
constexpr float kExactTol = 1.0e-4f;
constexpr float kBlendTol = 1.0e-3f;
constexpr float kLevelTolDb = 0.15f;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

std::vector<float> makeConstant(int numSamples, float amplitude)
{
    return std::vector<float>(static_cast<std::size_t>(numSamples), amplitude);
}
// Coherent sine: numCycles must be integral so the RMS over the window is the
// theoretical A / sqrt(2) (no spectral leakage).
std::vector<float> makeSine(int numSamples, double sampleRate, double frequency,
                            double amplitude, double phase = 0.0)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples));
    const double twoPi = 6.28318530717958647692;
    for (int i = 0; i < numSamples; ++i)
        buffer[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * std::sin(twoPi * frequency * i / sampleRate + phase));
    return buffer;
}

// RMS over the trailing portion of a buffer (skips IIR transient).
float trailingRms(const std::vector<float> &buffer, double skipRatio)
{
    const std::size_t start = static_cast<std::size_t>(buffer.size() * skipRatio);
    if (start >= buffer.size())
        return 0.0f;
    double sumSq = 0.0;
    std::size_t count = 0;
    for (std::size_t i = start; i < buffer.size(); ++i)
    {
        sumSq += static_cast<double>(buffer[i]) * buffer[i];
        ++count;
    }
    return count > 0 ? static_cast<float>(std::sqrt(sumSq / count)) : 0.0f;
}

float peakAbs(const std::vector<float> &buffer)
{
    float peak = 0.0f;
    for (const auto v : buffer)
        peak = std::max(peak, std::fabs(v));
    return peak;
}

// ---------------------------------------------------------------------------
// A juce::UnitTest subclass registered via a file-scope static instance.
// ---------------------------------------------------------------------------

class DetectorTests final : public juce::UnitTest
{
public:
    DetectorTests() : juce::UnitTest("ChugGate Detector Tests", "DSP") {}

    void runTest() override
    {
        beginTest("KeySourceRouter: missing sidechain activates internal fallback");
        runKeySourceRouterMissingSidechain();

        beginTest("KeySourceRouter: envelope blend endpoints and midpoint");
        runKeySourceRouterBlend();

        beginTest("KeyConditioner: HPF attenuates sub-cutoff sine");
        runKeyConditionerHpf();

        beginTest("PowerlineShield: 60 Hz notch attenuates hum fundamental");
        runPowerlineShieldNotch();

        beginTest("PowerlineShield: auto-detect selects stronger of 50/60 Hz");
        runPowerlineShieldAutoDetect();

        beginTest("Hum immunity: E_musical drops with large E_hum");
        runHumImmunity();

        beginTest("DetectorEngine: attack rise and release decay");
        runDetectorEngineAttackRelease();

        beginTest("DetectorEngine: peak-only vs rms-only weighting");
        runDetectorEngineWeights();

        beginTest("DetectorEngine: log level L ~ 20*log10(E+eps) on steady fixture");
        runDetectorEngineLogLevel();
    }

private:
    // ---- KeySourceRouter ---------------------------------------------------

    void runKeySourceRouterMissingSidechain()
    {
        KeySourceRouter router;
        router.prepare(kSampleRate, kBlockSize, 2, 0);

        // Main signal present, no sidechain buffer at all.
        auto main = makeConstant(kBlockSize, 0.5f);
        float *mainChannels[2] = { main.data(), nullptr };

        std::vector<float> key(static_cast<std::size_t>(kBlockSize), -1.0f);

        router.setSource(KeySourceRouter::Source::ExternalSidechain);
        router.setFallbackMode(KeySourceRouter::FallbackMode::InternalMain);
        router.process(mainChannels, 2, nullptr, 0, key.data(), kBlockSize);

        const auto &status = router.getStatus();

        // Spec 4.3: never silently treat a missing sidechain as a valid key.
        expect(! status.externalPresent,
               "Missing sidechain must not report externalPresent=true");
        expect(status.internalFallbackActive,
               "Missing external key must activate internal fallback");
        expect(status.lowConfidence,
               "Missing external key must flag low confidence");

        // InternalMain fallback must reproduce the main envelope, not silence.
        expect(std::fabs(key[0] - 0.5f) < kExactTol,
               "InternalMain fallback should follow main average envelope");
        expect(peakAbs(key) > 0.0f,
               "Fallback output must not be silent with main present");

        // CloseGate fallback should zero the key path entirely.
        std::fill(key.begin(), key.end(), -1.0f);
        router.setFallbackMode(KeySourceRouter::FallbackMode::CloseGate);
        router.process(mainChannels, 2, nullptr, 0, key.data(), kBlockSize);
        expect(peakAbs(key) < kExactTol,
               "CloseGate fallback must produce a silent key path");
    }

    void runKeySourceRouterBlend()
    {
        // Spec 4.2: E_K[n] = (1-mu)*E_I[n] + mu*E_E[n], mu in [0,1].
        // Constant-envelope fixture: internal abs avg = 0.5, external = 1.0.
        KeySourceRouter router;
        router.prepare(kSampleRate, kBlockSize, 1, 1);

        auto main = makeConstant(kBlockSize, 0.5f);
        auto ext = makeConstant(kBlockSize, 1.0f);
        float *mainChannels[1] = { main.data() };
        float *extChannels[1] = { ext.data() };

        auto runBlend = [&](float mu) {
            std::vector<float> key(static_cast<std::size_t>(kBlockSize), -1.0f);
            router.setSource(KeySourceRouter::Source::BlendedMainExternal);
            router.setExternalBlend(mu);
            router.setFallbackMode(KeySourceRouter::FallbackMode::InternalMain);
            router.process(mainChannels, 1, extChannels, 1, key.data(), kBlockSize);
            return key;
        };

        const auto k0 = runBlend(0.0f);
        expect(router.getStatus().externalPresent,
               "External must be present for blend test");
        expect(std::fabs(k0[0] - 0.5f) < kBlendTol,
               "mu=0 must yield internal envelope (0.5)");

        const auto k1 = runBlend(1.0f);
        expect(std::fabs(k1[0] - 1.0f) < kBlendTol,
               "mu=1 must yield external envelope (1.0)");

        const auto kMid = runBlend(0.5f);
        expect(std::fabs(kMid[0] - 0.75f) < kBlendTol,
               "mu=0.5 must yield midpoint envelope (0.75)");

        // Static helper sanity check on the same fixture.
        std::vector<float> internalEnv(static_cast<std::size_t>(kBlockSize), 0.5f);
        std::vector<float> externalEnv(static_cast<std::size_t>(kBlockSize), 1.0f);
        std::vector<float> blended(static_cast<std::size_t>(kBlockSize), -1.0f);
        KeySourceRouter::blendEnvelopes(internalEnv.data(), externalEnv.data(),
                                        blended.data(), kBlockSize, 0.5f);
        expect(std::fabs(blended[0] - 0.75f) < kExactTol,
               "blendEnvelopes static helper midpoint must be exact");
    }

    // ---- KeyConditioner ----------------------------------------------------

    void runKeyConditionerHpf()
    {
        // Spec 5.1: HPF attenuates content below the cutoff frequency.
        KeyConditioner cond;
        cond.prepare(kSampleRate, kBlockSize);
        cond.setHpf(true, 100.0f, FilterSlope::Db12);
        cond.setLpf(false, 20000.0f, FilterSlope::Db12);
        cond.setEmphasis(EmphasisMode::Off, 1000.0f, 0.707f, 0.0f, false);

        // Long enough for the IIR to settle, integral cycles for clean RMS.
        const int numSamples = static_cast<int>(kSampleRate); // 1 second
        auto subCutoff = makeSine(numSamples, kSampleRate, 30.0, 1.0);
        auto passband = makeSine(numSamples, kSampleRate, 500.0, 1.0);

        std::vector<float> subOut(subCutoff.size());
        std::vector<float> passOut(passband.size());
        cond.process(subCutoff.data(), subOut.data(), numSamples);
        cond.process(passband.data(), passOut.data(), numSamples);

        const float subRms = trailingRms(subOut, 0.5);
        const float passRms = trailingRms(passOut, 0.5);

        expect(passRms > 0.5f,
               "Passband sine (~500 Hz) should pass HPF near unity (RMS ~0.707)");
        expect(subRms < 0.25f * passRms,
               "Sub-cutoff sine (~30 Hz) must be attenuated well below passband");
        expect(subRms < passRms,
               "Sub-cutoff content must be attenuated relative to passband");
    }

    // ---- PowerlineShield ---------------------------------------------------

    void runPowerlineShieldNotch()
    {
        // Spec 5.4: notch at the hum fundamental attenuates it strongly while a
        // nearby musical tone passes.
        PowerlineShield shield;
        shield.prepare(kSampleRate, kBlockSize);
        shield.setHumMode(HumMode::Hz60);
        shield.setHarmonics(1);
        shield.setNotchQ(20.0f);

        const int numSamples = static_cast<int>(kSampleRate); // 1 second
        // 60 Hz: 60 cycles in 1 s (integral). 80 Hz: 80 cycles (integral).
        auto hum = makeSine(numSamples, kSampleRate, 60.0, 1.0);
        auto musical = makeSine(numSamples, kSampleRate, 80.0, 1.0);

        std::vector<float> humOut(hum.size());
        std::vector<float> musOut(musical.size());
        shield.process(hum.data(), humOut.data(), numSamples);
        shield.process(musical.data(), musOut.data(), numSamples);

        const float humRms = trailingRms(humOut, 0.5);
        const float musRms = trailingRms(musOut, 0.5);

        expect(musRms > 0.7f,
               "Nearby musical tone (80 Hz) should pass the 60 Hz notch");
        expect(humRms < 0.2f,
               "Hum fundamental (60 Hz) must be deeply notched");
        expect(humRms < 0.3f * musRms,
               "Notch must attenuate hum far more than a nearby musical tone");
    }

    void runPowerlineShieldAutoDetect()
    {
        // Spec 5.4: auto mode compares 50/60 Hz energy and picks the stronger.
        const int numSamples = static_cast<int>(kSampleRate * 0.5); // 0.5 s

        // Biased toward 50 Hz: pure 50 Hz fixture.
        {
            PowerlineShield shield;
            shield.prepare(kSampleRate, kBlockSize);
            shield.setHumMode(HumMode::Auto);
            shield.setHarmonics(1);

            auto fixture = makeSine(numSamples, kSampleRate, 50.0, 1.0);
            std::vector<float> out(fixture.size());
            shield.process(fixture.data(), out.data(), numSamples);

            expectEquals(static_cast<int>(shield.getChosenMode()),
                         static_cast<int>(HumMode::Hz50),
                         "Auto-detect must prefer 50 Hz on a 50 Hz-biased fixture");
            expect(shield.getAutoDetectConfidence() > 0.0f,
                   "Auto-detect confidence must be non-zero after sustained hum");
        }

        // Biased toward 60 Hz: pure 60 Hz fixture.
        {
            PowerlineShield shield;
            shield.prepare(kSampleRate, kBlockSize);
            shield.setHumMode(HumMode::Auto);
            shield.setHarmonics(1);

            auto fixture = makeSine(numSamples, kSampleRate, 60.0, 1.0);
            std::vector<float> out(fixture.size());
            shield.process(fixture.data(), out.data(), numSamples);

            expectEquals(static_cast<int>(shield.getChosenMode()),
                         static_cast<int>(HumMode::Hz60),
                         "Auto-detect must prefer 60 Hz on a 60 Hz-biased fixture");
        }
    }

    // ---- Hum immunity ------------------------------------------------------

    void runHumImmunity()
    {
        // Spec 5.5: E_musical = max(0, E_key - eta * E_hum).
        constexpr float eta = 0.5f;
        constexpr float eKey = 1.0f;

        const float small = PowerlineShield::calculateMusicalEnergy(eKey, 0.2f, eta);
        const float large = PowerlineShield::calculateMusicalEnergy(eKey, 3.0f, eta);

        expect(std::fabs(small - 0.9f) < kExactTol,
               "Small hum: E_musical = 1.0 - 0.5*0.2 = 0.9");
        expectEquals(large, 0.0f,
                     "Large hum must clamp E_musical to zero (max(0, 1.0 - 1.5))");
        expect(large < small,
               "E_musical must drop when E_hum grows for fixed E_key and eta");

        // Instance-level API honours the configured eta.
        PowerlineShield shield;
        shield.prepare(kSampleRate, kBlockSize);
        shield.setHumImmunityAmount(eta);
        const float instance = shield.applyHumImmunity(eKey, 0.4f);
        expect(std::fabs(instance - 0.8f) < kExactTol,
               "applyHumImmunity must use the configured eta (1.0 - 0.5*0.4 = 0.8)");
    }

    // ---- DetectorEngine ----------------------------------------------------

    void runDetectorEngineAttackRelease()
    {
        // Spec 6.2: envelope rises on attack when |x| > E_prev, decays on
        // release after the signal drops.
        DetectorEngine engine;
        engine.prepare(kSampleRate, kBlockSize);
        engine.setPeakWeight(0.5f);
        engine.setAttackTime(2.0f);
        engine.setReleaseTime(50.0f);
        engine.setPeakRelease(20.0f);
        engine.setRmsWindow(5.0f);
        engine.reset();

        // Step from silence to amplitude 1.0; envelope must rise over time.
        const int attackSamples = 200;
        std::vector<float> key(static_cast<std::size_t>(attackSamples), 1.0f);
        std::vector<float> env(static_cast<std::size_t>(attackSamples), 0.0f);
        engine.process(key.data(), env.data(), nullptr, attackSamples);

        expect(env[0] > 0.0f, "First attack sample must be non-zero");
        expect(env[attackSamples - 1] > env[0],
               "Envelope must rise during a sustained step (attack)");
        expect(env[attackSamples - 1] > 0.5f,
               "Envelope must approach the step amplitude after sustained attack");

        const float peakEnvelope = env[attackSamples - 1];

        // Drop to silence; envelope must release downward.
        const int releaseSamples = static_cast<int>(kSampleRate * 0.3);
        std::vector<float> silence(static_cast<std::size_t>(releaseSamples), 0.0f);
        std::vector<float> relEnv(static_cast<std::size_t>(releaseSamples), 0.0f);
        engine.process(silence.data(), relEnv.data(), nullptr, releaseSamples);

        expect(relEnv[releaseSamples - 1] < peakEnvelope,
               "Envelope must decay after signal drops (release)");
        expect(relEnv[releaseSamples - 1] < relEnv[0],
               "Envelope must keep decaying throughout the silent block");
    }

    void runDetectorEngineWeights()
    {
        // Spec 6.1: E = w_p*P + (1-w_p)*R. For a sine, P > R, so peak-only
        // weighting yields a higher steady envelope than rms-only.
        const int numSamples = static_cast<int>(kSampleRate); // 1 second, 200 cycles
        auto sine = makeSine(numSamples, kSampleRate, 200.0, 1.0);

        auto steadyEnvelope = [&](float weight) {
            DetectorEngine engine;
            engine.prepare(kSampleRate, kBlockSize);
            engine.setPeakWeight(weight);
            engine.setAttackTime(5.0f);
            engine.setReleaseTime(100.0f);
            engine.setPeakRelease(50.0f);
            engine.setRmsWindow(20.0f);
            engine.reset();
            std::vector<float> env(static_cast<std::size_t>(numSamples), 0.0f);
            engine.process(sine.data(), env.data(), nullptr, numSamples);
            return env.back();
        };

        const float peakOnly = steadyEnvelope(1.0f);
        const float rmsOnly = steadyEnvelope(0.0f);

        expect(peakOnly > 0.85f,
               "Peak-only weighting should track near the sine peak (~1.0)");
        expect(rmsOnly < 0.85f,
               "Rms-only weighting should track the sine RMS (~0.707)");
        expect(peakOnly > rmsOnly,
               "Peak-only envelope must exceed rms-only envelope for a sine");
    }

    void runDetectorEngineLogLevel()
    {
        // Spec 6.3: L[n] = 20*log10(E[n] + eps). Steady DC of amplitude 0.5
        // settles E to 0.5, so L ~ 20*log10(0.5) = -6.0206 dB.
        DetectorEngine engine;
        engine.prepare(kSampleRate, kBlockSize);
        engine.setPeakWeight(0.5f);
        engine.setAttackTime(2.0f);
        engine.setReleaseTime(50.0f);
        engine.setPeakRelease(20.0f);
        engine.setRmsWindow(5.0f);
        engine.reset();

        const int numSamples = static_cast<int>(kSampleRate); // plenty to settle
        auto dc = makeConstant(numSamples, 0.5f);
        std::vector<float> env(static_cast<std::size_t>(numSamples), 0.0f);
        std::vector<float> levelDb(static_cast<std::size_t>(numSamples), 0.0f);
        engine.process(dc.data(), env.data(), levelDb.data(), numSamples);

        constexpr float kDetectorEps = 1e-10f; // mirrors DetectorEngine::eps
        const float expectedLevelDb =
            20.0f * std::log10(0.5f + kDetectorEps);
        const float actualLevelDb = levelDb.back();

        expect(std::fabs(actualLevelDb - expectedLevelDb) < kLevelTolDb,
               "Steady log level must match 20*log10(E+eps) within tolerance");
        expect(std::fabs(env.back() - 0.5f) < 1.0e-2f,
               "Steady envelope must converge to the DC amplitude");
    }
};

// Static instance registers the test with juce::UnitTestRunner.
static DetectorTests s_detectorTestsInstance;

} // namespace
