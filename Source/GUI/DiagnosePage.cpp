// DiagnosePage.cpp
//
// See DiagnosePage.h. Implements the Diagnose view (CHUGGATE_SPEC.md
// §21.4 / §20.3 / §20.5 / §20.6).
//
// Layout (computed in resized()):
//
//   ┌────────────────────────────────────────────────────────────────────┐
//   │  DIAGNOSE                                                         │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  NOISE FINGERPRINT                                                │
//   │   ┌──────────────────────────────────────────────────────────┐     │
//   │   │  median floor + upper percentile + detector filter       │     │
//   │   │  peak markers, frequency axis                            │     │
//   │   └──────────────────────────────────────────────────────────┘     │
//   │   [50 Hz] [60 Hz] [Harm.] [Hiss] [Bursts]                          │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  REJECTED-SIGNAL AUDITION                                          │
//   │   [Rejected mode combo]   [State filter combo]                     │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  ATTACK INTEGRITY    TAIL INTEGRITY                                │
//   │   A_r: 0.00            Choke score: 0.00                           │
//   │   Preserved: 0         Choked: 0                                   │
//   │   Partial: 0                                                       │
//   │   Missed: 0                                                        │
//   ├────────────────────────────────────────────────────────────────────┤
//   │  ANALYZER MODE                                                     │
//   │   [Off | Live | Studio | Diagnose]                                 │
//   └────────────────────────────────────────────────────────────────────┘
//
// All paint work happens on the message thread via local snapshots
// refreshed from the analysis APIs and APVTS attachments.

#include "DiagnosePage.h"

#include "ChugLookAndFeel.h"

#include "../ParameterIDs.h"

#include <cmath>

// The two integrity analyzer headers are produced by phase H. They may
// not be present in a partially-completed build — guard the include so
// DiagnosePage remains buildable when the analyzers are not yet on
// disk. When the headers are absent, the analyzer pointers remain valid
// as opaque types and the snapshot simply stays at its zeroed default.
#if __has_include("../Analysis/AttackIntegrityAnalyzer.h")
  #include "../Analysis/AttackIntegrityAnalyzer.h"
  #define CHUGGATE_HAS_ATTACK_INTEGRITY 1
#else
  #define CHUGGATE_HAS_ATTACK_INTEGRITY 0
#endif

#if __has_include("../Analysis/TailIntegrityAnalyzer.h")
  #include "../Analysis/TailIntegrityAnalyzer.h"
  #define CHUGGATE_HAS_TAIL_INTEGRITY 1
#else
  #define CHUGGATE_HAS_TAIL_INTEGRITY 0
#endif

namespace ChugGate::GUI {

namespace
{
    //----------------------------------------------------------------------
    // Layout constants.
    //----------------------------------------------------------------------
    constexpr int kOuterInset       = 8;
    constexpr int kRowGap           = 6;
    constexpr int kGroupTitleHeight = 18;
    constexpr int kGroupInnerInset  = 6;

    // Vertical weights. The noise fingerprint is the largest panel; the
    // remaining three rows split the rest.
    constexpr float kNoiseWeight      = 0.46f;
    constexpr float kAuditionWeight   = 0.16f;
    constexpr float kIntegrityWeight  = 0.22f;
    constexpr float kModeWeight       = 0.16f;

    constexpr int kNoiseMinH          = 180;
    constexpr int kAuditionMinH       = 80;
    constexpr int kIntegrityMinH      = 100;
    constexpr int kModeMinH           = 60;

    constexpr int kInitialNumBins     = 128;

    //----------------------------------------------------------------------
    // APVTS → combo box binding helper. When the APVTS parameter exists
    // and is an AudioParameterChoice, the combo box is populated with the
    // choice's text items. Otherwise the combo stays empty and the page
    // remains usable.
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
    // Format an unsigned counter.
    //----------------------------------------------------------------------
    juce::String formatCount (std::uint64_t v)
    {
        return juce::String (static_cast<juce::int64> (v));
    }

