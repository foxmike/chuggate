#pragma once

#include <atomic>
#include <cstdint>
#include "PerformanceStateMachine.h"

namespace ChugGate::DSP {

class RejectedSignalRouter {
public:
  enum class Mode : std::uint8_t {
    Off = 0,
    All = 1,
    AllRejected = 1,
    FullBand = 2,
    Weight = 3,
    Voice = 4,
    Hiss = 5,
    HissCurtain = 6,
    NoiseModel = 7,
    Key = 8,
    DetectorKey = 8
  };

  enum class StateFilterMode : std::uint8_t {
    Off = 0,
    Hold = 1,
    Sustain = 2,
    Release = 3,
    Closed = 4
  };

  struct ProcessArgs {
    const float* dryMain = nullptr;   // x[n]
    const float* wetMain = nullptr;   // y[n]
    int numSamples = 0;

    // Optional per-band dry/wet buffers
    const float* dryLow = nullptr;
    const float* wetLow = nullptr;
    const float* dryMid = nullptr;
    const float* wetMid = nullptr;
    const float* dryHigh = nullptr;
    const float* wetHigh = nullptr;

    // Optional per-band reject buffers (if precalculated)
    const float* rejectLow = nullptr;
    const float* rejectMid = nullptr;
    const float* rejectHigh = nullptr;

    // Optional additional inputs
    const float* hissCurtainReject = nullptr;
    const float* noiseModelReject = nullptr;
    const float* detectorKey = nullptr;
  };

  RejectedSignalRouter() noexcept = default;

  void prepare(double sampleRate) noexcept;
  void reset() noexcept;

  /**
   * Processes the input dry/wet and optional band signals to fill the audition buffer.
   */
  void process(const ProcessArgs& args, float* auditionBuffer) noexcept;

  /**
   * Simple process path for direct full-band audition testing.
   */
  void process(const float* dryMain, const float* wetMain, float* auditionBuffer, int numSamples) noexcept;

  // Setters for configuration
  void setRejectedMode(int mode) noexcept;
  void setRejectedMode(Mode mode) noexcept;

  void setStateFilterMode(int mode) noexcept;
  void setStateFilterMode(StateFilterMode mode) noexcept;

  void setPerformanceState(PerformanceStateMachine::State state) noexcept;

  // Getters
  [[nodiscard]] Mode getRejectedMode() const noexcept { return rejectedMode_; }
  [[nodiscard]] StateFilterMode getStateFilterMode() const noexcept { return stateFilterMode_; }
  [[nodiscard]] PerformanceStateMachine::State getPerformanceState() const noexcept { return currentState_; }

private:
  [[nodiscard]] bool isStateFilterMatching() const noexcept;

  double sampleRate_ = 48000.0;
  std::atomic<Mode> rejectedMode_{Mode::Off};
  std::atomic<StateFilterMode> stateFilterMode_{StateFilterMode::Off};
  std::atomic<PerformanceStateMachine::State> currentState_{PerformanceStateMachine::State::Closed};
};

} // namespace ChugGate::DSP
