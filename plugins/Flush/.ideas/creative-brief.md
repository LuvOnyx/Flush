# Flush — Creative Brief (v2)

## Hook
**"A Pro-Q4-class dynamic EQ that also fixes the one thing no EQ fixes: your gain staging."**

## The Problem (why Flush exists)
Modern rap / RnB / drill mixing lives or dies on two things: a transparent, surgical EQ and
clean gain staging. FabFilter Pro-Q4 gives you the EQ but leaves the output knob to guesswork;
after shaping a sound the *loudness* has changed and there's no way to know if it's −1 dB or
+1 dB. Flush removes that guess entirely.

## Vision
A reference-class **24-band dynamic EQ** (Pro-Q4 quality bar) wrapped around the **Flush Match**
gain-staging engine:

- **24-band dynamic EQ** — every Pro-Q4 shape (Bell, Notch, shelves, cuts, Band Pass, Tilt,
  Flat Tilt, All Pass), continuous slopes to 96 dB/oct, per-band **dynamic mode** with
  threshold/range/attack/release, and per-band **Stereo / L / R / Mid / Side** placement.
- **Phase modes** — Low Latency / Natural / Linear (the Pro-Q trio), with live latency readout.
- **Unified Dynamics Engine** — one shared detector/gain-computer/ballistics core powers both the
  glue compressor (broadband) and every dynamic EQ band (band-limited). The compressor literally
  *is* the engine behind dynamic mode — consistent, predictable, and sound-quality-first.
- **Flush Match** — continuously measures input vs. output loudness and sets the output gain
  automatically: **Match Input** (tone changes, level stays) or **Target Level** (consistent
  level into the limiter). The explicit, always-visible take on gain staging.
- **Glue compressor** with auto-makeup and a gain-reduction meter.
- **Pro-Q4-grade UI** — glowing, draggable EQ nodes, high-quality spectrum analyzer, preset
  browser, settings modal with UI scaling, and a 120 Hz+ uncapped render loop.

## Sonic Character
**Transparent and precise.** Flush does not imprint its own tone; it makes the user's EQ and
compression decisions sound right without fighting the level.

**Sound quality is the product, not a feature list.** Flush targets the FabFilter / Sonnox Oxford
class on *sound*, not just on features: double-precision math, analog-matched (Orfanidis)
biquads with no cramping near Nyquist, correct linear phase, optional oversampling, and one
shared dynamics engine whose ballistics are consistent everywhere. The goal is not "another EQ"
— it is an EQ worth using and buying, where the DSP stands on its own next to the leaders.

## Signal Flow
```
Input → Input Trim → 24-band Dynamic EQ (phase mode, optional oversampling)
      → Glue Compressor → Flush Match (auto gain) → Output Trim → Output
```

## Mixbus behavior (primary use case)
On a stereo mixbus Flush receives the **summed L/R of every routed channel**, so it sees and
levels the entire mix. It sets/keeps a consistent *level* for the whole mix — it cannot (and is
not meant to) rebalance individual tracks.

"Leveling the whole mix" must be **slow**, or it would pump on verse/chorus/drop changes like a
bad compressor. Flush Match therefore:
- Measures the **Mid (mono-sum)** by default (width-stable) or **Stereo Mid+Side** energy.
- Uses a **long RMS window (~1–2 s)** and **slow gain smoothing (~1–3 s)**.
- Exposes a **Timing** control (Slow (Bus) / Medium / Fast).

## Mid/Side & phase (see it first; fix it in v2)
Flush *measures* Mid (L+R) and Side (L−R) and shows a phase-correlation meter, so you can spot
phase/width problems. A gain stage cannot *fix* phase — phase-fixing tools (M/S balance/width,
per-channel polarity) are v2. Per-band Mid/Side EQ *processing* is included in v1 (that's the
EQ acting on M or S, distinct from "fixing phase").

## Design Philosophy
**Pro-Q4 is the visual and quality reference.** Flat near-black panel, glowing neon
nodes/bands/lines, glassy knobs with catch-light, hairline bezels, precise numeric readouts,
120 Hz+ uncapped rendering. Native **C++ (Visage)** front-end — it must feel like a real,
compiled plugin, not a web page.

## Target Use Cases
- **Mixbus / master bus (primary)** — EQ the summed mix, keep the whole mix at a consistent
  level, normalize into the limiter.
- **Rap/RnB/drill vocals** — surgical dynamic EQ + consistent level.
- **808 / bass** — cut mud, tighten with dynamic EQ, keep the sub where it was.
- **Drums / bus glue** — shape and glue without runaway level changes.

## Non-Goals (v1)
- Spectral Dynamics mode, EQ Sketch, Spectrum Grab, Instance List, EQ Match (roadmap).
- Surround/Atmos (9.1.6) channel support.
- Saturation/character modeling (Pro-Q4 "Character" modes are a later option).