    //----------------------------------------------------------------------
    // Format a float ratio as a percentage with one decimal.
    //----------------------------------------------------------------------
    juce::String formatPercent (float v)
    {
        if (std::isnan (v)) return juce::String ("n/a");
        return juce::String (v * 100.0f, 1) + juce::String (" %");
    }
} // namespace

//==============================================================================
// Construction / destruction.
//==============================================================================
DiagnosePage::DiagnosePage()
    : noiseGroup_(),
      auditionGroup_(),
      integrityGroup_(),
      modeGroup_()
{
    setOpaque (false);
    setWantsKeyboardFocus (false);

    // Build the four group panels.
    noiseGroup_.panel     = std::make_unique<juce::Component> ("noisePanel");
    auditionGroup_.panel  = std::make_unique<juce::Component> ("auditionPanel");
    integrityGroup_.panel = std::make_unique<juce::Component> ("integrityPanel");
    modeGroup_.panel      = std::make_unique<juce::Component> ("modePanel");

    for (auto* g : { noiseGroup_.panel.get(),
                     auditionGroup_.panel.get(),
                     integrityGroup_.panel.get(),
                     modeGroup_.panel.get() })
    {
        if (g) addAndMakeVisible (*g);
    }

    // Rejected-signal mode combo (bound to audition.rejectedMode).
    rejectedModeCombo_ = std::make_unique<juce::ComboBox> ("rejectedModeCombo");
    rejectedModeCombo_->setComponentID ("diagnoseRejectedMode");
    rejectedModeCombo_->setTooltip ("Which rejected signal to audition (spec sec. 20.5)");
    addAndMakeVisible (*rejectedModeCombo_);

    rejectedModeLabel_ = std::make_unique<juce::Label> ("rejectedModeLabel",
                                                       "Rejected route");
    rejectedModeLabel_->setJustificationType (juce::Justification::centredLeft);
    rejectedModeLabel_->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*rejectedModeLabel_);

    rejectedModeCombo_->onChange = [this]
    {
        // Re-attachment keeps the APVTS in sync; nothing else to do here.
    };

    // State-filter mode combo (bound to audition.stateFilterMode).
    stateFilterCombo_ = std::make_unique<juce::ComboBox> ("stateFilterCombo");
    stateFilterCombo_->setComponentID ("diagnoseStateFilter");
    stateFilterCombo_->setTooltip ("Trigger audition only during this state (spec sec. 20.5)");
    addAndMakeVisible (*stateFilterCombo_);

    stateFilterLabel_ = std::make_unique<juce::Label> ("stateFilterLabel",
                                                      "State trigger");
    stateFilterLabel_->setJustificationType (juce::Justification::centredLeft);
    stateFilterLabel_->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*stateFilterLabel_);

    // Analyzer mode combo (not bound to APVTS; wired through callback).
    analyzerModeCombo_ = std::make_unique<juce::ComboBox> ("analyzerModeCombo");
    analyzerModeCombo_->setComponentID ("diagnoseAnalyzerMode");
    analyzerModeCombo_->addItem ("Off",      1);
    analyzerModeCombo_->addItem ("Live",     2);
    analyzerModeCombo_->addItem ("Studio",   3);
    analyzerModeCombo_->addItem ("Diagnose", 4);
    analyzerModeCombo_->setSelectedId (3, juce::dontSendNotification); // default = Diagnose
    analyzerModeCombo_->setTooltip ("Analyzer layer preset (spec sec. 20.6)");
    addAndMakeVisible (*analyzerModeCombo_);

    analyzerModeLabel_ = std::make_unique<juce::Label> ("analyzerModeLabel",
                                                       "Mode");
    analyzerModeLabel_->setJustificationType (juce::Justification::centredLeft);
    analyzerModeLabel_->setInterceptsMouseClicks (false, false);
    addAndMakeVisible (*analyzerModeLabel_);

    analyzerModeCombo_->onChange = [this]
    {
        if (! analyzerModeHandler_) return;
        // Map the combo selection (1..4) to a 0..3 mode index.
        const int id = analyzerModeCombo_->getSelectedId();
        const int modeIdx = std::max (0, id - 1);
        analyzerModeHandler_ (modeIdx);
    };

    // Initial snapshot capacity.
    ensureNoiseSnapshotCapacity (kInitialNumBins);

    // Start the refresh timer — runs on the message thread.
    startTimer (refreshMs_);
}

DiagnosePage::~DiagnosePage()
{
    stopTimer();
}

//==============================================================================
// Wiring.
//==============================================================================
void DiagnosePage::setNoiseFingerprint (ChugGate::Analysis::NoiseFingerprint* noise) noexcept
{
    noiseFingerprint_ = noise;
    refreshNow();
}

