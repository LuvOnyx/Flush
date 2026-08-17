// parity_tools.cpp — clean-room benchmarking harness (pure C++, no JUCE)
//
// The LEGAL way to "use the same data as Sonnox/UA": don't copy their DSP. Run
// test signals THROUGH a reference plugin, record the output, and measure the
// behavior (frequency response, THD, null residual) as a TARGET to match with
// our own independent implementation.
//
// This tool does two jobs:
//   gen     — write test stimuli as 32-bit-float WAV (sine / log sweep / dual
//             tone / impulse).
//   measure — read a captured WAV (16/24/32-bit PCM or 32/64-bit float) and
//             report level, fundamental level, THD, and harmonic levels.
//   null    — diff two WAVs (Flush out vs. reference out) -> RMS + peak residual.
//
// Build:  g++ -std=c++20 -O2 parity_tools.cpp -o parity_tools

#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- WAV write --
bool writeWav (const std::string& path, double sr,
               const std::vector<double>& l, const std::vector<double>& r, bool stereo)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (!f) { std::fprintf (stderr, "cannot open %s for write\n", path.c_str()); return false; }
    const int ch = stereo ? 2 : 1;
    const uint32_t n = (uint32_t)l.size();
    const uint32_t dataBytes = n * ch * 4;
    const uint32_t riffSize = 36 + dataBytes;

    std::fwrite ("RIFF", 1, 4, f);
    std::fwrite (&riffSize, 4, 1, f);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f);
    const uint32_t fmtSize = 16; std::fwrite (&fmtSize, 4, 1, f);
    const uint16_t audioFormat = 3;  std::fwrite (&audioFormat, 2, 1, f);   // IEEE float
    const uint16_t numCh = (uint16_t)ch; std::fwrite (&numCh, 2, 1, f);
    const uint32_t sampleRate = (uint32_t)sr; std::fwrite (&sampleRate, 4, 1, f);
    const uint32_t byteRate = sampleRate * ch * 4; std::fwrite (&byteRate, 4, 1, f);
    const uint16_t blockAlign = (uint16_t)(ch * 4); std::fwrite (&blockAlign, 2, 1, f);
    const uint16_t bits = 32; std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f);
    std::fwrite (&dataBytes, 4, 1, f);

    for (uint32_t i = 0; i < n; ++i) {
        const float L = (float)l[i], R = (float)(stereo ? r[i] : l[i]);
        std::fwrite (&L, 4, 1, f);
        if (stereo) std::fwrite (&R, 4, 1, f);
    }
    std::fclose (f);
    return true;
}

// ---------------------------------------------------------------- WAV read ---
struct Wav { double sr = 0; int channels = 0; std::vector<double> l, r; };

