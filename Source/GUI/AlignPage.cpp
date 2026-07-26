// AlignPage.cpp
//
// See AlignPage.h. Implements the Align view (CHUGGATE_SPEC.md §21.4 /
// §20.2 / §13.5).
//
// Layout (computed in resized()):
//
//   ┌─────────────────────────────────────────────────────────────────────┐
//   │  ALIGN                                                                │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │  ALIGNMENT SCOPE                                                      │
//   │   ┌─────────────────┐                                                │
//   │   │ Key onset       │  <- stacked lanes for key onset, main onset,   │
//   │   ├─────────────────┤     correlation curve                          │
//   │   │ Main onset      │                                                │
//   │   ├─────────────────┤                                                │
//   │   │ Correlation     │                                                │
//   │   └─────────────────┘                                                │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │  READOUTS                                                             │
//   │   Offset: ±X.XX ms    Confidence: 0.0–1.0                             │
//   │   Variation: ±X.XX ms Key delay: X.XX ms                              │
//   │   Lookahead: X.XX ms  Needs lookahead: yes/no                         │
//   ├─────────────────────────────────────────────────────────────────────┤
//   │  CONTROLS                                                             │
//   │   [Calibrate/Align] [Accept] [Retry]   Manual Trim [—————]           │
//   │                       Lookahead: [combo]                              │
//   └─────────────────────────────────────────────────────────────────────┘
//
// All waveform / curve painting reads from the page-owned snapshot,
// which is refreshed on the message thread from
// AlignmentScopeData::copy*() once per refresh period. Audio thread is
// never touched.

#include "AlignPage.h"

#include "ChugLookAndFeel.h"

#include "../ParameterIDs.h"

#include <cmath>

namespace ChugGate::GUI {

namespace
{
    //----------------------------------------------------------------------
    // Layout constants.
    //----------------------------------------------------------------------
    constexpr int kOuterInset       = 8;
    constexpr int kRowGap           = 6;
    constexpr int kGroupTitleHeight = 18;
    constexpr int kGroupInnerInset  = 6;

    // Vertical weights. The scope is by far the largest panel; controls
    // and readouts are smaller but always visible.
    constexpr float kScopeWeight     = 0.55f;
    constexpr float kReadoutsWeight  = 0.22f;
    constexpr float kControlsWeight  = 0.23f;

    constexpr int kScopeMinH        = 160;
    constexpr int kReadoutsMinH     = 90;
    constexpr int kControlsMinH     = 80;

    // Snapshot initial capacities. These are lower bounds — refresh grows
    // them as needed so the first paint never trips an undersized buffer.
    constexpr int kInitialKeyCap      = 256;
    constexpr int kInitialMainCap     = 256;
    constexpr int kInitialCorrCap     = 257;

    //----------------------------------------------------------------------
    // APVTS → combo box binding helper. When the APVTS parameter exists
    // and is an AudioParameterChoice, the combo box is populated with the
    // choice's text items. Otherwise the combo stays empty and the page
    // remains usable.
    //----------------------------------------------------------------------
    void addChoiceItems (juce::ComboBox& box,
                         juce::AudioProcessorValueTreeState& apvts,
                         const char* paramId)
    {
        box.clear();
        if (auto* p = apvts.getParameter (paramId))
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
            {
                int id = 1;
                for (const auto& s : choice->choices)
                    box.addItem (s, id++);
            }
        }
    }

    //----------------------------------------------------------------------
    // Format a float as a signed ms readout with one decimal place.
    //----------------------------------------------------------------------
    juce::String formatMs (float v)
    {
        if (std::isnan (v)) return juce::String ("n/a");
        return juce::String (v, 1) + juce::String (" ms");
    }

    //----------------------------------------------------------------------
    // Format a 0..1 confidence as a percentage.
    //----------------------------------------------------------------------
    juce::String formatConfidence (float v)
    {
        if (std::isnan (v)) return juce::String ("n/a");
        return juce::String (juce::jlimit (0, 100, v * 100.0f), 0) + juce::String (" %");
    }
} // namespace