void DiagnosePage::setAttackIntegrityAnalyzer (ChugGate::Analysis::AttackIntegrityAnalyzer* ai) noexcept
{
    attackAnalyzer_ = ai;
    refreshNow();
}

void DiagnosePage::setTailIntegrityAnalyzer (ChugGate::Analysis::TailIntegrityAnalyzer* ti) noexcept
{
    tailAnalyzer_ = ti;
    refreshNow();
}

void DiagnosePage::setApvts (juce::AudioProcessorValueTreeState& apvts) noexcept
{
    apvts_ = &apvts;

    if (rejectedModeCombo_
        && apvts.getParameter (ChugGate::ParameterIDs::Audition::rejectedMode) != nullptr)
    {
        addChoiceItems (*rejectedModeCombo_, apvts,
                        ChugGate::ParameterIDs::Audition::rejectedMode);
        rejectedModeAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts, ChugGate::ParameterIDs::Audition::rejectedMode, *rejectedModeCombo_);
    }

    if (stateFilterCombo_
        && apvts.getParameter (ChugGate::ParameterIDs::Audition::stateFilterMode) != nullptr)
    {
        addChoiceItems (*stateFilterCombo_, apvts,
                        ChugGate::ParameterIDs::Audition::stateFilterMode);
        stateFilterAttachment_ =
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts, ChugGate::ParameterIDs::Audition::stateFilterMode, *stateFilterCombo_);
    }

    refreshNow();
}

void DiagnosePage::setAnalyzerModeHandler (AnalyzerModeHandler handler)
{
    analyzerModeHandler_ = std::move (handler);
}

void DiagnosePage::refreshNow() noexcept
{
    repaint();
}

//==============================================================================
// juce::Timer — message-thread refresh of local snapshots.
//==============================================================================
void DiagnosePage::timerCallback()
{
    refreshNoiseSnapshot();
    refreshAnalyzerReadouts();

    repaint();
}

//==============================================================================
// Snapshot management.
//==============================================================================
void DiagnosePage::ensureNoiseSnapshotCapacity (int bins) noexcept
{
    if (bins < 16) bins = 16;

    if (static_cast<int> (noiseSnapshot_.medianFloor.size()) < bins)
        noiseSnapshot_.medianFloor.resize (static_cast<std::size_t> (bins), 0.0f);
    if (static_cast<int> (noiseSnapshot_.upperPercentile.size()) < bins)
        noiseSnapshot_.upperPercentile.resize (static_cast<std::size_t> (bins), 0.0f);
    if (static_cast<int> (noiseSnapshot_.detectorFilterResponse.size()) < bins)
        noiseSnapshot_.detectorFilterResponse.resize (static_cast<std::size_t> (bins), 0.0f);
}

