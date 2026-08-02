#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "StateManager.h"
#include "ParameterIDs.h"

#include "GUI/ChugLookAndFeel.h"
#include "GUI/MainHeader.h"

#include <cmath>

//==============================================================================
ChugGateAudioProcessorEditor::ChugGateAudioProcessorEditor (ChugGateAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    //--------------------------------------------------------------------------
    // Install the editor-owned ChugLookAndFeel as the default L&F BEFORE
    // any child components are constructed. setDefaultLookAndFeel is
    // picked up by children created afterwards; setLookAndFeel applies
    // the same L&F to the editor itself. We use both so the tab bar,
    // combo boxes and labels all read from the same palette.
    //--------------------------------------------------------------------------
    lookAndFeel_ = std::make_unique<ChugGate::GUI::ChugLookAndFeel>();
    juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel_.get());
    setLookAndFeel (lookAndFeel_.get());

    //--------------------------------------------------------------------------
    // Construct child components. Each one is APVTS-bound by itself; the
    // editor only injects analysis-side pointers.
    //--------------------------------------------------------------------------
    header_         = std::make_unique<ChugGate::GUI::MainHeader> (audioProcessor.getAPVTS());
    timeline_       = std::make_unique<ChugGate::GUI::PerformanceTimeline>();
    stateIndicator_ = std::make_unique<ChugGate::GUI::StateIndicator>();

    performPage_  = std::make_unique<ChugGate::GUI::PerformPage>  (audioProcessor.getAPVTS());
    shapePage_    = std::make_unique<ChugGate::GUI::ShapePage>    (audioProcessor.getAPVTS());
    detectPage_   = std::make_unique<ChugGate::GUI::DetectPage>   (audioProcessor.getAPVTS());
    alignPage_    = std::make_unique<ChugGate::GUI::AlignPage>();
    diagnosePage_ = std::make_unique<ChugGate::GUI::DiagnosePage>();

    //--------------------------------------------------------------------------
    // Inject the processor into the header so the sidechain / latency
    // / preset readouts can pull bus state and APVTS accessors. The
    // processor has already been wired with its analysis units in phase
    // H (AnalysisFifo, PerformanceHistory, etc.) so we just hand the
    // same instances through to the GUI components.
    //--------------------------------------------------------------------------
    header_->setProcessor (&audioProcessor);

    timeline_->setHistory (&audioProcessor.getPerformanceHistory());

    alignPage_->setApvts (audioProcessor.getAPVTS());
    alignPage_->setAlignmentScopeData (&audioProcessor.getAlignmentScopeData());

    diagnosePage_->setApvts (audioProcessor.getAPVTS());
    diagnosePage_->setNoiseFingerprint (&audioProcessor.getNoiseFingerprint());
    diagnosePage_->setAttackIntegrityAnalyzer (&audioProcessor.getAttackIntegrityAnalyzer());
    diagnosePage_->setTailIntegrityAnalyzer (&audioProcessor.getTailIntegrityAnalyzer());

    //--------------------------------------------------------------------------
    // Wire the action hooks. The editor routes the header's
    // Learn / Align shortcuts, the Perform page's hooks and the Align
    // page's workflow buttons to the same processor entry points so
    // every UI surface triggers identical behaviour.
    //--------------------------------------------------------------------------
    auto learnHook = [this]
    {
        // The processor owns the noise-learn workflow. Starting the
        // capture is non-blocking; the user invokes the apply step
        // from the Perform page's "Learn Noise" button (which calls
        // applyNoiseLearn) once enough noise has been captured.
        audioProcessor.startNoiseLearn();
    };
    auto alignHook = [this]
    {
        // Begin an alignment capture. Accept / Retry remain on the
        // Align page workflow buttons.
        audioProcessor.startAlignment();
    };

    header_->setLearnHandler (learnHook);
    header_->setAlignHandler (alignHook);
    performPage_->setLearnNoiseHook (learnHook);
    performPage_->setAlignDIHook (alignHook);

    alignPage_->setCalibrateHandler ([this] { audioProcessor.startAlignment(); });
    alignPage_->setAcceptHandler    ([this] { audioProcessor.acceptAlignment(); });
    alignPage_->setRetryHandler     ([this] { audioProcessor.startAlignment(); });

    diagnosePage_->setAnalyzerModeHandler ([this] (int modeIndex)
    {
        using Mode = ChugGate::Analysis::QualityMode;
        Mode mode = Mode::Studio;
        switch (modeIndex)
        {
            case 0:  mode = Mode::Off;       break;
            case 1:  mode = Mode::Live;      break;
            case 2:  mode = Mode::Studio;    break;
            case 3:  mode = Mode::Diagnose;  break;
            default: mode = Mode::Studio;    break;
        }
        audioProcessor.setAnalysisQualityMode (mode);
    });

    //--------------------------------------------------------------------------
    // Phase J - factory preset JSON loader (CHUGGATE_SPEC.md §24).
    //
    // The MainHeader's preset browser already loads XML presets via
    // StateManager. JSON factory presets (the categorised assets
    // authored in j.3) are loaded directly through the processor's
    // `loadFactoryPresetFromJson` because the existing XML path does
    // not understand JSON. We construct a small auxiliary browser
    // here so the editor surfaces every factory preset regardless of
    // format. The combo is populated lazily on the first refresh so
    // the constructor does not block on filesystem I/O.
    //--------------------------------------------------------------------------
    jsonPresetCombo_ = std::make_unique<juce::ComboBox> ("jsonPresetCombo");
    jsonPresetCombo_->setComponentID ("jsonPresetCombo");
    jsonPresetCombo_->setTooltip ("Factory preset (JSON) -- load on click");
    addAndMakeVisible (*jsonPresetCombo_);

    jsonPresetLoadButton_ = std::make_unique<juce::TextButton> ("Load Preset");
    jsonPresetLoadButton_->setComponentID ("jsonPresetLoadButton");
    jsonPresetLoadButton_->setTooltip ("Apply the selected JSON factory preset");
    jsonPresetLoadButton_->onClick = [this]
    {
        const int id = jsonPresetCombo_->getSelectedId();
        if (id <= 0) return;
        const juce::String name = jsonPresetCombo_->getItemText (id - 1);
        if (name.isNotEmpty())
            audioProcessor.loadFactoryPresetFromJson (name);
    };
    addAndMakeVisible (*jsonPresetLoadButton_);

    rebuildJsonPresetList();

    //--------------------------------------------------------------------------
    // Phase J - rejected-signal audition indicator. The Diagnose page
    // surfaces the rejected-mode / state-filter combos, but the user
    // also needs to know that the output is audition / bypass-of-wet
    // when auditioning (CHUGGATE_SPEC.md §20.5). A single read-only
    // label inside the editor suffices; the message-thread timer
    // refreshes it from the APVTS every tick.
    //--------------------------------------------------------------------------
    auditionIndicatorLabel_ = std::make_unique<juce::Label> ("", "Audition: off");
    auditionIndicatorLabel_->setComponentID ("auditionIndicatorLabel");
    auditionIndicatorLabel_->setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (*auditionIndicatorLabel_);

    // Dirty-state indicator next to the preset browser. Re-uses
    // processor.isStateDirty() which is set by the parameter listener
    // whenever a profile / tuning / preset-bridge parameter changes.
    dirtyStateLabel_ = std::make_unique<juce::Label> ("", "");
    dirtyStateLabel_->setComponentID ("dirtyStateLabel");
    dirtyStateLabel_->setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (*dirtyStateLabel_);

    //--------------------------------------------------------------------------
    // TabbedComponent — five tabs, one per spec §21.4 view. The tab bar
    // is rendered by ChugLookAndFeel's TabbedButtonBar overrides.
    //--------------------------------------------------------------------------
    tabs_ = std::make_unique<juce::TabbedComponent> (juce::TabbedButtonBar::TabsAtTop);
    tabs_->addTab ("Perform",  juce::Colours::transparentBlack, performPage_.get(),  false);
    tabs_->addTab ("Shape",    juce::Colours::transparentBlack, shapePage_.get(),    false);
    tabs_->addTab ("Detect",   juce::Colours::transparentBlack, detectPage_.get(),   false);
    tabs_->addTab ("Align",    juce::Colours::transparentBlack, alignPage_.get(),    false);
    tabs_->addTab ("Diagnose", juce::Colours::transparentBlack, diagnosePage_.get(), false);
    tabs_->setTabBarDepth (28);
    tabs_->setOutline (0);

    //--------------------------------------------------------------------------
    // Add all visible children. Order is the same as the spec layout.
    //--------------------------------------------------------------------------
    addAndMakeVisible (header_.get());
    addAndMakeVisible (timeline_.get());
    addAndMakeVisible (stateIndicator_.get());
    addAndMakeVisible (tabs_.get());

    //--------------------------------------------------------------------------
    // Initial state-indicator snapshot. The Timer will refresh it at
    // 30 Hz; we do one synchronous refresh so the indicator does not
    // start with an empty badge.
    //--------------------------------------------------------------------------
    lastStateChangeMs_ = juce::Time::getMillisecondCounter();
    timerCallback();

    //--------------------------------------------------------------------------
    // Start the editor's refresh timer. The processor already runs its
    // own timer for the AnalysisFifo -> PerformanceHistory pump; the
    // editor's timer only updates the StateIndicator and any
    // header-driven readouts that are not already handled by the
    // header's own timer.
    //--------------------------------------------------------------------------
    startTimer (33); // ~30 Hz

    //--------------------------------------------------------------------------
    // Resizability and limits per ChugGate specification §21.1.
    // Default 1280 x 760, freely resizable, minimum 920 x 580.
    //--------------------------------------------------------------------------
    setResizable (true, true);
    setResizeLimits (920, 580, 4096, 2160);
    setSize (1280, 760);
}