//==============================================================================
// Construction / destruction.
//==============================================================================
AlignPage::AlignPage()
    : scopeGroup_(),
      readoutsGroup_(),
      controlsGroup_()
{
    setOpaque (false);
    setWantsKeyboardFocus (false);

    // Build the three group panels.
    scopeGroup_.panel    = std::make_unique<juce::Component> ("alignScopePanel");
    readoutsGroup_.panel = std::make_unique<juce::Component> ("alignReadoutsPanel");
    controlsGroup_.panel = std::make_unique<juce::Component> ("alignControlsPanel");

    for (auto* g : { scopeGroup_.panel.get(),
                     readoutsGroup_.panel.get(),
                     controlsGroup_.panel.get() })
    {
        if (g) addAndMakeVisible (*g);
    }

    buildControls();
    ensureSnapshotCapacity (kInitialKeyCap, kInitialMainCap, kInitialCorrCap);

    // Start the refresh timer; it runs on the message thread and is a
    // no-op until setAlignmentScopeData() injects a model.
    startTimer (refreshMs_);
}

AlignPage::~AlignPage()
{
    stopTimer();
}

//==============================================================================
// Wiring.
//==============================================================================
void AlignPage::setAlignmentScopeData (ChugGate::Analysis::AlignmentScopeData* data) noexcept
{
    scopeData_ = data;
    refreshNow();
}

void AlignPage::setApvts (juce::AudioProcessorValueTreeState& apvts) noexcept
{
    apvts_ = &apvts;

    // Bind the manual trim slider to key.alignmentOffset.
    if (manualTrimSlider_ && apvts.getParameter (ChugGate::ParameterIDs::Key::alignmentOffset) != nullptr)
    {
        manualTrimAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts, ChugGate::ParameterIDs::Key::alignmentOffset, *manualTrimSlider_);
        manualTrimSlider_->setRange (-100.0, 100.0, 0.01);
        manualTrimSlider_->setValue (0.0, juce::dontSendNotification);
    }

    // Bind the lookahead readout combo to global.lookahead.
    if (lookaheadCombo_ && apvts.getParameter (ChugGate::ParameterIDs::Global::lookahead) != nullptr)
    {
        addChoiceItems (*lookaheadCombo_, apvts, ChugGate::ParameterIDs::Global::lookahead);
        lookaheadAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts, ChugGate::ParameterIDs::Global::lookahead, *lookaheadCombo_);
    }

    refreshNow();
}

void AlignPage::setCalibrateHandler (ButtonHandler handler)
{
    calibrateHandler_ = std::move (handler);
}

void AlignPage::setAcceptHandler (ButtonHandler handler)
{
    acceptHandler_ = std::move (handler);
}

void AlignPage::setRetryHandler (ButtonHandler handler)
{
    retryHandler_ = std::move (handler);
}

void AlignPage::refreshNow() noexcept
{
    repaint();
}

//==============================================================================
// juce::Timer — message-thread refresh of the local snapshot.
//==============================================================================
void AlignPage::timerCallback()
{
    refreshSnapshotFromScope();

    // Always repaint on the timer tick so the readouts reflect the
    // current snapshot (whether or not new data was available).
    repaint();
}

//==============================================================================
// Snapshot management.
//==============================================================================
void AlignPage::ensureSnapshotCapacity (int keyCap, int mainCap, int corrCap) noexcept
{
    if (static_cast<int> (snapshot_.keyOnset.size()) < keyCap)
        snapshot_.keyOnset.resize (static_cast<std::size_t> (keyCap), 0.0f);
    if (static_cast<int> (snapshot_.mainOnset.size()) < mainCap)
        snapshot_.mainOnset.resize (static_cast<std::size_t> (mainCap), 0.0f);
    if (static_cast<int> (snapshot_.correlation.size()) < corrCap)
        snapshot_.correlation.resize (static_cast<std::size_t> (corrCap), 0.0f);
}