void DiagnosePage::refreshNoiseSnapshot() noexcept
{
    // Reset scalar / flag fields; copy* APIs fill the vectors.
    noiseSnapshot_.numBins                = 0;
    noiseSnapshot_.numMultibandThresholds = 0;
    noiseSnapshot_.sampleRate             = 0.0;
    noiseSnapshot_.humFiftyHz             = false;
    noiseSnapshot_.humSixtyHz             = false;
    noiseSnapshot_.humHarmonics           = false;
    noiseSnapshot_.hfHiss                 = false;
    noiseSnapshot_.intermittentBursts     = false;
    noiseSnapshot_.humPeakBin             = -1;
    noiseSnapshot_.harmonicPeakBin        = -1;
    noiseSnapshot_.hissPeakBin            = -1;
    noiseSnapshot_.hasData                = false;

    if (noiseFingerprint_ == nullptr)
        return;

    const int bins = noiseFingerprint_->getNumBins();
    if (bins <= 0)
        return;

    ensureNoiseSnapshotCapacity (bins);

    const int copiedMed = noiseFingerprint_->copyMedianFloor (
        noiseSnapshot_.medianFloor.data(),
        static_cast<int> (noiseSnapshot_.medianFloor.size()));

    const int copiedUp = noiseFingerprint_->copyUpperPercentile (
        noiseSnapshot_.upperPercentile.data(),
        static_cast<int> (noiseSnapshot_.upperPercentile.size()));

    const int copiedDet = noiseFingerprint_->copyDetectorFilterResponse (
        noiseSnapshot_.detectorFilterResponse.data(),
        static_cast<int> (noiseSnapshot_.detectorFilterResponse.size()));

    const int copiedMb = noiseFingerprint_->copyMultibandThresholds (
        noiseSnapshot_.multibandThresholds.data(),
        static_cast<int> (noiseSnapshot_.multibandThresholds.size()));

    noiseSnapshot_.numBins                = copiedMed;
    noiseSnapshot_.numMultibandThresholds = copiedMb;
    noiseSnapshot_.sampleRate             = noiseFingerprint_->getSampleRate();

    noiseSnapshot_.humFiftyHz             = noiseFingerprint_->hasFiftyHzHum();
    noiseSnapshot_.humSixtyHz             = noiseFingerprint_->hasSixtyHzHum();
    noiseSnapshot_.humHarmonics           = noiseFingerprint_->hasHumHarmonics();
    noiseSnapshot_.hfHiss                 = noiseFingerprint_->hasHighFrequencyHiss();
    noiseSnapshot_.intermittentBursts     = noiseFingerprint_->hasIntermittentBursts();

    noiseSnapshot_.humPeakBin             = noiseFingerprint_->getHumPeakBin();
    noiseSnapshot_.harmonicPeakBin        = noiseFingerprint_->getHarmonicPeakBin();
    noiseSnapshot_.hissPeakBin            = noiseFingerprint_->getHissPeakBin();

    // Drop any noise-flag the analyzer didn't expose.
    (void) copiedUp;
    (void) copiedDet;

    noiseSnapshot_.hasData = (copiedMed > 0)
                          || noiseSnapshot_.humFiftyHz
                          || noiseSnapshot_.humSixtyHz
                          || noiseSnapshot_.humHarmonics
                          || noiseSnapshot_.hfHiss
                          || noiseSnapshot_.intermittentBursts;
}

void DiagnosePage::refreshAnalyzerReadouts() noexcept
{
    // Zero everything first so the readouts panel always paints a
    // coherent set of values (no half-cleared stale state).
    latestAR_         = 0.0f;
    preservedCount_   = 0;
    partialCount_     = 0;
    missedCount_      = 0;
    latestChokeScore_ = 0.0f;
    chokeCount_       = 0;

#if CHUGGATE_HAS_ATTACK_INTEGRITY
    if (attackAnalyzer_ != nullptr)
    {
        latestAR_       = attackAnalyzer_->getAttackPreservationRatio();
        preservedCount_ = attackAnalyzer_->getPreservedCount();
        partialCount_   = attackAnalyzer_->getPartialCount();
        missedCount_    = attackAnalyzer_->getMissedCount();
    }
#endif

#if CHUGGATE_HAS_TAIL_INTEGRITY
    if (tailAnalyzer_ != nullptr)
    {
        latestChokeScore_ = tailAnalyzer_->getChokeScore();
        chokeCount_       = tailAnalyzer_->getChokeCount();
    }
#endif
}

//==============================================================================
// Component overrides.
//==============================================================================
void DiagnosePage::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // Page backdrop.
    g.setColour (ChugLookAndFeel::getPanelColour().withAlpha (0.85f));
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour());
    g.drawRoundedRectangle (bounds.toFloat(), 4.0f, 1.0f);

    // Page title.
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("DIAGNOSE", bounds.removeFromTop (16), juce::Justification::centredLeft);

    // Group backgrounds.
    paintGroupBackground (g, noiseGroup_.panel.get(),     "Noise Fingerprint");
    paintGroupBackground (g, auditionGroup_.panel.get(),  "Rejected-Signal Audition");
    paintGroupBackground (g, integrityGroup_.panel.get(), "Attack / Tail Integrity");
    paintGroupBackground (g, modeGroup_.panel.get(),      "Analyzer Mode");

    // Noise section content (drawn on top of the panel background).
    if (noiseGroup_.panel)
    {
        auto inner = noiseGroup_.panel->getBounds()
                         .reduced (kGroupInnerInset, kGroupInnerInset + kGroupTitleHeight / 2);
        if (! noiseSnapshot_.hasData || noiseFingerprint_ == nullptr)
            paintEmptySection (g, inner, "No learned noise profile yet");
        else
            paintNoiseSection (g, inner);
    }

    // Audition row content (combo boxes are children; only paint
    // empty-state hint when no APVTS was injected).
    if (auditionGroup_.panel)
    {
        if (apvts_ == nullptr)
        {
            auto inner = auditionGroup_.panel->getBounds()
                             .reduced (kGroupInnerInset, kGroupInnerInset + kGroupTitleHeight / 2);
            paintEmptySection (g, inner, "Audition controls -- wire APVTS to bind");
        }
    }

    // Integrity readouts — always paint (analyzer pointers may be null,
    // which is reflected as zero values).
    if (integrityGroup_.panel)
    {
        auto inner = integrityGroup_.panel->getBounds()
                         .reduced (kGroupInnerInset, kGroupInnerInset + kGroupTitleHeight / 2);
        // Two columns: attack / tail.
        const int halfW = inner.getWidth() / 2;
        paintAttackReadouts (g, juce::Rectangle<int> (inner.getX(), inner.getY(),
                                                       halfW, inner.getHeight()));
        paintTailReadouts   (g, juce::Rectangle<int> (inner.getX() + halfW, inner.getY(),
                                                       inner.getWidth() - halfW, inner.getHeight()));
    }
}

