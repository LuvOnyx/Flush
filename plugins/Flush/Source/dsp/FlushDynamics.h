// Flush — Unified Dynamics Engine (pure C++, double precision, no JUCE)
//
// One detector -> gain-computer -> ballistics core, instantiated two ways:
//   * broadband instance  = the glue compressor (full signal -> full gain)
//   * per-band instances  = the dynamic EQ bands (band-filtered sidechain
//     -> band-limited gain). A dynamic band is a band-limited compressor.
//
// The engine returns a gain *reduction* in dB for a sidechain sample so the
// caller decides how to apply it (broadband vs. band-limited, makeup, metering).

#pragma once

#include "FlushCommon.h"

namespace flush {

struct DynamicsParams {
    double thresholdDb = -18.0;
    double ratio       = 4.0;       // >= 1 (compressor); infinity clamps internally
    double attackSec   = 0.010;
    double releaseSec  = 0.150;
    double kneeDb      = 6.0;       // soft-knee width (0 = hard knee)
    double rangeDb     = 60.0;      // max gain reduction (dynamic EQ: dyn_range)
    bool   expand      = false;     // true = downward expander (v1.1)
    bool   rms         = false;     // RMS detector instead of peak

    // Oxford-style "transparent" behaviour: program-dependent release. When gain
    // reduction is deep, the release slows (the compressor "hangs" on heavy GR
    // and lets go on light GR), which is what makes a clean mastering compressor
    // feel invisible instead of pumping. 0 disables (fixed release).
    bool   adaptiveRelease = true;
    double adaptiveAmount  = 4.0;   // 0 = fixed; ~4 = classic transparent hang

    // Lookahead (seconds): the CALLER delays the audio by this amount and applies
    // the (undelayed) gain reduction to the delayed audio, so the attack is fully
    // ramped BEFORE a transient arrives — no initial "grab" distortion. The engine
    // only REPORTS the latency; it never delays the GR itself (a delayed GR on
    // undelayed audio would be a LAG, not a lookahead). 0 disables.
    double lookaheadSec = 0.0;
};

class DynamicsEngine {
public:
    void reset(double sampleRate) {
        sr_ = sampleRate;
        envDb_ = -120.0;
        lastRelGr_ = -1.0;
        rms_.reset();
        grSmooth_.reset();
        applyParams();
    }

    void setParams(const DynamicsParams& p) {
        p_ = p;
        // Clamp hostile values (broken presets/automation) so the coefficient
        // math can never produce NaN/Inf or an inaudible/infinite time constant.
        p_.attackSec  = std::max(0.0001, sanitize(p.attackSec));
        p_.releaseSec = std::max(0.001,  sanitize(p.releaseSec));
        p_.ratio      = std::max(1.0,    sanitize(p.ratio));
        p_.kneeDb     = std::max(0.0,    sanitize(p.kneeDb));
        p_.rangeDb    = std::max(0.0,    sanitize(p.rangeDb));
        p_.thresholdDb = sanitize(p.thresholdDb);
        p_.adaptiveAmount = std::max(0.0, sanitize(p.adaptiveAmount));
        p_.lookaheadSec   = std::max(0.0, std::min(0.05, sanitize(p.lookaheadSec)));
        lookaheadSamples_ = (int)std::lround (p_.lookaheadSec * sr_);
        applyParams();
    }

    // Advance one sidechain sample; return the gain reduction in dB (>= 0).
    double processGrDb(double sideSample) {
        sideSample = sanitize (sideSample);

        // 1) detector
        double level;
        if (p_.rms) {
            level = std::sqrt(std::max(0.0, rms_.update(sideSample * sideSample)));
        } else {
            level = std::fabs(sideSample);
        }
        const double levelDb = linToDb(level);

        // 2) ballistics (attack when rising, release when falling)
        const double coeff = (levelDb > envDb_) ? atkCoeff_ : relCoeff_;
        envDb_ += coeff * (levelDb - envDb_);

        // 3) soft-knee gain computer
        const double over = envDb_ - p_.thresholdDb;
        double gr;
        if (over <= -0.5 * p_.kneeDb) {
            gr = 0.0;
        } else if (over >= 0.5 * p_.kneeDb) {
            gr = over;
        } else {
            const double t = over + 0.5 * p_.kneeDb;
            gr = t * t / (2.0 * p_.kneeDb);
        }
        gr *= (1.0 - 1.0 / p_.ratio);
        gr = std::min(std::max(gr, 0.0), p_.rangeDb);

        grSmooth_.update(gr);

        // 4) adaptive (program-dependent) release — deeper GR, slower release.
        //    Recomputed only when the smoothed GR moves by > 0.5 dB (cheap).
        if (p_.adaptiveRelease && p_.adaptiveAmount > 0.0) {
            const double g = grSmooth_.value();
            if (std::fabs(g - lastRelGr_) > 0.5) {
                lastRelGr_ = g;
                const double depth = g / std::max(p_.rangeDb, 1.0);
                const double relSec = p_.releaseSec * (1.0 + p_.adaptiveAmount * depth);
                relCoeff_ = 1.0 - std::exp(-1.0 / (relSec * sr_));
            }
        }

        return gr;
    }

    double currentGrDb() const { return grSmooth_.value(); }   // un-delayed (meter)
    double envDb() const { return envDb_; }
    double releaseCoeff() const { return relCoeff_; }          // for verification
    int lookaheadLatencySamples() const { return lookaheadSamples_; }

private:
    void applyParams() {
        if (sr_ > 0.0) {
            atkCoeff_ = 1.0 - std::exp(-1.0 / (p_.attackSec * sr_));
            relCoeff_ = 1.0 - std::exp(-1.0 / (p_.releaseSec * sr_));
            atkCoeff_ = sanitize(atkCoeff_);
            relCoeff_ = sanitize(relCoeff_);
        }
        if (p_.rms) rms_.setTau(std::max(0.0005, p_.attackSec), sr_);
        grSmooth_.setTau(0.050, sr_);   // meter smoothing (50 ms)
    }

    DynamicsParams p_;
    double sr_ = 48000.0;
    double envDb_ = -120.0;
    double atkCoeff_ = 0.0, relCoeff_ = 0.0;
    double lastRelGr_ = -1.0;
    OnePole rms_;
    OnePole grSmooth_;
    int lookaheadSamples_ = 0;
};

} // namespace flush
