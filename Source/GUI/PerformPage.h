// PerformPage.h
//
// Perform view (CHUGGATE_SPEC.md §21.4) — fast controls and macros.
//
// Hosts every primary control a user needs during a session:
//
//   - Threshold                            → global.thresholdOpen
//   - Tightness macro                      → cadence.amount (primary mapped param)
//   - Sustain macro                        → sustain.amount
//   - Hiss macro                           → hissCurtain.depth
//   - Depth (broadband floor)              → global.floor
//   - Attack Guard amount                  → attackGuard.amount
//   - Mode selector                        → global.mode
//   - Topology selector                    → global.topology
//   - Key source                           → key.source
//   - Lookahead                            → global.lookahead
//
// In addition two action hooks fire the editor-supplied listeners:
//
//   - "Learn Noise" — invokes ChugGateAudioProcessor::startNoiseLearn() via
//     the listener hook registered by the editor (does NOT run the learn
//     DSP itself; that's owned by the processor's analysis module).
//   - "Align DI"    — invokes the alignment start hook; likewise the editor
//     supplies the implementation that touches the AlignmentAnalyzer.
//
// A collapsible Expert Inspector (CHUGGATE_SPEC.md §21.6) lives at the
// bottom of the page. When expanded it surfaces every mapped underlying
// parameter beneath the macros so the user always sees what is being driven
// (no hidden parameters when expanded). Toggle via setExpertExpanded().
//
// All bound controls are owned and wired via APVTS attachments in the
// constructor; the page only deals with UI plumbing. PerformanceTimeline
// embedding is left to the editor (i.6) — a child Component placeholder is
// exposed via getTimelineMount() so the editor can add the timeline as a
// child of this page when desired.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "../ParameterIDs.h"

namespace ChugGate::GUI {

//==============================================================================
/**
 *  PerformPage — fast controls, macros and (optional) Expert Inspector row
 *  for the Perform view described in CHUGGATE_SPEC.md §21.4 / §21.5 /
 *  §21.6.
 *
 *  Construction requires an AudioProcessorValueTreeState reference which is
 *  used to wire every bound control. All attachments are owned; the page
 *  observes no DSP thread — every parameter read happens on the message
 *  thread through the APVTS.
 *
 *  Action buttons (Learn Noise, Align DI) call std::function listeners that
 *  the editor sets via setLearnNoiseHook / setAlignDIHook. Buttons do not
 *  perform any DSP themselves; the listeners delegate to the processor's
 *  analysis functions.
 *
 *  Threading:
 *      Construction, painting and resizing happen on the message thread.
 *      APVTS attachments are also message-thread only.
 */
class PerformPage : public juce::Component
{
public:
    //==========================================================================
    // Listener hook signatures. The editor installs std::function callbacks
    // that are fired from the message thread when the user presses the
    // corresponding button. Empty functions are a no-op.
    using ActionHook = std::function<void()>;

    //==========================================================================
    PerformPage (juce::AudioProcessorValueTreeState& apvts);
    ~PerformPage() override;

    //==========================================================================
    // Hook registration (message-thread). Setting either to an empty
    // std::function clears the hook. The buttons will still be visible and
    // respond visually but will not fire any DSP code.
    void setLearnNoiseHook (ActionHook hook);
    void setAlignDIHook    (ActionHook hook);

    //==========================================================================
    // Expert Inspector (§21.6) expand/collapse. Triggers a relayout. State
    // is held across resizes; default is collapsed so the page fits at the
    // minimum editor height.
    void setExpertExpanded (bool expanded);
    [[nodiscard]] bool isExpertExpanded() const noexcept { return expertExpanded_; }

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // Sub-components used by the page. We use std::unique_ptr for sliders
    // and combo boxes (JUCE attachment requires a stable address), and
    // std::unique_ptr for attachments so they can be destroyed before the
    // controls they bind.
    //==========================================================================
    // Primary knob row
    std::unique_ptr<juce::Slider>      thresholdSlider_;
    std::unique_ptr<juce::Label>       thresholdLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment_;

    std::unique_ptr<juce::Slider>      tightnessSlider_;
    std::unique_ptr<juce::Label>       tightnessLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tightnessAttachment_;

