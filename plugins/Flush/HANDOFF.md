# Flush — Handoff for the Next Agent

> **Goal of this document:** get you from a fresh `git clone` to a **compiled,
> loadable, verified** Flush VST3/AU/Standalone as fast as possible, then point
> you at the remaining work. Read this fully before touching code.

---

## 1. What Flush is (30-second brief)

**Flush** is a Pro-Q4-class **24-band dynamic EQ** wrapped around a
gain-staging engine, built on **JUCE 9 + Visage (pure C++ UI)** — no web stack.

- **24-band dynamic EQ** — bell/notch/shelves/cuts (12–48 dB/oct)/band-pass/
  tilt/flat-tilt/all-pass; **every band can be dynamic** (threshold/range/
  attack/release); per-band Stereo / Mid / Side placement.
- **Phase modes** — Low Latency (Vicanek matched) / Natural (Orfanidis decramped)
  / Linear (Kaiser-FIR, quality tiers Low/Med/High/Max).
- **Flush Match** (the hero feature) — auto output gain measured on
  **ITU-R BS.1770-4 K-weighted loudness**: *Match Input* (tone changes, level
  stays) or *Target* (normalize into the limiter). Bus-safe timing.
- **Compressor** — Sonnox-Oxford-style (soft knee + program-dependent adaptive
  release), plus a **Spectral** mode (FFT per-frequency dynamics, Pro-Q4 frontier).
- **Metering** — true-peak (4× oversampled), RMS, K-weighted loudness, Mid/Side,
  phase correlation, gain reduction, live "Flush: +x.x dB".

The DSP core is **pure C++ (double precision), no JUCE dependency**, and is
**61/61 verified** by a standalone test binary you can rebuild with plain `g++`.

---

## 2. Status at handoff — what is done vs. not

| Layer | Status | Confidence |
|---|---|---|
| DSP core (`Source/dsp/*.h`) | **Done + verified** | ✅ 61/61 assertions pass (g++) |
| JUCE processor (`PluginProcessor.*`) | **Written, not compiled** | ⚠️ follows repo conventions; needs first build |
| Visage UI (`VisageControls.h`, `PluginEditor.*`) | **Written, not compiled** | ⚠️ needs first build + visual tuning |
| CMakeLists.txt | **Written** | ⚠️ needs configure |
| Presets (7 factory + A/B) | **Done** (in processor) | ✅ |
| Full plugin build / DAW load / listen pass | **NOT done** | ❌ your job |

**The single most important fact:** everything below the JUCE boundary is proven;
everything *at* the JUCE/Visage boundary has never been compiled. Expect (and
budget for) a first-compile fix-up session. That is normal and expected.

---

## 3. Clone & prerequisites

```bash
git clone --recurse-submodules https://github.com/LuvOnyx/Flush.git
cd Flush
git checkout arena/01a00728-flush        # the working branch
```

The submodules are `_tools/JUCE` (JUCE 9), `_tools/visage`, `_tools/pluginval`.
If the clone above didn't pull them, run `git submodule update --init`.

### Per-platform prerequisites (from APC's README)

- **Windows:** Visual Studio 2022 (C++), CMake ≥ 3.22, WebView2 Runtime.
- **macOS:** Xcode + command-line tools, CMake ≥ 3.22.
- **Linux:** GCC 9+ / Clang 10+ (C++20), CMake ≥ 3.22, ninja or make,
  **libasound2-dev**, **libwebkit2gtk-4.1-dev**, **libegl-dev**,
  (libjack-dev optional).

---

## 4. First build (exact commands)

From the repo root:

```bash
# 1) Verify the DSP core WITHOUT any JUCE/visage toolchain (fast sanity check):
cd plugins/Flush/tests
g++ -std=c++20 -O2 -I../Source/dsp dsp_smoke.cpp -o dsp_smoke && ./dsp_smoke
cd ../../..

# 2) Configure (Visage is required for Flush):
cmake -B build -DAPC_ENABLE_VISAGE=ON .

# 3) Build just Flush:
cmake --build build --target flush -j
```