void AlignPage::refreshSnapshotFromScope() noexcept
{
    // Snapshot scalars are always zero-initialised when no data is
    // available, so the readouts panel paints "no data" cleanly without
    // displaying stale figures from a previous capture.
    snapshot_.offsetMs              = 0.0f;
    snapshot_.confidence            = 0.0f;
    snapshot_.offsetVariationMs     = 0.0f;
    snapshot_.recommendedKeyDelayMs = 0.0f;
    snapshot_.mainLookaheadMs       = 0.0f;
    snapshot_.needsMainLookahead    = false;
    snapshot_.keyLength             = 0;
    snapshot_.mainLength            = 0;
    snapshot_.correlationLength     = 0;
    snapshot_.hasData               = false;

    if (scopeData_ == nullptr)
        return;

    // Grow the snapshot buffers to the model's capacity, then copy the
    // most recent valid write into them. copyKeyOnset / copyMainOnset /
    // copyCorrelationCurve never allocate and return the number of
    // samples actually written.
    ensureSnapshotCapacity (scopeData_->getKeyOnsetCapacity(),
                            scopeData_->getMainOnsetCapacity(),
                            scopeData_->getCorrelationCapacity());

    snapshot_.keyLength = scopeData_->copyKeyOnset (
        snapshot_.keyOnset.data(),
        static_cast<int> (snapshot_.keyOnset.size()));

    snapshot_.mainLength = scopeData_->copyMainOnset (
        snapshot_.mainOnset.data(),
        static_cast<int> (snapshot_.mainOnset.size()));

    snapshot_.correlationLength = scopeData_->copyCorrelationCurve (
        snapshot_.correlation.data(),
        static_cast<int> (snapshot_.correlation.size()));

    snapshot_.offsetMs              = scopeData_->getOffsetMs();
    snapshot_.confidence            = scopeData_->getConfidence();
    snapshot_.offsetVariationMs     = scopeData_->getOffsetVariationMs();
    snapshot_.recommendedKeyDelayMs = scopeData_->getRecommendedKeyDelayMs();
    snapshot_.mainLookaheadMs       = scopeData_->getMainLookaheadMs();
    snapshot_.needsMainLookahead    = scopeData_->needsMainLookahead();

    snapshot_.hasData = (snapshot_.keyLength > 0)
                     || (snapshot_.mainLength > 0)
                     || (snapshot_.correlationLength > 0);
}

//==============================================================================
// Component overrides.
//==============================================================================
void AlignPage::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // Page backdrop.
    g.setColour (ChugLookAndFeel::getPanelColour().withAlpha (0.85f));
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour());
    g.drawRoundedRectangle (bounds.toFloat(), 4.0f, 1.0f);

    // Page title.
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("ALIGN", bounds.removeFromTop (16), juce::Justification::centredLeft);

    // Group backgrounds (rendered under the controls so children paint on
    // top of them).
    paintGroupBackground (g, scopeGroup_.panel.get(),    "Alignment Scope");
    paintGroupBackground (g, readoutsGroup_.panel.get(), "Calibration Readouts");
    paintGroupBackground (g, controlsGroup_.panel.get(), "Calibration Controls");

    // Scope content.
    if (scopeGroup_.panel)
    {
        auto scopeBounds = scopeGroup_.panel->getBounds()
                               .reduced (kGroupInnerInset, kGroupInnerInset + kGroupTitleHeight / 2);
        if (! snapshot_.hasData || scopeData_ == nullptr)
            paintEmptyState (g, scopeBounds);
        else
            paintScopePanel (g, scopeBounds);
    }

    // Readouts content.
    if (readoutsGroup_.panel)
    {
        auto readoutsBounds = readoutsGroup_.panel->getBounds()
                                  .reduced (kGroupInnerInset, kGroupInnerInset + kGroupTitleHeight / 2);
        paintReadouts (g, readoutsBounds);
    }
}

void AlignPage::paintGroupBackground (juce::Graphics& g,
                                      juce::Component* panel,
                                      const juce::String& title)
{
    if (panel == nullptr) return;
    auto bounds = panel->getBounds().toFloat();

    // Sunken-panel background — page panel is lighter so the group reads
    // as a nested surface.
    g.setColour (ChugLookAndFeel::getSunkenPanelColour().withAlpha (0.55f));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    auto titleBounds = juce::Rectangle<int> (panel->getX() + kGroupInnerInset,
                                             panel->getY() + 2,
                                             panel->getWidth() - kGroupInnerInset * 2,
                                             kGroupTitleHeight);
    g.drawText (title, titleBounds, juce::Justification::centredLeft);
}

