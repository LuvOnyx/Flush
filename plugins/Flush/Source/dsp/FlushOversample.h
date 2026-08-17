// Flush — 2x oversampler (pure C++, no JUCE)
//
// Near-Nyquist accuracy for high shelves and "air": the EQ section can run at
// 2x the host rate. Structure: zero-stuff + half-band lowpass (up), then
// half-band lowpass + decimate (down). The same windowed-sinc lowpass is used
// for both so the passband is flat and the image/alias bands are attenuated
// by the Kaiser window's stopband (~ -65 dB at beta = 6).

#pragma once

#include "FlushFir.h"

namespace flush {

class Oversampler2x
{
public:
    void reset (double baseFs)
    {
        baseFs_ = baseFs;
        // 97 taps -> group delay 48 @ 2x = 24 base samples (EVEN, so the
        // round-trip delay is an exact whole number of base samples — a 95-tap
        // filter would make the 4x cascade land on a half sample).
        const auto lp = windowedSincLowpass (baseFs * 2.0, baseFs * 0.45, 97, 6.0);
        up_.set (lp);
        down_.set (lp);
    }

    // Feed one base-rate sample; produce two 2x-rate samples in out[0], out[1].
    void upsample (double in, double out[2])
    {
        // Zero-stuff then filter at 2x rate; scale by 2 to restore the energy
        // halved by the inserted zero.
        out[0] = 2.0 * up_.process (in);
        out[1] = 2.0 * up_.process (0.0);
    }

    // Feed two 2x-rate samples; produce one base-rate sample (decimate by 2).
    //
    // PHASE-CRITICAL: the upsampler's interpolated base samples land on the EVEN
    // 2x positions (the first sample of each pair), so the decimator must keep
    // `in0`. Keeping `in1` (odd phase) splits the impulse peak across two base
    // samples — a half-sample delay error that no magnitude test ever catches,
    // but which misaligns host delay compensation. (Found by the impulse-delay
    // test.)
    double downsample (double in0, double in1)
    {
        const double out = down_.process (in0);
        down_.process (in1);
        return out;
    }

    // Round-trip group delay in BASE-rate samples (up delay + down delay).
    // With an even group delay @2x this is exact and integer.
    int latencySamples() const { return up_.latency(); }

private:
    double baseFs_ = 48000.0;
    FirFilter up_, down_;
};

// 4x oversampler: two cascaded 2x stages (base -> 2x -> 4x and back).
class Oversampler4x
{
public:
    void reset (double baseFs)
    {
        baseFs_ = baseFs;
        s1_.reset (baseFs);        // base  -> 2x
        s2_.reset (baseFs * 2.0);  // 2x    -> 4x
        d2_.reset (baseFs * 2.0);  // 4x    -> 2x
        d1_.reset (baseFs);        // 2x    -> base
    }

    void upsample (double in, double out[4])
    {
        double a[2]; s1_.upsample (in, a);
        double b[2]; s2_.upsample (a[0], b);
        double c[2]; s2_.upsample (a[1], c);
        out[0] = b[0]; out[1] = b[1]; out[2] = c[0]; out[3] = c[1];
    }

    double downsample (double i0, double i1, double i2, double i3)
    {
        const double a = d2_.downsample (i0, i1);
        const double b = d2_.downsample (i2, i3);
        return d1_.downsample (a, b);
    }

    // Round-trip group delay in BASE-rate samples.
    //   s1 (base <-> 2x): s1.latencySamples() base samples.
    //   s2 (2x <-> 4x):   s2.latencySamples() samples at the 2x rate = / 2 base.
    // (The old formula added both raw, over-reporting by s2/2 = ~24 samples and
    //  leaving the host delay-compensation misaligned.)
    double latencySamples() const { return s1_.latencySamples() + s2_.latencySamples() * 0.5; }

private:
    double baseFs_ = 48000.0;
    Oversampler2x s1_, s2_, d1_, d2_;
};

} // namespace flush
