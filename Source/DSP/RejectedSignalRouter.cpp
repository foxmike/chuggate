#include "RejectedSignalRouter.h"
#include <algorithm>

namespace ChugGate::DSP {

void RejectedSignalRouter::prepare(double sampleRate) noexcept {
  if (sampleRate > 0.0) {
    sampleRate_ = sampleRate;
  }
  reset();
}

void RejectedSignalRouter::reset() noexcept {
  // Clean up or reset any internal states
}

void RejectedSignalRouter::setRejectedMode(int mode) noexcept {
  if (mode >= 0 && mode <= static_cast<int>(Mode::Key)) {
    setRejectedMode(static_cast<Mode>(mode));
  }
}

void RejectedSignalRouter::setRejectedMode(Mode mode) noexcept {
  rejectedMode_.store(mode, std::memory_order_relaxed);
}

void RejectedSignalRouter::setStateFilterMode(int mode) noexcept {
  if (mode >= 0 && mode <= static_cast<int>(StateFilterMode::Closed)) {
    setStateFilterMode(static_cast<StateFilterMode>(mode));
  }
}

void RejectedSignalRouter::setStateFilterMode(StateFilterMode mode) noexcept {
  stateFilterMode_.store(mode, std::memory_order_relaxed);
}

void RejectedSignalRouter::setPerformanceState(PerformanceStateMachine::State state) noexcept {
  currentState_.store(state, std::memory_order_relaxed);
}

bool RejectedSignalRouter::isStateFilterMatching() const noexcept {
  const StateFilterMode filter = stateFilterMode_.load(std::memory_order_relaxed);
  if (filter == StateFilterMode::Off) {
    return true;
  }
  const PerformanceStateMachine::State state = currentState_.load(std::memory_order_relaxed);
  switch (filter) {
    case StateFilterMode::Hold:
      return state == PerformanceStateMachine::State::Hold;
    case StateFilterMode::Sustain:
      return state == PerformanceStateMachine::State::Sustain;
    case StateFilterMode::Release:
      return state == PerformanceStateMachine::State::Release;
    case StateFilterMode::Closed:
      return state == PerformanceStateMachine::State::Closed;
    default:
      break;
  }
  return false;
}

void RejectedSignalRouter::process(const ProcessArgs& args, float* auditionBuffer) noexcept {
  if (args.numSamples <= 0 || auditionBuffer == nullptr) {
    return;
  }

  const Mode mode = rejectedMode_.load(std::memory_order_relaxed);
  if (mode == Mode::Off || !isStateFilterMatching()) {
    std::fill_n(auditionBuffer, args.numSamples, 0.0f);
    return;
  }

  for (int i = 0; i < args.numSamples; ++i) {
    float r = 0.0f;
    const float x = args.dryMain ? args.dryMain[i] : 0.0f;
    const float y = args.wetMain ? args.wetMain[i] : 0.0f;

    switch (mode) {
      case Mode::All: {
        if (args.rejectLow || args.rejectMid || args.rejectHigh) {
          const float rL = args.rejectLow ? args.rejectLow[i] : 0.0f;
          const float rM = args.rejectMid ? args.rejectMid[i] : 0.0f;
          const float rH = args.rejectHigh ? args.rejectHigh[i] : 0.0f;
          r = rL + rM + rH;
        } else if (args.dryLow || args.wetLow || args.dryMid || args.wetMid || args.dryHigh || args.wetHigh) {
          const float rL = (args.dryLow && args.wetLow) ? (args.dryLow[i] - args.wetLow[i]) : 0.0f;
          const float rM = (args.dryMid && args.wetMid) ? (args.dryMid[i] - args.wetMid[i]) : 0.0f;
          const float rH = (args.dryHigh && args.wetHigh) ? (args.dryHigh[i] - args.wetHigh[i]) : 0.0f;
          r = rL + rM + rH;
        } else {
          r = x - y;
        }
        break;
      }
      case Mode::FullBand: {
        r = x - y;
        break;
      }
      case Mode::Weight: {
        if (args.rejectLow) {
          r = args.rejectLow[i];
        } else if (args.dryLow && args.wetLow) {
          r = args.dryLow[i] - args.wetLow[i];
        } else {
          r = x - y; // Fallback to full-band if low band is not provided
        }
        break;
      }
      case Mode::Voice: {
        if (args.rejectMid) {
          r = args.rejectMid[i];
        } else if (args.dryMid && args.wetMid) {
          r = args.dryMid[i] - args.wetMid[i];
        } else {
          r = 0.0f;
        }
        break;
      }
      case Mode::Hiss: {
        if (args.rejectHigh) {
          r = args.rejectHigh[i];
        } else if (args.dryHigh && args.wetHigh) {
          r = args.dryHigh[i] - args.wetHigh[i];
        } else {
          r = 0.0f;
        }
        break;
      }
      case Mode::HissCurtain: {
        if (args.hissCurtainReject) {
          r = args.hissCurtainReject[i];
        } else {
          r = 0.0f;
        }
        break;
      }
      case Mode::NoiseModel: {
        if (args.noiseModelReject) {
          r = args.noiseModelReject[i];
        } else {
          r = 0.0f;
        }
        break;
      }
      case Mode::Key: {
        if (args.detectorKey) {
          r = args.detectorKey[i];
        } else {
          r = 0.0f;
        }
        break;
      }
      default:
        r = 0.0f;
        break;
    }
    auditionBuffer[i] = r;
  }
}

void RejectedSignalRouter::process(const float* dryMain, const float* wetMain, float* auditionBuffer, int numSamples) noexcept {
  ProcessArgs args;
  args.dryMain = dryMain;
  args.wetMain = wetMain;
  args.numSamples = numSamples;
  process(args, auditionBuffer);
}

} // namespace ChugGate::DSP
