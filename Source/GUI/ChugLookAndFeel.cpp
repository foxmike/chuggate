// ChugLookAndFeel.cpp
//
// Implementation of ChugGate::GUI::ChugLookAndFeel.
// See ChugLookAndFeel.h for the public surface and CHUGGATE_SPEC.md §21.2
// for the design rationale.

#include "ChugLookAndFeel.h"

#include "../DSP/PerformanceStateMachine.h"

namespace ChugGate::GUI
{

namespace
{
    //==========================================================================
    // Suite palette constants. Centralised here so every override, accessor
    // and helper resolves to the same hex literals and we never drift across
    // the LookAndFeel implementation.
    //==========================================================================
    constexpr std::uint32_t kBackgroundColour      = 0xff0c0f15; // deep neutral base
    constexpr std::uint32_t kPanelColour           = 0xff151a22; // base panel
    constexpr std::uint32_t kRaisedPanelColour     = 0xff1c2230; // hover / focus panel
    constexpr std::uint32_t kSunkenPanelColour     = 0xff0a0d12; // recessed area / track
    constexpr std::uint32_t kOutlineColour         = 0xff3a4453; // 1 px separator
    constexpr std::uint32_t kOutlineStrongColour   = 0xff5a6878; // focus ring / strong edge
    constexpr std::uint32_t kTextColour            = 0xffe6ebf2; // primary text
    constexpr std::uint32_t kMutedTextColour       = 0xff8a96a8; // secondary text
    constexpr std::uint32_t kAccentColour          = 0xff00d4e5; // signature cyan
    constexpr std::uint32_t kAccentDimColour       = 0xff1e5d6a; // dim accent (track fills)
    constexpr std::uint32_t kWarningColour         = 0xffe5b13a; // restraint amber

    constexpr std::uint32_t kKeyTraceColour        = 0xff6cd4ff; // conditioned key envelope
    constexpr std::uint32_t kAudioTraceColour      = 0xfff3f6fa; // broadband audio trace
    constexpr std::uint32_t kThresholdColour       = 0xff00d4e5; // open threshold line
    constexpr std::uint32_t kNoiseFloorColour      = 0xffb6a89a; // learned noise floor
    constexpr std::uint32_t kGainTraceColour       = 0xff7fd07f; // gain reduction overlay
    constexpr std::uint32_t kAttenuationColour     = 0xcc1a222d; // attenuation shade

    //--------------------------------------------------------------------------
    // Gate-state accent palette. Hues are chosen so the eight plus feedback
    // colours are well-separated in luminance and span more than just the
    // red-green axis. Pairing with distinct shape symbols (paintStateSymbol
    // below) is what makes the badges color-blind safe per §21.2.
    //--------------------------------------------------------------------------
    struct StateColourEntry
    {
        GateState state;
        std::uint32_t active;
        std::uint32_t inactive;
    };

    constexpr StateColourEntry kStateColours[] = {
        { GateState::Closed,   0xff4a5260, 0xff2c313a }, // resting / muted
        { GateState::Primed,   0xffd3c054, 0xff5d5430 }, // pre-attack warm
        { GateState::Attack,   0xff00d4e5, 0xff1e5d6a }, // fast rising
        { GateState::Open,     0xff7fd07f, 0xff365d36 }, // confidently active
        { GateState::Hold,     0xff6cd4ff, 0xff2c5d6a }, // holding minimum
        { GateState::Sustain,  0xffe5b13a, 0xff5d4d2a }, // musical decay
        { GateState::Release,  0xffe58a4a, 0xff5d3a22 }, // fading
        { GateState::Reopen,   0xffb266ff, 0xff4d2c66 }, // reactivation
        { GateState::Feedback, 0xffe64a4a, 0xff5d2222 }  // protection alert
    };
    static_assert (juce::numElementsInArray (kStateColours) == 9,
                   "State colour table must cover every GateState value");

