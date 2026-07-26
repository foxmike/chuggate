// PerformPage.cpp
//
// See PerformPage.h. Implements the Perform view (CHUGGATE_SPEC.md §21.4
// / §21.5 / §21.6): primary knob row (Threshold + four macros + Depth +
// Attack Guard), selector row (mode / topology / key source / lookahead),
// action buttons (Learn Noise, Align DI) and a collapsible Expert
// Inspector row that surfaces every mapped underlying parameter.
//
// All controls are APVTS-bound. Buttons fire editor-supplied std::function
// hooks (setLearnNoiseHook / setAlignDIHook); no DSP is invoked from this
// file.
//
// Layout (resized() computes the rectangles; this file does not change
// window size):
//
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  [knob] [knob] [knob] [knob] [knob] [knob]   ← primary knob row   │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  [mode] [topology] [key src] [lookahead] [Learn] [Align DI]        │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  ▾ Expert Inspector  (collapsible row of every mapped underlying   │
//   │                       parameter — §21.6 "no hidden parameters")    │
//   └────────────────────────────────────────────────────────────────────┘
//
// The Perform view also hosts the Performance Timeline (§21.4) which the
// editor (i.6) embeds as a sibling above this page; we do not embed the
// timeline here so the editor owns final placement.

#include "PerformPage.h"

#include "ChugLookAndFeel.h"

namespace ChugGate::GUI {

namespace
{
    // Vertical layout weights — primary row, selector row, expert row.
    constexpr float kPrimaryRowWeight  = 0.50f;
    constexpr float kSelectorRowWeight = 0.22f;
    constexpr float kExpertRowWeight   = 0.28f;

    // Horizontal padding between adjacent knobs/selectors.
    constexpr int kHorizontalPadding  = 12;
    constexpr int kVerticalPadding    = 8;
    constexpr int kOuterInset         = 8;
    constexpr int kRowGap              = 6;

    // Label height above the control.
    constexpr int kLabelHeight        = 18;

    // Expert inspector sub-row heights.
    constexpr int kExpertToggleHeight = 22;
    constexpr int kExpertRowGap       = 6;
    constexpr int kExpertColumnWidth  = 110;
    constexpr int kExpertRowHeight    = 44;

    // Helper — apply common styling (text box position, name component) to
    // a freshly constructed rotary slider so every knob on the page reads
    // the same way. JUCE's slider can be styled in a LookAndFeel override;
    // setting the text box / name here keeps the layout deterministic.
    void styleRotary (juce::Slider& slider, juce::Label& nameLabel)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
        slider.setName (nameLabel.getName());
        // The name label is rendered above the slider (see layout); tell
        // the slider not to attach a label of its own.
        slider.setComponentID ("performRotary");
    }

    void styleCombo (juce::ComboBox& combo, juce::Label& nameLabel)
    {
        combo.setName (nameLabel.getName());
        combo.setComponentID ("performCombo");
    }

    // Bind a mode-style AudioParameterChoice's items to a combo box so the
    // index round-trips through the APVTS. Items are added with the same
    // labels used in the parameter layout (PluginProcessor.cpp). When the
    // APVTS parameter doesn't exist (e.g. partial wiring during early
    // development), this is a no-op so the page still constructs.
    void addChoiceItems (juce::ComboBox& box,
                         juce::AudioProcessorValueTreeState& apvts,
                         const char* paramId,
                         std::initializer_list<juce::String> items)
    {
        if (apvts.getParameter (paramId) == nullptr)
            return;
        box.clear();
        int id = 1;
        for (const auto& s : items)
            box.addItem (s, id++);
    }
} // namespace

//==============================================================================
// Construction / destruction.
//==============================================================================
PerformPage::PerformPage (juce::AudioProcessorValueTreeState& apvts)
    : apvts_ (apvts)
{
    setOpaque (false);
    setWantsKeyboardFocus (false);

    buildControls();
    buildExpertInspector();
    updateExpertVisibility();
}

PerformPage::~PerformPage() = default;

//==============================================================================
// Hook registration.
//==============================================================================
void PerformPage::setLearnNoiseHook (ActionHook hook)
{
    learnNoiseHook_ = std::move (hook);
}

void PerformPage::setAlignDIHook (ActionHook hook)
{
    alignDIHook_ = std::move (hook);
}

//==============================================================================
// Expert Inspector toggle.
//==============================================================================
void PerformPage::setExpertExpanded (bool expanded)
{
    if (expertExpanded_ == expanded) return;
    expertExpanded_ = expanded;
    if (expertToggle_)
        expertToggle_->setToggleState (expanded, juce::dontSendNotification);
    updateExpertVisibility();
    resized();
}

void PerformPage::updateExpertVisibility()
{
    if (expertPanel_)
        expertPanel_->setVisible (expertExpanded_);
}

//==============================================================================
// Component overrides.
//==============================================================================
void PerformPage::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // Backdrop — translucent panel so the page reads as a single surface
    // regardless of host background.
    g.setColour (ChugLookAndFeel::getPanelColour().withAlpha (0.85f));
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour());
    g.drawRoundedRectangle (bounds.toFloat(), 4.0f, 1.0f);

    // Section title bar — fast controls.
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("PERFORM", bounds.removeFromTop (16), juce::Justification::centredLeft);
}

