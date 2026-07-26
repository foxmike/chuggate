#include "SustainAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::DSP {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float minimumLevelDb = -100.0f;
constexpr float spectrumEpsilon = 1.0e-12f;

} // namespace

void SustainAnalyzer::prepare(double sampleRate, int maxBlockSize) noexcept {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 0.0;
  maxBlockSize_ = std::max(0, maxBlockSize);
  for (int n = 0; n < analysisWindowSize_; ++n) {
    window_[static_cast<std::size_t>(n)] =
        0.5f - 0.5f * std::cos(2.0f * pi * static_cast<float>(n) /
                               static_cast<float>(analysisWindowSize_ - 1));
  }
  controlHopSamples_ =
      sampleRate_ > 0.0
          ? std::max(1, static_cast<int>(std::lround(sampleRate_ * 0.01)))
          : 1;
  reset();
}

void SustainAnalyzer::reset() noexcept {
  sampleRing_.fill(0.0f);
  spectrum_.fill(0.0f);
  previousNormalizedSpectrum_.fill(0.0f);
  samplesUntilAnalysis_ = controlHopSamples_;
  writeIndex_ = 0;
  samplesInWindow_ = 0;
  havePreviousSpectrum_ = false;
  smoothedLogFundamental_ = 0.0f;
  haveSmoothedFundamental_ = false;
  previousAnalysisLevelDb_ = minimumLevelDb;
  havePreviousLevel_ = false;
  lastOutput_ = {};
  lastOutput_.spectralChange = 1.0f;
  lastOutput_.noiseLikeness = 1.0f;
  lastOutput_.adaptiveReleaseMs =
      std::min(baseReleaseMs_.load(std::memory_order_relaxed),
               maximumAdaptiveReleaseMs_.load(std::memory_order_relaxed));
}

SustainAnalyzer::Output
SustainAnalyzer::processSample(float rawMonoKey, float rawDetectorLevelDb,
                               const FundamentalEstimate &fundamental,
                               float externalKeyEvidence) noexcept {
  const float monoKey = std::clamp(finiteOr(rawMonoKey, 0.0f), -16.0f, 16.0f);
  const float detectorLevelDb = std::clamp(
      finiteOr(rawDetectorLevelDb, minimumLevelDb), minimumLevelDb, 24.0f);

  sampleRing_[static_cast<std::size_t>(writeIndex_)] = monoKey;
  writeIndex_ = (writeIndex_ + 1) % analysisWindowSize_;
  samplesInWindow_ = std::min(samplesInWindow_ + 1, analysisWindowSize_);

  if (--samplesUntilAnalysis_ <= 0) {
    samplesUntilAnalysis_ = controlHopSamples_;
    updateAnalysis(detectorLevelDb, fundamental, externalKeyEvidence);
  }

  return lastOutput_;
}

void SustainAnalyzer::process(const float *monoKey,
                              const float *detectorLevelDb, int numSamples,
                              const FundamentalEstimate &fundamental,
                              Output *output,
                              float externalKeyEvidence) noexcept {
  if (monoKey == nullptr || detectorLevelDb == nullptr || numSamples <= 0)
    return;

  for (int i = 0; i < numSamples; ++i) {
    const Output result = processSample(monoKey[i], detectorLevelDb[i],
                                        fundamental, externalKeyEvidence);
    if (output != nullptr)
      output[i] = result;
  }
}

void SustainAnalyzer::setEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
}

void SustainAnalyzer::setWeights(float harmonicWeight, float decayWeight,
                                 float externalEvidenceWeight,
                                 float noiseWeight) noexcept {
  harmonicWeight_.store(std::clamp(finiteOr(harmonicWeight, 0.0f), 0.0f, 2.0f),
                        std::memory_order_relaxed);
  decayWeight_.store(std::clamp(finiteOr(decayWeight, 0.0f), 0.0f, 2.0f),
                     std::memory_order_relaxed);
  externalWeight_.store(
      std::clamp(finiteOr(externalEvidenceWeight, 0.0f), 0.0f, 2.0f),
      std::memory_order_relaxed);
  noiseWeight_.store(std::clamp(finiteOr(noiseWeight, 0.0f), 0.0f, 2.0f),
                     std::memory_order_relaxed);
}

void SustainAnalyzer::setBaseRelease(float releaseMs) noexcept {
  baseReleaseMs_.store(std::clamp(finiteOr(releaseMs, 100.0f), 0.1f, 10000.0f),
                       std::memory_order_relaxed);
}

void SustainAnalyzer::setAdaptiveReleaseAmount(float amount) noexcept {
  adaptiveAmount_.store(std::clamp(finiteOr(amount, 0.0f), 0.0f, 20.0f),
                        std::memory_order_relaxed);
}

void SustainAnalyzer::setAdaptiveReleaseExponent(float gamma) noexcept {
  adaptiveExponent_.store(std::clamp(finiteOr(gamma, 1.0f), 0.1f, 8.0f),
                          std::memory_order_relaxed);
}

