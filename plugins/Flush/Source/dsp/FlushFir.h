// Flush — linear-phase FIR design + FIR filter (pure C++, no JUCE)
//
// The "Linear" phase mode. Design method: frequency-sampling — sample the exact
// decramped/matched magnitude response of a band (or the whole EQ) on the FFT
// grid, apply a linear phase ramp, inverse-FFT, and window with Kaiser. The
// result is a symmetric (exactly linear-phase) FIR whose magnitude matches the
// analog-matched IIR design — so the Linear mode sounds like the same EQ, with
// zero phase distortion instead of cramping or warping.

#pragma once

#include <vector>
#include <functional>
#include "FlushCommon.h"
#include "FlushFft.h"

namespace flush {

// Modified Bessel function I0(x) (series) — used by the Kaiser window.
inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0, x2 = x * x;
    for (int k = 1; k < 50; ++k) {
        term *= x2 / (4.0 * k * k);
        sum += term;
        if (term < 1e-15 * sum) break;
    }
    return sum;
}

inline std::vector<double> kaiserWindow (int n, double beta)
{
    const double denom = besselI0 (beta);
    std::vector<double> w (n);
    for (int i = 0; i < n; ++i) {
        const double t = 2.0 * i / (n - 1) - 1.0;      // -1 .. +1
        w[i] = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t))) / denom;
    }
    return w;
}

// Windowed-sinc lowpass FIR (used by the oversampler). Normalized to unit DC gain.
inline std::vector<double> windowedSincLowpass (double fs, double fc, int length, double beta = 6.0)
{
    const int M = (length - 1) / 2;
    const double fcNorm = 2.0 * fc / fs;               // cutoff in cycles/sample
    std::vector<double> h (length);
    const auto win = kaiserWindow (length, beta);
    double sum = 0.0;
    for (int n = 0; n < length; ++n) {
        const double m = n - M;
        h[n] = fcNorm * std::sin (kPi * fcNorm * m) / (kPi * fcNorm * m + 1e-30);
        if (m == 0) h[n] = fcNorm;
        h[n] *= win[n];
        sum += h[n];
    }
    for (auto& x : h) x /= sum;                        // unit DC gain
    return h;
}

// Linear-phase FIR from a magnitude response. mag(f) returns linear magnitude
// at frequency f (Hz); it must be even (mag(f) = mag(-f)). length must be odd.
//
// Frequency-sampling method with a direct (closed-form) inverse DFT:
//   h[n] = (1/N) * sum_k mag(f_k) * cos(2*pi*k*(n - M)/N)
// which is exactly symmetric (linear phase) and does not require a power-of-2
// FFT size. O(N^2), but this runs once per coefficient change — not per sample.
inline std::vector<double> designLinearPhaseFir (
    const std::function<double(double)>& mag, double fs, int length, double beta = 6.0)
{
    const int M = (length - 1) / 2;
    std::vector<double> h (length, 0.0);

    for (int n = 0; n < length; ++n) {
        double acc = 0.0;
        for (int k = 0; k < length; ++k) {
            const double f = (k <= length / 2) ? (double)k * fs / length
                                               : (double)(k - length) * fs / length;
            acc += mag (std::fabs (f)) * std::cos (2.0 * kPi * k * (n - M) / length);
        }
        h[n] = acc / length;
    }

    const auto win = kaiserWindow (length, beta);
    double dc = 0.0;
    for (int n = 0; n < length; ++n) { h[n] *= win[n]; dc += h[n]; }

    // Normalize to the exact requested DC magnitude (guards: skip if DC ~ 0).
    if (std::fabs (dc) > 1e-6) {
        const double norm = mag (0.0) / dc;
        for (auto& x : h) x *= norm;
    }
    return h;
}

// Direct-form FIR filter (transversal) with a circular delay line.
struct FirFilter
{
    std::vector<double> h;
    std::vector<double> buf;
    int pos = 0;

    void set (const std::vector<double>& coeffs)
    {
        h = coeffs;
        buf.assign (coeffs.empty() ? 1 : coeffs.size(), 0.0);
        pos = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    int latency() const { return ((int)h.size() - 1) / 2; }   // group delay (samples)

    double process (double x)
    {
        if (h.empty()) return x;
        buf[pos] = x;
        double y = 0.0;
        const int L = (int)h.size();
        for (int i = 0; i < L; ++i)
            y += h[i] * buf[(pos - i + L) % L];
        pos = (pos + 1) % L;
        return y;
    }
};

} // namespace flush