void PerformPage::resized()
{
    auto area = getLocalBounds();
    if (area.isEmpty()) return;

    // Skip the title bar.
    area.removeFromTop (16);

    const int totalH = area.getHeight();
    const int primaryH  = std::max (100, static_cast<int> (totalH * kPrimaryRowWeight));
    const int selectorH = std::max (60,  static_cast<int> (totalH * kSelectorRowWeight));
    const int expertH   = std::max (0,   totalH - primaryH - selectorH);

    layoutPrimaryRow  (area.removeFromTop (primaryH));
    area.removeFromTop (kVerticalPadding);
    layoutSelectorRow (area.removeFromTop (selectorH));
    area.removeFromTop (kVerticalPadding);
    layoutExpertRow   (area.removeFromTop (expertH));
}

//==============================================================================
// Layout helpers.
//==============================================================================
void PerformPage::layoutPrimaryRow (juce::Rectangle<int> area)
{
    // Six knobs in the primary row: Threshold, Tightness, Sustain, Hiss,
    // Depth, Attack Guard. Each gets a column with a label above the knob.
    constexpr int numKnobs = 6;
    const int colW = (area.getWidth() - kHorizontalPadding * (numKnobs - 1)) / numKnobs;

    auto placeKnob = [&] (juce::Slider* s, juce::Label* l)
    {
        if (s == nullptr || l == nullptr) return;
        auto col = area.removeFromLeft (colW);
        if (! area.isEmpty()) area.removeFromLeft (kHorizontalPadding);
        l->setBounds (col.removeFromTop (kLabelHeight));
        s->setBounds (col);
    };

    placeKnob (thresholdSlider_.get(),   thresholdLabel_.get());
    placeKnob (tightnessSlider_.get(),   tightnessLabel_.get());
    placeKnob (sustainSlider_.get(),     sustainLabel_.get());
    placeKnob (hissSlider_.get(),        hissLabel_.get());
    placeKnob (depthSlider_.get(),       depthLabel_.get());
    placeKnob (attackGuardSlider_.get(), attackGuardLabel_.get());
}

void PerformPage::layoutSelectorRow (juce::Rectangle<int> area)
{
    // Four combo selectors + two action buttons. Combos take the left
    // portion; buttons take the right.
    constexpr int numCombos = 4;
    const int comboW = (area.getWidth() - kHorizontalPadding * (numCombos - 1)) / (numCombos + 2);
    const int buttonW = comboW * 2 + kHorizontalPadding;

    auto placeCombo = [&] (juce::ComboBox* c, juce::Label* l)
    {
        if (c == nullptr || l == nullptr) return;
        auto col = area.removeFromLeft (comboW);
        if (! area.isEmpty()) area.removeFromLeft (kHorizontalPadding);
        l->setBounds (col.removeFromTop (kLabelHeight));
        c->setBounds (col);
    };

    placeCombo (modeCombo_.get(),      modeLabel_.get());
    placeCombo (topologyCombo_.get(),  topologyLabel_.get());
    placeCombo (keySourceCombo_.get(), keySourceLabel_.get());
    placeCombo (lookaheadCombo_.get(), lookaheadLabel_.get());

    // Action buttons — Learn Noise + Align DI on the right.
    if (learnNoiseButton_ && alignDIButton_)
    {
        auto buttonArea = area.removeFromRight (buttonW);
        if (buttonArea.getWidth() > 0)
        {
            const int halfW = (buttonArea.getWidth() - kHorizontalPadding) / 2;
            learnNoiseButton_->setBounds (buttonArea.removeFromLeft (halfW));
            buttonArea.removeFromLeft (kHorizontalPadding);
            alignDIButton_->setBounds (buttonArea);
        }
    }
}

