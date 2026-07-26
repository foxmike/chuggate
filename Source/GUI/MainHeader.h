// MainHeader.h
//
// Top-of-editor header strip (CHUGGATE_SPEC.md §21.3) — preset, profile,
// A/B controls, key source status, sidechain present/active indicator,
// Learn / Align shortcut buttons, Bypass, latency and CPU readouts.
//
// The header is fully message-thread and never invokes DSP. It is wired
// via the editor (i.6) using the APVTS reference plus a set of std::function
// callbacks that delegate into StateManager / ChugGateAudioProcessor:
//
//   - Preset selector            -> StateManager::getFactoryPresets() +
//                                  StateManager::loadPreset(name, true)
//   - Save preset button         -> StateManager::saveUserPreset(name)
//   - Profile selector           -> APVTS attachment to profile.index
//   - A/B state buttons          -> StateManager::setActiveABState("A"/"B")
//   - A/B Copy / Swap            -> StateManager::copyActiveToInactive() /
//                                  StateManager::swapABSlots()
//   - Bypass toggle              -> APVTS attachment to global.bypass
//   - Learn / Align shortcuts    -> editor-installed handlers (same code
//                                  path as PerformPage's hooks)
//   - Key source status label    -> APVTS value key.source (read-only)
//   - Sidechain indicator        -> processor bus query (presence + enabled)
//   - Latency readout            -> processor->getLatencySamples()
//   - CPU readout                -> message-thread tick duration relative
//                                  to the header's refresh period
//
// No parameter or DSP state is mutated by the header other than the
// APVTS attachments and the callbacks listed above. Painting is message-
// thread only and uses the ChugLookAndFeel palette accessors.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

class ChugGateAudioProcessor;