bool readWav (const std::string& path, Wav& w)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (!f) { std::fprintf (stderr, "cannot open %s\n", path.c_str()); return false; }

    char tag[5] = {0};
    std::fread (tag, 1, 4, f); if (std::strcmp (tag, "RIFF")) { std::fclose (f); return false; }
    uint32_t riffSize; std::fread (&riffSize, 4, 1, f);
    std::fread (tag, 1, 4, f); if (std::strcmp (tag, "WAVE")) { std::fclose (f); return false; }

    uint16_t audioFormat = 0, bits = 0, ch = 0;
    uint32_t sampleRate = 0;
    std::vector<uint8_t> data;
    bool sawFmt = false;

    while (std::fread (tag, 1, 4, f) == 4) {
        uint32_t sz; std::fread (&sz, 4, 1, f);
        if (!std::strcmp (tag, "fmt ")) {
            std::fread (&audioFormat, 2, 1, f);
            std::fread (&ch, 2, 1, f);
            std::fread (&sampleRate, 4, 1, f);
            std::fseek (f, 4, SEEK_CUR);              // byte rate
            std::fseek (f, 2, SEEK_CUR);              // block align
            std::fread (&bits, 2, 1, f);
            if (sz > 16) std::fseek (f, sz - 16, SEEK_CUR);
            sawFmt = true;
        } else if (!std::strcmp (tag, "data")) {
            data.resize (sz);
            std::fread (data.data(), 1, sz, f);
        } else {
            std::fseek (f, sz, SEEK_CUR);             // skip other chunks
        }
    }
    std::fclose (f);
    if (!sawFmt || data.empty()) { std::fprintf (stderr, "missing fmt/data chunk\n"); return false; }

    const int bytesPerSample = bits / 8;
    const int frameBytes = bytesPerSample * ch;
    const size_t frames = data.size() / frameBytes;
    w.sr = sampleRate; w.channels = ch;
    w.l.resize (frames); w.r.resize (frames);

    auto sampleAt = [&](size_t frame, int c) -> double {
        const uint8_t* p = &data[frame * frameBytes + c * bytesPerSample];
        if (audioFormat == 3 && bits == 32) {
            float v; std::memcpy (&v, p, 4); return v;
        } else if (audioFormat == 3 && bits == 64) {
            double v; std::memcpy (&v, p, 8); return v;
        } else if (audioFormat == 1) {
            int64_t raw = 0;
            for (int b = 0; b < bytesPerSample; ++b) {                       // little-endian
                raw |= (int64_t)p[b] << (8 * b);
            }
            if (bits == 24) raw = (raw << 40) >> 40;                          // sign-extend
            else if (bits == 16) raw = (int16_t)raw;
            else if (bits == 32) raw = (int32_t)raw;
            const double scale = (bits == 32) ? 2147483648.0
                               : (bits == 24) ? 8388608.0
                               : 32768.0;
            return (double)raw / scale;
        }
        return 0.0;
    };

    for (size_t i = 0; i < frames; ++i) {
        w.l[i] = sampleAt (i, 0);
        w.r[i] = ch > 1 ? sampleAt (i, 1) : w.l[i];
    }
    return true;
}

// --------------------------------------------------------------- Goertzel ----
// Magnitude of the component at frequency f (Hz) in x, window [from, to).
double goertzel (const std::vector<double>& x, size_t from, size_t to, double f, double sr)
{
    const double w = 2.0 * kPi * f / sr;
    const double c = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < to; ++i) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    const size_t N = to - from;
    const double re = s1 - s2 * std::cos (w);
    const double im = s2 * std::sin (w);
    return std::sqrt (re * re + im * im) * 2.0 / (double)N;
}

double db (double lin) { return (lin > 1e-15) ? 20.0 * std::log10 (lin) : -300.0; }

// ----------------------------------------------------------------- commands --
void cmdGenSine (const std::string& out, double f, double secs, double amp)
{
    const double sr = 48000.0;
    const size_t n = (size_t)(sr * secs);
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i) x[i] = amp * std::sin (2.0 * kPi * f * i / sr);
    writeWav (out, sr, x, x, false);
    std::printf ("wrote %s: %.0f Hz sine, %.1f s, %.2f dBFS\n",
                 out.c_str(), f, secs, db (amp));
}

void cmdGenSweep (const std::string& out, double secs)
{
    const double sr = 48000.0;
    const size_t n = (size_t)(sr * secs);
    std::vector<double> x (n);
    double phase = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double t = (double)i / sr;
        const double f = 20.0 * std::pow (1000.0, t / secs);       // 20 Hz -> 20 kHz log
        phase += 2.0 * kPi * f / sr;
        x[i] = 0.5 * std::sin (phase);
    }
    writeWav (out, sr, x, x, false);
    std::printf ("wrote %s: log sweep 20 Hz..20 kHz, %.1f s\n", out.c_str(), secs);
}

void cmdGenDual (const std::string& out, double f1, double f2, double secs)
{
    const double sr = 48000.0;
    const size_t n = (size_t)(sr * secs);
    std::vector<double> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = 0.25 * std::sin (2.0 * kPi * f1 * i / sr)
             + 0.25 * std::sin (2.0 * kPi * f2 * i / sr);
    writeWav (out, sr, x, x, false);
    std::printf ("wrote %s: dual tone %.0f + %.0f Hz, %.1f s\n", out.c_str(), f1, f2, secs);
}

