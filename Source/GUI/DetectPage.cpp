// DetectPage.cpp
//
// See DetectPage.h. Implements the Detect view (CHUGGATE_SPEC.md §21.4)
// for key conditioning, hum rejection, threshold, transient (Attack
// Guard) and sustain analysis controls.
//
// All controls are APVTS-bound. No DSP edits. Layout is computed in
// resized(); this file does not change window size.
//
// Layout (computed in resized()):
//
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  DETECT                                                            │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  KEY CONDITIONING   [HPF row]  [LPF row]  [Emphasis row]  [Align]  │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  HUM                [mode] [harmonics] [Q] [immunity]  [Listen]   │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  THRESHOLD          [open] [hysteresis] [attack] [hold] [release]  │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  ATTACK GUARD       [en] [amount] [sens] [freq] [decay] [soft] …  │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  SUSTAIN            [en] [amount] [ext] [harm] [decay] [noise] …   │
//   │     Feedback Friend  [en] [sens] [dur] [hold] [fmin] [fmax] …      │
//   └────────────────────────────────────────────────────────────────────┘
//
// The page lays out its five groups vertically with equal weight so every
// group is visible at the minimum editor height. Controls are arranged in
// rows inside each group, falling back to wrapping at the minimum width.

#include "DetectPage.h"

#include "ChugLookAndFeel.h"

namespace ChugGate::GUI {

namespace
{
    //----------------------------------------------------------------------
    // Layout constants.
    //----------------------------------------------------------------------
    constexpr int  kOuterInset         = 8;
    constexpr int  kRowGap             = 6;
    constexpr int  kColumnPadding      = 10;
    constexpr int  kLabelHeight        = 16;
    constexpr int  kGroupTitleHeight   = 18;
    constexpr int  kGroupInnerInset    = 6;

    // Five groups — equal vertical weights so every section remains
    // visible at the minimum editor height.
    constexpr float kKeyWeight          = 0.24f;
    constexpr float kHumWeight          = 0.14f;
    constexpr float kThresholdWeight    = 0.16f;
    constexpr float kAttackGuardWeight  = 0.22f;
    constexpr float kSustainWeight      = 0.24f;

    //----------------------------------------------------------------------
    // Style helpers — kept local because the styling intent differs
    // slightly between sections (some pages want a labelled rotary knob,
    // others want a labelled combo).
    //----------------------------------------------------------------------
    void styleRotary (juce::Slider& slider, juce::Label& nameLabel)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
        slider.setName (nameLabel.getName());
        slider.setComponentID ("detectRotary");
        // Sliders are keyboard-focusable by default in JUCE — leaving
        // wantsKeyboardFocus at the default value keeps tab navigation
        // working between adjacent controls.
    }

    void styleLinear (juce::Slider& slider, juce::Label& nameLabel)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 16);
        slider.setName (nameLabel.getName());
        slider.setComponentID ("detectLinear");
    }

    void styleCombo (juce::ComboBox& combo, juce::Label& nameLabel)
    {
        combo.setName (nameLabel.getName());
        combo.setComponentID ("detectCombo");
    }

    void styleToggle (juce::ToggleButton& button)
    {
        button.setComponentID ("detectToggle");
    }

    //----------------------------------------------------------------------
    // Populate a combo box from an AudioParameterChoice stored in the
    // APVTS. When the parameter isn't an AudioParameterChoice (or doesn't
    // exist at all) we leave the combo empty — the page remains usable.
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
    // Slider style chooser — rotary by default, switch to linear for
    // clearly-numeric one-dimensional values that read better as a
    // horizontal bar (alignment offset, decay, durations). Using a small
    // function lets every section opt in by passing a style hint without
    // duplicating the slider construction code.
    //----------------------------------------------------------------------
    enum class KnobStyle { Rotary, Linear };
    void styleSlider (juce::Slider& slider, juce::Label& nameLabel, KnobStyle s)
    {
        if (s == KnobStyle::Linear) styleLinear (slider, nameLabel);
        else                          styleRotary (slider, nameLabel);
    }
} // namespace

//==============================================================================
// GroupPanel helper.
//==============================================================================
DetectPage::GroupPanel::GroupPanel (const juce::String& name,
                                    const juce::String& titleText)
{
    panel = std::make_unique<juce::Component> (name);
    title = std::make_unique<juce::Label>     (name, titleText);
    title->setJustificationType (juce::Justification::centredLeft);
    title->setInterceptsMouseClicks (false, false);
}

//==============================================================================
// Construction / destruction.
//==============================================================================
DetectPage::DetectPage (juce::AudioProcessorValueTreeState& apvts)
    : keyGroup_         ("keyGroup",         "Key Conditioning"),
      humGroup_         ("humGroup",         "Hum (Powerline Shield)"),
      thresholdGroup_   ("thresholdGroup",   "Threshold / Detector"),
      attackGuardGroup_ ("attackGuardGroup", "Attack Guard"),
      sustainGroup_     ("sustainGroup",     "Sustain Intelligence"),
      apvts_            (apvts)
{
    setOpaque (false);
    setWantsKeyboardFocus (false);

    // Build groups. Order is the order they appear top-to-bottom.
    buildKeyGroup();
    buildHumGroup();
    buildThresholdGroup();
    buildAttackGuardGroup();
    buildSustainGroup();
}

DetectPage::~DetectPage() = default;

