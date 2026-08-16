// Flush — Flush Match auto-gain stage (pure C++, double precision, no JUCE)
//
// The hero feature: a loudness-matching output stage designed for the mixbus.
//   * Match Input : output loudness tracks input loudness (EQ/comp change the
//                   tone, never the level).
//   * Target      : output loudness normalizes to a reference level.
//
// "Loudness" here is **ITU-R BS.1770-4 K-weighted** — the perceptual, streaming-era
// definition — not raw sample RMS. That is Flush's differentiation: gain staging
// is a loudness problem, so the hero feature measures loudness.
//
// It is engineered to level a full mix WITHOUT pumping:
//   * long window (~1-2 s effective) on the measured K-weighted loudness,
//   * very slow gain smoothing (~1-3 s) on the applied gain,
//   * selectable loudness reference: Mid (mono-sum) — width-stable default —
//     or Stereo (Mid+Side energy = L^2 + R^2).
//
// Measurement is open-loop: input and (pre-gain) output loudness are measured
// independently, the delta is computed, then applied as a slow gain — so the
// loop cannot oscillate the way a naive feedback AGC would.

#pragma once

#include "FlushCommon.h"
#include "FlushLoudness.h"

namespace flush {

class FlushMatch {
public:
    enum class Mode { Off, MatchInput, Target };
    enum class Reference { Mid, Stereo };

    void reset(double sampleRate) {
        sr_ = sampleRate;
        kwInL_.init(sampleRate); kwInR_.init(sampleRate);
        kwOutL_.init(sampleRate); kwOutR_.init(sampleRate);
        inLoud_.reset();
        outLoud_.reset();
        gainDb_.reset();
        applyTiming();
    }

    void setMode(Mode m) { mode_ = m; }
    void setReference(Reference r) { ref_ = r; }
    void setTargetDb(double db) { targetDb_ = db; }

    // 0 = Slow (Bus), 1 = Medium, 2 = Fast.
    void setTiming(int timing) {
        timing_ = std::max(0, std::min(2, timing));
        applyTiming();
    }

    // Measure one input sample pair (before the processing chain).
    void pushInput(double l, double r) {
        inLoud_.update(loudnessPower(kwInL_.process(l), kwInR_.process(r)));
    }

    // Measure one output sample pair (after EQ/comp, BEFORE the flush gain).
    void pushOutput(double l, double r) {
        outLoud_.update(loudnessPower(kwOutL_.process(l), kwOutR_.process(r)));
    }

    // Advance the auto-gain one step; returns the linear gain to apply to the
    // output sample that was just pushed via pushOutput().
    double tickGain() {
        if (mode_ == Mode::Off)
            return dbToLin(gainDb_.value());   // frozen at last value

        const double inDb  = linToDb(std::sqrt(std::max(0.0, inLoud_.value())));
        const double outDb = linToDb(std::sqrt(std::max(0.0, outLoud_.value())));
        const double target = (mode_ == Mode::Target) ? targetDb_ : inDb;
        const double delta = target - outDb;

        // Clamp the auto-gain to a sane safety window so a mute/silence never
        // slams the output up +inf.
        const double clamped = std::max(-36.0, std::min(36.0, delta));
        return dbToLin(gainDb_.update(clamped));
    }

    // Current smoothed auto-gain in dB (for the "Flush: +x.x dB" readout).
    double deltaDb() const { return gainDb_.value(); }

private:
    double loudnessPower(double l, double r) const {
        if (ref_ == Reference::Mid) {
            // mono-sum (L+R)/2, squared  => proportional to (L+R)^2
            const double m = 0.5 * (l + r);
            return m * m;
        }
        // Stereo: average power of L and R (= (Mid^2 + Side^2)/2 energy).
        return 0.5 * (l * l + r * r);
    }

    void applyTiming() {
        if (sr_ <= 0.0) return;
        // RMS window (seconds) and gain smoothing (seconds) per timing mode.
        double rmsTau, gainTau;
        switch (timing_) {
            case 0: rmsTau = 0.80; gainTau = 1.60; break;  // Slow (Bus)
            case 2: rmsTau = 0.15; gainTau = 0.25; break;  // Fast
            default: rmsTau = 0.40; gainTau = 0.70; break; // Medium
        }
        inLoud_.setTau(rmsTau, sr_);
        outLoud_.setTau(rmsTau, sr_);
        gainDb_.setTau(gainTau, sr_);
    }

    Mode mode_ = Mode::MatchInput;
    Reference ref_ = Reference::Mid;
    int timing_ = 0;
    double targetDb_ = -18.0;
    double sr_ = 48000.0;

    KWeighting kwInL_, kwInR_, kwOutL_, kwOutR_;
    OnePole inLoud_;    // input K-weighted loudness power
    OnePole outLoud_;   // output (pre-gain) K-weighted loudness power
    OnePole gainDb_;    // smoothed auto-gain in dB
};

} // namespace flush
