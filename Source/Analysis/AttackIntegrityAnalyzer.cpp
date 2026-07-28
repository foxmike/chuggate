#include "AttackIntegrityAnalyzer.h"

#include "PerformanceHistory.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ChugGate::Analysis {

namespace {

constexpr int maxEventCapacity = 65536;
constexpr int maxAttackWindowSamples = 4096;

float dbToAmplitude(float db) noexcept {
  if (!std::isfinite(db)) {
    return 0.0f;
  }
  return std::pow(10.0f, db * 0.05f);
}

} // namespace

void AttackIntegrityAnalyzer::prepare(int maxEvents,
                                      int attackWindowSamples) noexcept {
  const int requestedCapacity = std::clamp(maxEvents, 0, maxEventCapacity);
  events_.allocate(requestedCapacity, false);
  eventCapacity_ =
      requestedCapacity > 0 && events_.getData() != nullptr
          ? requestedCapacity
          : 0;
  attackWindowSamples_ =
      std::clamp(attackWindowSamples, 1, maxAttackWindowSamples);
  reset();
}

void AttackIntegrityAnalyzer::reset() noexcept {
  writeIndex_ = 0;
  eventCount_.store(0, std::memory_order_relaxed);
  liveDecimationCounter_ = 0;
  lastObservedHistoryPushes_ = 0;
  retainedRatioSum_ = 0.0;
  latestRatio_.store(0.0f, std::memory_order_relaxed);
  meanRatio_.store(0.0f, std::memory_order_relaxed);
  preservedCount_.store(0, std::memory_order_relaxed);
  partialCount_.store(0, std::memory_order_relaxed);
  missedCount_.store(0, std::memory_order_relaxed);
}

void AttackIntegrityAnalyzer::setQualityMode(QualityMode mode) noexcept {
  qualityMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
  liveDecimationCounter_ = 0;
}

QualityMode AttackIntegrityAnalyzer::getQualityMode() const noexcept {
  return static_cast<QualityMode>(
      qualityMode_.load(std::memory_order_relaxed));
}

bool AttackIntegrityAnalyzer::shouldAnalyzeNextEvent() noexcept {
  switch (getQualityMode()) {
  case QualityMode::Off:
    return false;
  case QualityMode::Live:
    return (liveDecimationCounter_++ & 1) == 0;
  case QualityMode::Studio:
  case QualityMode::Diagnose:
  default:
    return true;
  }
}

void AttackIntegrityAnalyzer::observeOnset(const float *input,
                                           const float *output,
                                           int numSamples,
                                           float transientStrength) noexcept {
  if (input == nullptr || output == nullptr || numSamples <= 0 ||
      !shouldAnalyzeNextEvent()) {
    return;
  }

  double inputEnergy = 0.0;
  double outputEnergy = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    const double x = std::isfinite(input[i]) ? input[i] : 0.0;
    const double y = std::isfinite(output[i]) ? output[i] : 0.0;
    inputEnergy += x * x;
    outputEnergy += y * y;
  }

  addEvent(inputEnergy, outputEnergy, transientStrength);
}

void AttackIntegrityAnalyzer::observe(
    const PerformanceHistory &history) noexcept {
  const std::uint64_t totalPushes = history.getStoredPushes();
  if (totalPushes < lastObservedHistoryPushes_) {
    lastObservedHistoryPushes_ = 0;
  }

  const auto view = history.getView();
  const std::uint64_t delta = totalPushes - lastObservedHistoryPushes_;
  const int newSamples =
      static_cast<int>(std::min<std::uint64_t>(
          delta, static_cast<std::uint64_t>(view.numSamples)));
  lastObservedHistoryPushes_ = totalPushes;

  if (getQualityMode() == QualityMode::Off || newSamples <= 0 ||
      view.capacity <= 0 || view.transientMarker.empty()) {
    return;
  }

  const int firstNew = view.numSamples - newSamples;
  const int preRoll = attackWindowSamples_ / 2;
  const int postRoll = attackWindowSamples_ - preRoll;

  for (int logical = firstNew; logical < view.numSamples; ++logical) {
    const int markerIndex =
        (view.startIndex + logical) % view.capacity;
    const auto markerOffset = static_cast<std::size_t>(markerIndex);
    const float marker = view.transientMarker[markerOffset];
    if (!(marker > 0.0f) || !shouldAnalyzeNextEvent()) {
      continue;
    }

    const int begin = std::max(0, logical - preRoll);
    const int end = std::min(view.numSamples, logical + postRoll);
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;

    for (int windowIndex = begin; windowIndex < end; ++windowIndex) {
      const int physical =
          (view.startIndex + windowIndex) % view.capacity;
      const auto offset = static_cast<std::size_t>(physical);
      const double input = dbToAmplitude(view.mainEnvelope[offset]);
      const double gain = std::max(
          0.0, static_cast<double>(view.outputGain[offset]));
      const double output = input * gain;
      inputEnergy += input * input;
      outputEnergy += output * output;
    }

    addEvent(inputEnergy, outputEnergy, marker);
  }
}

