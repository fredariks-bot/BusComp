#include "ColorDSP.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>

namespace buscomp
{

void ColorDSP::prepare (double) { reset(); }

void ColorDSP::reset()
{
    smEnabled = enabledTarget.load (std::memory_order_relaxed);
    smAmount  = amountTarget.load (std::memory_order_relaxed);
}

float ColorDSP::processSample (float x, double rate) noexcept
{
    const float coeff = 1.0f - std::exp (-1.0f / (float) (0.01 * rate)); // 10 ms smoothing
    smEnabled += (enabledTarget.load (std::memory_order_relaxed) - smEnabled) * coeff;
    smAmount  += (amountTarget.load (std::memory_order_relaxed) - smAmount) * coeff;

    const float w = smEnabled * smAmount; // blend weight 0..1
    if (w <= 0.0f)
        return x;

    // drive 1..3 over the amount range; unity slope at x = 0 (level-matched to CLEAN)
    const float drive = 1.0f + 2.0f * smAmount;
    const float sat = std::tanh (drive * x) / drive;

    return x * (1.0f - w) + sat * w;
}

} // namespace buscomp