    juce::Colour lookupStateColour (GateState state, bool active) noexcept
    {
        const auto idx = static_cast<std::size_t> (state);
        if (idx >= juce::numElementsInArray (kStateColours))
            return active ? juce::Colour (kAccentColour) : juce::Colour (kAccentDimColour);
        const auto& entry = kStateColours[idx];
        return juce::Colour (active ? entry.active : entry.inactive);
    }
} // anonymous namespace

//==============================================================================
// Constructor.
//==============================================================================
ChugLookAndFeel::ChugLookAndFeel()
{
    // The base LookAndFeel_V4 colour scheme drives a wide range of default
    // colours (text editor, label, scrollbar thumb, popup menu, combo box,
    // caret, property component ...). We hand it a dark, cool, neutral scheme
    // matching the suite identity; individual overrides below refine the
    // mechanical details on top.
    setColourScheme (juce::LookAndFeel_V4::ColourScheme {
        juce::Colour (kBackgroundColour).getARGB(), // windowBackground
        juce::Colour (kPanelColour).getARGB(),      // widgetBackground
        juce::Colour (kPanelColour).getARGB(),      // menuBackground
        juce::Colour (kOutlineColour).getARGB(),    // outline
        juce::Colour (kTextColour).getARGB(),       // defaultText
        juce::Colour (kAccentDimColour).getARGB(),  // defaultFill
        juce::Colour (kBackgroundColour).getARGB(), // highlightedText
        juce::Colour (kAccentColour).getARGB(),     // highlightedFill
        juce::Colour (kTextColour).getARGB()        // menuText
    });

    // Slider-specific overrides — the colour scheme does not cover these.
    setColour (juce::Slider::backgroundColourId,          juce::Colour (kPanelColour));
    setColour (juce::Slider::thumbColourId,               juce::Colour (kAccentColour));
    setColour (juce::Slider::trackColourId,               juce::Colour (kSunkenPanelColour));
    setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (kAccentColour));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (kOutlineStrongColour));
    setColour (juce::Slider::textBoxTextColourId,         juce::Colour (kTextColour));
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colour (kSunkenPanelColour));
    setColour (juce::Slider::textBoxHighlightColourId,    juce::Colour (kAccentColour));
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colour (kOutlineColour));

    // Combo-box overlay tweaks.
    setColour (juce::ComboBox::arrowColourId, juce::Colour (kAccentColour));

    // Scrollbar — sharp mechanical thumb.
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (kOutlineStrongColour));
}

//==============================================================================
// Palette accessors.
//==============================================================================
juce::Colour ChugLookAndFeel::getBackgroundColour() noexcept     { return juce::Colour (kBackgroundColour); }
juce::Colour ChugLookAndFeel::getPanelColour() noexcept          { return juce::Colour (kPanelColour); }
juce::Colour ChugLookAndFeel::getRaisedPanelColour() noexcept    { return juce::Colour (kRaisedPanelColour); }
juce::Colour ChugLookAndFeel::getSunkenPanelColour() noexcept    { return juce::Colour (kSunkenPanelColour); }
juce::Colour ChugLookAndFeel::getOutlineColour() noexcept        { return juce::Colour (kOutlineColour); }
juce::Colour ChugLookAndFeel::getOutlineStrongColour() noexcept  { return juce::Colour (kOutlineStrongColour); }
juce::Colour ChugLookAndFeel::getTextColour() noexcept           { return juce::Colour (kTextColour); }
juce::Colour ChugLookAndFeel::getMutedTextColour() noexcept      { return juce::Colour (kMutedTextColour); }
juce::Colour ChugLookAndFeel::getAccentColour() noexcept         { return juce::Colour (kAccentColour); }
juce::Colour ChugLookAndFeel::getAccentDimColour() noexcept      { return juce::Colour (kAccentDimColour); }
juce::Colour ChugLookAndFeel::getWarningColour() noexcept        { return juce::Colour (kWarningColour); }

//==============================================================================
// Trace / threshold palette.
//==============================================================================
juce::Colour ChugLookAndFeel::getKeyTraceColour() noexcept       { return juce::Colour (kKeyTraceColour); }
juce::Colour ChugLookAndFeel::getAudioTraceColour() noexcept     { return juce::Colour (kAudioTraceColour); }
juce::Colour ChugLookAndFeel::getThresholdColour() noexcept      { return juce::Colour (kThresholdColour); }
juce::Colour ChugLookAndFeel::getNoiseFloorColour() noexcept     { return juce::Colour (kNoiseFloorColour); }
juce::Colour ChugLookAndFeel::getGainTraceColour() noexcept      { return juce::Colour (kGainTraceColour); }
juce::Colour ChugLookAndFeel::getAttenuationColour() noexcept    { return juce::Colour (kAttenuationColour); }

//==============================================================================
// Gate-state colour accessors.
//==============================================================================
juce::Colour ChugLookAndFeel::getStateColour (GateState state) noexcept
{
    return lookupStateColour (state, true);
}

juce::Colour ChugLookAndFeel::getStateColourInactive (GateState state) noexcept
{
    return lookupStateColour (state, false);
}

