// DiagnosePage.h
//
// Diagnose view (CHUGGATE_SPEC.md §21.4 / §20.3 / §20.5 / §20.6 / §19).
//
// A juce::Component that hosts the diagnostic visualizations and
// controls the spec calls out for the Diagnose tab:
//
//   - Noise Fingerprint (§20.3): the time-averaged learned noise
//     spectrum rendered as median floor + upper percentile curves with
//     optional detector filter response overlay and peak markers for
//     mains hum, harmonic peaks, and HF hiss.
//   - Rejected-Signal Audition (§20.5): routing mode combo box plus
//     state-trigger combo box so the user can hear what is being removed
//     (e.g. only what is removed during Hold / Sustain / Release /
//     Closed states).
//   - Attack Integrity Analyzer (Appendix A.4): latest A_r preservation
//     ratio plus preserved / partial / missed attack counts.
//   - Tail Integrity Analyzer (Appendix A.4): choke score plus choked
//     tail count.
//   - Analyzer Mode (§20.6): an Off / Live / Studio / Diagnose selector
//     wired through an editor-supplied std::function callback so the
//     page never touches the processor internals directly.
//
// All paint work happens on the message thread via local snapshots
// refreshed from Analysis::copy*() APIs. The audio thread is never
// accessed from this file. The component is null-safe: a null Noise
// Fingerprint or Analyzer object paints an empty-state placeholder
// rather than crashing.
//
// Wiring model:
//   - setNoiseFingerprint(pointer) — non-owning; nullptr paints empty
//   - setAttackIntegrityAnalyzer(pointer)
//   - setTailIntegrityAnalyzer(pointer)
//   - setApvts(reference) — installs APVTS so the audition combo boxes
//     can be APVTS-attached where parameter IDs exist
//   - setAnalyzerModeHandler(fn) — Off / Live / Studio / Diagnose changes
//     fire this callback with the new mode index (0..3)

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "../Analysis/NoiseFingerprint.h"

namespace ChugGate::Analysis {
// Forward declarations so we don't have to include the analyzers'
// (not-yet-existing) headers. DiagnosePage only invokes the public
// getters (getAttackPreservationRatio / getPreservedCount /
// getPartialCount / getMissedCount and getChokeScore / getChokeCount).
// The implementations resolve these through a thin interface declared
// in the .cpp; when the analyzers are wired in by Phase H, the
// corresponding concrete classes can be cast/forwarded to DiagnosePage
// via setters that accept the abstract interfaces.
class AttackIntegrityAnalyzer;
class TailIntegrityAnalyzer;
} // namespace ChugGate::Analysis

