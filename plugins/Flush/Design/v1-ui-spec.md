# Flush — UI Specification v1

> **Quality bar:** FabFilter Pro-Q4. Native C++ (Visage) — no web stack.
> **Window:** 1080 × 560 px (scalable; min 900 × 480, retina/DPI aware).
> **Framework:** Visage (pure C++), rendered on the GPU via `common/VisageJuceHost.h`.

## Layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ HEADER (48px)                                                                │
│ FLUSH  Gain-Staged Channel Strip      Phase: [Low Latency ▾]  Meter: [Bar|VU] │
│                                        Analyzer: [●] [Med ▾]   Latency: 0 ms  │
├──────────────┬───────────────────────────────────────────┬─────────┬─────────┤
│ INPUT (16%)  │  EQ + SPECTRUM ANALYZER (42%)              │ COMP    │ FLUSH + │
│              │                                           │ (19%)   │ OUTPUT  │
│  ┌────────┐  │  ┌─────────────────────────────────────┐  │         │ (23%)   │
│  │  TRIM  │  │  │  analyzer gradient fill + grid      │  │ THRESH  │         │
│  │  (knob)│  │  │  EQ curve + 4 draggable nodes       │  │ RATIO   │  MODE   │
│  └────────┘  │  │  (freq 20Hz→20kHz, dB -90→0)        │  │ ATTACK  │  [Off][M]│
│  IN meter    │  │                                     │  │ RELEASE │  [T]     │
│  (VU or bar) │  └─────────────────────────────────────┘  │ MAKEUP  │  TARGET  │
│  IN -12.4    │  Selected band: [LOW-MID ▾]  [BYPASS]      │ AUTO ✓  │  +x.x dB │
│  M: -14 S:-22│  FREQ ▸ 300 Hz   GAIN ▸ +2.0   Q ▸ 1.00    │ GR meter│  FLUSH   │
│  CORR +0.85  │                                           │         │  delta   │
│              │                                           │         │  OUT     │
│              │                                           │         │  meter   │
│              │                                           │         │  TRIM    │
└──────────────┴───────────────────────────────────────────┴─────────┴─────────┘
```

Five vertical regions after the header:

| Region | Width | Contents |
|---|---|---|
| **Input** | ~16% | Input Trim knob (large), input level meter (VU or bar), IN peak/RMS readout, Mid/Side + correlation mini-block |
| **EQ + Analyzer** | ~42% | Spectrum analyzer + EQ curve (draggable nodes), selected-band panel (type, bypass, Freq/Gain/Q knobs) |
| **Compressor** | ~19% | Threshold, Ratio, Attack, Release, Makeup knobs, Auto-Makeup toggle, GR meter |
| **Flush + Output** | ~23% | Flush Match mode buttons, Timing/Reference dropdowns, Target Level knob, big Flush delta readout, Output Trim knob, output meter |

## Region details

### Header
- **Left:** "FLUSH" wordmark + tagline "Gain-Staged Channel Strip".
- **Right:** Phase Mode dropdown (`Low Latency` / `Natural` / `Linear`), Meter Style toggle
  (`Bar` / `VU`), Analyzer on/off + Speed dropdown (`Slow` / `Medium` / `Fast`), and a live
  **Latency** readout (e.g. `0 ms`, or `10.7 ms` in Linear mode).

### Input
- Large glassy **Input Trim** knob (56 px), −24…+24 dB, numeric readout.
- **Input meter** — vertical bar (gradient green→yellow→red, peak-hold tick) or analog VU needle
  (curved scale, red over-zone), per `meter_mode`.
- IN peak + RMS numeric (dBFS).
- **M/S + correlation block:** Mid (L+R) and Side (L−R) dBFS readouts + a bipolar phase-correlation
  bar (−1 … +1) that tints amber/red as it drifts negative.

### EQ + Spectrum Analyzer
- The analyzer fills the region: **gradient-filled spectrum** (accent→transparent), fine log-frequency
  grid, dB scale −90…0, soft peak-hold. See style guide for the "expensive" rendering spec.
- **EQ curve overlay:** white composite curve of the 4 active bands, with 4 draggable node handles
  (drag vertically = gain, horizontally = freq; Q via handle + scroll or the Q knob).
- **Selected-band panel:** band-type dropdown (Low Shelf / Low-Mid / High-Mid / High Shelf), band
  on/off + bypass, and **Freq / Gain / Q** knobs (Q disabled for shelves). Phase mode lives in the
  header (applies to the whole EQ).

### Compressor
- Knobs: Threshold (−60…0 dB), Ratio (1:1…20:1), Attack (0.1…100 ms), Release (10…2000 ms),
  Makeup (−24…+24 dB). Auto-Makeup toggle.
- **GR meter** (vertical, downward, 0…−24 dB) with numeric.

### Flush Match + Output
- **Mode buttons:** Off / Match Input / Target (segmented, accent when active).
- **Timing** dropdown (Slow (Bus) / Medium / Fast) and **Reference** dropdown (Mid / Stereo).
- **Target Level** knob (−30…0 dB, default −18) — active in Target mode only.
- **Flush delta readout** — large, monospaced, e.g. `+1.2 dB` (green when compensating up,
  amber when down) — this is the "answer" the user learns from.
- **Output Trim** knob (56 px) + output meter (VU or bar) + OUT peak/RMS numeric.

## Control behaviors (FabFilter conventions)
- **Drag** on a knob = coarse; **Shift-drag** = fine; **double-click / Alt-click** = reset to default.
- **Hover** lifts the knob (subtle glow + lighten); **active** shows an accent value arc.
- **Bypassed sections** dim to 40% and grey the curve/meters; **disabled bands** drop off the curve.
- All meters/analyzer/readouts animate at 60 Hz with no jank (values are atomic snapshots).

## Metering & analyzer summary
- Level meters: peak + RMS, VU or bar via `meter_mode`.
- GR meter: compressor gain reduction (always a vertical bar).
- Correlation: bipolar bar, −1…+1.
- Analyzer: 4096-pt Hann FFT, 75% overlap, ~120 ms averaging (speed-scaled), 4.5 dB/oct pink tilt,
  ~1.5 s peak hold, gradient fill, log-frequency 20 Hz–20 kHz, −90…0 dB.
