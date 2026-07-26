// ShapePage.cpp
//
// See ShapePage.h. Spectral topology shaping page (CHUGGATE_SPEC.md §21.4).
// Hosts topology selection, crossovers, per-band parameters and Hiss
// Curtain parameters. APVTS bindings only.
//
// Layout (resized() computes the rectangles; the page itself does not
// change window size):
//
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  Topology selector      │  Hiss Curtain min/max/depth              │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  Crossovers (Dual / Tri Low / Tri High)                            │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  Low Band panel │ Mid Band panel │ High Band panel                  │
//   └────────────────────────────────────────────────────────────────────┘
//
// The multiband panel row uses a fixed three-column layout; when the
// available width drops below the comfortable width (~920 px minimum), the
// per-band columns shrink (each band panel adapts to the column width by
// stacking its controls vertically) so the page stays readable.

#include "ShapePage.h"

#include "ChugLookAndFeel.h"

namespace ChugGate::GUI {

namespace
{
    constexpr int kOuterInset          = 8;
    constexpr int kRowGap              = 6;
    constexpr int kColumnPadding       = 12;
    constexpr int kLabelHeight         = 18;

    // Vertical weights for the three sections.
    constexpr float kTopologyWeight    = 0.22f;
    constexpr float kCrossoverWeight   = 0.20f;
    constexpr float kBandsWeight       = 0.58f;

    // Style a slider for the shape page. Knobs are rotary with a text box
    // below; the LookAndFeel renders the labels via the standard label
    // override so we don't need custom painting here.
    void styleRotary (juce::Slider& slider, juce::Label& nameLabel)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        slider.setName (nameLabel.getName());
        slider.setComponentID ("shapeRotary");
    }

    void styleToggle (juce::ToggleButton& button)
    {
        button.setComponentID ("shapeToggle");
    }
} // namespace

//==============================================================================
// Construction / destruction.
//==============================================================================
ShapePage::ShapePage (juce::AudioProcessorValueTreeState& apvts)
    : apvts_ (apvts)
{
    setOpaque (false);
    setWantsKeyboardFocus (false);

    buildTopologyRow();
    buildCrossoverRow();
    buildBandControls (lowBand_,  "Low Band",  ChugGate::ParameterIDs::bandLow);
    buildBandControls (midBand_,  "Mid Band",  ChugGate::ParameterIDs::bandMid);
    buildBandControls (highBand_, "High Band", ChugGate::ParameterIDs::bandHigh);
}

ShapePage::~ShapePage() = default;

//==============================================================================
// Component overrides.
//==============================================================================
void ShapePage::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    g.setColour (ChugLookAndFeel::getPanelColour().withAlpha (0.85f));
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour());
    g.drawRoundedRectangle (bounds.toFloat(), 4.0f, 1.0f);

    // Section title.
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("SHAPE", bounds.removeFromTop (16), juce::Justification::centredLeft);
}

void ShapePage::resized()
{
    auto area = getLocalBounds();
    if (area.isEmpty()) return;

    area.removeFromTop (16); // title

    const int totalH    = area.getHeight();
    const int topologyH = std::max (60,  static_cast<int> (totalH * kTopologyWeight));
    const int xoverH    = std::max (50,  static_cast<int> (totalH * kCrossoverWeight));
    const int bandsH    = std::max (120, totalH - topologyH - xoverH - kRowGap * 2);

    layoutTopologyRow  (area.removeFromTop (topologyH));
    area.removeFromTop (kRowGap);
    layoutCrossoverRow (area.removeFromTop (xoverH));
    area.removeFromTop (kRowGap);
    layoutBandPanels   (area.removeFromTop (bandsH));
}

//==============================================================================
// Layout helpers.
//==============================================================================
void ShapePage::layoutTopologyRow (juce::Rectangle<int> area)
{
    // Left: topology selector. Right: Hiss Curtain sliders.
    if (topologyCombo_ && topologyLabel_)
    {
        auto left = area.removeFromLeft (area.getWidth() / 3);
        topologyLabel_->setBounds (left.removeFromTop (kLabelHeight));
        topologyCombo_->setBounds (left);
    }
    area.removeFromLeft (kColumnPadding);

    const int hissCols = 3;
    const int colW = (area.getWidth() - kColumnPadding * (hissCols - 1)) / hissCols;
    auto placeHiss = [&] (juce::Slider* s, juce::Label* l)
    {
        if (s == nullptr || l == nullptr) return;
        auto col = area.removeFromLeft (colW);
        if (! area.isEmpty()) area.removeFromLeft (kColumnPadding);
        l->setBounds (col.removeFromTop (kLabelHeight));
        s->setBounds (col);
    };
    placeHiss (hissMinSlider_.get(),   hissMinLabel_.get());
    placeHiss (hissMaxSlider_.get(),   hissMaxLabel_.get());
    placeHiss (hissDepthSlider_.get(), hissDepthLabel_.get());
}

