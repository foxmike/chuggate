// PerformanceTimeline.cpp
//
// See PerformanceTimeline.h. Paints the central §20.1 visualization. The
// component is fully self-contained on the message thread: the Timer
// refreshes a local snapshot via PerformanceHistory::copyRecent() and the
// paint path reads only from the snapshot.

#include "PerformanceTimeline.h"

#include "ChugLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ChugGate::GUI {

namespace
{
    // Lane vertical weight — relative heights of each lane in the stack.
    // Sum to 1.0. Tuned so the gate-state lane stays prominent (top) and
    // the transient / sustain row stays compact (bottom).
    constexpr float kLaneWeightGateState     = 0.18f;
    constexpr float kLaneWeightAttenuation  = 0.22f;
    constexpr float kLaneWeightEnvelopes    = 0.28f;
    constexpr float kLaneWeightThresholds   = 0.20f;
    constexpr float kLaneWeightSustain      = 0.12f;

    // Inset between lanes for the divider lines.
    constexpr int kLaneInset = 6;
    constexpr int kOuterInset = 4;

    // Convert linear gain to dBFS, clamped at -80 to avoid -inf in paint.
    inline float gainToDb (float gainLinear) noexcept
    {
        if (gainLinear <= 0.0f) return -80.0f;
        const float db = 20.0f * std::log10 (gainLinear);
        if (db < -80.0f) return -80.0f;
        if (db > 12.0f) return 12.0f;
        return db;
    }

    // Map a dBFS value in [-80, +12] to [0, 1] with 0 dB at the top.
    inline float dbToNormalised (float db) noexcept
    {
        if (db <= -80.0f) return 0.0f;
        if (db >= 12.0f) return 1.0f;
        return (db + 80.0f) / 92.0f;
    }
} // namespace

//==============================================================================
// Construction / destruction.
//==============================================================================
PerformanceTimeline::PerformanceTimeline()
{
    setOpaque (true);
    setWantsKeyboardFocus (false);
    // Start the timer immediately — the editor is expected to call
    // setHistory() soon. A null history produces an "empty state" paint
    // until data is wired up.
    startTimer (refreshMs_);
}

PerformanceTimeline::~PerformanceTimeline()
{
    stopTimer();
}

//==============================================================================
// History wiring.
//==============================================================================
void PerformanceTimeline::setHistory (ChugGate::Analysis::PerformanceHistory* history) noexcept
{
    history_ = history;
    // Don't trigger a refresh here — the next timer tick will do it. The
    // editor may set the history repeatedly during initialisation; defer
    // work to the message thread.
}

void PerformanceTimeline::setLayerVisible (LayerFlags::Flag flag, bool visible) noexcept
{
    const bool wasVisible = layerFlags_.has (flag);
    layerFlags_.set (flag, visible);
    if (wasVisible != visible)
        refreshNow();
}

void PerformanceTimeline::setLayerFlags (LayerFlags flags) noexcept
{
    if (flags.raw() != layerFlags_.raw())
    {
        layerFlags_ = flags;
        refreshNow();
    }
}

void PerformanceTimeline::setRefreshMs (int ms) noexcept
{
    if (ms < 10) ms = 10; // clamp to a sane minimum
    if (ms > 1000) ms = 1000;
    if (ms == refreshMs_) return;
    refreshMs_ = ms;
    startTimer (refreshMs_); // restart with new period
}

void PerformanceTimeline::refreshNow() noexcept
{
    repaint();
}

