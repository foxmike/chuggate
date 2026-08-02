// MainHeader.cpp
//
// Implementation of the top-of-editor header strip (CHUGGATE_SPEC.md
// §21.3). See MainHeader.h for the public surface and the rationale of
// the wiring model.
//
// Layout (computed in resized(), two horizontal rows):
//
//   ┌────────────────────────────────────────────────────────────────────┐
//   │ [Preset ▾] [Load] [Save] │ [Profile ▾] │ [A][B][Copy][Swap]       │
//   │ [Key: Main Input] [SC: —] │ [Learn] [Align] │ [Bypass] │ Lat: 0ms │
//   │ CPU: 0%                                                              │
//   └────────────────────────────────────────────────────────────────────┘
//
// The header is 56 px tall. At the minimum editor width (~920 px) every
// control stays visible across two fixed-height rows, with flexible space
// before the right-aligned controls on the second row.

#include "MainHeader.h"

#include "ChugLookAndFeel.h"

#include "../PluginProcessor.h"
#include "../StateManager.h"
#include "../ParameterIDs.h"

namespace ChugGate::GUI {

namespace
{
    //--------------------------------------------------------------------------
    // Layout constants
    //
    // Widths are sized so every control stays visible at the minimum
    // editor width (~920 px) with the standard 4 px outer insets. The
    // total row width sums to ~840 px including group gaps, which is
    // well under the 912 px available at 920 px editor width.
    //--------------------------------------------------------------------------
    constexpr int kHeaderHeight     = 56;
    constexpr int kOuterInset       = 4;
    constexpr int kGroupGap         = 4;   // gap between logical groups
    constexpr int kInnerGap         = 2;   // gap inside a group
    constexpr int kControlHeight    = 22;

    constexpr int kLabelWidth       = 24;  // small left labels
    constexpr int kComboWidth       = 140; // preset / profile combos
    constexpr int kSmallBtnWidth    = 48;  // load / save / learn / align
    constexpr int kABBtnWidth       = 26;  // A / B buttons
    constexpr int kABActionWidth    = 48;  // copy / swap
    constexpr int kBypassWidth      = 60;  // bypass toggle
    constexpr int kKeySourceWidth   = 110; // "Key: …" status
    constexpr int kSidechainWidth   = 70;  // "SC: …" status
    constexpr int kReadoutWidth     = 56;  // latency / CPU readouts

    //--------------------------------------------------------------------------
    // The profile list MUST match the APVTS layout in PluginProcessor.cpp
    // (the choice parameter "profile.index"). Hard-coding keeps the header
    // self-contained; both sides are stable per spec §27.
    //--------------------------------------------------------------------------
    const char* const kProfileLabels[] = {
        "Tight Rhythm",
        "Low-Tuned Rhythm",
        "Fast Alternate Picking",
        "Lead Sustain",
        "Rhythm + Lead Hybrid",
        "Controlled Feedback Lead",
        "Pre-Amp DI Gate",
        "Post-Amp Hiss Control",
        "DI-Keyed Reamp",
        "Stereo Rhythm Bus",
        "Active Bass",
        "Distorted Bass",
    };
    constexpr int kNumProfileLabels =
        static_cast<int> (sizeof (kProfileLabels) / sizeof (kProfileLabels[0]));

    //--------------------------------------------------------------------------
    // The key source list MUST match the APVTS layout for "key.source".
    // We only display the human-readable label; the underlying integer
    // index is the APVTS value.
    //--------------------------------------------------------------------------
    const char* const kKeySourceLabels[] = {
        "Main Input",
        "External SC",
        "Blended",
        "Left",
        "Right",
        "Stereo Max",
        "Stereo Avg",
        "Mid",
        "Side",
    };
    constexpr int kNumKeySourceLabels =
        static_cast<int> (sizeof (kKeySourceLabels) / sizeof (kKeySourceLabels[0]));

    //--------------------------------------------------------------------------
    // Style helpers
    //--------------------------------------------------------------------------
    void styleSmallButton (juce::TextButton& b)
    {
        b.setComponentID ("headerButton");
    }

    void styleSmallToggle (juce::ToggleButton& t)
    {
        t.setComponentID ("headerToggle");
    }

    void styleCombo (juce::ComboBox& c, juce::Label& name)
    {
        c.setName (name.getName());
        c.setComponentID ("headerCombo");
    }

