// AlignPage.h
//
// Align view (CHUGGATE_SPEC.md §21.4 / §20.2 / §13).
//
// A juce::Component that displays the DI alignment scope (§20.2) and the
// calibration workflow controls (§13.5):
//
//   - key onset waveform (top lane)
//   - main-audio onset waveform (middle lane)
//   - correlation curve (bottom lane)
//   - scalar readouts: selected offset (ms), confidence (0..1),
//     offset variation (ms), recommended key delay (ms),
//     main lookahead (ms) / needs-main-lookahead flag
//   - control buttons: Calibrate/Align, Accept, Retry
//   - manual trim slider bound to key.alignmentOffset
//   - optional APVTS-bound Lookahead readout (global.lookahead)
//
// The component owns no DSP. All waveform/curve painting comes from a
// local snapshot refreshed on the message thread via
// AlignmentScopeData::copy*() (which itself is null-safe). The audio
// thread is never accessed from this file.
//
// Wiring model:
//   - The editor injects the analysis model via setAlignmentScopeData()
//     (non-owning pointer; may be null until the processor is wired up).
//   - The editor installs std::function callbacks via setCalibrateHandler,
//     setAcceptHandler, setRetryHandler. Buttons invoke those callbacks
//     on the message thread when clicked.
//   - The editor installs the APVTS via setApvts(); the manual trim
//     slider and lookahead readout bind through APVTS attachments if the
//     relevant parameter IDs exist. Missing IDs leave the controls
//     inert but visible.
//
// Threading: construction, painting, resizing and timer-driven snapshot
// refresh all happen on the message thread.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "../Analysis/AlignmentScopeData.h"