//==============================================================================
// Color-blind-safe state vocabulary.
//==============================================================================
juce::String ChugLookAndFeel::getStateSymbol (GateState state)
{
    // Single Unicode glyph per state. Each is visually distinct both in shape
    // and in stroke weight so state identity survives greyscale printing or
    // red/green color-blindness. The lamp/badge API pairs each glyph with
    // its accent colour for additional redundancy.
    switch (state)
    {
        case GateState::Closed:   return juce::String::fromUTF8 ("\xE2\x9A\xAB"); // ⚫ filled circle
        case GateState::Primed:   return juce::String::fromUTF8 ("\xE2\x96\xB2"); // ▲ triangle
        case GateState::Attack:   return juce::String::fromUTF8 ("\xE2\xAC\x86"); // ⇧ up arrow
        case GateState::Open:     return juce::String::fromUTF8 ("\xE2\x9A\xA1"); // ⚡ lightning
        case GateState::Hold:     return juce::String::fromUTF8 ("\xE2\x96\xA0"); // ■ square
        case GateState::Sustain:  return juce::String::fromUTF8 ("\xE2\x97\x86"); // ◆ diamond
        case GateState::Release:  return juce::String::fromUTF8 ("\xE2\xAC\x87"); // ⇩ down arrow
        case GateState::Reopen:   return juce::String::fromUTF8 ("\xE2\x9A\x99"); // ⚙ gear (cyclic)
        case GateState::Feedback: return juce::String::fromUTF8 ("\xE2\x9C\x95"); // ✕ cross
    }
    return juce::String ("?");
}

juce::String ChugLookAndFeel::getStateAbbrev (GateState state)
{
    switch (state)
    {
        case GateState::Closed:   return "CLS";
        case GateState::Primed:   return "PRM";
        case GateState::Attack:   return "ATK";
        case GateState::Open:     return "OPN";
        case GateState::Hold:     return "HLD";
        case GateState::Sustain:  return "SUS";
        case GateState::Release:  return "REL";
        case GateState::Reopen:   return "ROP";
        case GateState::Feedback: return "FBK";
    }
    return "---";
}

juce::String ChugLookAndFeel::getStateName (GateState state)
{
    switch (state)
    {
        case GateState::Closed:   return "Closed";
        case GateState::Primed:   return "Primed";
        case GateState::Attack:   return "Attack";
        case GateState::Open:     return "Open";
        case GateState::Hold:     return "Hold";
        case GateState::Sustain:  return "Sustain";
        case GateState::Release:  return "Release";
        case GateState::Reopen:   return "Reopen";
        case GateState::Feedback: return "Feedback";
    }
    return "Unknown";
}

//==============================================================================
// Engine state -> GateState conversion.
//==============================================================================
GateState ChugLookAndFeel::gateStateFromEngine (int engineStateIndex) noexcept
{
    using ES = ::ChugGate::DSP::PerformanceStateMachine::State;
    switch (static_cast<ES> (engineStateIndex))
    {
        case ES::Closed:  return GateState::Closed;
        case ES::Primed:  return GateState::Primed;
        case ES::Attack:  return GateState::Attack;
        case ES::Open:    return GateState::Open;
        case ES::Hold:    return GateState::Hold;
        case ES::Sustain: return GateState::Sustain;
        case ES::Release: return GateState::Release;
        case ES::Reopen:  return GateState::Reopen;
    }
    return GateState::Closed;
}

//==============================================================================
// Internal helpers.
//==============================================================================
juce::Colour ChugLookAndFeel::resolveAccentColour (bool highlighted, bool active) noexcept
{
    if (! active)
        return juce::Colour (kAccentDimColour);
    return highlighted ? juce::Colour (kAccentColour).brighter (0.15f)
                        : juce::Colour (kAccentColour);
}

void ChugLookAndFeel::drawSharpRect (juce::Graphics& g,
                                     juce::Rectangle<float> bounds,
                                     juce::Colour fill,
                                     juce::Colour outline,
                                     float outlineThickness,
                                     float cornerRadius) noexcept
{
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerRadius);
    if (outline.getAlpha() > 0 && outlineThickness > 0.0f)
    {
        g.setColour (outline);
        g.drawRoundedRectangle (bounds.reduced (outlineThickness * 0.5f),
                                cornerRadius, outlineThickness);
    }
}

juce::Rectangle<float> ChugLookAndFeel::symbolBounds (
    juce::Rectangle<float> bounds, float padding) noexcept
{
    return bounds.reduced (padding);
}