//==============================================================================
// Component overrides.
//==============================================================================
void PerformanceTimeline::resized()
{
    // Recompute lane layout. We reserve outer padding for the background
    // and inner insets between lanes for the divider lines.
    const auto b = getLocalBounds();
    const int pad = kOuterInset;
    const int innerLeft   = b.getX() + pad;
    const int innerRight  = b.getRight() - pad;
    const int innerTop    = b.getY() + pad;
    const int innerBottom = b.getBottom() - pad;
    const int totalH = std::max (1, innerBottom - innerTop);

    // Order matches Lane enum top→bottom.
    const float wGate  = kLaneWeightGateState;
    const float wAtt   = kLaneWeightAttenuation;
    const float wEnv   = kLaneWeightEnvelopes;
    const float wThr   = kLaneWeightThresholds;

    layout_.rects[static_cast<int> (Lane::GateState)] = juce::Rectangle<int> (
        innerLeft, innerTop,
        std::max (1, innerRight - innerLeft),
        static_cast<int> (static_cast<float> (totalH) * wGate)).reduced (0, 0);

    const int gateBottom = layout_.rects[static_cast<int> (Lane::GateState)].getBottom();
    layout_.rects[static_cast<int> (Lane::Attenuation)] = juce::Rectangle<int> (
        innerLeft,
        gateBottom + kLaneInset,
        std::max (1, innerRight - innerLeft),
        static_cast<int> (static_cast<float> (totalH) * wAtt));

    const int attBottom = layout_.rects[static_cast<int> (Lane::Attenuation)].getBottom();
    layout_.rects[static_cast<int> (Lane::Envelopes)] = juce::Rectangle<int> (
        innerLeft,
        attBottom + kLaneInset,
        std::max (1, innerRight - innerLeft),
        static_cast<int> (static_cast<float> (totalH) * wEnv));

    const int envBottom = layout_.rects[static_cast<int> (Lane::Envelopes)].getBottom();
    layout_.rects[static_cast<int> (Lane::Thresholds)] = juce::Rectangle<int> (
        innerLeft,
        envBottom + kLaneInset,
        std::max (1, innerRight - innerLeft),
        static_cast<int> (static_cast<float> (totalH) * wThr));

    const int thrBottom = layout_.rects[static_cast<int> (Lane::Thresholds)].getBottom();
    layout_.rects[static_cast<int> (Lane::Sustain)] = juce::Rectangle<int> (
        innerLeft,
        thrBottom + kLaneInset,
        std::max (1, innerRight - innerLeft),
        std::max (1, innerBottom - (thrBottom + kLaneInset)));
}

void PerformanceTimeline::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    // Background — always paint the suite base colour, even when there is
    // no data, so the component reads as a uniform instrument surface.
    paintBackground (g, bounds);
    paintLaneFrames (g);

    const int n = snapshot_.numSamples;
    if (history_ == nullptr || n <= 0)
    {
        paintEmptyState (g, bounds);
        return;
    }

    if (layerFlags_.has (LayerFlags::GateStateLane))
        paintGateStateLane (g, layout_.rects[static_cast<int> (Lane::GateState)]);

    if (layerFlags_.has (LayerFlags::AttenuationShading)
        || layerFlags_.has (LayerFlags::OutputGain))
    {
        paintAttenuationLane (g, layout_.rects[static_cast<int> (Lane::Attenuation)]);
    }

    if (layerFlags_.has (LayerFlags::KeyEnvelope)
        || layerFlags_.has (LayerFlags::MainEnvelope))
    {
        paintEnvelopeLane (g, layout_.rects[static_cast<int> (Lane::Envelopes)]);
    }

    if (layerFlags_.has (LayerFlags::OpenThreshold)
        || layerFlags_.has (LayerFlags::CloseThreshold)
        || layerFlags_.has (LayerFlags::NoiseFloor)
        || (layerFlags_.has (LayerFlags::AlignmentOffset) && snapshot_.hasAlignment)
        || (layerFlags_.has (LayerFlags::LookaheadRegion) && snapshot_.hasLookahead))
    {
        paintThresholdLane (g, layout_.rects[static_cast<int> (Lane::Thresholds)]);
    }

    if (layerFlags_.has (LayerFlags::TransientMarkers)
        || layerFlags_.has (LayerFlags::SustainConfidence)
        || (layerFlags_.has (LayerFlags::LookaheadRegion) && snapshot_.hasLookahead))
    {
        paintSustainLane (g, layout_.rects[static_cast<int> (Lane::Sustain)]);
    }
}

