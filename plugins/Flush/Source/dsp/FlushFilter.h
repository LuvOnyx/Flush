// Flush — Biquad filter core (double precision, no JUCE dependency)
//
// Design families, all computed in float64:
//   1. Vicanek "matched" biquads (2016) — impulse-invariance poles +
//      magnitude-matched numerators. No bilinear-transform cramping near Nyquist.
//      Used for the "Low Latency" phase mode (minimum-phase, zero latency).
//   2. Orfanidis decramped peaking (1997) — the "prescribed Nyquist-frequency
//      gain" design that fixes the bilinear transform's high-frequency cramping.
//      This is the high-frequency-decramping technique reference EQs are built
//      on (FabFilter "Natural Phase" class). Used for the "Natural" mode.
//   3. RBJ "Audio EQ Cookbook" (2006) — the canonical bilinear-transform biquads.
//      DEMOTED to a measurement baseline + the few shapes where it remains the
//      correct canonical formula (notch, all-pass) or a documented interim
//      (shelves — decramped shelves via Massberg / Gunness-Chauhan are next).
//
// Coefficients are normalized so a0 == 1 (stored as b0,b1,b2,a1,a2).

#pragma once

#include "FlushCommon.h"

namespace flush {

enum class Shape {
    Bell, Notch, LowShelf, HighShelf,
    LowCut, HighCut, BandPass, TiltShelf, FlatTilt, AllPass
};

struct BiquadCoef {
    double b0 = 0.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
};

// Linear magnitude of a biquad at radian frequency w (w = 2*pi*f/fs).
// Used by the linear-phase FIR designer to sample a band's exact magnitude.
inline double magnitude (const BiquadCoef& c, double w)
{
    const double cw  = std::cos (w),  sw  = std::sin (w);
    const double c2w = std::cos (2.0 * w), s2w = std::sin (2.0 * w);
    const double nre = c.b0 + c.b1 * cw + c.b2 * c2w;
    const double nim = -(c.b1 * sw + c.b2 * s2w);
    const double dre = 1.0 + c.a1 * cw + c.a2 * c2w;
    const double dim = -(c.a1 * sw + c.a2 * s2w);
    const double den = dre * dre + dim * dim;
    return std::sqrt ((nre * nre + nim * nim) / std::max (den, 1e-30));
}

// Clamp a Q into a numerically safe band (guards division-by-zero / pole
// overflow in the coefficient math). The processor already clamps user input;
// this is the second line of defence against malformed presets/state.
inline double clampQ (double Q) { return std::max (1e-3, std::min (40.0, sanitize (Q))); }

// Clamp a filter frequency to (0, 0.49*fs) so tan(w0/2) can never reach inf.
inline double clampFc (double f0, double fs)
{
    if (!(fs > 0.0)) fs = 48000.0;
    return std::max (1.0, std::min (fs * 0.49, sanitize (f0)));
}

// Transposed Direct Form II — numerically robust, two state variables.
struct Biquad {
    BiquadCoef c;
    double s1 = 0.0, s2 = 0.0;

    void reset() { s1 = s2 = 0.0; }
    void setCoef(const BiquadCoef& cc) { c = cc; }

