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

// One-pole smoother (exponential moving average / RC lowpass).
// tau = time constant in seconds (the time to ~63% of a step).
struct OnePole {
    double a = 0.0;   // smoothing coefficient
    double y = 0.0;   // current state

    void setTau(double tau, double sampleRate) {
        a = 1.0 - std::exp(-1.0 / (tau * sampleRate));
    }
    void setCoeff(double coeff) { a = coeff; }
    void reset() { y = 0.0; }
    double update(double x) { y += a * (x - y); return y; }
    double value() const { return y; }
};

} // namespace flush
