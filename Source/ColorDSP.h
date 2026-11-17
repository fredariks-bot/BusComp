#pragma once

#include <atomic>

namespace buscomp
{

/** COLOR-mode saturation stage: a continuous, odd, monotonic soft-saturation
    curve  y = tanh(drive * x) / drive  (unity small-signal gain, |y| < 1/drive).

    The stage is blended by (enabled * amount), so amount = 0% passes the
    signal through unchanged. Being odd and memoryless, it adds no DC offset.
    It runs inside the (optionally oversampled) wet path, before the dry/wet
    mix, so its harmonics are anti-aliased together with the soft clipper. */
class ColorDSP
{
public:
    void prepare (double sampleRate);
    void reset();

    /** 0..1, smoothed internally (no zipper on automation). */
    void setEnabled (bool on) noexcept { enabledTarget.store (on ? 1.0f : 0.0f, std::memory_order_relaxed); }
    /** 0..1 color amount, smoothed internally. */
    void setAmount (float amount01) noexcept { amountTarget.store (amount01, std::memory_order_relaxed); }

    float processSample (float x, double rate) noexcept;

private:
    std::atomic<float> enabledTarget { 0.0f };
    std::atomic<float> amountTarget  { 0.5f };
    float smEnabled = 0.0f;
    float smAmount  = 0.5f;
};

} // namespace buscomp
