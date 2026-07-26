#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ChugGateAudioProcessor;

namespace ChugGate {

struct NoiseProfile {
    float broadbandFloor = -80.0f;
    float lowHum = -80.0f;
    float midrangeBuzz = -80.0f;
    float hfHiss = -80.0f;
    float intermittentSpikes = -80.0f;
    float confidence = 0.0f;
    float captureDuration = 0.0f;
    float lowBandFloor = -80.0f;
    float midBandFloor = -80.0f;
    float highBandFloor = -80.0f;

    juce::ValueTree toValueTree() const;
    static NoiseProfile fromValueTree(const juce::ValueTree& vt);
};

struct DIAlignment {
    double offset = 0.0;
    float confidence = 0.0f;
    float variation = 0.0f;
    juce::String metadata;

    juce::ValueTree toValueTree() const;
    static DIAlignment fromValueTree(const juce::ValueTree& vt);
};

class StateManager : private juce::AudioProcessorValueTreeState::Listener {
public:
    StateManager(ChugGateAudioProcessor& processor);
    ~StateManager() override;

    // A/B slots management
    void saveCurrentStateToSlot(const juce::String& slotName);
    void loadStateFromSlot(const juce::String& slotName);
    void swapABSlots();
    void copyActiveToInactive();
    juce::String getActiveABState() const;
    void setActiveABState(const juce::String& stateName);

    // Noise profile management
    void saveNoiseProfile(const NoiseProfile& profile);
    NoiseProfile getNoiseProfile() const;

    // DI alignment result management
    void saveDIAlignment(const DIAlignment& alignment);
    DIAlignment getDIAlignment() const;

    // Preset management
    juce::StringArray getFactoryPresets() const;
    juce::StringArray getUserPresets() const;
    bool loadPreset(const juce::String& presetName, bool isFactory);
    bool saveUserPreset(const juce::String& presetName);
    bool deleteUserPreset(const juce::String& presetName);

    // Full state serialization
    juce::ValueTree createFullState() const;
    void loadFullState(const juce::ValueTree& state);

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    void initializeSlots();
    void updateRealTimeFields();
    juce::ValueTree getOrCreateChild(const juce::Identifier& type);

    static juce::File getFactoryPresetDir();
    static juce::File getUserPresetDir();

    ChugGateAudioProcessor& processor;
    juce::File factoryPresetDir;
    juce::File userPresetDir;
    bool isRestoringState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StateManager)
};

} // namespace ChugGate
