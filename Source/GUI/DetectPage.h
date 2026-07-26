// DetectPage.h
//
// Detect view (CHUGGATE_SPEC.md §21.4) — key conditioning, hum rejection,
// threshold, transient (Attack Guard) and sustain analysis controls.
//
// Hosts the full set of "Detect" controls:
//
//   §5  Key Conditioning
//       - Key HPF        (enabled, frequency, slope)
//       - Key LPF        (enabled, frequency, slope)
//       - Detector Emphasis (frequency, gain, Q)
//       - Key alignment offset
//       - Missing-sidechain fallback
//       - Key Listen mode (audition)
//
//   §5.4 Hum Rejection (Powerline Shield)
//       - Hum mode, harmonics, notch Q, immunity amount
//
//   §6  Detector Engine / §7.2 Dual-Threshold Hysteresis
//       - Open threshold, hysteresis, attack, hold, release
//
//   §8  Pick Attack Guard
//       - enabled, amount, sensitivity, frequency, decay, soft-note assist,
//         max threshold reduction, confirmation window
//
//   §9  Sustain Intelligence
//       - enabled, amount, release extension, harmonic / decay / noise
//         weights
//       - Feedback Friend subsection
//         (feedback enabled, sensitivity, min duration, max hold, freq
//          range, floor, require external key, safety timeout)
//
// All controls are APVTS-bound. No DSP is invoked from this file. The
// editor (i.6) embeds this page as the "Detect" tab content.
//
// Layout (computed in resized()):
//
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  DETECT                                                            │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  KEY CONDITIONING   [HPF en f slope] [LPF en f slope] [Emphasis]   │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  HUM                [mode] [harmonics] [Q] [immunity]  + Listen    │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  THRESHOLD          [open] [hysteresis] [attack] [hold] [release]  │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  ATTACK GUARD       [en] [amount] [sens] [freq] [decay] [soft] …   │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  SUSTAIN            [en] [amount] [ext] [harm] [decay] [noise] …   │
//   │     Feedback Friend  [en] [sens] [dur] [hold] [fmin] [fmax] …      │
//   └────────────────────────────────────────────────────────────────────┘

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "../ParameterIDs.h"

namespace ChugGate::GUI {

//==============================================================================
/**
 *  DetectPage — Detect view (CHUGGATE_SPEC.md §21.4).
 *
 *  Surfaces every key-conditioning, hum-rejection, threshold, transient
 *  (Attack Guard) and sustain control. Each control is APVTS-bound through
 *  SliderAttachment / ComboBoxAttachment / ButtonAttachment so the page
 *  owns no DSP state and never reads parameters directly.
 *
 *  Construction requires an AudioProcessorValueTreeState reference; all
 *  attachments are constructed in the constructor. Missing parameters
 *  (e.g. during early wiring of the APVTS layout) result in the control
 *  still being constructed but bound as a no-op — the page remains usable.
 *
 *  Threading: construction, painting and resizing happen on the message
 *  thread. The page holds no audio-thread state.
 */
class DetectPage : public juce::Component
{
public:
    //==========================================================================
    DetectPage (juce::AudioProcessorValueTreeState& apvts);
    ~DetectPage() override;

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // A small helper struct representing a labelled section (group) inside
    // the page. Each group owns its own background panel and a bold title
    // label; controls are placed in the inner area during layout.
    struct GroupPanel
    {
        std::unique_ptr<juce::Component> panel;
        std::unique_ptr<juce::Label>     title;

        explicit GroupPanel (const juce::String& name, const juce::String& titleText);
    };

    //==========================================================================
    // Key Conditioning group (§5) — HPF, LPF, Emphasis, alignment offset,
    // fallback mode, listen mode.
    GroupPanel keyGroup_;

    // HPF
    std::unique_ptr<juce::ToggleButton>     keyHpfEnabledToggle_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyHpfEnabledAttachment_;
    std::unique_ptr<juce::Slider>           keyHpfFrequencySlider_;
    std::unique_ptr<juce::Label>            keyHpfFrequencyLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> keyHpfFrequencyAttachment_;
    std::unique_ptr<juce::ComboBox>          keyHpfSlopeCombo_;
    std::unique_ptr<juce::Label>             keyHpfSlopeLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyHpfSlopeAttachment_;

    // LPF
    std::unique_ptr<juce::ToggleButton>     keyLpfEnabledToggle_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyLpfEnabledAttachment_;
    std::unique_ptr<juce::Slider>           keyLpfFrequencySlider_;
    std::unique_ptr<juce::Label>            keyLpfFrequencyLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> keyLpfFrequencyAttachment_;
    std::unique_ptr<juce::ComboBox>          keyLpfSlopeCombo_;
    std::unique_ptr<juce::Label>             keyLpfSlopeLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyLpfSlopeAttachment_;

