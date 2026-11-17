#pragma once

#include <atomic>

namespace buscomp
{

/** Gain-reduction meter: smoothed display value plus a peak-hold with
    reset. The audio thread writes lock-free atomics only; the GUI reads
    them and can request a peak reset via an atomic flag. */
class GainReductionMeter
{
public:
    static constexpr float maxDisplayDb = 20.0f;
    static constexpr float minDb = -120.0f;

    void prepare (double sampleRate);
    void reset();

    /** Audio thread: feeds the current gain reduction in dB (>= 0). */
    void processSample (float gainReductionDb, double sampleRate) noexcept;

    /** Smoothed value for the bar graph (0..maxDisplayDb). */
    float getGainReductionDb() const noexcept;
    /** Raw, unsmoothed DSP value (used by the tests). */
    float getGainReductionDbRaw() const noexcept;
    /** Peak-hold value in dB of reduction. */
    float getPeakDb() const noexcept;

    /** GUI thread. */
    void resetPeak();

private:
    float smooth = 0.0f;
    float peak = 0.0f;
    float holdTimer = 0.0f;
    float displayCoeff = 0.0f;
    double sampleRate = 44100.0;

    std::atomic<float> currentDb { 0.0f };
    std::atomic<float> rawDb     { 0.0f };
    std::atomic<float> peakDb    { 0.0f };
    std::atomic<bool>  resetRequested { false };
};

} // namespace buscomp