//==============================================================================
// State symbol painter.
//   Paints a distinct, state-specific glyph into the supplied bounds. The
//   glyph is paired with the accent colour at the call site (badge helper)
//   so colour is never the sole carrier of state identity — required by
//   CHUGGATE_SPEC.md §21.2.
//==============================================================================
void ChugLookAndFeel::paintStateSymbol (juce::Graphics& g,
                                        juce::Rectangle<float> bounds,
                                        GateState state,
                                        juce::Colour foreground) noexcept
{
    g.setColour (foreground);

    const auto s = symbolBounds (bounds, juce::jmax (1.0f, bounds.getWidth() * 0.20f));
    const float cx = s.getCentreX();
    const float cy = s.getCentreY();
    const float w  = s.getWidth();
    const float h  = s.getHeight();

    const float stroke = juce::jlimit (1.0f, 3.0f, juce::jmin (w, h) * 0.18f);
    juce::Path p;

    switch (state)
    {
        case GateState::Closed:
        {
            // Filled disc — resting state.
            g.fillEllipse (s);
        }
        break;

        case GateState::Primed:
        {
            // Upward triangle outline — pre-attack.
            p.startNewSubPath (cx,          s.getY());
            p.lineTo          (s.getRight(), s.getBottom());
            p.lineTo          (s.getX(),     s.getBottom());
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (stroke));
        }
        break;

        case GateState::Attack:
        {
            // Upward arrow shaft + chevron — three strokes share the start point.
            p.startNewSubPath (cx,            s.getY());
            p.lineTo          (cx,            s.getBottom());
            p.startNewSubPath (cx,            s.getY());
            p.lineTo          (cx - w * 0.30f, cy);
            p.startNewSubPath (cx,            s.getY());
            p.lineTo          (cx + w * 0.30f, cy);
            g.strokePath (p, juce::PathStrokeType (stroke));
        }
        break;

        case GateState::Open:
        {
            // Lightning bolt — the only state symbol with negative slope.
            const float x0 = s.getX();
            const float y0 = s.getY();
            const float dx = w * 0.5f;
            p.startNewSubPath (x0 + dx * 0.8f, y0);
            p.lineTo          (x0 + dx * 0.1f, cy);
            p.lineTo          (x0 + dx * 0.6f, cy);
            p.lineTo          (x0 + dx * 0.2f, s.getBottom());
            p.lineTo          (x0 + dx * 0.9f, cy);
            p.lineTo          (x0 + dx * 0.4f, cy);
            p.closeSubPath();
            g.fillPath (p);
        }
        break;

        case GateState::Hold:
        {
            // Solid square — stable / held.
            g.fillRect (s);
        }
        break;

        case GateState::Sustain:
        {
            // Diamond outline.
            p.startNewSubPath (cx, s.getY());
            p.lineTo          (s.getRight(), cy);
            p.lineTo          (cx, s.getBottom());
            p.lineTo          (s.getX(), cy);
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (stroke));
        }
        break;

        case GateState::Release:
        {
            // Downward arrow shaft + chevron — three strokes share the start point.
            p.startNewSubPath (cx,            s.getBottom());
            p.lineTo          (cx,            s.getY());
            p.startNewSubPath (cx,            s.getBottom());
            p.lineTo          (cx - w * 0.30f, cy);
            p.startNewSubPath (cx,            s.getBottom());
            p.lineTo          (cx + w * 0.30f, cy);
            g.strokePath (p, juce::PathStrokeType (stroke));
        }
        break;

        case GateState::Reopen:
        {
            // Ring — reactivation / cyclic — plus a tick marker indicating
            // directional motion so the symbol stays distinct from Hold.
            juce::Path ring;
            ring.addEllipse (s);
            g.strokePath (ring, juce::PathStrokeType (stroke));
            juce::Path tick;
            tick.startNewSubPath (s.getX() + w * 0.25f, cy);
            tick.lineTo          (cx,                    s.getY() + h * 0.20f);
            tick.lineTo          (s.getRight() - w * 0.10f, cy);
            g.strokePath (tick, juce::PathStrokeType (stroke));
        }
        break;

        case GateState::Feedback:
        {
            // Cross / X — protection alert.
            p.startNewSubPath (s.getX(),     s.getY());
            p.lineTo          (s.getRight(), s.getBottom());
            p.startNewSubPath (s.getRight(), s.getY());
            p.lineTo          (s.getX(),     s.getBottom());
            g.strokePath (p, juce::PathStrokeType (stroke));
        }
        break;
    }
}

//==============================================================================
// Badge / lamp helpers.
//==============================================================================
void ChugLookAndFeel::drawStateBadge (juce::Graphics& g,
                                      juce::Rectangle<float> bounds,
                                      GateState state,
                                      bool active) const
{
    const auto fill = active ? getStateColour (state)
                             : getStateColourInactive (state);
    const auto outline = active ? juce::Colour (kOutlineStrongColour)
                                : juce::Colour (kOutlineColour);
    drawSharpRect (g, bounds, fill, outline, 1.0f, 2.0f);

    // Symbol is drawn in a colour that contrasts strongly with the fill so it
    // survives color-blind viewing. We pick near-black for bright fills and
    // near-white for dark fills.
    const float luminance = fill.getBrightness();
    const auto symbolColour = luminance > 0.55f
                                  ? juce::Colour (kBackgroundColour)
                                  : juce::Colour (kTextColour);

    paintStateSymbol (g, bounds, state, symbolColour);
}

void ChugLookAndFeel::drawStateBadge (juce::Graphics& g,
                                      juce::Rectangle<int> bounds,
                                      GateState state,
                                      bool active) const
{
    drawStateBadge (g, bounds.toFloat(), state, active);
}