void PerformPage::layoutExpertRow (juce::Rectangle<int> area)
{
    if (! expertToggle_) return;

    // Toggle row at the top.
    expertToggle_->setBounds (area.removeFromTop (kExpertToggleHeight));
    area.removeFromTop (kRowGap);

    if (expertExpanded_ && expertPanel_)
    {
        expertPanel_->setBounds (area);
        expertPanel_->setVisible (true);

        // Lay out the expert inspector sliders in a wrapping grid. We use
        // a fixed column width so the layout is predictable at the minimum
        // editor width (~920). Spillover scrolls vertically — the panel
        // sets its own visible area; controls beyond the bottom are simply
        // not positioned (paint handles overflow gracefully).
        const int cols = std::max (1, area.getWidth() / (kExpertColumnWidth + kHorizontalPadding));
        const int rowH = kExpertRowHeight;
        int col = 0, row = 0;
        const int innerTop = kOuterInset;
        for (auto& ec : expertControls_)
        {
            if (! ec.slider || ! ec.label) continue;
            const int x = kOuterInset + col * (kExpertColumnWidth + kHorizontalPadding);
            const int y = innerTop + row * (rowH + kExpertRowGap);
            auto cellR = juce::Rectangle<int> (x, y, kExpertColumnWidth, rowH);
            ec.label->setBounds (cellR.removeFromTop (kLabelHeight));
            ec.slider->setBounds (cellR);
            ++col;
            if (col >= cols)
            {
                col = 0;
                ++row;
            }
        }
    }
    else if (expertPanel_)
    {
        expertPanel_->setVisible (false);
    }
}

//==============================================================================
// Control construction.
//==============================================================================
void PerformPage::makeRotaryWithLabel (std::unique_ptr<juce::Slider>& sliderOut,
                                       std::unique_ptr<juce::Label>&  labelOut,
                                       const juce::String& name,
                                       const juce::String& text)
{
    sliderOut = std::make_unique<juce::Slider> (name);
    labelOut  = std::make_unique<juce::Label>  (name, text);
    labelOut->setJustificationType (juce::Justification::centred);
    labelOut->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*sliderOut);
    addAndMakeVisible (*labelOut);
    styleRotary (*sliderOut, *labelOut);
}

void PerformPage::makeComboWithLabel (std::unique_ptr<juce::ComboBox>& comboOut,
                                      std::unique_ptr<juce::Label>&    labelOut,
                                      const juce::String& name,
                                      const juce::String& text)
{
    comboOut = std::make_unique<juce::ComboBox> (name);
    labelOut = std::make_unique<juce::Label>    (name, text);
    labelOut->setJustificationType (juce::Justification::centredLeft);
    labelOut->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*comboOut);
    addAndMakeVisible (*labelOut);
    styleCombo (*comboOut, *labelOut);
}

void PerformPage::buildControls()
{
    using namespace ChugGate::ParameterIDs;

    // Primary knob row --------------------------------------------------------
    makeRotaryWithLabel (thresholdSlider_,   thresholdLabel_,
                         "thresholdSlider",  "Threshold");
    thresholdAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, Global::thresholdOpen, *thresholdSlider_);

    makeRotaryWithLabel (tightnessSlider_,   tightnessLabel_,
                         "tightnessSlider",  "Tightness");
    tightnessAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, Cadence::amount, *tightnessSlider_);

    makeRotaryWithLabel (sustainSlider_,     sustainLabel_,
                         "sustainSlider",    "Sustain");
    sustainAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, Sustain::amount, *sustainSlider_);

    makeRotaryWithLabel (hissSlider_,        hissLabel_,
                         "hissSlider",       "Hiss");
    hissAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, HissCurtain::depth, *hissSlider_);

    makeRotaryWithLabel (depthSlider_,       depthLabel_,
                         "depthSlider",      "Depth");
    depthAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, Global::floor, *depthSlider_);

    makeRotaryWithLabel (attackGuardSlider_, attackGuardLabel_,
                         "attackGuardSlider","Attack Guard");
    attackGuardAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, AttackGuard::amount, *attackGuardSlider_);

    // Selector row ------------------------------------------------------------
    makeComboWithLabel (modeCombo_,      modeLabel_,      "modeCombo",      "Mode");
    addChoiceItems (*modeCombo_, apvts_, Global::mode,
                    { "Chug", "Lead", "Hybrid", "Manual" });
    modeAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts_, Global::mode, *modeCombo_);

    makeComboWithLabel (topologyCombo_,  topologyLabel_,  "topologyCombo",  "Topology");
    addChoiceItems (*topologyCombo_, apvts_, Global::topology,
                    { "FullBand", "DualBand", "TriBand", "HissCurtain" });
    topologyAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts_, Global::topology, *topologyCombo_);

    makeComboWithLabel (keySourceCombo_, keySourceLabel_, "keySourceCombo", "Key Source");
    addChoiceItems (*keySourceCombo_, apvts_, Key::source,
                    { "Main Input", "External Sidechain", "Blended", "Left", "Right",
                      "Stereo Max", "Stereo Average", "Mid", "Side" });
    keySourceAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts_, Key::source, *keySourceCombo_);

    makeComboWithLabel (lookaheadCombo_, lookaheadLabel_, "lookaheadCombo", "Lookahead");
    addChoiceItems (*lookaheadCombo_, apvts_, Global::lookahead,
                    { "Off", "0.25 ms", "0.5 ms", "1 ms", "2 ms", "5 ms", "10 ms" });
    lookaheadAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts_, Global::lookahead, *lookaheadCombo_);

    // Action buttons ----------------------------------------------------------
    learnNoiseButton_ = std::make_unique<juce::TextButton> ("Learn Noise");
    alignDIButton_    = std::make_unique<juce::TextButton> ("Align DI");
    learnNoiseButton_->setComponentID ("learnNoiseButton");
    alignDIButton_->setComponentID    ("alignDIButton");
    addAndMakeVisible (*learnNoiseButton_);
    addAndMakeVisible (*alignDIButton_);

    learnNoiseButton_->onClick = [this] { learnNoiseClicked(); };
    alignDIButton_->onClick    = [this] { alignDiClicked(); };
}