void DiagnosePage::paintGroupBackground (juce::Graphics& g,
                                         juce::Component* panel,
                                         const juce::String& title)
{
    if (panel == nullptr) return;
    auto bounds = panel->getBounds().toFloat();

    g.setColour (ChugLookAndFeel::getSunkenPanelColour().withAlpha (0.55f));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    auto titleBounds = juce::Rectangle<int> (panel->getX() + kGroupInnerInset,
                                             panel->getY() + 2,
                                             panel->getWidth() - kGroupInnerInset * 2,
                                             kGroupTitleHeight);
    g.drawText (title, titleBounds, juce::Justification::centredLeft);
}

void DiagnosePage::paintEmptySection (juce::Graphics& g, juce::Rectangle<int> area,
                                      const juce::String& message)
{
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (message, area, juce::Justification::centred, true);
}

//==============================================================================
// Painting helpers.
//==============================================================================
void DiagnosePage::paintNoiseSection (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;

    // Split into a spectrum plot on top and feature flags below.
    const int flagStripH = std::min (22, area.getHeight() / 4);
    auto plotArea = area;
    auto flagArea = plotArea.removeFromBottom (flagStripH);

    // Plot background.
    g.setColour (ChugLookAndFeel::getSunkenPanelColour().withAlpha (0.35f));
    g.fillRect (plotArea);
    g.setColour (ChugLookAndFeel::getOutlineColour().withAlpha (0.4f));
    g.drawRect (plotArea, 1);

    const int bins = noiseSnapshot_.numBins;
    if (bins <= 1)
        return;

    // Plot extents in dB. The floor / percentile buffers are stored in
    // dB. We display a fixed [-100, 0] dB window so the user has a
    // constant reference.
    constexpr float kMinDb = -100.0f;
    constexpr float kMaxDb =    0.0f;

    auto yForDb = [&] (float db) -> float
    {
        const float norm = juce::jlimit (0.0f, 1.0f,
                                          (db - kMinDb) / (kMaxDb - kMinDb));
        return static_cast<float> (plotArea.getBottom())
             - norm * static_cast<float> (plotArea.getHeight());
    };

    auto xForBin = [&] (int bin) -> float
    {
        const float norm = static_cast<float> (bin)
                         / static_cast<float> (bins - 1);
        return static_cast<float> (plotArea.getX())
             + norm * static_cast<float> (plotArea.getWidth());
    };

    // Median-floor polyline.
    auto drawCurve = [&] (const std::vector<float>& v)
    {
        juce::Path path;
        bool started = false;
        for (int b = 0; b < bins && b < static_cast<int> (v.size()); ++b)
        {
            const float x = xForBin (b);
            const float y = yForDb (v[static_cast<std::size_t> (b)]);
            if (! started)
            {
                path.startNewSubPath (x, y);
                started = true;
            }
            else
            {
                path.lineTo (x, y);
            }
        }
        return path;
    };

    g.setColour (ChugLookAndFeel::getNoiseFloorColour());
    g.strokePath (drawCurve (noiseSnapshot_.medianFloor),
                  juce::PathStrokeType (1.25f));

    g.setColour (ChugLookAndFeel::getNoiseFloorColour().withAlpha (0.45f));
    g.strokePath (drawCurve (noiseSnapshot_.upperPercentile),
                  juce::PathStrokeType (1.0f));

    // Detector filter response overlay.
    if (! noiseSnapshot_.detectorFilterResponse.empty())
    {
        g.setColour (ChugLookAndFeel::getAudioTraceColour().withAlpha (0.85f));
        g.strokePath (drawCurve (noiseSnapshot_.detectorFilterResponse),
                      juce::PathStrokeType (1.0f));
    }

    // Peak markers.
    auto drawPeak = [&] (int binIdx, juce::Colour c)
    {
        if (binIdx < 0 || binIdx >= bins) return;
        const float x = xForBin (binIdx);
        g.setColour (c);
        g.drawLine (x, static_cast<float> (plotArea.getY() + 2),
                    x, static_cast<float> (plotArea.getBottom() - 2),
                    1.5f);
        // Small triangle at the top of the marker so it reads as an event
        // rather than a stray vertical line.
        juce::Path tri;
        tri.startNewSubPath (x - 3.0f, static_cast<float> (plotArea.getY() + 2));
        tri.lineTo (x + 3.0f, static_cast<float> (plotArea.getY() + 2));
        tri.lineTo (x,           static_cast<float> (plotArea.getY() + 8));
        tri.closeSubPath();
        g.fillPath (tri);
    };

    drawPeak (noiseSnapshot_.humPeakBin,      ChugLookAndFeel::getWarningColour());
    drawPeak (noiseSnapshot_.harmonicPeakBin, ChugLookAndFeel::getAccentColour());
    drawPeak (noiseSnapshot_.hissPeakBin,     ChugLookAndFeel::getAudioTraceColour());

    // Multiband thresholds: small horizontal ticks at the top of the
    // plot, one per threshold lane. We render only the count that was
    // actually written so we never go out of bounds.
    if (noiseSnapshot_.numMultibandThresholds > 0)
    {
        g.setColour (ChugLookAndFeel::getOutlineStrongColour().withAlpha (0.8f));
        const int top = plotArea.getY() + 4;
        for (int t = 0; t < noiseSnapshot_.numMultibandThresholds
                       && t < static_cast<int> (noiseSnapshot_.multibandThresholds.size()); ++t)
        {
            const float db = noiseSnapshot_.multibandThresholds[
                static_cast<std::size_t> (t)];
            const float y  = yForDb (db);
            g.drawLine (static_cast<float> (plotArea.getX()), y,
                        static_cast<float> (plotArea.getRight()), y, 1.0f);
        }
        (void) top; // top reserved for future per-tick labels
    }

    // Frequency-axis labels (Hz). Render a small set of ticks — exact
    // bin indices depend on sample rate, so we just show a few major
    // stops: 100 Hz, 1 kHz, 5 kHz, 10 kHz when they fall inside the
    // spectrum.
    if (noiseSnapshot_.sampleRate > 0.0)
    {
        const double fs = noiseSnapshot_.sampleRate;
        const double nyquist = fs * 0.5;
        struct Tick { double hz; const char* label; };
        const Tick ticks[] = {
            {  100.0, "100"  },
            { 1000.0, "1k"   },
            { 5000.0, "5k"   },
            {10000.0, "10k"  }
        };

        g.setColour (ChugLookAndFeel::getMutedTextColour());
        g.setFont (juce::FontOptions (9.0f));
        for (const auto& t : ticks)
        {
            if (t.hz > nyquist) continue;
            const float x = plotArea.getX()
                          + static_cast<float> (t.hz / nyquist)
                          * static_cast<float> (plotArea.getWidth());
            g.drawLine (x, static_cast<float> (plotArea.getBottom() - 4),
                        x, static_cast<float> (plotArea.getBottom()), 1.0f);
            g.drawText (t.label,
                        juce::Rectangle<int> (static_cast<int> (x) - 16,
                                              plotArea.getBottom() - 14, 32, 12),
                        juce::Justification::centred, false);
        }
    }

    // Feature flags strip.
    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (9.5f));

    auto flagText = [] (bool on, const juce::String& name)
    {
        return (on ? juce::String::fromUTF8 ("\xE2\x97\x8F ")
                   : juce::String::fromUTF8 ("\xE2\x97\x8B ")) + name;
    };

    auto drawFlag = [&] (int idx, const juce::String& text, juce::Colour c)
    {
        const int total = 5;
        const int w = flagArea.getWidth() / total;
        auto r = juce::Rectangle<int> (flagArea.getX() + idx * w, flagArea.getY(),
                                       w, flagArea.getHeight());
        g.setColour (c);
        g.drawText (text, r, juce::Justification::centredLeft, true);
    };

    drawFlag (0, flagText (noiseSnapshot_.humFiftyHz, "50 Hz"),
              ChugLookAndFeel::getWarningColour());
    drawFlag (1, flagText (noiseSnapshot_.humSixtyHz, "60 Hz"),
              ChugLookAndFeel::getWarningColour());
    drawFlag (2, flagText (noiseSnapshot_.humHarmonics, "Harmonics"),
              ChugLookAndFeel::getAccentColour());
    drawFlag (3, flagText (noiseSnapshot_.hfHiss, "HF hiss"),
              ChugLookAndFeel::getAudioTraceColour());
    drawFlag (4, flagText (noiseSnapshot_.intermittentBursts, "Bursts"),
              ChugLookAndFeel::getWarningColour());
}

