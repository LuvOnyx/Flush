# Flush — DSP Architecture Specification (v6)

## Differentiation META (2026) — why Flush is not "another EQ"

The meta decision is not "find a newer filter formula". Flat-EQ magnitude response is a
**solved, commoditized** problem — thousands of plugins (and thousands of AI agents building
plugins) ship the same biquads and the same RMS meter, which is exactly why they all sound the
same. Flush's differentiation is to stop competing there and win where the category is actually
moving:

1. **Compete on loudness, not flat EQ.** Flush's entire identity is gain staging — and gain
   staging is a *perceptual loudness* problem, not a sample-power problem. The hero feature
   (Flush Match) and the meters speak **ITU-R BS.1770-4 K-weighted loudness + true-peak**
   instead of the commodity RMS/sample-peak meter. Almost no EQ does this.
2. **Precision topology, not just formulas.** TPT/ZDF state-variable biquads + coefficient
   morphing (click-free automation, low-frequency stability, no denormal/quantization noise) —
   the modern *how*, versus the direct-form-II that everyone ships.
3. **The feature frontier** — dynamic EQ with proper detectors + per-band M/S, and spectral
   dynamics (Pro-Q4's newest feature) as the roadmap's parity target.
4. **Analog character via topology/component modeling** as the "sound identity" layer (what
   makes Pro-Q4's Character modes and modeled analog EQs actually sound different) — iterative
   R&D, honestly flagged.
5. **Controlled/mixed phase** (adjustable pre-ringing) beyond a linear-phase on/off switch.

**What we explicitly do NOT chase:** another biquad formula. That space is won; the remaining
wins are perceptual correctness, topology precision, and the feature frontier.



## Sound Quality Standard (the product, not a checklist)

Flush is not "another EQ" — the DSP is engineered to reference-class standards, where sound
quality is the product. Concrete techniques, all on the v1 path:

- **Loudness-native metering (the differentiation)** — Flush Match and the meters use
  **ITU-R BS.1770-4 K-weighted loudness** (verified against the standard's published 48 kHz
  coefficients to 0.000000 dB) and **true-peak** via 4x oversampled windowed-sinc interpolation,
  not commodity RMS/sample-peak.
- **Double-precision math** — all coefficient computation and internal filter state in
  `float64`, converted at the block edges only. No cumulative rounding in the signal path.
- **Analog-matched / decramped filters — the defining difference.** The bilinear transform
  (the RBJ "Audio EQ Cookbook" class) pins the Nyquist gain to the DC gain and "cramps" the
  high end — that is the commodity sound. Flush uses the matched/decramped family instead:
  - **Vicanek "matched" biquads (2016)** — impulse-invariance poles + magnitude-matched
    numerators. Used for the **Low Latency** phase mode (minimum-phase, zero latency).
  - **Orfanidis decramped peaking (1997)** — the "prescribed Nyquist-frequency gain" design
    that fixed cramping; this is the high-frequency-decramping technique reference EQs (the
    FabFilter "Natural Phase" class) are built on. Used for the **Natural** phase mode.
  - **Measured** (12 dB bell, Q=2): RBJ narrows **47%** near Nyquist; Vicanek holds within
    **7%**; Orfanidis within **2.1%** — while keeping RBJ's exact Q feel.
  - **RBJ is demoted to a measurement baseline** (and the still-canonical notch/all-pass
    formulas). It is never a user-facing path.
- **Decramped shelves (Vicanek 2019)** — "Matched One-Pole Digital Shelving Filters": the shelf
  matches the analog prototype across the whole range (verified: max |digital − analog| = 0.05 dB
  @ fc=2 kHz and 0.27 dB @ fc=10 kHz; exact unity DC; plateau hits +12.000 dB with no droop).
  Replaces the commodity RBJ shelf.
- **Decramped cuts** — Vicanek matched LP/HP sections, cascaded for 12–48 dB/oct slopes (1–4
  sections per node).
- **Gain–Q interaction** — optional analog-style coupling so Q widens as gain approaches zero,
  matching hardware/Pro-Q feel.
- **Oxford-style transparent dynamics** — program-dependent (adaptive) release in the shared
  Dynamics Engine: heavy gain reduction "hangs" and light GR lets go, so dynamic bands and the
  glue compressor stay invisible instead of pumping (verified: deep GR → 3× slower release).
- **Phase modes:**
  - **Low Latency** — Vicanek matched biquads (minimum-phase, zero latency).
  - **Natural** — Orfanidis decramped peaking (minimum-phase, zero latency).
  - **Linear** — high-quality linear-phase FIR (Kaiser windowed-sinc, controlled ripple, exact
    magnitude), configurable latency reported via `setLatencySamples()`.
- **Optional 2x/4x polyphase oversampling** — for near-Nyquist accuracy on high shelves/air and
  for linear-phase stopband quality.
- **Precision topology** — TPT/ZDF state-variable filter (denormal-free, low-frequency stable;
  verified −3.010 dB @ fc for LP/HP) for the cut filters, plus **coefficient morphing**
  (output-crossfade) so parameter changes/preset loads can never introduce a discontinuous step.
  Direct-digital-domain filter design (2010+) is a further roadmap item.
- **Sample-accurate smoothing** — coefficients ramp per-sample with no zipper noise; automation
  is click-free.
- **Denormal protection** — `ScopedNoDenormals` + FTZ so silence costs nothing and never crackles.

> Honest note (kept on record): matching FabFilter's Natural Phase or Sonnox Oxford's exact
> proprietary response is years of R&D we cannot shortcut. Our path is correct, reference-grade
> DSP + measurement + listening iteration. The architecture is what makes parity achievable;
> parity itself is the journey — not a promise made in one pass.

## Unified Dynamics Engine (compressor powers dynamic EQ)

A single, well-tested dynamics core, instantiated two ways:

```
┌───────────────────────────────────────────────────────────────┐
│                    DYNAMICS ENGINE (shared)                    │
│  sidechain ─► detector (Peak/RMS, optional lookahead)          │
│            ─► gain computer (threshold + ratio/range, soft knee)│
│            ─► ballistics (attack/release smoothing)            │
│            ─► time-varying gain multiplier                     │
└───────────────────────────────────────────────────────────────┘
             │                                    │
   broadband instance                    per-band instances
   (Glue Compressor)                     (Dynamic EQ bands)
   full signal ─► engine ─►               band-filtered signal ─► engine ─►
   broadband gain                        band-limited gain
```

- **Broadband instance = glue compressor:** full signal through the engine → full-spectrum gain.
- **Per-band instance = dynamic EQ:** sidechain is the band-filtered signal (taken pre-gain so
  the detector tracks the signal *entering* the band), and the resulting gain is applied
  *through that same band filter*. A dynamic band is literally a band-limited compressor.
- **Why one engine:** consistent ballistics, one code path to optimize for sound quality, and
  the compressor genuinely powers every dynamic band.

## Core Components

1. **Input Trim** — `juce::dsp::Gain` (dB).
2. **Band Manager** — up to 24 bands in a `ValueTree`; each band is a config node (shape, freq,
   gain, Q, slope, dynamic settings, channel placement, solo). Add/remove/reorder/solo lives
   here, independent of the audio path.
3. **24-band Dynamic EQ** — per active band:
   - **Static shapes:** Bell, Notch, Low/High Shelf, Low/High Cut, Band Pass, Tilt Shelf,
     Flat Tilt, All Pass.
   - **Every node supports dynamic mode** (bell/shelf) — the band's share of the Dynamics Engine,
     with the Oxford-style adaptive release.
   - **Cut slopes 12–48 dB/oct** on every node (1–4 cascaded matched sections).
   - **Per-channel placement:** Stereo / L / R / Mid / Side (M/S via sum/diff matrix).
   - Phase mode applies to the whole EQ section; linear phase routes bands through an FIR path.
4. **Dynamics Engine + Glue Compressor** — the Sonnox-Oxford-style transparent compressor:
   clean feed-forward detector (Peak/RMS), soft knee, and program-dependent adaptive release.
   The compressor's user parameters are the broadband instance; dynamic bands reuse the engine
   with per-band threshold/range/attack/release/detector + adaptive release.
5. **Flush Match** — auto gain stage (Mid or Stereo Mid+Side loudness reference, bus-safe
   Slow/Medium/Fast timing, Match Input / Target modes). Long RMS window + slow smoothing so it
   levels a full mix without pumping. (Flush's explicit, always-visible take on gain staging.)
6. **Metering taps** — input/output **true-peak** + RMS, K-weighted loudness, Mid/Side levels,
   phase correlation, GR, Flush delta, current latency → `std::atomic<float>` for the UI. No
   locks in the audio thread.
7. **Spectrum analyzer (Pro-Q4 bar)** — 4096-pt Hann FFT, 75% overlap, exponential averaging,
   configurable tilt/range/freeze, soft peak hold, gradient fill; magnitudes to the UI over a
   lock-free double buffer.

## Processing Chain

```
buffer → Input Trim → [meter_in] → Band Manager/Dynamic EQ (24 bands, phase mode)
       → Glue Compressor (broadband Dynamics Engine) → Flush Match (auto gain)
       → Output Trim → [meter_out] → buffer
```

## Parameter Mapping

| Parameter(s) | Component | Function |
| :--- | :--- | :--- |
| `input_gain`, `output_gain` | Gain stages | Trim |
| `eq_*` global + band fields | Band Manager + Dynamic EQ | Shape/dynamics/channel per band; phase mode, oversample |
| `comp_*` | Glue Compressor | Broadband instance of the Dynamics Engine |
| `dyn_*` (per band) | Dynamic EQ bands | Per-band instances of the Dynamics Engine |
| `flush_*`, `match_target` | Flush Match | Mode/timing/reference/target |
| `preset_*`, `ui_scale`, `fps_mode`, `analyzer_*`, `meter_mode`, `theme_accent` | UI/settings | Presets, scaling, refresh, analyzer, meters, accent |

## Real-Time Safety

- No heap allocation, locks, or file I/O in `processBlock`; `juce::ScopedNoDenormals`.
- Parameters read via atomics; coefficients recomputed only on change (cache invalidation).
- FIR/FFT paths pre-allocated in `prepareToPlay`; analyzer FFT can run on a worker thread.

## Complexity Assessment

**Score: 4 (Expert)** — 24-band dynamic EQ with phase modes + oversampling + shared dynamics
engine (glue compressor + dynamic bands) + auto-gain stage + analyzer + metering.
(5 is reserved for synthesis/spectral-resynthesis class.)

## Out of scope for v1 (roadmap)

- Spectral Dynamics mode (per-frequency dynamic EQ, Pro-Q4's newest feature).
- EQ Sketch / Spectrum Grab / Instance List / EQ Match.
- External sidechain for dynamic bands.
- Surround/Atmos (9.1.6) channel support.