ChugGateAudioProcessorEditor::~ChugGateAudioProcessorEditor()
{
    // Stop the editor's timer first so a late tick cannot touch the
    // partially-destroyed member hierarchy.
    stopTimer();

    // Detach the editor's L&F and clear the default so destruction
    // order does not leave a dangling pointer in the JUCE L&F chain.
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
// Phase J - rebuild the JSON factory preset combo. Called from the
// constructor and from refreshNow() so the list is current even if the
// host moved the executable to a non-standard location. The combo IDs
// are 1-based because JUCE combo IDs are stable references, not array
// indices. Empty directories leave a single "(no presets)" placeholder.
//==============================================================================
void ChugGateAudioProcessorEditor::rebuildJsonPresetList() noexcept
{
    if (jsonPresetCombo_ == nullptr) return;

    jsonPresetCombo_->clear();

    const auto names = audioProcessor.getFactoryPresetsJson();
    if (names.size() > 0)
    {
        int id = 1;
        jsonPresetCombo_->addSectionHeading ("Factory (JSON)");
        for (const auto& name : names)
            jsonPresetCombo_->addItem (name, id++);
        jsonPresetCombo_->setSelectedId (1, juce::dontSendNotification);
    }
    else
    {
        jsonPresetCombo_->addItem ("(no JSON presets found)", 1);
        jsonPresetCombo_->setSelectedId (1, juce::dontSendNotification);
    }
}

//==============================================================================
// Phase J - refresh the audition indicator and dirty-state label from
// the APVTS. Called from timerCallback() at 30 Hz. The audition
// indicator maps the rejected-mode / state-filter parameters to a
// short human-readable label so the user knows the output is
// audition / bypass-of-wet when auditioning (CHUGGATE_SPEC.md §20.5).
// The dirty indicator is a single "*" character that turns on when
// isStateDirty() returns true.
//==============================================================================
void ChugGateAudioProcessorEditor::refreshStatusLabels() noexcept
{
    if (auditionIndicatorLabel_ != nullptr)
    {
        const auto& apvts = audioProcessor.getAPVTS();
        const int rejectedMode = static_cast<int> (std::lround (
            apvts.getRawParameterValue (
                ChugGate::ParameterIDs::Audition::rejectedMode) != nullptr
            ? apvts.getRawParameterValue (
                ChugGate::ParameterIDs::Audition::rejectedMode)->load()
            : 0.0f));

        if (rejectedMode == 0)
        {
            auditionIndicatorLabel_->setText ("Audition: off",
                                             juce::dontSendNotification);
        }
        else
        {
            const char* label = "All";
            switch (rejectedMode)
            {
                case 1: label = "All rejected";   break;
                case 2: label = "Full band";      break;
                case 3: label = "Weight band";    break;
                case 4: label = "Voice band";     break;
                case 5: label = "Hiss band";      break;
                case 6: label = "Hiss curtain";   break;
                case 7: label = "Noise model";    break;
                case 8: label = "Detector key";   break;
                default: label = "Unknown";       break;
            }
            auditionIndicatorLabel_->setText ("Audition: " + juce::String (label) +
                                             " (rejected signal -- output is bypass-of-wet)",
                                             juce::dontSendNotification);
        }
    }

    if (dirtyStateLabel_ != nullptr)
    {
        if (audioProcessor.isStateDirty())
            dirtyStateLabel_->setText ("State: dirty*",
                                      juce::dontSendNotification);
        else
            dirtyStateLabel_->setText ("State: clean",
                                      juce::dontSendNotification);
    }
}

//==============================================================================
void ChugGateAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Suite background — same gradient as before. Uses the LookAndFeel
    // palette so we keep colour usage centralised.
    const auto bg = ChugGate::GUI::ChugLookAndFeel::getBackgroundColour();
    g.fillAll (bg);
}

