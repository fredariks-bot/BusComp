#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace buscomp
{

/** 4th-order high-pass filter (two cascaded 2nd-order Butterworth biquads,
    24 dB/octave) used exclusively in the sidechain detector path.

    All state is preallocated in prepare(); processSample() performs no
    heap allocation and is safe to call from the audio thread. */
class SidechainFilter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Sets the corner frequency in Hz and clears the filter state.
        Values <= 0 disable the filter. */
    void setFrequency (float hz);

    /** Updates the corner frequency without touching the filter state
        (for click-free, smoothed changes from the audio thread). */
    void updateFrequency (float hz) noexcept;
    float getFrequency() const noexcept { return frequency; }
    bool isEnabled() const noexcept { return frequency > 0.0f; }

    float processSample (int channel, float x) noexcept;

    /** In-place processing of a whole buffer (used by the tests). */
    void processBlock (juce::AudioBuffer<float>& buffer);

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void reset() noexcept { z1 = z2 = 0.0; }
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void updateCoefficients();

    double sampleRate = 44100.0;
    float  frequency = 0.0f;
    int    numChannels = 0;
    std::vector<Biquad> stages; // 2 cascaded biquads per channel
};

} // namespace buscomp