void ShapePage::layoutCrossoverRow (juce::Rectangle<int> area)
{
    constexpr int numCross = 3;
    const int colW = (area.getWidth() - kColumnPadding * (numCross - 1)) / numCross;

    auto placeCross = [&] (juce::Slider* s, juce::Label* l)
    {
        if (s == nullptr || l == nullptr) return;
        auto col = area.removeFromLeft (colW);
        if (! area.isEmpty()) area.removeFromLeft (kColumnPadding);
        l->setBounds (col.removeFromTop (kLabelHeight));
        s->setBounds (col);
    };

    placeCross (dualCrossoverSlider_.get(),     dualCrossoverLabel_.get());
    placeCross (triLowCrossoverSlider_.get(),   triLowCrossoverLabel_.get());
    placeCross (triHighCrossoverSlider_.get(),  triHighCrossoverLabel_.get());
}

void ShapePage::layoutBandPanels (juce::Rectangle<int> area)
{
    constexpr int numBands = 3;
    const int colW = (area.getWidth() - kColumnPadding * (numBands - 1)) / numBands;

    layoutBandPanel (lowBand_,  area.removeFromLeft (colW));
    if (! area.isEmpty()) area.removeFromLeft (kColumnPadding);
    layoutBandPanel (midBand_,  area.removeFromLeft (colW));
    if (! area.isEmpty()) area.removeFromLeft (kColumnPadding);
    layoutBandPanel (highBand_, area);
}

void ShapePage::layoutBandPanel (BandControls& b, juce::Rectangle<int> area)
{
    if (! b.panel) return;
    b.panel->setBounds (area);

    auto inner = area.reduced (kOuterInset);
    if (b.title)
    {
        b.title->setBounds (inner.removeFromTop (kLabelHeight));
    }
    inner.removeFromTop (2);

    // Primary row: floor, thresholdOffset, releaseMultiplier.
    constexpr int numPrimary = 3;
    const int primaryH = std::min (inner.getHeight() / 2,
                                  std::max (60, inner.getHeight() * 2 / 3));
    auto primary = inner.removeFromTop (primaryH);
    const int priColW = (primary.getWidth() - kColumnPadding * (numPrimary - 1)) / numPrimary;
    auto placePri = [&] (juce::Slider* s, juce::Label* l, auto& areaIn)
    {
        if (s == nullptr || l == nullptr) return;
        auto col = areaIn.removeFromLeft (priColW);
        if (! areaIn.isEmpty()) areaIn.removeFromLeft (kColumnPadding);
        l->setBounds (col.removeFromTop (kLabelHeight));
        s->setBounds (col);
    };
    auto priArea = primary;
    placePri (b.floorSlider.get(),               b.floorLabel.get(),               priArea);
    placePri (b.thresholdOffsetSlider.get(),     b.thresholdOffsetLabel.get(),     priArea);
    placePri (b.releaseMultiplierSlider.get(),   b.releaseMultiplierLabel.get(),   priArea);

    // Detail row: attackOffset, holdOffset, detectorWeighting, solo, mute.
    inner.removeFromTop (kRowGap);
    auto detail = inner;
    const int detailColW = (detail.getWidth() - kColumnPadding * 4) / 5;
    auto placeDet = [&] (juce::Slider* s, juce::Label* l, auto& areaIn)
    {
        if (s == nullptr || l == nullptr) return;
        auto col = areaIn.removeFromLeft (detailColW);
        if (! areaIn.isEmpty()) areaIn.removeFromLeft (kColumnPadding);
        l->setBounds (col.removeFromTop (kLabelHeight));
        s->setBounds (col);
    };
    auto detailArea = detail;
    placeDet (b.attackOffsetSlider.get(),     b.attackOffsetLabel.get(),     detailArea);
    placeDet (b.holdOffsetSlider.get(),       b.holdOffsetLabel.get(),       detailArea);
    placeDet (b.detectorWeightingSlider.get(),b.detectorWeightingLabel.get(),detailArea);

    // Solo + mute take the last two columns of the detail row.
    auto toggleArea = detailArea.removeFromLeft (detailColW * 2 + kColumnPadding);
    if (b.soloButton)
    {
        b.soloButton->setBounds (toggleArea.removeFromLeft (detailColW));
    }
    toggleArea.removeFromLeft (kColumnPadding);
    if (b.muteButton)
    {
        b.muteButton->setBounds (toggleArea.removeFromLeft (detailColW));
    }
}