void ChugLookAndFeel::drawStateBadgeWithLabel (juce::Graphics& g,
                                               juce::Rectangle<float> bounds,
                                               GateState state,
                                               bool active,
                                               bool drawLabelText) const
{
    constexpr float labelPadding = 6.0f;
    constexpr float badgeWidth   = 18.0f;

    auto badge = bounds.removeFromLeft (juce::jmin (badgeWidth, bounds.getWidth()));
    drawStateBadge (g, badge, state, active);

    if (! drawLabelText)
        return;

    bounds.removeFromLeft (labelPadding);

    g.setColour (active ? juce::Colour (kTextColour)
                        : juce::Colour (kMutedTextColour));
    g.setFont (juce::FontOptions (juce::jlimit (10.0f, 14.0f, bounds.getHeight() * 0.65f),
                                  juce::Font::bold));
    g.drawFittedText (getStateName (state),
                      bounds.toType<int>(),
                      juce::Justification::centredLeft,
                      1);
}

//==============================================================================
// Slider overrides.
//==============================================================================
int ChugLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    // Mechanical / precise — slightly larger than the V4 default so the
    // accent thumb reads as instrumentation rather than decoration.
    return juce::jmax (8, juce::jmin (slider.getHeight(), slider.getWidth()) / 4);
}

void ChugLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                        int x, int y, int width, int height,
                                        float sliderPosProportional,
                                        float rotaryStartAngle,
                                        float rotaryEndAngle,
                                        juce::Slider& slider)
{
    // Bounds and geometry.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (8.0f);
    const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto knob    = bounds.withSizeKeepingCentre (diameter, diameter);
    const auto centre  = knob.getCentre();

    // Outer track ring (sunken, mechanical).
    juce::Path track;
    track.addArc (knob.getX(), knob.getY(), knob.getWidth(), knob.getHeight(),
                  rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (juce::Colour (kSunkenPanelColour));
    g.strokePath (track, juce::PathStrokeType (juce::jmax (2.0f, diameter * 0.08f),
                                              juce::PathStrokeType::mitered,
                                              juce::PathStrokeType::square));

    // Filled arc from start angle up to current value.
    const float clamped = juce::jlimit (0.0f, 1.0f, sliderPosProportional);
    const float currentAngle = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * clamped;

    juce::Path fillArc;
    fillArc.addArc (knob.getX(), knob.getY(), knob.getWidth(), knob.getHeight(),
                    rotaryStartAngle, currentAngle, true);

    g.setColour (resolveAccentColour (slider.isMouseOverOrDragging(), slider.isEnabled()));
    g.strokePath (fillArc, juce::PathStrokeType (juce::jmax (2.0f, diameter * 0.08f),
                                                 juce::PathStrokeType::mitered,
                                                 juce::PathStrokeType::square));

    // Thumb — flat indicator, mechanical not glossy.
    const float innerDiameter = diameter * 0.62f;
    const auto thumb = juce::Rectangle<float> (innerDiameter, innerDiameter)
                           .withCentre (centre);
    g.setColour (juce::Colour (kRaisedPanelColour));
    g.fillEllipse (thumb);
    g.setColour (juce::Colour (kOutlineStrongColour));
    g.drawEllipse (thumb, 1.0f);

    // Index notch — a single 2 px mark on the indicator pointing in the
    // direction of motion. Reinforces the fast / precise / mechanical feel.
    const float notchLength = innerDiameter * 0.45f;
    const float notchThickness = juce::jmax (1.5f, diameter * 0.04f);
    const float notchAngle = currentAngle - juce::MathConstants<float>::halfPi;

    juce::Line<float> notch (
        centre,
        centre.translated (std::cos (notchAngle) * notchLength * 0.5f,
                           std::sin (notchAngle) * notchLength * 0.5f));

    g.setColour (juce::Colour (kAccentColour));
    g.drawLine (notch, notchThickness);

    // Tick at start / end so the range is always readable.
    auto tick = [&g, centre, diameter, notchThickness] (float angle)
    {
        const float r0 = diameter * 0.46f;
        const float r1 = diameter * 0.50f;
        juce::Line<float> line (
            centre.translated (std::cos (angle) * r0, std::sin (angle) * r0),
            centre.translated (std::cos (angle) * r1, std::sin (angle) * r1));
        g.setColour (juce::Colour (kOutlineStrongColour));
        g.drawLine (line, notchThickness);
    };
    tick (rotaryStartAngle);
    tick (rotaryEndAngle);
}

void ChugLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                        int x, int y, int width, int height,
                                        float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                        juce::Slider::SliderStyle style,
                                        juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool isVertical = (style == juce::Slider::LinearVertical
                          || style == juce::Slider::LinearBarVertical);
    const bool isBarStyle = (style == juce::Slider::LinearBar
                          || style == juce::Slider::LinearBarVertical);

    // Track dimensions — tight, high-contrast, mechanical.
    const float trackThickness = isVertical
                                    ? juce::jmax (4.0f, bounds.getWidth() * 0.30f)
                                    : juce::jmax (4.0f, bounds.getHeight() * 0.30f);
    const auto track = isVertical
                          ? bounds.withSizeKeepingCentre (trackThickness, bounds.getHeight() - 8.0f)
                          : bounds.withSizeKeepingCentre (bounds.getWidth() - 8.0f, trackThickness);

    drawSharpRect (g, track,
                   juce::Colour (kSunkenPanelColour),
                   juce::Colour (kOutlineColour),
                   1.0f, 1.0f);

    // Filled portion of the track.
    const float clamped = juce::jlimit (0.0f, 1.0f, sliderPos);
    juce::Rectangle<float> filled;
    if (isVertical)
        filled = track.withTop (track.getY() + track.getHeight() * (1.0f - clamped));
    else
        filled = track.withRight (track.getX() + track.getWidth() * clamped);

    g.setColour (resolveAccentColour (slider.isMouseOverOrDragging(), slider.isEnabled()));
    g.fillRoundedRectangle (filled.reduced (1.0f), 1.0f);

    if (isBarStyle)
        return; // Bar styles render fill-only — no separate thumb.

    // Thumb — sharp, mechanical, with single-pixel outline.
    const float thumbThickness = isVertical
                                    ? trackThickness + 4.0f
                                    : bounds.getHeight() * 0.40f;
    const float thumbLength    = isVertical
                                    ? bounds.getHeight() * 0.18f
                                    : trackThickness + 4.0f;

    juce::Rectangle<float> thumb;
    if (isVertical)
        thumb = juce::Rectangle<float> (thumbThickness, thumbLength)
                    .withCentre ({ track.getCentreX(),
                                   track.getY() + track.getHeight() * (1.0f - clamped) });
    else
        thumb = juce::Rectangle<float> (thumbLength, thumbThickness)
                    .withCentre ({ track.getX() + track.getWidth() * clamped,
                                   track.getCentreY() });

    drawSharpRect (g, thumb,
                   juce::Colour (kRaisedPanelColour),
                   juce::Colour (kAccentColour),
                   1.0f, 1.0f);
}