void DiagnosePage::paintAttackReadouts (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;

    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    auto titleArea = area.removeFromTop (16);
    g.drawText ("Attack integrity", titleArea, juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (13.0f));
    auto write = [&] (int row, const juce::String& left, const juce::String& right)
    {
        const int lineH = std::max (14, area.getHeight() / 4);
        auto r = juce::Rectangle<int> (area.getX(),
                                       area.getY() + row * lineH,
                                       area.getWidth(), lineH).reduced (2, 0);
        const int halfW = r.getWidth() / 2;
        g.drawText (left,  juce::Rectangle<int> (r.getX(), r.getY(), halfW, r.getHeight()),
                    juce::Justification::centredLeft, true);
        g.drawText (right, juce::Rectangle<int> (r.getX() + halfW, r.getY(),
                                                 r.getWidth() - halfW, r.getHeight()),
                    juce::Justification::centredRight, true);
    };

#if CHUGGATE_HAS_ATTACK_INTEGRITY
    if (attackAnalyzer_ == nullptr)
    {
        g.setColour (ChugLookAndFeel::getMutedTextColour());
        g.drawText ("Attack analyzer not wired",
                    juce::Rectangle<int> (area.getX(), area.getY() + area.getHeight() / 2 - 8,
                                          area.getWidth(), 16),
                    juce::Justification::centred, true);
        return;
    }

    write (0, "A_r:",       formatPercent (latestAR_));
    write (1, "Preserved:", formatCount (preservedCount_));
    write (2, "Partial:",   formatCount (partialCount_));
    write (3, "Missed:",    formatCount (missedCount_));
#else
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.drawText ("Attack analyzer header not present",
                juce::Rectangle<int> (area.getX(), area.getY() + area.getHeight() / 2 - 8,
                                      area.getWidth(), 16),
                juce::Justification::centred, true);
#endif
}