//==============================================================================
// Component overrides.
//==============================================================================
void DetectPage::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // Page backdrop.
    g.setColour (ChugLookAndFeel::getPanelColour().withAlpha (0.85f));
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour());
    g.drawRoundedRectangle (bounds.toFloat(), 4.0f, 1.0f);

    // Section title.
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("DETECT", bounds.removeFromTop (16), juce::Justification::centredLeft);

    // Draw each group background + title. We don't recompute the layout
    // here — resized() is the source of truth for the layout positions;
    // this just ensures the title text always renders at the top of each
    // group's bounds.
    paintGroupBackground (g, keyGroup_,         "Key Conditioning");
    paintGroupBackground (g, humGroup_,         "Hum (Powerline Shield)");
    paintGroupBackground (g, thresholdGroup_,   "Threshold / Detector");
    paintGroupBackground (g, attackGuardGroup_, "Attack Guard");
    paintGroupBackground (g, sustainGroup_,     "Sustain Intelligence");
}

void DetectPage::paintGroupBackground (juce::Graphics& g,
                                       GroupPanel& group,
                                       const juce::String& titleText)
{
    if (! group.panel) return;
    auto bounds = group.panel->getBounds().toFloat();

    // Sunken-panel background — the page panel is lighter than this so the
    // group reads as a nested surface.
    g.setColour (ChugLookAndFeel::getSunkenPanelColour().withAlpha (0.55f));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    // Title rendered above the group contents.
    if (group.title)
    {
        g.setColour (ChugLookAndFeel::getTextColour());
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        auto titleBounds = juce::Rectangle<int> (group.panel->getX() + kGroupInnerInset,
                                                 group.panel->getY() + 2,
                                                 group.panel->getWidth() - kGroupInnerInset * 2,
                                                 kGroupTitleHeight);
        g.drawText (titleText, titleBounds, juce::Justification::centredLeft);
    }
}

void DetectPage::resized()
{
    auto area = getLocalBounds();
    if (area.isEmpty()) return;

    area.removeFromTop (16); // page title

    const int totalH = area.getHeight();
    const int gaps   = kRowGap * 4;

    // Compute vertical weights so each group gets a fair share; floors at
    // a reasonable minimum so very short pages still render something
    // legible.
    const int keyH         = std::max (110, static_cast<int> (totalH * kKeyWeight));
    const int humH         = std::max (70,  static_cast<int> (totalH * kHumWeight));
    const int thresholdH   = std::max (80,  static_cast<int> (totalH * kThresholdWeight));
    const int attackGuardH = std::max (110, static_cast<int> (totalH * kAttackGuardWeight));
    const int sustainH     = std::max (110, totalH - keyH - humH - thresholdH - attackGuardH - gaps);

    layoutKeyGroup         (area.removeFromTop (keyH));         area.removeFromTop (kRowGap);
    layoutHumGroup         (area.removeFromTop (humH));         area.removeFromTop (kRowGap);
    layoutThresholdGroup   (area.removeFromTop (thresholdH));   area.removeFromTop (kRowGap);
    layoutAttackGuardGroup (area.removeFromTop (attackGuardH)); area.removeFromTop (kRowGap);
    layoutSustainGroup     (area.removeFromTop (sustainH));
}

//==============================================================================
// Construction helpers.
//==============================================================================
void DetectPage::makeRotaryWithLabel (std::unique_ptr<juce::Slider>& sliderOut,
                                      std::unique_ptr<juce::Label>&  labelOut,
                                      const juce::String& name,
                                      const juce::String& text,
                                      juce::Component* parent)
{
    sliderOut = std::make_unique<juce::Slider> (name);
    labelOut  = std::make_unique<juce::Label>  (name, text);
    labelOut->setJustificationType (juce::Justification::centred);
    labelOut->setInterceptsMouseClicks (false, false);
    if (parent != nullptr) parent->addAndMakeVisible (*sliderOut);
    else                   addAndMakeVisible  (*sliderOut);
    if (parent != nullptr) parent->addAndMakeVisible (*labelOut);
    else                   addAndMakeVisible  (*labelOut);
    styleRotary (*sliderOut, *labelOut);
}

void DetectPage::makeComboWithLabel (std::unique_ptr<juce::ComboBox>& comboOut,
                                     std::unique_ptr<juce::Label>&    labelOut,
                                     const juce::String& name,
                                     const juce::String& text,
                                     juce::Component* parent)
{
    comboOut = std::make_unique<juce::ComboBox> (name);
    labelOut = std::make_unique<juce::Label>    (name, text);
    labelOut->setJustificationType (juce::Justification::centredLeft);
    labelOut->setInterceptsMouseClicks (false, false);
    if (parent != nullptr) parent->addAndMakeVisible (*comboOut);
    else                   addAndMakeVisible  (*comboOut);
    if (parent != nullptr) parent->addAndMakeVisible (*labelOut);
    else                   addAndMakeVisible  (*labelOut);
    styleCombo (*comboOut, *labelOut);
}

void DetectPage::makeToggle (std::unique_ptr<juce::ToggleButton>& toggleOut,
                             const juce::String& name,
                             const juce::String& text,
                             juce::Component* parent)
{
    toggleOut = std::make_unique<juce::ToggleButton> (text);
    toggleOut->setName (name);
    if (parent != nullptr) parent->addAndMakeVisible (*toggleOut);
    else                   addAndMakeVisible  (*toggleOut);
    styleToggle (*toggleOut);
}