//==============================================================================
// Button overrides.
//==============================================================================
void ChugLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                            juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f, 0.5f);

    juce::Colour fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = juce::Colour (kSunkenPanelColour);
    else if (shouldDrawButtonAsHighlighted)
        fill = juce::Colour (kRaisedPanelColour);
    else
        fill = juce::Colour (kPanelColour);

    // Toggle / on-state accent (used by latched buttons such as Learn / Bypass).
    const bool on = button.getToggleState();
    juce::Colour outline = on ? juce::Colour (kAccentColour)
                             : juce::Colour (kOutlineColour);
    if (shouldDrawButtonAsHighlighted)
        outline = juce::Colour (kOutlineStrongColour);
    if (shouldDrawButtonAsDown)
        outline = juce::Colour (kAccentColour);

    drawSharpRect (g, bounds, fill, outline,
                   on || shouldDrawButtonAsDown ? 1.5f : 1.0f, 2.0f);
}

juce::Font ChugLookAndFeel::getTextButtonFont (juce::TextButton& /*button*/, int buttonHeight)
{
    // Slightly condensed and bold — reads as instrumentation, not decoration.
    const float height = juce::jlimit (10.0f, 18.0f, buttonHeight * 0.55f);
    return juce::FontOptions (height, juce::Font::bold);
}

void ChugLookAndFeel::drawToggleButton (juce::Graphics& g,
                                        juce::ToggleButton& button,
                                        bool shouldDrawButtonAsHighlighted,
                                        bool shouldDrawButtonAsDown)
{
    // Mechanical square tick box + label. Color-blind safe because the tick
    // is shape (square fill) not colour.
    const float fontSize = juce::jmin (15.0f, (float) button.getHeight() * 0.75f);
    const float tickWidth = fontSize * 1.1f;

    drawTickBox (g, button,
                 4.0f, ((float) button.getHeight() - tickWidth) * 0.5f,
                 tickWidth, tickWidth,
                 button.getToggleState(),
                 button.isEnabled(),
                 shouldDrawButtonAsHighlighted,
                 shouldDrawButtonAsDown);

    g.setColour (button.findColour (juce::ToggleButton::textColourId));
    g.setFont (fontSize);

    if (! button.isEnabled())
        g.setOpacity (0.5f);

    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (juce::roundToInt (tickWidth) + 10)
                                             .withTrimmedRight (2),
                      juce::Justification::centredLeft, 1);
}

