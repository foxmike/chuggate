// ChugLookAndFeel.h
//
// Project LookAndFeel_V4 subclass implementing the ChugGate visual identity
// described in CHUGGATE_SPEC.md §21.2 — fast, mechanical, precise, controlled.
//
//   * Dark neutral base palette
//   * Bright gate-state accents paired with distinct shape/character symbols
//     so that state is never conveyed by hue alone (color-blind safe, §21.2)
//   * Suite-consistent with ShredBoost / ShredEQ / ShredSpace (same family
//     of dark neutrals and accent hues)
//   * Restrained animation: no pulsing, no glow, sharp corners / tight radii
//     on every control so the chrome reads as instrumentation rather than
//     ornament
//
// Other GUI components (PerformanceTimeline, StateIndicator, page bodies,
// MainHeader, PluginEditor) obtain their colors, trace colors, threshold
// colors and state symbol vocabulary through the public accessors defined
// here. There is no audio DSP, APVTS wiring, page composition or owning
// of any other UI object inside this class.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ChugGate::GUI {

//==============================================================================
/**
 *  Gate-state vocabulary used by every GUI surface that needs to talk about
 *  the Performance State Engine.
 *
 *  The values mirror ChugGate::DSP::PerformanceStateMachine::State so the two
 *  enums can be converted losslessly; an extra Feedback entry covers the
 *  orthogonal feedback-protection flag surfaced by the state machine and the
 *  feedback detector (see CHUGGATE_SPEC.md §11).
 */
enum class GateState : std::uint8_t
{
    Closed = 0,
    Primed,
    Attack,
    Open,
    Hold,
    Sustain,
    Release,
    Reopen,
    Feedback
};

//==============================================================================
/**
 *  Look-and-feel implementation that drives the project visual identity.
 *
 *  Public surface (non-virtual, non-overridden):
 *      - Palette accessors    (background / panel / outline / text / muted)
 *      - Trace colour accessors (key, audio, threshold, noise floor) so the
 *        timeline painter and indicator can share the palette without
 *        hard-coding hex literals.
 *      - State-symbol API     (shape glyph + abbreviation + long name) so any
 *        lamp / badge can pair hue with a distinct shape — required by
 *        §21.2 color-blind safety.
 *      - drawStateBadge*()    helper renderers that paint a small square /
 *        rectangular badge using the active GateState's hue and shape symbol.
 *
 *  Virtual surface (LookAndFeel_V4 overrides implemented in ChugLookAndFeel.cpp):
 *      - drawRotarySlider, drawLinearSlider
 *      - drawButtonBackground, drawToggleButton
 *      - drawComboBox, getComboBoxFont, positionComboBoxText
 *      - drawLabel, getLabelFont, getLabelBorderSize
 *      - drawScrollbar
 *      - drawTabButton, getTabButtonFont, drawTabButtonText,
 *        drawTabbedButtonBarBackground, createTabButtonShape,
 *        fillTabButtonShape, getTabButtonOverlap, getTabButtonSpaceAroundImage
 */
class ChugLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChugLookAndFeel();

    //==========================================================================
    // Suite palette (static so non-component callers can fetch without holding
    // a pointer to the editor's LookAndFeel instance).
    //==========================================================================
    static juce::Colour getBackgroundColour() noexcept;
    static juce::Colour getPanelColour() noexcept;
    static juce::Colour getRaisedPanelColour() noexcept;
    static juce::Colour getSunkenPanelColour() noexcept;
    static juce::Colour getOutlineColour() noexcept;
    static juce::Colour getOutlineStrongColour() noexcept;
    static juce::Colour getTextColour() noexcept;
    static juce::Colour getMutedTextColour() noexcept;
    static juce::Colour getAccentColour() noexcept;
    static juce::Colour getAccentDimColour() noexcept;
    static juce::Colour getWarningColour() noexcept;

    //==========================================================================
    // Trace / threshold palette — used by PerformanceTimeline and similar
    // graphing components. Defining them here avoids hex-literal duplication
    // across timeline painters and indicator painters.
    //==========================================================================
    static juce::Colour getKeyTraceColour() noexcept;
    static juce::Colour getAudioTraceColour() noexcept;
    static juce::Colour getThresholdColour() noexcept;
    static juce::Colour getNoiseFloorColour() noexcept;
    static juce::Colour getGainTraceColour() noexcept;
    static juce::Colour getAttenuationColour() noexcept;

    //==========================================================================
    // Gate-state colour accessors.
    //
    // getStateColour(state)         — saturated hue used when the state is
    //                                 actively "current" (the badge is lit).
    // getStateColourInactive(state) — desaturated / dim variant used when the
    //                                 state is being shown for context but
    //                                 not active (e.g. legend).
    //==========================================================================
    static juce::Colour getStateColour (GateState state) noexcept;
    static juce::Colour getStateColourInactive (GateState state) noexcept;

    //==========================================================================
    // Color-blind-safe state vocabulary.
    //
    //   getStateSymbol(state)  — single distinct Unicode glyph used as the
    //                             shape half of the color-blind pairing
    //                             (e.g. "■", "▲", "◆", "✦", "✕" …). Always
    //                             paired with hue so state identification
    //                             never depends on colour alone.
    //   getStateAbbrev(state)  — short ASCII code used as text fallback in
    //                             compact readouts ("CLS", "OPN", "FBK" …).
    //   getStateName(state)    — human-readable name used in tooltips and
    //                             the StateIndicator status line.
    //==========================================================================
    static juce::String getStateSymbol (GateState state);
    static juce::String getStateAbbrev (GateState state);
    static juce::String getStateName (GateState state);

    //==========================================================================
    // Conversion helpers so callers that already have an enum value from the
    // engine can convert without knowing the engine header. The integer
    // overload accepts a raw PerformanceStateMachine::State value cast to int
    // and works even when the DSP module is not included at the call site.
    //==========================================================================
    static GateState gateStateFromEngine (int engineStateIndex) noexcept;

    //==========================================================================
    // Lamp / badge helpers — paint a small badge combining hue and shape so
    // state is conveyed redundantly (color-blind safe). The badge is square
    // with tight 2 px radius, a 1 px outline drawn in the suite outline
    // colour, and the state symbol centred in the badge.
    //==========================================================================
    void drawStateBadge (juce::Graphics& g,
                         juce::Rectangle<float> bounds,
                         GateState state,
                         bool active = true) const;

    void drawStateBadge (juce::Graphics& g,
                         juce::Rectangle<int> bounds,
                         GateState state,
                         bool active = true) const;

    void drawStateBadgeWithLabel (juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  GateState state,
                                  bool active,
                                  bool drawLabelText) const;

    //==========================================================================
    // LookAndFeel_V4 overrides — drawing / sizing of common controls.
    //==========================================================================
    void drawRotarySlider (juce::Graphics&,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override;

    void drawLinearSlider (juce::Graphics&,
                           int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override;

    int getSliderThumbRadius (juce::Slider& slider) override;

    void drawButtonBackground (juce::Graphics&,
                               juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton& button,
                                  int buttonHeight) override;

    void drawToggleButton (juce::Graphics&,
                           juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawTickBox (juce::Graphics&,
                      juce::Component& component,
                      float x, float y, float w, float h,
                      bool ticked, bool isEnabled,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&,
                       int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;

    juce::Font getComboBoxFont (juce::ComboBox& box) override;

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;

    void drawLabel (juce::Graphics&, juce::Label& label) override;
    juce::Font getLabelFont (juce::Label& label) override;
    juce::BorderSize<int> getLabelBorderSize (juce::Label& label) override;

    void drawScrollbar (juce::Graphics&,
                        juce::ScrollBar& scrollbar,
                        int x, int y, int width, int height,
                        bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    int getDefaultScrollbarWidth() override;

    void drawTabButton (juce::TabBarButton& button,
                        juce::Graphics& g,
                        bool isMouseOver, bool isMouseDown) override;

    juce::Font getTabButtonFont (juce::TabBarButton& button, float height) override;

    void drawTabButtonText (juce::TabBarButton& button,
                            juce::Graphics& g,
                            bool isMouseOver, bool isMouseDown) override;

    void drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar,
                                        juce::Graphics& g) override;

    void createTabButtonShape (juce::TabBarButton& button,
                               juce::Path& path,
                               bool isMouseOver, bool isMouseDown) override;

    void fillTabButtonShape (juce::TabBarButton& button,
                             juce::Graphics& g,
                             const juce::Path& path,
                             bool isMouseOver, bool isMouseDown) override;

    int getTabButtonOverlap (int tabDepth) override;
    int getTabButtonSpaceAroundImage() override;

private:
    //==========================================================================
    // Shared drawing helpers used by the LookAndFeel overrides.
    //==========================================================================
    static juce::Colour resolveAccentColour (bool highlighted, bool active) noexcept;
    static void drawSharpRect (juce::Graphics& g,
                               juce::Rectangle<float> bounds,
                               juce::Colour fill,
                               juce::Colour outline,
                               float outlineThickness = 1.0f,
                               float cornerRadius = 2.0f) noexcept;

    // Centre-of-bounds helper used by badge / symbol painters.
    static juce::Rectangle<float> symbolBounds (
        juce::Rectangle<float> bounds, float padding = 2.0f) noexcept;

    // State badge interior symbol painter — draws the shape half of the
    // color-blind pairing. Caller supplies the colour so it can be tuned to
    // contrast against the badge background.
    static void paintStateSymbol (juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  GateState state,
                                  juce::Colour foreground) noexcept;
};

} // namespace ChugGate::GUI