// Flush — compact radix-2 complex FFT (pure C++, no JUCE)
//
// Shared by two subsystems: (1) linear-phase FIR design (frequency-sampling
// method) and (2) the spectrum analyzer. Iterative in-place Cooley-Tukey with
// bit-reversal permutation; inverse via conjugate trick (see ifft()).

#pragma once

#include <vector>
#include <cmath>
#include "FlushCommon.h"

namespace flush {

struct Complex { double re, im; };

// In-place radix-2 FFT. inverse=true computes the inverse transform WITHOUT the
// 1/n normalization (use ifft() for a correctly-scaled inverse).
inline void fft (Complex* a, int n, bool inverse)
{
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }

    for (int len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / len * (inverse ? 1.0 : -1.0);
        const Complex wlen { std::cos (ang), std::sin (ang) };
        for (int i = 0; i < n; i += len) {
            Complex w { 1.0, 0.0 };
            for (int k = 0; k < len / 2; ++k) {
                const Complex u = a[i + k];
                const Complex v {
                    a[i + k + len / 2].re * w.re - a[i + k + len / 2].im * w.im,
                    a[i + k + len / 2].re * w.im + a[i + k + len / 2].im * w.re };
                a[i + k]         = { u.re + v.re, u.im + v.im };
                a[i + k + len / 2] = { u.re - v.re, u.im - v.im };
                const Complex wn { w.re * wlen.re - w.im * wlen.im,
                                   w.re * wlen.im + w.im * wlen.re };
                w = wn;
            }
        }
    }
}

// Correctly-scaled inverse FFT: ifft(x) = conj(fft(conj(x))) / n.
inline void ifft (Complex* a, int n)
{
    for (int i = 0; i < n; ++i) a[i].im = -a[i].im;
    fft (a, n, false);
    for (int i = 0; i < n; ++i) { a[i].re /= n; a[i].im = -a[i].im / n; }
}

// Next power of two >= n.
inline int nextPow2 (int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Hann window (periodic, normalized so a full-scale sine reads its true level).
inline std::vector<double> hannWindow (int n)
{
    std::vector<double> w (n);
    for (int i = 0; i < n; ++i)
        w[i] = 0.5 * (1.0 - std::cos (2.0 * kPi * i / n));
    return w;
}

} // namespace flush
