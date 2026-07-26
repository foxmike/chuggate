#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
class ChugGateAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    ChugGateAudioProcessorEditor (ChugGateAudioProcessor&);
    ~ChugGateAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChugGateAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChugGateAudioProcessorEditor)
};
