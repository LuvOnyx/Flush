# Flush — Style Guide v2 (Pro-Q4 recreation)

> Reference quality: **FabFilter Pro-Q4** — dark, neon-glow, glassy. Native Visage rendering
> (GPU, anti-aliased), 120 Hz+.

## 1. Color tokens

| Token | Hex | Use |
|---|---|---|
| `--bg-0` | `#101216` | Panel base (bottom of gradient) |
| `--bg-1` | `#1B1E24` | Panel base (top of gradient) |
| `--surface` | `#23272F` | Knob plates, wells, controls |
| `--surface-2` | `#2A3038` | Hover/raised, active segments, modal |
| `--stroke` | `#0B0D10` | Inner 1px inset border |
| `--hairline` | `#343B45` | Outer hairline / separators |
| `--accent` | `#3EA6FF` | Primary accent (Blue) — neon glow source |
| `--accent-soft` | `#8FCBFF` | Glow highlight |
| `--accent-cyan` | `#3EE6E0` | Cyan theme variant |
| `--accent-orange` | `#FF9E5C` | Orange theme variant |
| `--accent-violet` | `#B18CFF` | Violet theme variant |
| `--text` | `#D4DBE3` | Primary text / values |
| `--text-dim` | `#9099A5` | Secondary text |
| `--label` | `#6C7480` | Section titles / small labels |
| `--meter-green` | `#3DDC84` | −60…−12 dB |
| `--meter-yellow` | `#FFD166` | −12…−3 dB |
| `--meter-red` | `#FF5C5C` | −3…0 dB / over |
| `--warn` | `#FFB020` | Negative correlation |

**Per-band palette** (Pro-Q-style color coding — each band gets its own hue; selected band
glows): the 24 slots cycle through a curated hue wheel (blue → cyan → green → yellow → orange →
pink → violet …) with consistent saturation/luminance so no band outshouts another.

## 2. Typography
- **Font:** Lato (embedded; Lato-Bold for the wordmark/headers if available).
- Scale (px, DPI-scaled): wordmark 16 · tagline 10 (`--label`) · section titles 11 uppercase
  (+20% tracking, `--label`) · knob values 14 (`--text`) · small readouts 10 (`--text-dim`) ·
  Flush delta 22 (monospaced) · latency 11 (`--text-dim`).

## 3. Neon glow (the Pro-Q signature)
- **Selected/hovered EQ band:** outer glow `--accent` @ 35% blur 12 px, node handle with a
  brighter core + `--accent-soft` halo, 2 px band curve brightened.
- **Curve:** 2 px composite, `--text` @ 90%, with per-band color where bands overlap the
  selected band's color dominance.
- **Glow is additive & GPU-cheap:** render glow as layered translucent strokes/radial fills,
  not expensive blurs, to stay ≥120 Hz.

## 4. Knobs (glassy rotary)
- Radial gradient `#4A505C → #262B33`, 1px `--stroke` rim, 1px `--hairline` outer, top-left
  catch-light `#565D6B` @ 25%, cap `#30363F` with `--accent-soft` glint.
- **Value arc** 2px `--accent` −135°…+135°; inactive track `--text-dim` @ 30%.
- Hover: lift (scale 1.04) + glow `--accent` @ 20%. Active: arc → `--accent-soft`.
- Sizes: 44 px params · 56 px Input/Output Trim.

## 5. Meters
- **Vertical bar:** 24 px wide, gradient green→yellow→red, 3 px rounded top, 2 s peak-hold tick,
  1 px `--hairline` frame, dB ticks every 6 dB.
- **VU needle:** analog 180° dial, cream needle `#E8EDF2`, black hub, arc scale with green/
  yellow/red zones, ~300 ms ballistics, 60–120 Hz interpolation.

## 6. Spectrum analyzer (the "not web appy" part)
- **Fill:** vertical gradient `--accent` @ 70% (bottom) → `--accent` @ 0% (top); soft, feathered
  top edge. No flat solid fills.
- **Grid:** 1px `--stroke` lines (decades brighter), 6 dB horizontal steps, log-frequency labels
  (20…20k Hz) in `--label`, dB axis −90…0.
- **Peak hold:** `--text` @ 60%, ~1.5 s fall, freeze = hold.
- **Tilt:** 4.5 dB/oct default (configurable 0–6) so broadband reads flat.
- Anti-aliased polygon + lines, 120 Hz from the lock-free magnitude buffer.

## 7. Buttons / toggles / dropdowns / modal
- **Segmented buttons:** `--surface`; active = `--surface-2` + `--accent` text + 1 px `--accent`
  underline.
- **Toggles:** 28 px pill, `--surface` off / `--accent` on, soft inner shadow.
- **Dropdowns:** `--surface` + `--hairline` border, chevron glyph, popup list on `--surface-2`
  with hover row highlight.
- **Settings modal:** `--surface-2` panel, 1 px `--hairline`, drop shadow, **dim + blur the
  editor behind it**; UI-scaling slider with numeric readout and reset affordance; closes on
  outside click / Esc / ✕.

## 8. Spacing & borders
- 1 px `--stroke` inset + 1 px `--hairline` outer on sections; 12 px section padding; hairline
  separators between regions; 4 px label→value, 8 px knob→label gaps.

## 9. Interaction states
- `default → hover → active → disabled/bypass`
- Disabled/bypass: 40% opacity + grey; bypassed EQ/comp grey the curve/GR meter; solo dims the
  non-soloed bands.
