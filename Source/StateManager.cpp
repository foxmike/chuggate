#include "StateManager.h"
#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include <cmath>

namespace ChugGate {

//==============================================================================
juce::ValueTree NoiseProfile::toValueTree() const
{
    juce::ValueTree vt("NoiseProfile");
    vt.setProperty("broadbandFloor", broadbandFloor, nullptr);
    vt.setProperty("lowHum", lowHum, nullptr);
    vt.setProperty("midrangeBuzz", midrangeBuzz, nullptr);
    vt.setProperty("hfHiss", hfHiss, nullptr);
    vt.setProperty("intermittentSpikes", intermittentSpikes, nullptr);
    vt.setProperty("confidence", confidence, nullptr);
    vt.setProperty("captureDuration", captureDuration, nullptr);
    vt.setProperty("lowBandFloor", lowBandFloor, nullptr);
    vt.setProperty("midBandFloor", midBandFloor, nullptr);
    vt.setProperty("highBandFloor", highBandFloor, nullptr);
    return vt;
}

NoiseProfile NoiseProfile::fromValueTree(const juce::ValueTree& vt)
{
    NoiseProfile np;
    if (vt.isValid())
    {
        np.broadbandFloor = vt.getProperty("broadbandFloor", -80.0f);
        np.lowHum = vt.getProperty("lowHum", -80.0f);
        np.midrangeBuzz = vt.getProperty("midrangeBuzz", -80.0f);
        np.hfHiss = vt.getProperty("hfHiss", -80.0f);
        np.intermittentSpikes = vt.getProperty("intermittentSpikes", -80.0f);
        np.confidence = vt.getProperty("confidence", 0.0f);
        np.captureDuration = vt.getProperty("captureDuration", 0.0f);
        np.lowBandFloor = vt.getProperty("lowBandFloor", -80.0f);
        np.midBandFloor = vt.getProperty("midBandFloor", -80.0f);
        np.highBandFloor = vt.getProperty("highBandFloor", -80.0f);
    }
    return np;
}

//==============================================================================
juce::ValueTree DIAlignment::toValueTree() const
{
    juce::ValueTree vt("DIAlignment");
    vt.setProperty("offset", offset, nullptr);
    vt.setProperty("confidence", confidence, nullptr);
    vt.setProperty("variation", variation, nullptr);
    vt.setProperty("metadata", metadata, nullptr);
    return vt;
}

DIAlignment DIAlignment::fromValueTree(const juce::ValueTree& vt)
{
    DIAlignment di;
    if (vt.isValid())
    {
        di.offset = vt.getProperty("offset", 0.0);
        di.confidence = vt.getProperty("confidence", 0.0f);
        di.variation = vt.getProperty("variation", 0.0f);
        di.metadata = vt.getProperty("metadata", "").toString();
    }
    return di;
}

//==============================================================================
StateManager::StateManager(ChugGateAudioProcessor& proc)
    : processor(proc)
{
    factoryPresetDir = getFactoryPresetDir();
    userPresetDir = getUserPresetDir();

    // Register A/B state listener
    processor.getAPVTS().addParameterListener(ChugGate::ParameterIDs::abState, this);

    // Initialize slots in the ValueTree
    initializeSlots();

    // Initialize real-time atomic fields from the current state
    updateRealTimeFields();
}

StateManager::~StateManager()
{
    processor.getAPVTS().removeParameterListener(ChugGate::ParameterIDs::abState, this);
}

//==============================================================================
void StateManager::saveCurrentStateToSlot(const juce::String& slotName)
{
    auto slot = getOrCreateChild(slotName);
    auto state = processor.getAPVTS().state;

    for (int i = 0; i < state.getNumProperties(); ++i)
    {
        auto propName = state.getPropertyName(i);
        if (propName.toString() != ChugGate::ParameterIDs::abState)
        {
            slot.setProperty(propName, state.getProperty(propName), nullptr);
        }
    }
}

void StateManager::loadStateFromSlot(const juce::String& slotName)
{
    auto slot = getOrCreateChild(slotName);
    auto& apvts = processor.getAPVTS();

    // Temporarily suspend listening to avoid double-handling
    isRestoringState = true;

    for (int i = 0; i < slot.getNumProperties(); ++i)
    {
        auto propName = slot.getPropertyName(i);
        if (propName.toString() != ChugGate::ParameterIDs::abState)
        {
            apvts.state.setProperty(propName, slot.getProperty(propName), nullptr);
        }
    }

    isRestoringState = false;
}

void StateManager::swapABSlots()
{
    juce::String activeSlotName = getActiveABState() == "A" ? "SlotA" : "SlotB";
    saveCurrentStateToSlot(activeSlotName);

    auto slotA = getOrCreateChild("SlotA");
    auto slotB = getOrCreateChild("SlotB");

    auto temp = slotA.createCopy();

    slotA.removeAllProperties(nullptr);
    slotA.removeAllChildren(nullptr);
    for (int i = 0; i < slotB.getNumProperties(); ++i)
        slotA.setProperty(slotB.getPropertyName(i), slotB.getProperty(slotB.getPropertyName(i)), nullptr);
    for (int i = 0; i < slotB.getNumChildren(); ++i)
        slotA.addChild(slotB.getChild(i).createCopy(), -1, nullptr);

    slotB.removeAllProperties(nullptr);
    slotB.removeAllChildren(nullptr);
    for (int i = 0; i < temp.getNumProperties(); ++i)
        slotB.setProperty(temp.getPropertyName(i), temp.getProperty(temp.getPropertyName(i)), nullptr);
    for (int i = 0; i < temp.getNumChildren(); ++i)
        slotB.addChild(temp.getChild(i).createCopy(), -1, nullptr);

    loadStateFromSlot(activeSlotName);
}

void StateManager::copyActiveToInactive()
{
    juce::String activeSlotName = getActiveABState() == "A" ? "SlotA" : "SlotB";
    juce::String inactiveSlotName = activeSlotName == "SlotA" ? "SlotB" : "SlotA";

    saveCurrentStateToSlot(inactiveSlotName);
}

juce::String StateManager::getActiveABState() const
{
    if (auto* param = processor.getAPVTS().getParameter(ChugGate::ParameterIDs::abState))
    {
        return param->getValue() < 0.5f ? "A" : "B";
    }
    return "A";
}

void StateManager::setActiveABState(const juce::String& stateName)
{
    if (auto* param = processor.getAPVTS().getParameter(ChugGate::ParameterIDs::abState))
    {
        float newValue = (stateName == "A") ? 0.0f : 1.0f;
        if (std::abs(param->getValue() - newValue) > 0.05f)
        {
            param->setValueNotifyingHost(newValue);
        }
    }
}

//==============================================================================
void StateManager::saveNoiseProfile(const NoiseProfile& profile)
{
    auto child = getOrCreateChild("NoiseProfile");
    child.setProperty("broadbandFloor", profile.broadbandFloor, nullptr);
    child.setProperty("lowHum", profile.lowHum, nullptr);
    child.setProperty("midrangeBuzz", profile.midrangeBuzz, nullptr);
    child.setProperty("hfHiss", profile.hfHiss, nullptr);
    child.setProperty("intermittentSpikes", profile.intermittentSpikes, nullptr);
    child.setProperty("confidence", profile.confidence, nullptr);
    child.setProperty("captureDuration", profile.captureDuration, nullptr);
    child.setProperty("lowBandFloor", profile.lowBandFloor, nullptr);
    child.setProperty("midBandFloor", profile.midBandFloor, nullptr);
    child.setProperty("highBandFloor", profile.highBandFloor, nullptr);

    // Update real-time fields
    processor.realTimeState.noiseBroadbandFloor.store(profile.broadbandFloor);
    processor.realTimeState.noiseLowBandFloor.store(profile.lowBandFloor);
    processor.realTimeState.noiseMidBandFloor.store(profile.midBandFloor);
    processor.realTimeState.noiseHighBandFloor.store(profile.highBandFloor);
}

NoiseProfile StateManager::getNoiseProfile() const
{
    auto& apvts = processor.getAPVTS();
    auto child = apvts.state.getChildWithName("NoiseProfile");
    return NoiseProfile::fromValueTree(child);
}

void StateManager::saveDIAlignment(const DIAlignment& alignment)
{
    auto child = getOrCreateChild("DIAlignment");
    child.setProperty("offset", alignment.offset, nullptr);
    child.setProperty("confidence", alignment.confidence, nullptr);
    child.setProperty("variation", alignment.variation, nullptr);
    child.setProperty("metadata", alignment.metadata, nullptr);

    // Update real-time fields
    processor.realTimeState.alignmentOffset.store(alignment.offset);
}

DIAlignment StateManager::getDIAlignment() const
{
    auto& apvts = processor.getAPVTS();
    auto child = apvts.state.getChildWithName("DIAlignment");
    return DIAlignment::fromValueTree(child);
}

//==============================================================================
juce::StringArray StateManager::getFactoryPresets() const
{
    juce::StringArray names;
    if (factoryPresetDir.isDirectory())
    {
        juce::Array<juce::File> files;
        factoryPresetDir.findChildFiles(files, juce::File::findFiles, false, "*.xml");
        for (const auto& file : files)
            names.add(file.getFileNameWithoutExtension());
    }
    return names;
}

juce::StringArray StateManager::getUserPresets() const
{
    juce::StringArray names;
    if (userPresetDir.isDirectory())
    {
        juce::Array<juce::File> files;
        userPresetDir.findChildFiles(files, juce::File::findFiles, false, "*.xml");
        for (const auto& file : files)
            names.add(file.getFileNameWithoutExtension());
    }
    return names;
}

bool StateManager::loadPreset(const juce::String& presetName, bool isFactory)
{
    auto dir = isFactory ? factoryPresetDir : userPresetDir;
    auto file = dir.getChildFile(presetName + ".xml");

    if (!file.existsAsFile())
        return false;

    if (auto xml = juce::XmlDocument::parse(file))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        if (state.isValid())
        {
            loadFullState(state);
            return true;
        }
    }
    return false;
}