The built plugin lands under `build/Flush_artefacts/` (VST3 / AU / Standalone
depending on platform).

> APC also ships `scripts/build-and-install.sh` (macOS/Linux) and
> `scripts/build-and-install.ps1` (Windows) which honor `apc.config.json` paths —
> use those instead of raw `cmake` if you prefer the framework's wrapper.

---

## 5. First-compile checklist (expected fix-ups)

These are the places a first build will most likely bite. Check them in order.

1. **`BinaryData` namespace.** `VisageControls.h` references
   `flush_BinaryData::LatoRegular_ttf`. `CMakeLists.txt` uses
   `juce_add_binary_data(flush_Assets ... NAMESPACE flush_BinaryData)` with the
   Lato font from `_tools/visage/visage_graphics/fonts/Lato-Regular.ttf`. If the
   font path is missing at configure time, that's the first failure — fix the path.

2. **Visage `canvas.arc` parameter order.** In `FlushKnob`/`FlushMeter`, the arc
   is drawn as `(x, y, width, thickness, center_radians, radians, rounded)`.
   Verify against the actual signature in
   `_tools/visage/visage_graphics/canvas.h` — arc *orientation* (clock vs.
   counter-clock, where 0° points) is the #1 visual thing to tune after it runs.

3. **`canvas.text` string type.** UI code passes `const char*` (via `.toRawUTF8()`)
   — `visage::String` has a `const char*` constructor, so this should be fine;
   if not, wrap in `visage::String(...)`.

4. **Visage `Frame::mouseDown` vs `on_mouse_down_`.** `FlushKnob`/`FlushEqDisplay`
   etc. override `mouseDown/mouseDrag/mouseUp`, which the verified
   `frame.h` provides as virtuals. If a subclass name collides, rename.

5. **`juce::jlimit`/`juce::StringArray` etc.** — all standard JUCE; nothing exotic.

6. **Member order** — `VisagePluginEditor` (in `common/VisageJuceHost.h`) is the
   host bridge; `PluginEditor.*` follows the APC visage template. If you hit the
   webview/relay ordering issue, that's a *webview* plugin problem — Flush is
   visage, so ignore those warnings.

7. **`setLatencySamples`** must be called (it is, in `updateLatency()`) so hosts
   delay-compensate linear-phase/oversampling/spectral latency. The 2x/4x
   oversampler latency is exact and integer (48 / 72 samples @ 48 kHz) after the
   half-sample phase fix — the impulse-delay tests in `dsp_smoke.cpp` prove it.

8. **Mouse routing (IMPORTANT, already fixed).** The shared `common/VisageJuceHost.h`
   now hit-tests the deepest child frame (`frameAtPoint`) and converts native→logical
   coordinates before dispatching. If you see knobs/nodes not responding, check this
   bridge first — it was the #1 reason child-frame interaction was dead.

9. **Keyboard focus.** `EDITOR_WANTS_KEYBOARD_FOCUS` is `TRUE` so Delete/Backspace
   and arrow-nudge reach the EQ display. Some DAWs will still eat those keys —
   verify in your host; if a host steals them, that's expected DAW behavior, not a
   Flush bug.

---

## 6. After it loads — the "measure → listen → iterate" loop

This is the actual remaining work. Do it in this order:

1. **Null test.** Route audio through Flush with all bands flat, EQ/comp bypassed,
   Flush Match off, and confirm bit-transparent output (or a documented,
   inaudible floor).
2. **Flush Match sanity.** +6 dB EQ boost → output should return to input level
   (Match Input) with slow, pump-free movement. Verify on a full mix, not just a
   sine.
3. **Listen to the three phase modes** on a high shelf/air band near 16–20 kHz.
   Natural (Orfanidis) must sound more open than Low Latency (Vicanek) — the
   decramping is audible as "more air, less closed top end."
