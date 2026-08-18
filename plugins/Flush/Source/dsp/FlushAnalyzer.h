// Flush — FFT spectrum analyzer (pure C++, no JUCE)
//
// The Pro-Q4-quality visualizer: Hann-windowed FFT with 75% overlap,
// exponential power averaging, a configurable pink-noise tilt, soft peak hold,
// and a decimated dB magnitude output for the GPU canvas. This is a *display*
// stage — it never touches the audio path; it only observes it.

#pragma once

#include <vector>
#include <cmath>
#include "FlushCommon.h"
#include "FlushFft.h"

namespace flush {

class Analyzer
{
public:
    void reset (double fs, int fftSize = 4096)
    {
        fs_ = fs;
        fftSize_ = fftSize;
        hop_ = fftSize / 4;                        // 75% overlap
        half_ = fftSize / 2 + 1;

        buf_.assign (fftSize, 0.0);
        write_ = 0;
        window_ = hannWindow (fftSize);
        work_.assign (fftSize, {0.0, 0.0});

        avg_.assign (half_, 0.0);
        peak_.assign (half_, -1e9);
        frozenPeak_.assign (half_, -1e9);
        out_.assign (512, -90.0f);

        setSpeed (1);                              // medium
        tiltDbOct_ = 4.5;
        rangeDb_ = 90.0;
        frozen_ = false;

        // Peak hold fall (~1.5 s), expressed per FFT hop.
        const double hopsPerSec = fs / hop_;
        peakDecay_ = std::exp (-1.0 / (1.5 * hopsPerSec));
    }

    // 0 slow, 1 medium, 2 fast (averaging time constant).
    void setSpeed (int s)
    {
        const double alpha[3] = { 0.985, 0.95, 0.85 };
        alpha_ = alpha[std::max (0, std::min (2, s))];
    }
    void setTilt (double dBOct)   { tiltDbOct_ = std::max (0.0, std::min (12.0, sanitize (dBOct))); }
    void setRange (double dB)     { rangeDb_ = std::max (30.0, std::min (120.0, sanitize (dB))); }

    // Freeze = hold the current peak envelope as a hard snapshot. While frozen,
    // the display shows exactly what was there at the moment of freeze (no decay,
    // no live update) — FabFilter's analyzer Freeze.
    void setFreeze (bool f)
    {
        if (f && !frozen_)
            frozenPeak_ = peak_;                     // snapshot on the rising edge
        frozen_ = f;
    }

    // Feed mono samples (mix of L/R); FFT + average happen internally.
    void process (const double* mono, int n)
    {
        if (mono == nullptr || n <= 0) return;
        for (int i = 0; i < n; ++i) {
            buf_[write_++] = sanitize (mono[i]);
            if (write_ == fftSize_) {
                analyze ();
                // Shift by the hop (75% overlap with the previous window).
                for (int j = 0; j < fftSize_ - hop_; ++j) buf_[j] = buf_[j + hop_];
                write_ = fftSize_ - hop_;
            }
        }
    }

    // Decimated, tilt-applied, peak-held magnitudes in dB ([-range, +12]).
    // Each output index covers a band of FFT bins; the band MAX is taken so
    // narrow spectral peaks are preserved through decimation (naive subsampling
    // would read the skirt of a narrow peak and lose 10+ dB).
    //
    // THREAD NOTE: process() runs on the audio thread; magnitudes() is called
    // from the UI thread. This is a display-only best-effort read — a concurrent
    // FFT write can tear a column for one frame, which is visually negligible
    // and never touches the audio path.
    const std::vector<float>& magnitudes()
    {
        const int N = (int)out_.size();
        const double fRef = 1000.0;
        const double binHz = fs_ / (2.0 * (half_ - 1));    // Hz per FFT bin

        for (int i = 0; i < N; ++i) {
            const int b0 = (int)((double)i * half_ / N);
            const int b1 = std::min (half_, (int)((double)(i + 1) * half_ / N + 0.5));
            double power = 0.0;
            if (frozen_) {
                // Frozen: use the snapshot only (live avg_/peak_ are ignored).
                for (int b = b0; b < b1; ++b)
                    power = std::max (power, frozenPeak_[b]);
            } else {
                for (int b = b0; b < b1; ++b) {
                    peak_[b] = std::max (avg_[b], peak_[b] * peakDecay_);
                    power = std::max (power, std::max (avg_[b], peak_[b]));
                }
            }

            const double f = 0.5 * (b0 + b1) * binHz;
            double db = (power > 1e-16) ? 10.0 * std::log10 (power) : -rangeDb_;
            db += tiltDbOct_ * std::log2 (std::max (1e-3, f / fRef));
            // Upper clamp allows the tilt to push the display above 0 dBFS
            // (Pro-Q-style tilt display), but stays bounded.
            out_[i] = (float)std::max (-rangeDb_, std::min (12.0, db));
        }
        return out_;
    }

private:
    void analyze()
    {
        for (int i = 0; i < fftSize_; ++i)
            work_[i] = { buf_[i] * window_[i], 0.0 };

        fft (work_.data(), fftSize_, false);

        // Hann coherent gain: a full-scale sine reads 0 dB after this scale.
        // DC and Nyquist bins carry the window's full energy in a single bin
        // (|X| = A*N/2), so they need 2/N — otherwise they read +6 dB hot.
        for (int k = 0; k < half_; ++k) {
            const double scale = (k == 0 || k == half_ - 1) ? 2.0 / fftSize_ : 4.0 / fftSize_;
            const double re = work_[k].re * scale;
            const double im = work_[k].im * scale;
            const double p = re * re + im * im;
            avg_[k] = alpha_ * avg_[k] + (1.0 - alpha_) * p;
        }
    }

    double fs_ = 48000.0;
    int fftSize_ = 4096, hop_ = 1024, half_ = 2049, write_ = 0;

    std::vector<double> buf_, window_;
    std::vector<Complex> work_;
    std::vector<double> avg_, peak_, frozenPeak_;
    std::vector<float> out_;

    double alpha_ = 0.95;
    double tiltDbOct_ = 4.5;
    double rangeDb_ = 90.0;
    double peakDecay_ = 0.99;
    bool frozen_ = false;
};

} // namespace flush