bool StateManager::saveUserPreset(const juce::String& presetName)
{
    if (!userPresetDir.exists())
    {
        if (!userPresetDir.createDirectory())
            return false;
    }

    auto file = userPresetDir.getChildFile(presetName + ".xml");
    auto state = createFullState();

    if (auto xml = state.createXml())
        return xml->writeTo(file);

    return false;
}

bool StateManager::deleteUserPreset(const juce::String& presetName)
{
    auto file = userPresetDir.getChildFile(presetName + ".xml");
    if (file.existsAsFile())
        return file.deleteFile();
    return false;
}

//==============================================================================
juce::ValueTree StateManager::createFullState() const
{
    auto state = processor.getAPVTS().copyState();
    state.setProperty("schema_version", ChugGate::ParameterIDs::schemaVersion, nullptr);
    return state;
}

void StateManager::loadFullState(const juce::ValueTree& state)
{
    if (!state.isValid() || !state.hasType(processor.getAPVTS().state.getType()))
        return;

    isRestoringState = true;
    processor.getAPVTS().replaceState(state.createCopy());
    isRestoringState = false;

    updateRealTimeFields();
}

//==============================================================================
void StateManager::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (isRestoringState)
        return;

    if (parameterID == ChugGate::ParameterIDs::abState)
    {
        juce::String newSlot = (newValue < 0.5f) ? "SlotA" : "SlotB";
        juce::String oldSlot = (newSlot == "SlotA") ? "SlotB" : "SlotA";

        juce::MessageManager::callAsync([this, newSlot, oldSlot]()
        {
            saveCurrentStateToSlot(oldSlot);
            loadStateFromSlot(newSlot);
        });
    }
}