void DiagnosePage::paintTailReadouts (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;

    g.setColour (ChugLookAndFeel::getTextColour());
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    auto titleArea = area.removeFromTop (16);
    g.drawText ("Tail integrity", titleArea, juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (13.0f));
    auto write = [&] (int row, const juce::String& left, const juce::String& right)
    {
        const int lineH = std::max (14, area.getHeight() / 2);
        auto r = juce::Rectangle<int> (area.getX(),
                                       area.getY() + row * lineH,
                                       area.getWidth(), lineH).reduced (2, 0);
        const int halfW = r.getWidth() / 2;
        g.drawText (left,  juce::Rectangle<int> (r.getX(), r.getY(), halfW, r.getHeight()),
                    juce::Justification::centredLeft, true);
        g.drawText (right, juce::Rectangle<int> (r.getX() + halfW, r.getY(),
                                                 r.getWidth() - halfW, r.getHeight()),
                    juce::Justification::centredRight, true);
    };

#if CHUGGATE_HAS_TAIL_INTEGRITY
    if (tailAnalyzer_ == nullptr)
    {
        g.setColour (ChugLookAndFeel::getMutedTextColour());
        g.drawText ("Tail analyzer not wired",
                    juce::Rectangle<int> (area.getX(), area.getY() + area.getHeight() / 2 - 8,
                                          area.getWidth(), 16),
                    juce::Justification::centred, true);
        return;
    }

    write (0, "Choke score:", formatPercent (latestChokeScore_));
    write (1, "Choked:",      formatCount (chokeCount_));
#else
    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.drawText ("Tail analyzer header not present",
                juce::Rectangle<int> (area.getX(), area.getY() + area.getHeight() / 2 - 8,
                                      area.getWidth(), 16),
                juce::Justification::centred, true);
#endif
}

