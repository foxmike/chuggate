// =============================================================================
// ChugGate - Lookahead Delay Unit Tests
// =============================================================================
// Focused juce::UnitTest cases covering the Phase G maths described in
// .abracapocus/CHUGGATE_SPEC.md section 17 (Lookahead):
//
//   * Default Off: getLatencySamples()==0 and process() is a bit-exact
//     pass-through within float epsilon on both an impulse and a sine.
//   * Each discrete legal ms value at fs=48000 reports
//     latencySamples == round(tau_L * fs) and delays an impulse peak to the
//     expected output index (±1 sample tolerance).
//   * Illegal / midpoint lookahead requests snap (or clamp) to a legal
//     setting; the documented behavior is the nearest-legal snap implemented
//     by LookaheadDelay::snapToLookahead, with ties resolved to the smaller
//     legal value because the scan uses strict less-than comparison.
//   * Finite output / no NaNs when the lookahead setting is changed
//     mid-stream over a short block span (5 ms equal-time crossfade).
//
// All fixtures are deterministic: fixed sample rate (48000), fixed block
// lengths, and explicit numeric tolerances. No audio hardware is touched.
// =============================================================================

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "../Source/DSP/LookaheadDelay.h"

namespace {

using namespace ChugGate::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 128;
constexpr int kNumChannels = 1;

constexpr float kExactTol = 1.0e-6f;
constexpr float kEpsilonTol = 1.0e-5f;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

std::vector<float> makeImpulse(int numSamples, int impulseIndex, float amplitude)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples), 0.0f);
    if (impulseIndex >= 0 && impulseIndex < numSamples)
        buffer[static_cast<std::size_t>(impulseIndex)] = amplitude;
    return buffer;
}

std::vector<float> makeSine(int numSamples, double sampleRate, double frequency,
                            double amplitude)
{
    std::vector<float> buffer(static_cast<std::size_t>(numSamples));
    const double twoPi = 6.28318530717958647692;
    for (int i = 0; i < numSamples; ++i)
        buffer[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * std::sin(twoPi * frequency * i / sampleRate));
    return buffer;
}

bool allFinite(const std::vector<float> &buffer)
{
    for (const auto v : buffer)
        if (!std::isfinite(v))
            return false;
    return true;
}

int argMaxAbs(const std::vector<float> &buffer)
{
    int index = 0;
    float best = -1.0f;
    for (int i = 0; i < static_cast<int>(buffer.size()); ++i)
    {
        const float a = std::fabs(buffer[static_cast<std::size_t>(i)]);
        if (a > best)
        {
            best = a;
            index = i;
        }
    }
    return index;
}

// ---------------------------------------------------------------------------
// A juce::UnitTest subclass registered via a file-scope static instance.
// ---------------------------------------------------------------------------

class LookaheadTests final : public juce::UnitTest
{
public:
    LookaheadTests() : juce::UnitTest("ChugGate Lookahead Tests", "DSP") {}

    void runTest() override
    {
        beginTest("Default Off: zero latency and pass-through on impulse");
        runDefaultOffImpulse();

        beginTest("Default Off: pass-through on sine within float epsilon");
        runDefaultOffSine();

        beginTest("Legal ms settings report round(tau*fs) latency");
        runLegalLatencySamples();

        beginTest("Legal ms settings delay impulse peak to expected index");
        runLegalImpulseDelay();

        beginTest("Illegal midpoint snaps to nearest legal setting");
        runIllegalSnap();

        beginTest("Mid-stream lookahead changes stay finite / no NaNs");
        runMidStreamChangeFinite();
    }

private:
    // ---- Default Off -------------------------------------------------------

    void runDefaultOffImpulse()
    {
        LookaheadDelay delay;
        delay.prepare(kSampleRate, kBlockSize, kNumChannels);

        expect(delay.getLookaheadEnum() == LookaheadDelay::Lookahead::Off,
               "Freshly constructed lookahead must default to Off");
        expect(delay.getLatencySamples() == 0,
               "Off lookahead must report zero latency samples");

        auto input = makeImpulse(kBlockSize, 0, 1.0f);
        std::vector<float> output(input.size(), 0.0f);
        float *inChannels[1] = { input.data() };
        float *outChannels[1] = { output.data() };
        delay.process(inChannels, outChannels, 1, kBlockSize);

        for (int i = 0; i < kBlockSize; ++i)
            expect(std::fabs(output[static_cast<std::size_t>(i)] - input[static_cast<std::size_t>(i)]) < kEpsilonTol,
                   "Off lookahead must be a pass-through (impulse)");
    }

