// Flush — Spectral Dynamics (pure C++, no JUCE)
//
// Pro-Q4's newest feature: frequency-domain dynamics. Instead of a broadband
// compressor (or a band-limited dynamic EQ), the FFT bins themselves are
// gain-reduced independently — only the frequencies that exceed the threshold
// are attenuated, leaving everything else untouched. This is the most surgical
// "de-esser / resonance tamer" behaviour available, and it is a genuinely
// different class of processor from the time-domain dynamics engine.
//
// Implementation: weighted overlap-add STFT (Hann, 75% overlap) with per-bin
// attack/release smoothing. Adds a fixed latency of (fftSize - hop) samples.

#pragma once

#include <vector>
#include <deque>
#include <cmath>
#include "FlushCommon.h"
#include "FlushFft.h"

namespace flush {

struct SpectralParams {
    double thresholdDb = -20.0;
    double ratio       = 20.0;     // per-bin ratio (high = limiter-like)
    double rangeDb     = 24.0;     // max per-bin gain reduction
    double attackSec   = 0.010;
    double releaseSec  = 0.150;
    double kneeDb      = 6.0;
};

class SpectralDynamics
{
public:
    void reset (double fs, int fftSize = 2048)
    {
        // Force the FFT size to a power of two >= 256 — the radix-2 FFT is
        // undefined on other sizes, and N < 4 would make hop_ == 0 (infinite
        // loop / out-of-bounds).
        fftSize = std::max (256, nextPow2 (fftSize));
        fs_ = (fs > 0.0) ? fs : 48000.0;
        N_ = fftSize;
        hop_ = N_ / 4;                        // 75% overlap
        latency_ = N_ - hop_;

        window_ = hannWindow (N_);
        synScale_ = 2.0 / 3.0;                // Hann COLA gain for hop = N/4

        inBuf_.assign (N_, 0.0);
        inAcc_.assign (hop_, 0.0);
        inCount_ = 0;
        outBuf_.assign (N_, 0.0);
        fft_ = std::vector<Complex> (N_, {0.0, 0.0});
        gr_.assign (N_ / 2 + 1, 0.0);

        // Output delay (overlap-add latency) — a FIFO pre-filled with silence.
        // Each processed frame contributes `hop` output samples that represent
        // time (now - latency), so they are delayed by exactly `latency` samples.
        outDelay_.clear();
        for (int i = 0; i < latency_; ++i) outDelay_.push_back (0.0);

        peakGr_ = 0.0;
        applyParams();
    }

    void setParams (const SpectralParams& p)
    {
        p_ = p;
        // Clamp hostile values (broken presets/automation) so the per-frame
        // coefficient math and gain computer stay finite.
        p_.attackSec  = std::max (0.0001, sanitize (p.attackSec));
        p_.releaseSec = std::max (0.001,  sanitize (p.releaseSec));
        p_.ratio      = std::max (1.0,    sanitize (p.ratio));
        p_.kneeDb     = std::max (0.0,    sanitize (p.kneeDb));
        p_.rangeDb    = std::max (0.0,    sanitize (p.rangeDb));
        p_.thresholdDb = sanitize (p.thresholdDb);
        applyParams();
    }

    // Process n samples in place (buffered internally for any block size).
    void process (double* io, int n)
    {
        if (io == nullptr || n <= 0) return;
        for (int i = 0; i < n; ++i) {
            inAcc_[inCount_++] = sanitize (io[i]);
            if (inCount_ == hop_) {
                pushFrame();
                inCount_ = 0;
            }
            io[i] = outDelay_.front();
            outDelay_.pop_front();
        }
    }

    int latency() const { return latency_; }
    double peakGrDb() const { return peakGr_; }

private:
    void applyParams()
    {
        if (fs_ > 0.0) {
            // The envelope updates once per FFT frame (hop samples), so the
            // ballistics time constants are expressed in frames, not samples.
            const double frameRate = fs_ / hop_;
            atk_ = 1.0 - std::exp (-1.0 / (p_.attackSec * frameRate));
            rel_ = 1.0 - std::exp (-1.0 / (p_.releaseSec * frameRate));
        }
    }

    void pushFrame()
    {
        // Slide the input buffer and append the new hop.
        for (int i = 0; i < N_ - hop_; ++i) inBuf_[i] = inBuf_[i + hop_];
        for (int i = 0; i < hop_; ++i) inBuf_[N_ - hop_ + i] = inAcc_[i];

        // Hann window + FFT.
        for (int i = 0; i < N_; ++i) fft_[i] = { inBuf_[i] * window_[i], 0.0 };
        fft (fft_.data(), N_, false);

        // Per-bin dynamics.
        peakGr_ = 0.0;
        for (int k = 0; k <= N_ / 2; ++k) {
            // Hann coherent gain 0.5 -> a full-scale sine (amplitude 1.0) reads
            // 0 dBFS: |bin| = A*N/4, so 4/N * |bin| = A. The DC and Nyquist bins
            // are the exception: the window's full DC energy lands in a single
            // bin (|X| = A*N/2), so they need 2/N — a uniform 4/N would read
            // them 2x loud (+6 dB) and over-compress DC / Nyquist content.
            const double norm = (k == 0 || k == N_ / 2) ? 2.0 / N_ : 4.0 / N_;
            const double mag = norm * std::hypot (fft_[k].re, fft_[k].im);
            const double db = linToDb (mag);

            const double over = db - p_.thresholdDb;
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
            gr = std::min (std::max (gr, 0.0), p_.rangeDb);

            // Attack/release per bin (independent smoothness across the spectrum).
            gr_[k] = (gr > gr_[k]) ? gr_[k] + atk_ * (gr - gr_[k])
                                   : gr_[k] + rel_ * (gr - gr_[k]);
            peakGr_ = std::max (peakGr_, gr_[k]);

            const double gain = dbToLin (-gr_[k]);
            fft_[k].re *= gain; fft_[k].im *= gain;
            if (k > 0 && k < N_ / 2) {          // mirror the negative frequencies
                fft_[N_ - k].re *= gain;
                fft_[N_ - k].im *= gain;
            }
        }

        // Inverse FFT + synthesis window + overlap-add.
        ifft (fft_.data(), N_);
        for (int i = 0; i < N_; ++i)
            outBuf_[i] += fft_[i].re * window_[i] * synScale_;

        // The first `hop` output samples are now fully accumulated; enqueue them
        // into the output delay FIFO (they emerge `latency` samples later).
        for (int i = 0; i < hop_; ++i)
            outDelay_.push_back (outBuf_[i]);
        // Shift the overlap-add buffer.
        for (int i = 0; i < N_ - hop_; ++i) outBuf_[i] = outBuf_[i + hop_];
        for (int i = N_ - hop_; i < N_; ++i) outBuf_[i] = 0.0;
    }

    SpectralParams p_;
    double fs_ = 48000.0;
    int N_ = 2048, hop_ = 512, latency_ = 1536;

    std::vector<double> window_;
    double synScale_ = 1.0;

    std::vector<double> inBuf_, inAcc_, outBuf_;
    std::vector<Complex> fft_;
    std::vector<double> gr_;
    int inCount_ = 0;

    std::deque<double> outDelay_;

    double atk_ = 0.0, rel_ = 0.0;
    double peakGr_ = 0.0;
};

} // namespace flush