//==============================================================================
// Timer — message thread.
//==============================================================================
void PerformanceTimeline::timerCallback()
{
    if (history_ == nullptr)
    {
        // No data yet. Still repaint occasionally so the "no data" hint
        // appears even if resized()/paint() are not externally triggered.
        repaint();
        return;
    }

    if (refreshSnapshotFromHistory() > 0)
        repaint();
}

int PerformanceTimeline::refreshSnapshotFromHistory() noexcept
{
    if (history_ == nullptr)
        return 0;

    // Capacity-driven size: the snapshot capacity tracks the history
    // capacity so we never request more than the history can supply.
    const int histCap = history_->getCapacity();
    if (histCap <= 0)
        return 0;

    snapshot_.ensureCapacity (histCap);
    snapshot_.controlRateHz = history_->getControlRateHz();
    snapshot_.secondsVisible = history_->getDurationSeconds();

    ChugGate::Analysis::PerformanceHistory::CopyBuffers bufs;
    bufs.keyEnvelope         = snapshot_.keyEnv.data();
    bufs.mainEnvelope        = snapshot_.mainEnv.data();
    bufs.thresholdOpen       = snapshot_.threshOpen.data();
    bufs.thresholdClose      = snapshot_.threshClose.data();
    bufs.noiseFloor          = snapshot_.noiseFloor.data();
    bufs.outputGain          = snapshot_.outputGain.data();
    bufs.transientMarker     = snapshot_.transientMarker.data();
    bufs.sustainConfidence   = snapshot_.sustainConfidence.data();
    bufs.alignmentOffset     = snapshot_.hasAlignment ? snapshot_.alignmentOffset.data() : nullptr;
    bufs.gateState           = snapshot_.gateState.data();
    bufs.lookaheadRegion     = snapshot_.hasLookahead ? snapshot_.lookaheadRegion.data() : nullptr;

    // Probe optional series availability once, then keep the flag pinned
    // for the rest of the session. The history flips hasAlignment_ /
    // hasLookahead_ on the first Diagnose-mode push; until then the
    // spans are empty and we keep the snapshot in the "no" state.
    const auto view = history_->getView();
    snapshot_.hasAlignment = ! view.alignmentOffset.empty();
    snapshot_.hasLookahead = ! view.lookaheadRegion.empty();
    if (! snapshot_.hasAlignment)
        bufs.alignmentOffset = nullptr;
    if (! snapshot_.hasLookahead)
        bufs.lookaheadRegion = nullptr;

    const int copied = history_->copyRecent (bufs, histCap);
    snapshot_.numSamples = copied;
    return copied;
}

//==============================================================================
// Drawing helpers.
//==============================================================================
void PerformanceTimeline::paintBackground (juce::Graphics& g, juce::Rectangle<int> bounds) noexcept
{
    g.fillAll (ChugLookAndFeel::getPanelColour());
    g.setColour (ChugLookAndFeel::getOutlineColour());
    g.drawRect (bounds, 1);
}

void PerformanceTimeline::paintLaneFrames (juce::Graphics& g) noexcept
{
    g.setColour (ChugLookAndFeel::getSunkenPanelColour());
    for (int i = 0; i < static_cast<int> (Lane::Count); ++i)
    {
        const auto r = layout_.rects[i];
        g.fillRect (r);
        g.setColour (ChugLookAndFeel::getOutlineColour());
        g.drawRect (r, 1);
        g.setColour (ChugLookAndFeel::getSunkenPanelColour());
    }
}

void PerformanceTimeline::paintEmptyState (juce::Graphics& g, juce::Rectangle<int> bounds) noexcept
{
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Performance Timeline — no data", bounds,
                juce::Justification::centred, true);
}

