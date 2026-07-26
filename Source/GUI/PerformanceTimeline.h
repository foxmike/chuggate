// PerformanceTimeline.h
//
// Central performance visualization (CHUGGATE_SPEC.md §20.1).
//
// A juce::Component that paints a horizontal "recent time" view of the
// PerformanceHistory ring buffer, overlaying the lane series the spec
// requires:
//
//   - conditioned key envelope
//   - main audio envelope
//   - open + close thresholds
//   - noise-floor estimate
//   - transient markers
//   - sustain confidence
//   - gate state lane
//   - output gain
//   - attenuation shading between unprocessed and gated gain
//
// Optional lanes (alignment offset, lookahead region) are rendered when the
// history exposes them. All paint work happens on the message thread; the
// audio thread is never touched from this component.
//
// The history pointer is set by the editor via setHistory(). The component
// is null-safe: a null history produces an empty paint (just the suite
// background and a "no data" hint) rather than a crash.
//
// Visibility of each lane is gated by small LayerFlags so later analyzer
// modes (Off / Live / Studio / Diagnose, see §20.6) can wire the per-mode
// preset without touching paint code.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../Analysis/PerformanceHistory.h"

namespace ChugGate::GUI {

//==============================================================================
/**
 *  Bit-set of layer visibility flags for the Performance Timeline.
 *
 *  Each flag toggles one logical lane / overlay from the spec. A flag set
 *  here corresponds to one of the "every visual layer individually
 *  switchable" entries in §20.6. Defaults cover the full Studio preset.
 */
class LayerFlags
{
public:
    enum Flag : std::uint32_t
    {
        KeyEnvelope         = 1u << 0,
        MainEnvelope        = 1u << 1,
        OpenThreshold       = 1u << 2,
        CloseThreshold      = 1u << 3,
        NoiseFloor          = 1u << 4,
        TransientMarkers    = 1u << 5,
        SustainConfidence   = 1u << 6,
        GateStateLane       = 1u << 7,
        OutputGain          = 1u << 8,
        AttenuationShading  = 1u << 9,
        AlignmentOffset     = 1u << 10,
        LookaheadRegion     = 1u << 11,
    };

    constexpr LayerFlags() noexcept = default;
    constexpr LayerFlags (std::uint32_t v) noexcept : value_ (v) {}

    constexpr LayerFlags operator| (LayerFlags other) const noexcept
    {
        return LayerFlags (value_ | other.value_);
    }
    constexpr LayerFlags operator& (LayerFlags other) const noexcept
    {
        return LayerFlags (value_ & other.value_);
    }
    constexpr LayerFlags& operator|= (LayerFlags other) noexcept
    {
        value_ |= other.value_; return *this;
    }
    constexpr LayerFlags& operator&= (LayerFlags other) noexcept
    {
        value_ &= other.value_; return *this;
    }

    [[nodiscard]] constexpr bool has (Flag f) const noexcept
    {
        return (value_ & static_cast<std::uint32_t> (f)) != 0u;
    }
    constexpr void set (Flag f, bool on) noexcept
    {
        const auto mask = static_cast<std::uint32_t> (f);
        if (on) value_ |= mask; else value_ &= ~mask;
    }

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value_; }

    // Sensible default: everything visible. Lets the Studio preset be the
    // out-of-the-box view; later analyzer modes can pass a smaller set.
    static constexpr LayerFlags studioDefault() noexcept
    {
        return LayerFlags (KeyEnvelope | MainEnvelope | OpenThreshold
                           | CloseThreshold | NoiseFloor | TransientMarkers
                           | SustainConfidence | GateStateLane | OutputGain
                           | AttenuationShading);
    }

private:
    std::uint32_t value_ = studioDefault().value_;
};

//==============================================================================
/**
 *  PerformanceTimeline — central visualization for §20.1.
 *
 *  Sourcing:
 *      The component holds a non-owning pointer to a PerformanceHistory
 *      instance. The pointer is set via setHistory() (callable from the
 *      editor's message thread) and may be null. The component never
 *      mutates the history capacity or pushes into it; it only reads.
 *
 *  Threading:
 *      All reads happen inside timerCallback() (message thread). We copy
 *      the most recent samples into member path buffers via
 *      PerformanceHistory::copyRecent(), then trigger a repaint. The paint
 *      path uses the local snapshots and never touches the history.
 *
 *  Paint:
 *      The component is laid out top→bottom as a stack of lanes:
 *        - Gain lane (output gain with attenuation shading)
 *        - Audio/key envelope lane (overlaid traces)
 *        - Threshold/floor lane (open / close / noise floor)
 *        - Sustain/transient lane
 *        - Gate state lane (rectangles per state, color-blind-safe symbol)
 *      The time axis is left-to-right; older samples on the left.
 */