//==============================================================================
// Resized.
//==============================================================================
void DiagnosePage::resized()
{
    auto area = getLocalBounds();
    if (area.isEmpty()) return;

    area.removeFromTop (16); // page title

    const int totalH = area.getHeight();
    const int gaps   = kRowGap * 3;

    const int noiseH     = std::max (kNoiseMinH,
                                     static_cast<int> (totalH * kNoiseWeight));
    const int auditionH  = std::max (kAuditionMinH,
                                     static_cast<int> (totalH * kAuditionWeight));
    const int integrityH = std::max (kIntegrityMinH,
                                     static_cast<int> (totalH * kIntegrityWeight));
    const int modeH      = std::max (kModeMinH,
                                     totalH - noiseH - auditionH - integrityH - gaps);

    layoutNoiseSection    (area.removeFromTop (noiseH));     area.removeFromTop (kRowGap);
    layoutAuditionRow     (area.removeFromTop (auditionH));  area.removeFromTop (kRowGap);
    layoutIntegrityRow    (area.removeFromTop (integrityH)); area.removeFromTop (kRowGap);
    layoutAnalyzerModeRow (area.removeFromTop (modeH));
}

//==============================================================================
// Layout helpers.
//==============================================================================
void DiagnosePage::layoutNoiseSection (juce::Rectangle<int> area)
{
    if (noiseGroup_.panel) noiseGroup_.panel->setBounds (area);
}

void DiagnosePage::layoutAuditionRow (juce::Rectangle<int> area)
{
    if (auditionGroup_.panel) auditionGroup_.panel->setBounds (area);

    // Inner area: two combos side-by-side, each with its label above.
    auto inner = area.reduced (kGroupInnerInset);
    inner.removeFromTop (kGroupTitleHeight);
    if (inner.isEmpty()) return;

    const int halfW = inner.getWidth() / 2;
    const int labelH = 14;

    auto left  = juce::Rectangle<int> (inner.getX(), inner.getY(),
                                        halfW, inner.getHeight());
    auto right = juce::Rectangle<int> (inner.getX() + halfW, inner.getY(),
                                       inner.getWidth() - halfW, inner.getHeight());

    if (rejectedModeLabel_)
        rejectedModeLabel_->setBounds (left.removeFromTop (labelH));
    if (rejectedModeCombo_)
        rejectedModeCombo_->setBounds (left.withHeight (juce::jmax (28, left.getHeight())));

    if (stateFilterLabel_)
        stateFilterLabel_->setBounds (right.removeFromTop (labelH));
    if (stateFilterCombo_)
        stateFilterCombo_->setBounds (right.withHeight (juce::jmax (28, right.getHeight())));
}

void DiagnosePage::layoutIntegrityRow (juce::Rectangle<int> area)
{
    if (integrityGroup_.panel) integrityGroup_.panel->setBounds (area);
}

void DiagnosePage::layoutAnalyzerModeRow (juce::Rectangle<int> area)
{
    if (modeGroup_.panel) modeGroup_.panel->setBounds (area);

    // Inner area: label + combo on the right half (centered vertically).
    auto inner = area.reduced (kGroupInnerInset,
                               kGroupTitleHeight + kGroupInnerInset);
    if (inner.isEmpty()) return;

    const int comboW = std::min (220, inner.getWidth() / 2);
    const int labelH = 14;

    auto right = inner.removeFromRight (comboW);
    if (analyzerModeLabel_)
        analyzerModeLabel_->setBounds (right.removeFromTop (labelH));
    if (analyzerModeCombo_)
        analyzerModeCombo_->setBounds (right);
}

} // namespace ChugGate::GUI