void PerformanceTimeline::paintGateStateLane (juce::Graphics& g, juce::Rectangle<int> lane) noexcept
{
    const int n = snapshot_.numSamples;
    if (n <= 0) return;

    // One state-cell per visible sample. We collapse runs of equal state
    // into wider segments so the lane reads as discrete state phases.
    int runStart = 0;
    std::uint8_t runState = snapshot_.gateState[0];
    const auto baseColour = ChugLookAndFeel::getOutlineColour();
    g.setColour (baseColour);

    auto drawRun = [&] (int start, int end, std::uint8_t state) {
        const float x0 = xForSample (start, n, lane);
        const float x1 = xForSample (end + 1, n, lane);
        const float w = std::max (1.0f, x1 - x0);
        const juce::Rectangle<float> cell (x0, static_cast<float> (lane.getY()),
                                          w, static_cast<float> (lane.getHeight()));
        const auto gs = static_cast<GateState> (state);
        const auto fill = ChugLookAndFeel::getStateColour (gs);
        g.setColour (fill);
        g.fillRect (cell);
        g.setColour (ChugLookAndFeel::getOutlineStrongColour());
        g.drawRect (cell, 1);

        // Color-blind-safe: paint the state symbol + 3-letter abbreviation
        // centred in the cell when the cell is wide enough. The text is
        // drawn with the active badge's foreground (background-aware)
        // so it remains legible against the state colour.
        if (w >= 24.0f)
        {
            const auto fg = ChugLookAndFeel::getTextColour();
            const juce::String sym = ChugLookAndFeel::getStateSymbol (gs)
                                   + " " + ChugLookAndFeel::getStateAbbrev (gs);
            g.setColour (fg);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (sym, cell, juce::Justification::centred, false);
        }
    };

    for (int i = 1; i < n; ++i)
    {
        if (snapshot_.gateState[i] != runState)
        {
            drawRun (runStart, i - 1, runState);
            runStart = i;
            runState = snapshot_.gateState[i];
        }
    }
    drawRun (runStart, n - 1, runState);
}

void PerformanceTimeline::paintAttenuationLane (juce::Graphics& g, juce::Rectangle<int> lane) noexcept
{
    const int n = snapshot_.numSamples;
    if (n <= 0) return;

    // Output gain: 0 dB at the top, -80 dB at the bottom. We render the
    // gain trace as a filled area; the "attenuation" overlay is the gap
    // between the unity line (top) and the actual gain — shaded in the
    // suite attenuation colour so the amount of ducking is instantly
    // visible.
    juce::Path gainTrace;
    juce::Path fill;

    const float unityY = static_cast<float> (lane.getY()); // 0 dB
    bool first = true;
    for (int i = 0; i < n; ++i)
    {
        const float x = xForSample (i, n, lane);
        const float g = snapshot_.outputGain[i];
        const float y = yForGain (g, lane); // bottom = -inf
        if (first)
        {
            gainTrace.startNewSubPath (x, y);
            fill.startNewSubPath (x, unityY);
            fill.lineTo (x, y);
            first = false;
        }
        else
        {
            gainTrace.lineTo (x, y);
            fill.lineTo (x, y);
        }
    }
    if (n > 0)
    {
        const float xEnd = xForSample (n - 1, n, lane);
        fill.lineTo (xEnd, unityY);
        fill.closeSubPath();
    }

    // Attenuation shading (the "between unprocessed and gated gain" band
    // called out in §20.1) — drawn first so the gain trace sits on top.
    if (layerFlags_.has (LayerFlags::AttenuationShading))
    {
        g.setColour (ChugLookAndFeel::getAttenuationColour());
        g.fillPath (fill);
    }

    // Output gain trace line.
    if (layerFlags_.has (LayerFlags::OutputGain))
    {
        g.setColour (ChugLookAndFeel::getGainTraceColour());
        g.strokePath (gainTrace, juce::PathStrokeType (1.4f));

        // Unity reference line at 0 dB so the user can see the gap.
        g.setColour (ChugLookAndFeel::getOutlineStrongColour());
        g.drawLine (static_cast<float> (lane.getX()), unityY,
                    static_cast<float> (lane.getRight()), unityY, 1.0f);
    }
}