    std::unique_ptr<juce::Slider>      sustainSlider_;
    std::unique_ptr<juce::Label>       sustainLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainAttachment_;

    std::unique_ptr<juce::Slider>      hissSlider_;
    std::unique_ptr<juce::Label>       hissLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hissAttachment_;

    std::unique_ptr<juce::Slider>      depthSlider_;
    std::unique_ptr<juce::Label>       depthLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment_;

    std::unique_ptr<juce::Slider>      attackGuardSlider_;
    std::unique_ptr<juce::Label>       attackGuardLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardAttachment_;

    // Selector row
    std::unique_ptr<juce::ComboBox>     modeCombo_;
    std::unique_ptr<juce::Label>        modeLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment_;

    std::unique_ptr<juce::ComboBox>     topologyCombo_;
    std::unique_ptr<juce::Label>        topologyLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> topologyAttachment_;

    std::unique_ptr<juce::ComboBox>     keySourceCombo_;
    std::unique_ptr<juce::Label>        keySourceLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keySourceAttachment_;

    std::unique_ptr<juce::ComboBox>     lookaheadCombo_;
    std::unique_ptr<juce::Label>        lookaheadLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lookaheadAttachment_;

    // Action buttons
    std::unique_ptr<juce::TextButton>   learnNoiseButton_;
    std::unique_ptr<juce::TextButton>   alignDIButton_;

    // Expert Inspector (§21.6) — a single collapsible panel showing every
    // mapped underlying parameter beneath the macros. The button toggles
    // expansion; toggling setExpertExpanded() also drives the button's
    // toggle state.
    std::unique_ptr<juce::TextButton>   expertToggle_;
    std::unique_ptr<juce::Component>    expertPanel_;

    // Bound expert inspector controls (owned). All of these are APVTS-bound
    // when constructed; they live inside expertPanel_ and are made visible
    // / invisible when the inspector toggles.
    struct ExpertControl
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label>  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::vector<ExpertControl> expertControls_;

    //==========================================================================
    // Editor-supplied action hooks (std::function) — invoked by button
    // clicks on the message thread. Empty by default.
    ActionHook learnNoiseHook_;
    ActionHook alignDIHook_;

    //==========================================================================
    // APVTS reference (non-owning). The page never mutates the state; it
    // only constructs attachments against it.
    juce::AudioProcessorValueTreeState& apvts_;

    // Whether the Expert Inspector is currently expanded.
    bool expertExpanded_ = false;

    //==========================================================================
    // Helpers
    //==========================================================================
    // Build a rotary knob paired with a label. The slider is configured for
    // rotary style, the label is attached as the slider's name component so
    // the LookAndFeel can render it via its getLabelFont()/drawLabel()
    // overrides. Both components are added as child components of the page.
    void makeRotaryWithLabel (std::unique_ptr<juce::Slider>& sliderOut,
                              std::unique_ptr<juce::Label>&  labelOut,
                              const juce::String& name,
                              const juce::String& text);

    // Build a labelled combo box. The label sits above the combo box; both
    // are added as child components of the page.
    void makeComboWithLabel  (std::unique_ptr<juce::ComboBox>& comboOut,
                              std::unique_ptr<juce::Label>&    labelOut,
                              const juce::String& name,
                              const juce::String& text);

    // Construct all APVTS-bound controls and their attachments.
    void buildControls();

    // Build the Expert Inspector sub-panel. This creates the sliders for
    // every underlying parameter listed in §21.5 / §21.6 (the macros
    // primary knobs above are kept; this row surfaces the rest).
    void buildExpertInspector();

    // Tear down / rebuild the expert panel children visibility when the
    // expand state changes. The inspector's sliders remain bound the
    // whole time so changing params while collapsed still updates state.
    void updateExpertVisibility();

    // Resize helpers for each row.
    void layoutPrimaryRow  (juce::Rectangle<int> area);
    void layoutSelectorRow (juce::Rectangle<int> area);
    void layoutExpertRow   (juce::Rectangle<int> area);

    // Button click handlers (JUCE Button::Listener).
    void learnNoiseClicked();
    void alignDiClicked();
    void expertToggleClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformPage)
};

} // namespace ChugGate::GUI