//==============================================================================
// Key Conditioning group (§5).
//==============================================================================
void DetectPage::buildKeyGroup()
{
    using namespace ChugGate::ParameterIDs;

    // Group panel.
    addAndMakeVisible (*keyGroup_.panel);
    if (keyGroup_.title) keyGroup_.panel->addAndMakeVisible (*keyGroup_.title);
    auto* panel = keyGroup_.panel.get();

    // HPF — enabled toggle + frequency + slope.
    makeToggle (keyHpfEnabledToggle_, "keyHpfEnabled", "HPF", panel);
    if (apvts_.getParameter (Key::hpfEnabled) != nullptr)
    {
        keyHpfEnabledAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, Key::hpfEnabled, *keyHpfEnabledToggle_);
    }

    makeRotaryWithLabel (keyHpfFrequencySlider_, keyHpfFrequencyLabel_,
                         "keyHpfFreq", "Freq", panel);
    if (apvts_.getParameter (Key::hpfFrequency) != nullptr)
    {
        keyHpfFrequencyAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::hpfFrequency, *keyHpfFrequencySlider_);
    }

    makeComboWithLabel (keyHpfSlopeCombo_, keyHpfSlopeLabel_,
                        "keyHpfSlope", "Slope", panel);
    addChoiceItems (*keyHpfSlopeCombo_, apvts_, Key::hpfSlope);
    if (apvts_.getParameter (Key::hpfSlope) != nullptr)
    {
        keyHpfSlopeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, Key::hpfSlope, *keyHpfSlopeCombo_);
    }

    // LPF — enabled toggle + frequency + slope.
    makeToggle (keyLpfEnabledToggle_, "keyLpfEnabled", "LPF", panel);
    if (apvts_.getParameter (Key::lpfEnabled) != nullptr)
    {
        keyLpfEnabledAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, Key::lpfEnabled, *keyLpfEnabledToggle_);
    }

    makeRotaryWithLabel (keyLpfFrequencySlider_, keyLpfFrequencyLabel_,
                         "keyLpfFreq", "Freq", panel);
    if (apvts_.getParameter (Key::lpfFrequency) != nullptr)
    {
        keyLpfFrequencyAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::lpfFrequency, *keyLpfFrequencySlider_);
    }

    makeComboWithLabel (keyLpfSlopeCombo_, keyLpfSlopeLabel_,
                        "keyLpfSlope", "Slope", panel);
    addChoiceItems (*keyLpfSlopeCombo_, apvts_, Key::lpfSlope);
    if (apvts_.getParameter (Key::lpfSlope) != nullptr)
    {
        keyLpfSlopeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, Key::lpfSlope, *keyLpfSlopeCombo_);
    }

    // Emphasis — frequency / gain / Q.
    makeRotaryWithLabel (keyEmphasisFreqSlider_, keyEmphasisFreqLabel_,
                         "keyEmphasisFreq", "Emphasis Freq", panel);
    if (apvts_.getParameter (Key::emphasisFreq) != nullptr)
    {
        keyEmphasisFreqAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::emphasisFreq, *keyEmphasisFreqSlider_);
    }

    makeRotaryWithLabel (keyEmphasisGainSlider_, keyEmphasisGainLabel_,
                         "keyEmphasisGain", "Gain", panel);
    if (apvts_.getParameter (Key::emphasisGain) != nullptr)
    {
        keyEmphasisGainAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::emphasisGain, *keyEmphasisGainSlider_);
    }

    makeRotaryWithLabel (keyEmphasisQSlider_, keyEmphasisQLabel_,
                         "keyEmphasisQ", "Q", panel);
    if (apvts_.getParameter (Key::emphasisQ) != nullptr)
    {
        keyEmphasisQAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::emphasisQ, *keyEmphasisQSlider_);
    }

    // Alignment offset (linear slider — value is in ms and reads cleanly
    // as a horizontal bar with a centred 0 indicator).
    makeRotaryWithLabel (keyAlignmentOffsetSlider_, keyAlignmentOffsetLabel_,
                         "keyAlignmentOffset", "Align", panel);
    styleSlider (*keyAlignmentOffsetSlider_, *keyAlignmentOffsetLabel_, KnobStyle::Linear);
    if (apvts_.getParameter (Key::alignmentOffset) != nullptr)
    {
        keyAlignmentOffsetAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::alignmentOffset, *keyAlignmentOffsetSlider_);
    }

    // Fallback + Listen.
    makeComboWithLabel (keyFallbackCombo_, keyFallbackLabel_,
                        "keyFallback", "Fallback", panel);
    addChoiceItems (*keyFallbackCombo_, apvts_, Key::fallbackMode);
    if (apvts_.getParameter (Key::fallbackMode) != nullptr)
    {
        keyFallbackAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, Key::fallbackMode, *keyFallbackCombo_);
    }

    makeComboWithLabel (keyListenModeCombo_, keyListenModeLabel_,
                        "keyListenMode", "Listen", panel);
    addChoiceItems (*keyListenModeCombo_, apvts_, Key::listenMode);
    if (apvts_.getParameter (Key::listenMode) != nullptr)
    {
        keyListenModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, Key::listenMode, *keyListenModeCombo_);
    }
}

