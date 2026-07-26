// ShapePage.h
//
// Shape view (CHUGGATE_SPEC.md §21.4) — spectral topology shaping.
//
// Hosts every control for switching between Full / Dual / Tri / Hiss
// Curtain topologies, configuring crossovers and per-band envelopes
// (floor, threshold offset, release multiplier, attack/hold offsets, solo,
// mute, detector weighting), and shaping the Hiss Curtain (min / max
// cutoff + depth). APVTS attachments only; no DSP edits here.
//
// The page is designed to remain readable at the minimum editor width
// (~920 px) by laying the three band panels side-by-side with a wrapping
// column count when the available width shrinks. The Hiss Curtain row
// lives at the top so it stays visible regardless of which topology is
// selected.
//
// Layout (computed in resized()):
//
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  Topology: [combo]        Hiss Curtain: [min] [max] [depth]        │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  Crossovers (visible per topology):                                 │
//   │  [Dual crossover]  [Tri low xover]  [Tri high xover]                │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  Low Band   │   Mid Band   │   High Band                             │
//   │  floor/ofs/ │   floor/ofs/ │   floor/ofs/                            │
//   │  rel mult … │   rel mult … │   rel mult …                            │
//   └────────────────────────────────────────────────────────────────────┘
//
// Per-band detail rows are organised so floor / thresholdOffset / release
// multiplier (the spec's "per-band floor/threshold-offset/release-multiplier
// controls") are always visible; attack / hold offsets, solo, mute and
// detector weighting live in a per-band compact detail strip beneath.
//
// All APVTS bindings go through SliderAttachment / ComboBoxAttachment /
// ButtonAttachment. The page itself is message-thread only.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

#include "../ParameterIDs.h"

namespace ChugGate::GUI {

//==============================================================================
/**
 *  ShapePage — spectral topology shaping (CHUGGATE_SPEC.md §21.4).
 *
 *  Surfaces the topology selector, crossover frequencies, per-band
 *  parameters (Low, Mid, High) and Hiss Curtain parameters. All controls
 *  are APVTS-bound; the page does not touch the processor or run DSP.
 *
 *  Threading: construction, painting and resizing happen on the message
 *  thread. The page holds no audio-thread state.
 */
class ShapePage : public juce::Component
{
public:
    //==========================================================================
    ShapePage (juce::AudioProcessorValueTreeState& apvts);
    ~ShapePage() override;

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // Topology + Hiss Curtain row.
    std::unique_ptr<juce::ComboBox>     topologyCombo_;
    std::unique_ptr<juce::Label>        topologyLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> topologyAttachment_;

    // Hiss Curtain sliders.
    std::unique_ptr<juce::Slider>      hissMinSlider_;
    std::unique_ptr<juce::Label>       hissMinLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hissMinAttachment_;

    std::unique_ptr<juce::Slider>      hissMaxSlider_;
    std::unique_ptr<juce::Label>       hissMaxLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hissMaxAttachment_;

    std::unique_ptr<juce::Slider>      hissDepthSlider_;
    std::unique_ptr<juce::Label>       hissDepthLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hissDepthAttachment_;

    // Crossover row (visible per topology; never disabled here, the user
    // can edit regardless — the DSP side ignores parameters that don't
    // apply to the active topology).
    std::unique_ptr<juce::Slider>      dualCrossoverSlider_;
    std::unique_ptr<juce::Label>       dualCrossoverLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dualCrossoverAttachment_;

    std::unique_ptr<juce::Slider>      triLowCrossoverSlider_;
    std::unique_ptr<juce::Label>       triLowCrossoverLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> triLowCrossoverAttachment_;

    std::unique_ptr<juce::Slider>      triHighCrossoverSlider_;
    std::unique_ptr<juce::Label>       triHighCrossoverLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> triHighCrossoverAttachment_;

    //==========================================================================
    // Per-band controls. We build one shared panel of controls for each of
    // the three bands (Low, Mid, High). The struct holds everything so
    // adding a fourth band later is a single struct-array push.
    struct BandControls
    {
        const char* bandName = nullptr;
        const ChugGate::ParameterIDs::Band* ids = nullptr;

        std::unique_ptr<juce::Component>                panel;
        std::unique_ptr<juce::Label>                    title;

        std::unique_ptr<juce::Slider>                   floorSlider;
        std::unique_ptr<juce::Label>                    floorLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> floorAttachment;

        std::unique_ptr<juce::Slider>                   thresholdOffsetSlider;
        std::unique_ptr<juce::Label>                    thresholdOffsetLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdOffsetAttachment;

        std::unique_ptr<juce::Slider>                   releaseMultiplierSlider;
        std::unique_ptr<juce::Label>                    releaseMultiplierLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseMultiplierAttachment;

        // Secondary detail row (attack / hold offsets, detector weighting)
        // and toggles (solo, mute).
        std::unique_ptr<juce::Slider>                   attackOffsetSlider;
        std::unique_ptr<juce::Label>                    attackOffsetLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackOffsetAttachment;

        std::unique_ptr<juce::Slider>                   holdOffsetSlider;
        std::unique_ptr<juce::Label>                    holdOffsetLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> holdOffsetAttachment;

        std::unique_ptr<juce::Slider>                   detectorWeightingSlider;
        std::unique_ptr<juce::Label>                    detectorWeightingLabel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> detectorWeightingAttachment;

        std::unique_ptr<juce::ToggleButton>             soloButton;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;

        std::unique_ptr<juce::ToggleButton>             muteButton;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    };

    BandControls lowBand_;
    BandControls midBand_;
    BandControls highBand_;

    //==========================================================================
    // Construction / layout helpers.
    void makeRotaryWithLabel (std::unique_ptr<juce::Slider>& sliderOut,
                              std::unique_ptr<juce::Label>&  labelOut,
                              const juce::String& name,
                              const juce::String& text);

    void buildTopologyRow();
    void buildCrossoverRow();
    void buildBandControls (BandControls& out,
                            const char* bandName,
                            const ChugGate::ParameterIDs::Band& ids);

    // Layout helpers.
    void layoutTopologyRow (juce::Rectangle<int> area);
    void layoutCrossoverRow(juce::Rectangle<int> area);
    void layoutBandPanels  (juce::Rectangle<int> area);
    void layoutBandPanel   (BandControls& b, juce::Rectangle<int> area);

    juce::AudioProcessorValueTreeState& apvts_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapePage)
};

} // namespace ChugGate::GUI