//==============================================================================
// Control construction helpers.
//==============================================================================
void ShapePage::makeRotaryWithLabel (std::unique_ptr<juce::Slider>& sliderOut,
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

void ShapePage::buildTopologyRow()
{
    using namespace ChugGate::ParameterIDs;

    // Topology selector.
    topologyCombo_ = std::make_unique<juce::ComboBox> ("topologyCombo");
    topologyLabel_ = std::make_unique<juce::Label>    ("topologyCombo", "Topology");
    topologyLabel_->setJustificationType (juce::Justification::centredLeft);
    topologyLabel_->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*topologyCombo_);
    addAndMakeVisible (*topologyLabel_);
    if (apvts_.getParameter (Global::topology) != nullptr)
    {
        topologyCombo_->addItem ("FullBand",    1);
        topologyCombo_->addItem ("DualBand",    2);
        topologyCombo_->addItem ("TriBand",     3);
        topologyCombo_->addItem ("HissCurtain", 4);
        topologyAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, Global::topology, *topologyCombo_);
    }

    // Hiss Curtain — min / max / depth.
    makeRotaryWithLabel (hissMinSlider_,   hissMinLabel_,
                         "hissMin",        "Hiss Min");
    hissMinAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, HissCurtain::minFreq, *hissMinSlider_);

    makeRotaryWithLabel (hissMaxSlider_,   hissMaxLabel_,
                         "hissMax",        "Hiss Max");
    hissMaxAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, HissCurtain::maxFreq, *hissMaxSlider_);

    makeRotaryWithLabel (hissDepthSlider_, hissDepthLabel_,
                         "hissDepth",      "Hiss Depth");
    hissDepthAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, HissCurtain::depth, *hissDepthSlider_);
}

void ShapePage::buildCrossoverRow()
{
    using namespace ChugGate::ParameterIDs;

    makeRotaryWithLabel (dualCrossoverSlider_,     dualCrossoverLabel_,
                         "dualCrossover",          "Dual X-Over");
    dualCrossoverAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, Crossovers::dualFreq, *dualCrossoverSlider_);

    makeRotaryWithLabel (triLowCrossoverSlider_,   triLowCrossoverLabel_,
                         "triLowCrossover",        "Tri Low X-Over");
    triLowCrossoverAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, Crossovers::triLowFreq, *triLowCrossoverSlider_);

    makeRotaryWithLabel (triHighCrossoverSlider_,  triHighCrossoverLabel_,
                         "triHighCrossover",       "Tri High X-Over");
    triHighCrossoverAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts_, Crossovers::triHighFreq, *triHighCrossoverSlider_);
}

void ShapePage::buildBandControls (BandControls& out,
                                   const char* bandName,
                                   const ChugGate::ParameterIDs::Band& ids)
{
    out.bandName = bandName;
    out.ids      = &ids;

    // Per-band panel — a single Component that hosts all band controls so
    // the layout code can treat each band as one rectangle.
    out.panel = std::make_unique<juce::Component> (bandName);
    out.title = std::make_unique<juce::Label>    (bandName, bandName);
    out.title->setJustificationType (juce::Justification::centred);
    out.title->setInterceptsMouseClicks (false, false);
    out.panel->addAndMakeVisible (*out.title);
    addAndMakeVisible (*out.panel);

    auto attachSlider = [&] (std::unique_ptr<juce::Slider>& slider,
                             std::unique_ptr<juce::Label>&  label,
                             const char* paramId,
                             const juce::String& humanName,
                             auto& attachmentSlot)
    {
        slider = std::make_unique<juce::Slider> (humanName);
        label  = std::make_unique<juce::Label>  (humanName, humanName);
        label->setJustificationType (juce::Justification::centred);
        label->setInterceptsMouseClicks (false, false);
        out.panel->addAndMakeVisible (*slider);
        out.panel->addAndMakeVisible (*label);
        styleRotary (*slider, *label);
        if (apvts_.getParameter (paramId) != nullptr)
        {
            attachmentSlot =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                    apvts_, paramId, *slider);
        }
    };

    attachSlider (out.floorSlider,               out.floorLabel,
                  ids.floor,                     "Floor",
                  out.floorAttachment);
    attachSlider (out.thresholdOffsetSlider,     out.thresholdOffsetLabel,
                  ids.thresholdOffset,           "Threshold",
                  out.thresholdOffsetAttachment);
    attachSlider (out.releaseMultiplierSlider,   out.releaseMultiplierLabel,
                  ids.releaseMultiplier,         "Release ×",
                  out.releaseMultiplierAttachment);
    attachSlider (out.attackOffsetSlider,        out.attackOffsetLabel,
                  ids.attackOffset,              "Attack",
                  out.attackOffsetAttachment);
    attachSlider (out.holdOffsetSlider,          out.holdOffsetLabel,
                  ids.holdOffset,                "Hold",
                  out.holdOffsetAttachment);
    attachSlider (out.detectorWeightingSlider,   out.detectorWeightingLabel,
                  ids.detectorWeighting,         "Weight",
                  out.detectorWeightingAttachment);

    // Solo / mute toggles.
    out.soloButton = std::make_unique<juce::ToggleButton> ("Solo");
    out.muteButton = std::make_unique<juce::ToggleButton> ("Mute");
    styleToggle (*out.soloButton);
    styleToggle (*out.muteButton);
    out.panel->addAndMakeVisible (*out.soloButton);
    out.panel->addAndMakeVisible (*out.muteButton);
    if (apvts_.getParameter (ids.solo) != nullptr)
    {
        out.soloAttachment =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, ids.solo, *out.soloButton);
    }
    if (apvts_.getParameter (ids.mute) != nullptr)
    {
        out.muteAttachment =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, ids.mute, *out.muteButton);
    }
}

} // namespace ChugGate::GUI