//==============================================================================
void ChugGateAudioProcessorEditor::resized()
{
    //--------------------------------------------------------------------------
    // Layout per CHUGGATE_SPEC.md §21.3.
    //
    //  ┌──────────────────────────────────────────────────────────────┐
    //  │ MainHeader (56 px)                                            │
    //  ├──────────────────────────────────────────────────────────────┤
    //  │ PerformanceTimeline          │ StateIndicator                  │
    //  │                              │  (state, duration, gain, ...)   │
    //  ├──────────────────────────────────────────────────────────────┤
    //  │ TabbedComponent (Perform / Shape / Detect / Align / Diagnose)│
    //  └──────────────────────────────────────────────────────────────┘
    //
    //  Vertical weights are chosen so the timeline + state indicator row
    //  stays readable at the minimum editor height (580 px) — at min
    //  size we still have ~150 px for the timeline and a meaningful tab
    //  body underneath.
    //--------------------------------------------------------------------------
    auto area = getLocalBounds();

    // Header
    header_->setBounds (area.removeFromTop (56));

    // Timeline + state indicator row
    const int timelineRowHeight = juce::jlimit (140, 320, area.getHeight() / 3);
    auto timelineRow = area.removeFromTop (timelineRowHeight);

    const int stateWidth = juce::jlimit (220, 320, timelineRow.getWidth() / 4);
    stateIndicator_->setBounds (timelineRow.removeFromRight (stateWidth));
    timeline_->setBounds (timelineRow);

    // Tabs fill the rest
    tabs_->setBounds (area);
}