void SustainAnalyzer::setMaximumAdaptiveRelease(float maximumMs) noexcept {
  maximumAdaptiveReleaseMs_.store(
      std::clamp(finiteOr(maximumMs, 1000.0f), 0.1f, 10000.0f),
      std::memory_order_relaxed);
}

void SustainAnalyzer::setSustainThreshold(float threshold) noexcept {
  sustainThreshold_.store(std::clamp(finiteOr(threshold, 0.35f), 0.0f, 1.0f),
                          std::memory_order_relaxed);
}

void SustainAnalyzer::updateAnalysis(float detectorLevelDb,
                                     const FundamentalEstimate &fundamental,
                                     float externalKeyEvidence) noexcept {
  updateSmoothedFundamental(fundamental);

  const float hopSeconds =
      sampleRate_ > 0.0 ? static_cast<float>(controlHopSamples_ / sampleRate_)
                        : 0.01f;
  float slope = 0.0f;
  if (havePreviousLevel_ && hopSeconds > 0.0f)
    slope = (detectorLevelDb - previousAnalysisLevelDb_) / hopSeconds;
  previousAnalysisLevelDb_ = detectorLevelDb;
  havePreviousLevel_ = true;

  if (!enabled_.load(std::memory_order_relaxed)) {
    lastOutput_ = {};
    lastOutput_.decaySlopeDbPerSecond = slope;
    lastOutput_.spectralChange = 1.0f;
    lastOutput_.noiseLikeness = 1.0f;
    lastOutput_.adaptiveReleaseMs =
        std::min(baseReleaseMs_.load(std::memory_order_relaxed),
                 maximumAdaptiveReleaseMs_.load(std::memory_order_relaxed));
    return;
  }

  calculateSpectrum();
  float totalPower = 0.0f;
  for (float power : spectrum_)
    totalPower += power;

  float spectralChange = 1.0f;
  if (totalPower > spectrumEpsilon) {
    float distance = 0.0f;
    for (int bin = 0; bin < spectrumBins_; ++bin) {
      const float normalized =
          spectrum_[static_cast<std::size_t>(bin)] / totalPower;
      if (havePreviousSpectrum_)
        distance += std::abs(
            normalized -
            previousNormalizedSpectrum_[static_cast<std::size_t>(bin)]);
      previousNormalizedSpectrum_[static_cast<std::size_t>(bin)] = normalized;
    }
    if (havePreviousSpectrum_)
      spectralChange = std::clamp(0.5f * distance, 0.0f, 1.0f);
    havePreviousSpectrum_ = true;
  }

  const float smoothedFundamental =
      haveSmoothedFundamental_ ? std::exp(smoothedLogFundamental_) : 0.0f;
  float harmonicity = calculateHarmonicity(smoothedFundamental);
  if (!fundamental.validity)
    harmonicity = 0.0f;
  else
    harmonicity *=
        std::clamp(finiteOr(fundamental.confidence, 0.0f), 0.0f, 1.0f);

  // A broad range of negative slopes is accepted. The logarithmic shape
  // avoids rejecting a natural tail merely because its decay rate changes as
  // it approaches the noise floor.
  float validDecay = 0.0f;
  if (slope < -0.05f) {
    const float magnitude = -slope;
    const float slowEnough = std::clamp(magnitude / 2.0f, 0.0f, 1.0f);
    const float notAbrupt =
        1.0f - std::clamp((magnitude - 30.0f) / 90.0f, 0.0f, 1.0f);
    validDecay = slowEnough * notAbrupt;
  }

  const float externalEvidence =
      std::clamp(finiteOr(externalKeyEvidence, 0.0f), 0.0f, 1.0f);
  const float noiseLikeness = std::clamp(
      (1.0f - harmonicity) * (0.35f + 0.65f * spectralChange), 0.0f, 1.0f);
  const float rawScore =
      harmonicWeight_.load(std::memory_order_relaxed) * harmonicity +
      decayWeight_.load(std::memory_order_relaxed) * validDecay +
      externalWeight_.load(std::memory_order_relaxed) * externalEvidence -
      noiseWeight_.load(std::memory_order_relaxed) * noiseLikeness;
  const float sustainScore = std::clamp(rawScore, 0.0f, 1.0f);

  const float baseRelease = baseReleaseMs_.load(std::memory_order_relaxed);
  const float adaptiveRelease =
      baseRelease *
      (1.0f + adaptiveAmount_.load(std::memory_order_relaxed) *
                  std::pow(sustainScore,
                           adaptiveExponent_.load(std::memory_order_relaxed)));
  const float maximumRelease =
      maximumAdaptiveReleaseMs_.load(std::memory_order_relaxed);
  const bool active =
      sustainScore >= sustainThreshold_.load(std::memory_order_relaxed);

  lastOutput_.sustainScore = sustainScore;
  lastOutput_.harmonicity = harmonicity;
  lastOutput_.spectralChange = spectralChange;
  lastOutput_.validDecay = validDecay;
  lastOutput_.externalEvidence = externalEvidence;
  lastOutput_.noiseLikeness = noiseLikeness;
  lastOutput_.decaySlopeDbPerSecond = slope;
  lastOutput_.smoothedFundamentalHz = smoothedFundamental;
  lastOutput_.adaptiveReleaseMs =
      std::clamp(adaptiveRelease, 0.1f, maximumRelease);
  lastOutput_.sustainActive = active;
  lastOutput_.sustainProtection = active;
}