void cmdGenImpulse (const std::string& out, double secs)
{
    const double sr = 48000.0;
    const size_t n = (size_t)(sr * secs);
    std::vector<double> x (n, 0.0);
    x[0] = 1.0;
    writeWav (out, sr, x, x, false);
    std::printf ("wrote %s: impulse + %.1f s silence\n", out.c_str(), secs);
}

void cmdMeasure (const std::string& in, double f)
{
    Wav w; if (!readWav (in, w)) return;
    const size_t n = w.l.size();
    const size_t from = n / 4, to = n - n / 16;          // skip plugin latency + tail

    double peak = 0.0, rms = 0.0;
    for (size_t i = 0; i < n; ++i) { peak = std::max (peak, std::fabs (w.l[i])); rms += w.l[i] * w.l[i]; }
    rms = std::sqrt (rms / n);

    const double fund = goertzel (w.l, from, to, f, w.sr);
    double harmPow = 0.0;
    std::printf ("  harmonics:\n");
    for (int h = 2; h <= 10; ++h) {
        const double m = goertzel (w.l, from, to, f * h, w.sr);
        harmPow += m * m;
        std::printf ("    H%02d: %7.2f dBFS\n", h, db (m));
    }
    const double thdLin = std::sqrt (harmPow) / std::max (fund, 1e-12);
    std::printf ("  peak: %.2f dBFS   RMS: %.2f dBFS\n", db (peak), db (rms));
    std::printf ("  fundamental (%.0f Hz): %.2f dBFS\n", f, db (fund));
    std::printf ("  THD: %.4f %%  (%.2f dB)\n", thdLin * 100.0, db (thdLin));
}

void cmdNull (const std::string& a, const std::string& b)
{
    Wav wa, wb;
    if (!readWav (a, wa) || !readWav (b, wb)) return;
    const size_t n = std::min (wa.l.size(), wb.l.size());
    if (n == 0) { std::fprintf (stderr, "empty files\n"); return; }
    double sumSq = 0.0, peak = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = wa.l[i] - wb.l[i];
        sumSq += d * d;
        peak = std::max (peak, std::fabs (d));
    }
    const double rms = std::sqrt (sumSq / n);
    std::printf ("null residual (%zu samples): RMS %.2f dBFS, peak %.2f dBFS\n",
                 n, db (rms), db (peak));
    std::printf ("  (< -60 dBFS RMS = effectively indistinguishable)\n");
}

void usage (const char* argv0)
{
    std::fprintf (stderr,
        "usage:\n"
        "  %s gen <out.wav> sine <freqHz> <secs> [amp=0.5]\n"
        "  %s gen <out.wav> sweep <secs>\n"
        "  %s gen <out.wav> dual <f1> <f2> <secs>\n"
        "  %s gen <out.wav> impulse <secs>\n"
        "  %s measure <in.wav> sine <freqHz>\n"
        "  %s null <a.wav> <b.wav>\n", argv0, argv0, argv0, argv0, argv0, argv0);
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) { usage (argv[0]); return 2; }
    const std::string cmd = argv[1];

    if (cmd == "gen" && argc >= 5) {
        const std::string out = argv[2];
        const std::string kind = argv[3];
        if (kind == "sine" && argc >= 6) {
            const double f = std::atof (argv[4]), secs = std::atof (argv[5]);
            const double amp = argc >= 7 ? std::atof (argv[6]) : 0.5;
            cmdGenSine (out, f, secs, amp);
            return 0;
        }
        if (kind == "sweep" && argc >= 5) { cmdGenSweep (out, std::atof (argv[4])); return 0; }
        if (kind == "dual" && argc >= 7) { cmdGenDual (out, std::atof (argv[4]), std::atof (argv[5]), std::atof (argv[6])); return 0; }
        if (kind == "impulse" && argc >= 5) { cmdGenImpulse (out, std::atof (argv[4])); return 0; }
        usage (argv[0]); return 2;
    }
    if (cmd == "measure" && argc >= 5 && std::string (argv[3]) == "sine") {
        cmdMeasure (argv[2], std::atof (argv[4]));
        return 0;
    }
    if (cmd == "null" && argc >= 4) {
        cmdNull (argv[2], argv[3]);
        return 0;
    }
    usage (argv[0]);
    return 2;
}