    void runDefaultOffSine()
    {
        LookaheadDelay delay;
        delay.prepare(kSampleRate, kBlockSize, kNumChannels);
        delay.setLookaheadEnum(LookaheadDelay::Lookahead::Off);

        expect(delay.getLatencySamples() == 0,
               "Off lookahead must report zero latency samples");

        const int numSamples = 256;
        auto input = makeSine(numSamples, kSampleRate, 1000.0, 0.5);
        std::vector<float> output(input.size(), 0.0f);
        float *inChannels[1] = { input.data() };
        float *outChannels[1] = { output.data() };
        delay.process(inChannels, outChannels, 1, numSamples);

        for (int i = 0; i < numSamples; ++i)
            expect(std::fabs(output[static_cast<std::size_t>(i)] - input[static_cast<std::size_t>(i)]) < kEpsilonTol,
                   "Off lookahead must be a pass-through (sine)");
    }

    // ---- Legal latency samples --------------------------------------------

    void runLegalLatencySamples()
    {
        struct Setting
        {
            LookaheadDelay::Lookahead lookahead;
            float ms;
        };

        const Setting settings[] = {
            { LookaheadDelay::Lookahead::Off, 0.0f },
            { LookaheadDelay::Lookahead::Ms0_25, 0.25f },
            { LookaheadDelay::Lookahead::Ms0_5, 0.5f },
            { LookaheadDelay::Lookahead::Ms1, 1.0f },
            { LookaheadDelay::Lookahead::Ms2, 2.0f },
            { LookaheadDelay::Lookahead::Ms5, 5.0f },
            { LookaheadDelay::Lookahead::Ms10, 10.0f },
        };

        for (const auto &s : settings)
        {
            LookaheadDelay delay;
            delay.prepare(kSampleRate, kBlockSize, kNumChannels);
            delay.setLookaheadEnum(s.lookahead);

            const int expected = static_cast<int>(std::lround(s.ms * 0.001 * kSampleRate));
            const int actual = delay.getLatencySamples();

            expect(actual == expected,
                   "Latency samples must equal round(tau*fs) for ms=" + juce::String(s.ms)
                       + " (expected=" + juce::String(expected) + ", actual=" + juce::String(actual) + ")");

            expect(std::fabs(delay.getLookaheadMs() - s.ms) < kExactTol,
                   "getLookaheadMs must report the legal ms value");
        }
    }

    // ---- Legal impulse delay ----------------------------------------------

    void runLegalImpulseDelay()
    {
        struct Setting
        {
            LookaheadDelay::Lookahead lookahead;
            float ms;
        };

        const Setting settings[] = {
            { LookaheadDelay::Lookahead::Ms0_25, 0.25f },
            { LookaheadDelay::Lookahead::Ms0_5, 0.5f },
            { LookaheadDelay::Lookahead::Ms1, 1.0f },
            { LookaheadDelay::Lookahead::Ms2, 2.0f },
            { LookaheadDelay::Lookahead::Ms5, 5.0f },
            { LookaheadDelay::Lookahead::Ms10, 10.0f },
        };

        // Plenty of headroom past the largest delay (10 ms = 480 samples).
        const int numSamples = 1024;

        for (const auto &s : settings)
        {
            // Set the target before prepare() so the delay line initializes
            // its tap to the destination delay with no 5 ms crossfade
            // transition; otherwise the source tap (delay 0) dominates the
            // first 240 samples and the impulse peaks at index 0.
            LookaheadDelay delay;
            delay.setLookaheadEnum(s.lookahead);
            delay.prepare(kSampleRate, numSamples, kNumChannels);

            const int expectedLatency = static_cast<int>(std::lround(s.ms * 0.001 * kSampleRate));
            expect(delay.getLatencySamples() == expectedLatency,
                   "Latency must match round(tau*fs) before processing");

            auto input = makeImpulse(numSamples, 0, 1.0f);
            std::vector<float> output(input.size(), 0.0f);
            float *inChannels[1] = { input.data() };
            float *outChannels[1] = { output.data() };
            delay.process(inChannels, outChannels, 1, numSamples);

            const int peakIndex = argMaxAbs(output);
            expect(std::abs(peakIndex - expectedLatency) <= 1,
                   "Delayed impulse peak must appear at the latency index (±1) for ms="
                       + juce::String(s.ms) + " (expected=" + juce::String(expectedLatency)
                       + ", actual=" + juce::String(peakIndex) + ")");

            // Peak amplitude must survive the delay line intact (no transition
            // is active because prepare established a steady tap).
            expect(std::fabs(output[static_cast<std::size_t>(peakIndex)] - 1.0f) < kEpsilonTol,
                   "Delayed impulse peak amplitude must be preserved");
        }
    }