void AlignPage::paintEmptyState (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("No alignment data — press Calibrate/Align to capture onsets",
                area, juce::Justification::centred, true);
}

//==============================================================================
// Painting helpers.
//==============================================================================
void AlignPage::paintScopePanel (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.getHeight() <= 4)
        return;

    // Split the scope area into three stacked waveform lanes. Equal
    // weights; each lane gets at least 24 px of height so the label
    // remains legible.
    const int laneH = std::max (24, area.getHeight() / 3);
    juce::Rectangle<int> lane = area;

    auto keyLane   = lane.removeFromTop (laneH);
    auto mainLane  = lane.removeFromTop (laneH);
    auto corrLane  = lane; // remainder

    paintWaveformLane (g, keyLane,
                       snapshot_.keyOnset.data(), snapshot_.keyLength,
                       ChugLookAndFeel::getKeyTraceColour(), "Key onset");

    paintWaveformLane (g, mainLane,
                       snapshot_.mainOnset.data(), snapshot_.mainLength,
                       ChugLookAndFeel::getAudioTraceColour(), "Main onset");

    paintCorrelationLane (g, corrLane);
}

void AlignPage::paintWaveformLane (juce::Graphics& g, juce::Rectangle<int> area,
                                   const float* data, int length,
                                   juce::Colour traceColour,
                                   const juce::String& label)
{
    if (area.isEmpty()) return;

    // Lane background.
    g.setColour (ChugLookAndFeel::getSunkenPanelColour().withAlpha (0.35f));
    g.fillRect (area);
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.4f));
    g.drawRect (area, 1);

    // Centre line.
    const int cy = area.getCentreY();
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.3f));
    g.drawLine (static_cast<float> (area.getX()),
                static_cast<float> (cy),
                static_cast<float> (area.getRight()),
                static_cast<float> (cy),
                1.0f);

    // Waveform: bin the data into the available pixel width using a
    // max-magnitude reduction so each pixel shows the largest absolute
    // value in its column. The amplitude is normalised by the largest
    // |sample| in the buffer; for purely magnitude-onset traces (non-
    // negative) this yields a 1.0-amplitude fill, which matches the
    // way onset envelopes read in the spec.
    if (data != nullptr && length > 1)
    {
        const int width  = area.getWidth();
        const int height = area.getHeight() / 2 - 2;

        // Find the maximum absolute amplitude for normalisation. Skipped
        // when the buffer is all-zero (no data yet).
        float maxAbs = 0.0f;
        for (int i = 0; i < length; ++i)
            maxAbs = std::max (maxAbs, std::abs (data[i]));
        if (maxAbs <= 0.0f) maxAbs = 1.0f;

        // Build the polyline as a Path — one segment per pixel column.
        juce::Path path;
        const float xScale = static_cast<float> (width - 1)
                           / static_cast<float> (length - 1);
        for (int x = 0; x < width; ++x)
        {
            // Map this pixel to the source-sample range. Use a max-reduce
            // when the source samples-per-pixel > 1; use linear interp
            // when it is < 1.
            const float srcPosF = static_cast<float> (x) / xScale;
            const int   srcPos0 = juce::jlimit (0, length - 1,
                                                static_cast<int> (srcPosF));
            const int   srcPos1 = juce::jlimit (0, length - 1, srcPos0 + 1);
            const float frac    = srcPosF - static_cast<float> (srcPos0);

            const float v0 = data[srcPos0];
            const float v1 = data[srcPos1];
            const float v  = v0 + (v1 - v0) * frac;

            const float px = static_cast<float> (area.getX()) + static_cast<float> (x);
            const float py = static_cast<float> (cy)
                           - (v / maxAbs) * static_cast<float> (height);

            if (x == 0) path.startNewSubPath (px, py);
            else         path.lineTo (px, py);
        }

        g.setColour (traceColour);
        g.strokePath (path, juce::PathStrokeType (1.25f));
    }

    // Lane label.
    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    auto labelArea = area.reduced (4, 2).removeFromLeft (80);
    g.drawText (label, labelArea, juce::Justification::topLeft, true);
}

