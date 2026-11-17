#include "SidechainFilter.h"
#include <cmath>

namespace buscomp
{

void SidechainFilter::prepare (double newSampleRate, int newNumChannels)
{
    sampleRate  = newSampleRate;
    numChannels = juce::jmax (1, newNumChannels);
    stages.assign ((size_t) numChannels * 2, Biquad());
    updateCoefficients();
    reset();
}

void SidechainFilter::reset()
{
    for (auto& s : stages)
        s.reset();
}

void SidechainFilter::setFrequency (float hz)
{
    frequency = hz;
    updateCoefficients();
    reset();
}

void SidechainFilter::updateFrequency (float hz) noexcept
{
    if (hz == frequency)
        return;
    frequency = hz;
    updateCoefficients();
}

void SidechainFilter::updateCoefficients()
{
    if (frequency <= 0.0f || sampleRate <= 0.0)
    {
        for (auto& s : stages)
            s = Biquad(); // passthrough
        return;
    }

    // Clamp the corner frequency well below Nyquist so the filter stays
    // stable at every supported sample rate (44.1 kHz - 192 kHz).
    const double fc = juce::jmin ((double) frequency, sampleRate * 0.45);
    const double w0 = 2.0 * juce::MathConstants<double>::pi * fc / sampleRate;
    const double cosW0 = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * 0.70710678118654752); // Q = 1/sqrt(2)

    const double a0 = 1.0 + alpha;
    const double b0 =  (1.0 + cosW0) * 0.5 / a0;
    const double b1 = -(1.0 + cosW0)       / a0;
    const double b2 =  (1.0 + cosW0) * 0.5 / a0;
    const double a1 = -2.0 * cosW0 / a0;
    const double a2 =  (1.0 - alpha) / a0;

    for (auto& s : stages)
    {
        s.b0 = b0; s.b1 = b1; s.b2 = b2; s.a1 = a1; s.a2 = a2;
    }
}

float SidechainFilter::processSample (int channel, float x) noexcept
{
    if (! isEnabled() || stages.empty())
        return x;

    auto* st = stages.data() + (size_t) juce::jlimit (0, numChannels - 1, channel) * 2;
    const double y = st[1].process (st[0].process ((double) x));
    return (float) y;
}

void SidechainFilter::processBlock (juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumChannels();
    for (int ch = 0; ch < n; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = processSample (ch, data[i]);
    }
}

} // namespace buscomp
