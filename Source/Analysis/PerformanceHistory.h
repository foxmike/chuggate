#pragma once

#include "AnalysisFifo.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ChugGate::Analysis {

// PerformanceHistory — fixed-memory ring buffer of timeline samples backing the
// Performance Timeline visualization (spec §20.1).
//
// Slot rate: one slot per pushFromFrame / push call, gated by a fixed
// control-rate "history Hz" (default 100 Hz, configurable in prepare). This is
// the rate at which the message-thread consumer (Timer/AsyncUpdater draining
// AnalysisFifo) is allowed to write to history, independent of audio block
// size. The audio thread itself never touches this class.
//
// Quality mode behaviour (see AnalysisFifo::QualityMode, defined in h.1):
//   - Off      : all pushes are no-ops; storage and indices are preserved so
//                switching back to a live mode resumes seamlessly.
//   - Live     : 2:1 decimation — every other push is stored, so the visible
//                window covers the requested duration with half the slot
//                density. Reduces slot writes on slower paint rates.
//   - Studio   : 1:1 decimation — every push is stored. Full slot density.
//   - Diagnose : 1:1 decimation; alignmentOffset and lookaheadRegion series
//                are populated when the AnalysisFrame exposes them.
//
// Memory: every series buffer is sized in prepare() to ceil(maxDurationSeconds
// * controlRateHz) and only re-grown by setDurationSeconds() on the message
// thread. The hot push path never allocates.
class PerformanceHistory {
public:
  // Per-series "view" returned by getView() — non-owning spans over the
  // ring storage. The valid samples are numSamples consecutive logical slots
  // starting at startIndex (oldest first). The caller can either paint in
  // place (handling the wrap-around at startIndex + numSamples) or call
  // copyRecent() to obtain a contiguous block.
  struct TimelineView {
    int numSamples = 0;
    int capacity = 0;
    int startIndex = 0; // oldest valid sample index in the ring

    std::span<const float> keyEnvelope;
    std::span<const float> mainEnvelope;
    std::span<const float> thresholdOpen;
    std::span<const float> thresholdClose;
    std::span<const float> noiseFloor;
    std::span<const float> outputGain;
    std::span<const float> transientMarker;
    std::span<const float> sustainConfidence;
    std::span<const float> alignmentOffset; // empty when not used
    std::span<const uint8_t> gateState;
    std::span<const uint8_t> lookaheadRegion; // empty when not used
  };

  // Caller-owned output buffers used by copyRecent(). Any pointer that is
  // nullptr is skipped. Capacity is in samples; the most recent samples are
  // copied when the requested capacity is smaller than getNumSamples().
  struct CopyBuffers {
    float *keyEnvelope = nullptr;
    float *mainEnvelope = nullptr;
    float *thresholdOpen = nullptr;
    float *thresholdClose = nullptr;
    float *noiseFloor = nullptr;
    float *outputGain = nullptr;
    float *transientMarker = nullptr;
    float *sustainConfidence = nullptr;
    float *alignmentOffset = nullptr;
    uint8_t *gateState = nullptr;
    uint8_t *lookaheadRegion = nullptr;
  };

  PerformanceHistory() noexcept = default;

  // prepare(controlRateHz, maxDurationSeconds, numChannels) — sizing pass.
  // Storage is sized for the *maximum* duration at the chosen control rate,
  // so subsequent setDurationSeconds() calls only adjust the valid window
  // (or reallocate down, never up beyond maxDurationSeconds * controlRateHz).
  // numChannels is accepted for API symmetry with multi-channel timeline
  // consumers; envelope series are currently mono (main + key), as are the
  // gate-state and threshold lanes, matching the spec §20.1 visualization.
  void prepare(double controlRateHz, double maxDurationSeconds = 30.0,
               int numChannels = 0) noexcept;

  // Sizing pass accepting an explicit config struct (use either form).
  struct Config {
    double controlRateHz = 100.0;
    double minDurationSeconds = 2.0;
    double maxDurationSeconds = 30.0;
    double initialDurationSeconds = 10.0;
    int numChannels = 0;
  };
  void prepare(const Config &cfg) noexcept;

  // Reset indices and counters. Does not free storage; safe to call between
  // songs / preset changes on the message thread.
  void reset() noexcept;

  // Quality mode controls whether pushes are stored and at what decimation.
  // Safe to call from the message or UI thread; reads on the message thread
  // are coherent on every platform we target.
  void setQualityMode(QualityMode mode) noexcept;
  [[nodiscard]] QualityMode getQualityMode() const noexcept;

