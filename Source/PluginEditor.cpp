#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ChugGateAudioProcessorEditor::ChugGateAudioProcessorEditor (ChugGateAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Resizability and limits per ChugGate specification
    setResizable (true, true);
    setResizeLimits (920, 580, 2400, 1600);
    setSize (1280, 760);
}

ChugGateAudioProcessorEditor::~ChugGateAudioProcessorEditor()
{
}

//==============================================================================
void ChugGateAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark premium background gradient
    juce::ColourGradient grad (
        juce::Colour (0xff0f1219), 0.0f, 0.0f,
        juce::Colour (0xff080a0f), 0.0f, static_cast<float> (getHeight()),
        false
    );
    g.setGradientFill (grad);
    g.fillAll();

    // Draw header panel title
    g.setColour (juce::Colour (0xff00e5ff));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("CHUGGATE", 20, 15, 150, 24, juce::Justification::left);

    g.setColour (juce::Colour (0xff8f9ba8));
    g.setFont (juce::FontOptions (11.0f, juce::Font::plain));
    g.drawText ("ADAPTIVE GATE & SPECTRUM NOISE SUPPRESSOR", 125, 20, 400, 16, juce::Justification::left);
}

void ChugGateAudioProcessorEditor::resized()
{
    // Layout logic for child components will go here in subsequent tasks
}