4. **Dynamic band / compressor** — the adaptive release should feel invisible,
   not pumpy; the Spectral compressor mode should tame a resonance without
   touching the rest of the signal.
5. **UI** — verify knob drag / shift-fine / double-click-reset, VU↔bar meter
   switch, draggable EQ nodes, preset toolbar + A/B, settings modal
   (scale/refresh/accent). Tune arc orientation, radii, and colors to taste.

---

## 7. Honest quality boundary (read this before "improving")

- **We are NOT matching FabFilter Natural Phase or Sonnox Oxford exactly** — those
  are proprietary, years-long R&D. What we have is *correct, reference-grade DSP*
  (double precision, analog-matched/decramped filters, proper linear phase,
  oversampling, real detectors, BS.1770 loudness) — the architecture that makes
  parity *possible*. Parity is an iterative journey measured by A/B listening,
  not a switch to flip.
- **The DSP was chosen deliberately, not by default.** RBJ cookbook is present
  only as a *measurement baseline* to prove the others beat it (47% near-Nyquist
  cramping vs Vicanek 7% vs Orfanidis 2.1%). Do NOT "upgrade" filters back to RBJ
  or to another cookbook without measuring the trade-off.
- **Differentiation is loudness, not flat EQ.** Flush Match + meters speak
  BS.1770 K-weighted loudness + true-peak. Preserve that; it's the product.

---

## 8. Known remaining TODOs (in rough priority order)

- [ ] **First compile + fix-ups** (§5) — unblocks everything.
- [ ] **User preset save/load** — format already defined (full state XML, see
      `getStateInformation`); only the UI "save" button + a file browser are missing.
- [ ] **UI visual fine-tuning** — arc orientation, exact radii/colors, analyzer
      gradient (currently a per-column alpha falloff; a true gradient fill would
      look more Pro-Q), context-menu styling against the host's appearance.
- [ ] **4× oversampling** is implemented but unverified end-to-end in the plugin
      (the DSP test covers it standalone).
- [ ] **Spectral dynamics** could get a lookahead + stereo-link option.
- [ ] **Gated integrated LUFS** (BS.1770 gating) for Flush Match — currently
      K-weighted *short-term* loudness, which is correct for a live plugin but
      not the gated/integrated variant.
- [ ] **Gain-Q interaction curve** — currently a linear model; could match the
      Pro-Q curve more closely.
- [ ] **Presets**: more factory content, especially for rap/RnB/drill.

---

## 9. File map

```
plugins/Flush/
├── HANDOFF.md                 ← you are here
├── README.md                  ← feature + verification summary
├── CMakeLists.txt             ← build config (visage, Lato font, source list)
├── status.json                ← APC phase tracker (implementation_progress)
├── .ideas/                    ← creative brief, parameter spec, architecture, plan
├── Design/v2-*                ← UI spec + style guide (Pro-Q4 reference)
├── Source/
│   ├── PluginProcessor.{h,cpp}  ← JUCE processor: APVTS, 24-band state, DSP chain,
│   │                              presets, A/B, metering, latency
│   ├── PluginEditor.{h,cpp}      ← Visage editor host (wraps common/VisageJuceHost.h)
│   ├── VisageControls.h          ← entire UI (knobs, meters, EQ display, presets,
│   │                              settings modal) — pure Visage, no web
│   └── dsp/                      ← PURE C++ DSP core (double precision, no JUCE)
│       ├── FlushCommon.h         · FlushFilter.h   (RBJ/Vicanek/Orfanidis/SVF)
│       ├── FlushDynamics.h       · FlushSpectral.h · FlushMatch.h
│       ├── FlushLoudness.h       · FlushFft.h      · FlushFir.h
│       ├── FlushOversample.h     · FlushAnalyzer.h
└── tests/dsp_smoke.cpp         ← 61 assertions, standalone (g++ only)
```

**Good luck — and measure before you "improve."**
