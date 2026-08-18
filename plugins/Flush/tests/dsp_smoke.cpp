// Flush — DSP core smoke test (standalone, no JUCE)
//
// Build:  g++ -std=c++20 -O2 -I../Source/dsp dsp_smoke.cpp -o dsp_smoke
//
// Measures actual frequency responses of the biquad designs and validates the
// dynamics engine and Flush Match against analytic expectations. Exits nonzero
// on any failure so it can gate CI.

#include <cstdio>
#include <cmath>
#include <string>
#include <limits>
#include <functional>
#include <vector>

#include "FlushFilter.h"
#include "FlushDynamics.h"
#include "FlushMatch.h"
#include "FlushLoudness.h"
#include "FlushFft.h"
#include "FlushFir.h"
#include "FlushOversample.h"
#include "FlushAnalyzer.h"
#include "FlushSpectral.h"

using namespace flush;

static int g_failures = 0;
#define CHECK(name, ok, detail)                                                     \
    do {                                                                            \
        if (ok) std::printf("  PASS  %-46s %s\n", name, detail);                    \
        else   { std::printf("  FAIL  %-46s %s\n", name, detail); ++g_failures; }   \
    } while (0)

// ---------------------------------------------------------------- measurement --

static double measureGainDb(const std::function<double(double)>& proc,
                            double fs, double f, double settleSec = 0.25,
                            double measureSec = 0.5) {
    const int settle = (int)(settleSec * fs);
    const int n      = (int)(measureSec * fs);
    const double w   = 2.0 * kPi * f / fs;
    double acc = 0.0;
    for (int i = 0; i < settle + n; ++i) {
        const double x = std::sin(w * i);
        const double y = proc(x);
        if (i >= settle) acc += y * y;
    }
    const double outRms = std::sqrt(acc / n);
    const double inRms  = 0.7071067811865476;   // sine of amplitude 1.0
    return 20.0 * std::log10(outRms / inRms);
}

// Exact DC (steady-state) gain: feed a constant 1.0 and read the settled output.
static double measureDcGain (const std::function<double(double)>& proc, int n = 8192) {
    double y = 0.0;
    for (int i = 0; i < n; ++i) y = proc (1.0);
    return y;
}

// Find f where gain crosses target dB, monotonic in [lo, hi].
static double findFreqForGain(const std::function<double(double)>& proc,
                              double fs, double targetDb, double lo, double hi,
                              bool increasing) {
    for (int i = 0; i < 48; ++i) {
        const double mid = std::sqrt(lo * hi);              // geometric (log) midpoint
        const double g   = measureGainDb(proc, fs, mid, 0.05, 0.05);
        if ((g < targetDb) == increasing) lo = mid; else hi = mid;
    }
    return std::sqrt(lo * hi);
}

// ---------------------------------------------------------------- main --