class PerformanceTimeline : public juce::Component,
                            private juce::Timer
{
public:
    //==========================================================================
    PerformanceTimeline();
    ~PerformanceTimeline() override;

    //==========================================================================
    // History wiring (null-safe). The history pointer is non-owning; the
    // component never deletes it.
    void setHistory (ChugGate::Analysis::PerformanceHistory* history) noexcept;
    [[nodiscard]] ChugGate::Analysis::PerformanceHistory* getHistory() const noexcept
    {
        return history_;
    }

    //==========================================================================
    // Layer visibility (later analyzer modes will setLayerVisible() to
    // toggle specific lanes per §20.6). Safe to call from the message
    // thread; will trigger a repaint when changed.
    void setLayerVisible (LayerFlags::Flag flag, bool visible) noexcept;
    void setLayerFlags (LayerFlags flags) noexcept;
    [[nodiscard]] LayerFlags getLayerFlags() const noexcept { return layerFlags_; }

    //==========================================================================
    // Refresh rate in milliseconds. The Timer drives the message-thread
    // repaint cadence. Default 33 ms (~30 fps) is suitable for Live paint
    // rates; callers can speed it up for Studio / Diagnose. Must be > 0.
    void setRefreshMs (int ms) noexcept;
    [[nodiscard]] int getRefreshMs() const noexcept { return refreshMs_; }

    //==========================================================================
    // Hint that data may have changed (e.g. when the editor wires up the
    // processor's push timer). Triggers a non-blocking repaint.
    void refreshNow() noexcept;

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // juce::Timer — fires on the message thread; refreshes the local
    // snapshot from the history and repaints.
    void timerCallback() override;

    //==========================================================================
    // Refresh local snapshot from the history (message thread). Returns
    // the number of samples written into the buffers (0 when no data is
    // available or the history is null).
    int refreshSnapshotFromHistory() noexcept;

    //==========================================================================
    // Drawing helpers (private, paint-only). Each helper receives the
    // lane rect and paints into it. Helpers read the local snapshot
    // populated by refreshSnapshotFromHistory().
    void paintBackground   (juce::Graphics& g, juce::Rectangle<int> bounds) noexcept;
    void paintLaneFrames   (juce::Graphics& g) noexcept;
    void paintGateStateLane(juce::Graphics& g, juce::Rectangle<int> lane) noexcept;
    void paintAttenuationLane(juce::Graphics& g, juce::Rectangle<int> lane) noexcept;
    void paintEnvelopeLane (juce::Graphics& g, juce::Rectangle<int> lane) noexcept;
    void paintThresholdLane(juce::Graphics& g, juce::Rectangle<int> lane) noexcept;
    void paintSustainLane  (juce::Graphics& g, juce::Rectangle<int> lane) noexcept;
    void paintEmptyState   (juce::Graphics& g, juce::Rectangle<int> bounds) noexcept;

    // Map a sample index [0, snapshotNumSamples) to an x pixel within
    // the lane rect. Handles the partial-window case (numSamples < capacity)
    // by simply stretching the available samples to the full width so the
    // user always sees a continuous strip of recent history.
    [[nodiscard]] float xForSample (int sampleIndex,
                                    int totalSamples,
                                    juce::Rectangle<int> lane) const noexcept;

    // Map a normalised envelope value [0,1] to a y pixel within the lane.
    [[nodiscard]] float yForEnvelope (float normalised,
                                      juce::Rectangle<int> lane) const noexcept;

    // Map a gain value [0,1] to a y pixel (0 dB at top, -inf at bottom).
    [[nodiscard]] float yForGain (float gainLinear,
                                  juce::Rectangle<int> lane) const noexcept;

    //==========================================================================
    // Layered lane layout. We hold a vertical stack of rectangles computed
    // in resized() and reread in paint(). Order top→bottom matches the
    // visual reading order the user expects: gate state at top (most
    // important), attenuation next, then envelopes, thresholds, and
    // sustain / transients at the bottom.
    enum class Lane : int
    {
        GateState = 0,
        Attenuation,
        Envelopes,
        Thresholds,
        Sustain,
        Count
    };

    struct LaneLayout
    {
        juce::Rectangle<int> rects[static_cast<int> (Lane::Count)];
    };

    //==========================================================================
    // History pointer (non-owning). The editor is responsible for keeping
    // the underlying history alive for as long as this component holds the
    // pointer. May be null.
    ChugGate::Analysis::PerformanceHistory* history_ = nullptr;

    // Cached snapshot. Refreshed on the message thread from the history
    // via copyRecent(). All paint-time reads come from these buffers, so
    // the paint path never touches the history directly. Capacity tracks
    // the history's capacity to keep memory bounded.
    struct Snapshot
    {
        int capacity = 0;     // physical buffer size (== snapshot size)
        int numSamples = 0;   // most-recent valid samples
        double controlRateHz = 100.0;
        double secondsVisible = 0.0;

        std::vector<float> keyEnv;
        std::vector<float> mainEnv;
        std::vector<float> threshOpen;
        std::vector<float> threshClose;
        std::vector<float> noiseFloor;
        std::vector<float> outputGain;
        std::vector<float> transientMarker;
        std::vector<float> sustainConfidence;
        std::vector<float> alignmentOffset;
        std::vector<std::uint8_t> gateState;
        std::vector<std::uint8_t> lookaheadRegion;

        bool hasAlignment = false;
        bool hasLookahead = false;

        void ensureCapacity (int cap)
        {
            if (cap <= 0) cap = 1;
            if (cap == capacity) return;

            // Resize all series to the new capacity. We zero-init so
            // unpopulated samples (when numSamples < capacity) paint
            // as the suite background.
            keyEnv.assign (cap, 0.0f);
            mainEnv.assign (cap, 0.0f);
            threshOpen.assign (cap, 0.0f);
            threshClose.assign (cap, 0.0f);
            noiseFloor.assign (cap, 0.0f);
            outputGain.assign (cap, 1.0f); // unprocessed gain = unity
            transientMarker.assign (cap, 0.0f);
            sustainConfidence.assign (cap, 0.0f);
            alignmentOffset.assign (cap, 0.0f);
            gateState.assign (cap, 0);
            lookaheadRegion.assign (cap, 0);

            capacity = cap;
            // Force numSamples to be clamped on next access.
            if (numSamples > cap) numSamples = cap;
        }
    };

    Snapshot snapshot_;
    LaneLayout layout_;
    LayerFlags layerFlags_;
    int refreshMs_ = 33; // ~30 fps
};

} // namespace ChugGate::GUI
