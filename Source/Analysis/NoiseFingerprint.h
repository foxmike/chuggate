#pragma once

#include "AnalysisFifo.h"

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace ChugGate::Analysis {

// NoiseFingerprint — analysis-side display model for the Noise Fingerprint
// view (spec §20.3). Owns the time-averaged spectral buffers the Diagnose
// GUI paints against. The class does not own any FFT, learn, or threshold
// DSP — it consumes snapshots produced upstream (e.g. by
// DSP::NoiseFloorEstimator + DSP::PowerlineShield + DSP::HissCurtain +
// DSP::MultibandGate) and stores them in fixed preallocated storage for
// painting. It is never touched on the audio thread.
//
// Bin layout: linear-spaced magnitude bins from 0 Hz to Nyquist
// (sampleRate / 2). binCenterHz(b) = (b + 0.5) * sampleRate / (2 * numBins).
// binForFrequency(hz) = clamp(int(hz * 2 * numBins / sampleRate), 0, numBins).
// The DSP side is free to resample learned spectra into numBins before
// handing them off; the display model trusts the caller.
//
// Quality gating (QualityMode declared in AnalysisFifo.h):
//   - Off      : all set* updates are no-ops. Existing data is preserved
//                so switching back to a live mode resumes seamlessly.
//                setQualityMode(Off) clears existing data immediately.
//   - Live /
//     Studio /
//     Diagnose : updates proceed. setSpectrum/updateFromLearnedProfile copy
//                into preallocated buffers; the steady-state update and
//                read paths never allocate.
//
// Memory: every spectrum/filter response/peak-marker buffer is sized in
// prepare() and never resized on the steady-state update or read paths.
// Storage is owned via std::vector<float> so that re-prepare() can resize
// cleanly; the hot update path uses memcpy into the already-sized vector
// storage.
class NoiseFingerprint {
public:
  // Default bin count and clamp limits. Prepare will silently clamp a
  // requested numBins to [minNumBins, maxNumBins].
  static constexpr int defaultNumBins = 128;
  static constexpr int minNumBins = 16;
  static constexpr int maxNumBins = 1024;
  static constexpr int maxMultibandThresholds = 4;

  // Input snapshot — the data a non-audio worker hands to the display
  // model after it has finished a learn or recompute pass. nullptr fields
  // (or count == 0) leave the corresponding buffer unchanged so partial
  // updates are valid. sampleRate is used by the display model for its
  // frequency axis; 0 disables any bin→Hz mapping and leaves the caller
  // responsible for the bin indices of the peak markers.
  struct LearnedProfile {
    // Per-bin median floor in dB. Count must match numBins when non-null.
    const float *medianFloor = nullptr;
    // Per-bin upper percentile in dB. Optional.
    const float *upperPercentile = nullptr;
    int numBins = 0;
    // Sample rate at which the spectra were binned. Determines the
    // frequency axis the GUI renders. 0 disables frequency mapping.
    double sampleRate = 0.0;
    // Feature flags from the learn / analyse pass.
    bool humFiftyHz = false;
    bool humSixtyHz = false;
    bool hasHumHarmonics = false;
    bool hasHighFrequencyHiss = false;
    bool hasIntermittentBursts = false;
    // Bin indices where the GUI should overlay peak markers. -1 means
    // "no marker". Caller is responsible for mapping from frequency to
    // bin if it doesn't already operate in bin space.
    int humPeakBin = -1;
    int harmonicPeakBin = -1;
    int hissPeakBin = -1;
    // Optional detector filter response (one scalar per bin). The GUI
    // renders this as a secondary curve overlaid on the noise floor.
    const float *detectorFilterResponse = nullptr;
    int detectorFilterLength = 0;
    // Optional multiband open-threshold lane. The GUI renders one tick
    // per entry against the magnitude axis.
    const float *multibandThresholds = nullptr;
    int numMultibandThresholds = 0;
  };

  NoiseFingerprint() noexcept = default;

  // Sizes every owned buffer to numBins (clamped) and remembers the
  // sample rate for the bin→Hz mapping. Allocates on the message thread.
  // Calling prepare() again drops and reallocates storage; the GUI should
  // expect the buffers to be reset to zero on re-prepare.
  void prepare(double sampleRate, int numBins = defaultNumBins) noexcept;

  // Zero all owned buffers and reset every flag. Storage is preserved.
  // Safe on the message thread.
  void reset() noexcept;

  // Quality mode gates set* updates. Switching to Off clears all owned
  // data so the next paint shows an empty background. Switching out of
  // Off leaves the cleared state; the next setSpectrum/updateFromLearned
  // will repopulate it.
  void setQualityMode(QualityMode mode) noexcept;
  [[nodiscard]] QualityMode getQualityMode() const noexcept;

  // Snapshot copy. No-op when quality mode is Off (preserves existing
  // data). Any nullptr or zero-length field leaves the corresponding
  // internal buffer unchanged. The peaks are stored verbatim (the caller
  // has already mapped from Hz to bin if it cares).
  void updateFromLearnedProfile(const LearnedProfile &profile) noexcept;

  // Direct spectrum replacement. count is clipped to getNumBins().
  // No-op when quality mode is Off.
  void setSpectrum(const float *medianFloor, const float *upperPercentile,
                   int count) noexcept;

  // Multiband thresholds can be set independently (e.g. when threshold
  // recompute happens separately from full learn). Count is clipped to
  // [0, maxMultibandThresholds]. No-op when quality mode is Off.
  void setMultibandThresholds(const float *thresholds, int count) noexcept;

  // Detector filter response curve. Length is clipped to getNumBins().
  // No-op when quality mode is Off.
  void setDetectorFilterResponse(const float *response, int count) noexcept;

  // Zero-copy read access for the GUI. Pointers are valid until the
  // next prepare()/reset(); lengths are stable across calls.
  [[nodiscard]] const float *getMedianFloor() const noexcept;
  [[nodiscard]] const float *getUpperPercentile() const noexcept;
  [[nodiscard]] const float *getDetectorFilterResponse() const noexcept;
  [[nodiscard]] const float *getMultibandThresholds() const noexcept;
  [[nodiscard]] std::span<const float> getMedianFloorSpan() const noexcept;
  [[nodiscard]] std::span<const float> getUpperPercentileSpan() const noexcept;
  [[nodiscard]] std::span<const float> getDetectorFilterResponseSpan()
      const noexcept;
  [[nodiscard]] std::span<const float> getMultibandThresholdsSpan() const
      noexcept;

  [[nodiscard]] int getNumBins() const noexcept { return numBins_; }
  [[nodiscard]] int getNumMultibandThresholds() const noexcept {
    return numMultibandThresholds_;
  }
  [[nodiscard]] double getSampleRate() const noexcept {
    return sampleRate_;
  }

  // Race-safe flag accessors. The analysis/worker thread writes via
  // updateFromLearnedProfile; the GUI timer reads via these accessors.
  // Atomic relaxed ordering is sufficient because the values are scalar
  // status flags (no cross-field invariants).
  [[nodiscard]] bool hasFiftyHzHum() const noexcept {
    return humFiftyHz_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool hasSixtyHzHum() const noexcept {
    return humSixtyHz_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool hasHumHarmonics() const noexcept {
    return humHarmonics_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool hasHighFrequencyHiss() const noexcept {
    return hfHiss_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] bool hasIntermittentBursts() const noexcept {
    return intermittentBursts_.load(std::memory_order_relaxed);
  }

  // Bin indices of the peak markers, or -1 when no marker should be
  // drawn.
  [[nodiscard]] int getHumPeakBin() const noexcept {
    return humPeakBin_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getHarmonicPeakBin() const noexcept {
    return harmonicPeakBin_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] int getHissPeakBin() const noexcept {
    return hissPeakBin_.load(std::memory_order_relaxed);
  }

  // Frequency-axis helpers. Linear-spaced bins across [0, sampleRate/2).
  // Returns 0 when the sample rate is 0.
  [[nodiscard]] float binCenterHz(int bin) const noexcept;
  [[nodiscard]] int binForFrequency(float hz) const noexcept;

  // Caller-owned copy helpers. dst may be nullptr (skipped). Returns the
  // number of bins written (= min(numBins, dstCapacity)). Safe on the
  // message/GUI thread; never allocates.
  int copyMedianFloor(float *dst, int dstCapacity) const noexcept;
  int copyUpperPercentile(float *dst, int dstCapacity) const noexcept;
  int copyDetectorFilterResponse(float *dst, int dstCapacity) const noexcept;
  int copyMultibandThresholds(float *dst, int dstCapacity) const noexcept;

private:
  std::vector<float> medianFloor_;
  std::vector<float> upperPercentile_;
  std::vector<float> detectorFilterResponse_;
  std::vector<float> multibandThresholds_;

  int numBins_ = 0;
  int numMultibandThresholds_ = 0;
  double sampleRate_ = 0.0;

  std::atomic<int> qualityMode_{static_cast<int>(QualityMode::Studio)};

  // Feature flags (set via updateFromLearnedProfile).
  std::atomic<bool> humFiftyHz_{false};
  std::atomic<bool> humSixtyHz_{false};
  std::atomic<bool> humHarmonics_{false};
  std::atomic<bool> hfHiss_{false};
  std::atomic<bool> intermittentBursts_{false};

  // Peak bin markers (-1 = none).
  std::atomic<int> humPeakBin_{-1};
  std::atomic<int> harmonicPeakBin_{-1};
  std::atomic<int> hissPeakBin_{-1};
};

} // namespace ChugGate::Analysis