void DetectPage::layoutKeyGroup (juce::Rectangle<int> area)
{
    if (! keyGroup_.panel) return;
    keyGroup_.panel->setBounds (area);

    auto inner = area.reduced (kGroupInnerInset);
    inner.removeFromTop (kGroupTitleHeight); // leave room for the title

    // Four logical sub-rows: HPF, LPF, Emphasis, Aux (Align + Fallback +
    // Listen). Each sub-row gets an equal vertical share so the layout
    // remains readable at the minimum width.
    const int subRows = 4;
    const int colP    = kColumnPadding;
    const int subH    = (inner.getHeight() - colP * (subRows - 1)) / subRows;

    // We need 4 separate row rects because each row layout is independent;
    // capturing them by value means row 2 doesn't depend on row 1's
    // residual width.
    auto row1 = inner.removeFromTop (subH);
    inner.removeFromTop (colP);
    auto row2 = inner.removeFromTop (subH);
    inner.removeFromTop (colP);
    auto row3 = inner.removeFromTop (subH);
    inner.removeFromTop (colP);
    auto row4 = inner.removeFromTop (subH);

    auto takeCol = [] (juce::Rectangle<int>& areaIn, int colW, int idx,
                       int n) -> juce::Rectangle<int>
    {
        auto r = areaIn.removeFromLeft (colW);
        if (idx < n - 1) areaIn.removeFromLeft (colP);
        return r;
    };

    // HPF row — toggle + frequency + slope (3 columns).
    {
        constexpr int n = 3;
        const int colW = (row1.getWidth() - colP * (n - 1)) / n;
        if (keyHpfEnabledToggle_) keyHpfEnabledToggle_->setBounds (takeCol (row1, colW, 0, n));
        auto rc1 = takeCol (row1, colW, 1, n);
        if (keyHpfFrequencyLabel_) keyHpfFrequencyLabel_->setBounds (rc1.removeFromTop (kLabelHeight));
        if (keyHpfFrequencySlider_) keyHpfFrequencySlider_->setBounds (rc1);
        auto rc2 = takeCol (row1, colW, 2, n);
        if (keyHpfSlopeLabel_) keyHpfSlopeLabel_->setBounds (rc2.removeFromTop (kLabelHeight));
        if (keyHpfSlopeCombo_)  keyHpfSlopeCombo_->setBounds  (rc2);
    }

    // LPF row — toggle + frequency + slope (3 columns).
    {
        constexpr int n = 3;
        const int colW = (row2.getWidth() - colP * (n - 1)) / n;
        if (keyLpfEnabledToggle_) keyLpfEnabledToggle_->setBounds (takeCol (row2, colW, 0, n));
        auto rc1 = takeCol (row2, colW, 1, n);
        if (keyLpfFrequencyLabel_) keyLpfFrequencyLabel_->setBounds (rc1.removeFromTop (kLabelHeight));
        if (keyLpfFrequencySlider_) keyLpfFrequencySlider_->setBounds (rc1);
        auto rc2 = takeCol (row2, colW, 2, n);
        if (keyLpfSlopeLabel_) keyLpfSlopeLabel_->setBounds (rc2.removeFromTop (kLabelHeight));
        if (keyLpfSlopeCombo_)  keyLpfSlopeCombo_->setBounds  (rc2);
    }

    // Emphasis row — three knobs side-by-side.
    {
        constexpr int n = 3;
        const int colW = (row3.getWidth() - colP * (n - 1)) / n;
        auto placeKnob = [&] (int idx, juce::Label* l, juce::Slider* s)
        {
            auto col = takeCol (row3, colW, idx, n);
            if (l != nullptr) l->setBounds (col.removeFromTop (kLabelHeight));
            if (s != nullptr) s->setBounds (col);
        };
        placeKnob (0, keyEmphasisFreqLabel_.get(), keyEmphasisFreqSlider_.get());
        placeKnob (1, keyEmphasisGainLabel_.get(), keyEmphasisGainSlider_.get());
        placeKnob (2, keyEmphasisQLabel_.get(),    keyEmphasisQSlider_.get());
    }

    // Aux row — alignment (linear slider) + fallback combo + listen combo.
    {
        constexpr int n = 3;
        const int colW = (row4.getWidth() - colP * (n - 1)) / n;
        auto rc0 = takeCol (row4, colW, 0, n);
        if (keyAlignmentOffsetLabel_) keyAlignmentOffsetLabel_->setBounds (rc0.removeFromTop (kLabelHeight));
        if (keyAlignmentOffsetSlider_) keyAlignmentOffsetSlider_->setBounds (rc0);
        auto rc1 = takeCol (row4, colW, 1, n);
        if (keyFallbackLabel_) keyFallbackLabel_->setBounds (rc1.removeFromTop (kLabelHeight));
        if (keyFallbackCombo_)  keyFallbackCombo_->setBounds  (rc1);
        auto rc2 = takeCol (row4, colW, 2, n);
        if (keyListenModeLabel_) keyListenModeLabel_->setBounds (rc2.removeFromTop (kLabelHeight));
        if (keyListenModeCombo_)  keyListenModeCombo_->setBounds  (rc2);
    }
}

//==============================================================================
// Hum group (§5.4).
//==============================================================================
void DetectPage::buildHumGroup()
{
    using namespace ChugGate::ParameterIDs;

    addAndMakeVisible (*humGroup_.panel);
    if (humGroup_.title) humGroup_.panel->addAndMakeVisible (*humGroup_.title);
    auto* panel = humGroup_.panel.get();

    makeComboWithLabel (humModeCombo_, humModeLabel_,
                        "humMode", "Mode", panel);
    addChoiceItems (*humModeCombo_, apvts_, Key::humMode);
    if (apvts_.getParameter (Key::humMode) != nullptr)
    {
        humModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, Key::humMode, *humModeCombo_);
    }

    makeComboWithLabel (humHarmonicsCombo_, humHarmonicsLabel_,
                        "humHarmonics", "Harmonics", panel);
    addChoiceItems (*humHarmonicsCombo_, apvts_, Key::humHarmonics);
    if (apvts_.getParameter (Key::humHarmonics) != nullptr)
    {
        humHarmonicsAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, Key::humHarmonics, *humHarmonicsCombo_);
    }

    makeRotaryWithLabel (humNotchQSlider_, humNotchQLabel_,
                         "humNotchQ", "Notch Q", panel);
    if (apvts_.getParameter (Key::humNotchQ) != nullptr)
    {
        humNotchQAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::humNotchQ, *humNotchQSlider_);
    }

    makeRotaryWithLabel (humImmunitySlider_, humImmunityLabel_,
                         "humImmunity", "Immunity", panel);
    if (apvts_.getParameter (Key::humImmunityAmount) != nullptr)
    {
        humImmunityAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Key::humImmunityAmount, *humImmunitySlider_);
    }
}

