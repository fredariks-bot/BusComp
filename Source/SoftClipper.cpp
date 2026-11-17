#include "SoftClipper.h"
#include <cmath>

namespace buscomp
{

void SoftClipper::prepare (double) { reset(); }

void SoftClipper::reset()
{
    smEnabled = enabledTarget.load (std::memory_order_relaxed);
    smThresholdDb = thresholdTarget.load (std::memory_order_relaxed);
    smKnee = kneeTarget.load (std::memory_order_relaxed);
    maxOver = 0.0f;
    totalSamples = 0;
    clippedSamples = 0;
    maxOverDb.store (0.0f, std::memory_order_relaxed);
    clipRatio.store (0.0f, std::memory_order_relaxed);
    resetRequested.store (false, std::memory_order_relaxed);
}

float SoftClipper::processSample (float x, double rate) noexcept
{
    const float coeff = 1.0f - std::exp (-1.0f / (float) (0.01 * rate)); // 10 ms smoothing
    smEnabled     += (enabledTarget.load (std::memory_order_relaxed) - smEnabled) * coeff;
    smThresholdDb += (thresholdTarget.load (std::memory_order_relaxed) - smThresholdDb) * coeff;
    smKnee        += (kneeTarget.load (std::memory_order_relaxed) - smKnee) * coeff;

    const float t = std::pow (10.0f, smThresholdDb / 20.0f); // linear threshold
    const float a = std::fabs (x);

    if (smEnabled <= 0.0f)
        return x; // bypassed: bit-transparent passthrough

    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        maxOver = 0.0f;
        totalSamples = 0;
        clippedSamples = 0;
    }

    const float d = 1.0f + 6.0f * smKnee;
    const float soft = t * std::tanh (d * a / t) / d; // unity slope at 0, asymptote t/d... see header
    const float clipped = std::copysign (soft, x);

    // blend for click-free enable/disable
    const float y = x * (1.0f - smEnabled) + clipped * smEnabled;

    // stats: count every processed sample while (nearly) enabled
    if (smEnabled > 0.5f)
    {
        ++totalSamples;
        if (a > t)
        {
            ++clippedSamples;
            const float overDb = 20.0f * std::log10 (a / t);
            if (overDb > maxOver)
                maxOver = overDb;
        }
        maxOverDb.store (maxOver, std::memory_order_relaxed);
        clipRatio.store (totalSamples > 0 ? (float) ((double) clippedSamples / (double) totalSamples) : 0.0f,
                         std::memory_order_relaxed);
    }

    return y;
}

} // namespace buscomp