void StateManager::initializeSlots()
{
    auto slotA = getOrCreateChild("SlotA");
    auto slotB = getOrCreateChild("SlotB");

    auto state = processor.getAPVTS().state;

    if (slotA.getNumProperties() == 0)
    {
        for (int i = 0; i < state.getNumProperties(); ++i)
        {
            auto propName = state.getPropertyName(i);
            if (propName.toString() != ChugGate::ParameterIDs::abState)
                slotA.setProperty(propName, state.getProperty(propName), nullptr);
        }
    }

    if (slotB.getNumProperties() == 0)
    {
        for (int i = 0; i < state.getNumProperties(); ++i)
        {
            auto propName = state.getPropertyName(i);
            if (propName.toString() != ChugGate::ParameterIDs::abState)
                slotB.setProperty(propName, state.getProperty(propName), nullptr);
        }
    }
}

void StateManager::updateRealTimeFields()
{
    auto np = getNoiseProfile();
    processor.realTimeState.noiseBroadbandFloor.store(np.broadbandFloor);
    processor.realTimeState.noiseLowBandFloor.store(np.lowBandFloor);
    processor.realTimeState.noiseMidBandFloor.store(np.midBandFloor);
    processor.realTimeState.noiseHighBandFloor.store(np.highBandFloor);

    auto di = getDIAlignment();
    processor.realTimeState.alignmentOffset.store(di.offset);
}

juce::ValueTree StateManager::getOrCreateChild(const juce::Identifier& type)
{
    auto& apvts = processor.getAPVTS();
    auto child = apvts.state.getChildWithName(type);
    if (!child.isValid())
    {
        child = juce::ValueTree(type);
        apvts.state.addChild(child, -1, nullptr);
    }
    return child;
}

juce::File StateManager::getFactoryPresetDir()
{
    auto exeFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    
    // Check multiple potential locations relative to executable
    auto dir = exeFile.getParentDirectory().getChildFile("Resources").getChildFile("Presets");
    if (dir.isDirectory()) return dir;

    dir = exeFile.getParentDirectory().getParentDirectory().getChildFile("Resources").getChildFile("Presets");
    if (dir.isDirectory()) return dir;

    dir = exeFile.getParentDirectory().getParentDirectory().getParentDirectory().getChildFile("Resources").getChildFile("Presets");
    if (dir.isDirectory()) return dir;

    // Check project workspace folder explicitly if we are in development environment
    dir = juce::File("/home/mike/dev/mike/music/plugins/chuggate/Resources/Presets");
    if (dir.isDirectory()) return dir;

    return juce::File::getCurrentWorkingDirectory().getChildFile("Resources").getChildFile("Presets");
}

juce::File StateManager::getUserPresetDir()
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    return appData.getChildFile("ChugGate").getChildFile("Presets");
}

} // namespace ChugGate