void DetectPage::layoutHumGroup (juce::Rectangle<int> area)
{
    if (! humGroup_.panel) return;
    humGroup_.panel->setBounds (area);

    auto inner = area.reduced (kGroupInnerInset);
    inner.removeFromTop (kGroupTitleHeight);

    // Single row of four controls.
    constexpr int n = 4;
    const int colW = (inner.getWidth() - kColumnPadding * (n - 1)) / n;

    auto placeKnob = [&] (int idx, juce::Label* l, juce::Component* c,
                          bool isCombo)
    {
        if (c == nullptr) return;
        auto col = inner.removeFromLeft (colW);
        if (idx < n - 1) inner.removeFromLeft (kColumnPadding);
        if (l != nullptr) l->setBounds (col.removeFromTop (kLabelHeight));
        c->setBounds (col);
        juce::ignoreUnused (isCombo);
    };

    placeKnob (0, humModeLabel_.get(),      humModeCombo_.get(),      true);
    placeKnob (1, humHarmonicsLabel_.get(), humHarmonicsCombo_.get(), true);
    placeKnob (2, humNotchQLabel_.get(),    humNotchQSlider_.get(),   false);
    placeKnob (3, humImmunityLabel_.get(),  humImmunitySlider_.get(), false);
}

//==============================================================================
// Threshold / Detector group (§6 / §7.2).
//==============================================================================
void DetectPage::buildThresholdGroup()
{
    using namespace ChugGate::ParameterIDs;

    addAndMakeVisible (*thresholdGroup_.panel);
    if (thresholdGroup_.title) thresholdGroup_.panel->addAndMakeVisible (*thresholdGroup_.title);
    auto* panel = thresholdGroup_.panel.get();

    // Open threshold.
    makeRotaryWithLabel (thresholdOpenSlider_, thresholdOpenLabel_,
                         "thresholdOpen", "Open", panel);
    if (apvts_.getParameter (Global::thresholdOpen) != nullptr)
    {
        thresholdOpenAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Global::thresholdOpen, *thresholdOpenSlider_);
    }

    // Hysteresis.
    makeRotaryWithLabel (hysteresisSlider_, hysteresisLabel_,
                         "hysteresis", "Hyst.", panel);
    if (apvts_.getParameter (Global::hysteresis) != nullptr)
    {
        hysteresisAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Global::hysteresis, *hysteresisSlider_);
    }

    // Detector ballistics — attack / hold / release.
    makeRotaryWithLabel (attackSlider_, attackLabel_,
                         "attack", "Attack", panel);
    if (apvts_.getParameter (Global::attack) != nullptr)
    {
        attackAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Global::attack, *attackSlider_);
    }

    makeRotaryWithLabel (holdSlider_, holdLabel_,
                         "hold", "Hold", panel);
    if (apvts_.getParameter (Global::hold) != nullptr)
    {
        holdAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Global::hold, *holdSlider_);
    }

    makeRotaryWithLabel (releaseSlider_, releaseLabel_,
                         "release", "Release", panel);
    if (apvts_.getParameter (Global::release) != nullptr)
    {
        releaseAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Global::release, *releaseSlider_);
    }
}

void DetectPage::layoutThresholdGroup (juce::Rectangle<int> area)
{
    if (! thresholdGroup_.panel) return;
    thresholdGroup_.panel->setBounds (area);

    auto inner = area.reduced (kGroupInnerInset);
    inner.removeFromTop (kGroupTitleHeight);

    // Single row of five knobs.
    constexpr int n = 5;
    const int colW = (inner.getWidth() - kColumnPadding * (n - 1)) / n;

    auto placeKnob = [&] (int idx, juce::Label* l, juce::Slider* s)
    {
        if (s == nullptr) return;
        auto col = inner.removeFromLeft (colW);
        if (idx < n - 1) inner.removeFromLeft (kColumnPadding);
        if (l != nullptr) l->setBounds (col.removeFromTop (kLabelHeight));
        s->setBounds (col);
    };

    placeKnob (0, thresholdOpenLabel_.get(), thresholdOpenSlider_.get());
    placeKnob (1, hysteresisLabel_.get(),    hysteresisSlider_.get());
    placeKnob (2, attackLabel_.get(),        attackSlider_.get());
    placeKnob (3, holdLabel_.get(),          holdSlider_.get());
    placeKnob (4, releaseLabel_.get(),       releaseSlider_.get());
}