    // ---- Illegal / midpoint snap ------------------------------------------

    void runIllegalSnap()
    {
        // snapToLookahead selects the nearest legal value from
        // { 0, 0.25, 0.5, 1, 2, 5, 10 } ms. Ties are resolved to the smaller
        // value because the scan uses strict less-than comparison.
        LookaheadDelay delay;
        delay.prepare(kSampleRate, kBlockSize, kNumChannels);

        // 0.8 ms is closer to 1.0 ms than to 0.5 ms.
        delay.setLookaheadMs(0.8f);
        expect(std::fabs(delay.getLookaheadMs() - 1.0f) < kExactTol,
               "0.8 ms must snap to 1.0 ms");

        // 3.0 ms is closer to 2.0 ms than to 5.0 ms.
        delay.setLookaheadMs(3.0f);
        expect(std::fabs(delay.getLookaheadMs() - 2.0f) < kExactTol,
               "3.0 ms must snap to 2.0 ms");

        // 7.0 ms is closer to 5.0 ms than to 10.0 ms.
        delay.setLookaheadMs(7.0f);
        expect(std::fabs(delay.getLookaheadMs() - 5.0f) < kExactTol,
               "7.0 ms must snap to 5.0 ms");

        // Exact midpoint 0.75 ms (equidistant from 0.5 and 1.0) snaps down to
        // 0.5 ms under the strict less-than tie rule.
        delay.setLookaheadMs(0.75f);
        expect(std::fabs(delay.getLookaheadMs() - 0.5f) < kExactTol,
               "0.75 ms midpoint must snap down to 0.5 ms (strict < tie rule)");

        // Values above the maximum clamp to the maximum legal setting.
        delay.setLookaheadMs(25.0f);
        expect(std::fabs(delay.getLookaheadMs() - 10.0f) < kExactTol,
               "25.0 ms must snap/clamp to 10.0 ms");

        // Negative values snap to the nearest legal value (0.0 ms / Off).
        delay.setLookaheadMs(-1.0f);
        expect(std::fabs(delay.getLookaheadMs() - 0.0f) < kExactTol,
               "Negative ms must snap to 0.0 ms (Off)");

        // Non-finite values select Off per the documented contract.
        delay.setLookaheadMs(std::numeric_limits<float>::quiet_NaN());
        expect(delay.getLookaheadEnum() == LookaheadDelay::Lookahead::Off,
               "NaN ms must select Off");
        expect(delay.getLatencySamples() == 0,
               "NaN ms must yield zero latency");
    }

    // ---- Mid-stream change finiteness -------------------------------------

    void runMidStreamChangeFinite()
    {
        LookaheadDelay delay;
        delay.prepare(kSampleRate, kBlockSize, kNumChannels);
        delay.setLookaheadEnum(LookaheadDelay::Lookahead::Off);

        const int numSamples = 256;
        auto input = makeSine(numSamples, kSampleRate, 220.0, 0.4);
        std::vector<float> output(input.size(), 0.0f);
        float *inChannels[1] = { input.data() };
        float *outChannels[1] = { output.data() };

        // Stream several blocks while retargeting the lookahead mid-stream.
        for (int block = 0; block < 8; ++block)
        {
            switch (block)
            {
                case 1: delay.setLookaheadEnum(LookaheadDelay::Lookahead::Ms1); break;
                case 3: delay.setLookaheadEnum(LookaheadDelay::Lookahead::Ms5); break;
                case 5: delay.setLookaheadEnum(LookaheadDelay::Lookahead::Ms0_5); break;
                case 6: delay.setLookaheadEnum(LookaheadDelay::Lookahead::Ms10); break;
                default: break;
            }

            delay.process(inChannels, outChannels, 1, numSamples);

            expect(allFinite(output),
                   "Output must remain finite when lookahead changes mid-stream (block="
                       + juce::String(block) + ")");

            // Bounded output: pass-through of a 0.4-amplitude sine through a
            // linear delay line must never explode.
            const float peak = *std::max_element(output.begin(), output.end(),
                [](float a, float b) { return std::fabs(a) < std::fabs(b); });
            expect(std::fabs(peak) <= 1.0f,
                   "Output must stay bounded during lookahead transitions");
        }
    }
};

// Static instance registers the test with juce::UnitTestRunner.
static LookaheadTests s_lookaheadTestsInstance;

} // namespace