    inline double process(double x) {
        x = sanitize (x);
        const double y = c.b0 * x + s1;
        s1 = sanitize (c.b1 * x - c.a1 * y + s2);
        s2 = sanitize (c.b2 * x - c.a2 * y);
        return y;
    }
};

// ---------------------------------------------------------------- RBJ family --

namespace rbj {

// Peaking / bell EQ. A boost of +N dB followed by a cut of -N dB at the same
// f0/Q returns to unity (a deliberate RBJ design guarantee).
inline BiquadCoef bell(double fs, double f0, double gainDb, double Q) {
    const double A   = dbToLin(gainDb / 2.0);          // 10^(dB/40)
    const double w0  = 2.0 * kPi * f0 / fs;
    const double cw  = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0  = 1.0 + alpha / A;

    BiquadCoef r;
    r.b0 = (1.0 + alpha * A) / a0;
    r.b1 = (-2.0 * cw) / a0;
    r.b2 = (1.0 - alpha * A) / a0;
    r.a1 = (-2.0 * cw) / a0;
    r.a2 = (1.0 - alpha / A) / a0;
    return r;
}

// Shared shelf intermediate: 2*sqrt(A)*alpha, expressed via the "slope" S in (0,1].
inline double shelfTwoSqrtAalpha(double A, double w0, double S) {
    // 2*sqrt(A)*alpha = sin(w0) * sqrt( (A^2+1)*(1/S - 1) + 2A )
    return std::sin(w0) * std::sqrt((A * A + 1.0) * (1.0 / S - 1.0) + 2.0 * A);
}

inline BiquadCoef lowShelf(double fs, double f0, double gainDb, double S) {
    const double A   = dbToLin(gainDb / 2.0);
    const double w0  = 2.0 * kPi * f0 / fs;
    const double cw  = std::cos(w0);
    const double two = shelfTwoSqrtAalpha(A, w0, S);
    const double a0  = (A + 1.0) + (A - 1.0) * cw + two;

    BiquadCoef r;
    r.b0 = A * ((A + 1.0) - (A - 1.0) * cw + two) / a0;
    r.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0;
    r.b2 = A * ((A + 1.0) - (A - 1.0) * cw - two) / a0;
    r.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0;
    r.a2 = ((A + 1.0) + (A - 1.0) * cw - two) / a0;
    return r;
}

inline BiquadCoef highShelf(double fs, double f0, double gainDb, double S) {
    const double A   = dbToLin(gainDb / 2.0);
    const double w0  = 2.0 * kPi * f0 / fs;
    const double cw  = std::cos(w0);
    const double two = shelfTwoSqrtAalpha(A, w0, S);
    const double a0  = (A + 1.0) - (A - 1.0) * cw + two;

    BiquadCoef r;
    r.b0 = A * ((A + 1.0) + (A - 1.0) * cw + two) / a0;
    r.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
    r.b2 = A * ((A + 1.0) + (A - 1.0) * cw - two) / a0;
    r.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
    r.a2 = ((A + 1.0) - (A - 1.0) * cw - two) / a0;
    return r;
}

inline BiquadCoef lowpass(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    BiquadCoef r;
    r.b0 = (1.0 - cw) * 0.5 / a0;
    r.b1 = (1.0 - cw) / a0;
    r.b2 = (1.0 - cw) * 0.5 / a0;
    r.a1 = -2.0 * cw / a0;
    r.a2 = (1.0 - alpha) / a0;
    return r;
}

inline BiquadCoef highpass(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    BiquadCoef r;
    r.b0 = (1.0 + cw) * 0.5 / a0;
    r.b1 = -(1.0 + cw) / a0;
    r.b2 = (1.0 + cw) * 0.5 / a0;
    r.a1 = -2.0 * cw / a0;
    r.a2 = (1.0 - alpha) / a0;
    return r;
}

// Bandpass, constant 0 dB peak gain.
inline BiquadCoef bandpass(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    BiquadCoef r;
    r.b0 = alpha / a0;
    r.b1 = 0.0;
    r.b2 = -alpha / a0;
    r.a1 = -2.0 * cw / a0;
    r.a2 = (1.0 - alpha) / a0;
    return r;
}

inline BiquadCoef notch(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    BiquadCoef r;
    r.b0 = 1.0 / a0;
    r.b1 = -2.0 * cw / a0;
    r.b2 = 1.0 / a0;
    r.a1 = -2.0 * cw / a0;
    r.a2 = (1.0 - alpha) / a0;
    return r;
}

inline BiquadCoef allpass(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * Q);
    const double a0 = 1.0 + alpha;
    BiquadCoef r;
    r.b0 = (1.0 - alpha) / a0;
    r.b1 = -2.0 * cw / a0;
    r.b2 = (1.0 + alpha) / a0;
    r.a1 = -2.0 * cw / a0;
    r.a2 = (1.0 - alpha) / a0;
    return r;
}

} // namespace rbj

// ------------------------------------------------------- Vicanek matched family --