//==============================================================================
// Attack Guard group (§8).
//==============================================================================
void DetectPage::buildAttackGuardGroup()
{
    using namespace ChugGate::ParameterIDs;

    addAndMakeVisible (*attackGuardGroup_.panel);
    if (attackGuardGroup_.title) attackGuardGroup_.panel->addAndMakeVisible (*attackGuardGroup_.title);
    auto* panel = attackGuardGroup_.panel.get();

    makeToggle (attackGuardEnabledToggle_, "attackGuardEnabled", "Enabled", panel);
    if (apvts_.getParameter (AttackGuard::enabled) != nullptr)
    {
        attackGuardEnabledAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, AttackGuard::enabled, *attackGuardEnabledToggle_);
    }

    makeRotaryWithLabel (attackGuardAmountSlider_, attackGuardAmountLabel_,
                         "attackGuardAmount", "Amount", panel);
    if (apvts_.getParameter (AttackGuard::amount) != nullptr)
    {
        attackGuardAmountAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, AttackGuard::amount, *attackGuardAmountSlider_);
    }

    makeRotaryWithLabel (attackGuardSensitivitySlider_, attackGuardSensitivityLabel_,
                         "attackGuardSens", "Sens.", panel);
    if (apvts_.getParameter (AttackGuard::sensitivity) != nullptr)
    {
        attackGuardSensitivityAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, AttackGuard::sensitivity, *attackGuardSensitivitySlider_);
    }

    makeRotaryWithLabel (attackGuardFrequencySlider_, attackGuardFrequencyLabel_,
                         "attackGuardFreq", "Freq", panel);
    if (apvts_.getParameter (AttackGuard::frequency) != nullptr)
    {
        attackGuardFrequencyAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, AttackGuard::frequency, *attackGuardFrequencySlider_);
    }

    makeRotaryWithLabel (attackGuardDecaySlider_, attackGuardDecayLabel_,
                         "attackGuardDecay", "Decay", panel);
    if (apvts_.getParameter (AttackGuard::decay) != nullptr)
    {
        attackGuardDecayAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, AttackGuard::decay, *attackGuardDecaySlider_);
    }

    makeRotaryWithLabel (attackGuardSoftAssistSlider_, attackGuardSoftAssistLabel_,
                         "attackGuardSoft", "Soft", panel);
    if (apvts_.getParameter (AttackGuard::softNoteAssist) != nullptr)
    {
        attackGuardSoftAssistAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, AttackGuard::softNoteAssist, *attackGuardSoftAssistSlider_);
    }

    makeRotaryWithLabel (attackGuardMaxReductionSlider_, attackGuardMaxReductionLabel_,
                         "attackGuardMaxRed", "Max Red.", panel);
    if (apvts_.getParameter (AttackGuard::maxThreshReduction) != nullptr)
    {
        attackGuardMaxReductionAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, AttackGuard::maxThreshReduction, *attackGuardMaxReductionSlider_);
    }

    makeRotaryWithLabel (attackGuardConfirmationSlider_, attackGuardConfirmationLabel_,
                         "attackGuardConfirm", "Confirm", panel);
    if (apvts_.getParameter (AttackGuard::confirmationWindow) != nullptr)
    {
        attackGuardConfirmationAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, AttackGuard::confirmationWindow, *attackGuardConfirmationSlider_);
    }
}

void DetectPage::layoutAttackGuardGroup (juce::Rectangle<int> area)
{
    if (! attackGuardGroup_.panel) return;
    attackGuardGroup_.panel->setBounds (area);

    auto inner = area.reduced (kGroupInnerInset);
    inner.removeFromTop (kGroupTitleHeight);

    // Two rows of four columns. Row 1: enabled, amount, sens, freq.
    // Row 2: confirm, decay, soft, max red.
    constexpr int n = 4;
    const int colW = (inner.getWidth() - kColumnPadding * (n - 1)) / n;
    const int rowH = (inner.getHeight() - kColumnPadding) / 2;

    auto row1 = inner.removeFromTop (rowH);
    inner.removeFromTop (kColumnPadding);
    auto row2 = inner;

    auto placeKnobRow = [&] (juce::Rectangle<int>& r, int idx,
                             juce::Label* l, juce::Component* c)
    {
        auto col = r.removeFromLeft (colW);
        if (idx < n - 1) r.removeFromLeft (kColumnPadding);
        if (l != nullptr) l->setBounds (col.removeFromTop (kLabelHeight));
        if (c != nullptr) c->setBounds (col);
    };

    // Row 1 — toggle fills column 0 without a label; columns 1..3 are
    // labelled knobs (amount, sensitivity, frequency).
    {
        auto c0 = row1.removeFromLeft (colW);
        row1.removeFromLeft (kColumnPadding);
        if (attackGuardEnabledToggle_) attackGuardEnabledToggle_->setBounds (c0);
        placeKnobRow (row1, 1, attackGuardAmountLabel_.get(),     attackGuardAmountSlider_.get());
        placeKnobRow (row1, 2, attackGuardSensitivityLabel_.get(),attackGuardSensitivitySlider_.get());
        placeKnobRow (row1, 3, attackGuardFrequencyLabel_.get(),  attackGuardFrequencySlider_.get());
    }

    // Row 2 — four labelled knobs: confirmation, decay, soft assist, max
    // threshold reduction. No toggle on this row.
    placeKnobRow (row2, 0, attackGuardConfirmationLabel_.get(),attackGuardConfirmationSlider_.get());
    placeKnobRow (row2, 1, attackGuardDecayLabel_.get(),        attackGuardDecaySlider_.get());
    placeKnobRow (row2, 2, attackGuardSoftAssistLabel_.get(),   attackGuardSoftAssistSlider_.get());
    placeKnobRow (row2, 3, attackGuardMaxReductionLabel_.get(), attackGuardMaxReductionSlider_.get());
}

