#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GUI/ChugLookAndFeel.h"
#include "GUI/MainHeader.h"
#include "GUI/PerformanceTimeline.h"
#include "GUI/StateIndicator.h"
#include "GUI/PerformPage.h"
#include "GUI/ShapePage.h"
#include "GUI/DetectPage.h"
#include "GUI/AlignPage.h"
#include "GUI/DiagnosePage.h"

#include "PluginProcessor.h"

//==============================================================================
/**
 *  ChugGateAudioProcessorEditor — top-level ChugGate editor window
 *  (CHUGGATE_SPEC.md §21.1 / §21.3).
 *
 *  Window:
 *      - Default size 1280 x 760; min 920 x 580.
 *      - Resizable in both directions (true). Stays resizable when
 *        running as a VST3 and as Standalone.
 *      - High-DPI aware (JUCE handles this via the global scale factor).
 *
 *  Hierarchy:
 *      ChugGateAudioProcessorEditor
 *      ├── MainHeader        (top row, height = 36 px; preset, profile,
 *      │                      A/B, key source, sidechain, learn/align,
 *      │                      bypass, latency, CPU)
 *      ├── Timeline + State  (PerformanceTimeline on the left, StateIndicator
 *      │                      docked to the right; ~220 px tall)
 *      └── TabbedComponent   (Perform / Shape / Detect / Align / Diagnose
 *                             tabs; fills the remaining body)
 *
 *  Look-and-feel:
 *      The editor owns a std::unique_ptr<ChugLookAndFeel> and installs
 *      it as the default L&F before any child components are
 *      constructed. This guarantees every child (including the tab
 *      bar) gets the suite palette / fonts.
 *
 *  Threading:
 *      All public methods are message-thread only. The editor owns a
 *      juce::Timer that updates the StateIndicator snapshot and the
 *      bypass/profile status at 30 Hz. The AnalysisFifo →
 *      PerformanceHistory pump lives on the processor's own timer and
 *      is NOT duplicated here (the editor only reads the
 *      already-populated history through the analysis accessors).
 */
class ChugGateAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    ChugGateAudioProcessorEditor (ChugGateAudioProcessor&);
    ~ChugGateAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    //==============================================================================
    // Keyboard focus traversal — re-enable the standard JUCE focus
    // ordering so Tab/Shift+Tab walk through every child. We never
    // disable focus on any child control.
    bool keyPressed (const juce::KeyPress& key) override;

private:
    //==============================================================================
    // juce::Timer — message-thread refresh of the StateIndicator and the
    // header status readouts (latency / CPU / sidechain are refreshed
    // inside the header's own timer). At 30 Hz the snapshot stays
    // visibly current without pegging the message thread.
    void timerCallback() override;

    //==============================================================================
    // Build the StateSnapshot pushed into the StateIndicator. Uses the
    // engine's atomic real-time fields (currentState / lastGain) plus
    // a couple of APVTS reads to populate the protection-flag bit-set
    // and a short reason string.
    ChugGate::GUI::StateSnapshot buildStateSnapshot() noexcept;

    // Track the last state change to compute a duration in ms.
    int lastEngineState_ = -1;
    juce::int64 lastStateChangeMs_ = 0;

    //==============================================================================
    ChugGateAudioProcessor& audioProcessor;

    // Editor-owned LookAndFeel (set as both default and the editor's own
    // L&F before any children are constructed).
    std::unique_ptr<ChugGate::GUI::ChugLookAndFeel> lookAndFeel_;

    // Child components.
    std::unique_ptr<ChugGate::GUI::MainHeader>          header_;
    std::unique_ptr<ChugGate::GUI::PerformanceTimeline> timeline_;
    std::unique_ptr<ChugGate::GUI::StateIndicator>      stateIndicator_;

    // Tabbed pages — one page per spec §21.4 view.
    std::unique_ptr<ChugGate::GUI::PerformPage>  performPage_;
    std::unique_ptr<ChugGate::GUI::ShapePage>    shapePage_;
    std::unique_ptr<ChugGate::GUI::DetectPage>   detectPage_;
    std::unique_ptr<ChugGate::GUI::AlignPage>    alignPage_;
    std::unique_ptr<ChugGate::GUI::DiagnosePage> diagnosePage_;

    // TabbedComponent host (owns the tab bar). The pages themselves are
    // held by the editor so other code can reach them; TabbedComponent
    // takes non-owning pointers via addTab.
    std::unique_ptr<juce::TabbedComponent> tabs_;

    // Phase J - factory preset JSON browser. Exists alongside the
    // MainHeader's XML preset browser so the editor surfaces every
    // factory preset regardless of format. The combo is read-only;
    // the Load button routes through the processor's JSON loader.
    std::unique_ptr<juce::ComboBox>   jsonPresetCombo_;
    std::unique_ptr<juce::TextButton> jsonPresetLoadButton_;

    // Phase J - status labels. The audition indicator is visible at
    // all times so the user knows when the output is the rejected-
    // signal audition (CHUGGATE_SPEC.md §20.5). The dirty indicator
    // shows when the current state has been mutated since the last
    // preset / profile / tuning load.
    std::unique_ptr<juce::Label> auditionIndicatorLabel_;
    std::unique_ptr<juce::Label> dirtyStateLabel_;

    // Phase J - helper that walks the JSON preset directory and
    // rebuilds the factory preset combo. Called from the constructor
    // and from refreshNow(). Message-thread only.
    void rebuildJsonPresetList() noexcept;
    // Refresh the audition indicator and dirty-state label from the
    // APVTS. Message-thread only.
    void refreshStatusLabels() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChugGateAudioProcessorEditor)
};
