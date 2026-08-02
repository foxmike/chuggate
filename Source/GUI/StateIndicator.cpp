// StateIndicator.cpp
//
// See StateIndicator.h. Paints the compact §20.4 readout. Pure message-
// thread code; the only state is the snapshot pushed in from the editor.

#include "StateIndicator.h"

#include <cmath>

namespace ChugGate::GUI {

namespace
{
    constexpr int kOuterInset = 6;
    constexpr int kBadgeSize  = 36;       // square badge in the top-left
    constexpr int kBadgeTextGap = 10;     // gap between badge and text column
    constexpr int kRowGap = 2;            // gap between header and meta lines

    // Format a duration in milliseconds as a compact human string.
    //   <1000  -> "NNN ms"
    //   <10000 -> "N.NN s"
    //   else   -> "NN s"
    juce::String formatDuration (double ms)
    {
        if (! (ms >= 0.0) || ! (ms < 1.0e9))
            return juce::String();
        if (ms < 1000.0)
            return juce::String (static_cast<int> (std::lround (ms))) + " ms";
        if (ms < 10000.0)
        {
            juce::String s;
            s << juce::String (ms / 1000.0, 2);
            return s + " s";
        }
        return juce::String (static_cast<int> (std::lround (ms / 1000.0))) + " s";
    }

    // Format a gain value in dBFS for the meta line.
    juce::String formatGain (float db)
    {
        if (std::isnan (db)) return "--";
        if (db <= -80.0f) return "-inf dB";
        juce::String s;
        s << juce::String (static_cast<double> (db), 1);
        return s + " dB";
    }
} // namespace

//==============================================================================
// Construction / destruction.
//==============================================================================
StateIndicator::StateIndicator()
{
    setOpaque (false);
    setWantsKeyboardFocus (false);
}

StateIndicator::~StateIndicator() = default;

//==============================================================================
// Snapshot / setters.
//==============================================================================
void StateIndicator::setStateSnapshot (const StateSnapshot& snap) noexcept
{
    snapshot_ = snap;
    repaint();
}

void StateIndicator::setState (GateState state) noexcept
{
    if (snapshot_.state == state) return;
    snapshot_.state = state;
    repaint();
}

void StateIndicator::setReason (const juce::String& reason) noexcept
{
    if (snapshot_.reasonText == reason) return;
    snapshot_.reasonText = reason;
    repaint();
}

void StateIndicator::setDurationMs (double durationMs) noexcept
{
    if (snapshot_.durationMs == durationMs) return;
    snapshot_.durationMs = durationMs;
    repaint();
}

void StateIndicator::setGainDb (float gainDb) noexcept
{
    // Avoid repaints on float quantisation noise — only repaint when the
    // displayed value would change. We compare at 0.05 dB precision which
    // is finer than the format string.
    if (std::fabs (snapshot_.gainDb - gainDb) < 0.05f) return;
    snapshot_.gainDb = gainDb;
    repaint();
}

void StateIndicator::setProtectionFlag (std::uint32_t flag, bool on) noexcept
{
    const bool was = (snapshot_.protectionFlags & flag) != 0u;
    if (was == on) return;
    if (on) snapshot_.protectionFlags |= flag;
    else     snapshot_.protectionFlags &= ~flag;
    repaint();
}

//==============================================================================
// Component overrides.
//==============================================================================
void StateIndicator::resized()
{
    // The component is purely text/badge driven. We don't keep any
    // sub-component geometry, so this is a no-op; paint() recomputes the
    // layout from getLocalBounds() each frame.
}

void StateIndicator::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    // Backdrop — translucent panel so the badge stays legible against
    // any host background.
    g.setColour (ChugLookAndFeel::getPanelColour().withAlpha (0.85f));
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    g.setColour (ChugLookAndFeel::getOutlineColour());
    g.drawRoundedRectangle (bounds.toFloat(), 4.0f, 1.0f);

    // Layout:
    //   [ badge ]  STATE — reason (duration)
    //              gain: -12.3 dB  •  Attack Guard • Feedback Friend
    const int badgeSize = std::min (kBadgeSize,
                                    std::min (bounds.getHeight() - 2 * kOuterInset,
                                              bounds.getWidth()  - 2 * kOuterInset));
    const juce::Rectangle<int> badgeRect (bounds.getX() + kOuterInset,
                                          bounds.getY() + kOuterInset,
                                          std::max (0, badgeSize),
                                          std::max (0, badgeSize));
    const int textX = badgeRect.getRight() + kBadgeTextGap;
    const int textRight = bounds.getRight() - kOuterInset;
    const int textWidth = std::max (0, textRight - textX);

