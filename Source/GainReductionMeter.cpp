#include "GainReductionMeter.h"
#include <cmath>
#include <algorithm>

namespace buscomp
{

void GainReductionMeter::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    displayCoeff = 1.0f - std::exp (-1.0f / (float) (0.03 * sampleRate)); // ~30 ms display smoothing
    reset();
}

void GainReductionMeter::reset()
{
    smooth = 0.0f;
    peak = 0.0f;
    holdTimer = 0.0f;
    currentDb.store (0.0f, std::memory_order_relaxed);
    rawDb.store (0.0f, std::memory_order_relaxed);
    peakDb.store (0.0f, std::memory_order_relaxed);
    resetRequested.store (false, std::memory_order_relaxed);
}

void GainReductionMeter::processSample (float grDb, double rate) noexcept
{
    const float gr = std::max (0.0f, grDb);

    smooth += (gr - smooth) * displayCoeff;

    if (gr > peak)
    {
        peak = gr;
        holdTimer = 0.0f;
    }
    else
    {
        holdTimer += (float) (1.0 / rate);
        if (holdTimer > 1.5f) // 1.5 s hold, then release towards the current value
        {
            const float decay = std::exp (-1.0f / (float) (0.5 * rate));
            peak = std::max (gr, peak * decay);
        }
    }

    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        peak = gr;
        holdTimer = 0.0f;
    }

    rawDb.store (gr, std::memory_order_relaxed);
    currentDb.store (smooth, std::memory_order_relaxed);
    peakDb.store (peak, std::memory_order_relaxed);
}

float GainReductionMeter::getGainReductionDb() const noexcept
{
    return currentDb.load (std::memory_order_relaxed);
}

float GainReductionMeter::getGainReductionDbRaw() const noexcept
{
    return rawDb.load (std::memory_order_relaxed);
}

float GainReductionMeter::getPeakDb() const noexcept
{
    return peakDb.load (std::memory_order_relaxed);
}

void GainReductionMeter::resetPeak()
{
    resetRequested.store (true, std::memory_order_relaxed);
}

} // namespace buscomp
