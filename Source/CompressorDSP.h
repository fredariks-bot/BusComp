#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

#include "SidechainFilter.h"
#include "ColorDSP.h"
#include "SoftClipper.h"
#include "LevelMeter.h"
#include "GainReductionMeter.h"

namespace buscomp
{

/** Complete bus-compressor engine.

    Signal flow per sample (at the current, possibly oversampled rate):
      sidechain HPF -> stereo-linked peak detector -> gain computer
      (hard/soft knee) -> attack/release envelope (fixed or auto) ->
      CLEAN passthrough or COLOR saturation -> dry/wet parallel mix ->
      optional soft clipper -> makeup gain (manual + auto) -> output.

    With oversampling enabled the whole chain above runs inside the
    oversampled domain, so the two nonlinear stages (COLOR saturation and
    soft clipper) share one anti-aliasing oversampling chain.

    The audio thread performs no heap allocation, no locking and no I/O.
    All smoothing is one-pole and sample-rate independent. */
class CompressorDSP
{
public:
    static constexpr int maxChannels = 2;

    struct Settings
    {
        float thresholdDb  = -18.0f;
        float ratio        = 4.0f;
        float attackMs     = 10.0f;
        float releaseSec   = 0.4f;
        bool  autoRelease  = false;
        float makeupDb     = 0.0f;
        bool  autoGain     = false;
        float mix          = 1.0f;   // 0..1 (dry..wet)
        float stereoLink   = 1.0f;   // 0..1
        float scHpfHz      = 0.0f;   // 0 = off
        bool  softKnee     = false;  // 6 dB knee width
        int   oversampling = 0;      // 0 = off, 1 = 2x, 2 = 4x
        bool  colorMode    = false;  // CLEAN / COLOR
        float colorAmount  = 0.5f;   // 0..1
        bool  softClipOn   = false;
        float clipThresholdDb = -3.0f;
        float clipKnee     = 0.5f;   // 0..1
    };

    CompressorDSP();

    /** Message thread. Preallocates everything for the given sample rate,
        maximum block size and channel count (1 = mono, 2 = stereo). */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Audio thread (or message thread): publishes new parameter targets.
        Values are smoothed inside process() - no zipper noise. */
    void setSettings (const Settings& newSettings);

    /** Message thread: requests an oversampling mode change. The actual
        (allocation-free) switch happens at the start of the next
        process() call; filter states are reset there. */
    void setOversamplingMode (int stages) noexcept;
    int  getOversamplingMode() const noexcept { return activeMode.load(); }

    /** Latency in base-rate samples of the currently active mode. */
    int  getLatencySamples() const noexcept;
    /** Latency for an arbitrary mode (message thread, e.g. for host reporting). */
    int  getLatencySamplesForMode (int mode) const noexcept;

    /** Full processing chain, in place. Audio thread only. */
    void process (juce::AudioBuffer<float>& buffer);

    // --- meter access (lock-free reads for the GUI) ---
    float getGainReductionDb() const noexcept { return grMeter.getGainReductionDb(); }
    float getGainReductionDbRaw() const noexcept { return grMeter.getGainReductionDbRaw(); }
    float getGainReductionPeakDb() const noexcept { return grMeter.getPeakDb(); }
    float getInputDb  (int ch) const noexcept { return inputMeter.getLevelDb (ch); }
    float getInputHoldDb  (int ch) const noexcept { return inputMeter.getHoldDb (ch); }
    float getInputRmsDb   (int ch) const noexcept { return inputMeter.getRmsDb (ch); }
    float getOutputDb (int ch) const noexcept { return outputMeter.getLevelDb (ch); }
    float getOutputHoldDb (int ch) const noexcept { return outputMeter.getHoldDb (ch); }
    float getOutputRmsDb  (int ch) const noexcept { return outputMeter.getRmsDb (ch); }
    bool  isInputClipping  (int ch) const noexcept { return inputMeter.isClipping (ch); }
    bool  isOutputClipping (int ch) const noexcept { return outputMeter.isClipping (ch); }
    float getClipMaxOvershootDb() const noexcept { return clipper.getMaxOvershootDb(); }
    float getClipRatio() const noexcept { return clipper.getClipRatio(); }
    void  resetMeters() noexcept
    {
        grMeter.resetPeak();
        inputMeter.resetHoldAndClip();
        outputMeter.resetHoldAndClip();
        clipper.resetStats();
    }

    /** Gain computer: input level in dBFS -> gain reduction in dB (>= 0).
        Hard knee by default; with softKnee a 6 dB wide quadratic knee that
        is continuous in value and slope at both knee edges. */
    static float gainComputer (float levelDb, float thresholdDb, float ratio, bool softKnee) noexcept;

private:
    void processCore (juce::dsp::AudioBlock<float>& block, double rate);
    void applyOversamplingMode (int mode);

    double sampleRate = 44100.0;
    int    maxBlock = 512;
    int    numChannels = 2;

    // parameter targets (atomics; written from any thread, read per block)
    std::atomic<float> tThreshold { -18.0f }, tRatio { 4.0f }, tAttack { 10.0f },
                       tRelease { 0.4f }, tMakeup { 0.0f }, tMix { 1.0f },
                       tLink { 1.0f }, tScHpf { 0.0f }, tColorAmount { 0.5f },
                       tClipThreshold { -3.0f }, tClipKnee { 0.5f };
    std::atomic<bool>  tAutoRelease { false }, tAutoGain { false }, tSoftKnee { false },
                       tColorMode { false }, tSoftClip { false };

    // smoothed parameter state (audio thread only)
    float smThreshold = -18.0f, smRatio = 4.0f, smAttackMs = 10.0f, smReleaseSec = 0.4f;
    float smMakeup = 0.0f, smMix = 1.0f, smLink = 1.0f, smScHpf = 0.0f;

    SidechainFilter scFilter;
    ColorDSP        colorStage;
    SoftClipper     clipper;

    juce::dsp::Oversampling<float> os2;
    juce::dsp::Oversampling<float> os4;
    juce::AudioBuffer<float> coreBuffer;  // scratch for the oversampled path
    std::atomic<int> pendingMode { 0 };
    std::atomic<int> activeMode  { 0 };

    // envelope state per channel
    float gr[maxChannels] { 0.0f, 0.0f };
    float holdTime = 0.0f;
    float tRelSmoothed = 0.4f;

    // auto-gain measurement (leaky RMS, dry input vs. wet post-color, pre-mix)
    float inRms = 0.0f, outRms = 0.0f, smAutoMakeup = 0.0f;

    LevelMeter inputMeter, outputMeter;
    GainReductionMeter grMeter;
};

} // namespace buscomp
