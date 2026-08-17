// Flush — DSP common utilities (pure C++, double precision, no JUCE dependency)
//
// This header is deliberately dependency-free so the DSP core can be unit-tested
// standalone with a plain C++ compiler. Everything that affects sound is computed
// in double precision (float64) here.
#pragma once

#include <cmath>
#include <algorithm>
#include <vector>

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

// Simple integer-sample delay line (for compressor lookahead: the audio is
// delayed, the undelayed gain reduction is applied to it). Delay is set once at
// configure time; process() is allocation-free.
struct DelayLine {
    std::vector<double> buf;
    int delay = 0;
    int pos = 0;

    void setDelay(int d) {
        delay = std::max(0, d);
        // Buffer of exactly `delay` slots: read-then-write at `pos` yields a
        // delay of exactly `delay` samples (a size of delay+1 would be one
        // sample too long — the impulse-delay test caught this off-by-one).
        buf.assign(std::max(1, delay), 0.0);
        pos = 0;
    }

    double process(double x) {
        if (delay == 0) return sanitize(x);
        const double y = buf[pos];
        buf[pos] = sanitize(x);
        pos = (pos + 1) % (int)buf.size();
        return y;
    }
};

} // namespace flush
