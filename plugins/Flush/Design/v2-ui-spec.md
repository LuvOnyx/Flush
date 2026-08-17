# Flush — UI Specification v2 (Pro-Q4 recreation)

> **Reference:** FabFilter Pro-Q4. Native C++ (Visage). No web stack.
> **Window:** 1100 × 640 px default (scalable 50%–200% via settings; retina/DPI aware).
> **Refresh:** uncapped render loop targeting **≥120 Hz** (see "Render loop").

## Layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ HEADER (52px)                                                               │
│ FLUSH        Phase:[Low Latency ▾]  Oversample:[Off ▾]  [⚙]   Latency 0.0ms │
│ Gain-Staged Dynamic EQ                                          Meter:[Bar|VU]│
├──────────────┬───────────────────────────────────────────────────────────────┤
│ INPUT (15%)  │   EQ + SPECTRUM ANALYZER  (55%)                               │
│              │                                                               │
│  ┌────────┐  │   ┌───────────────────────────────────────────────────────┐   │
│  │  TRIM  │  │   │  spectrum gradient fill + log grid + peak hold        │   │
│  └────────┘  │   │  EQ curve (glowing, per-band colored) + 24 node slots │   │
│  IN meter    │   │  draggable nodes (freq↔ / gain↕), multi-band select    │   │
│  (VU | bar)  │   │  band handles glow neon on hover/select                │   │
│  IN -12.4    │   └───────────────────────────────────────────────────────┘   │
│  M:-14 S:-22 │   Band:[Bell ▾] Dyn:[●] [Solo] [Stereo ▾]   Freq  Gain  Q     │
│  CORR +0.85  │   300 Hz  +2.0 dB  1.00      Thr -24  Rng 12  Atk 10  Rel 150 │
├──────────────┼───────────────────────────────┬───────────────────────────────┤
│ PRESET (15%) │  COMPRESSOR (15%)              │ FLUSH MATCH + OUTPUT (15%)    │
│              │                               │                               │
│ [Bass ▾]     │  THRESH  RATIO  ATTACK        │  MODE [Off][Match][Target]     │
│ [A|B]  [⇄]   │  RELEASE  MAKEUP  AUTO ✓      │  Timing [Slow ▾]  Ref [Mid ▾]  │
│ [💾 Save]    │  GR meter                      │  TARGET knob   FLUSH +1.2 dB   │
│ (category     │                               │  OUT meter (VU|bar)  TRIM knob │
│  list)       │                               │  OUT -12.2                     │
└──────────────┴───────────────────────────────┴───────────────────────────────┘
```

## Region details

### Header
- **Left:** "FLUSH" wordmark + tagline "Gain-Staged Dynamic EQ".
- **Right:** Phase Mode dropdown, Oversampling dropdown, **Settings gear (⚙)** (opens the modal),
  live **Latency** readout, and Meter Style toggle (Bar/VU).

### Interactive EQ display (the centerpiece)
- Spectrum analyzer fills the region behind the curve (see v2 style guide for the "expensive" spec).
- **EQ curve:** a composite of all active bands; **each band renders in its own accent color**
  (Pro-Q's per-band color coding) with a **neon glow** on the selected/hovered band.
- **24 node slots** along the curve; nodes are draggable (vertical = gain, horizontal = freq,
  scroll/⌘-drag = Q). Multi-band selection via rubber-band or ⇧-click.
- Selected band shows a **glow halo** + the selected-band editor panel below the display
  (shape dropdown, dynamic on/off, solo, channel placement, Freq/Gain/Q, and dynamic
  Threshold/Range/Attack/Release when dynamic is on).

### Input
- Large glassy Input Trim knob + input meter (VU needle or vertical bar via `meter_mode`).
- IN peak/RMS readouts + **Mid/Side + phase-correlation** mini-block.

### Preset browser
- Category dropdown (Clean Up · Bass · Drums · Vocals · Rap/RnB · Mastering · FX) with a
  scrollable preset list.
- **A/B compare** buttons with copy (⇄), and **Save** (writes to the user-presets directory).
- Preset load restores the **entire 24-band + dynamic + Flush Match state**.

### Compressor
- Threshold / Ratio / Attack / Release / Makeup knobs + Auto-Makeup toggle + GR meter (vertical).

### Flush Match + Output
- Mode segmented buttons (Off / Match Input / Target), Timing + Reference dropdowns, Target Level
  knob, **large Flush delta readout** (e.g. `+1.2 dB`, green up / amber down), Output Trim knob,
  output meter (VU/bar), OUT peak/RMS readouts.

## Settings modal (⚙ — Pro-Q4 style)
A dimmed, blurred overlay panel centered on the window with:
- **UI Scaling** slider 50%–200% (with a numeric readout and a "reset to 100%" affordance) —
  resizes the entire editor live, exactly like Pro-Q4.
- **UI Refresh** dropdown: 60 / 120 / **Uncapped**.
- **Accent Color** swatches (Blue / Cyan / Orange / Violet).
- **Close** (✕) — modal dismisses on outside click / Esc.

## Render loop (uncapped / 120 Hz+)
- The editor runs a **high-frequency repaint timer** (120 Hz) plus **on-demand invalidation**
  when controls change, so dragging nodes/knobs feels immediate.
- `fps_mode`:
  - **60** — fixed 60 Hz (low CPU).
  - **120** — fixed 120 Hz.
  - **Uncapped** — timer at max practical rate; the Visage GPU canvas redraws on every frame the
    host allows (vsync-bound). Target is **≥120 Hz** for crispness.
- Meters/analyzer/readouts animate at the render rate with smoothed (not stepped) values.

## Control behaviors (FabFilter conventions)
- Drag = coarse · Shift-drag = fine · double-click / Alt-click = reset.
- Hover lifts knobs (glow + lighten); active shows an accent value arc.
- Bypassed sections dim to 40% and grey the curve/meters; disabled bands drop off the curve;
  soloed bands highlight while others dim.
- Spectrum Grab-style interaction: click-drag on a spectrum peak creates/adjusts a band (v1
  scope: node editing; Spectrum Grab is a v1.1 stretch goal).
