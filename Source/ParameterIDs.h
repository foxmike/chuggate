#pragma once

// These identifiers are persisted by hosts and presets. Once released, their
// spelling and meaning must remain stable. The close threshold intentionally
// has no ID: it is always derived as thresholdOpen - hysteresis.
namespace ChugGate::ParameterIDs {
inline constexpr int schemaVersion = 1;

namespace Global {
inline constexpr auto enabled = "global.enabled";
inline constexpr auto mode = "global.mode";
inline constexpr auto topology = "global.topology";
inline constexpr auto gainMode = "global.gainMode";
inline constexpr auto thresholdOpen = "global.thresholdOpen";
inline constexpr auto hysteresis = "global.hysteresis";
inline constexpr auto attack = "global.attack";
inline constexpr auto hold = "global.hold";
inline constexpr auto release = "global.release";
inline constexpr auto maxAdaptiveRelease = "global.maxAdaptiveRelease";
inline constexpr auto floor = "global.floor";
inline constexpr auto ratio = "global.ratio";
inline constexpr auto knee = "global.knee";
inline constexpr auto curve = "global.curve";
inline constexpr auto lookahead = "global.lookahead";
inline constexpr auto outputTrim = "global.outputTrim";
inline constexpr auto bypass = "global.bypass";
} // namespace Global

namespace Key {
inline constexpr auto source = "key.source";
inline constexpr auto externalBlend = "key.externalBlend";
inline constexpr auto hpfEnabled = "key.hpfEnabled";
inline constexpr auto hpfFrequency = "key.hpfFrequency";
inline constexpr auto hpfSlope = "key.hpfSlope";
inline constexpr auto lpfEnabled = "key.lpfEnabled";
inline constexpr auto lpfFrequency = "key.lpfFrequency";
inline constexpr auto lpfSlope = "key.lpfSlope";
inline constexpr auto emphasisFreq = "key.emphasisFreq";
inline constexpr auto emphasisGain = "key.emphasisGain";
inline constexpr auto emphasisQ = "key.emphasisQ";
inline constexpr auto humMode = "key.humMode";
inline constexpr auto humHarmonics = "key.humHarmonics";
inline constexpr auto humNotchQ = "key.humNotchQ";
inline constexpr auto humImmunityAmount = "key.humImmunityAmount";
inline constexpr auto alignmentOffset = "key.alignmentOffset";
inline constexpr auto fallbackMode = "key.fallbackMode";
inline constexpr auto listenMode = "key.listenMode";
} // namespace Key

namespace AttackGuard {
inline constexpr auto enabled = "attackGuard.enabled";
inline constexpr auto amount = "attackGuard.amount";
inline constexpr auto sensitivity = "attackGuard.sensitivity";
inline constexpr auto frequency = "attackGuard.frequency";
inline constexpr auto decay = "attackGuard.decay";
inline constexpr auto softNoteAssist = "attackGuard.softNoteAssist";
inline constexpr auto maxThresholdReduction =
    "attackGuard.maxThresholdReduction";
inline constexpr auto confirmationWindow = "attackGuard.confirmationWindow";
} // namespace AttackGuard

namespace Sustain {
inline constexpr auto enabled = "sustain.enabled";
inline constexpr auto amount = "sustain.amount";
inline constexpr auto releaseExtension = "sustain.releaseExtension";
inline constexpr auto harmonicWeight = "sustain.harmonicWeight";
inline constexpr auto decayWeight = "sustain.decayWeight";
inline constexpr auto noiseWeight = "sustain.noiseWeight";
inline constexpr auto feedbackEnabled = "sustain.feedbackEnabled";
inline constexpr auto feedbackSensitivity = "sustain.feedbackSensitivity";
inline constexpr auto feedbackMinDuration = "sustain.feedbackMinDuration";
inline constexpr auto feedbackMaxHold = "sustain.feedbackMaxHold";
inline constexpr auto feedbackFreqMin = "sustain.feedbackFreqMin";
inline constexpr auto feedbackFreqMax = "sustain.feedbackFreqMax";
inline constexpr auto feedbackFloor = "sustain.feedbackFloor";
inline constexpr auto feedbackRequireExtKey = "sustain.feedbackRequireExtKey";
inline constexpr auto feedbackSafetyTimeout = "sustain.feedbackSafetyTimeout";
} // namespace Sustain

namespace Cadence {
inline constexpr auto enabled = "cadence.enabled";
inline constexpr auto amount = "cadence.amount";
inline constexpr auto releaseFraction = "cadence.releaseFraction";
inline constexpr auto sourceMode = "cadence.sourceMode";
} // namespace Cadence

namespace NoiseLearn {
inline constexpr auto snrMargin = "noiseLearn.snrMargin";
inline constexpr auto adaptiveEnabled = "noiseLearn.adaptiveEnabled";
inline constexpr auto adaptiveRate = "noiseLearn.adaptiveRate";
} // namespace NoiseLearn

struct Band {
  const char *floor;
  const char *thresholdOffset;
  const char *attackOffset;
  const char *holdOffset;
  const char *releaseMultiplier;
  const char *solo;
  const char *mute;
  const char *detectorWeighting;
};

inline constexpr Band bandLow{
    "bandLow.floor",      "bandLow.thresholdOffset",   "bandLow.attackOffset",
    "bandLow.holdOffset", "bandLow.releaseMultiplier", "bandLow.solo",
    "bandLow.mute",       "bandLow.detectorWeighting"};

inline constexpr Band bandMid{
    "bandMid.floor",      "bandMid.thresholdOffset",   "bandMid.attackOffset",
    "bandMid.holdOffset", "bandMid.releaseMultiplier", "bandMid.solo",
    "bandMid.mute",       "bandMid.detectorWeighting"};

inline constexpr Band bandHigh{"bandHigh.floor",
                               "bandHigh.thresholdOffset",
                               "bandHigh.attackOffset",
                               "bandHigh.holdOffset",
                               "bandHigh.releaseMultiplier",
                               "bandHigh.solo",
                               "bandHigh.mute",
                               "bandHigh.detectorWeighting"};

namespace HissCurtain {
inline constexpr auto minFreq = "hissCurtain.minFreq";
inline constexpr auto maxFreq = "hissCurtain.maxFreq";
inline constexpr auto depth = "hissCurtain.depth";
} // namespace HissCurtain

namespace Stereo {
inline constexpr auto linkMode = "stereo.linkMode";
inline constexpr auto linkAmount = "stereo.linkAmount";
} // namespace Stereo

namespace Crossovers {
inline constexpr auto dualFreq = "crossovers.dualFreq";
inline constexpr auto triLowFreq = "crossovers.triLowFreq";
inline constexpr auto triHighFreq = "crossovers.triHighFreq";
} // namespace Crossovers

namespace Audition {
inline constexpr auto rejectedMode = "audition.rejectedMode";
inline constexpr auto stateFilterMode = "audition.stateFilterMode";
} // namespace Audition

inline constexpr auto profileIndex = "profile.index";
inline constexpr auto tuningIndex = "tuning.index";
inline constexpr auto abState = "ab.state";
} // namespace ChugGate::ParameterIDs
