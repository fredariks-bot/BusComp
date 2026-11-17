#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstdint>

namespace buscomp
{

/** Optional soft clipper, placed after the parallel mix and before the final
    makeup/output gain.

    Curve (continuous, odd, monotonic, memoryless => no DC offset):
        y = sign(x) * t * tanh(d * |x| / t) / d
    with t = linear clip threshold and d = 1 + 6 * knee (knee 0..1).
    Unity slope at x = 0 (small signals pass linearly), |y| saturates towards
    t/d... for knee = 0 (d = 1) this is the classic t * tanh(|x| / t) soft
    clipper with asymptote t; larger knee = harder transition. Positive and
    negative peaks are treated symmetrically.

    Enable/disable is crossfaded over ~10 ms so activation causes no clicks.
    When disabled the signal passes through unchanged.

    Stats (lock-free atomics for the GUI): maximum overshoot above the
    threshold in dB (peak-hold) and the share of clipped samples since the
    last reset. */
class SoftClipper
{
public:
    void prepare (double sampleRate);
    void reset();

    void setEnabled (bool on) noexcept { enabledTarget.store (on ? 1.0f : 0.0f, std::memory_order_relaxed); }
    void setThresholdDb (float db) noexcept { thresholdTarget.store (db, std::memory_order_relaxed); }
    /** knee 0..1 */
    void setKnee (float knee01) noexcept { kneeTarget.store (knee01, std::memory_order_relaxed); }

    float processSample (float x, double rate) noexcept;

    float getMaxOvershootDb() const noexcept { return maxOverDb.load (std::memory_order_relaxed); }
    float getClipRatio() const noexcept { return clipRatio.load (std::memory_order_relaxed); }
    void  resetStats() noexcept { resetRequested.store (true, std::memory_order_relaxed); }

private:
    std::atomic<float> enabledTarget   { 0.0f };
    std::atomic<float> thresholdTarget { -3.0f };
    std::atomic<float> kneeTarget      { 0.5f };

    float smEnabled = 0.0f;
    float smThresholdDb = -3.0f;
    float smKnee = 0.5f;

    std::atomic<float> maxOverDb { 0.0f };
    std::atomic<float> clipRatio { 0.0f };
    std::atomic<bool>  resetRequested { false };

    float    maxOver = 0.0f;
    uint64_t totalSamples = 0;
    uint64_t clippedSamples = 0;
};

} // namespace buscomp