namespace ChugGate::GUI {

//==============================================================================
/**
 *  DiagnosePage — diagnostic visualizations + analyzer mode controls.
 *
 *  Sections:
 *    - Noise Fingerprint: median floor + upper percentile + detector
 *      filter response overlay + peak markers + feature flags.
 *    - Rejected-Signal Audition: mode combo + state-filter combo.
 *    - Attack Integrity: latest A_r + preserved / partial / missed counts.
 *    - Tail Integrity: choke score + choked-tail count.
 *    - Analyzer Mode: Off / Live / Studio / Diagnose combo wired through
 *      an editor-supplied std::function callback.
 *
 *  The page is null-safe. Every section renders an empty-state hint when
 *  its backing analysis object is null or produces no data yet.
 *
 *  Threading: construction, painting, resizing and timer-driven
 *  snapshot refresh all happen on the message thread. paint() reads
 *  only from member-owned snapshots.
 */
class DiagnosePage : public juce::Component,
                     private juce::Timer
{
public:
    //==========================================================================
    // Editor-supplied callback signatures. Empty std::function is a
    // no-op so the page remains usable during early wiring.
    //==========================================================================
    // Analyzer mode change. Argument is the new mode index in the same
    // order the combo displays: 0=Off, 1=Live, 2=Studio, 3=Diagnose.
    using AnalyzerModeHandler = std::function<void (int)>;

    //==========================================================================
    DiagnosePage();
    ~DiagnosePage() override;

    //==========================================================================
    // Wiring.
    //==========================================================================
    // Inject the analysis-side model (non-owning). Pass nullptr to
    // disconnect; subsequent paints show empty-state placeholders. Safe
    // to call from the message thread.
    void setNoiseFingerprint (ChugGate::Analysis::NoiseFingerprint* noise) noexcept;
    void setAttackIntegrityAnalyzer (ChugGate::Analysis::AttackIntegrityAnalyzer* ai) noexcept;
    void setTailIntegrityAnalyzer   (ChugGate::Analysis::TailIntegrityAnalyzer*   ti) noexcept;

    // Install the APVTS used by the rejected-signal audition combo boxes.
    // Attachments are constructed lazily inside setApvts() and only
    // when the parameter IDs exist.
    void setApvts (juce::AudioProcessorValueTreeState& apvts) noexcept;

    // Register an analyzer-mode callback. Invoked on the message thread
    // when the user changes the Off / Live / Studio / Diagnose selector.
    void setAnalyzerModeHandler (AnalyzerModeHandler handler);

    // Force a message-thread repaint (e.g. after wiring changes).
    void refreshNow() noexcept;

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // juce::Timer — drives the message-thread snapshot refresh.
    void timerCallback() override;

    //==========================================================================
    // Snapshot data the paint path reads from. Allocated in the ctor
    // and resized on the message thread. Snapshot content is updated
    // only on the message thread inside refreshSnapshots().
    struct NoiseSnapshot
    {
        std::vector<float> medianFloor;
        std::vector<float> upperPercentile;
        std::vector<float> detectorFilterResponse;
        std::vector<float> multibandThresholds;
        int  numBins                  = 0;
        int  numMultibandThresholds   = 0;
        double sampleRate             = 0.0;

        // Feature flags.
        bool humFiftyHz         = false;
        bool humSixtyHz         = false;
        bool humHarmonics       = false;
        bool hfHiss             = false;
        bool intermittentBursts = false;

        // Peak bin markers (-1 = none).
        int  humPeakBin         = -1;
        int  harmonicPeakBin    = -1;
        int  hissPeakBin        = -1;

        bool hasData = false;
    };

    void refreshNoiseSnapshot() noexcept;
    void refreshAnalyzerReadouts() noexcept;
    void ensureNoiseSnapshotCapacity (int bins) noexcept;

    //==========================================================================
    // Painting helpers — each renders into the supplied rectangle using
    // only the local snapshot and the ChugLookAndFeel palette.
    void paintGroupBackground (juce::Graphics& g,
                               juce::Component* panel,
                               const juce::String& title);
    void paintNoiseSection      (juce::Graphics& g, juce::Rectangle<int> area);
    void paintAttackReadouts    (juce::Graphics& g, juce::Rectangle<int> area);
    void paintTailReadouts      (juce::Graphics& g, juce::Rectangle<int> area);
    void paintEmptySection      (juce::Graphics& g, juce::Rectangle<int> area,
                                 const juce::String& message);

    //==========================================================================
    // Layout helpers — assign rectangles to each visible region.
    void layoutNoiseSection   (juce::Rectangle<int> area);
    void layoutAuditionRow    (juce::Rectangle<int> area);
    void layoutIntegrityRow   (juce::Rectangle<int> area);
    void layoutAnalyzerModeRow(juce::Rectangle<int> area);

    //==========================================================================
    // Section panels (groups). The bold title text is rendered by
    // paintGroupBackground() directly (via Graphics::drawText) so we
    // don't need a juce::Label child per group — keeps the component
    // graph minimal.
    struct GroupPanel
    {
        std::unique_ptr<juce::Component> panel;
    };

    GroupPanel noiseGroup_;
    GroupPanel auditionGroup_;
    GroupPanel integrityGroup_;
    GroupPanel modeGroup_;

    //==========================================================================
    // Controls.
    std::unique_ptr<juce::ComboBox> rejectedModeCombo_;
    std::unique_ptr<juce::Label>    rejectedModeLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        rejectedModeAttachment_;

    std::unique_ptr<juce::ComboBox> stateFilterCombo_;
    std::unique_ptr<juce::Label>    stateFilterLabel_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        stateFilterAttachment_;

    std::unique_ptr<juce::ComboBox> analyzerModeCombo_;
    std::unique_ptr<juce::Label>    analyzerModeLabel_;

    //==========================================================================
    // Editor-supplied analyzer mode handler.
    AnalyzerModeHandler analyzerModeHandler_;

    //==========================================================================
    // Analysis-side data (non-owning). nullptr produces empty-state paint.
    ChugGate::Analysis::NoiseFingerprint*        noiseFingerprint_ = nullptr;
    ChugGate::Analysis::AttackIntegrityAnalyzer* attackAnalyzer_   = nullptr;
    ChugGate::Analysis::TailIntegrityAnalyzer*   tailAnalyzer_     = nullptr;

    // APVTS reference (non-owning). nullptr leaves attachments unbound.
    juce::AudioProcessorValueTreeState* apvts_ = nullptr;

    // Owned snapshot.
    NoiseSnapshot noiseSnapshot_;

    // Integrity readouts (atomics refreshed on the message thread via
    // direct getter calls; no mutex needed because both producer
    // (analyzer getter) and consumer (DiagnosePage) are message-thread
    // only).
    float latestAR_         = 0.0f;
    std::uint64_t preservedCount_ = 0;
    std::uint64_t partialCount_   = 0;
    std::uint64_t missedCount_    = 0;
    float latestChokeScore_ = 0.0f;
    std::uint64_t chokeCount_     = 0;

    // Refresh cadence (ms).
    int refreshMs_ = 33;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiagnosePage)
};

} // namespace ChugGate::GUI