#pragma once

#include "AnalysisFifo.h"

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace ChugGate::Analysis {

// AlignmentScopeData — analysis-side display model for the DI Alignment
// Scope view (spec §20.2). Owns the onset waveform and correlation-curve
// buffers the GUI paints against, plus the scalar recommendation the
// view annotates (selected offset, confidence, offset variation, required
// key delay / main lookahead). The class does not run any cross-
// correlation or onset detection — it consumes snapshots produced
// upstream (e.g. by DSP::AlignmentAnalyzer and the onset-pair ring)
// and stores them in fixed preallocated buffers for painting. It is
// never touched on the audio thread.
//
// Storage layout: three independent fixed-capacity float buffers sized
// in prepare(). The lengths reported by getKeyOnsetLength() etc. track
// the most recent valid write so the GUI can paint exactly the visible
// window without allocating a temp vector. Scalar recommendation fields
// are atomics so the GUI timer can poll them while the analysis thread
// updates them.
//
// Quality gating (QualityMode declared in AnalysisFifo.h):
//   - Off      : set* updates are no-ops. Existing buffers and scalars
//                are preserved so switching back to a live mode resumes
//                seamlessly. setQualityMode(Off) clears data and
//                scalars immediately.
//   - Live /
//     Studio /
//     Diagnose : updates proceed; set* APIs copy into preallocated
//                storage, scalars are written via relaxed atomics.
//                Steady-state update and read paths never allocate.
class AlignmentScopeData {
public:
  // Defaults and clamp limits for the three buffers. Prepare() silently
  // clamps requests into [min, max].
  static constexpr int defaultKeyWindowSamples = 512;
  static constexpr int defaultMainWindowSamples = 512;
  static constexpr int defaultCorrelationLength = 1023;
  static constexpr int minWindowSamples = 32;
  static constexpr int maxWindowSamples = 8192;
  static constexpr int minCorrelationLength = 17;
  static constexpr int maxCorrelationLength = 16385;

  // Scalar recommendation snapshot. The display model stores one of
  // these verbatim; the caller (e.g. an adapter that reads from
  // DSP::AlignmentAnalyzer::Result) populates the fields it has and
  // leaves the rest at default.
  struct Snapshot {
    float offsetMs = 0.0f;
    float confidence = 0.0f;
    float offsetVariationMs = 0.0f;
    float recommendedKeyDelayMs = 0.0f;
    float mainLookaheadMs = 0.0f;
    bool needsMainLookahead = false;
  };

  AlignmentScopeData() noexcept = default;

  // Sizes the three owned buffers. Allocates on the message thread.
  // Calling prepare() again drops and reallocates storage.
  void prepare(int keyWindowSamples = defaultKeyWindowSamples,
               int mainWindowSamples = defaultMainWindowSamples,
               int correlationLength = defaultCorrelationLength) noexcept;

  // Zero every owned buffer and reset all scalars/lengths. Storage is
  // preserved. Safe on the message thread.
  void reset() noexcept;

  // Quality mode gates set* updates. Switching to Off clears all owned
  // data and resets the scalar recommendation so the next paint shows
  // an empty scope and zeroed-out annotation.
  void setQualityMode(QualityMode mode) noexcept;
  [[nodiscard]] QualityMode getQualityMode() const noexcept;

  // Onset waveform copies. count is clipped to the corresponding
  // capacity. No-op when quality mode is Off.
  void setKeyOnsetWaveform(const float *samples, int count) noexcept;
  void setMainOnsetWaveform(const float *samples, int count) noexcept;
  // Convenience: copy both onset windows in a single call. Each count
  // is independently clipped. No-op when quality mode is Off.
  void setOnsetWaveforms(const float *key, int keyCount, const float *main,
                         int mainCount) noexcept;

  // Correlation curve copy. count is clipped to the correlation
  // capacity. No-op when quality mode is Off.
  void setCorrelationCurve(const float *samples, int count) noexcept;

  // Scalar recommendation. Both overloads are no-ops when quality mode
  // is Off.
  void setResult(const Snapshot &snapshot) noexcept;
  void setResult(float offsetMs, float confidence, float offsetVariationMs,
                 float recommendedKeyDelayMs, float mainLookaheadMs,
                 bool needsMainLookahead) noexcept;

  // Zero-copy read access. Pointers are valid until the next
  // prepare()/reset(); capacities and lengths are stable across reads.
  [[nodiscard]] const float *getKeyOnsetBuffer() const noexcept;
  [[nodiscard]] const float *getMainOnsetBuffer() const noexcept;
  [[nodiscard]] const float *getCorrelationBuffer() const noexcept;
  [[nodiscard]] std::span<const float> getKeyOnsetSpan() const noexcept;
  [[nodiscard]] std::span<const float> getMainOnsetSpan() const noexcept;
  [[nodiscard]] std::span<const float> getCorrelationSpan() const noexcept;

  [[nodiscard]] int getKeyOnsetCapacity() const noexcept {
    return keyOnsetCapacity_;
  }
  [[nodiscard]] int getMainOnsetCapacity() const noexcept {
    return mainOnsetCapacity_;
  }
  [[nodiscard]] int getCorrelationCapacity() const noexcept {
    return correlationCapacity_;
  }
  // Length tracks the most recent valid write (≤ capacity). Atomic so
  // the GUI timer can race the analysis thread without a mutex.
  [[nodiscard]] int getKeyOnsetLength() const noexcept {
    return keyOnsetLength_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getMainOnsetLength() const noexcept {
    return mainOnsetLength_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getCorrelationLength() const noexcept {
    return correlationLength_.load(std::memory_order_relaxed);
  }

  // Atomic scalar accessors — analysis-thread writes, GUI-thread reads.
  [[nodiscard]] float getOffsetMs() const noexcept {
    return offsetMs_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getConfidence() const noexcept {
    return confidence_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getOffsetVariationMs() const noexcept {
    return offsetVariationMs_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getRecommendedKeyDelayMs() const noexcept {
    return recommendedKeyDelayMs_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] float getMainLookaheadMs() const noexcept {
    return mainLookaheadMs_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool needsMainLookahead() const noexcept {
    return needsMainLookahead_.load(std::memory_order_relaxed);
  }

  // Caller-owned copy helpers (copyRecent-style). dst may be nullptr
  // (skipped). Returns the number of samples written — equal to the
  // smaller of the current length and dstCapacity. Safe on the
  // message/GUI thread; never allocates.
  int copyKeyOnset(float *dst, int dstCapacity) const noexcept;
  int copyMainOnset(float *dst, int dstCapacity) const noexcept;
  int copyCorrelationCurve(float *dst, int dstCapacity) const noexcept;

private:
  std::vector<float> keyOnset_;
  std::vector<float> mainOnset_;
  std::vector<float> correlation_;

  int keyOnsetCapacity_ = 0;
  int mainOnsetCapacity_ = 0;
  int correlationCapacity_ = 0;

  std::atomic<int> keyOnsetLength_{0};
  std::atomic<int> mainOnsetLength_{0};
  std::atomic<int> correlationLength_{0};

  std::atomic<int> qualityMode_{static_cast<int>(QualityMode::Studio)};

  // Scalar recommendation.
  std::atomic<float> offsetMs_{0.0f};
  std::atomic<float> confidence_{0.0f};
  std::atomic<float> offsetVariationMs_{0.0f};
  std::atomic<float> recommendedKeyDelayMs_{0.0f};
  std::atomic<float> mainLookaheadMs_{0.0f};
  std::atomic<bool> needsMainLookahead_{false};
};

} // namespace ChugGate::Analysis