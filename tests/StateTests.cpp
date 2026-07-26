// =============================================================================
// ChugGate - State / Profile / Preset Tests (Phase J, CHUGGATE_SPEC.md §22–§25)
// -----------------------------------------------------------------------------
// Focused juce::UnitTest cases covering the Phase J validation surface
// (CHUGGATE_SPEC.md §28 areas for preset / A-B / profile):
//
//   1. Profiles catalog          (§23) — exactly 12 profiles, unique non-empty
//                                     names, applyProfileDefaults mutates the
//                                     expected keys without throwing.
//   2. TuningProfiles catalog    (§25) — factory tuning list non-empty,
//                                     applyTuningAdjustments mutates only the
//                                     allowed tuning-sensitive subset.
//   3. StateManager A/B          (§22) — copy / switch / serialize-restore of
//                                     slots, including schema_version.
//   4. Factory presets           (§24) — the six expected JSON files parse
//                                     and contain meta.name + parameters.
//   5. Crossfade / bypass smoke  (§22.1/§22.2) — A/B switch + bypass toggle
//                                     helpers do not throw.
//
// All tests are deterministic: no audio hardware, no host processBlock. The
// StateManager fixture constructs a real ChugGateAudioProcessor (which is
// cheap without prepareToPlay) so the message-thread A/B APIs exercise the
// same code path used by the editor.
//
// NOTE on CMake registration: this file is intentionally not added to
// CMakeLists.txt here. The j.4 task is expected to register
// tests/StateTests.cpp (plus Source/PluginProcessor.cpp, Source/StateManager.cpp,
// Source/Profiles.cpp, Source/TuningProfiles.cpp and the juce_audio_processors
// link dependency) on the ChugGateTests target.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <set>
#include <utility>

#include "../Source/ParameterIDs.h"
#include "../Source/Profiles.h"
#include "../Source/TuningProfiles.h"
#include "../Source/StateManager.h"
#include "../Source/PluginProcessor.h"

namespace {

namespace ID = ChugGate::ParameterIDs;

// The six factory preset JSON files required by CHUGGATE_SPEC.md §24.
const juce::StringArray kExpectedFactoryPresets = {
    "Rhythm_ModernTight",
    "Lead_SingingSustain",
    "DIKeyed_Rhythm",
    "PostAmp_HissSuppression",
    "Bass_ActiveNoiseControl",
    "Diag_LearnMyRig",
};

// Resolve the repository-owned Resources/Presets directory deterministically
// from this source file's location so the test is reproducible from a clean
// clone and independent of the host's current working directory.
juce::File resolveFactoryPresetDir()
{
    // tests/StateTests.cpp -> repo root = parent of tests/.
    const auto repoRoot =
        juce::File::getSpecialLocation(juce::File::currentApplicationFile)
            .getParentDirectory();
    // Prefer the path derived from __FILE__ which is the most stable.
    const auto fromSource =
        juce::File(__FILE__).getParentDirectory().getParentDirectory()
            .getChildFile("Resources").getChildFile("Presets");
    if (fromSource.isDirectory())
        return fromSource;

    // Fall back to the processor's own resolver (searches around the exe).
    const auto fromProcessor = ChugGateAudioProcessor::getFactoryPresetDir();
    if (fromProcessor.isDirectory())
        return fromProcessor;

    // Last resort: search upward from the repo root.
    auto dir = repoRoot;
    for (int i = 0; i < 6 && dir.isDirectory(); ++i)
    {
        const auto candidate = dir.getChildFile("Resources").getChildFile("Presets");
        if (candidate.isDirectory())
            return candidate;
        dir = dir.getParentDirectory();
    }
    return fromSource;
}

// The StateManager abState listener posts its slot save/load work via
// MessageManager::callAsync. In the console unit-test harness the message
// loop is never pumped (TestMain does not run a dispatch loop), so those
// async callbacks do not fire during a test. The A/B assertions below
// therefore drive the synchronous StateManager slot APIs directly and do
// not depend on async delivery, keeping the tests deterministic and free
// of any JUCE_MODAL_LOOPS_PERMITTED dependency.

// Set an AudioParameterFloat-style parameter by denormalized value.
void setDenormalized(juce::AudioProcessorValueTreeState& apvts,
                     const char* paramId, float denormalized)
{
    if (auto* param = apvts.getParameter(paramId))
    {
        const auto& range = param->getNormalisableRange();
        const float clamped = juce::jlimit(range.start, range.end, denormalized);
        param->setValueNotifyingHost(range.convertTo0to1(clamped));
    }
}

float getDenormalized(juce::AudioProcessorValueTreeState& apvts,
                      const char* paramId)
{
    if (auto* param = apvts.getParameter(paramId))
    {
        const auto& range = param->getNormalisableRange();
        return range.convertFrom0to1(param->getValue());
    }
    return 0.0f;
}

class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest("ChugGate State Tests", "State") {}