void PerformPage::buildExpertInspector()
{
    using namespace ChugGate::ParameterIDs;

    // Toggle button — always visible; collapses / expands the row.
    expertToggle_ = std::make_unique<juce::TextButton> ("Expert Inspector");
    expertToggle_->setComponentID ("expertToggle");
    expertToggle_->setClickingTogglesState (true);
    expertToggle_->setToggleState (expertExpanded_, juce::dontSendNotification);
    addAndMakeVisible (*expertToggle_);
    expertToggle_->onClick = [this] { expertToggleClicked(); };

    // Panel — collapsed by default. Lives invisibly as a sibling until
    // expanded; visibility is driven in updateExpertVisibility().
    expertPanel_ = std::make_unique<juce::Component> ("expertPanel");
    addChildComponent (*expertPanel_);

    // All underlying parameters that the macros / selectors above don't
    // already expose (§21.5 + §21.6 "no hidden parameters when expanded").
    struct ExpertEntry
    {
        const char* paramId;
        const char* label;
    };

    const ExpertEntry entries[] = {
        // Global envelope
        { Global::hysteresis,        "Hysteresis"        },
        { Global::attack,            "Attack"            },
        { Global::hold,              "Hold"              },
        { Global::release,           "Release"           },
        { Global::maxAdaptiveRelease,"Max Rel."          },
        { Global::ratio,             "Ratio"             },
        { Global::knee,              "Knee"              },
        { Global::curve,             "Curve"             },
        // Attack Guard detail
        { AttackGuard::sensitivity,  "AG Sensitivity"    },
        { AttackGuard::frequency,    "AG Frequency"      },
        { AttackGuard::maxThresholdReduction, "AG Max Red." },
        // Sustain detail
        { Sustain::releaseExtension, "Sustain Ext."      },
        { Sustain::harmonicWeight,   "Sustain Harm."     },
        { Sustain::decayWeight,      "Sustain Decay"     },
        // Cadence detail
        { Cadence::releaseFraction,  "Cadence Rel."      },
        // Noise Learn detail
        { NoiseLearn::snrMargin,     "Noise Margin"      },
    };

    expertControls_.clear();
    expertControls_.reserve (std::size (entries));
    for (const auto& e : entries)
    {
        ExpertControl ec;
        ec.slider = std::make_unique<juce::Slider> (e.label);
        ec.slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        ec.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 16);
        ec.label = std::make_unique<juce::Label> (e.label, e.label);
        ec.label->setJustificationType (juce::Justification::centred);
        ec.label->setInterceptsMouseClicks (false, false);
        if (apvts_.getParameter (e.paramId) != nullptr)
        {
            ec.attachment =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts_, e.paramId, *ec.slider);
        }
        expertPanel_->addAndMakeVisible (*ec.slider);
        expertPanel_->addAndMakeVisible (*ec.label);
        expertControls_.push_back (std::move (ec));
    }
}

//==============================================================================
// Button handlers.
//==============================================================================
void PerformPage::learnNoiseClicked()
{
    if (learnNoiseHook_) learnNoiseHook_();
}

void PerformPage::alignDiClicked()
{
    if (alignDIHook_) alignDIHook_();
}

void PerformPage::expertToggleClicked()
{
    setExpertExpanded (expertToggle_->getToggleState());
}

} // namespace ChugGate::GUI