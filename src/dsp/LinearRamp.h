#pragma once

// Exact comparison is intended: an unchanged target must not restart the ramp.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"

namespace labq8::dsp
{
// Linear ramp advanced once per control tick: the smoothing of E0 (BellBand
// keeps its own frozen copy). Shared by EqBand and the dynamic band.
struct LinearRamp
{
    double current = 0.0, target = 0.0, step = 0.0;
    int remainingTicks = 0;

    void snap (double value) noexcept
    {
        current = target = value;
        step = 0.0;
        remainingTicks = 0;
    }

    void setTarget (double value, int ticks) noexcept
    {
        if (value == target)
            return;

        target = value;

        if (ticks <= 0)
        {
            snap (value);
            return;
        }

        step = (target - current) / ticks;
        remainingTicks = ticks;
    }

    bool tick() noexcept
    {
        if (remainingTicks <= 0)
            return false;

        if (--remainingTicks == 0)
            current = target;
        else
            current += step;

        return true;
    }

    bool isSettled() const noexcept { return remainingTicks == 0; }
};
} // namespace labq8::dsp

#pragma clang diagnostic pop