void AttackIntegrityAnalyzer::addEvent(double inputEnergy,
                                       double outputEnergy,
                                       float transientStrength) noexcept {
  if (eventCapacity_ <= 0) {
    return;
  }

  Event event;
  event.inputEnergy =
      static_cast<float>(std::min<double>(
          std::max(0.0, inputEnergy), std::numeric_limits<float>::max()));
  event.outputEnergy =
      static_cast<float>(std::min<double>(
          std::max(0.0, outputEnergy), std::numeric_limits<float>::max()));
  event.preservationRatio = static_cast<float>(
      std::max(0.0, outputEnergy) / std::max(inputEnergy, energyEpsilon));
  event.transientStrength =
      std::isfinite(transientStrength) ? transientStrength : 0.0f;

  if (event.preservationRatio >= preservedThreshold) {
    event.classification = Classification::Preserved;
  } else if (event.preservationRatio >= partialThreshold) {
    event.classification = Classification::Partial;
  } else {
    event.classification = Classification::Missed;
  }

  const int count = eventCount_.load(std::memory_order_relaxed);
  if (count == eventCapacity_) {
    removeFromSummary(events_[writeIndex_]);
  }

  events_[writeIndex_] = event;
  writeIndex_ = (writeIndex_ + 1) % eventCapacity_;
  const int nextCount = std::min(count + 1, eventCapacity_);
  eventCount_.store(nextCount, std::memory_order_relaxed);

  retainedRatioSum_ += event.preservationRatio;
  switch (event.classification) {
  case Classification::Preserved:
    preservedCount_.fetch_add(1, std::memory_order_relaxed);
    break;
  case Classification::Partial:
    partialCount_.fetch_add(1, std::memory_order_relaxed);
    break;
  case Classification::Missed:
    missedCount_.fetch_add(1, std::memory_order_relaxed);
    break;
  }

  latestRatio_.store(event.preservationRatio, std::memory_order_relaxed);
  meanRatio_.store(
      static_cast<float>(retainedRatioSum_ / static_cast<double>(nextCount)),
      std::memory_order_relaxed);
}

void AttackIntegrityAnalyzer::removeFromSummary(
    const Event &event) noexcept {
  retainedRatioSum_ =
      std::max(0.0, retainedRatioSum_ - event.preservationRatio);
  switch (event.classification) {
  case Classification::Preserved:
    preservedCount_.fetch_sub(1, std::memory_order_relaxed);
    break;
  case Classification::Partial:
    partialCount_.fetch_sub(1, std::memory_order_relaxed);
    break;
  case Classification::Missed:
    missedCount_.fetch_sub(1, std::memory_order_relaxed);
    break;
  }
}

AttackIntegrityAnalyzer::Event
AttackIntegrityAnalyzer::getLatestEvent() const noexcept {
  const int count = eventCount_.load(std::memory_order_relaxed);
  if (count <= 0 || eventCapacity_ <= 0) {
    return {};
  }
  const int index = (writeIndex_ + eventCapacity_ - 1) % eventCapacity_;
  return events_[index];
}

int AttackIntegrityAnalyzer::copyRecentEvents(
    Event *destination, int destinationCapacity) const noexcept {
  if (destination == nullptr || destinationCapacity <= 0 ||
      eventCapacity_ <= 0) {
    return 0;
  }

  const int count = eventCount_.load(std::memory_order_relaxed);
  const int toCopy = std::min(count, destinationCapacity);
  int source = writeIndex_ - toCopy;
  if (source < 0) {
    source += eventCapacity_;
  }
  for (int i = 0; i < toCopy; ++i) {
    destination[i] = events_[source];
    source = (source + 1) % eventCapacity_;
  }
  return toCopy;
}

} // namespace ChugGate::Analysis
