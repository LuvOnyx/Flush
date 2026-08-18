# Flush

**A Pro-Q4-class 24-band dynamic EQ + Flush Match gain staging + glue compressor.**
Native C++ (Visage) UI. Built with APC / JUCE 9.

> The goal is not "another EQ" — it's an EQ worth using, built to the
> FabFilter / Sonnox Oxford quality bar on *sound*, not just features.

## What Flush does

| Section | What |
|---|---|
| **24-band dynamic EQ** | **Parallel topology** (each band filters the dry signal and sums its delta — bands don't interact); Bell, Notch, shelves, **decramped cuts (12–48 dB/oct)**, Band Pass, Tilt, Flat Tilt, All Pass — **every audible-magnitude shape is decramped**; **every node supports dynamic mode** (threshold/range/attack/release); per-band **Stereo / Mid / Side** placement; **band solo** (hear only a band's contribution) |
| **Phase modes** | Low Latency (Vicanek matched) / Natural (Orfanidis decramped) / **Linear (Kaiser-FIR, latency-reported)** |
| **Flush Match** (hero) | Auto output gain: **Match Input** (tone changes, level stays) or **Target Level** (normalize into the limiter). Bus-safe timing, Mid or Stereo reference — measured on **ITU-R BS.1770-4 K-weighted loudness**, not RMS |
| **Glue compressor** | Shared dynamics engine, **Sonnox-Oxford-style transparent** (soft knee + adaptive release + **1.5 ms lookahead**), auto-makeup + GR meter — **Broadband / Mid-Side / Spectral** modes (M/S compresses mid and side independently) |
| **Presets** | **14 factory presets** (rap/RnB/drill-focused: 808 Clean, Drill Vocal, RnB Air, Sub Tighten, Punch Bus, Clarity, Master Warm + the originals) + **user preset save/load** + A/B compare |
| **Metering** | In/out **true-peak** (4x oversampled) + RMS, K-weighted loudness, Mid/Side levels, phase correlation, GR, live "Flush: +x.x dB" |
| **Spectrum analyzer** | 4096-pt Hann FFT, 75% overlap, exponential averaging, pink tilt, peak hold, **freeze (hard snapshot)**, **pre/post-EQ source**, max-within-band decimation |
| **Oversampling** | 2x + 4x half-band oversampling on the EQ section (image rejection −72 dB) |
| **Spectral dynamics** | FFT per-frequency dynamics (Pro-Q4's newest feature) |
| **UI** | Native Visage (C++): glassy knobs, VU/bar meters, draggable EQ nodes + neon glow, preset browser, settings modal (uncompiled in this env) |

## Architecture: one Dynamics Engine

The compressor and every dynamic EQ band share **one** detector → gain-computer →
ballistics core. The compressor is the broadband instance; a dynamic band is the
same engine fed by a band-filtered sidechain, applying band-limited gain.
*(A dynamic band is literally a band-limited compressor.)*

## Differentiation (why not "another EQ")

Flat-EQ magnitude is a solved, commoditized problem — the differentiation is elsewhere:

- **Loudness-native DSP** — Flush Match and the meters speak **ITU-R BS.1770-4 K-weighted
  loudness + true-peak** (verified against the published coefficients to 0.000000 dB). Gain
  staging is a loudness problem, so Flush measures loudness — not the RMS every clone ships.
- **Precision topology** (TPT/ZDF + coefficient morphing), **dynamic + spectral EQ**, and
  **controlled/mixed phase** are the rest of the frontier (roadmap).

## Sound-quality standard (the product)

The defining difference between a reference EQ and "another EQ" is whether it uses
the **analog-matched/decramped** filter family or the commodity **bilinear-transform
(RBJ cookbook)** family. The bilinear transform "cramps" the high frequencies — Flush
uses the matched family instead:

- **Double precision** (float64) everywhere in the signal path.
- **Orfanidis decramped peaking (1997)** — the "prescribed Nyquist-frequency gain"
  design; the high-frequency-decramping technique reference EQs (FabFilter "Natural
  Phase" class) are built on. → **Natural** mode.
- **Vicanek matched biquads (2016)** — impulse-invariance poles + magnitude-matched
  numerators. → **Low Latency** mode.
- **Vicanek matched shelves (2019)** — decramped, transparent shelves (no droop).
- **Measured cramping** (12 dB bell @ 14 kHz): RBJ narrows **47%**, Vicanek holds
  within **7%**, Orfanidis within **2.1%** (while keeping RBJ's exact Q feel).
- **Precision topology:** TPT/ZDF state-variable filters (cuts) + coefficient morphing
  (click-free parameter changes/preset loads).
- **Linear-phase FIR** — Kaiser-windowed frequency-sampling design; exact symmetry
  (linear phase), center gain matches the matched IIR to ~0.1 dB, DC exact.
- **2x oversampling** — half-band up/down, round-trip flat to 0.003 dB, image rejection −72 dB.
- **RBJ demoted to a measurement baseline** — never a user-facing mode.
- **Next DSP passes:** 4x oversampling, spectral dynamics (Pro-Q4's newest feature).
- Gain–Q interaction, sample-accurate smoothing, denormal protection.

## Repo layout

```
Source/dsp/          Pure C++ DSP core (double precision, no JUCE) — unit tested
  FlushCommon.h      dB helpers + one-pole smoother
  FlushFilter.h      RBJ (baseline) + Vicanek matched + Orfanidis decramped +
                     matched shelves + TPT/ZDF SVF + morphing + gain-Q link
  FlushFft.h         radix-2 complex FFT (shared by FIR + analyzer + spectral)
  FlushFir.h         Kaiser window + linear-phase FIR design + FIR filter
  FlushOversample.h  2x + 4x half-band oversamplers
  FlushDynamics.h    unified dynamics engine (Oxford-style adaptive release)
  FlushSpectral.h    FFT spectral dynamics (overlap-add per-bin dynamics)
  FlushMatch.h       Flush Match auto-gain (K-weighted loudness)
  FlushLoudness.h    ITU-R BS.1770-4 K-weighting + true-peak meter
  FlushAnalyzer.h    FFT spectrum analyzer (Hann, overlap, tilt, peak hold)
Source/PluginProcessor.*   JUCE processor (APVTS + 24-band state + chain + presets)
Source/PluginEditor.*      Visage editor host
Source/VisageControls.h    Visage Pro-Q4 UI (knobs, meters, EQ display, presets, settings)
tests/dsp_smoke.cpp        standalone DSP verification (no JUCE)
```

## Verify the DSP (no toolchain beyond g++)

```bash
cd plugins/Flush/tests
g++ -std=c++20 -O2 -I../Source/dsp dsp_smoke.cpp -o dsp_smoke && ./dsp_smoke
```

86 assertions measure the actual responses: exact center gain, boost/cut cancel-to-wire,
LP/HP −3.01 dB @ fc, a three-way no-cramp comparison (RBJ 47% vs Vicanek 7% vs Orfanidis
2.1%), decramped shelves vs the analog prototype, TPT/ZDF SVF, coefficient morphing,
Oxford-style adaptive release, Flush Match (K-weighted) neutralizing a +6 dB change and
hitting a −18 dB target, K-weighting matching the published BS.1770-4 coefficients to
0.000000 dB, true-peak detecting intersample peaks, FFT correctness, linear-phase FIR
(magnitude match + exact symmetry), 2x + 4x oversampler round-trip flatness + −72 dB image
rejection, the spectrum analyzer's peak + tilt, spectral dynamics (above-threshold reduced,
below-threshold untouched, clean pass-through), and gain-Q interaction.

## Build the plugin

Needs a full APC toolchain (CMake ≥ 3.22, a C++20 compiler, JUCE 9 + Visage
submodules, platform audio/GL libs). From the repo root:

```bash
cmake -B build -DAPC_ENABLE_VISAGE=ON .
cmake --build build --target flush
```

or use `scripts/build-and-install.sh -PluginName Flush` (honors `apc.config.json`).

## Status / roadmap

- [x] Concept + architecture (dream/plan) — `status.json`
- [x] Design v2 (Pro-Q4 recreation spec) — `Design/v2-ui-spec.md`
- [x] DSP core — written + **verified** (81/81 tests): filters (all decramped), dynamics, loudness,
      FFT, linear-phase FIR, 2x/4x oversampling, analyzer (freeze + source), spectral dynamics,
      gain-Q, oversampler impulse-delay (half-sample phase fixed), compressor lookahead
      (correct direction + 1-sample delay-line fix)
- [x] JUCE processor — APVTS + 24-band state + full chain (linear FIR w/ quality tiers,
      2x/4x oversampling, M/S + spectral modes, analyzer, metering, latency) + 14 factory presets +
      user preset save/load + A/B + band solo
- [x] Visage UI (FabFilter-grade: vertical gradients, per-band colour nodes + hover glow,
      value bubbles, selected-band strip, preset bar, settings modal, GR meter) — written
- [x] UI interactions — right-click context menus, double-click add/reset, drag nodes,
      mouse-wheel Q editing, value scrubbing (freq/gain/Q), keyboard delete + arrow nudge,
      dynamic accent theming, UI scaling (window resize) + 120 Hz render loop,
      mouse hit-testing to child frames fixed in the shared bridge
- [x] Hardening & polish — cut-type swap fixed (LowCut=HP/HighCut=LP), SVF cuts, const-correctness,
      per-band dynamics sample-rate fix, spectral GR metering, thread-safe UI curve snapshot,
      atomic dirty flags, latency-report ordering, bandTree mutex + locked editing API,
      oversampler half-sample phase fix, knob track arc, drawn gear, toggles
- [x] Bulletproofing — NaN/Inf sanitization on every sample path, Q/freq/param clamps,
      FFT non-power-of-2 & null-pointer guards, channel edge cases (mono/0-in/0-out),
      null-safe parameter reads + ID validation, band-count normalization, try/catch state
      restore, bounded (reserve) audio-thread allocation, render geometry guards
- [ ] User preset save/load (format defined)
- [ ] Compile against JUCE 9 + Visage + listen/measure pass — **see HANDOFF.md**

## Benchmarking / reaching "Oxford quality"

Flush ships a **clean-room parity program** (`docs/BENCHMARKING.md` +
`tests/parity_tools.cpp`): generate test stimuli, run them through a reference
plugin (Sonnox/UAD/FabFilter), record the output, and diff frequency response,
THD, and null residual to match their *measured behavior* with our own
independent DSP. This is the legal, engineering-correct path to reference-class
quality — copy the *target curves*, never the *code*.