void AlignPage::paintCorrelationLane (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;

    // Lane background.
    g.setColour (ChugLookAndFeel::getSunkenPanelColour().withAlpha (0.35f));
    g.fillRect (area);
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.4f));
    g.drawRect (area, 1);

    // Centre line at zero. Correlation values are signed in [-1, 1].
    const int cy = area.getCentreY();
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.3f));
    g.drawLine (static_cast<float> (area.getX()),
                static_cast<float> (cy),
                static_cast<float> (area.getRight()),
                static_cast<float> (cy),
                1.0f);

    // +1 reference (top) and -1 reference (bottom) so the user can read
    // the correlation strength at a glance.
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.2f));
    g.drawLine (static_cast<float> (area.getX()),
                static_cast<float> (area.getY() + 2),
                static_cast<float> (area.getRight()),
                static_cast<float> (area.getY() + 2),
                1.0f);
    g.drawLine (static_cast<float> (area.getX()),
                static_cast<float> (area.getBottom() - 2),
                static_cast<float> (area.getRight()),
                static_cast<float> (area.getBottom() - 2),
                1.0f);

    if (snapshot_.correlationLength > 1 && snapshot_.correlation.data() != nullptr)
    {
        const int   width  = area.getWidth();
        const int   height = area.getHeight() / 2 - 4;
        const int   length = snapshot_.correlationLength;
        const float* data  = snapshot_.correlation.data();

        juce::Path path;
        const float xScale = static_cast<float> (width - 1)
                           / static_cast<float> (length - 1);
        for (int x = 0; x < width; ++x)
        {
            const float srcPosF = static_cast<float> (x) / xScale;
            const int   srcPos0 = juce::jlimit (0, length - 1,
                                                static_cast<int> (srcPosF));
            const int   srcPos1 = juce::jlimit (0, length - 1, srcPos0 + 1);
            const float frac    = srcPosF - static_cast<float> (srcPos0);

            const float v0 = data[srcPos0];
            const float v1 = data[srcPos1];
            const float v  = v0 + (v1 - v0) * frac;

            const float px = static_cast<float> (area.getX()) + static_cast<float> (x);
            const float py = static_cast<float> (cy)
                           - (v) * static_cast<float> (height);

            if (x == 0) path.startNewSubPath (px, py);
            else         path.lineTo (px, py);
        }

        // Highlight the selected offset bin. Map offsetMs onto the
        // correlation index (length is the full ±range, centred).
        g.setColour (ChugLookAndFeel::getAccentColour());
        g.strokePath (path, juce::PathStrokeType (1.5f));

        // Marker — vertical line at the selected offset. The correlation
        // curve has length N centred on lag = 0, so bin idx = N/2 +
        // round(offsetMs / maxLagMs * N/2). We don't know maxLagMs
        // exactly; use length/2 as the centre so the line lands on the
        // nominal zero-lag column when offsetMs is zero. Without a
        // reported maxLag we paint a "no marker" hint via a small
        // triangle on the centre line so the user can still see the
        // anchor.
        const int centreIdx = length / 2;
        const float centreX = static_cast<float> (area.getX())
                            + static_cast<float> (centreIdx) / static_cast<float> (length - 1)
                            * static_cast<float> (width - 1);
        g.setColour (ChugLookAndFeel::getAccentDimColour());
        g.drawLine (centreX, static_cast<float> (area.getY() + 4),
                    centreX, static_cast<float> (area.getBottom() - 4),
                    1.0f);
    }

    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    auto labelArea = area.reduced (4, 2).removeFromLeft (96);
    g.drawText ("Correlation", labelArea, juce::Justification::topLeft, true);
}