//==============================================================================
// Sustain Intelligence group (§9 / §10).
//==============================================================================
void DetectPage::buildSustainGroup()
{
    using namespace ChugGate::ParameterIDs;

    addAndMakeVisible (*sustainGroup_.panel);
    if (sustainGroup_.title) sustainGroup_.panel->addAndMakeVisible (*sustainGroup_.title);
    auto* panel = sustainGroup_.panel.get();

    // Sustain core.
    makeToggle (sustainEnabledToggle_, "sustainEnabled", "Enabled", panel);
    if (apvts_.getParameter (Sustain::enabled) != nullptr)
    {
        sustainEnabledAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, Sustain::enabled, *sustainEnabledToggle_);
    }

    makeRotaryWithLabel (sustainAmountSlider_, sustainAmountLabel_,
                         "sustainAmount", "Amount", panel);
    if (apvts_.getParameter (Sustain::amount) != nullptr)
    {
        sustainAmountAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::amount, *sustainAmountSlider_);
    }

    makeRotaryWithLabel (sustainReleaseExtensionSlider_, sustainReleaseExtensionLabel_,
                         "sustainExt", "Ext.", panel);
    if (apvts_.getParameter (Sustain::releaseExtension) != nullptr)
    {
        sustainReleaseExtensionAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::releaseExtension, *sustainReleaseExtensionSlider_);
    }

    makeRotaryWithLabel (sustainHarmonicWeightSlider_, sustainHarmonicWeightLabel_,
                         "sustainHarm", "Harm.", panel);
    if (apvts_.getParameter (Sustain::harmonicWeight) != nullptr)
    {
        sustainHarmonicWeightAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::harmonicWeight, *sustainHarmonicWeightSlider_);
    }

    makeRotaryWithLabel (sustainDecayWeightSlider_, sustainDecayWeightLabel_,
                         "sustainDecay", "Decay", panel);
    if (apvts_.getParameter (Sustain::decayWeight) != nullptr)
    {
        sustainDecayWeightAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::decayWeight, *sustainDecayWeightSlider_);
    }

    makeRotaryWithLabel (sustainNoiseWeightSlider_, sustainNoiseWeightLabel_,
                         "sustainNoise", "Noise", panel);
    if (apvts_.getParameter (Sustain::noiseWeight) != nullptr)
    {
        sustainNoiseWeightAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::noiseWeight, *sustainNoiseWeightSlider_);
    }

    // Feedback Friend (§10).
    makeToggle (feedbackEnabledToggle_, "feedbackEnabled", "FB", panel);
    if (apvts_.getParameter (Sustain::feedbackEnabled) != nullptr)
    {
        feedbackEnabledAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, Sustain::feedbackEnabled, *feedbackEnabledToggle_);
    }

    makeRotaryWithLabel (feedbackSensitivitySlider_, feedbackSensitivityLabel_,
                         "feedbackSens", "Sens.", panel);
    if (apvts_.getParameter (Sustain::feedbackSensitivity) != nullptr)
    {
        feedbackSensitivityAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::feedbackSensitivity, *feedbackSensitivitySlider_);
    }

    makeRotaryWithLabel (feedbackMinDurationSlider_, feedbackMinDurationLabel_,
                         "feedbackMinDur", "Min Dur", panel);
    if (apvts_.getParameter (Sustain::feedbackMinDuration) != nullptr)
    {
        feedbackMinDurationAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::feedbackMinDuration, *feedbackMinDurationSlider_);
    }

    makeRotaryWithLabel (feedbackMaxHoldSlider_, feedbackMaxHoldLabel_,
                         "feedbackMaxHold", "Max Hold", panel);
    if (apvts_.getParameter (Sustain::feedbackMaxHold) != nullptr)
    {
        feedbackMaxHoldAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::feedbackMaxHold, *feedbackMaxHoldSlider_);
    }

    makeRotaryWithLabel (feedbackFreqMinSlider_, feedbackFreqMinLabel_,
                         "feedbackFMin", "F Min", panel);
    if (apvts_.getParameter (Sustain::feedbackFreqMin) != nullptr)
    {
        feedbackFreqMinAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::feedbackFreqMin, *feedbackFreqMinSlider_);
    }

    makeRotaryWithLabel (feedbackFreqMaxSlider_, feedbackFreqMaxLabel_,
                         "feedbackFMax", "F Max", panel);
    if (apvts_.getParameter (Sustain::feedbackFreqMax) != nullptr)
    {
        feedbackFreqMaxAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::feedbackFreqMax, *feedbackFreqMaxSlider_);
    }

    makeRotaryWithLabel (feedbackFloorSlider_, feedbackFloorLabel_,
                         "feedbackFloor", "Floor", panel);
    if (apvts_.getParameter (Sustain::feedbackFloor) != nullptr)
    {
        feedbackFloorAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::feedbackFloor, *feedbackFloorSlider_);
    }

    makeToggle (feedbackRequireExtKeyToggle_, "feedbackRequireExt", "Ext Key", panel);
    if (apvts_.getParameter (Sustain::feedbackRequireExtKey) != nullptr)
    {
        feedbackRequireExtKeyAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, Sustain::feedbackRequireExtKey, *feedbackRequireExtKeyToggle_);
    }

    makeRotaryWithLabel (feedbackSafetyTimeoutSlider_, feedbackSafetyTimeoutLabel_,
                         "feedbackSafety", "Safety", panel);
    styleSlider (*feedbackSafetyTimeoutSlider_, *feedbackSafetyTimeoutLabel_, KnobStyle::Linear);
    if (apvts_.getParameter (Sustain::feedbackSafetyTimeout) != nullptr)
    {
        feedbackSafetyTimeoutAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, Sustain::feedbackSafetyTimeout, *feedbackSafetyTimeoutSlider_);
    }
}

