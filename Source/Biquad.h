#pragma once
#include <algorithm>
#include <cmath>

// Small RBJ-cookbook biquad toolkit shared by the EQ and the tape-head filter.
namespace dsp_util
{
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct BiquadState
{
    double z1 = 0.0, z2 = 0.0;

    inline float process (const BiquadCoeffs& c, float x) noexcept
    {
        const double in = x;
        const double out = c.b0 * in + z1;
        z1 = c.b1 * in - c.a1 * out + z2;
        z2 = c.b2 * in - c.a2 * out;
        return (float) out;
    }

    void reset() noexcept { z1 = z2 = 0.0; }
};

inline double clampFreq (double f, double fs) noexcept { return std::clamp (f, 10.0, fs * 0.45); }

inline BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline BiquadCoeffs makePeak (double fs, double f, double q, double gainDb) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 6.283185307179586 * clampFreq (f, fs) / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    return normalise (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A,
                      1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
}

inline BiquadCoeffs makeLowShelf (double fs, double f, double q, double gainDb) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 6.283185307179586 * clampFreq (f, fs) / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), t = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) - (A - 1) * cw + t),
                      2.0 * A * ((A - 1) - (A + 1) * cw),
                      A * ((A + 1) - (A - 1) * cw - t),
                      (A + 1) + (A - 1) * cw + t,
                      -2.0 * ((A - 1) + (A + 1) * cw),
                      (A + 1) + (A - 1) * cw - t);
}

inline BiquadCoeffs makeHighShelf (double fs, double f, double q, double gainDb) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 6.283185307179586 * clampFreq (f, fs) / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), t = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1) + (A - 1) * cw + t),
                      -2.0 * A * ((A - 1) + (A + 1) * cw),
                      A * ((A + 1) + (A - 1) * cw - t),
                      (A + 1) - (A - 1) * cw + t,
                      2.0 * ((A - 1) - (A + 1) * cw),
                      (A + 1) - (A - 1) * cw - t);
}

inline BiquadCoeffs makeLowpass (double fs, double f, double q) noexcept
{
    const double w0 = 6.283185307179586 * clampFreq (f, fs) / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    return normalise ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5,
                      1.0 + alpha, -2.0 * cw, 1.0 - alpha);
}
} // namespace dsp_util