namespace matched {

// Impulse-invariance poles, Vicanek eq (12). q = 1/(2Q).
// Q is clamped first: tiny Q -> huge q -> cosh overflow; guarded here so the
// coefficients can never become NaN/Inf regardless of the caller.
inline void poles(double w0, double Q, double& a1, double& a2) {
    const double q  = 1.0 / (2.0 * clampQ(Q));
    const double ew = std::exp(-q * w0);
    a2 = ew * ew;
    if (q <= 1.0)
        a1 = -2.0 * ew * std::cos(std::sqrt(1.0 - q * q) * w0);
    else {
        // Clamp the argument so cosh cannot overflow (~+/-710 -> ~1e308).
        const double arg = std::min (700.0, std::sqrt (q * q - 1.0) * w0);
        a1 = -2.0 * ew * std::cosh(arg);
    }
}

// Magnitude-squared helper quantities evaluated at w0 (Vicanek eq 26/27).
struct MagParts {
    double phi0, phi1, phi2;   // eq 26
    double A0, A1, A2;         // eq 27
};

inline MagParts magParts(double w0, double a1, double a2) {
    MagParts m;
    m.phi0 = std::cos(w0 * 0.5); m.phi0 *= m.phi0;   // cos^2(w0/2)
    m.phi1 = std::sin(w0 * 0.5); m.phi1 *= m.phi1;   // sin^2(w0/2)
    m.phi2 = 4.0 * m.phi0 * m.phi1;
    const double p = 1.0 + a1 + a2;
    const double n = 1.0 - a1 + a2;
    m.A0 = p * p;
    m.A1 = n * n;
    m.A2 = -4.0 * a2;
    return m;
}

// Matched peaking EQ (Vicanek 4.4). G = linear peak gain; no Nyquist cramping.
inline BiquadCoef bell(double fs, double f0, double gainDb, double Q) {
    // 0 dB is the exact identity — fast path (also avoids degenerate math).
    if (std::fabs (gainDb) < 1e-9) {
        BiquadCoef r; r.b0 = 1.0; return r;
    }
    const double G  = dbToLin(gainDb);
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    double a1, a2;
    poles(w0, Q, a1, a2);
    const MagParts m = magParts(w0, a1, a2);

    const double B0 = m.A0;                                   // unity at DC
    const double R1 = (m.A0 * m.phi0 + m.A1 * m.phi1 + m.A2 * m.phi2) * G * G;
    const double R2 = (-m.A0 + m.A1 + 4.0 * (m.phi0 - m.phi1) * m.A2) * G * G;
    const double B2 = (R1 - R2 * m.phi1 - B0) / (4.0 * m.phi1 * m.phi1);
    const double B1 = R2 + B0 + 4.0 * (m.phi1 - m.phi0) * B2;

    const double sB0 = std::sqrt(B0);
    const double sB1 = std::sqrt(std::max(0.0, B1));
    const double W   = 0.5 * (sB0 + sB1);

    BiquadCoef r;
    r.b0 = 0.5 * (W + std::sqrt(std::max(0.0, W * W + B2)));
    r.b1 = 0.5 * (sB0 - sB1);
    r.b2 = -B2 / (4.0 * r.b0);
    r.a1 = a1;
    r.a2 = a2;
    return r;
}

// Matched lowpass (Vicanek 4.1). |H(f0)| = Q (=> -3.01 dB at Q = 1/sqrt(2)).
inline BiquadCoef lowpass(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    double a1, a2;
    poles(w0, Q, a1, a2);
    const MagParts m = magParts(w0, a1, a2);

    const double R1 = (m.A0 * m.phi0 + m.A1 * m.phi1 + m.A2 * m.phi2) * Q * Q;
    const double B1 = (R1 - m.A0 * m.phi0) / m.phi1;         // B0 == A0
    const double sB0 = 1.0 + a1 + a2;                        // eq 34
    const double sB1 = std::sqrt(std::max(0.0, B1));

    BiquadCoef r;
    r.b0 = 0.5 * (sB0 + sB1);
    r.b1 = sB0 - r.b0;
    r.b2 = 0.0;
    r.a1 = a1;
    r.a2 = a2;
    return r;
}

// Matched highpass (Vicanek 4.2).
inline BiquadCoef highpass(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    double a1, a2;
    poles(w0, Q, a1, a2);
    const MagParts m = magParts(w0, a1, a2);

    const double b0 = std::sqrt(std::max(0.0, m.A0 * m.phi0 + m.A1 * m.phi1 + m.A2 * m.phi2))
                      * Q / (4.0 * m.phi1);
    BiquadCoef r;
    r.b0 = b0;
    r.b1 = -2.0 * b0;
    r.b2 = b0;
    r.a1 = a1;
    r.a2 = a2;
    return r;
}

// Matched bandpass (Vicanek 4.3), unity gain at center.
inline BiquadCoef bandpass(double fs, double f0, double Q) {
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    double a1, a2;
    poles(w0, Q, a1, a2);
    const MagParts m = magParts(w0, a1, a2);

    const double R1 = m.A0 * m.phi0 + m.A1 * m.phi1 + m.A2 * m.phi2;
    const double R2 = -m.A0 + m.A1 + 4.0 * (m.phi0 - m.phi1) * m.A2;
    const double B2 = (R1 - R2 * m.phi1) / (4.0 * m.phi1 * m.phi1);
    const double B1 = R2 + 4.0 * (m.phi1 - m.phi0) * B2;

    const double b1 = -0.5 * std::sqrt(std::max(0.0, B1));
    const double b0 = 0.5 * (std::sqrt(std::max(0.0, B2 + b1 * b1)) - b1);

    BiquadCoef r;
    r.b0 = b0;
    r.b1 = b1;
    r.b2 = -b0 - b1;
    r.a1 = a1;
    r.a2 = a2;
    return r;
}

} // namespace matched