    void runTest() override
    {
        beginTest("Profiles: exactly 12 unique non-empty names (§23)");
        runProfilesCatalog();

        beginTest("Profiles: applyProfileDefaults mutates expected keys");
        runProfilesApply();

        beginTest("TuningProfiles: factory tunings non-empty (§25)");
        runTuningCatalog();

        beginTest("TuningProfiles: applyTuningAdjustments only touches allowed fields");
        runTuningApply();

        beginTest("StateManager A/B: copy / switch / serialize-restore with schema_version (§22)");
        runABStateRoundTrip();

        beginTest("Factory presets: six JSON files parse with meta + parameters (§24)");
        runFactoryPresets();

        beginTest("Crossfade / bypass smoke: A/B switch + bypass toggle do not throw (§22.1/§22.2)");
        runCrossfadeBypassSmoke();
    }

private:
    // ---- 1. Profiles catalog ------------------------------------------------

    void runProfilesCatalog()
    {
        using namespace ChugGate::Profiles;

        expectEquals(profileCount(), 12, "Profile count must be exactly 12");

        const auto names = listProfileNames();
        expectEquals(names.size(), 12, "listProfileNames must return 12 entries");

        std::set<juce::String> seen;
        for (int i = 0; i < names.size(); ++i)
        {
            expect(names[i].isNotEmpty(),
                   "Profile name must be non-empty (index " + juce::String(i) + ")");
            const auto [it, inserted] = seen.insert(names[i]);
            expect(inserted,
                   "Profile name must be unique: \"" + names[i] + "\"");
        }

        // Spot-check a representative subset of the §23 documented set.
        expect(names.contains("Tight Rhythm"), "§23 set must include Tight Rhythm");
        expect(names.contains("Lead Sustain"), "§23 set must include Lead Sustain");
        expect(names.contains("Active Bass"), "§23 set must include Active Bass");
        expect(names.contains("Distorted Bass"), "§23 set must include Distorted Bass");
        expect(names.contains("Post-Amp Hiss Control"), "§23 set must include Post-Amp Hiss Control");

        // Stable index lookup consistency.
        for (int i = 0; i < 12; ++i)
        {
            const auto* info = infoByIndex(i);
            expect(info != nullptr, "infoByIndex must be non-null for valid index");
            expect(info != nullptr && info->index == i,
                   "ProfileInfo.index must match the queried index");
            expect(info != nullptr && juce::String(info->name) == names[i],
                   "ProfileInfo.name must match listProfileNames order");
            expect(info != nullptr && juce::String(info->stableId).isNotEmpty(),
                   "Profile stableId must be non-empty");
        }

        expect(infoByIndex(-1) == nullptr, "Negative index must return nullptr");
        expect(infoByIndex(12) == nullptr, "Out-of-range index must return nullptr");
    }

    // ---- 2. Profiles apply --------------------------------------------------

