// Flush — DSP common utilities (pure C++, double precision, no JUCE dependency)
//
// This header is deliberately dependency-free so the DSP core can be unit-tested
// standalone with a plain C++ compiler. Everything that affects sound is computed
// in double precision (float64) here.
#pragma once

#include <cmath>
#include <algorithm>

namespace flush {

inline constexpr double kPi = 3.14159265358979323846;

inline double dbToLin(double db)  { return std::pow(10.0, db / 20.0); }
inline double linToDb(double lin) { return (lin > 1e-12) ? 20.0 * std::log10(lin) : -120.0; }

// Sanitize a sample value: any NaN/Inf (e.g. a broken preset or a denormal
// mishap upstream) is collapsed to 0.0 so it cannot poison filter state or the
// meters. Cheap (2 compares) and safe on the audio thread.
inline double sanitize(double x) { return std::isfinite(x) ? x : 0.0; }

// One-pole smoother (exponential moving average / RC lowpass).
// tau = time constant in seconds (the time to ~63% of a step).
struct OnePole {
    double a = 0.0;   // smoothing coefficient
    double y = 0.0;   // current state

    void setTau(double tau, double sampleRate) {
        // Guard degenerate time constants and sample rates -> coefficient 0
        // (state frozen), never NaN from exp(-inf) or division by zero.
        if (!(tau > 0.0) || !(sampleRate > 0.0)) { a = 0.0; return; }
        a = 1.0 - std::exp(-1.0 / (tau * sampleRate));
        a = sanitize(a);
    }
    void setCoeff(double coeff) { a = std::max(0.0, std::min(1.0, sanitize(coeff))); }
    void reset() { y = 0.0; }
    double update(double x) { y += a * (sanitize(x) - y); return y; }
    double value() const { return y; }
};

} // namespace flush
