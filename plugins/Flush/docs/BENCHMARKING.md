# Flush — Benchmarking & Parity Program (clean-room)

> **The question:** "Can we just use the same data as Sonnox / Universal Audio?"
>
> **The short answer:** No — their DSP coefficients, filter tables, and impulse
> responses are proprietary and can't be copied. **But** there is a legitimate,
> measurement-driven path to reach their quality, and this document is it.

---

## 1. The legal / ethical line (read this first)

| You may | You may NOT |
|---|---|
| Study **public academic DSP** (Orfanidis, Vicanek, Massberg, RBJ, Zölzer, ITU BS.1770…) | Copy anyone's code, coefficients, or data tables extracted from a binary |
| Study **expired analog hardware schematics** (Pultec, API, Neve, SSL, 1176, LA-2A…) | Extract or lift a competitor's DSP/IR data |
| **Measure** a reference plugin's I/O behavior (run test tones through it, record the output) | Decompile/disassemble a competitor's binary |
| Design an independent implementation to **match a measured target curve** | Reproduce a competitor's proprietary filter design from leaked source |

The clean-room rule: **measure the *behavior*, never the *implementation*.** A
frequency-response or THD curve you measured is a fact about physics, not IP.
The code that produced it is IP. This is how every serious audio company does
competitive analysis.

---

## 2. Three legitimate sources of "their information"

### 2a. Public academic DSP (already in Flush)

Sonnox, Universal Audio, and FabFilter all stand on the *same published papers*
Flush already uses — this is the "same data" that is genuinely shareable:

- **Orfanidis (1997)** — "Digital parametric equalizer design with prescribed
  Nyquist-frequency gain" → Flush's *Natural* phase mode.
- **Vicanek (2016)** — "Matched Second Order Digital Filters" → *Low Latency* mode.
- **Vicanek (2019)** — "Matched One-Pole Digital Shelving Filters" → shelves.
- **Massberg (2008)** — analog-matched shelving/low-pass (roadmap).
- **ITU-R BS.1770-4** — K-weighting + true-peak → Flush Match + meters.
- **RBJ Audio EQ Cookbook** — retained as a *measurement baseline* only.

### 2b. Expired analog hardware schematics (UA's "character" realm)

Universal Audio's entire business is modeling classic analog circuits. Those
circuits are described in **expired patents and public service manuals** — the
topology and component *values* are public domain. Modeling the *circuit* is
legitimate; UA's specific model of it is theirs.

For a future Flush **"Character" mode** (optional, not baked in — Flush's core
is transparent), the classic references to study are:

| Hardware | What it teaches | For |
|---|---|---|
| Pultec EQP-1A | Passive boost/attenuate shelves, transformer/tube saturation | "warm" low-end + air |
| API 550A | Proportional-Q bell filters (Q grows with gain) | musical midrange |
| Neve 1073 | Inductor-based bands + class-A stage saturation | vocal/character EQ |
| SSL 4000 E | The "glue" bus EQ + VCA comp | mixbus |
| 1176 / LA-2A / 33609 | Detector non-linearity + program-dependent release | compressor character |

These are the legitimate "data" behind the analog-sounding plugins.

### 2c. Clean-room measurement (the actionable one)

Run test signals *through* a reference plugin, record the output, and treat the
measured behavior as a **target** to match with independent code. The included
`parity_tools` harness does exactly this (see §4).

---

## 3. What to measure (and what each tells you)

| Measurement | Tool | What it proves |
|---|---|---|
| **Frequency response** (magnitude) | stepped sines + `measure` | Your EQ matches the reference's *tone shaping* |
| **Phase response** | null of two IRs, or sweep + analysis | Natural/Linear phase parity |
| **THD / harmonics** | `measure sine` (reports H2..H10) | Character: analog-modeled plugins have a THD "fingerprint" |
| **Null residual** | `null a.wav b.wav` | The ultimate "are we identical" test |
| **Latency** | impulse → first-nonzero sample | Delay compensation |

**The honest expectation:** a *transparent* EQ (Sonnox Oxford is clean; UA's are
clean-ish) can be matched almost exactly on **magnitude + phase** — that's a
solvable, physical target. **THD/"character"** is where you can get *close* but
not *identical*, because it depends on their exact non-linear component models.
Magnitude/phase parity ≈ "sounds transparently the same"; THD parity ≈ "same
character".

---

## 4. Workflow (the local agent runs this)

### 4a. Build the harness

```bash
cd plugins/Flush/tests
g++ -std=c++20 -O2 parity_tools.cpp -o parity_tools
```

### 4b. Generate stimuli

```bash
# A stepped-sine set for frequency response (the response sweep):
for f in 20 50 100 200 500 1000 2000 5000 10000 15000 20000; do
  ./parity_tools gen sine_$f.wav sine $f 1.0 0.5
done

# A log sweep (for IR/phase + subjective listening):
./parity_tools gen sweep.wav sweep 5.0

# Dual tone + impulse:
./parity_tools gen dual.wav dual 440 1000 2.0
./parity_tools gen impulse.wav impulse 1.0
```

### 4c. Capture the reference

1. Load **Sonnox Oxford EQ** / **UAD Cambridge** / **FabFilter Pro-Q 4** in a DAW
   (or a command-line host) at the same sample rate (48 kHz), 24-bit or 32-bit.
2. Bypass everything except ONE bell band (e.g. +6 dB, 1 kHz, Q 1) — or a shelf,
   or a cut — and set the phase mode you want to match.
3. Render each `sine_*.wav` through the plugin → `ref_*.wav`.
4. Also render the sweep and impulse.

### 4d. Capture Flush

Same steps, Flush loaded with the matching band + phase mode.

### 4e. Diff

```bash
for f in 20 50 100 200 500 1000 2000 5000 10000 15000 20000; do
  echo "== $f Hz =="
  ./parity_tools measure ref_$f.wav sine $f | grep fundamental
  ./parity_tools measure flush_$f.wav sine $f | grep fundamental
done

# Overall residual on a matched band:
./parity_tools null ref_1000.wav flush_1000.wav
```

**Iterate:** adjust Flush's filter design until the *measured* magnitude curve
matches the reference's, then listen A/B. That is the parity loop.

### 4f. THD / character comparison

```bash
./parity_tools measure ref_1000.wav sine 1000    # note the H2..H10 pattern
./parity_tools measure flush_1000.wav sine 1000
```

A clean digital EQ shows THD ≈ 0 (just the harness's ~0.03% leakage floor).
An analog-modeled plugin shows a distinct harmonic pattern — that pattern is the
"character" you'd optionally model (§2b), not something you chase for transparency.

---

## 5. Honest quality boundary (unchanged)

- Flush is a **transparent** EQ. Matching Sonnox Oxford's *clean* behavior is a
  real, achievable goal via magnitude/phase measurement. Matching a *specific
  analog character* (UAD) requires modeling the circuit (§2b) — a separate,
  optional feature, not the core.
- "Oxford-quality" is ultimately proven by **measure + listen + iterate**, not by
  a feature checklist. This document + `parity_tools.cpp` is the measurement half.
  The listening half needs ears and a running build — the local agent's job.