void ChugLookAndFeel::drawTickBox (juce::Graphics& g,
                                   juce::Component& component,
                                   float x, float y, float w, float h,
                                   bool ticked, bool isEnabled,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    const auto bounds = juce::Rectangle<float> (x, y, w, h);
    juce::Colour fill   = juce::Colour (kSunkenPanelColour);
    juce::Colour outline = juce::Colour (kOutlineColour);

    if (shouldDrawButtonAsDown)        outline = juce::Colour (kAccentColour);
    else if (shouldDrawButtonAsHighlighted) outline = juce::Colour (kOutlineStrongColour);

    if (ticked)
        fill = juce::Colour (kAccentColour);

    if (! isEnabled)
        fill = fill.withAlpha (0.5f);

    drawSharpRect (g, bounds, fill, outline, 1.0f, 1.0f);

    if (ticked && isEnabled)
    {
        // Tick mark drawn in background colour so it contrasts against the
        // accent fill. Two-segment stroke keeps it mechanical.
        juce::Path tick;
        tick.startNewSubPath (bounds.getX() + w * 0.20f, bounds.getCentreY());
        tick.lineTo         (bounds.getX() + w * 0.45f, bounds.getBottom() - h * 0.25f);
        tick.lineTo         (bounds.getX() + w * 0.80f, bounds.getY()        + h * 0.20f);
        g.setColour (juce::Colour (kBackgroundColour));
        g.strokePath (tick, juce::PathStrokeType (juce::jmax (1.5f, w * 0.12f),
                                                 juce::PathStrokeType::mitered,
                                                 juce::PathStrokeType::square));
    }
    // mark the unused-component parameter use
    (void) component;
}

//==============================================================================
// Combo box overrides.
//==============================================================================
void ChugLookAndFeel::drawComboBox (juce::Graphics& g,
                                    int width, int height, bool isButtonDown,
                                    int buttonX, int buttonY, int buttonW, int buttonH,
                                    juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat();
    juce::Colour fill   = juce::Colour (kPanelColour);
    juce::Colour outline = juce::Colour (kOutlineColour);

    if (isButtonDown || box.hasKeyboardFocus (true))
        outline = juce::Colour (kAccentColour);
    else if (box.isMouseOverOrDragging())
        outline = juce::Colour (kOutlineStrongColour);

    drawSharpRect (g, bounds, fill, outline,
                   isButtonDown ? 1.5f : 1.0f, 2.0f);

    // Arrow chevron — sharp, single colour, mechanical.
    const auto arrowBounds = juce::Rectangle<float> (
        (float) buttonX, (float) buttonY, (float) buttonW, (float) buttonH).reduced (4.0f);
    const float ax = arrowBounds.getCentreX();
    const float ay = arrowBounds.getCentreY();
    const float aw = arrowBounds.getWidth()  * 0.35f;
    const float ah = arrowBounds.getHeight() * 0.30f;

    juce::Path chevron;
    chevron.startNewSubPath (ax - aw, ay - ah * 0.5f);
    chevron.lineTo         (ax,      ay + ah * 0.5f);
    chevron.lineTo         (ax + aw, ay - ah * 0.5f);

    g.setColour (juce::Colour (kAccentColour));
    g.strokePath (chevron, juce::PathStrokeType (juce::jmax (1.0f, ah * 0.18f),
                                                 juce::PathStrokeType::mitered,
                                                 juce::PathStrokeType::square));
}

juce::Font ChugLookAndFeel::getComboBoxFont (juce::ComboBox& /*box*/)
{
    return juce::FontOptions (13.0f, juce::Font::plain);
}

void ChugLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    // Match the mechanical / sharp geometry: minimal padding, hard edge.
    label.setBounds (4, 1,
                     box.getWidth() - 24,
                     box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
// Label overrides.
//==============================================================================
void ChugLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // Sharp, flat, no rounded gradient — labels are instrument readouts.
    if (label.isBeingEdited())
        return; // Editor handles its own drawing.

    const auto bg = label.findColour (juce::Label::backgroundColourId);
    if (bg.getAlpha() > 0)
        g.fillAll (bg);

    const auto outlineColour = label.findColour (juce::Label::outlineColourId);
    if (outlineColour.getAlpha() > 0)
    {
        g.setColour (outlineColour);
        g.drawRect (label.getLocalBounds(), 1);
    }

    const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
    g.setFont (getLabelFont (label));

    g.drawFittedText (label.getText(),
                      getLabelBorderSize (label).subtractedFrom (label.getLocalBounds()),
                      label.getJustificationType(),
                      juce::jmax (1, (int) ((float) label.getHeight() / g.getCurrentFont().getHeight())),
                      label.getMinimumHorizontalScale());
}