namespace ChugGate::GUI {

//==============================================================================
/**
 *  MainHeader — top-of-editor strip (spec §21.3, header row).
 *
 *  Construction requires the APVTS so the bound controls (profile,
 *  bypass) can be attached. The processor pointer is set separately via
 *  setProcessor() so the header can observe processor-derived state
 *  (sidechain bus status, latency, sidechain bus state) without
 *  requiring the processor at construction time.
 *
 *  Wiring model:
 *      - setProcessor(ChugGateAudioProcessor*)  injects the processor
 *        (non-owning; required for sidechain + latency readouts).
 *      - setLearnHandler(fn) / setAlignHandler(fn) install the actions
 *        fired by the Learn / Align shortcut buttons.
 *      - The preset selector is driven directly by
 *        StateManager::getFactoryPresets() and a "Save user preset"
 *        helper that delegates to StateManager::saveUserPreset().
 *
 *  Threading: all public methods are message-thread only. The header
 *  owns a small juce::Timer that refreshes the latency and CPU readouts
 *  on the message thread. No audio-thread state is touched.
 */
class MainHeader : public juce::Component,
                   private juce::Timer
{
public:
    //==========================================================================
    // Callbacks used for the Learn / Align shortcut buttons. Empty
    // std::function is a no-op so the header remains usable during
    // initialisation.
    using ActionHook = std::function<void()>;

    //==========================================================================
    MainHeader (juce::AudioProcessorValueTreeState& apvts);
    ~MainHeader() override;

    //==========================================================================
    // Inject the processor (non-owning). Must be called before the
    // editor is shown so the sidechain + latency readouts have data.
    // Safe to call from the message thread.
    void setProcessor (ChugGateAudioProcessor* processor) noexcept;

    //==========================================================================
    // Wire the Learn Noise / Align DI shortcut buttons. The editor's
    // implementation typically calls ChugGateAudioProcessor::startNoiseLearn
    // / startAlignment and the Learn hook also kicks off a poll for the
    // apply step.
    void setLearnHandler (ActionHook hook);
    void setAlignHandler (ActionHook hook);

    //==========================================================================
    // Manually request a refresh of the header readouts (latency, CPU,
    // sidechain). The internal timer handles this at 30 Hz but the
    // editor can force a tick after a bus-layout change.
    void refreshNow() noexcept;

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // juce::Timer — message-thread refresh of derived readouts.
    void timerCallback() override;

    //==========================================================================
    // Helpers
    //==========================================================================
    // Refresh the sidechain present/active indicator from the processor's
    // bus layout. Safe to call on the message thread; getBus is a
    // non-mutating query.
    void refreshSidechainStatus() noexcept;

    // Refresh the latency readout (processor->getLatencySamples()) and
    // update the read-only label. Safe on the message thread.
    void refreshLatency() noexcept;

    // Refresh the CPU readout using a moving average of message-thread
    // tick duration. Honest about the metric: it is the GUI tick CPU,
    // not the audio thread.
    void refreshCpu() noexcept;

    // Repopulate the preset combo with the current factory + user
    // preset lists from the StateManager. Called from the constructor
    // and from refreshNow().
    void rebuildPresetList() noexcept;

    // Format a milliseconds-of-latency value for the readout label.
    static juce::String formatLatencyMs (int latencySamples,
                                         double sampleRate) noexcept;

    //==========================================================================
    // Bound controls (APVTS attachments)
    std::unique_ptr<juce::ComboBox>      profileCombo_;
    std::unique_ptr<juce::Label>         profileLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        profileAttachment_;

    std::unique_ptr<juce::ToggleButton>  bypassToggle_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        bypassAttachment_;

    //==========================================================================
    // Preset selector (combo + Load + Save As) — no APVTS attachment; the
    // preset system lives in the StateManager and the editor wires the
    // side-effects.
    std::unique_ptr<juce::ComboBox>      presetCombo_;
    std::unique_ptr<juce::Label>         presetLabel_;
    std::unique_ptr<juce::TextButton>    presetLoadButton_;
    std::unique_ptr<juce::TextButton>    presetSaveButton_;

    //==========================================================================
    // A/B buttons (radio-style: A, B + Copy + Swap). No APVTS attachment —
    // the editor wires the state manager so the underlying APVTS change
    // fires a save/restore of the *other* slot via StateManager.
    std::unique_ptr<juce::TextButton>    aButton_;
    std::unique_ptr<juce::TextButton>    bButton_;
    std::unique_ptr<juce::TextButton>    copyButton_;
    std::unique_ptr<juce::TextButton>    swapButton_;

    //==========================================================================
    // Shortcut action buttons
    std::unique_ptr<juce::TextButton>    learnButton_;
    std::unique_ptr<juce::TextButton>    alignButton_;

    //==========================================================================
    // Status / readouts
    std::unique_ptr<juce::Label>         keySourceStatusLabel_;
    std::unique_ptr<juce::Label>         sidechainStatusLabel_;
    std::unique_ptr<juce::Label>         latencyLabel_;
    std::unique_ptr<juce::Label>         cpuLabel_;

    //==========================================================================
    // Editor-supplied action hooks (Learn Noise / Align DI). Empty
    // std::function is a no-op.
    ActionHook learnHook_;
    ActionHook alignHook_;

    //==========================================================================
    // APVTS reference (non-owning). The header never mutates the state
    // outside of the APVTS attachments.
    juce::AudioProcessorValueTreeState& apvts_;

    //==========================================================================
    // Processor pointer (non-owning). May be null until the editor wires
    // it; in that case sidechain / latency readouts show a dash.
    ChugGateAudioProcessor* processor_ = nullptr;

    //==========================================================================
    // Cached sidechain + state values (refreshed by the timer).
    bool sidechainPresent_  = false;
    bool sidechainActive_   = false;
    int  keySourceIndex_    = 0;
    int  lastLatencySamples_ = 0;
    double lastSampleRate_   = 0.0;

    // CPU% moving average (GUI tick duration / refresh period). Honest
    // about scope: this is the message-thread tick, not audio CPU.
    float cpuPercentSmoothed_ = 0.0f;

    // Refresh cadence (ms). 33 ms ≈ 30 Hz — matches the rest of the GUI.
    static constexpr int kRefreshMs = 33;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainHeader)
};

} // namespace ChugGate::GUI