    //--------------------------------------------------------------------------
    // Read a parameter's current integer value (for choice parameters we
    // use the rounded normalised value as the index). Returns 0 when the
    // parameter is missing so the header can show a sane default label.
    //--------------------------------------------------------------------------
    int readChoiceIndex (juce::AudioProcessorValueTreeState& apvts,
                         const char* paramId)
    {
        if (auto* p = apvts.getRawParameterValue (paramId))
        {
            const int n = static_cast<int> (std::lround (p->load()));
            if (n >= 0) return n;
        }
        return 0;
    }
} // namespace

//==============================================================================
// Construction / destruction
//==============================================================================
MainHeader::MainHeader (juce::AudioProcessorValueTreeState& apvts)
    : apvts_ (apvts)
{
    setOpaque (false);
    setWantsKeyboardFocus (false);

    //--------------------------------------------------------------------------
    // Preset selector
    //--------------------------------------------------------------------------
    presetLabel_ = std::make_unique<juce::Label> ("", "Preset");
    presetLabel_->setComponentID ("headerLabel");
    addAndMakeVisible (*presetLabel_);

    presetCombo_ = std::make_unique<juce::ComboBox> ("preset");
    styleCombo (*presetCombo_, *presetLabel_);
    addAndMakeVisible (*presetCombo_);

    presetLoadButton_ = std::make_unique<juce::TextButton> ("Load");
    styleSmallButton (*presetLoadButton_);
    presetLoadButton_->setTooltip ("Load the selected preset into the current state");
    presetLoadButton_->onClick = [this]
    {
        const int id = presetCombo_->getSelectedId();
        if (id <= 0) return;
        const juce::String name = presetCombo_->getItemText (id - 1);
        if (auto* sm = (processor_ != nullptr) ? processor_->getStateManager() : nullptr)
        {
            // The combo stores user presets prefixed with a separator; we
            // decide factory vs user by re-querying the StateManager's
            // factory list.
            const auto factory = sm->getFactoryPresets();
            const bool isFactory = factory.contains (name);
            sm->loadPreset (name, isFactory);
        }
    };
    addAndMakeVisible (*presetLoadButton_);

    presetSaveButton_ = std::make_unique<juce::TextButton> ("Save");
    styleSmallButton (*presetSaveButton_);
    presetSaveButton_->setTooltip ("Save the current state as a user preset");
    presetSaveButton_->onClick = [this]
    {
        if (auto* sm = (processor_ != nullptr) ? processor_->getStateManager() : nullptr)
        {
            const int id = presetCombo_->getSelectedId();
            juce::String name;
            if (id > 0)
                name = presetCombo_->getItemText (id - 1);
            if (name.isEmpty())
                name = "User Preset";
            sm->saveUserPreset (name);
            rebuildPresetList();
        }
    };
    addAndMakeVisible (*presetSaveButton_);

    //--------------------------------------------------------------------------
    // Profile selector — APVTS attachment
    //--------------------------------------------------------------------------
    profileLabel_ = std::make_unique<juce::Label> ("", "Profile");
    profileLabel_->setComponentID ("headerLabel");
    addAndMakeVisible (*profileLabel_);

    profileCombo_ = std::make_unique<juce::ComboBox> ("profile");
    styleCombo (*profileCombo_, *profileLabel_);
    for (int i = 0; i < kNumProfileLabels; ++i)
        profileCombo_->addItem (kProfileLabels[i], i + 1);
    addAndMakeVisible (*profileCombo_);
    profileAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
            (apvts_, ParameterIDs::profileIndex, *profileCombo_);

    //--------------------------------------------------------------------------
    // A/B buttons
    //--------------------------------------------------------------------------
    aButton_ = std::make_unique<juce::TextButton> ("A");
    styleSmallButton (*aButton_);
    aButton_->setClickingTogglesState (false);
    aButton_->onClick = [this]
    {
        if (auto* sm = (processor_ != nullptr) ? processor_->getStateManager() : nullptr)
            sm->setActiveABState ("A");
    };
    addAndMakeVisible (*aButton_);

    bButton_ = std::make_unique<juce::TextButton> ("B");
    styleSmallButton (*bButton_);
    bButton_->setClickingTogglesState (false);
    bButton_->onClick = [this]
    {
        if (auto* sm = (processor_ != nullptr) ? processor_->getStateManager() : nullptr)
            sm->setActiveABState ("B");
    };
    addAndMakeVisible (*bButton_);

    copyButton_ = std::make_unique<juce::TextButton> ("Copy");
    styleSmallButton (*copyButton_);
    copyButton_->setTooltip ("Copy active A/B slot into the inactive slot");
    copyButton_->onClick = [this]
    {
        if (auto* sm = (processor_ != nullptr) ? processor_->getStateManager() : nullptr)
            sm->copyActiveToInactive();
    };
    addAndMakeVisible (*copyButton_);

    swapButton_ = std::make_unique<juce::TextButton> ("Swap");
    styleSmallButton (*swapButton_);
    swapButton_->setTooltip ("Swap A and B slot contents");
    swapButton_->onClick = [this]
    {
        if (auto* sm = (processor_ != nullptr) ? processor_->getStateManager() : nullptr)
            sm->swapABSlots();
    };
    addAndMakeVisible (*swapButton_);

    //--------------------------------------------------------------------------
    // Learn / Align shortcut buttons
    //--------------------------------------------------------------------------
    learnButton_ = std::make_unique<juce::TextButton> ("Learn");
    styleSmallButton (*learnButton_);
    learnButton_->setTooltip ("Start the noise learn capture (also exposed on the Perform page)");
    learnButton_->onClick = [this]
    {
        if (learnHook_) learnHook_();
    };
    addAndMakeVisible (*learnButton_);

    alignButton_ = std::make_unique<juce::TextButton> ("Align");
    styleSmallButton (*alignButton_);
    alignButton_->setTooltip ("Start the DI alignment capture (also exposed on the Perform page)");
    alignButton_->onClick = [this]
    {
        if (alignHook_) alignHook_();
    };
    addAndMakeVisible (*alignButton_);

    //--------------------------------------------------------------------------
    // Bypass toggle — APVTS attachment
    //--------------------------------------------------------------------------
    bypassToggle_ = std::make_unique<juce::ToggleButton> ("Bypass");
    bypassToggle_->setButtonText ("Bypass");
    styleSmallToggle (*bypassToggle_);
    addAndMakeVisible (*bypassToggle_);
    bypassAttachment_ =
        std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>
            (apvts_, ParameterIDs::Global::bypass, *bypassToggle_);

    //--------------------------------------------------------------------------
    // Read-only status labels
    //--------------------------------------------------------------------------
    keySourceStatusLabel_ = std::make_unique<juce::Label> ("", "Key: --");
    keySourceStatusLabel_->setComponentID ("headerStatusLabel");
    keySourceStatusLabel_->setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (*keySourceStatusLabel_);

    sidechainStatusLabel_ = std::make_unique<juce::Label> ("", "SC: --");
    sidechainStatusLabel_->setComponentID ("headerStatusLabel");
    sidechainStatusLabel_->setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (*sidechainStatusLabel_);

    latencyLabel_ = std::make_unique<juce::Label> ("", "Lat: --");
    latencyLabel_->setComponentID ("headerStatusLabel");
    latencyLabel_->setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (*latencyLabel_);

    cpuLabel_ = std::make_unique<juce::Label> ("", "CPU: --");
    cpuLabel_->setComponentID ("headerStatusLabel");
    cpuLabel_->setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (*cpuLabel_);

    //--------------------------------------------------------------------------
    // Populate preset combo and start the refresh timer
    //--------------------------------------------------------------------------
    rebuildPresetList();
    startTimer (kRefreshMs);
}

MainHeader::~MainHeader()
{
    stopTimer();
}

//==============================================================================
// Wiring
//==============================================================================
void MainHeader::setProcessor (ChugGateAudioProcessor* processor) noexcept
{
    processor_ = processor;
    rebuildPresetList();
    refreshNow();
}

void MainHeader::setLearnHandler (ActionHook hook)
{
    learnHook_ = std::move (hook);
}

void MainHeader::setAlignHandler (ActionHook hook)
{
    alignHook_ = std::move (hook);
}

void MainHeader::refreshNow() noexcept
{
    refreshSidechainStatus();
    refreshLatency();
    refreshCpu();

    // Key source status
    keySourceIndex_ = readChoiceIndex (apvts_, ParameterIDs::Key::source);
    if (keySourceIndex_ >= 0 && keySourceIndex_ < kNumKeySourceLabels)
        keySourceStatusLabel_->setText ("Key: " + juce::String (kKeySourceLabels[keySourceIndex_]),
                                        juce::dontSendNotification);
    else
    keySourceStatusLabel_->setText ("Key: --", juce::dontSendNotification);
}

//==============================================================================
// Component overrides
//==============================================================================
void MainHeader::paint (juce::Graphics& g)
{
    // Header background — slightly raised panel so the row reads as a
    // strip distinct from the timeline below. Uses the suite palette
    // through ChugLookAndFeel accessors so the editor never duplicates
    // hex literals.
    const auto bounds = getLocalBounds().toFloat();
    const auto headerFill   = ChugLookAndFeel::getRaisedPanelColour();
    const auto headerStroke = ChugLookAndFeel::getOutlineColour();
    g.setColour (headerFill);
    g.fillRect (bounds);
    g.setColour (headerStroke);
    g.drawLine (static_cast<float> (bounds.getX()),
                static_cast<float> (bounds.getBottom()),
                static_cast<float> (bounds.getRight()),
                static_cast<float> (bounds.getBottom()),
                1.0f);
}

void MainHeader::resized()
{
    auto area = getLocalBounds().reduced (kOuterInset, 4);

    auto topRow = area.removeFromTop (kControlHeight);
    area.removeFromTop (4);
    auto bottomRow = area.removeFromTop (kControlHeight);

    // 1) Preset group: [combo][Load][Save]
    {
        const int w = kComboWidth + kInnerGap + kSmallBtnWidth
                       + kInnerGap + kSmallBtnWidth;
        auto group = topRow.removeFromLeft (w);
        presetCombo_->setBounds (group.removeFromLeft (kComboWidth));
        group.removeFromLeft (kInnerGap);
        presetLoadButton_->setBounds (group.removeFromLeft (kSmallBtnWidth));
        group.removeFromLeft (kInnerGap);
        presetSaveButton_->setBounds (group.removeFromLeft (kSmallBtnWidth));
    }
    topRow.removeFromLeft (kInnerGap);

    // 2) Profile group: [combo]
    {
        const int w = kComboWidth;
        auto group = topRow.removeFromLeft (w);
        profileCombo_->setBounds (group.removeFromLeft (kComboWidth));
    }
    topRow.removeFromLeft (kGroupGap);

    // 3) A/B group: [A][B][Copy][Swap]
    {
        const int w = kABBtnWidth + kInnerGap + kABBtnWidth
                       + kInnerGap + kABActionWidth
                       + kInnerGap + kABActionWidth;
        auto group = topRow.removeFromLeft (w);
        aButton_->setBounds (group.removeFromLeft (kABBtnWidth));
        group.removeFromLeft (kInnerGap);
        bButton_->setBounds (group.removeFromLeft (kABBtnWidth));
        group.removeFromLeft (kInnerGap);
        copyButton_->setBounds (group.removeFromLeft (kABActionWidth));
        group.removeFromLeft (kInnerGap);
        swapButton_->setBounds (group.removeFromLeft (kABActionWidth));
    }

    // 4) Key source status + sidechain indicator
    keySourceStatusLabel_->setBounds (bottomRow.removeFromLeft (kKeySourceWidth));
    bottomRow.removeFromLeft (kGroupGap);
    sidechainStatusLabel_->setBounds (bottomRow.removeFromLeft (kSidechainWidth));

    bottomRow.removeFromLeft (kGroupGap);

    // 5) Learn / Align shortcut buttons
    learnButton_->setBounds (bottomRow.removeFromLeft (kSmallBtnWidth));
    bottomRow.removeFromLeft (kGroupGap);
    alignButton_->setBounds (bottomRow.removeFromLeft (kSmallBtnWidth));

    // 6) Bypass toggle and readouts on the right edge.
    cpuLabel_->setBounds (bottomRow.removeFromRight (kReadoutWidth));
    bottomRow.removeFromRight (kGroupGap);
    latencyLabel_->setBounds (bottomRow.removeFromRight (kReadoutWidth));
    bottomRow.removeFromRight (kGroupGap);
    bypassToggle_->setBounds (bottomRow.removeFromRight (kBypassWidth));
}

//==============================================================================
// Timer
//==============================================================================
void MainHeader::timerCallback()
{
    // We compute the GUI tick CPU as the wall-clock time spent inside
    // this callback relative to the refresh period. The header refreshes
    // only its derived readouts here; the rest of the GUI is updated by
    // the editor's own timer.
    const auto startTime = juce::Time::getMillisecondCounterHiRes();

    refreshSidechainStatus();
    refreshLatency();
    refreshCpu();

    // Key source (cheap APVTS read)
    const int newKeySrc = readChoiceIndex (apvts_, ParameterIDs::Key::source);
    if (newKeySrc != keySourceIndex_)
    {
        keySourceIndex_ = newKeySrc;
        if (keySourceIndex_ >= 0 && keySourceIndex_ < kNumKeySourceLabels)
            keySourceStatusLabel_->setText ("Key: " + juce::String (kKeySourceLabels[keySourceIndex_]),
                                            juce::dontSendNotification);
        else
            keySourceStatusLabel_->setText ("Key: --", juce::dontSendNotification);
    }

    const auto endTime = juce::Time::getMillisecondCounterHiRes();
    const double tickMs = endTime - startTime;
    const double pct = 100.0 * tickMs / static_cast<double> (kRefreshMs);
    const float smoothed = 0.9f * cpuPercentSmoothed_ + 0.1f * static_cast<float> (pct);
    cpuPercentSmoothed_ = juce::jlimit (0.0f, 100.0f, smoothed);

    cpuLabel_->setText ("CPU: " + juce::String (static_cast<int> (std::lround (cpuPercentSmoothed_))) + "%",
                        juce::dontSendNotification);
}

//==============================================================================
// Refresh helpers
//==============================================================================
void MainHeader::refreshSidechainStatus() noexcept
{
    bool present = false;
    bool active  = false;
    if (processor_ != nullptr)
    {
        // The processor has a second input bus ("Sidechain") per the APVTS
        // bus layout. The bus is "present" when the count is > 1 and
        // "active" when it has been enabled by the host.
        if (processor_->getBusCount (true) > 1)
        {
            present = true;
            if (auto* bus = processor_->getBus (true, 1))
                active = bus->isEnabled();
        }
    }

    if (present != sidechainPresent_ || active != sidechainActive_)
    {
        sidechainPresent_ = present;
        sidechainActive_  = active;
        juce::String text = "SC: ";
        if (! present)       text += "none";
        else if (active)     text += "active";
        else                 text += "armed";
        sidechainStatusLabel_->setText (text, juce::dontSendNotification);
    }
}

void MainHeader::refreshLatency() noexcept
{
    if (processor_ == nullptr)
    {
        if (lastLatencySamples_ != 0)
        {
            lastLatencySamples_ = 0;
        latencyLabel_->setText ("Lat: --", juce::dontSendNotification);
        }
        return;
    }

    const int samples = processor_->getLatencySamples();
    const double rate = (processor_->getSampleRate() > 0.0)
                            ? processor_->getSampleRate() : 0.0;
    if (samples != lastLatencySamples_ || std::abs (rate - lastSampleRate_) > 0.01)
    {
        lastLatencySamples_ = samples;
        lastSampleRate_     = rate;
        latencyLabel_->setText ("Lat: " + formatLatencyMs (samples, rate),
                                juce::dontSendNotification);
    }
}

void MainHeader::refreshCpu() noexcept
{
    // CPU is updated inside timerCallback() where the tick duration is
    // measurable. This method is a no-op kept for the public API so
    // callers can force a tick without restarting the timer.
}

juce::String MainHeader::formatLatencyMs (int latencySamples, double sampleRate) noexcept
{
    if (latencySamples <= 0 || sampleRate <= 0.0)
        return "0 ms";

    const double ms = 1000.0 * static_cast<double> (latencySamples) / sampleRate;
    if (ms < 1.0)
    {
        juce::String s;
        s << juce::String (ms, 2);
        return s + " ms";
    }
    if (ms < 10.0)
    {
        juce::String s;
        s << juce::String (ms, 1);
        return s + " ms";
    }
    return juce::String (static_cast<int> (std::lround (ms))) + " ms";
}

void MainHeader::rebuildPresetList() noexcept
{
    if (presetCombo_ == nullptr) return;

    presetCombo_->clear();

    int nextId = 1;
    juce::StringArray factory;
    juce::StringArray user;
    if (processor_ != nullptr)
    {
        if (auto* sm = processor_->getStateManager())
        {
            factory = sm->getFactoryPresets();
            user    = sm->getUserPresets();
        }
    }

    if (factory.size() > 0)
    {
        presetCombo_->addSectionHeading ("Factory");
        for (const auto& name : factory)
            presetCombo_->addItem (name, nextId++);
    }
    if (user.size() > 0)
    {
        presetCombo_->addSectionHeading ("User");
        for (const auto& name : user)
            presetCombo_->addItem (name, nextId++);
    }
    if (nextId == 1)
        presetCombo_->addItem ("(no presets found)", nextId++);
    presetCombo_->setSelectedId (1, juce::dontSendNotification);
}

} // namespace ChugGate::GUI