    void runProfilesApply()
    {
        using namespace ChugGate::Profiles;

        // Use a ParameterMap (no live APVTS required) so the test stays
        // lightweight and deterministic.
        for (int i = 0; i < 12; ++i)
        {
            ParameterMap pm;
            const auto written = applyProfileDefaults(i, pm);
            expect(written.size() > 0,
                   "applyProfileDefaults must write at least one parameter "
                   "for profile " + juce::String(i));

            // Every written ID must be present in the map with a finite value.
            bool allFinite = true;
            bool allPresent = true;
            for (const auto& id : written)
            {
                const auto it = pm.find(id);
                if (it == pm.end())
                {
                    allPresent = false;
                    break;
                }
                if (!std::isfinite(it->second))
                    allFinite = false;
            }
            expect(allPresent, "Every written ID must be present in the map");
            expect(allFinite, "Every written value must be finite");

            // Re-applying must be idempotent (no throw, same keys).
            ParameterMap pm2;
            const auto written2 = applyProfileDefaults(i, pm2);
            expect(written2 == written,
                   "applyProfileDefaults must be deterministic across calls");
        }

        // Out-of-range index returns an empty list and does not throw.
        ParameterMap pmSafe;
        const auto emptySafe = applyProfileDefaults(-5, pmSafe);
        expect(emptySafe.isEmpty(),
               "Out-of-range profile index must write nothing");
        const auto emptyHigh = applyProfileDefaults(999, pmSafe);
        expect(emptyHigh.isEmpty(),
               "Out-of-range profile index must write nothing");
    }

    // ---- 3. Tuning catalog --------------------------------------------------

    void runTuningCatalog()
    {
        using namespace ChugGate::TuningProfiles;

        expect(tuningCount() == 19, "Tuning count must be 19 (0..18 + Custom)");

        const auto names = listTuningNames();
        expect(names.size() > 0, "Factory tuning list must be non-empty");
        expectEquals(names.size(), tuningCount(),
                     "listTuningNames size must match tuningCount");

        std::set<juce::String> seen;
        for (const auto& n : names)
        {
            expect(n.isNotEmpty(), "Tuning name must be non-empty");
            seen.insert(n);
        }
        expect(static_cast<int>(seen.size()) == names.size(),
               "Tuning names must be unique");

        // Every index resolves to a valid descriptor.
        for (int i = 0; i < tuningCount(); ++i)
        {
            const auto* info = infoByIndex(i);
            expect(info != nullptr, "infoByIndex must resolve valid tuning index");
            expect(info != nullptr && info->index == i,
                   "TuningInfo.index must match queried index");
        }
        expect(infoByIndex(-1) == nullptr, "Negative tuning index -> nullptr");
        expect(infoByIndex(tuningCount()) == nullptr,
               "Out-of-range tuning index -> nullptr");
    }

    // ---- 4. Tuning apply ----------------------------------------------------

    void runTuningApply()
    {
        using namespace ChugGate::TuningProfiles;
        using namespace ChugGate::Profiles;

        // The complete allowed tuning-sensitive subset (§25).
        const std::set<juce::String> allowed = {
            ID::Key::hpfFrequency,
            ID::Crossovers::triLowFreq,
            ID::bandLow.detectorWeighting,
            ID::bandMid.detectorWeighting,
            ID::bandHigh.detectorWeighting,
            ID::Sustain::feedbackFreqMin,
            ID::Sustain::feedbackFreqMax,
            ID::Key::humMode,
            ID::Key::humImmunityAmount,
        };

        for (int t = 0; t < tuningCount(); ++t)
        {
            ParameterMap pm;
            // Seed a value that tunings must NOT touch to detect stray writes.
            pm[ID::Global::thresholdOpen] = -42.0f;
            pm[ID::Global::gainMode] = 1.0f;
            pm[ID::AttackGuard::amount] = 0.55f;
            pm[ID::Global::topology] = 2.0f;

            const auto written = applyTuningAdjustments(t, pm);
            expect(written.size() > 0,
                   "applyTuningAdjustments must write at least one field");

            // Every written key must belong to the allowed subset.
            for (const auto& id : written)
            {
                expect(allowed.count(id) > 0,
                       "Tuning wrote a non-allowed field: " + id);
            }

            // No scenario-specific value may have been mutated.
            expect(pm[ID::Global::thresholdOpen] == -42.0f,
                   "Tuning must not touch global.thresholdOpen");
            expect(pm[ID::Global::gainMode] == 1.0f,
                   "Tuning must not touch global.gainMode");
            expect(pm[ID::AttackGuard::amount] == 0.55f,
                   "Tuning must not touch attackGuard.amount");
            expect(pm[ID::Global::topology] == 2.0f,
                   "Tuning must not touch global.topology");

            // The adjustments descriptor must be finite and self-consistent.
            const auto adj = adjustmentsFor(t);
            expect(std::isfinite(adj.keyHpfDefaultHz), "keyHpfDefaultHz finite");
            expect(std::isfinite(adj.lowBandCrossoverHz), "lowBandCrossoverHz finite");
            expect(std::isfinite(adj.detectorLowWeight), "detectorLowWeight finite");
            expect(std::isfinite(adj.pitchMinHz) && std::isfinite(adj.pitchMaxHz),
                   "Advisory pitch bounds must be finite");
            expect(adj.pitchMinHz <= adj.pitchMaxHz,
                   "pitchMinHz must not exceed pitchMaxHz");
            expect(adj.sustainMinHz <= adj.sustainMaxHz,
                   "sustainMinHz must not exceed sustainMaxHz");
        }
    }

