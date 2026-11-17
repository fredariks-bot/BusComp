#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace buscomp
{

/** Stereo peak/RMS level meter with peak-hold and clip detection.

    The audio thread only writes lock-free atomics; the GUI thread reads
    them. resetHoldAndClip() is triggered via an atomic flag, so no mutex
    is ever taken in the audio callback. */
class LevelMeter
{
public:
    static constexpr int maxChannels = 2;
    static constexpr float minDb = -120.0f;

    void prepare (double sampleRate);
    void reset();

    /** Feeds one block of audio (up to 2 channels). Audio thread only. */
    void process (const juce::AudioBuffer<float>& buffer);

    /** Feeds one channel of audio. Audio thread only. */
    void processChannel (int ch, const float* data, int numSamples);

    static float toDb (float linear) noexcept
    {
        return linear > 1.0e-9f ? juce::Decibels::gainToDecibels (linear, minDb) : minDb;
    }

    float getLevelDb (int ch) const noexcept;
    float getHoldDb  (int ch) const noexcept;
    float getRmsDb   (int ch) const noexcept;
    bool  isClipping (int ch) const noexcept;

    /** GUI thread: clears peak-hold and clip flags. */
    void resetHoldAndClip();

private:
    void processSample (int ch, float x) noexcept;

    double sampleRate = 44100.0;
    float peakDecayPerSample = 0.0f;
    float rmsCoeffPerSample  = 0.0f;

    float peak[maxChannels]  { 0.0f, 0.0f };
    float hold[maxChannels]  { 0.0f, 0.0f };
    float holdTimer[maxChannels] { 0.0f, 0.0f };
    float rms[maxChannels]   { 0.0f, 0.0f };
    bool  clipped[maxChannels] { false, false };

    std::atomic<float> levelDb[maxChannels] { minDb, minDb };
    std::atomic<float> holdDb[maxChannels]  { minDb, minDb };
    std::atomic<float> rmsDb[maxChannels]   { minDb, minDb };
    std::atomic<bool>  clipFlag[maxChannels] { false, false };
    std::atomic<bool>  resetRequested { false };
};

} // namespace buscomp