    // Emphasis
    std::unique_ptr<juce::Slider>           keyEmphasisFreqSlider_;
    std::unique_ptr<juce::Label>            keyEmphasisFreqLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> keyEmphasisFreqAttachment_;
    std::unique_ptr<juce::Slider>           keyEmphasisGainSlider_;
    std::unique_ptr<juce::Label>            keyEmphasisGainLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> keyEmphasisGainAttachment_;
    std::unique_ptr<juce::Slider>           keyEmphasisQSlider_;
    std::unique_ptr<juce::Label>            keyEmphasisQLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> keyEmphasisQAttachment_;

    // Alignment offset
    std::unique_ptr<juce::Slider>           keyAlignmentOffsetSlider_;
    std::unique_ptr<juce::Label>            keyAlignmentOffsetLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> keyAlignmentOffsetAttachment_;

    // Fallback + Listen (§5.6 / §5.6)
    std::unique_ptr<juce::ComboBox>          keyFallbackCombo_;
    std::unique_ptr<juce::Label>             keyFallbackLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyFallbackAttachment_;
    std::unique_ptr<juce::ComboBox>          keyListenModeCombo_;
    std::unique_ptr<juce::Label>             keyListenModeLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyListenModeAttachment_;

    //==========================================================================
    // Hum group (§5.4) — Powerline Shield. Hum mode, harmonics, notch Q,
    // immunity amount.
    GroupPanel humGroup_;

    std::unique_ptr<juce::ComboBox>          humModeCombo_;
    std::unique_ptr<juce::Label>             humModeLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> humModeAttachment_;
    std::unique_ptr<juce::ComboBox>          humHarmonicsCombo_;
    std::unique_ptr<juce::Label>             humHarmonicsLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> humHarmonicsAttachment_;
    std::unique_ptr<juce::Slider>           humNotchQSlider_;
    std::unique_ptr<juce::Label>            humNotchQLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humNotchQAttachment_;
    std::unique_ptr<juce::Slider>           humImmunitySlider_;
    std::unique_ptr<juce::Label>            humImmunityLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humImmunityAttachment_;

    //==========================================================================
    // Threshold / Detector group (§6, §7.2) — open threshold, hysteresis,
    // detector attack / hold / release ballistics.
    GroupPanel thresholdGroup_;

    std::unique_ptr<juce::Slider>           thresholdOpenSlider_;
    std::unique_ptr<juce::Label>            thresholdOpenLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdOpenAttachment_;
    std::unique_ptr<juce::Slider>           hysteresisSlider_;
    std::unique_ptr<juce::Label>            hysteresisLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hysteresisAttachment_;
    std::unique_ptr<juce::Slider>           attackSlider_;
    std::unique_ptr<juce::Label>            attackLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment_;
    std::unique_ptr<juce::Slider>           holdSlider_;
    std::unique_ptr<juce::Label>            holdLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> holdAttachment_;
    std::unique_ptr<juce::Slider>           releaseSlider_;
    std::unique_ptr<juce::Label>            releaseLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment_;

    //==========================================================================
    // Attack Guard group (§8) — Pick Attack Guard controls.
    GroupPanel attackGuardGroup_;

    std::unique_ptr<juce::ToggleButton>     attackGuardEnabledToggle_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attackGuardEnabledAttachment_;
    std::unique_ptr<juce::Slider>           attackGuardAmountSlider_;
    std::unique_ptr<juce::Label>            attackGuardAmountLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardAmountAttachment_;
    std::unique_ptr<juce::Slider>           attackGuardSensitivitySlider_;
    std::unique_ptr<juce::Label>            attackGuardSensitivityLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardSensitivityAttachment_;
    std::unique_ptr<juce::Slider>           attackGuardFrequencySlider_;
    std::unique_ptr<juce::Label>            attackGuardFrequencyLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardFrequencyAttachment_;
    std::unique_ptr<juce::Slider>           attackGuardDecaySlider_;
    std::unique_ptr<juce::Label>            attackGuardDecayLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardDecayAttachment_;
    std::unique_ptr<juce::Slider>           attackGuardSoftAssistSlider_;
    std::unique_ptr<juce::Label>            attackGuardSoftAssistLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardSoftAssistAttachment_;
    std::unique_ptr<juce::Slider>           attackGuardMaxReductionSlider_;
    std::unique_ptr<juce::Label>            attackGuardMaxReductionLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardMaxReductionAttachment_;
    std::unique_ptr<juce::Slider>           attackGuardConfirmationSlider_;
    std::unique_ptr<juce::Label>            attackGuardConfirmationLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackGuardConfirmationAttachment_;

    //==========================================================================
    // Sustain group (§9) — sustain controls plus a Feedback Friend
    // sub-section (§10).
    GroupPanel sustainGroup_;

    std::unique_ptr<juce::ToggleButton>     sustainEnabledToggle_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sustainEnabledAttachment_;
    std::unique_ptr<juce::Slider>           sustainAmountSlider_;
    std::unique_ptr<juce::Label>            sustainAmountLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainAmountAttachment_;
    std::unique_ptr<juce::Slider>           sustainReleaseExtensionSlider_;
    std::unique_ptr<juce::Label>            sustainReleaseExtensionLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainReleaseExtensionAttachment_;
    std::unique_ptr<juce::Slider>           sustainHarmonicWeightSlider_;
    std::unique_ptr<juce::Label>            sustainHarmonicWeightLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainHarmonicWeightAttachment_;
    std::unique_ptr<juce::Slider>           sustainDecayWeightSlider_;
    std::unique_ptr<juce::Label>            sustainDecayWeightLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainDecayWeightAttachment_;
    std::unique_ptr<juce::Slider>           sustainNoiseWeightSlider_;
    std::unique_ptr<juce::Label>            sustainNoiseWeightLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sustainNoiseWeightAttachment_;

    // Feedback Friend sub-section (§10) — toggle + sensitivity + duration
    // + hold + frequency range + floor + require ext key + safety timeout.
    std::unique_ptr<juce::ToggleButton>     feedbackEnabledToggle_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> feedbackEnabledAttachment_;
    std::unique_ptr<juce::Slider>           feedbackSensitivitySlider_;
    std::unique_ptr<juce::Label>            feedbackSensitivityLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackSensitivityAttachment_;
    std::unique_ptr<juce::Slider>           feedbackMinDurationSlider_;
    std::unique_ptr<juce::Label>            feedbackMinDurationLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackMinDurationAttachment_;
    std::unique_ptr<juce::Slider>           feedbackMaxHoldSlider_;
    std::unique_ptr<juce::Label>            feedbackMaxHoldLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackMaxHoldAttachment_;
    std::unique_ptr<juce::Slider>           feedbackFreqMinSlider_;
    std::unique_ptr<juce::Label>            feedbackFreqMinLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackFreqMinAttachment_;
    std::unique_ptr<juce::Slider>           feedbackFreqMaxSlider_;
    std::unique_ptr<juce::Label>            feedbackFreqMaxLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackFreqMaxAttachment_;
    std::unique_ptr<juce::Slider>           feedbackFloorSlider_;
    std::unique_ptr<juce::Label>            feedbackFloorLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackFloorAttachment_;
    std::unique_ptr<juce::ToggleButton>     feedbackRequireExtKeyToggle_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> feedbackRequireExtKeyAttachment_;
    std::unique_ptr<juce::Slider>           feedbackSafetyTimeoutSlider_;
    std::unique_ptr<juce::Label>            feedbackSafetyTimeoutLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackSafetyTimeoutAttachment_;

    //==========================================================================
    // Construction helpers
    void makeRotaryWithLabel (std::unique_ptr<juce::Slider>& sliderOut,
                              std::unique_ptr<juce::Label>&  labelOut,
                              const juce::String& name,
                              const juce::String& text,
                              juce::Component* parent);
    void makeComboWithLabel  (std::unique_ptr<juce::ComboBox>& comboOut,
                              std::unique_ptr<juce::Label>&    labelOut,
                              const juce::String& name,
                              const juce::String& text,
                              juce::Component* parent);
    void makeToggle          (std::unique_ptr<juce::ToggleButton>& toggleOut,
                              const juce::String& name,
                              const juce::String& text,
                              juce::Component* parent);

    void buildKeyGroup();
    void buildHumGroup();
    void buildThresholdGroup();
    void buildAttackGuardGroup();
    void buildSustainGroup();

    // Layout helpers
    void layoutKeyGroup         (juce::Rectangle<int> area);
    void layoutHumGroup         (juce::Rectangle<int> area);
    void layoutThresholdGroup   (juce::Rectangle<int> area);
    void layoutAttackGuardGroup (juce::Rectangle<int> area);
    void layoutSustainGroup     (juce::Rectangle<int> area);

    // Paint a section background and title.
    void paintGroupBackground (juce::Graphics& g,
                               GroupPanel& group,
                               const juce::String& titleText);

    //==========================================================================
    // APVTS reference (non-owning). The page never mutates the state; it
    // only constructs attachments against it.
    juce::AudioProcessorValueTreeState& apvts_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DetectPage)
};

} // namespace ChugGate::GUI