// Matched one-pole shelving filters (Vicanek 2019, "Matched One-Pole Digital
// Shelving Filters"). These match the analog shelf prototype across the whole
// audio range — including corner frequencies near/above Nyquist — where the
// bilinear-transform (RBJ) shelf droops/cramps. This is the transparent,
// "no droop" shelf behaviour reference EQs are built on.
//
// Notation: fc is normalized to Nyquist (fN = fc / (fs/2)); fm = 0.9 is the
// (improved) matching point; G is the linear shelf gain.

namespace shelfMatch {

struct ShelfParams { double alpha, beta; };

// Eq 12 (the improved variant, matching point fm = 0.9 below Nyquist).
inline ShelfParams params (double fcNorm, double G)
{
    constexpr double fm   = 0.9;
    constexpr double invFm2 = 1.0 / (fm * fm);
    constexpr double invPhim = 1.0 / (1.0 - std::cos (kPi * fm));
    constexpr double twoOverPi2 = 2.0 / (kPi * kPi);

    const double invGfc2 = 1.0 / (G * fcNorm * fcNorm);
    const double Gfc2    = G / (fcNorm * fcNorm);

    ShelfParams p;
    p.alpha = twoOverPi2 * (invFm2 + invGfc2) - invPhim;
    p.beta  = twoOverPi2 * (invFm2 + Gfc2)    - invPhim;
    return p;
}

// Eq 10: solve the one-pole feedback/feedforward ratios from alpha/beta.
inline double poleFrom (double x)
{
    const double rad = std::sqrt (std::max (0.0, 1.0 + 2.0 * x));
    return -x / (1.0 + x + rad);
}

// High shelf, unity at DC, gain G at Nyquist (b2 = a2 = 0).
inline BiquadCoef highShelf (double fs, double fc, double gainDb)
{
    const double G    = dbToLin (gainDb);
    const double fN   = std::max (1e-4, fc / (0.5 * fs));
    const ShelfParams s = params (fN, G);

    const double a1 = poleFrom (s.alpha);
    const double b  = poleFrom (s.beta);       // b = b1/b0

    BiquadCoef r;
    r.b0 = (1.0 + a1) / (1.0 + b);
    r.b1 = b * r.b0;
    r.a1 = a1;
    return r;
}

// Low shelf = G / highShelf(G)  (reciprocal transfer function, scaled by G).
inline BiquadCoef lowShelf (double fs, double fc, double gainDb)
{
    const BiquadCoef h = highShelf (fs, fc, gainDb);
    BiquadCoef r;
    r.b0 = dbToLin (gainDb) / h.b0;
    r.b1 = dbToLin (gainDb) * h.a1 / h.b0;
    r.a1 = h.b1 / h.b0;
    return r;
}

} // namespace shelfMatch

// ------------------------------------------------ TPT/ZDF state-variable ----

// Chamberlin/cytomic TPT (Topology-Preserving Transform) ZDF state-variable
// filter. Advantages over direct-form biquads: denormal-free at silence, very
// stable at low frequencies (good for high-pass cuts), and robust under
// modulation. It is a bilinear-warped design, so for the *no-cramp* property at
// high frequencies the matched coefficient designs above remain the choice —
// this topology is the LF-precision / numerical-stability tool.
struct SvFilter
{
    enum class Kind { LowPass, HighPass, BandPass, Notch, AllPass };