    // ---- 5. StateManager A/B round-trip ------------------------------------

    void runABStateRoundTrip()
    {
        ChugGateAudioProcessor processor;
        auto& apvts = processor.getAPVTS();
        auto* sm = processor.getStateManager();
        expect(sm != nullptr, "StateManager must exist after construction");

        // Start from a known state on slot A.
        expect(sm->getActiveABState() == "A",
               "Default active A/B state must be A");

        constexpr float kValueA = -30.0f;
        constexpr float kValueMutated = -50.0f;

        setDenormalized(apvts, ID::Global::thresholdOpen, kValueA);
        expectEquals(getDenormalized(apvts, ID::Global::thresholdOpen),
                     kValueA,
                     "thresholdOpen must round-trip the fixture value");

        // Save current (A) into SlotA, then copy active->inactive so SlotB
        // holds the same snapshot.
        sm->saveCurrentStateToSlot("SlotA");
        sm->copyActiveToInactive();

        // Mutate the active slot (A).
        setDenormalized(apvts, ID::Global::thresholdOpen, kValueMutated);
        expectEquals(getDenormalized(apvts, ID::Global::thresholdOpen),
                     kValueMutated,
                     "Mutated A must reflect the new value");

        // Switch active to B. The abState listener posts an async save/load
        // which is not pumped in the test harness; explicitly load SlotB so
        // the restore is deterministic regardless of async timing.
        sm->setActiveABState("B");
        sm->loadStateFromSlot("SlotB");

        expect(sm->getActiveABState() == "B",
               "After switch, active A/B state must be B");
        expectEquals(getDenormalized(apvts, ID::Global::thresholdOpen),
                     kValueA,
                     "Switching to B must restore B's snapshot value");

        // Mutate B and switch back to A; A must still hold its mutated value.
        setDenormalized(apvts, ID::Global::thresholdOpen, -99.0f);
        sm->setActiveABState("A");
        sm->loadStateFromSlot("SlotA");
        expectEquals(getDenormalized(apvts, ID::Global::thresholdOpen),
                     kValueMutated,
                     "Switching back to A must restore A's mutated snapshot");

        // ---- Serialization round-trip with schema_version -----------------
        const auto full = sm->createFullState();
        expect(full.isValid(), "createFullState must return a valid ValueTree");
        expect(full.hasType(apvts.state.getType()),
               "Full state must carry the APVTS state type");
        expect(full.hasProperty("schema_version"),
               "Full state must carry schema_version");
        expectEquals(int(full["schema_version"]),
                     int(ID::schemaVersion),
                     "schema_version must match ParameterIDs::schemaVersion");

        const auto slotA = full.getChildWithName("SlotA");
        const auto slotB = full.getChildWithName("SlotB");
        expect(slotA.isValid(), "SlotA must serialize as a child of full state");
        expect(slotB.isValid(), "SlotB must serialize as a child of full state");

        // Serialize to XML and reload.
        const auto xml = full.createXml();
        expect(xml != nullptr, "Full state must be XML-serializable");
        const auto reloaded = juce::ValueTree::fromXml(*xml);
        expect(reloaded.isValid(), "Reloaded ValueTree must be valid");
        expect(reloaded.hasProperty("schema_version"),
               "Reloaded state must carry schema_version");
        expectEquals(int(reloaded["schema_version"]),
                     int(ID::schemaVersion),
                     "Reloaded schema_version must match");
        expect(reloaded.getChildWithName("SlotA").isValid(),
               "Reloaded state must preserve SlotA");
        expect(reloaded.getChildWithName("SlotB").isValid(),
               "Reloaded state must preserve SlotB");

        // loadFullState must accept the reloaded tree without throwing and
        // must preserve schema_version in the live state.
        sm->loadFullState(reloaded);
        expect(apvts.state.hasProperty("schema_version"),
               "Live APVTS state must carry schema_version after loadFullState");
        expectEquals(int(apvts.state.getProperty("schema_version")),
                     int(ID::schemaVersion),
                     "Live schema_version must match after loadFullState");
    }