juce::Font ChugLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::BorderSize<int> ChugLookAndFeel::getLabelBorderSize (juce::Label& label)
{
    // Tight border — labels sit flush inside their panels.
    return label.getBorderSize();
}

//==============================================================================
// Scrollbar overrides.
//==============================================================================
int ChugLookAndFeel::getDefaultScrollbarWidth()
{
    return 10;
}

void ChugLookAndFeel::drawScrollbar (juce::Graphics& g,
                                     juce::ScrollBar& scrollbar,
                                     int x, int y, int width, int height,
                                     bool isScrollbarVertical,
                                     int thumbStartPosition, int thumbSize,
                                     bool isMouseOver, bool isMouseDown)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    g.setColour (juce::Colour (kPanelColour));
    g.fillRect (bounds);

    if (thumbSize <= 0)
        return;

    juce::Rectangle<float> thumb;
    if (isScrollbarVertical)
        thumb = { (float) x,
                  (float) (y + thumbStartPosition),
                  (float) width,
                  (float) thumbSize };
    else
        thumb = { (float) (x + thumbStartPosition),
                  (float) y,
                  (float) thumbSize,
                  (float) height };

    juce::Colour thumbColour = juce::Colour (kOutlineStrongColour);
    if (isMouseDown)      thumbColour = juce::Colour (kAccentColour);
    else if (isMouseOver) thumbColour = juce::Colour (kAccentColour).withAlpha (0.7f);

    drawSharpRect (g, thumb.reduced (1.0f), thumbColour,
                   juce::Colour (kOutlineColour), 1.0f, 1.0f);

    // Avoid unused-parameter warning while keeping the API signature.
    (void) scrollbar;
}

//==============================================================================
// Tab overrides — sharp rectangular tabs with a strong accent on the active
// tab. Tight 0 overlap so tabs read as separate panels, not a continuous
// strip.
//==============================================================================
int ChugLookAndFeel::getTabButtonOverlap (int /*tabDepth*/)
{
    return 0;
}

int ChugLookAndFeel::getTabButtonSpaceAroundImage()
{
    return 4;
}

void ChugLookAndFeel::createTabButtonShape (juce::TabBarButton& button,
                                            juce::Path& path,
                                            bool /*isMouseOver*/, bool /*isMouseDown*/)
{
    // Sharp rectangle, sized to the tab content + chrome. No rounding —
    // tabs read as machined panels.
    path.addRectangle (button.getActiveArea().toFloat());
}

void ChugLookAndFeel::fillTabButtonShape (juce::TabBarButton& button,
                                          juce::Graphics& g,
                                          const juce::Path& path,
                                          bool isMouseOver, bool /*isMouseDown*/)
{
    const bool front = button.isFrontTab();
    juce::Colour fill   = front ? juce::Colour (kPanelColour) : juce::Colour (kBackgroundColour);
    juce::Colour outline = front ? juce::Colour (kAccentColour) : juce::Colour (kOutlineColour);

    if (isMouseOver && ! front)
        fill = juce::Colour (kRaisedPanelColour);

    g.setColour (fill);
    g.fillPath (path);
    g.setColour (outline);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void ChugLookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar,
                                                     juce::Graphics& g)
{
    g.setColour (juce::Colour (kBackgroundColour));
    g.fillRect (bar.getLocalBounds());
}

juce::Font ChugLookAndFeel::getTabButtonFont (juce::TabBarButton& /*button*/, float height)
{
    // Bold, slightly smaller than the control — tabs are headers, not data.
    const float size = juce::jlimit (11.0f, 16.0f, height * 0.55f);
    return juce::FontOptions (size, juce::Font::bold);
}

void ChugLookAndFeel::drawTabButtonText (juce::TabBarButton& button,
                                         juce::Graphics& g,
                                         bool isMouseOver, bool isMouseDown)
{
    const bool front = button.isFrontTab();
    juce::Colour textColour = front ? juce::Colour (kTextColour)
                                    : juce::Colour (kMutedTextColour);
    if (isMouseDown)
        textColour = juce::Colour (kAccentColour);
    else if (isMouseOver && ! front)
        textColour = juce::Colour (kTextColour);

    g.setColour (textColour);
    g.setFont (getTabButtonFont (button, (float) button.getTabbedButtonBar().getHeight()));
    g.drawFittedText (button.getButtonText(),
                      button.getTextArea(),
                      juce::Justification::centred,
                      1);
}

void ChugLookAndFeel::drawTabButton (juce::TabBarButton& button,
                                     juce::Graphics& g,
                                     bool isMouseOver, bool isMouseDown)
{
    juce::Path shape;
    createTabButtonShape (button, shape, isMouseOver, isMouseDown);
    fillTabButtonShape (button, g, shape, isMouseOver, isMouseDown);
    drawTabButtonText (button, g, isMouseOver, isMouseDown);
}

} // namespace ChugGate::GUI