  // Duration in [minDurationSeconds, maxDurationSeconds]. Decreasing the
  // duration may trim the valid window; increasing it (up to the maximum
  // prepared for) reuses existing storage. May reallocate when growing past
  // the previously allocated capacity — only safe on the message thread.
  void setDurationSeconds(double seconds) noexcept;
  [[nodiscard]] double getDurationSeconds() const noexcept {
    return durationSeconds_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] double getMinDurationSeconds() const noexcept {
    return minDurationSeconds_;
  }
  [[nodiscard]] double getMaxDurationSeconds() const noexcept {
    return maxDurationSeconds_;
  }
  [[nodiscard]] double getControlRateHz() const noexcept {
    return controlRateHz_;
  }
  [[nodiscard]] int getNumChannels() const noexcept { return numChannels_; }

  [[nodiscard]] int getCapacity() const noexcept { return capacity_; }
  [[nodiscard]] int getNumSamples() const noexcept {
    return numValid_.load(std::memory_order_relaxed);
  }

  // Push the canonical timeline fields from an AnalysisFrame. Decimates per
  // the active QualityMode (Off = no-op, Live = 2:1). Always returns the
  // number of slots actually written (0 or 1).
  int pushFromFrame(const AnalysisFrame &frame) noexcept;

  // Push individual scalars. Same gating/decimation as pushFromFrame. The
  // alignmentOffset and lookaheadRegion parameters are only used when the
  // active QualityMode is Diagnose and the frame structure previously
  // exposed those fields; they default to zero/false for compatibility.
  int push(float keyEnvelope, float mainEnvelope, float thresholdOpen,
           float thresholdClose, float noiseFloor, uint8_t gateState,
           float outputGain, float transientMarker, float sustainConfidence,
           float alignmentOffset = 0.0f, bool lookaheadRegion = false) noexcept;

  // Zero-copy read. Returns non-owning spans into the preallocated storage.
  // The caller may inspect startIndex/numSamples to handle wrap-around when
  // painting.
  [[nodiscard]] TimelineView getView() const noexcept;

  // Copy the most-recent up-to-dstCapacity samples into caller-owned buffers
  // (see CopyBuffers). nullptr pointers are skipped. Returns the number of
  // samples written into each non-nullptr buffer.
  int copyRecent(CopyBuffers &buffers, int dstCapacity) const noexcept;

  // Diagnostic counters (for instrumentation in the Diagnose view).
  [[nodiscard]] std::uint64_t getTotalPushes() const noexcept {
    return totalPushes_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getStoredPushes() const noexcept {
    return storedPushes_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getDroppedFromQualityMode() const noexcept {
    return droppedByMode_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t getOverwriteCount() const noexcept {
    return overwriteCount_.load(std::memory_order_relaxed);
  }

private:
  // Compute capacity for a given duration at the current control rate.
  [[nodiscard]] int capacityForSeconds(double seconds) const noexcept;

  // Resize all series vectors to exactly newCapacity slots. Preserves
  // existing data on a reallocation by copying the in-order contents. Only
  // callable from the message thread.
  void reallocateStorage(int newCapacity) noexcept;

  // Returns true if a push should actually be stored given the current
  // QualityMode and Live decimation counter.
  [[nodiscard]] bool shouldStoreNextPush() noexcept;

  // Storage. All vectors are sized in prepare() to the maximum capacity
  // (or shrunk by setDurationSeconds down to the requested capacity).
  std::vector<float> keyEnv_;
  std::vector<float> mainEnv_;
  std::vector<float> threshOpen_;
  std::vector<float> threshClose_;
  std::vector<float> noiseFloor_;
  std::vector<float> outputGain_;
  std::vector<float> transient_;
  std::vector<float> sustainConf_;
  std::vector<float> alignment_;
  std::vector<std::uint8_t> gateState_;
  std::vector<std::uint8_t> lookahead_;

  int capacity_ = 0; // physical slot count
  int numChannels_ = 0;

  // Ring state. Both writer and reader are the message thread; the
  // atomics are only used so that getters are safe when called from the
  // message thread while a setter (e.g. setQualityMode) is called from
  // the UI thread.
  std::atomic<int> writeIndex_{0};
  std::atomic<int> numValid_{0};

  // Live-mode decimation counter — wraps modulo 2.
  int liveDecimCounter_ = 0;

  // Sizing parameters. Set in prepare(), constant thereafter.
  double controlRateHz_ = 0.0;
  double minDurationSeconds_ = 2.0;
  double maxDurationSeconds_ = 30.0;
  std::atomic<double> durationSeconds_{0.0};

  // Optional series — populated only when QualityMode is Diagnose *and*
  // the AnalysisFrame source actually exposes the fields. The flags
  // default to false (the empty spans returned by getView() make the
  // absence explicit to the GUI).
  bool hasAlignment_ = false;
  bool hasLookahead_ = false;

  std::atomic<int> qualityMode_{static_cast<int>(QualityMode::Studio)};
  std::atomic<std::uint64_t> totalPushes_{0};
  std::atomic<std::uint64_t> storedPushes_{0};
  std::atomic<std::uint64_t> droppedByMode_{0};
  std::atomic<std::uint64_t> overwriteCount_{0};
};

} // namespace ChugGate::Analysis
