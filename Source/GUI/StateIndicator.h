// StateIndicator.h
//
// Compact state readout (CHUGGATE_SPEC.md §20.4) — "more than a lamp".
//
//   OPEN — Pick transient
//   SUSTAIN — Harmonic tail (18 ms remaining)
//   RELEASE — Hiss band closing
//   CLOSED — Below learned noise margin
//
// The component shows:
//
//   - current gate state (name + color-blind-safe symbol + 3-letter abbrev)
//   - duration spent in the current state (ms)
//   - current output gain (dB)
//   - a short reason string ("Pick transient", "Harmonic tail", ...)
//   - active protection flags (Attack Guard, Feedback Friend, …) rendered
//     as small text labels so the user can tell which safety nets are on
//
// All updates flow through a small StateSnapshot POD / setter that the
// editor pushes from the message thread (typically from its per-block
// timer). The component itself does not poll the processor; the editor
// decides the cadence.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <string>

#include "ChugLookAndFeel.h"

namespace ChugGate::GUI {

//==============================================================================
/**
 *  StateSnapshot — plain data carried from the editor's message-thread
 *  push to the indicator. The indicator is fully data-driven: setStateSnapshot
 *  is the only API the editor needs to call.
 *
 *  Fields:
 *    - state           : current GateState (drives colour + symbol)
 *    - durationMs      : time spent in the current state
 *    - gainDb          : current output gain in dB (full-scale reference)
 *    - reasonText      : short human-readable reason ("Pick transient" …)
 *    - protectionFlags : bit-set of active protections (Attack Guard,
 *                        Feedback Friend, etc.). The indicator renders the
 *                        names of the flags that are set, in declaration
 *                        order, separated by middots.
 */
struct StateSnapshot
{
    GateState state = GateState::Closed;
    double    durationMs = 0.0;
    float     gainDb = -80.0f;
    juce::String reasonText;
    std::uint32_t protectionFlags = 0;
};

//==============================================================================
/**
 *  Named protection flags. Bit positions are part of the public API so the
 *  editor can set them with a clear `snapshot.protectionFlags |= (1u << kAttackGuard);`
 *  pattern without needing to know the indicator's header.
 */
namespace StateProtections
{
    constexpr std::uint32_t AttackGuard        = 1u << 0;
    constexpr std::uint32_t FeedbackFriend     = 1u << 1;
    constexpr std::uint32_t HumShield          = 1u << 2;
    constexpr std::uint32_t LegatoReopen       = 1u << 3;
    constexpr std::uint32_t HissCurtain        = 1u << 4;
    constexpr std::uint32_t Lookahead          = 1u << 5;
    constexpr std::uint32_t SustainExtender    = 1u << 6;
    constexpr std::uint32_t MultibandDepth     = 1u << 7;
} // namespace StateProtections

//==============================================================================
/**
 *  StateIndicator — compact badge / readout.
 *
 *  Layout (top→bottom):
 *      [symbol] STATE — reason (duration)
 *      gain: -12.3 dB   •   Attack Guard • Feedback Friend
 *
 *  The component pairs hue with a distinct shape symbol + 3-letter
 *  abbreviation so state identification never depends on colour alone
 *  (spec §21.2).
 */
class StateIndicator : public juce::Component
{
public:
    //==========================================================================
    StateIndicator();
    ~StateIndicator() override;

    //==========================================================================
    // Message-thread snapshot setter. Pushes the latest indicator data
    // and triggers a repaint. Safe to call from the editor's per-block
    // timer.
    void setStateSnapshot (const StateSnapshot& snap) noexcept;
    [[nodiscard]] const StateSnapshot& getStateSnapshot() const noexcept { return snapshot_; }

    // Convenience setters used by callers that don't want to build a full
    // StateSnapshot. All of them internally rebuild the snapshot and call
    // setStateSnapshot().
    void setState (GateState state) noexcept;
    void setReason (const juce::String& reason) noexcept;
    void setDurationMs (double durationMs) noexcept;
    void setGainDb (float gainDb) noexcept;
    void setProtectionFlag (std::uint32_t flag, bool on) noexcept;

    //==========================================================================
    // Component overrides
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    // Build the protection-flag string ("Attack Guard • Feedback Friend" …)
    // from the snapshot's bit set. Always returns text — never colour — so
    // the user can tell which protections are active even with a colour
    // impairment.
    [[nodiscard]] juce::String buildProtectionLabel() const noexcept;

    //==========================================================================
    // Returns a short human label for a single protection bit, or empty
    // when the bit is not a known protection.
    [[nodiscard]] static juce::String protectionName (std::uint32_t bit) noexcept;

    StateSnapshot snapshot_;
};

} // namespace ChugGate::GUI