void PerformanceTimeline::paintEnvelopeLane (juce::Graphics& g, juce::Rectangle<int> lane) noexcept
{
    const int n = snapshot_.numSamples;
    if (n <= 0) return;

    // Envelopes are stored in normalised units — already in [0, 1].
    auto tracePath = [&] (const std::vector<float>& series) {
        juce::Path p;
        bool started = false;
        for (int i = 0; i < n; ++i)
        {
            const float x = xForSample (i, n, lane);
            const float y = yForEnvelope (series[i], lane);
            if (! started)
            {
                p.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                p.lineTo (x, y);
            }
        }
        return p;
    };

    if (layerFlags_.has (LayerFlags::KeyEnvelope))
    {
        g.setColour (ChugLookAndFeel::getKeyTraceColour());
        g.strokePath (tracePath (snapshot_.keyEnv), juce::PathStrokeType (1.2f));
    }
    if (layerFlags_.has (LayerFlags::MainEnvelope))
    {
        g.setColour (ChugLookAndFeel::getAudioTraceColour());
        g.strokePath (tracePath (snapshot_.mainEnv), juce::PathStrokeType (1.2f));
    }
}

void PerformanceTimeline::paintThresholdLane (juce::Graphics& g, juce::Rectangle<int> lane) noexcept
{
    const int n = snapshot_.numSamples;
    if (n <= 0) return;

    auto tracePath = [&] (const std::vector<float>& series) {
        juce::Path p;
        bool started = false;
        for (int i = 0; i < n; ++i)
        {
            const float x = xForSample (i, n, lane);
            const float y = yForEnvelope (series[i], lane);
            if (! started)
            {
                p.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                p.lineTo (x, y);
            }
        }
        return p;
    };

    if (layerFlags_.has (LayerFlags::NoiseFloor))
    {
        g.setColour (ChugLookAndFeel::getNoiseFloorColour());
        g.strokePath (tracePath (snapshot_.noiseFloor), juce::PathStrokeType (1.0f));
    }
    if (layerFlags_.has (LayerFlags::CloseThreshold))
    {
        g.setColour (ChugLookAndFeel::getThresholdColour().withMultipliedAlpha (0.7f));
        g.strokePath (tracePath (snapshot_.threshClose),
                      juce::PathStrokeType (1.0f));
    }
    if (layerFlags_.has (LayerFlags::OpenThreshold))
    {
        g.setColour (ChugLookAndFeel::getThresholdColour());
        g.strokePath (tracePath (snapshot_.threshOpen),
                      juce::PathStrokeType (1.4f));
    }

    // Alignment offset — when the history exposes it, draw a thin line
    // at the offset value (normalised to the same y-axis as thresholds).
    if (layerFlags_.has (LayerFlags::AlignmentOffset) && snapshot_.hasAlignment && n > 1)
    {
        juce::Path alignPath;
        bool started = false;
        for (int i = 0; i < n; ++i)
        {
            const float x = xForSample (i, n, lane);
            const float y = yForEnvelope (snapshot_.alignmentOffset[i], lane);
            if (! started)
            {
                alignPath.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                alignPath.lineTo (x, y);
            }
        }
        g.setColour (ChugLookAndFeel::getAccentColour().withMultipliedAlpha (0.6f));
        g.strokePath (alignPath, juce::PathStrokeType (1.0f));
    }

    // Lookahead region — render as a translucent vertical band at the
    // right edge of the lane when the per-sample flag is set.
    if (layerFlags_.has (LayerFlags::LookaheadRegion) && snapshot_.hasLookahead)
    {
        g.setColour (ChugLookAndFeel::getAccentDimColour());
        for (int i = 0; i < n; ++i)
        {
            if (snapshot_.lookaheadRegion[i] != 0)
            {
                const float x0 = xForSample (i, n, lane);
                const float x1 = xForSample (i + 1, n, lane);
                g.fillRect (x0, static_cast<float> (lane.getY()),
                            std::max (1.0f, x1 - x0),
                            static_cast<float> (lane.getHeight()));
            }
        }
    }
}