void AlignPage::paintReadouts (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;

    // Lay out the readouts in a 2-column grid. Each cell is ~12 px tall.
    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (10.0f));

    const int lineH = std::max (14, area.getHeight() / 3);
    auto rows = area;
    auto row  = [&] (int line) -> juce::Rectangle<int>
    {
        return juce::Rectangle<int> (rows.getX(),
                                     rows.getY() + line * lineH,
                                     rows.getWidth(),
                                     lineH).reduced (2, 0);
    };

    auto writeRow = [&] (int lineIdx, const juce::String& left, const juce::String& right)
    {
        auto r = row (lineIdx);
        const int halfW = r.getWidth() / 2;
        g.drawText (left,  juce::Rectangle<int> (r.getX(), r.getY(), halfW, r.getHeight()),
                    juce::Justification::centredLeft, true);
        g.drawText (right, juce::Rectangle<int> (r.getX() + halfW, r.getY(),
                                                 r.getWidth() - halfW, r.getHeight()),
                    juce::Justification::centredRight, true);
    };

    // No data → replace all readouts with placeholder text.
    if (! snapshot_.hasData || scopeData_ == nullptr)
    {
        g.setColour (ChugLookAndFeel::getMutedTextColour());
        g.drawText ("No data", area, juce::Justification::centred, true);
        return;
    }

    // Row 0: Offset / Confidence
    writeRow (0,
              "Offset: " + formatMs (snapshot_.offsetMs),
              "Confidence: " + formatConfidence (snapshot_.confidence));

    // Row 1: Variation / Recommended key delay
    writeRow (1,
              "Variation: " + formatMs (snapshot_.offsetVariationMs),
              "Key delay: " + formatMs (snapshot_.recommendedKeyDelayMs));

    // Row 2: Main lookahead / needs-main-lookahead flag
    writeRow (2,
              "Main lookahead: " + formatMs (snapshot_.mainLookaheadMs),
              snapshot_.needsMainLookahead
                  ? juce::String ("Needs lookahead: YES")
                  : juce::String ("Needs lookahead: no"));
}

//==============================================================================
// Resized.
//==============================================================================
void AlignPage::resized()
{
    auto area = getLocalBounds();
    if (area.isEmpty()) return;

    area.removeFromTop (16); // page title

    const int totalH = area.getHeight();
    const int gaps   = kRowGap * 2;

    const int scopeH    = std::max (kScopeMinH,
                                    static_cast<int> (totalH * kScopeWeight));
    const int readoutsH = std::max (kReadoutsMinH,
                                    static_cast<int> (totalH * kReadoutsWeight));
    const int controlsH = std::max (kControlsMinH,
                                    totalH - scopeH - readoutsH - gaps);

    layoutScopeRow    (area.removeFromTop (scopeH));    area.removeFromTop (kRowGap);
    layoutReadoutsRow (area.removeFromTop (readoutsH)); area.removeFromTop (kRowGap);
    layoutControlsRow (area.removeFromTop (controlsH));
}

//==============================================================================
// Layout helpers.
//==============================================================================
void AlignPage::layoutScopeRow (juce::Rectangle<int> area)
{
    if (scopeGroup_.panel) scopeGroup_.panel->setBounds (area);
}

void AlignPage::layoutReadoutsRow (juce::Rectangle<int> area)
{
    if (readoutsGroup_.panel) readoutsGroup_.panel->setBounds (area);
}