namespace ChugGate::GUI {

//==============================================================================
/**
 *  AlignPage — DI alignment scope and calibration workflow (spec §20.2 /
 *  §13.5).
 *
 *  Renders three stacked waveform/curve lanes from an
 *  AlignmentScopeData model and exposes the calibration workflow
 *  controls. The component is null-safe: a null analysis model paints
 *  empty-scope placeholders rather than crashing.
 *
 *  Public surface:
 *    - setAlignmentScopeData(pointer)  — inject the analysis model
 *    - setApvts(reference)             — install APVTS for attachments
 *    - setCalibrateHandler(fn)         — Calibrate/Align button
 *    - setAcceptHandler(fn)            — Accept button
 *    - setRetryHandler(fn)             — Retry button
 *    - refreshNow()                    — force a message-thread repaint
 *
 *  All snapshot reads happen on the message thread inside timerCallback().
 *  paint() reads only from member-owned snapshots.
 */
class AlignPage : public juce::Component,
                  private juce::Timer
{
public:
    //==========================================================================
    // Editor-supplied button handler signature. Empty std::function is a
    // no-op so the page remains usable during early wiring.
    using ButtonHandler = std::function<void()>;

    //==========================================================================
    AlignPage();
    ~AlignPage() override;

    //==========================================================================
    // Wiring.
    //==========================================================================
    // Inject the analysis-side model (non-owning). Pass nullptr to
    // disconnect; subsequent paints show empty-scope placeholders. Safe
    // to call from the message thread.
    void setAlignmentScopeData (ChugGate::Analysis::AlignmentScopeData* data) noexcept;

    // Install the APVTS used by the manual trim slider and the
    // lookahead readout. Attachments are created lazily inside the
    // constructor; missing parameter IDs leave the controls inert.
    void setApvts (juce::AudioProcessorValueTreeState& apvts) noexcept;

    // Register button handlers. Empty handlers are valid (button click is
    // then a visual no-op). Safe to call from the message thread.
    void setCalibrateHandler (ButtonHandler handler);
    void setAcceptHandler    (ButtonHandler handler);
    void setRetryHandler     (ButtonHandler handler);

    // Force a message-thread repaint (e.g. after setAlignmentScopeData).
    void refreshNow() noexcept;

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // juce::Timer — drives the message-thread snapshot refresh.
    void timerCallback() override;

    //==========================================================================
    // Snapshot data the paint path reads from. Allocated in the ctor and
    // resized on the message thread. Snapshot content is updated only on
    // the message thread inside refreshSnapshot().
    struct ScopeSnapshot
    {
        std::vector<float> keyOnset;
        std::vector<float> mainOnset;
        std::vector<float> correlation;

        int  keyLength    = 0;
        int  mainLength   = 0;
        int  correlationLength = 0;

        float offsetMs              = 0.0f;
        float confidence            = 0.0f;
        float offsetVariationMs     = 0.0f;
        float recommendedKeyDelayMs = 0.0f;
        float mainLookaheadMs       = 0.0f;
        bool  needsMainLookahead    = false;

        bool hasData = false; // true once any waveform/curve is non-empty
    };

    void refreshSnapshotFromScope() noexcept;

    //==========================================================================
    // Snapshot owned by this component (message-thread only). ensureCapacity
    // grows the vectors to a target size without ever shrinking below the
    // most recent peak length — paint always reads valid data.
    void ensureSnapshotCapacity (int keyCap, int mainCap, int corrCap) noexcept;

    //==========================================================================
    // Control construction helpers.
    void buildControls();

    //==========================================================================
    // Painting helpers — each renders into the supplied rectangle using
    // only the local snapshot and ChugLookAndFeel palette. Waveform lanes
    // share a common painter because the only difference between the
    // three is colour and the numeric range they normalise against.
    void paintScopePanel     (juce::Graphics& g, juce::Rectangle<int> area);
    void paintWaveformLane   (juce::Graphics& g, juce::Rectangle<int> area,
                              const float* data, int length,
                              juce::Colour traceColour, const juce::String& label);
    void paintCorrelationLane(juce::Graphics& g, juce::Rectangle<int> area);
    void paintReadouts       (juce::Graphics& g, juce::Rectangle<int> area);
    void paintEmptyState     (juce::Graphics& g, juce::Rectangle<int> area);

    void paintGroupBackground (juce::Graphics& g,
                               juce::Component* panel,
                               const juce::String& title);

    //==========================================================================
    // Layout helpers — assign rectangles to each visible region.
    void layoutScopeRow   (juce::Rectangle<int> area);
    void layoutReadoutsRow(juce::Rectangle<int> area);
    void layoutControlsRow(juce::Rectangle<int> area);

    //==========================================================================
    // Section panels (groups). Each owns its own background component.
    // The bold title text is rendered by paintGroupBackground() directly
    // (via Graphics::drawText) so we don't need a juce::Label child per
    // group — keeps the component graph minimal.
    struct GroupPanel
    {
        std::unique_ptr<juce::Component> panel;
    };

    GroupPanel scopeGroup_;
    GroupPanel readoutsGroup_;
    GroupPanel controlsGroup_;

    //==========================================================================
    // Buttons + sliders.
    std::unique_ptr<juce::TextButton> calibrateButton_;
    std::unique_ptr<juce::TextButton> acceptButton_;
    std::unique_ptr<juce::TextButton> retryButton_;

    std::unique_ptr<juce::Slider>      manualTrimSlider_;
    std::unique_ptr<juce::Label>       manualTrimLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        manualTrimAttachment_;

    std::unique_ptr<juce::ComboBox>    lookaheadCombo_;
    std::unique_ptr<juce::Label>       lookaheadLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        lookaheadAttachment_;

    //==========================================================================
    // Editor-supplied button handlers.
    ButtonHandler calibrateHandler_;
    ButtonHandler acceptHandler_;
    ButtonHandler retryHandler_;

    //==========================================================================
    // Analysis-side data (non-owning). nullptr produces empty-scope paint.
    ChugGate::Analysis::AlignmentScopeData* scopeData_ = nullptr;

    // APVTS reference (non-owning). nullptr leaves attachments unbound.
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;

    // Snapshot owned by the page (message-thread only).
    ScopeSnapshot snapshot_;

    // Refresh cadence (ms). 33 ms ≈ 30 fps — fast enough that the
    // waveform refresh feels live but slow enough not to peg the
    // message thread.
    int refreshMs_ = 33;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AlignPage)
};

} // namespace ChugGate::GUI