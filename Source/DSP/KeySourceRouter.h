#pragma once

namespace ChugGate::DSP {

class KeySourceRouter {
public:
  enum class Source {
    Main,
    ExternalSidechain,
    BlendedMainExternal,
    Left,
    Right,
    StereoMax,
    StereoAverage,
    Mid,
    Side
  };

  enum class FallbackMode { InternalMain, HoldState, CloseGate };

  struct Status {
    bool externalPresent = false;
    bool externalSilent = false;
    bool channelMismatch = false;
    bool lowConfidence = false;
    bool internalFallbackActive = false;
    bool alignmentNotCalibrated = true;
  };

  void prepare(double sampleRate, int maxBlockSize, int numMainChannels,
               int numSidechainChannels) noexcept;
  void reset() noexcept;

  void setSource(Source source) noexcept { source_ = source; }
  void setExternalBlend(float blend) noexcept;
  void setFallbackMode(FallbackMode mode) noexcept { fallbackMode_ = mode; }
  void setAlignmentCalibrated(bool calibrated) noexcept {
    alignmentCalibrated_ = calibrated;
  }

  [[nodiscard]] Source getSource() const noexcept { return source_; }
  [[nodiscard]] float getExternalBlend() const noexcept {
    return externalBlend_;
  }
  [[nodiscard]] FallbackMode getFallbackMode() const noexcept {
    return fallbackMode_;
  }
  [[nodiscard]] const Status &getStatus() const noexcept { return status_; }

  // All buffers are caller-owned. Channel arrays and individual channel
  // pointers may be null; such inputs are reported through Status rather than
  // dereferenced. The output may not alias an input channel.
  void process(const float *const *mainChannels, int numMainChannels,
               const float *const *sidechainChannels, int numSidechainChannels,
               float *monoKey, int numSamples) noexcept;

  void process(const float *const *mainChannels, int numMainChannels,
               const float *const *sidechainChannels, int numSidechainChannels,
               float *monoKey, int numSamples, Source source,
               float externalBlend, FallbackMode fallbackMode) noexcept;

  void processBlock(const float *const *mainChannels, int numMainChannels,
                    const float *const *sidechainChannels,
                    int numSidechainChannels, float *monoKey,
                    int numSamples) noexcept {
    process(mainChannels, numMainChannels, sidechainChannels,
            numSidechainChannels, monoKey, numSamples);
  }

  // Blends already-computed envelopes. This is also the operation used by the
  // BlendedMainExternal source after deriving an envelope from each path.
  static void blendEnvelopes(const float *internalEnvelope,
                             const float *externalEnvelope, float *keyEnvelope,
                             int numSamples, float externalBlend) noexcept;

private:
  static float clampBlend(float blend) noexcept;
  static int countUsableChannels(const float *const *channels,
                                 int numChannels) noexcept;
  static bool hasUsableChannelArray(const float *const *channels,
                                    int numChannels) noexcept;
  static bool isSilent(const float *const *channels, int numChannels,
                       int numSamples) noexcept;
  static float averageSample(const float *const *channels, int numChannels,
                             int sample) noexcept;
  static float averageEnvelope(const float *const *channels, int numChannels,
                               int sample) noexcept;

  void writeMainSource(const float *const *mainChannels, int numMainChannels,
                       float *monoKey, int numSamples, Source source) noexcept;
  void applyUnavailableExternalFallback(const float *const *mainChannels,
                                        int numMainChannels, float *monoKey,
                                        int numSamples,
                                        FallbackMode fallbackMode) noexcept;

  double sampleRate_ = 0.0;
  int maxBlockSize_ = 0;
  int preparedMainChannels_ = 0;
  int preparedSidechainChannels_ = 0;
  Source source_ = Source::Main;
  FallbackMode fallbackMode_ = FallbackMode::InternalMain;
  float externalBlend_ = 1.0f;
  float lastKeySample_ = 0.0f;
  bool alignmentCalibrated_ = false;
  Status status_{};
};

} // namespace ChugGate::DSP
