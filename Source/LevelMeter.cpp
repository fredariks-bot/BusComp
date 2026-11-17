#include "LevelMeter.h"
#include <cmath>

namespace buscomp
{

void LevelMeter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    // Peak ballistics: falls by ~60 dB within 0.6 s when no new peak arrives.
    peakDecayPerSample = std::exp (std::log (1.0e-6f) / (float) (0.6 * sampleRate));
    // RMS window: ~300 ms leaky integrator.
    rmsCoeffPerSample = 1.0f - std::exp (-1.0f / (float) (0.3 * sampleRate));
    reset();
}

void LevelMeter::reset()
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        peak[ch] = 0.0f;
        hold[ch] = 0.0f;
        holdTimer[ch] = 0.0f;
        rms[ch] = 0.0f;
        clipped[ch] = false;
        levelDb[ch].store (minDb, std::memory_order_relaxed);
        holdDb[ch].store (minDb, std::memory_order_relaxed);
        rmsDb[ch].store (minDb, std::memory_order_relaxed);
        clipFlag[ch].store (false, std::memory_order_relaxed);
    }
    resetRequested.store (false, std::memory_order_relaxed);
}

void LevelMeter::processSample (int ch, float x) noexcept
{
    const float a = std::fabs (x);

    if (a >= 0.999f)
        clipped[ch] = true;

    if (a > peak[ch])
        peak[ch] = a;
    else
        peak[ch] *= peakDecayPerSample;

    if (a > hold[ch])
    {
        hold[ch] = a;
        holdTimer[ch] = 0.0f;
    }
    else
    {
        holdTimer[ch] += (float) (1.0 / sampleRate);
        if (holdTimer[ch] > 1.5f) // hold for 1.5 s, then follow the decaying peak
            hold[ch] = juce::jmax (hold[ch] * peakDecayPerSample, peak[ch]);
    }

    rms[ch] += (a * a - rms[ch]) * rmsCoeffPerSample;

    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        hold[ch] = peak[ch];
        holdTimer[ch] = 0.0f;
        clipped[ch] = false;
    }
}

void LevelMeter::processChannel (int ch, const float* data, int numSamples)
{
    ch = ch & 1;
    for (int i = 0; i < numSamples; ++i)
        processSample (ch, data[i]);

    levelDb[ch].store (toDb (peak[ch]), std::memory_order_relaxed);
    holdDb[ch].store  (toDb (hold[ch]), std::memory_order_relaxed);
    rmsDb[ch].store   (toDb (std::sqrt (rms[ch])), std::memory_order_relaxed);
    clipFlag[ch].store (clipped[ch], std::memory_order_relaxed);
}

void LevelMeter::process (const juce::AudioBuffer<float>& buffer)
{
    const int n = juce::jmin (buffer.getNumChannels(), maxChannels);
    for (int ch = 0; ch < n; ++ch)
        processChannel (ch, buffer.getReadPointer (ch), buffer.getNumSamples());
}

float LevelMeter::getLevelDb (int ch) const noexcept
{
    return levelDb[ch & 1].load (std::memory_order_relaxed);
}

float LevelMeter::getHoldDb (int ch) const noexcept
{
    return holdDb[ch & 1].load (std::memory_order_relaxed);
}

float LevelMeter::getRmsDb (int ch) const noexcept
{
    return rmsDb[ch & 1].load (std::memory_order_relaxed);
}

bool LevelMeter::isClipping (int ch) const noexcept
{
    return clipFlag[ch & 1].load (std::memory_order_relaxed);
}

void LevelMeter::resetHoldAndClip()
{
    resetRequested.store (true, std::memory_order_relaxed);
}

} // namespace buscomp