void AlignPage::layoutControlsRow (juce::Rectangle<int> area)
{
    if (controlsGroup_.panel) controlsGroup_.panel->setBounds (area);

    // Inner area (skip title bar + padding).
    auto inner = area.reduced (kGroupInnerInset,
                               kGroupTitleHeight + kGroupInnerInset);

    // Three buttons on the left, manual trim + lookahead readout on the
    // right. Heights split evenly between buttons and the trim row.
    if (inner.isEmpty()) return;

    const int buttonRowH = std::max (24, inner.getHeight() / 2);
    const int trimRowH   = inner.getHeight() - buttonRowH;

    auto buttonArea = inner.removeFromTop (buttonRowH);
    auto trimArea   = inner.removeFromTop (trimRowH);

    if (calibrateButton_ && acceptButton_ && retryButton_)
    {
        const int w = (buttonArea.getWidth() - kRowGap * 2) / 3;
        calibrateButton_->setBounds (buttonArea.removeFromLeft (w));
        buttonArea.removeFromLeft (kRowGap);
        acceptButton_->setBounds (buttonArea.removeFromLeft (w));
        buttonArea.removeFromLeft (kRowGap);
        retryButton_->setBounds (buttonArea);
    }

    if (trimArea.isEmpty()) return;

    // Trim row: left = slider (with label above), right = lookahead combo
    // (with label above).
    constexpr int trimLabelH = 14;
    auto trimTop = trimArea.removeFromTop (trimLabelH);

    const int comboW = std::min (160, trimArea.getWidth() / 3);
    auto sliderArea = trimArea;
    auto comboArea  = trimArea.removeFromRight (comboW);

    if (manualTrimSlider_)
    {
        manualTrimSlider_->setSliderStyle (juce::Slider::LinearHorizontal);
        manualTrimSlider_->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
        manualTrimSlider_->setBounds (sliderArea);
    }
    if (manualTrimLabel_)
        manualTrimLabel_->setBounds (trimTop.removeFromLeft (80));
    if (lookaheadCombo_)
        lookaheadCombo_->setBounds (comboArea);
    if (lookaheadLabel_)
        lookaheadLabel_->setBounds (trimTop.removeFromRight (comboW));
}

//==============================================================================
// Control construction.
//==============================================================================
void AlignPage::buildControls()
{
    // Calibrate / Accept / Retry buttons.
    calibrateButton_ = std::make_unique<juce::TextButton> ("Calibrate / Align");
    acceptButton_    = std::make_unique<juce::TextButton> ("Accept");
    retryButton_     = std::make_unique<juce::TextButton> ("Retry");

    calibrateButton_->setComponentID ("alignCalibrateButton");
    acceptButton_->setComponentID    ("alignAcceptButton");
    retryButton_->setComponentID     ("alignRetryButton");

    calibrateButton_->setTooltip    ("Start the DI alignment calibration capture (spec §13.5)");
    acceptButton_->setTooltip       ("Apply the captured offset to key.alignmentOffset");
    retryButton_->setTooltip        ("Discard the captured offset and try again");

    addAndMakeVisible (*calibrateButton_);
    addAndMakeVisible (*acceptButton_);
    addAndMakeVisible (*retryButton_);

    calibrateButton_->onClick = [this] { if (calibrateHandler_) calibrateHandler_(); };
    acceptButton_->onClick    = [this] { if (acceptHandler_)    acceptHandler_();    };
    retryButton_->onClick     = [this] { if (retryHandler_)     retryHandler_();     };

    // Manual trim slider (ms offset). Range matches
    // Key::alignmentOffset; the APVTS attachment overrides the range if
    // the parameter exists. setApvts() does the actual binding.
    manualTrimSlider_ = std::make_unique<juce::Slider> ("manualTrim");
    manualTrimSlider_->setSliderStyle (juce::Slider::LinearHorizontal);
    manualTrimSlider_->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
    manualTrimSlider_->setRange (-100.0, 100.0, 0.01);
    manualTrimSlider_->setValue (0.0, juce::dontSendNotification);
    manualTrimSlider_->setComponentID ("alignManualTrim");
    manualTrimSlider_->setTooltip ("Manual key alignment trim in ms (bound to key.alignmentOffset)");
    addAndMakeVisible (*manualTrimSlider_);

    manualTrimLabel_ = std::make_unique<juce::Label> ("manualTrimLabel", "Manual trim");
    manualTrimLabel_->setJustificationType (juce::Justification::centredLeft);
    manualTrimLabel_->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*manualTrimLabel_);

    // Lookahead readout combo (bound to global.lookahead).
    lookaheadCombo_ = std::make_unique<juce::ComboBox> ("lookaheadCombo");
    lookaheadCombo_->setComponentID ("alignLookahead");
    lookaheadCombo_->setTooltip ("Main-path lookahead (spec §17)");
    addAndMakeVisible (*lookaheadCombo_);

    lookaheadLabel_ = std::make_unique<juce::Label> ("lookaheadLabel", "Lookahead");
    lookaheadLabel_->setJustificationType (juce::Justification::centredLeft);
    lookaheadLabel_->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*lookaheadLabel_);
}

} // namespace ChugGate::GUI