    // ---- 6. Factory presets -------------------------------------------------

    void runFactoryPresets()
    {
        const auto presetDir = resolveFactoryPresetDir();
        expect(presetDir.isDirectory(),
               "Factory preset directory must exist: " + presetDir.getFullPathName());

        for (const auto& name : kExpectedFactoryPresets)
        {
            const auto file = presetDir.getChildFile(name + ".json");
            expect(file.existsAsFile(),
                   "Factory preset JSON must exist: " + file.getFullPathName());

            juce::var parsed;
            const auto ok = juce::JSON::parse(file.loadFileAsString(), parsed);
            juce::ignoreUnused(ok);
            expect(parsed.isObject(),
                   "Preset " + name + " must parse to a JSON object");

            const auto* meta = parsed.getProperty("meta", juce::var()).getDynamicObject();
            expect(meta != nullptr,
                   "Preset " + name + " must contain a meta object");
            expect(meta != nullptr && meta->hasProperty("name"),
                   "Preset " + name + " meta must contain a name");

            const auto* params = parsed.getProperty("parameters", juce::var()).getDynamicObject();
            expect(params != nullptr,
                   "Preset " + name + " must contain a parameters object");
            expect(params != nullptr && params->getProperties().size() > 0,
                   "Preset " + name + " parameters object must be non-empty");

            // meta.schema_version, if present, must equal the current schema.
            if (meta != nullptr && meta->hasProperty("schema_version"))
            {
                expectEquals(int(meta->getProperty("schema_version")),
                             int(ID::schemaVersion),
                             "Preset " + name + " meta.schema_version must match");
            }
        }

        // Exactly the six expected JSON files (no missing, count check).
        juce::Array<juce::File> files;
        presetDir.findChildFiles(files, juce::File::findFiles, false, "*.json");
        expectEquals(files.size(), 6,
                     "Resources/Presets must contain exactly six JSON files");
    }

    // ---- 7. Crossfade / bypass smoke ---------------------------------------

    void runCrossfadeBypassSmoke()
    {
        ChugGateAudioProcessor processor;
        auto& apvts = processor.getAPVTS();
        auto* sm = processor.getStateManager();

        // A/B switch smoke: toggling the active slot must not throw and the
        // active-state getter must reflect it. This is the message-thread
        // trigger for the audio-thread A/B crossfade (§22.1).
        expect(sm->getActiveABState() == "A", "Initial active slot must be A");
        sm->setActiveABState("B");
        expect(sm->getActiveABState() == "B",
               "After switch, active slot must read B");
        sm->setActiveABState("A");
        expect(sm->getActiveABState() == "A",
               "After switch back, active slot must read A");

        // copyActiveToInactive / swapABSlots must not throw.
        sm->copyActiveToInactive();
        sm->swapABSlots();

        // Bypass helper smoke (§22.2): toggling global.bypass through APVTS
        // is the message-thread trigger for the bypass crossfade. It must
        // not throw and the parameter value must reflect the toggle.
        const float before = getDenormalized(apvts, ID::Global::bypass);
        juce::ignoreUnused(before);

        auto* bypassParam = apvts.getParameter(ID::Global::bypass);
        expect(bypassParam != nullptr, "global.bypass parameter must exist");
        if (bypassParam != nullptr)
        {
            bypassParam->setValueNotifyingHost(1.0f); // bypass ON
            expect(bypassParam->getValue() > 0.5f,
                   "Bypass toggle ON must be reflected by the parameter");

            bypassParam->setValueNotifyingHost(0.0f); // bypass OFF
            expect(bypassParam->getValue() < 0.5f,
                   "Bypass toggle OFF must be reflected by the parameter");
        }
    }
};

// Static instance registers the test with juce::UnitTestRunner.
static StateTests s_stateTestsInstance;

} // namespace