    void set (Kind k, double fs, double fc, double Q)
    {
        kind = k;
        // Clamp frequency (tan(pi/2) = inf) and Q (1/Q divide-by-zero) so the
        // SVF coefficients are always finite, even for hostile parameter values.
        const double f = clampFc (fc, fs);
        g  = std::tan (kPi * f / fs);
        kd = 1.0 / clampQ (Q);
        a1 = 1.0 / (1.0 + g * (g + kd));
        a2 = g * a1;
        a3 = g * a2;
        ic1eq = ic2eq = 0.0;
    }

    double process (double x)
    {
        x = sanitize (x);
        const double v3 = x - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;

        switch (kind) {
            case Kind::LowPass:  return sanitize (v2);
            case Kind::BandPass: return sanitize (kd * v1);          // unity gain at center
            case Kind::HighPass: return sanitize (x - kd * v1 - v2);
            case Kind::Notch:    return sanitize (x - kd * v1);       // low + high
            default:             return sanitize (x - kd * v1 - 2.0 * v2);  // all-pass
        }
    }

    Kind kind = Kind::LowPass;
    double g = 0.0, kd = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    double ic1eq = 0.0, ic2eq = 0.0;
};

// -------------------------------------------------- coefficient morphing ----

// Click-free coefficient changes via output crossfade. When coefficients change
// (automation, node drag), the old and new filters run in parallel and their
// outputs are blended over a short ramp — both filters are always stable, so the
// transition can never click, no matter how extreme the change.
struct MorphingBiquad
{
    void setCoeffs (const BiquadCoef& c, int ramp = 64)
    {
        if (ramp <= 0) {                       // instant (no morph)
            active.setCoef (c);
            active.reset();
            morphing = false;
            return;
        }
        incoming.setCoef (c);
        incoming.reset();
        morphing = true;
        mix = 0.0;
        step = 1.0 / std::max (1, ramp);
    }

    double process (double x)
    {
        if (!morphing)
            return active.process (x);

        const double y0 = active.process (x);
        const double y1 = incoming.process (x);
        const double y  = y0 + mix * (y1 - y0);
        mix += step;
        if (mix >= 1.0) {
            active = incoming;
            morphing = false;
        }
        return sanitize (y);
    }

    Biquad active;
    Biquad incoming;
    bool morphing = false;
    double mix = 0.0, step = 0.0;
};

// ------------------------------------------------ Orfanidis decramped peaking --