//==============================================================================
bool ChugGateAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Forward Tab / Shift+Tab through JUCE's standard focus-traversal
    // chain. We do not consume these keys — the LookAndFeel-supplied
    // focus order walks every child control. Returning false lets the
    // base class perform its own default handling so arrow keys and
    // other shortcuts behave normally.
    juce::ignoreUnused (key);
    return false;
}

//==============================================================================
void ChugGateAudioProcessorEditor::timerCallback()
{
    // Build a fresh StateSnapshot from the engine's real-time atomic
    // fields and APVTS. The push is non-blocking (setStateSnapshot
    // copies by value and calls repaint()).
    stateIndicator_->setStateSnapshot (buildStateSnapshot());

    // Refresh the status labels (audition indicator + dirty state).
    refreshStatusLabels();

    // Header status labels (latency, CPU, sidechain) refresh inside the
    // header's own timer; nothing to do here.
}

//==============================================================================
ChugGate::GUI::StateSnapshot ChugGateAudioProcessorEditor::buildStateSnapshot() noexcept
{
    ChugGate::GUI::StateSnapshot snap;

    // Engine state -> GateState. The enum values of the DSP state
    // machine and the GUI GateState are aligned 0..7; the GUI enum has
    // one extra "Feedback" entry that overlays the Feedback Friend
    // protection flag (see below).
    const int engineState = static_cast<int> (audioProcessor.getCurrentState());
    const auto gateState = ChugGate::GUI::ChugLookAndFeel::gateStateFromEngine (engineState);
    snap.state = gateState;

    // Duration in current state
    if (engineState != lastEngineState_)
    {
        lastEngineState_    = engineState;
        lastStateChangeMs_  = juce::Time::getMillisecondCounter();
    }
    snap.durationMs = static_cast<double> (juce::Time::getMillisecondCounter() - lastStateChangeMs_);

    // Gain (dBFS). The engine stores linear gain in realTimeState.lastGain.
    const float linearGain = audioProcessor.getLastGain();
    if (linearGain <= 0.0f) snap.gainDb = -80.0f;
    else                    snap.gainDb = juce::Decibels::gainToDecibels (linearGain, -80.0f);

    // Reason — short human label per state. The ChugLookAndFeel already
    // exposes getStateName(); the indicator displays it on its own, but
    // we provide a richer "reason" string here (state + context) so the
    // indicator can show why the gate is in this state.
    const auto stateName = ChugGate::GUI::ChugLookAndFeel::getStateName (gateState);
    switch (gateState)
    {
        case ChugGate::GUI::GateState::Closed:  snap.reasonText = stateName + " -- below threshold"; break;
        case ChugGate::GUI::GateState::Primed:  snap.reasonText = stateName + " -- onset detected";   break;
        case ChugGate::GUI::GateState::Attack:  snap.reasonText = stateName + " -- opening";          break;
        case ChugGate::GUI::GateState::Open:    snap.reasonText = stateName + " -- passing audio";    break;
        case ChugGate::GUI::GateState::Hold:    snap.reasonText = stateName + " -- holding";          break;
        case ChugGate::GUI::GateState::Sustain: snap.reasonText = stateName + " -- harmonic tail";    break;
        case ChugGate::GUI::GateState::Release: snap.reasonText = stateName + " -- closing";          break;
        case ChugGate::GUI::GateState::Reopen:  snap.reasonText = stateName + " -- reopening";        break;
        case ChugGate::GUI::GateState::Feedback:snap.reasonText = stateName + " -- feedback held";    break;
    }

    // Protection flags — derive from APVTS choices/toggles. We use the
    // raw parameter values directly (the message thread is allowed to
    // read them). Constants are namespaced members of
    // ChugGate::GUI::StateProtections.
    auto& apvts = audioProcessor.getAPVTS();
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::AttackGuard::enabled))
        if (p->load() > 0.5f) snap.protectionFlags |= ChugGate::GUI::StateProtections::AttackGuard;
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::Sustain::feedbackEnabled))
        if (p->load() > 0.5f) snap.protectionFlags |= ChugGate::GUI::StateProtections::FeedbackFriend;
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::Key::humMode))
    {
        // humMode: 0=Off, 1=50, 2=60, 3=Auto
        if (std::lround (p->load()) != 0) snap.protectionFlags |= ChugGate::GUI::StateProtections::HumShield;
    }
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::Cadence::enabled))
        if (p->load() > 0.5f) snap.protectionFlags |= ChugGate::GUI::StateProtections::LegatoReopen;
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::HissCurtain::depth))
    {
        if (p->load() > 0.05f) snap.protectionFlags |= ChugGate::GUI::StateProtections::HissCurtain;
    }
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::Global::lookahead))
    {
        // 0 = Off; any other index means a lookahead value is active
        if (std::lround (p->load()) > 0) snap.protectionFlags |= ChugGate::GUI::StateProtections::Lookahead;
    }
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::Sustain::enabled))
    {
        if (p->load() > 0.5f) snap.protectionFlags |= ChugGate::GUI::StateProtections::SustainExtender;
    }
    if (auto* p = apvts.getRawParameterValue (ChugGate::ParameterIDs::Global::topology))
    {
        // 0=FullBand, 1=DualBand, 2=TriBand, 3=HissCurtain
        if (std::lround (p->load()) > 0) snap.protectionFlags |= ChugGate::GUI::StateProtections::MultibandDepth;
    }

    return snap;
}