void SustainAnalyzer::updateSmoothedFundamental(
    const FundamentalEstimate &fundamental) noexcept {
  const float frequency = finiteOr(fundamental.estimatedFundamental, 0.0f);
  if (!fundamental.validity || frequency <= 0.0f)
    return;

  const float logFrequency = std::log(frequency);
  if (!haveSmoothedFundamental_) {
    smoothedLogFundamental_ = logFrequency;
    haveSmoothedFundamental_ = true;
    return;
  }

  // Approximately 120 ms in log-frequency space. Slow bends and periodic
  // vibrato therefore move the harmonic windows without creating a separate
  // pitch-instability penalty.
  const float hopSeconds =
      sampleRate_ > 0.0 ? static_cast<float>(controlHopSamples_ / sampleRate_)
                        : 0.01f;
  const float alpha = 1.0f - std::exp(-hopSeconds / 0.12f);
  smoothedLogFundamental_ += alpha * (logFrequency - smoothedLogFundamental_);
}

void SustainAnalyzer::calculateSpectrum() noexcept {
  spectrum_.fill(0.0f);
  if (samplesInWindow_ < analysisWindowSize_ || sampleRate_ <= 0.0)
    return;

  for (int bin = 0; bin < spectrumBins_; ++bin) {
    const float phaseStep = 2.0f * pi * static_cast<float>(bin + 1) /
                            static_cast<float>(analysisWindowSize_);
    const float stepReal = std::cos(phaseStep);
    const float stepImaginary = std::sin(phaseStep);
    float oscillatorReal = 1.0f;
    float oscillatorImaginary = 0.0f;
    float real = 0.0f;
    float imaginary = 0.0f;
    for (int n = 0; n < analysisWindowSize_; ++n) {
      const float sample = sampleAt(n) * window_[static_cast<std::size_t>(n)];
      real += sample * oscillatorReal;
      imaginary -= sample * oscillatorImaginary;
      const float nextReal =
          oscillatorReal * stepReal - oscillatorImaginary * stepImaginary;
      oscillatorImaginary =
          oscillatorImaginary * stepReal + oscillatorReal * stepImaginary;
      oscillatorReal = nextReal;
    }
    spectrum_[static_cast<std::size_t>(bin)] =
        real * real + imaginary * imaginary;
  }
}

float SustainAnalyzer::calculateHarmonicity(
    float fundamentalHz) const noexcept {
  if (fundamentalHz <= 0.0f || sampleRate_ <= 0.0)
    return 0.0f;

  float totalPower = 0.0f;
  for (float power : spectrum_)
    totalPower += power;
  if (totalPower <= spectrumEpsilon)
    return 0.0f;

  const float binWidth =
      static_cast<float>(sampleRate_) / static_cast<float>(analysisWindowSize_);
  const float highestFrequency = binWidth * static_cast<float>(spectrumBins_);
  std::array<bool, spectrumBins_> harmonicBins{};

  for (int harmonic = 1; harmonic <= 8; ++harmonic) {
    const float frequency = fundamentalHz * static_cast<float>(harmonic);
    if (frequency > highestFrequency + binWidth)
      break;
    const int centre = static_cast<int>(std::lround(frequency / binWidth)) - 1;
    // A one-bin neighbourhood tolerates leakage and f0 motion. Marking bins
    // prevents overlapping low-frequency harmonic windows being counted twice.
    for (int offset = -1; offset <= 1; ++offset) {
      const int bin = centre + offset;
      if (bin >= 0 && bin < spectrumBins_)
        harmonicBins[static_cast<std::size_t>(bin)] = true;
    }
  }

  float harmonicPower = 0.0f;
  for (int bin = 0; bin < spectrumBins_; ++bin) {
    if (harmonicBins[static_cast<std::size_t>(bin)])
      harmonicPower += spectrum_[static_cast<std::size_t>(bin)];
  }
  return std::clamp(harmonicPower / (totalPower + spectrumEpsilon), 0.0f, 1.0f);
}

float SustainAnalyzer::sampleAt(int chronologicalIndex) const noexcept {
  return sampleRing_[static_cast<std::size_t>(
      (writeIndex_ + chronologicalIndex) % analysisWindowSize_)];
}

float SustainAnalyzer::finiteOr(float value, float fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

} // namespace ChugGate::DSP