namespace orfanidis {

// Peaking EQ with a prescribed Nyquist-frequency gain (Orfanidis, J. Audio Eng.
// Soc., June 1997). The bilinear transform pins the Nyquist gain to the DC gain
// and "cramps" the high end; Orfanidis instead prescribes a Nyquist gain G1 so
// the digital filter matches the analog prototype through the whole Nyquist
// interval. This is the high-frequency-decramping core of reference EQs.
//
// Faithful transcription of Orfanidis's own peq.m:
//   G0 = DC gain (linear), G = peak gain (linear), GB = band-edge gain (linear),
//   w0 = center frequency (rad/sample), dw = bandwidth (rad/sample).
inline BiquadCoef peq (double G0, double G, double GB, double w0, double dw)
{
    const double F   = std::fabs (G * G - GB * GB);
    const double G00 = std::fabs (G * G - G0 * G0);
    const double F00 = std::fabs (GB * GB - G0 * G0);
    if (F < 1e-15 || G00 < 1e-15) {              // 0 dB gain -> passthrough
        BiquadCoef r; r.b0 = 1.0; return r;
    }

    const double w0mpi2 = w0 * w0 - kPi * kPi;   // (w0^2 - pi^2)
    const double num = G0 * G0 * w0mpi2 * w0mpi2 + G * G * F00 * kPi * kPi * dw * dw / F;
    const double den = w0mpi2 * w0mpi2 + F00 * kPi * kPi * dw * dw / F;

    // Nyquist-frequency gain. Falls back to the reference gain G0 if the
    // prescribed value would leave the real axis (band too wide + too close to
    // Nyquist) — degrades gracefully only in that pathological region.
    const double g1sq = num / den;
    const double G1 = (g1sq > 0.0) ? std::sqrt (g1sq) : G0;

    const double G01 = std::fabs (G * G - G0 * G1);
    const double G11 = std::fabs (G * G - G1 * G1);
    const double F01 = std::fabs (GB * GB - G0 * G1);
    const double F11 = std::fabs (GB * GB - G1 * G1);

    const double t = std::tan (0.5 * w0);
    const double W2 = std::sqrt (G11 / G00) * t * t;
    const double DW = (1.0 + std::sqrt (F00 / F11) * W2) * std::tan (0.5 * dw);

    const double C = F11 * DW * DW - 2.0 * W2 * (F01 - std::sqrt (F00 * F11));
    const double D = 2.0 * W2 * (G01 - std::sqrt (G00 * G11));

    const double A = std::sqrt ((C + D) / F);
    const double B = std::sqrt ((G * G * C + GB * GB * D) / F);

    const double denC = 1.0 + W2 + A;
    BiquadCoef r;
    r.b0 = (G1 + G0 * W2 + B) / denC;
    r.b1 = -2.0 * (G1 - G0 * W2) / denC;
    r.b2 = (G1 - B + G0 * W2) / denC;
    r.a1 = -2.0 * (1.0 - W2) / denC;
    r.a2 = (1.0 + W2 - A) / denC;
    return r;
}

// Constant-Q peaking EQ in dB: DC gain 0 dB, band-edge gain = half the peak
// (on the dB scale, i.e. the geometric mean), Q -> bandwidth dw = w0 / Q.
inline BiquadCoef bell (double fs, double f0, double gainDb, double Q)
{
    if (std::fabs (gainDb) < 1e-9) {
        BiquadCoef r; r.b0 = 1.0; return r;
    }
    const double G  = dbToLin (gainDb);
    const double GB = std::sqrt (G);            // half-gain on the dB scale
    const double w0 = 2.0 * kPi * clampFc (f0, fs) / fs;
    const double dw = w0 / clampQ (Q);          // constant-Q bandwidth
    return peq (1.0, G, GB, w0, dw);
}

} // namespace orfanidis

// ------------------------------------------------------------ first-order tilt --

// First-order shelf with DC gain g0 and Nyquist gain g1 (BLT, prewarped at f0).
// Used for "Flat Tilt": g0 = G, g1 = 1/G — a constant tilt across the spectrum.
inline BiquadCoef firstOrderTilt(double fs, double f0, double gainDb) {
    const double G = dbToLin(gainDb);                 // DC gain
    const double w = std::tan(kPi * clampFc (f0, fs) / fs);   // tan(w0/2), clamped
    const double g0 = G, g1 = 1.0 / std::max (G, 1e-12);
    const double den = w + 1.0;
    BiquadCoef r;
    r.b0 = (g0 * w + g1) / den;
    r.b1 = (g0 * w - g1) / den;
    r.b2 = 0.0;
    r.a1 = (w - 1.0) / den;
    r.a2 = 0.0;
    return r;
}

// First-order lowpass/highpass (BLT, prewarped) for 6 dB/oct cuts.
inline BiquadCoef firstOrderLowpass(double fs, double f0) {
    const double k = std::tan(kPi * clampFc (f0, fs) / fs);   // tan(w0/2), clamped
    const double den = k + 1.0;
    BiquadCoef r;
    r.b0 = k / den;
    r.b1 = k / den;
    r.b2 = 0.0;
    r.a1 = (k - 1.0) / den;
    r.a2 = 0.0;
    return r;
}

inline BiquadCoef firstOrderHighpass(double fs, double f0) {
    const double k = std::tan(kPi * clampFc (f0, fs) / fs);   // tan(w0/2), clamped
    const double den = k + 1.0;
    BiquadCoef r;
    r.b0 = 1.0 / den;
    r.b1 = -1.0 / den;
    r.b2 = 0.0;
    r.a1 = (k - 1.0) / den;
    r.a2 = 0.0;
    return r;
}

// Analog-style gain-Q interaction (Pro-Q "Gain-Q Link"): Q and gain influence
// each other — bandwidth narrows as |gain| grows, matching the feel of analog
// console EQ. v1 linear model (documented approximation of the reference curves).
inline double gainQLinkedQ (double Q, double gainDb)
{
    return Q * (1.0 + std::fabs (gainDb) / 18.0);
}

} // namespace flush
