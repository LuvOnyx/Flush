# Flush — Implementation Plan (v4)

## Complexity Score: 4

## Implementation Strategy: Phased

### Phase 4.1.1 — Core infrastructure + Flush Match
- [x] `FlushAudioProcessor` skeleton: APVTS (global + per-band automation + compressor + Flush Match + display), state ValueTree for the 24-band definition.
- [x] Input/output trim, Flush Match (Match Input first) with Mid/Stereo reference + bus-safe timing, metering taps → atomics.
- [x] **Loudness-native (the differentiation):** Flush Match + meters use ITU-R BS.1770-4 K-weighted loudness + 4x-oversampled true-peak (verified against the published 48 kHz coefficients to 0.000000 dB).
- [ ] Build + load test.

### Phase 4.1.2 — EQ engine (static)
- [x] Band Manager (add/remove/reorder/solo, ValueTree).
- [x] Bell: Vicanek matched (Low Latency) + Orfanidis decramped (Natural), double precision. RBJ demoted to measurement baseline.
- [x] LP/HP/BP: Vicanek matched. Notch/all-pass: RBJ (canonical).
- [x] Decramped shelves: Vicanek 2019 matched one-pole (verified vs analog prototype).
- [x] Cut slopes 12-48 dB/oct (cascaded matched sections, per node).
- [x] Precision topology: TPT/ZDF SVF (cuts) + coefficient morphing (verified).
- [x] Phase modes: Low Latency (matched) / Natural (Orfanidis) / Linear (Kaiser FIR + `setLatencySamples`).
- [x] Sample-accurate smoothing (coefficient morphing); coefficient cache invalidation; gain-Q interaction.
- [ ] Build + test (JUCE/visage compile).

### Phase 4.1.3 — Unified Dynamics Engine (compressor + dynamic EQ)
- [x] Build the shared Dynamics Engine: detector (Peak/RMS, optional lookahead) → soft-knee gain
  computer (threshold + ratio/range) → attack/release ballistics → gain multiplier.
- [x] Broadband instance = glue compressor (threshold/ratio/attack/release/detector/auto-makeup + GR report).
- [x] Oxford-style transparent release: program-dependent adaptive release (verified: deep GR → 3× slower release).
- [x] Per-band instances = dynamic EQ (band-filtered sidechain, band-limited gain; threshold/range/attack/release/detector; Compress & Expand). Every node supports dynamic.
- [x] Per-band channel placement (Stereo / L / R / Mid / Side).
- [x] Spectral Dynamics (Pro-Q4 frontier): FFT-based per-frequency dynamics via weighted overlap-add
  (verified: above-threshold tone reduced toward threshold, below-threshold untouched, clean pass-through).
- [ ] Build + test (the sound-quality core of the whole plugin).

### Phase 4.1.4 — Oversampling
- [x] 2x half-band oversampler on the EQ section (near-Nyquist accuracy). Verified: round-trip
  flat to 0.003 dB, image rejection −72 dB.
- [x] 4x oversampling (cascade). Verified: round-trip flat to 0.004 dB.
- [ ] Build + test.

### Phase 4.1.5 — Spectrum analyzer + metering
- [x] 4096-pt Hann FFT, 75% overlap, exponential averaging, tilt/range/freeze, peak hold,
  max-within-band decimation → lock-free buffer. Verified: 3 kHz peak −0.17 dB, tilt +13.5 dB.
- [x] Full metering: true-peak/RMS, K-weighted loudness, M/S, correlation, GR, Flush delta, latency.

### Phase 4.1.6 — Visage UI (Pro-Q4 recreation)
- [x] Custom controls: glassy knobs, VU + bar meters, click-to-cycle choice buttons.
- [x] Interactive EQ display: analyzer + draggable nodes (freq/gain), neon accent.
- [x] Spectrum analyzer rendering behind the curve (gradient-ish fill, log grid, peak hold).
- [x] High-refresh render loop (120 Hz + on-demand invalidation; uncapped targets ≥120 Hz) — in PluginEditor.
- [x] Wire parameters + metering atomics; 60–120 Hz repaint.
- [x] Window ~1100×640 (scalable, retina/DPI aware).
- [ ] Preset browser panel + A/B compare.
- [ ] Settings modal: UI scaling (0.5–2.0), refresh (60/120/Uncapped), accent color.
- [x] Preset browser panel (factory list + A/B compare).
- [x] Settings modal (UI scaling 0.5–2.0, refresh 60/120/Uncapped, accent color; scrim + close-on-outside).
- [ ] Compile + visual fine-tune against Visage (arc orientation, radii, colors).

### Phase 4.1.7 — Presets + polish
- [x] Factory preset pack (Clean Up, Bass, Drums, Vocal Presence, Rap/RnB 808, Mastering Glue, FX Wash).
- [x] A/B compare slots (store/recall full band + parameter state).
- [ ] User preset save/load (writes to OS user-presets dir) — format defined, UI save button pending.
- [ ] Bypass/disabled-state dimming, latency readout, edge cases.

## Dependencies

**JUCE modules:** audio_basics, audio_processors, audio_plugin_client, audio_devices,
audio_utils, core, data_structures, dsp, events, graphics, gui_basics.

**Visage:** `visage::visage` + `common/VisageJuceHost.h` (build with `-DAPC_ENABLE_VISAGE=ON`).

**Assets:** Lato (+ Lato-Bold if available) embedded via `juce_add_binary_data`.

## Risk Assessment

**High risk:**
- Visage render/event integration in a JUCE editor (use `VisageJuceHost.h` windowless path).
- Linear-phase FIR latency correctness (must `setLatencySamples` or hosts mis-compensate).
- Dynamic EQ stability (detector ballistics; prevent gain overshoot/clipping in the audio path).

**Medium risk:**
- Filter design quality (decramped matched/Orfanidis, then Massberg/Gunness-Chauhan shelves) — the core of "not sounding cheap".
- Analyzer CPU + UI smoothness at 120 Hz (worker-thread FFT, decimation, GPU rendering).
- Preset/state capture of a 24-band dynamic model.

**Low risk:**
- Gain stages, parameter plumbing, metering taps.

## Quality Journey (honest, explicit)

- We will not match FabFilter Natural Phase or Sonnox Oxford's exact response in one pass —
  those are proprietary, years-long R&D efforts.
- Our path: correct reference-grade DSP (double precision, analog-matched biquads, proper
  linear phase, oversampling, one shared dynamics engine) → build → **measure + listen** → iterate.
- Parity is a target we refine toward; the architecture is what makes parity possible.

## Out of scope for v1 (roadmap)

- Spectral Dynamics, EQ Sketch, Spectrum Grab, Instance List, EQ Match.
- External sidechain, surround/Atmos.
- Gated integrated LUFS (K-weighted *short-term* loudness is implemented; BS.1770 gating + integrated measurement is a v1.1 refinement).
