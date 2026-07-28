#include "TailIntegrityAnalyzer.h"

#include "PerformanceHistory.h"

#include <algorithm>
#include <cmath>

namespace ChugGate::Analysis {

namespace {

constexpr int maxEventCapacity = 65536;
constexpr int maxTailWindowSamples = 16384;
constexpr double dbToNaturalLog = 0.11512925464970229; // ln(10) / 20

template <typename LogAmplitudeAt>
float fitDecayRate(int numSamples, LogAmplitudeAt logAmplitudeAt) noexcept {
  if (numSamples < 2) {
    return 0.0f;
  }

  const double meanX = 0.5 * static_cast<double>(numSamples - 1);
  double meanY = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    meanY += logAmplitudeAt(i);
  }
  meanY /= static_cast<double>(numSamples);

  double covariance = 0.0;
  double variance = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    const double dx = static_cast<double>(i) - meanX;
    covariance += dx * (logAmplitudeAt(i) - meanY);
    variance += dx * dx;
  }

  if (!(variance > 0.0)) {
    return 0.0f;
  }
  return static_cast<float>(std::max(0.0, -covariance / variance));
}

double amplitudeLog(float amplitude) noexcept {
  const double finiteAmplitude =
      std::isfinite(amplitude) ? std::abs(static_cast<double>(amplitude)) : 0.0;
  return std::log(std::max(
      finiteAmplitude, static_cast<double>(TailIntegrityAnalyzer::amplitudeEpsilon)));
}

bool isClosureState(std::uint8_t state) noexcept {
  return state == TailIntegrityAnalyzer::releaseGateState ||
         state == TailIntegrityAnalyzer::closedGateState;
}

} // namespace

void TailIntegrityAnalyzer::prepare(int maxEvents,
                                    int tailWindowSamples) noexcept {
  const int requestedCapacity = std::clamp(maxEvents, 0, maxEventCapacity);
  events_.allocate(requestedCapacity, false);
  eventCapacity_ =
      requestedCapacity > 0 && events_.getData() != nullptr
          ? requestedCapacity
          : 0;
  tailWindowSamples_ =
      std::clamp(tailWindowSamples, 2, maxTailWindowSamples);
  reset();
}

void TailIntegrityAnalyzer::reset() noexcept {
  writeIndex_ = 0;
  eventCount_.store(0, std::memory_order_relaxed);
  liveDecimationCounter_ = 0;
  lastObservedHistoryPushes_ = 0;
  previousGateState_ = closedGateState;
  hasPreviousGateState_ = false;
  retainedChokeScoreSum_ = 0.0;
  retainedMainDecaySum_ = 0.0;
  retainedOutputDecaySum_ = 0.0;
  latestChokeScore_.store(0.0f, std::memory_order_relaxed);
  latestChoked_.store(false, std::memory_order_relaxed);
  meanChokeScore_.store(0.0f, std::memory_order_relaxed);
  meanMainDecayRate_.store(0.0f, std::memory_order_relaxed);
  meanOutputDecayRate_.store(0.0f, std::memory_order_relaxed);
  chokeCount_.store(0, std::memory_order_relaxed);
  cleanTailCount_.store(0, std::memory_order_relaxed);
}

void TailIntegrityAnalyzer::setQualityMode(QualityMode mode) noexcept {
  qualityMode_.store(static_cast<int>(mode), std::memory_order_relaxed);
  liveDecimationCounter_ = 0;
}

QualityMode TailIntegrityAnalyzer::getQualityMode() const noexcept {
  return static_cast<QualityMode>(
      qualityMode_.load(std::memory_order_relaxed));
}