void DetectPage::layoutSustainGroup (juce::Rectangle<int> area)
{
    if (! sustainGroup_.panel) return;
    sustainGroup_.panel->setBounds (area);

    auto inner = area.reduced (kGroupInnerInset);
    inner.removeFromTop (kGroupTitleHeight);

    // Three sub-sections stacked vertically:
    //   - Sustain core       (1 row × 6 columns)
    //   - Feedback Friend    (2 rows × 4 columns)
    //   - Safety timeout     (1 row × 1 linear slider, full width)
    constexpr int fbCols = 4;
    const int colW = (inner.getWidth() - kColumnPadding * (fbCols - 1)) / fbCols;
    const int fbRowH = std::max (40, colW);
    const int coreRowH = std::max (40, std::min (inner.getHeight() / 4, colW));
    const int safetyH  = std::max (28, std::min (36, inner.getHeight() / 8));

    // Sustain core row — enabled toggle + 5 labelled knobs.
    auto coreRow = inner.removeFromTop (coreRowH);
    inner.removeFromTop (kColumnPadding);
    {
        constexpr int n = 6;
        const int coreColW = (coreRow.getWidth() - kColumnPadding * (n - 1)) / n;
        auto placeKnob = [&] (juce::Rectangle<int>& r, int idx,
                              juce::Component* first,
                              juce::Label* l, juce::Component* c)
        {
            auto col = r.removeFromLeft (coreColW);
            if (idx < n - 1) r.removeFromLeft (kColumnPadding);
            if (first != nullptr)
            {
                first->setBounds (col);
            }
            else
            {
                if (l != nullptr) l->setBounds (col.removeFromTop (kLabelHeight));
                if (c != nullptr) c->setBounds (col);
            }
        };
        placeKnob (coreRow, 0, sustainEnabledToggle_.get(),       nullptr, nullptr);
        placeKnob (coreRow, 1, nullptr, sustainAmountLabel_.get(),        sustainAmountSlider_.get());
        placeKnob (coreRow, 2, nullptr, sustainReleaseExtensionLabel_.get(), sustainReleaseExtensionSlider_.get());
        placeKnob (coreRow, 3, nullptr, sustainHarmonicWeightLabel_.get(),    sustainHarmonicWeightSlider_.get());
        placeKnob (coreRow, 4, nullptr, sustainDecayWeightLabel_.get(),       sustainDecayWeightSlider_.get());
        placeKnob (coreRow, 5, nullptr, sustainNoiseWeightLabel_.get(),       sustainNoiseWeightSlider_.get());
    }

    // Feedback Friend rows — 2 × 4 columns. The two toggles fill column 0
    // of each row without labels.
    auto fbRow1 = inner.removeFromTop (fbRowH);
    inner.removeFromTop (kColumnPadding);
    auto fbRow2 = inner.removeFromTop (fbRowH);
    inner.removeFromTop (kColumnPadding);

    auto placeFbRow = [&] (juce::Rectangle<int>& r,
                           juce::Component* first,
                           juce::Label* l2, juce::Component* c2,
                           juce::Label* l3, juce::Component* c3,
                           juce::Label* l4, juce::Component* c4)
    {
        auto c0 = r.removeFromLeft (colW);
        r.removeFromLeft (kColumnPadding);
        if (first != nullptr) first->setBounds (c0);

        auto take = [&] (int i) -> juce::Rectangle<int>
        {
            auto col = r.removeFromLeft (colW);
            if (i < fbCols - 1) r.removeFromLeft (kColumnPadding);
            return col;
        };
        auto a = take (1); if (l2 != nullptr) l2->setBounds (a.removeFromTop (kLabelHeight)); if (c2 != nullptr) c2->setBounds (a);
        auto b = take (2); if (l3 != nullptr) l3->setBounds (b.removeFromTop (kLabelHeight)); if (c3 != nullptr) c3->setBounds (b);
        auto cc = take (3); if (l4 != nullptr) l4->setBounds (cc.removeFromTop (kLabelHeight)); if (c4 != nullptr) c4->setBounds (cc);
    };

    // Row 1: FB enable, sensitivity, min duration, max hold.
    placeFbRow (fbRow1,
                feedbackEnabledToggle_.get(),
                feedbackSensitivityLabel_.get(), feedbackSensitivitySlider_.get(),
                feedbackMinDurationLabel_.get(),  feedbackMinDurationSlider_.get(),
                feedbackMaxHoldLabel_.get(),      feedbackMaxHoldSlider_.get());

    // Row 2: FB require-ext-key toggle, freq min, freq max, floor.
    placeFbRow (fbRow2,
                feedbackRequireExtKeyToggle_.get(),
                feedbackFreqMinLabel_.get(), feedbackFreqMinSlider_.get(),
                feedbackFreqMaxLabel_.get(), feedbackFreqMaxSlider_.get(),
                feedbackFloorLabel_.get(),    feedbackFloorSlider_.get());

    // Safety timeout — linear slider strip at the bottom of the group.
    auto safetyRow = inner.removeFromTop (safetyH);
    if (feedbackSafetyTimeoutLabel_)
    {
        auto labelArea = safetyRow.removeFromLeft (120);
        feedbackSafetyTimeoutLabel_->setBounds (labelArea);
    }
    if (feedbackSafetyTimeoutSlider_)
        feedbackSafetyTimeoutSlider_->setBounds (safetyRow);
}

} // namespace ChugGate::GUI