    // 1) Badge — colour + symbol, drawn via the shared LookAndFeel helper
    //    so colour-blind safety comes from the same vocabulary used by
    //    every other gate-state surface.
    if (badgeSize > 0)
    {
        if (auto* lf = dynamic_cast<ChugLookAndFeel*> (&getLookAndFeel()))
        {
            lf->drawStateBadge (g, badgeRect.toFloat(),
                                snapshot_.state, /*active=*/ true);
        }
        else
        {
            // Fallback: draw the badge manually using the static palette
            // accessors when the LookAndFeel is not (yet) the project one.
            const auto stateColour = ChugLookAndFeel::getStateColour (snapshot_.state);
            g.setColour (stateColour);
            g.fillRect (badgeRect.toFloat());
            g.setColour (ChugLookAndFeel::getOutlineStrongColour());
            g.drawRect (badgeRect, 1);
            g.setColour (ChugLookAndFeel::getTextColour());
            g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (ChugLookAndFeel::getStateSymbol (snapshot_.state),
                        badgeRect.toFloat(),
                        juce::Justification::centred, false);
        }
    }

    if (textWidth <= 0)
        return;

    // 2) Header line: "<ABBREV> <STATE> — <reason> (duration)"
    const juce::String abbrev = ChugLookAndFeel::getStateAbbrev (snapshot_.state);
    const juce::String stateName = ChugLookAndFeel::getStateName (snapshot_.state);
    const juce::String duration  = formatDuration (snapshot_.durationMs);

    juce::String header;
    header << abbrev << "  " << stateName;
    if (snapshot_.reasonText.isNotEmpty())
    {
        header << "  --  " << snapshot_.reasonText;
    }
    if (duration.isNotEmpty())
    {
        header << "  (" << duration << ")";
    }

    // The header line takes the upper half; the meta line takes the lower
    // half. We size the line heights from the available height.
    const int totalH = bounds.getHeight() - 2 * kOuterInset;
    const int headerH = std::max (16, totalH / 2);
    const int metaH   = std::max (14, totalH - headerH - kRowGap);

    const juce::Rectangle<int> headerRect (textX,
                                           bounds.getY() + kOuterInset,
                                           textWidth,
                                           std::min (headerH, badgeSize));
    const juce::Rectangle<int> metaRect (textX,
                                         headerRect.getBottom() + kRowGap,
                                         textWidth,
                                         metaH);

    g.setColour (ChugLookAndFeel::getStateColour (snapshot_.state));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawFittedText (header, headerRect,
                      juce::Justification::centredLeft, 1, 1.0f);

    // 3) Meta line: "gain: -12.3 dB  •  Attack Guard • Feedback Friend"
    juce::String meta;
    meta << "gain: " << formatGain (snapshot_.gainDb);
    const auto protLabel = buildProtectionLabel();
    if (protLabel.isNotEmpty())
    {
        meta << "   *   " << protLabel;
    }

    g.setColour (ChugLookAndFeel::getMutedTextColour());
    g.setFont (juce::FontOptions (11.0f));
    g.drawFittedText (meta, metaRect,
                      juce::Justification::centredLeft, 1, 1.0f);
}

//==============================================================================
// Helpers.
//==============================================================================
juce::String StateIndicator::protectionName (std::uint32_t bit) noexcept
{
    switch (bit)
    {
        case StateProtections::AttackGuard:     return "Attack Guard";
        case StateProtections::FeedbackFriend:  return "Feedback Friend";
        case StateProtections::HumShield:       return "Hum Shield";
        case StateProtections::LegatoReopen:    return "Legato Reopen";
        case StateProtections::HissCurtain:     return "Hiss Curtain";
        case StateProtections::Lookahead:       return "Lookahead";
        case StateProtections::SustainExtender: return "Sustain Extender";
        case StateProtections::MultibandDepth:  return "Multiband Depth";
        default:                                return {};
    }
}

juce::String StateIndicator::buildProtectionLabel() const noexcept
{
    juce::String out;
    bool first = true;
    // Walk the bit positions we know about so the order is stable.
    constexpr std::uint32_t knownBits[] = {
        StateProtections::AttackGuard,
        StateProtections::FeedbackFriend,
        StateProtections::HumShield,
        StateProtections::LegatoReopen,
        StateProtections::HissCurtain,
        StateProtections::Lookahead,
        StateProtections::SustainExtender,
        StateProtections::MultibandDepth,
    };
    for (const auto bit : knownBits)
    {
        if ((snapshot_.protectionFlags & bit) == 0u)
            continue;
        const auto name = protectionName (bit);
        if (name.isEmpty())
            continue;
        if (! first) out << " * ";
        out << name;
        first = false;
    }
    return out;
}

} // namespace ChugGate::GUI