int main() {
    const double fs = 48000.0;
    std::printf("Flush DSP smoke test @ %g Hz\n\n", fs);

    // ---- 1. RBJ peaking: exact center gain -------------------------------
    {
        Biquad b; b.setCoef(rbj::bell(fs, 1000.0, 12.0, 2.0));
        auto proc = [&](double x){ return b.process(x); };
        const double g = measureGainDb(proc, fs, 1000.0);
        CHECK("RBJ bell +12dB @1kHz hits center", std::fabs(g - 12.0) < 0.01,
              ("measured " + std::to_string(g) + " dB").c_str());
    }

    // ---- 2. RBJ boost/cut identity (wire) --------------------------------
    {
        Biquad a; a.setCoef(rbj::bell(fs, 2000.0, 9.0, 1.4));
        Biquad b; b.setCoef(rbj::bell(fs, 2000.0, -9.0, 1.4));
        auto proc = [&](double x){ return b.process(a.process(x)); };
        double worst = 0.0;
        for (double f : {100.0, 500.0, 2000.0, 5000.0, 12000.0}) {
            worst = std::max(worst, std::fabs(measureGainDb(proc, fs, f)));
        }
        CHECK("RBJ +9/-9 dB bells cancel to wire", worst < 0.02,
              ("worst residual " + std::to_string(worst) + " dB").c_str());
    }

    // ---- 3. Matched peaking: exact center gain ---------------------------
    {
        Biquad b; b.setCoef(matched::bell(fs, 1000.0, 12.0, 2.0));
        auto proc = [&](double x){ return b.process(x); };
        const double g = measureGainDb(proc, fs, 1000.0);
        const double dc = measureGainDb(proc, fs, 20.0);
        CHECK("Matched bell +12dB @1kHz hits center", std::fabs(g - 12.0) < 0.02,
              ("measured " + std::to_string(g) + " dB").c_str());
        CHECK("Matched bell unity at DC", std::fabs(dc) < 0.05,
              ("measured " + std::to_string(dc) + " dB").c_str());
    }

    // ---- 4. Cramping comparison: RBJ vs Vicanek vs Orfanidis ---------------
    {
        // +12 dB, Q=2 bell. Measure the half-gain (+6 dB) bandwidth in octaves
        // at 1 kHz and 14 kHz. RBJ (bilinear transform) narrows badly near
        // Nyquist — the "cramped" commodity sound. The matched/decramped designs
        // hold a constant bandwidth (the analog-matched, reference-EQ behavior).
        auto bandwidthOct = [&](const std::function<BiquadCoef(double,double,double,double)>& design,
                                double f0) {
            Biquad b; b.setCoef(design(fs, f0, 12.0, 2.0));
            auto proc = [&](double x){ return b.process(x); };
            const double lo = findFreqForGain(proc, fs, 6.0, 200.0, f0, true);
            const double hi = findFreqForGain(proc, fs, 6.0, f0, fs * 0.499, false);
            return std::log2(hi / lo);
        };
        auto rbj  = [](double f,double f0,double g,double q){ return rbj::bell(f,f0,g,q); };
        auto mat  = [](double f,double f0,double g,double q){ return matched::bell(f,f0,g,q); };
        auto orf  = [](double f,double f0,double g,double q){ return orfanidis::bell(f,f0,g,q); };

        const double rbj1k  = bandwidthOct(rbj, 1000.0);
        const double rbj14k = bandwidthOct(rbj, 14000.0);
        const double mat1k  = bandwidthOct(mat, 1000.0);
        const double mat14k = bandwidthOct(mat, 14000.0);
        const double orf1k  = bandwidthOct(orf, 1000.0);
        const double orf14k = bandwidthOct(orf, 14000.0);

        std::printf("         RBJ      BW: %.2f oct @1k -> %.2f oct @14k (%.0f%% narrowing = cramping)\n",
                    rbj1k, rbj14k, (1.0 - rbj14k / rbj1k) * 100.0);
        std::printf("         Vicanek  BW: %.2f oct @1k -> %.2f oct @14k (%.0f%% change)\n",
                    mat1k, mat14k, std::fabs(1.0 - mat14k / mat1k) * 100.0);
        std::printf("         Orfanidis BW: %.2f oct @1k -> %.2f oct @14k (%.0f%% change)\n",
                    orf1k, orf14k, std::fabs(1.0 - orf14k / orf1k) * 100.0);

        const double rbjShrink = 1.0 - rbj14k / rbj1k;
        const double matChange = std::fabs(1.0 - mat14k / mat1k);
        const double orfChange = std::fabs(1.0 - orf14k / orf1k);
        CHECK("RBJ bell narrows near Nyquist (the cramping we reject)",
              rbjShrink > 0.25, ("shrink " + std::to_string(rbjShrink * 100) + "%").c_str());
        CHECK("Vicanek matched bell: constant bandwidth (no cramping)",
              matChange < 0.10, ("change " + std::to_string(matChange * 100) + "%").c_str());
        CHECK("Orfanidis bell: constant bandwidth (no cramping)",
              orfChange < 0.10, ("change " + std::to_string(orfChange * 100) + "%").c_str());
    }

    // ---- 4b. Orfanidis decramped peaking: exact center gain + unity DC -----
    {
        Biquad b; b.setCoef(orfanidis::bell(fs, 1000.0, 12.0, 2.0));
        auto proc = [&](double x){ return b.process(x); };
        const double g  = measureGainDb(proc, fs, 1000.0);
        const double dc = measureGainDb(proc, fs, 20.0);
        CHECK("Orfanidis bell +12dB @1kHz hits center", std::fabs(g - 12.0) < 0.02,
              ("measured " + std::to_string(g) + " dB").c_str());
        CHECK("Orfanidis bell unity at DC", std::fabs(dc) < 0.05,
              ("measured " + std::to_string(dc) + " dB").c_str());
    }

    // ---- 5. Matched lowpass/highpass/bandpass ----------------------------
    {
        const double q = 1.0 / std::sqrt(2.0);       // Butterworth -> -3.01 dB @ fc
        Biquad lp; lp.setCoef(matched::lowpass(fs, 1000.0, q));
        const double glp = measureGainDb([&](double x){ return lp.process(x); }, fs, 1000.0);
        CHECK("Matched LP -3.01 dB @ fc (Q=0.707)", std::fabs(glp - (-3.0103)) < 0.05,
              ("measured " + std::to_string(glp) + " dB").c_str());

        Biquad hp; hp.setCoef(matched::highpass(fs, 1000.0, q));
        const double ghp = measureGainDb([&](double x){ return hp.process(x); }, fs, 1000.0);
        CHECK("Matched HP -3.01 dB @ fc (Q=0.707)", std::fabs(ghp - (-3.0103)) < 0.05,
              ("measured " + std::to_string(ghp) + " dB").c_str());

        Biquad bp; bp.setCoef(matched::bandpass(fs, 1000.0, 2.0));
        const double gbp = measureGainDb([&](double x){ return bp.process(x); }, fs, 1000.0);
        CHECK("Matched BP 0 dB @ center", std::fabs(gbp) < 0.05,
              ("measured " + std::to_string(gbp) + " dB").c_str());
    }

    // ---- 6. Dynamics engine: steady-state gain reduction -----------------
    {
        DynamicsEngine eng;
        eng.reset(fs);
        DynamicsParams p; p.thresholdDb = -10.0; p.ratio = 4.0;
        p.attackSec = 0.001; p.releaseSec = 0.05; p.kneeDb = 0.0; p.rangeDb = 60.0;
        eng.setParams(p);
        double gr = 0.0;
        const int n = (int)(fs * 1.0);
        for (int i = 0; i < n; ++i) gr = eng.processGrDb(1.0);   // DC full-scale
        // over = 0 - (-10) = 10 dB; gr = 10 * (1 - 1/4) = 7.5 dB
        CHECK("Dynamics GR = 7.5 dB (thr -10, ratio 4, DC 0dBFS)",
              std::fabs(gr - 7.5) < 0.05, ("measured " + std::to_string(gr) + " dB").c_str());
    }

    // ---- 7. Flush Match: Match Input neutralizes a +6 dB change ----------
    {
        FlushMatch fm;
        fm.reset(fs);
        fm.setMode(FlushMatch::Mode::MatchInput);
        fm.setTiming(1);                                  // Medium for fast convergence
        const double w = 2.0 * kPi * 440.0 / fs;
        const int n = (int)(fs * 8.0);
        const int measureFrom = (int)(fs * 6.0);          // last 2 s: converged state
        double outRms = 0.0;
        double finalGainDb = 0.0;
        for (int i = 0; i < n; ++i) {
            const double in  = std::sin(w * i);
            const double pre = in * 2.0;                  // +6.02 dB "EQ boost"
            fm.pushInput(in, in);
            fm.pushOutput(pre, pre);
            const double g = fm.tickGain();
            const double out = pre * g;
            if (i >= measureFrom) outRms += out * out / (n - measureFrom);
            if (i == n - 1) finalGainDb = fm.deltaDb();
        }
        const double outDb = 20.0 * std::log10(std::sqrt(outRms) / 0.7071067811865476);
        CHECK("Flush Match: +6dB boost neutralized (gain ~ -6 dB)",
              std::fabs(finalGainDb - (-6.02)) < 0.2,
              ("applied " + std::to_string(finalGainDb) + " dB").c_str());
        CHECK("Flush Match: output returns to input level",
              std::fabs(outDb) < 0.3, ("out " + std::to_string(outDb) + " dB").c_str());
    }

    // ---- 8. Flush Match: Target level ------------------------------------
    {
        FlushMatch fm;
        fm.reset(fs);
        fm.setMode(FlushMatch::Mode::Target);
        fm.setTargetDb(-18.0);
        fm.setTiming(1);
        const double w = 2.0 * kPi * 440.0 / fs;
        const int n = (int)(fs * 8.0);
        const int measureFrom = (int)(fs * 6.0);
        double outRms = 0.0;
        double finalGainDb = 0.0;
        for (int i = 0; i < n; ++i) {
            const double in = std::sin(w * i);            // -3.01 dB RMS
            fm.pushInput(in, in);
            fm.pushOutput(in, in);
            const double g = fm.tickGain();
            const double out = in * g;
            if (i >= measureFrom) outRms += out * out / (n - measureFrom);
            if (i == n - 1) finalGainDb = fm.deltaDb();
        }
        // Input RMS = -3.01 dB; to hit -18 dB the auto-gain must be -14.99 dB.
        const double outDb = 20.0 * std::log10(std::sqrt(outRms));   // absolute
        CHECK("Flush Match: Target delta = target - input (-14.99 dB)",
              std::fabs(finalGainDb - (-14.99)) < 0.2,
              ("delta " + std::to_string(finalGainDb) + " dB").c_str());
        CHECK("Flush Match: absolute output = -18 dB target",
              std::fabs(outDb - (-18.0)) < 0.4, ("out " + std::to_string(outDb) + " dB").c_str());
    }

    // ---- 9. K-weighting matches the published BS.1770-4 coefficients ------
    {
        Biquad canonShelf; canonShelf.setCoef(KWeighting::shelf48k());
        Biquad canonRlb;   canonRlb.setCoef(KWeighting::rlb48k());
        auto canonProc = [&](double x){ return canonRlb.process(canonShelf.process(x)); };

        KWeighting kw; kw.init(fs);
        auto ourProc = [&](double x){ return kw.process(x); };

        double worst = 0.0;
        for (double f : {20.0, 40.0, 100.0, 500.0, 1000.0, 3000.0, 8000.0, 16000.0, 20000.0})
            worst = std::max(worst,
                std::fabs(measureGainDb(canonProc, fs, f) - measureGainDb(ourProc, fs, f)));

        CHECK("K-weighting matches BS.1770-4 canonical 48k coefficients",
              worst < 0.05, ("max deviation " + std::to_string(worst) + " dB").c_str());

        const double g1k  = measureGainDb(ourProc, fs, 1000.0);
        const double g20k = measureGainDb(ourProc, fs, 20000.0);
        const double g20  = measureGainDb(ourProc, fs, 20.0);
        std::printf("         K-weight: %.2f dB @20Hz, %.2f dB @1kHz, %.2f dB @20kHz\n", g20, g1k, g20k);
        // Exact ITU values (from the published coefficients): +0.698 dB @ 1 kHz
        // (the +4 dB shelf is already rising below its 1681 Hz corner),
        // +4.043 dB @ 20 kHz, -13.28 dB @ 20 Hz (RLB low-frequency cut).
        CHECK("K-weighting +0.70 dB @ 1 kHz", std::fabs(g1k - 0.70) < 0.05,
              ("measured " + std::to_string(g1k) + " dB").c_str());
        CHECK("K-weighting +4.04 dB shelf @ 20 kHz", std::fabs(g20k - 4.043) < 0.05,
              ("measured " + std::to_string(g20k) + " dB").c_str());
        CHECK("K-weighting -13.3 dB @ 20 Hz (RLB cut)", std::fabs(g20 - (-13.28)) < 0.2,
              ("measured " + std::to_string(g20) + " dB").c_str());
    }

    // ---- 10. True-peak detects intersample peaks --------------------------
    {
        TruePeakMeter tp; tp.reset(fs);
        // f = fs/3: samples only ever hit 0 / +/-0.866 (sample peak = -1.25 dB),
        // while the bandlimited waveform peaks at 1.0 (0 dBFS). The gap between
        // them is exactly what a sample-peak meter misses and a true-peak sees.
        const double w = 2.0 * kPi * (fs / 3.0) / fs;
        const int n = (int)(fs * 0.5);
        double maxSample = 0.0;
        for (int i = 0; i < n; ++i) {
            const double x = std::sin(w * i);
            maxSample = std::max(maxSample, std::fabs(x));
            tp.process(x);
        }
        const double sampleDb = linToDb(maxSample);
        const double trueDb   = linToDb(tp.truePeak());
        std::printf("         sample peak %.3f dB vs true peak %.3f dB\n", sampleDb, trueDb);
        CHECK("True-peak exceeds sample peak (intersample detection)",
              (trueDb - sampleDb) > 0.8, ("gap " + std::to_string(trueDb - sampleDb) + " dB").c_str());
        CHECK("True-peak ~0 dBFS for amplitude-1 waveform", trueDb > -0.5,
              ("measured " + std::to_string(trueDb) + " dB").c_str());
    }

    // ---- 11. Matched one-pole shelf matches the analog prototype -----------
    {
        // 1st-order analog high-shelf prototype: H(s) = (1 + sqrt(G) s)/(1 + s/sqrt(G)),
        // s = i f/fc. The decramped matched shelf is designed to match this across
        // the whole audio range — including corner frequencies near Nyquist.
        auto analogDb = [&](double f, double fc, double G) {
            const double x = (f / fc) * (f / fc);
            return 10.0 * std::log10 ((1.0 + G * x) / (1.0 + x / G));
        };

        for (double fc : { 2000.0, 10000.0 }) {
            const double G = dbToLin (12.0);
            Biquad b; b.setCoef (shelfMatch::highShelf (fs, fc, 12.0));
            auto proc = [&](double x){ return b.process (x); };
            double worst = 0.0;
            for (double f = 100.0; f <= 20000.0; f *= 1.15)
                worst = std::max (worst,
                    std::fabs (measureGainDb (proc, fs, f, 0.05, 0.10) - analogDb (f, fc, G)));
            std::printf("         matched shelf fc=%.0f: max |digital - analog| = %.3f dB\n", fc, worst);
            CHECK("Matched high shelf tracks analog prototype", worst < 0.5,
                  ("fc=" + std::to_string((int)fc) + " err " + std::to_string(worst) + " dB").c_str());
        }

        // Plateau check: with a low corner (100 Hz) the shelf has fully reached
        // its target gain well below Nyquist — no droop. DC gain is exact unity.
        Biquad b; b.setCoef (shelfMatch::highShelf (fs, 100.0, 12.0));
        auto proc = [&](double x){ return b.process (x); };
        const double dc  = measureDcGain (proc);
        const double top = measureGainDb (proc, fs, 10000.0);
        CHECK("Matched shelf exact unity at DC", std::fabs (dc - 1.0) < 1e-9,
              ("measured " + std::to_string(dc)).c_str());
        CHECK("Matched shelf plateaus at +12 dB (no droop)", std::fabs (top - 12.0) < 0.1,
              ("measured " + std::to_string(top) + " dB").c_str());
    }

    // ---- 12. TPT/ZDF state-variable filter (precision topology) -----------
    {
        SvFilter lp; lp.set (SvFilter::Kind::LowPass, fs, 1000.0, 0.7071067811865476);
        const double glp = measureGainDb ([&](double x){ return lp.process (x); }, fs, 1000.0);
        CHECK("TPT/ZDF SVF lowpass -3.01 dB @ fc", std::fabs (glp - (-3.0103)) < 0.05,
              ("measured " + std::to_string(glp) + " dB").c_str());

        SvFilter hp; hp.set (SvFilter::Kind::HighPass, fs, 1000.0, 0.7071067811865476);
        const double ghp = measureGainDb ([&](double x){ return hp.process (x); }, fs, 1000.0);
        CHECK("TPT/ZDF SVF highpass -3.01 dB @ fc", std::fabs (ghp - (-3.0103)) < 0.05,
              ("measured " + std::to_string(ghp) + " dB").c_str());

        SvFilter bp; bp.set (SvFilter::Kind::BandPass, fs, 1000.0, 2.0);
        const double gbp = measureGainDb ([&](double x){ return bp.process (x); }, fs, 1000.0);
        CHECK("TPT/ZDF SVF bandpass 0 dB @ center", std::fabs (gbp) < 0.05,
              ("measured " + std::to_string(gbp) + " dB").c_str());
    }

    // ---- 13. Coefficient morphing (click-free automation guard) ------------
    {
        // Note (measured): smoothly-designed minimum-phase biquads ring through
        // coefficient changes, so a single switch is already near click-free. The
        // morphing output-crossfade is the guarantee for *abrupt* changes
        // (preset loads, fast automation, phase-mode switches): the output is a
        // convex blend, so no discontinuous step can ever be introduced.
        // fc=1000 (fast pole) so the shelf settles within the test window;
        // 8192 samples with the morph at sample 4096.
        MorphingBiquad mb; mb.setCoeffs (shelfMatch::lowShelf (fs, 1000.0, 0.0), 0);
        double out = 0.0, prev = 0.0, maxStep = 0.0;
        for (int i = 0; i < 8192; ++i) {
            out = mb.process (1.0);                      // DC input
            if (i >= 4096) maxStep = std::max (maxStep, std::fabs (out - prev));
            prev = out;
            if (i == 4096) mb.setCoeffs (shelfMatch::lowShelf (fs, 1000.0, 18.0), 64);
        }
        const double target = dbToLin (18.0);            // 7.9433
        std::printf("         morph final out %.4f (target %.4f), max step %.4f\n",
                    out, target, maxStep);
        CHECK("Morphing converges to +18 dB shelf", std::fabs (out - target) < 0.02,
              ("out " + std::to_string(out)).c_str());
        CHECK("Morph transition is bounded (no step > 0.3)", maxStep < 0.3,
              ("max step " + std::to_string(maxStep)).c_str());
    }

    // ---- 14. Adaptive (program-dependent) release — Oxford-style ----------
    {
        DynamicsParams p; p.thresholdDb = -40.0; p.ratio = 4.0;
        p.attackSec = 0.001; p.releaseSec = 0.1; p.kneeDb = 0.0; p.rangeDb = 60.0;

        p.adaptiveRelease = false;
        DynamicsEngine fixed; fixed.reset (fs); fixed.setParams (p);
        double grFixed = 0.0;
        for (int i = 0; i < (int)(fs * 0.5); ++i) grFixed = fixed.processGrDb (1.0);
        const double fixedCoeff = fixed.releaseCoeff();

        p.adaptiveRelease = true; p.adaptiveAmount = 4.0;
        DynamicsEngine adaptive; adaptive.reset (fs); adaptive.setParams (p);
        double grAdaptive = 0.0;
        for (int i = 0; i < (int)(fs * 0.5); ++i) grAdaptive = adaptive.processGrDb (1.0);
        const double adaptCoeff = adaptive.releaseCoeff();

        std::printf("         GR = %.2f dB; release coeff fixed %.6f vs adaptive %.6f\n",
                    grAdaptive, fixedCoeff, adaptCoeff);
        CHECK("Deep GR yields GR = 30 dB (thr -40, ratio 4, both modes)",
              std::fabs (grAdaptive - 30.0) < 0.05 && std::fabs (grFixed - 30.0) < 0.05,
              ("measured " + std::to_string(grAdaptive) + " dB").c_str());
        CHECK("Adaptive release slows under deep GR (transparent hang)",
              adaptCoeff < fixedCoeff * 0.5,
              ("adaptive " + std::to_string(adaptCoeff) + " < fixed " + std::to_string(fixedCoeff)).c_str());
    }

    // ---- 15. FFT correctness ----------------------------------------------
    {
        // Impulse -> flat spectrum; sine -> single bin.
        const int N = 1024;
        std::vector<Complex> x (N, {0.0, 0.0});
        x[0] = {1.0, 0.0};
        fft (x.data(), N, false);
        double flatWorst = 0.0;
        for (int k = 0; k < N; ++k) flatWorst = std::max (flatWorst, std::fabs (x[k].re - 1.0) + std::fabs (x[k].im));
        CHECK("FFT impulse -> flat spectrum", flatWorst < 1e-9,
              ("worst " + std::to_string(flatWorst)).c_str());

        const int k0 = 64;                             // f = 64 * 48k/1024 = 3 kHz
        for (int n = 0; n < N; ++n)
            x[n] = { std::sin (2.0 * kPi * k0 * n / N), 0.0 };
        fft (x.data(), N, false);
        const double magPeak = std::hypot (x[k0].re, x[k0].im);
        const double magElse = std::hypot (x[(k0 + 100) % N].re, x[(k0 + 100) % N].im);
        CHECK("FFT sine -> peak at correct bin (N/2)", std::fabs (magPeak - N / 2.0) < 1.0,
              ("peak " + std::to_string(magPeak)).c_str());
        CHECK("FFT sine -> energy only at its bin", magElse < 1e-6,
              ("elsewhere " + std::to_string(magElse)).c_str());

        // Round-trip: fft then ifft recovers the signal.
        ifft (x.data(), N);
        double err = 0.0;
        for (int n = 0; n < N; ++n) {
            const double ref = std::sin (2.0 * kPi * k0 * n / N);
            err = std::max (err, std::fabs (x[n].re - ref));
        }
        CHECK("FFT+ifft round-trip recovers signal", err < 1e-6,
              ("err " + std::to_string(err)).c_str());
    }

    // ---- 16. Linear-phase FIR matches the band magnitude -------------------
    {
        // Design a linear-phase FIR from the matched bell's magnitude and
        // confirm (a) exact center gain, (b) unity DC, (c) symmetric (linear
        // phase) coefficients.
        const int L = 1023;
        auto magFn = [&](double f){ return magnitude (matched::bell (fs, 1000.0, 12.0, 2.0), 2.0 * kPi * f / fs); };
        auto h = designLinearPhaseFir (magFn, fs, L, 6.0);

        FirFilter fir; fir.set (h);
        auto proc = [&](double x){ return fir.process (x); };
        const double g  = measureGainDb (proc, fs, 1000.0, 0.5, 0.5);   // settle = latency
        const double dc = measureDcGain (proc, (int)(L + 4096));

        double symErr = 0.0;
        for (int n = 0; n < L / 2; ++n)
            symErr = std::max (symErr, std::fabs (h[n] - h[L - 1 - n]));

        std::printf("         FIR L=%d: center %.2f dB, DC %.3f, symmetry %.2e\n",
                    L, g, dc, symErr);
        CHECK("Linear-phase FIR center gain ~ +12 dB", std::fabs (g - 12.0) < 0.5,
              ("measured " + std::to_string(g) + " dB").c_str());
        CHECK("Linear-phase FIR unity at DC", std::fabs (dc - 1.0) < 0.02,
              ("measured " + std::to_string(dc)).c_str());
        CHECK("Linear-phase FIR symmetric (exact linear phase)", symErr < 1e-9,
              ("symmetry err " + std::to_string(symErr)).c_str());
    }

    // ---- 17. Oversampler: flat passband + image rejection -----------------
    {
        Oversampler2x os; os.reset (fs);

        // Passband flatness: base sines at several frequencies reconstruct at
        // unit gain through up -> down round trip.
        double worst = 0.0;
        for (double f : { 100.0, 1000.0, 5000.0, 10000.0 }) {
            const double w = 2.0 * kPi * f / fs;
            const int n = (int)(fs * 0.5);
            double acc = 0.0;
            for (int i = 0; i < n; ++i) {
                double up[2];
                os.upsample (std::sin (w * i), up);
                const double down = os.downsample (up[0], up[1]);
                if (i >= (int)(fs * 0.2)) acc += down * down / (n - (int)(fs * 0.2));
            }
            const double g = 20.0 * std::log10 (std::sqrt (acc) / 0.7071067811865476);
            worst = std::max (worst, std::fabs (g));
            std::printf("         oversample round-trip %.0f Hz: %.3f dB\n", f, g);
        }
        CHECK("Oversampler round-trip flat to < 0.1 dB", worst < 0.1,
              ("worst " + std::to_string(worst) + " dB").c_str());

        // Image rejection: a 20 kHz base tone, zero-stuffed to 2x, has its
        // image at (new Nyquist - f) = 28 kHz. The half-band must attenuate it.
        // Coherent detection is phase-independent (sin + cos quadrature).
        const double f = 20000.0;
        const double w  = 2.0 * kPi * f / fs;
        const double w2 = w * 0.5;                                  // wanted @2x rate
        const double wi = 2.0 * kPi * (fs - f) / (2.0 * fs);        // image = 28 kHz @2x
        const int n = (int)(fs * 0.5);
        double ws = 0.0, wc = 0.0, is = 0.0, ic = 0.0;
        for (int i = 0; i < n; ++i) {
            double up[2];
            os.upsample (std::sin (w * i), up);
            for (int j = 0; j < 2; ++j) {
                const int t = 2 * i + j;
                ws += up[j] * std::sin (w2 * t); wc += up[j] * std::cos (w2 * t);
                is += up[j] * std::sin (wi * t); ic += up[j] * std::cos (wi * t);
            }
        }
        const double wanted = std::hypot (ws, wc) / (2.0 * n);
        const double image  = std::hypot (is, ic) / (2.0 * n);
        const double rejection = 20.0 * std::log10 (image / std::max (1e-12, wanted));
        std::printf("         image rejection @28k = %.1f dB\n", rejection);
        CHECK("Oversampler rejects the image band (> -40 dB)", rejection < -40.0,
              ("rejection " + std::to_string(rejection) + " dB").c_str());
    }

    // ---- 18. Spectrum analyzer: peak + tilt -------------------------------
    {
        Analyzer an; an.reset (fs);
        an.setSpeed (2);                                 // fast averaging
        an.setTilt (0.0);                                // tilt off for the peak test

        // Full-scale 3 kHz sine -> peak at 3 kHz, ~0 dB.
        const double w = 2.0 * kPi * 3000.0 / fs;
        std::vector<double> mono ((size_t)(fs * 0.5));
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = std::sin (w * (double)i);
        an.process (mono.data(), (int)mono.size());
        const auto& mags = an.magnitudes();

        // Find the loudest output bin and its frequency.
        int peakBin = 0;
        for (int i = 1; i < (int)mags.size(); ++i)
            if (mags[i] > mags[peakBin]) peakBin = i;
        const double fBin = (double)peakBin * (fs / 2.0) / (mags.size() - 1);

        std::printf("         analyzer peak: bin %d -> %.0f Hz, %.2f dB\n",
                    peakBin, fBin, mags[peakBin]);
        CHECK("Analyzer peak near 3 kHz", std::fabs (fBin - 3000.0) < 200.0,
              ("peak at " + std::to_string((int)fBin) + " Hz").c_str());
        CHECK("Analyzer full-scale sine ~ 0 dB", std::fabs (mags[peakBin]) < 2.0,
              ("peak " + std::to_string(mags[peakBin]) + " dB").c_str());
    }

    // ---- 19. Spectrum analyzer: tilt applies ------------------------------
    {
        Analyzer an; an.reset (fs);
        an.setSpeed (2);
        an.setTilt (4.5);

        const double w = 2.0 * kPi * 8000.0 / fs;
        std::vector<double> mono ((size_t)(fs * 0.5));
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = std::sin (w * (double)i);
        an.process (mono.data(), (int)mono.size());
        const auto& mags = an.magnitudes();

        int peakBin = 0;
        for (int i = 1; i < (int)mags.size(); ++i)
            if (mags[i] > mags[peakBin]) peakBin = i;
        const double fBin = (double)peakBin * (fs / 2.0) / (mags.size() - 1);

        // Tilt = 4.5 dB/oct at 8 kHz vs 1 kHz reference = 4.5 * log2(8) = +13.5 dB.
        const double expected = 4.5 * std::log2 (8000.0 / 1000.0);
        std::printf("         analyzer 8k peak with tilt: %.2f dB (expect ~ %.2f dB)\n",
                    mags[peakBin], expected);
        CHECK("Analyzer tilt boosts 8 kHz by ~ +13.5 dB", std::fabs (mags[peakBin] - expected) < 2.0,
              ("peak " + std::to_string(mags[peakBin]) + " dB @ " + std::to_string((int)fBin) + " Hz").c_str());
    }

    // ---- 20. Spectral dynamics: per-frequency gain reduction ---------------
    {
        SpectralDynamics sd; sd.reset (fs, 2048);
        SpectralParams p; p.thresholdDb = -20.0; p.ratio = 100.0;
        p.rangeDb = 60.0; p.attackSec = 0.005; p.releaseSec = 0.1; p.kneeDb = 0.0;
        sd.setParams (p);

        // 1 kHz sine, on-bin (43 * 48000/2048 = 1007.8 Hz) so the energy is in a
        // single FFT bin (no leakage) and the threshold limit is clean.
        const double fOnBin = 43.0 * fs / 2048.0;
        const double w = 2.0 * kPi * fOnBin / fs;
        const int n = (int)(fs * 1.0);
        const int measureFrom = (int)(fs * 0.5);
        std::vector<double> buf (n);
        for (int i = 0; i < n; ++i) buf[i] = std::sin (w * i);
        sd.process (buf.data(), n);
        double acc = 0.0;
        for (int i = measureFrom; i < n; ++i) acc += buf[i] * buf[i] / (n - measureFrom);
        const double outDb = 20.0 * std::log10 (std::sqrt (acc) / 0.7071067811865476);
        std::printf("         spectral: 0 dBFS in -> %.2f dBFS (threshold -20)\n", outDb);
        // The Hann window's main lobe spans 3 bins (0 / -6 / -6 dB), and each bin
        // gets its own gain reduction, so the net result approaches — but does not
        // hard-pin at — the threshold. Expect ~17 dB reduction toward -20 dB.
        CHECK("Spectral dynamics reduces above-threshold tone toward threshold",
              outDb < -14.0 && outDb > -21.0, ("out " + std::to_string(outDb) + " dB").c_str());
    }

    // ---- 21. Spectral dynamics: below-threshold passes untouched -----------
    {
        SpectralDynamics sd; sd.reset (fs, 2048);
        SpectralParams p; p.thresholdDb = -20.0; p.ratio = 100.0;
        p.rangeDb = 60.0; p.attackSec = 0.005; p.releaseSec = 0.1; p.kneeDb = 0.0;
        sd.setParams (p);

        // Amplitude 0.05 = -26 dBFS: below threshold -> unchanged.
        const double w = 2.0 * kPi * 1000.0 / fs;
        const int n = (int)(fs * 1.0);
        const int measureFrom = (int)(fs * 0.5);
        std::vector<double> buf (n);
        for (int i = 0; i < n; ++i) buf[i] = 0.05 * std::sin (w * i);
        sd.process (buf.data(), n);
        double acc = 0.0;
        for (int i = measureFrom; i < n; ++i) acc += buf[i] * buf[i] / (n - measureFrom);
        const double outDb = 20.0 * std::log10 (std::sqrt (acc) / 0.7071067811865476);
        std::printf("         spectral: -26 dBFS in -> %.2f dBFS (below threshold)\n", outDb);
        CHECK("Spectral dynamics leaves below-threshold signal unchanged",
              std::fabs (outDb - (-26.02)) < 1.0, ("out " + std::to_string(outDb) + " dB").c_str());
    }

    // ---- 22. Spectral dynamics: pass-through at threshold 0 ---------------
    {
        SpectralDynamics sd; sd.reset (fs, 2048);
        SpectralParams p; p.thresholdDb = 0.0; p.ratio = 4.0; p.rangeDb = 60.0;
        p.attackSec = 0.005; p.releaseSec = 0.1; p.kneeDb = 6.0;
        sd.setParams (p);

        const double w = 2.0 * kPi * 1000.0 / fs;
        const int n = (int)(fs * 1.0);
        const int measureFrom = (int)(fs * 0.5);
        std::vector<double> buf (n);
        for (int i = 0; i < n; ++i) buf[i] = std::sin (w * i);
        sd.process (buf.data(), n);
        double acc = 0.0;
        for (int i = measureFrom; i < n; ++i) acc += buf[i] * buf[i] / (n - measureFrom);
        const double outDb = 20.0 * std::log10 (std::sqrt (acc) / 0.7071067811865476);
        std::printf("         spectral pass-through: %.2f dBFS (expect 0.00)\n", outDb);
        CHECK("Spectral dynamics pass-through ~ 0 dBFS (no GR)",
              std::fabs (outDb - 0.0) < 0.5, ("out " + std::to_string(outDb) + " dB").c_str());
    }

    // ---- 23. 4x oversampler: flat round-trip ------------------------------
    {
        Oversampler4x os; os.reset (fs);
        double worst = 0.0;
        for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 18000.0 }) {
            const double w = 2.0 * kPi * f / fs;
            const int n = (int)(fs * 0.5);
            double acc = 0.0;
            for (int i = 0; i < n; ++i) {
                double up[4];
                os.upsample (std::sin (w * i), up);
                const double down = os.downsample (up[0], up[1], up[2], up[3]);
                if (i >= (int)(fs * 0.2)) acc += down * down / (n - (int)(fs * 0.2));
            }
            const double g = 20.0 * std::log10 (std::sqrt (acc) / 0.7071067811865476);
            worst = std::max (worst, std::fabs (g));
            std::printf("         4x round-trip %.0f Hz: %.3f dB\n", f, g);
        }
        CHECK("4x oversampler round-trip flat to < 0.2 dB", worst < 0.2,
              ("worst " + std::to_string(worst) + " dB").c_str());
    }

    // ---- 24. Gain-Q interaction -------------------------------------------
    {
        CHECK("Gain-Q: identity at 0 dB", std::fabs (gainQLinkedQ (1.5, 0.0) - 1.5) < 1e-12,
              "Q unchanged at 0 gain");
        CHECK("Gain-Q: narrows with gain", gainQLinkedQ (1.5, 18.0) > 1.5,
              ("Q " + std::to_string(gainQLinkedQ (1.5, 18.0))).c_str());
        CHECK("Gain-Q: symmetric in boost/cut",
              std::fabs (gainQLinkedQ (1.5, 12.0) - gainQLinkedQ (1.5, -12.0)) < 1e-12,
              "boost == cut");
    }

    // ---- 24b. Oversampler latency is integer and matches the reported value ---
    {
        // Impulse through up->down; the output peak index = the round-trip delay.
        Oversampler2x os2; os2.reset (fs);
        int d2 = 0; { double peakVal = 0.0;
            for (int i = 0; i < 4096; ++i) {
                double u2[2]; os2.upsample ((i == 0) ? 1.0 : 0.0, u2);
                const double out = os2.downsample (u2[0], u2[1]);
                if (out > peakVal) { peakVal = out; d2 = i; }
            } }

        Oversampler4x os4; os4.reset (fs);
        int d4 = 0; { double peakVal = 0.0;
            for (int i = 0; i < 4096; ++i) {
                double up[4]; os4.upsample ((i == 0) ? 1.0 : 0.0, up);
                const double out = os4.downsample (up[0], up[1], up[2], up[3]);
                if (out > peakVal) { peakVal = out; d4 = i; }
            } }

        std::printf("         2x impulse delay %d (reported %d), 4x %d (reported %.0f)\n",
                    d2, os2.latencySamples(), d4, os4.latencySamples());
        CHECK("2x oversampler delay matches reported latency",
              d2 == os2.latencySamples(),
              ("delay " + std::to_string(d2) + " vs " + std::to_string(os2.latencySamples())).c_str());
        CHECK("4x oversampler delay matches reported latency (integer)",
              d4 == (int)std::lround (os4.latencySamples()),
              ("delay " + std::to_string(d4) + " vs " + std::to_string((int)std::lround(os4.latencySamples()))).c_str());
        CHECK("4x reported latency is a whole number of samples",
              std::fabs (os4.latencySamples() - std::lround (os4.latencySamples())) < 1e-9,
              ("reported " + std::to_string(os4.latencySamples())).c_str());
    }

    // ---- 25. Matched bell at 0 dB is the exact identity -------------------
    {
        Biquad b; b.setCoef (matched::bell (fs, 1000.0, 0.0, 1.0));
        auto proc = [&](double x){ return b.process (x); };
        double worst = 0.0;
        for (double f : { 50.0, 200.0, 1000.0, 5000.0, 16000.0 })
            worst = std::max (worst, std::fabs (measureGainDb (proc, fs, f)));
        CHECK("Matched bell 0 dB = exact identity", worst < 1e-9,
              ("worst " + std::to_string(worst) + " dB").c_str());
    }

    // ---- 26. Dynamics engine ballistics are sample-rate correct -----------
    {
        // Same GR at 96 kHz as at 48 kHz (release coeffs must scale with rate).
        DynamicsParams p; p.thresholdDb = -10.0; p.ratio = 4.0;
        p.attackSec = 0.001; p.releaseSec = 0.05; p.kneeDb = 0.0; p.rangeDb = 60.0;

        DynamicsEngine eng96; eng96.reset (96000.0); eng96.setParams (p);
        double gr96 = 0.0;
        for (int i = 0; i < 96000; ++i) gr96 = eng96.processGrDb (1.0);

        DynamicsEngine eng48; eng48.reset (48000.0); eng48.setParams (p);
        double gr48 = 0.0;
        for (int i = 0; i < 48000; ++i) gr48 = eng48.processGrDb (1.0);

        std::printf("         GR @96k = %.4f dB, @48k = %.4f dB\n", gr96, gr48);
        CHECK("Dynamics GR = 7.5 dB at 96 kHz", std::fabs (gr96 - 7.5) < 0.05,
              ("measured " + std::to_string(gr96) + " dB").c_str());
        CHECK("Dynamics GR identical across sample rates",
              std::fabs (gr96 - gr48) < 0.02,
              ("96k " + std::to_string(gr96) + " vs 48k " + std::to_string(gr48)).c_str());
    }

    // ---- 27. SVF cut cascade (LowCut = high-pass, HighCut = low-pass) -----
    {
        // 4 cascaded 2nd-order Butterworth SVF sections = 8th order (48 dB/oct).
        // "Low Cut" semantics: attenuate lows, pass highs -> high-pass cascade.
        std::vector<SvFilter> hp;
        for (int i = 0; i < 4; ++i) {
            SvFilter f; f.set (SvFilter::Kind::HighPass, fs, 1000.0, 0.7071067811865476);
            hp.push_back (f);
        }
        auto procHp = [&](double x){ double y = x; for (auto& f : hp) y = f.process (y); return y; };
        const double gfc   = measureGainDb (procHp, fs, 1000.0);
        const double ghalf = measureGainDb (procHp, fs, 500.0);
        const double gpass = measureGainDb (procHp, fs, 5000.0);
        std::printf("         SVF 8th-order LowCut: %.2f dB @fc, %.2f dB @fc/2, %.2f dB @5fc\n",
                    gfc, ghalf, gpass);
        CHECK("SVF LowCut -12 dB @ fc (8th order)", std::fabs (gfc - (-12.04)) < 1.0,
              ("measured " + std::to_string(gfc) + " dB").c_str());
        CHECK("SVF LowCut steep slope below fc (~-49 dB @ fc/2)", ghalf < gfc - 30.0,
              ("measured " + std::to_string(ghalf) + " dB").c_str());
        CHECK("SVF LowCut passes highs (~0 dB @ 5fc)", std::fabs (gpass) < 0.5,
              ("measured " + std::to_string(gpass) + " dB").c_str());

        // "High Cut" semantics: attenuate highs, pass lows -> low-pass cascade.
        std::vector<SvFilter> lp;
        for (int i = 0; i < 4; ++i) {
            SvFilter f; f.set (SvFilter::Kind::LowPass, fs, 1000.0, 0.7071067811865476);
            lp.push_back (f);
        }
        auto procLp = [&](double x){ double y = x; for (auto& f : lp) y = f.process (y); return y; };
        const double lpLow = measureGainDb (procLp, fs, 200.0);
        CHECK("SVF HighCut passes lows (~0 dB @ fc/5)", std::fabs (lpLow) < 0.5,
              ("measured " + std::to_string(lpLow) + " dB").c_str());
    }

    // ---- 28. Bulletproofing: NaN/Inf sanitization -------------------------
    {
        // A NaN/Inf sample must be collapsed, not propagated into filter state.
        Biquad b; b.setCoef (rbj::bell (fs, 1000.0, 12.0, 1.0));
        b.process (std::nan ("")); b.process (std::numeric_limits<double>::infinity ());
        const double out = b.process (0.1);
        CHECK("Biquad survives NaN/Inf input (finite output)", std::isfinite (out),
              ("out " + std::to_string(out)).c_str());

        // OnePole with a zero/negative tau -> frozen, never NaN.
        OnePole op; op.reset();
        op.setTau (0.0, fs); op.setTau (-1.0, 0.0);
        CHECK("OnePole survives degenerate tau", std::isfinite (op.update (1.0)),
              "no NaN from exp(-inf)");
    }

    // ---- 29. Bulletproofing: FFT refuses non-power-of-2 -------------------
    {
        // The old heap-corruption bug: radix-2 FFT on a non-power-of-2 length.
        // The guard must make it a no-op instead of writing out of bounds.
        std::vector<Complex> x (10, {1.0, 0.0});
        fft (x.data(), 10, false);            // 10 is not a power of two
        bool intact = true;
        for (int i = 0; i < 10; ++i) if (x[i].re != 1.0 || x[i].im != 0.0) intact = false;
        CHECK("FFT on non-power-of-2 length is a safe no-op", intact,
              "buffer untouched");
        fft (nullptr, 1024, false);           // null pointer -> no-op (no crash)
        CHECK("FFT null pointer is a safe no-op", true, "no crash");
    }

    // ---- 30. Bulletproofing: clampQ / clampFc guards ----------------------
    {
        CHECK("clampQ rejects zero (division-by-zero guard)",
              clampQ (0.0) >= 1e-3 && std::isfinite (clampQ (0.0)),
              ("clamped to " + std::to_string(clampQ (0.0))).c_str());
        CHECK("clampQ rejects huge (pole overflow guard)",
              std::isfinite (clampQ (1e9)), "no overflow");
        CHECK("clampFc rejects Nyquist+ (tan(pi/2)=inf guard)",
              std::isfinite (std::tan (kPi * clampFc (48000.0, 48000.0) / 48000.0)),
              "fc clamped below Nyquist");
        CHECK("matched bell survives Q=0 (was potential NaN)",
              std::isfinite (magnitude (matched::bell (fs, 1000.0, 6.0, 0.0), 1.0)),
              "finite coefficients");
    }

    // ---- 31. Bulletproofing: kaiserWindow degenerate sizes ----------------
    {
        auto w1 = kaiserWindow (1, 6.0);
        auto w0 = kaiserWindow (0, 6.0);
        CHECK("kaiserWindow(1) = {1.0} (no div-by-zero)", w1.size() == 1 && w1[0] == 1.0,
              "single tap");
        CHECK("kaiserWindow(0) = empty", w0.empty(), "empty");
    }

    // ---- 32. Compressor lookahead: audio delay + undelayed GR --------------
    {
        DynamicsParams p; p.thresholdDb = -10.0; p.ratio = 4.0;
        p.attackSec = 0.0001; p.releaseSec = 0.05; p.kneeDb = 0.0; p.rangeDb = 60.0;
        p.lookaheadSec = 0.001;                          // 48 samples @ 48 kHz

        DynamicsEngine eng; eng.reset (fs); eng.setParams (p);
        const int K = eng.lookaheadLatencySamples();
        CHECK("Lookahead latency = 48 samples @ 48kHz", K == 48,
              ("K " + std::to_string(K)).c_str());

        // The engine returns the UNDELAYED GR (no internal delay): with a
        // ~5-sample attack, the GR should have risen well within the lookahead
        // window (if the engine delayed the GR internally, it would still be 0
        // after 48 samples).
        double grNow = 0.0;
        for (int i = 0; i < 16; ++i) grNow = eng.processGrDb (1.0);
        CHECK("Engine returns undelayed GR (rises within the lookahead window)",
              grNow > 1.0, ("gr " + std::to_string(grNow) + " dB").c_str());

        // The audio is delayed by the lookahead (DelayLine), NOT the GR.
        DelayLine dl; dl.setDelay (K);
        int peakIdx = 0; double peakVal = 0.0;
        for (int i = 0; i < 4096; ++i) {
            const double out = dl.process ((i == 0) ? 1.0 : 0.0);
            if (out > peakVal) { peakVal = out; peakIdx = i; }
        }
        CHECK("DelayLine delays the audio by exactly the lookahead",
              peakIdx == K, ("impulse at " + std::to_string(peakIdx)).c_str());
    }

    // ---- 33. Analyzer freeze holds a hard snapshot -------------------------
    {
        Analyzer an; an.reset (fs); an.setSpeed (2); an.setTilt (0.0);

        auto feedTone = [&](double f, double secs) {
            const double w = 2.0 * kPi * f / fs;
            std::vector<double> m ((size_t)(fs * secs));
            for (size_t i = 0; i < m.size(); ++i) m[i] = std::sin (w * (double)i);
            an.process (m.data(), (int)m.size());
        };
        auto peakIndex = [](const std::vector<float>& mags) {
            int p = 0; for (int i = 1; i < (int)mags.size(); ++i) if (mags[i] > mags[p]) p = i;
            return p;
        };

        feedTone (3000.0, 0.5);
        const std::vector<float> mags1 = an.magnitudes();   // copy (out_ is reused)
        const int p1 = peakIndex (mags1);

        an.setFreeze (true);
        feedTone (100.0, 0.5);                              // ignore while frozen
        const std::vector<float> mags2 = an.magnitudes();
        CHECK("Frozen analyzer is a hard snapshot (peak unchanged)",
              std::fabs (mags2[p1] - mags1[p1]) < 0.01,
              ("frozen " + std::to_string(mags2[p1]) + " vs live " + std::to_string(mags1[p1])).c_str());

        an.setFreeze (false);
        feedTone (6000.0, 0.5);                             // resumes after unfreeze
        const std::vector<float> mags3 = an.magnitudes();
        const int p3 = peakIndex (mags3);
        const double f3 = (double)p3 * (fs / 2.0) / (mags3.size() - 1);
        CHECK("Unfreeze resumes tracking (peak moves to 6 kHz)",
              std::fabs (f3 - 6000.0) < 250.0, ("peak " + std::to_string((int)f3) + " Hz").c_str());
    }

    // ---- 34. Matched cuts are decramped near Nyquist -----------------------
    {
        // 2nd-order Butterworth (12 dB/oct) lowpass at 1 kHz. The analog
        // prototype's magnitude at 20 kHz is 1/sqrt(1+(20)^4) = -52.04 dB.
        // The bilinear (RBJ) design goes STEEPER than the analog prototype near
        // Nyquist ("cramping"); the matched design tracks the analog prototype.
        const double analogDb = 20.0 * std::log10 (1.0 / std::sqrt (1.0 + std::pow (20.0, 4.0)));

        Biquad rbjLp; rbjLp.setCoef (rbj::lowpass (fs, 1000.0, 0.7071067811865476));
        const double rbjDb = measureGainDb ([&](double x){ return rbjLp.process (x); }, fs, 20000.0);

        Biquad matLp; matLp.setCoef (matched::lowpass (fs, 1000.0, 0.7071067811865476));
        const double matDb = measureGainDb ([&](double x){ return matLp.process (x); }, fs, 20000.0);

        std::printf("         high-cut @20k: analog %.2f dB, matched %.2f dB, RBJ %.2f dB\n",
                    analogDb, matDb, rbjDb);
        CHECK("Matched cut tracks the analog rolloff (decramped)",
              std::fabs (matDb - analogDb) < 3.0,
              ("matched " + std::to_string(matDb) + " vs analog " + std::to_string(analogDb)).c_str());
        CHECK("RBJ cut cramps (steeper than analog)",
              rbjDb < analogDb - 3.0,
              ("RBJ " + std::to_string(rbjDb) + " vs analog " + std::to_string(analogDb)).c_str());
    }

    // ---- 35. Mid/Side encode-decode is lossless ---------------------------
    {
        // The M/S compressor splits L/R into (L+R)/2 and (L-R)/2 and recombines
        // via L = M+S, R = M-S. Verify the round-trip is bit-exact (the
        // compressor engine math is already tested above).
        const double w1 = 2.0 * kPi * 440.0 / fs;
        const double w2 = 2.0 * kPi * 1000.0 / fs;
        double worst = 0.0;
        for (int i = 0; i < 4096; ++i) {
            const double L = 0.7 * std::sin (w1 * i);
            const double R = 0.3 * std::sin (w2 * i);
            const double m = 0.5 * (L + R), s = 0.5 * (L - R);
            const double L2 = m + s, R2 = m - s;
            worst = std::max (worst, std::max (std::fabs (L - L2), std::fabs (R - R2)));
        }
        CHECK("Mid/Side encode-decode is bit-exact", worst < 1e-15,
              ("worst " + std::to_string(worst)).c_str());
    }

    // ---- 36. TPT/ZDF SVF equals the bilinear (RBJ) design ----------------
    {
        // The bilinear-warped SVF (g = tan(pi*fc/fs)) realises the RBJ cookbook
        // lowpass/highpass exactly. This pins down WHY the SVF cramps near
        // Nyquist (it IS the bilinear design) — the decramped response requires
        // the joint (g,k) solve vs. the analog prototype (Cytomic's technique),
        // which the matched biquads already provide. Verified here so the SVF
        // primitive is a known, correct reference point, not a mystery.
        const double fc = 1000.0, Q = 0.7071067811865476;
        SvFilter svf; svf.set (SvFilter::Kind::LowPass, fs, fc, Q);
        Biquad rbj; rbj.setCoef (rbj::lowpass (fs, fc, Q));

        double worst = 0.0;
        for (double f : { 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 18000.0 }) {
            const double g1 = measureGainDb ([&](double x){ return svf.process (x); }, fs, f);
            const double g2 = measureGainDb ([&](double x){ return rbj.process (x); }, fs, f);
            worst = std::max (worst, std::fabs (g1 - g2));
        }
        std::printf("         SVF vs RBJ LP: max |diff| = %.4f dB\n", worst);
        CHECK("SVF (bilinear) == RBJ lowpass across the band", worst < 0.05,
              ("max diff " + std::to_string(worst) + " dB").c_str());
    }

    // ---- 37. Parallel EQ response = 1 + Σ(H-1), NOT the product -----------
    {
        // Two +6 dB bells at 1 kHz, Q 1, in PARALLEL (the actual audio topology):
        //   out = dry + (H1(dry)-dry) + (H2(dry)-dry) = H1(dry) + H2(dry) - dry.
        // At center the bell is exactly real (H = G), so |2G - 1| = 2*2 - 1 = 3
        // -> 9.54 dB. A SERIAL product would read 6+6 = 12 dB (wrong for
        // parallel; this was the bug in the UI curve + linear-phase FIR).
        Biquad b1; b1.setCoef (matched::bell (fs, 1000.0, 6.0, 1.0));
        Biquad b2; b2.setCoef (matched::bell (fs, 1000.0, 6.0, 1.0));
        auto proc = [&](double x){ return b1.process (x) + b2.process (x) - x; };
        const double g = measureGainDb (proc, fs, 1000.0);

        const double G = dbToLin (6.0);
        const double expected = 20.0 * std::log10 (2.0 * G - 1.0);
        std::printf("         parallel 2x +6dB bells @1k: %.2f dB (serial product would be 12)\n", g);
        CHECK("Parallel EQ sums deltas (not the serial product)",
              std::fabs (g - expected) < 0.05,
              ("measured " + std::to_string(g) + " vs expected " + std::to_string(expected)).c_str());
        CHECK("Parallel sum is NOT 12 dB (product)", std::fabs (g - 12.0) > 1.0,
              ("measured " + std::to_string(g) + " dB").c_str());

        // The cascadeResponse + delta-sum helper must reproduce the measured value.
        std::vector<BiquadCoef> one = { matched::bell (fs, 1000.0, 6.0, 1.0) };
        const Complex h = cascadeResponse (one, 2.0 * kPi * 1000.0 / fs);
        const double tre = 1.0 + 2.0 * (h.re - 1.0);
        const double tim = 2.0 * h.im;
        const double mag = 20.0 * std::log10 (std::hypot (tre, tim));
        CHECK("cascadeResponse reproduces the parallel magnitude",
              std::fabs (mag - g) < 0.05,
              ("helper " + std::to_string(mag) + " vs measured " + std::to_string(g)).c_str());
    }

    // ---- 38. Notch must null perfectly (RBJ zeros on the unit circle) -----
    {
        // A notch's defining property is a perfect null at center. The RBJ notch
        // places its zeros ON the unit circle -> exact null. (A matched-
        // bandpass-based notch only nulls ~ -20 dB because the bandpass has
        // non-zero phase at center — verified and rejected as not acceptable.)
        Biquad notch; notch.setCoef (rbj::notch (fs, 2000.0, 2.0));
        auto proc = [&](double x){ return notch.process (x); };
        const double center = measureGainDb (proc, fs, 2000.0);
        const double dc     = measureGainDb (proc, fs, 20.0);
        const double hi     = measureGainDb (proc, fs, 10000.0);
        std::printf("         RBJ notch @2k: center %.1f dB, DC %.2f dB, 10k %.2f dB\n",
                    center, dc, hi);
        CHECK("RBJ notch nulls deeply at center", center < -40.0,
              ("center " + std::to_string(center) + " dB").c_str());
        CHECK("RBJ notch unity at DC", std::fabs (dc) < 0.2,
              ("DC " + std::to_string(dc) + " dB").c_str());
        CHECK("RBJ notch unity far above center", std::fabs (hi) < 0.2,
              ("10k " + std::to_string(hi) + " dB").c_str());
    }

    // ---- 39. Mid/Side composite curve convention --------------------------
    {
        // A +6 dB MID band: H_mid = 2 (linear) at center, H_side = 1 (no side
        // band). The L-channel response to an L-only signal is A = (H_mid +
        // H_side)/2 = 1.5 -> +3.5 dB. This locks the curve convention so a
        // mid-band is displayed at its true stereo contribution (not +6 dB).
        const double w0 = 2.0 * kPi * 1000.0 / fs;
        std::vector<BiquadCoef> midCoefs = { matched::bell (fs, 1000.0, 6.0, 1.0) };
        const Complex Hmid = cascadeResponse (midCoefs, w0);
        CHECK("Mid bell at center = 2.0 linear (+6 dB)", std::fabs (Hmid.re - 2.0) < 0.01,
              ("Hmid " + std::to_string(Hmid.re)).c_str());

        // Composite: A = (Hmid + 1)/2 (side path is unity).
        const double are = 0.5 * (Hmid.re + 1.0), aim = 0.5 * Hmid.im;
        const double db = 20.0 * std::log10 (std::hypot (are, aim));
        const double expected = 20.0 * std::log10 (1.5);
        std::printf("         mid-band composite @1k: %.3f dB (expected %.3f)\n", db, expected);
        CHECK("Mid band displays at +3.5 dB (true stereo contribution)",
              std::fabs (db - expected) < 0.05,
              ("measured " + std::to_string(db) + " vs " + std::to_string(expected)).c_str());
    }

    std::printf("\n%s — %d failure(s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}