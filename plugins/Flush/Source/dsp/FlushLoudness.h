// Flush — Perceptual loudness + true-peak metering (pure C++, no JUCE)
//
// Flush's differentiation is *perceptual*: gain staging is a loudness problem,
// not a sample-power problem. So the hero feature and the meters speak the
// streaming-era standard — ITU-R BS.1770-4 — instead of the commodity RMS meter
// every other EQ ships.

#pragma once

#include "FlushFilter.h"

namespace flush {

// =============================================================================
// K-Weighting (ITU-R BS.1770-4)
//
// Two cascaded biquads (the standard's own structure):
//   stage 1 — "pre-filter" shelving:  +4 dB, fc = 1681.3 Hz, Q = 0.7071
//   stage 2 — RLB high-pass:            fc = 38.13 Hz,  Q = 0.5
//
// These are *measurement* filters (the standard literally publishes biquad
// coefficients), which is why they use the canonical bilinear-transform design —
// they are not part of the audible EQ path. The +4 dB shelf approximates ear
// sensitivity above 2 kHz; the RLB stage shapes the low-frequency roll-off.
// =============================================================================
struct KWeighting
{
    Biquad shelf, hpf;

    void reset() { shelf.reset(); hpf.reset(); }

    void init (double fs)
    {
        // Exact ITU-R BS.1770-4 design (parameters inverted from the published
        // 48 kHz coefficients; matches libebur128, the de-facto reference).
        //   pre-filter:  +3.9998 dB shelf, f0 = 1681.974 Hz, Q = 0.707175
        //   RLB stage:   high-pass,        f0 = 38.1355 Hz,  Q = 0.500327
        shelf.setCoef (prefilter (fs));
        hpf.setCoef (rlb (fs));
    }

    // General bilinear biquad from the BS.1770 design equations:
    //   K = tan(pi*f0/fs)
    //   a0 = 1 + K/Q + K^2 ; a1 = 2(K^2 - 1) ; a2 = 1 - K/Q + K^2
    //   b0 = Vh + Vb*K/Q + Vl*K^2 ; b1 = 2(Vl*K^2 - Vh) ; b2 = Vh - Vb*K/Q + Vl*K^2
    // The published coefficients normalize a0 -> 1. The RLB additionally keeps
    // its numerator at exactly [1, -2, 1] (b0 = 1) instead of dividing by a0.
    static BiquadCoef design (double fs, double f0, double Vh, double Vb, double Vl,
                              double Q, bool keepNumeratorUnit)
    {
        const double K  = std::tan (kPi * f0 / fs);
        const double K2 = K * K;
        const double KQ = K / Q;
        const double a0 = 1.0 + KQ + K2;
        const double numScale = keepNumeratorUnit ? 1.0 : a0;

        BiquadCoef r;
        r.b0 = (Vh + Vb * KQ + Vl * K2) / numScale;
        r.b1 = 2.0 * (Vl * K2 - Vh) / numScale;
        r.b2 = (Vh - Vb * KQ + Vl * K2) / numScale;
        r.a1 = 2.0 * (K2 - 1.0) / a0;
        r.a2 = (1.0 - KQ + K2) / a0;
        return r;
    }

    static BiquadCoef prefilter (double fs)
    {
        const double Vh = std::pow (10.0, 3.999843853973347 / 20.0);   // 1.5848647...
        const double Vb = std::pow (Vh, 0.4996667741545416);           // ~sqrt(Vh)
        return design (fs, 1681.974450955533, Vh, Vb, 1.0, 0.7071752369554196, false);
    }

    static BiquadCoef rlb (double fs)
    {
        return design (fs, 38.13547087602444, 1.0, 0.0, 0.0, 0.5003270373238773, true);
    }

    double process (double x) { return hpf.process (shelf.process (x)); }

    // Reference: the exact published BS.1770-4 coefficients @ 48 kHz (a0 = 1),
    // kept for verification so our sample-rate-general derivation is checked
    // against the standard itself, not against ourselves.
    static BiquadCoef shelf48k() {
        return { 1.53512485958697, -2.69169618940638, 1.19839281085285,
                -1.69065929318241,  0.73248077421585 };
    }
    static BiquadCoef rlb48k() {
        return { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
    }
};

// =============================================================================
// True-peak meter (BS.1770-4 style)
//
// Sample peak under-reports the level the DAC/limiter actually sees: a digital
// signal can reconstruct ABOVE 0 dBFS between samples. True-peak measures the
// bandlimited waveform itself via 4x oversampling (windowed-sinc interpolation
// at the three fractional phases between samples).
// =============================================================================
class TruePeakMeter
{
public:
    static constexpr int kRadius = 8;
    static constexpr int kTaps   = 2 * kRadius + 1;

    TruePeakMeter() { buildKernels(); }

    void reset (double fs)
    {
        for (int i = 0; i < kTaps; ++i) buf_[i] = 0.0;
        hold_ = 0.0;
        samplePeak_ = 0.0;
        release_ = std::exp (-1.0 / (1.5 * fs));      // 1.5 s meter release
    }

    // Feed one sample; returns the held true peak (linear, instant attack).
    // The estimate is centered kRadius samples in the past so the interpolation
    // kernel is symmetric — true-peak metering is allowed (and expected) to add
    // this small latency (BS.1770-4's own measurement introduces oversampling
    // delay).
    double process (double x)
    {
        for (int i = 1; i < kTaps; ++i) buf_[i - 1] = buf_[i];
        buf_[kTaps - 1] = x;

        samplePeak_ = std::max (samplePeak_, std::fabs (buf_[kRadius]));

        double inst = std::fabs (buf_[kRadius]);
        for (int ph = 0; ph < 3; ++ph) {               // phases -0.25 / -0.5 / -0.75
            double y = 0.0;
            for (int n = 0; n < kTaps; ++n) y += buf_[n] * kernel_[ph][n];
            inst = std::max (inst, std::fabs (y));
        }

        hold_ = std::max (inst, hold_ * release_);
        return hold_;
    }

    double truePeak() const { return hold_; }
    double samplePeak() const { return samplePeak_; }

private:
    // Symmetric interpolation kernel around the buffer center. Coefficient for
    // sample buf_[n] (relative index m = n - kRadius) when evaluating at
    // fractional offset tau in (-1, 0):
    //     k = sinc(tau - m) * w(tau - m),  |tau - m| <= R
    void buildKernels()
    {
        for (int ph = 0; ph < 3; ++ph) {
            const double tau = -0.25 * (ph + 1);       // -0.25, -0.5, -0.75
            for (int n = 0; n < kTaps; ++n) {
                const double m = n - kRadius;          // -R .. +R
                const double t = tau - m;
                kernel_[ph][n] = sinc (t) * window (t);
            }
        }
    }

    static double sinc (double t)
    {
        if (std::fabs (t) < 1e-9) return 1.0;
        const double p = kPi * t;
        return std::sin (p) / p;
    }

    static double window (double t)
    {
        if (std::fabs (t) > kRadius) return 0.0;
        return std::cos (kPi * t / (2.0 * kRadius));   // half-cosine
    }

    double buf_[kTaps];
    double kernel_[3][kTaps];
    double hold_ = 0.0;
    double samplePeak_ = 0.0;
    double release_ = 1.0;
};

} // namespace flush