bool TailIntegrityAnalyzer::shouldAnalyzeNextEvent() noexcept {
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

void TailIntegrityAnalyzer::observeTail(const float *main,
                                        const float *output,
                                        int numSamples,
                                        float sustainConfidence,
                                        int closurePoint) noexcept {
  if (main == nullptr || output == nullptr || numSamples < 2 ||
      !shouldAnalyzeNextEvent()) {
    return;
  }

  const float mainRate = fitDecayRate(
      numSamples, [main](int i) noexcept { return amplitudeLog(main[i]); });
  const float outputRate = fitDecayRate(
      numSamples,
      [output](int i) noexcept { return amplitudeLog(output[i]); });
  addEvent(mainRate, outputRate, sustainConfidence, closurePoint);
}

void TailIntegrityAnalyzer::observe(
    const PerformanceHistory &history) noexcept {
  const std::uint64_t totalPushes = history.getStoredPushes();
  if (totalPushes < lastObservedHistoryPushes_) {
    lastObservedHistoryPushes_ = 0;
    hasPreviousGateState_ = false;
  }

  const auto view = history.getView();
  const std::uint64_t delta = totalPushes - lastObservedHistoryPushes_;
  const int newSamples =
      static_cast<int>(std::min<std::uint64_t>(
          delta, static_cast<std::uint64_t>(view.numSamples)));
  lastObservedHistoryPushes_ = totalPushes;

  if (newSamples <= 0 || view.capacity <= 0 || view.gateState.empty()) {
    return;
  }

  const int firstNew = view.numSamples - newSamples;
  for (int logical = firstNew; logical < view.numSamples; ++logical) {
    const int physical = (view.startIndex + logical) % view.capacity;
    const auto physicalOffset = static_cast<std::size_t>(physical);
    const std::uint8_t state = view.gateState[physicalOffset];
    const bool enteredClosure =
        hasPreviousGateState_ && state != previousGateState_ &&
        isClosureState(state);

    previousGateState_ = state;
    hasPreviousGateState_ = true;

    if (!enteredClosure || getQualityMode() == QualityMode::Off ||
        !shouldAnalyzeNextEvent()) {
      continue;
    }

    const int begin = std::max(0, logical - tailWindowSamples_ + 1);
    const int count = logical - begin + 1;
    if (count < 2) {
      continue;
    }

    float sustainSum = 0.0f;
    const float mainRate = fitDecayRate(
        count, [&view, begin](int i) noexcept {
          const int index =
              (view.startIndex + begin + i) % view.capacity;
          const float db =
              view.mainEnvelope[static_cast<std::size_t>(index)];
          return std::isfinite(db)
                     ? static_cast<double>(db) * dbToNaturalLog
                     : std::log(static_cast<double>(
                           TailIntegrityAnalyzer::amplitudeEpsilon));
        });
    const float outputRate = fitDecayRate(
        count, [&view, begin](int i) noexcept {
          const int index =
              (view.startIndex + begin + i) % view.capacity;
          const auto offset = static_cast<std::size_t>(index);
          const float db = view.mainEnvelope[offset];
          const double mainLog =
              std::isfinite(db)
                  ? static_cast<double>(db) * dbToNaturalLog
                  : std::log(static_cast<double>(
                        TailIntegrityAnalyzer::amplitudeEpsilon));
          const float gainValue = view.outputGain[offset];
          const double gain =
              std::isfinite(gainValue)
                  ? std::max(static_cast<double>(gainValue),
                             static_cast<double>(
                                 TailIntegrityAnalyzer::amplitudeEpsilon))
                  : static_cast<double>(
                        TailIntegrityAnalyzer::amplitudeEpsilon);
          return mainLog + std::log(gain);
        });

    for (int i = begin; i <= logical; ++i) {
      const int index = (view.startIndex + i) % view.capacity;
      sustainSum += std::clamp(
          view.sustainConfidence[static_cast<std::size_t>(index)],
          0.0f, 1.0f);
    }
    const float sustain =
        sustainSum / static_cast<float>(count);
    addEvent(mainRate, outputRate, sustain, logical);
  }
}

void TailIntegrityAnalyzer::addEvent(float mainDecayRate,
                                     float outputDecayRate,
                                     float sustainConfidence,
                                     int closurePoint) noexcept {
  if (eventCapacity_ <= 0) {
    return;
  }

  Event event;
  event.mainDecayRate =
      std::isfinite(mainDecayRate) ? std::max(0.0f, mainDecayRate) : 0.0f;
  event.outputDecayRate =
      std::isfinite(outputDecayRate) ? std::max(0.0f, outputDecayRate) : 0.0f;
  event.sustainConfidence =
      std::isfinite(sustainConfidence)
          ? std::clamp(sustainConfidence, 0.0f, 1.0f)
          : 0.0f;
  event.closurePoint = closurePoint;

  const float difference =
      std::max(0.0f, event.outputDecayRate - event.mainDecayRate);
  const float relativeDifference =
      difference / std::max(event.mainDecayRate, minimumDecayRateDifference);
  event.chokeScore = event.sustainConfidence * relativeDifference;
  event.choked =
      event.sustainConfidence >= minimumSustainConfidence &&
      difference >= minimumDecayRateDifference &&
      event.outputDecayRate >=
          event.mainDecayRate * chokeDecayRateRatio &&
      event.chokeScore >= chokeScoreThreshold;

  const int count = eventCount_.load(std::memory_order_relaxed);
  if (count == eventCapacity_) {
    removeFromSummary(events_[writeIndex_]);
  }

  events_[writeIndex_] = event;
  writeIndex_ = (writeIndex_ + 1) % eventCapacity_;
  const int nextCount = std::min(count + 1, eventCapacity_);
  eventCount_.store(nextCount, std::memory_order_relaxed);

  retainedChokeScoreSum_ += event.chokeScore;
  retainedMainDecaySum_ += event.mainDecayRate;
  retainedOutputDecaySum_ += event.outputDecayRate;
  if (event.choked) {
    chokeCount_.fetch_add(1, std::memory_order_relaxed);
  } else {
    cleanTailCount_.fetch_add(1, std::memory_order_relaxed);
  }

  latestChokeScore_.store(event.chokeScore, std::memory_order_relaxed);
  latestChoked_.store(event.choked, std::memory_order_relaxed);
  meanChokeScore_.store(
      static_cast<float>(retainedChokeScoreSum_ / nextCount),
      std::memory_order_relaxed);
  meanMainDecayRate_.store(
      static_cast<float>(retainedMainDecaySum_ / nextCount),
      std::memory_order_relaxed);
  meanOutputDecayRate_.store(
      static_cast<float>(retainedOutputDecaySum_ / nextCount),
      std::memory_order_relaxed);
}

void TailIntegrityAnalyzer::removeFromSummary(
    const Event &event) noexcept {
  retainedChokeScoreSum_ =
      std::max(0.0, retainedChokeScoreSum_ - event.chokeScore);
  retainedMainDecaySum_ =
      std::max(0.0, retainedMainDecaySum_ - event.mainDecayRate);
  retainedOutputDecaySum_ =
      std::max(0.0, retainedOutputDecaySum_ - event.outputDecayRate);
  if (event.choked) {
    chokeCount_.fetch_sub(1, std::memory_order_relaxed);
  } else {
    cleanTailCount_.fetch_sub(1, std::memory_order_relaxed);
  }
}

TailIntegrityAnalyzer::Event
TailIntegrityAnalyzer::getLatestEvent() const noexcept {
  const int count = eventCount_.load(std::memory_order_relaxed);
  if (count <= 0 || eventCapacity_ <= 0) {
    return {};
  }
  const int index = (writeIndex_ + eventCapacity_ - 1) % eventCapacity_;
  return events_[index];
}

int TailIntegrityAnalyzer::copyRecentEvents(
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
