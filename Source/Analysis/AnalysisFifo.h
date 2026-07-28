#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace ChugGate::Analysis {

// Controls the amount of visual analysis work performed by the audio-thread
// producer. Push-rate decimation for Live mode is intentionally left to the
// producer, which can avoid constructing frames that will not be displayed.
enum class QualityMode : int {
  Off = 0,
  Live,
  Studio,
  Diagnose
};

// One bounded, allocation-free snapshot of the values used by the visual
// diagnostic system. These are linked/summary values rather than per-channel
// sample buffers, so they remain scalar for mono and stereo configurations.
//
// transientMarker is retained as the timeline-facing name for
// transientStrength. The two names alias the same storage.
struct AnalysisFrame {
  float keyEnvelope = 0.0f;
  float mainEnvelope = 0.0f;
  float thresholdOpen = 0.0f;
  float thresholdClose = 0.0f;
  float noiseFloor = 0.0f;
  int gateState = 0;
  float outputGain = 1.0f;
  bool transient = false;
  union {
    float transientStrength = 0.0f;
    float transientMarker;
  };
  float sustainConfidence = 0.0f;

  // Optional Diagnose timeline lanes. Keeping them in the same trivial
  // snapshot avoids a second audio-to-analysis transport.
  float alignmentOffset = 0.0f;
  bool hasAlignmentOffset = false;
  bool lookaheadRegion = false;
  bool hasLookaheadRegion = false;
};

static_assert(std::is_trivially_copyable_v<AnalysisFrame>);
static_assert(std::is_standard_layout_v<AnalysisFrame>);

// Lock-free single-producer/single-consumer transport from the audio thread to
// the analysis/message thread. prepare() and reset() must not overlap either
// hot-path operation.
class AnalysisFifo {
public:
  AnalysisFifo() noexcept = default;

  // Allocates capacity for exactly maxFrames readable frames. AbstractFifo
  // reserves one internal slot, so the backing allocation contains one extra
  // element. Call only during a non-realtime prepare pass.
  void prepare(int maxFrames, int numChannels) noexcept;

  // Empties the queue and clears its overload diagnostic without releasing
  // storage.
  void reset() noexcept;

  // Audio-thread producer. Returns false when analysis is Off or the queue is
  // full. Only a full queue increments the drop counter.
  [[nodiscard]] bool tryPush(const AnalysisFrame &frame) noexcept;

  // Analysis/message-thread consumer. Returns false when no frame is ready.
  [[nodiscard]] bool tryPop(AnalysisFrame &frame) noexcept;

  void setQualityMode(QualityMode mode) noexcept;
  [[nodiscard]] QualityMode getQualityMode() const noexcept;

  [[nodiscard]] std::uint64_t getDropCount() const noexcept;
  [[nodiscard]] int getCapacity() const noexcept { return capacity_; }
  [[nodiscard]] int getNumChannels() const noexcept { return numChannels_; }

private:
  juce::AbstractFifo fifo_{1};
  juce::HeapBlock<AnalysisFrame> storage_;

  int capacity_ = 0;
  int numChannels_ = 0;

  std::atomic<int> qualityMode_{static_cast<int>(QualityMode::Studio)};
  std::atomic<std::uint64_t> dropCount_{0};
};

} // namespace ChugGate::Analysis
