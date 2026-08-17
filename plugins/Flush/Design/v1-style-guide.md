# Flush — Style Guide v1

> Reference quality: **FabFilter Pro-Q4**. Native Visage rendering (GPU, anti-aliased).

## 1. Color tokens

| Token | Hex | Use |
|---|---|---|
| `--bg-0` | `#17191E` | Panel base (bottom of vertical gradient) |
| `--bg-1` | `#23262C` | Panel base (top of vertical gradient) |
| `--surface` | `#262A31` | Knob plates, section wells, control backgrounds |
| `--surface-2` | `#2E333B` | Hover/raised surfaces, active segments |
| `--stroke` | `#0E0F12` | Inner 1px inset border |
| `--hairline` | `#3A3F48` | Outer 1px hairline / separators |
| `--accent` | `#5BA8FF` | Primary accent (curve, arcs, active states) |
| `--accent-soft` | `#8FC5FF` | Highlight / glow |
| `--text` | `#C9D1DA` | Primary text, values |
| `--text-dim` | `#8A93A0` | Secondary text |
| `--label` | `#6B7480` | Section titles, small labels |
| `--meter-green` | `#3DDC84` | Meter −60…−12 dB |
| `--meter-yellow` | `#FFD166` | Meter −12…−3 dB |
| `--meter-red` | `#FF5C5C` | Meter −3…0 dB / over |
| `--warn` | `#FFB020` | Negative correlation warning |

Accent is **light blue `#5BA8FF`** (Pro-Q family). Warm the accent to `#FF9E5C` for an orange
variant if preferred — the token system makes this a one-line change.

## 2. Typography
- **Font:** Lato (embedded via `juce_add_binary_data`; add Lato-Bold for headers if available,
  otherwise synthesize hierarchy with size + `--text` color).
- Scale (px, DPI-scaled): wordmark 15 · section titles 11 (uppercase, +20% tracking, `--label`) ·
  knob values 14 (`--text`) · small readouts 10 (`--text-dim`) · big Flush delta 20 (monospaced).

## 3. Knobs (glassy rotary)
- **Body:** radial gradient `#4A505C → #2A2E36`, 1px `--stroke` rim, 1px `--hairline` outer.
- **Highlight:** top-left soft arc `#565D6B` @ 25% for the "glass" catch-light.
- **Cap:** `#333842` with a thin `--accent-soft` glint.
- **Value arc:** 2px `--accent` arc from −135° to +135°; `--text-dim` for the inactive track.
- **Hover:** lift (scale 1.04) + outer glow `--accent` @ 20%. **Active:** arc brightens to `--accent-soft`.
- **Sizes:** 44 px standard params · 56 px Input/Output Trim.

## 4. Meters
- **Vertical bar:** 24 px wide, height ~320 px (input) / ~280 px (output). Gradient stops
  `--meter-green → --meter-yellow → --meter-red`, 3 px rounded top, 2 s peak-hold tick, 1 px
  `--hairline` frame, dB tick labels every 6 dB.
- **VU needle:** analog dial, 180° sweep, cream needle `#E8EDF2` with black hub, thin arc scale,
  green/yellow/red zones matching the bar gradient, ballistics ~300 ms rise / ~300 ms fall,
  smooth interpolation at 60 Hz. Same peak/RMS data as the bar.

## 5. Spectrum analyzer (the "not web appy" part)
- **Fill:** vertical gradient from `--accent` @ 70% (bottom) → `--accent` @ 0% (top) — no flat
  solid fills, no cheap single-color polygon.
- **Grid:** 1px lines at `--stroke` (decade lines slightly brighter), 6 dB horizontal steps,
  log-frequency labels (20/50/100/200/500/1k/2k/5k/10k/20k Hz) in `--label`.
- **Peak hold:** `--text` @ 60%, falling at ~1.5 s.
- **EQ curve:** 2 px `#EAF0F6` composite + 4 node handles (`--accent` ring, white center);
  handle glow on hover/selection.
- Anti-aliased polygon + lines (Visage canvas), 60 Hz update from the lock-free magnitude buffer.

## 6. Buttons / toggles / dropdowns
- **Segmented buttons** (mode selectors): `--surface`, active = `--surface-2` + `--accent` text.
- **Toggles:** 28 px pill, `--surface` off / `--accent` on, soft inner shadow.
- **Dropdowns:** `--surface` with `--hairline` border, chevron glyph, popup list on `--surface-2`
  with hover row highlight; value shown in `--text`, label in `--label`.

## 7. Spacing & borders
- 1 px `--stroke` inset + 1 px `--hairline` outer on every section (crisp, "machined" edges).
- Section padding 12 px; control label→value gap 4 px; knob→label gap 8 px.
- Hairline separators between the five body regions.

## 8. Interaction states
- `default → hover → active → disabled/bypass`
- Disabled/bypass: 40% opacity + grey (no accent); bypassed EQ/comp also grey the curve/GR meter.