void PerformanceTimeline::paintSustainLane (juce::Graphics& g, juce::Rectangle<int> lane) noexcept
{
    const int n = snapshot_.numSamples;
    if (n <= 0) return;

    // Sustain confidence as a filled area at the bottom of the lane.
    if (layerFlags_.has (LayerFlags::SustainConfidence))
    {
        juce::Path p;
        const float baseY = static_cast<float> (lane.getBottom());
        bool started = false;
        for (int i = 0; i < n; ++i)
        {
            const float x = xForSample (i, n, lane);
            const float conf = std::clamp (snapshot_.sustainConfidence[i], 0.0f, 1.0f);
            const float h = static_cast<float> (lane.getHeight()) * conf;
            const float y = baseY - h;
            if (! started)
            {
                p.startNewSubPath (x, baseY);
                p.lineTo (x, y);
                started = true;
            }
            else
            {
                p.lineTo (x, y);
            }
        }
        if (n > 0)
        {
            const float xEnd = xForSample (n - 1, n, lane);
            p.lineTo (xEnd, baseY);
            p.closeSubPath();
        }
        g.setColour (ChugLookAndFeel::getWarningColour().withMultipliedAlpha (0.55f));
        g.fillPath (p);
    }

    // Transient markers — short vertical bars at the top of the lane
    // when the per-sample marker is non-zero. The threshold is left to
    // the producer (typically 0.5 on a normalised transient strength).
    if (layerFlags_.has (LayerFlags::TransientMarkers))
    {
        g.setColour (ChugLookAndFeel::getAccentColour());
        for (int i = 0; i < n; ++i)
        {
            if (snapshot_.transientMarker[i] > 0.0f)
            {
                const float x = xForSample (i, n, lane);
                g.drawLine (x, static_cast<float> (lane.getY()),
                            x, static_cast<float> (lane.getY()) + 4.0f, 2.0f);
            }
        }
    }

    // Lookahead region — same band, drawn on the sustain lane when not
    // already rendered in the threshold lane.
    if (layerFlags_.has (LayerFlags::LookaheadRegion) && snapshot_.hasLookahead)
    {
        g.setColour (ChugLookAndFeel::getAccentDimColour().withMultipliedAlpha (0.6f));
        for (int i = 0; i < n; ++i)
        {
            if (snapshot_.lookaheadRegion[i] != 0)
            {
                const float x0 = xForSample (i, n, lane);
                const float x1 = xForSample (i + 1, n, lane);
                g.fillRect (x0, static_cast<float> (lane.getY()),
                            std::max (1.0f, x1 - x0),
                            static_cast<float> (lane.getHeight()));
            }
        }
    }
}

//==============================================================================
// Coordinate helpers.
//==============================================================================
float PerformanceTimeline::xForSample (int sampleIndex, int totalSamples,
                                        juce::Rectangle<int> lane) const noexcept
{
    if (totalSamples <= 0) return static_cast<float> (lane.getX());
    const int last = std::max (0, totalSamples - 1);
    const int clamped = std::clamp (sampleIndex, 0, last);
    const float t = static_cast<float> (clamped) / static_cast<float> (last);
    return static_cast<float> (lane.getX())
         + t * static_cast<float> (lane.getWidth());
}

float PerformanceTimeline::yForEnvelope (float normalised, juce::Rectangle<int> lane) const noexcept
{
    const float v = std::clamp (normalised, 0.0f, 1.0f);
    const float top = static_cast<float> (lane.getY());
    const float bot = static_cast<float> (lane.getBottom());
    return bot - v * (bot - top);
}

float PerformanceTimeline::yForGain (float gainLinear, juce::Rectangle<int> lane) const noexcept
{
    const float db = gainToDb (gainLinear);
    const float v = dbToNormalised (db); // 0 dB at v=1, -80 dB at v=0
    return yForEnvelope (v, lane);
}

} // namespace ChugGate::GUI
