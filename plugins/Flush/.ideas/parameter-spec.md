# Flush — Parameter Specification (v2)

> Pro-Q4-class dynamic EQ + Flush Match gain-staging + glue compressor.
> Band model mirrors Pro-Q4: **up to 24 bands**, every band can be dynamic.

## Section 1: EQ — 24-band dynamic (the star)

### 1.1 Global EQ

| ID | Name | Type | Range | Default | Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `eq_enabled` | EQ On/Off | Bool | off / on | on | — | Master EQ bypass |
| `eq_phase_mode` | Phase Mode | Choice | Low Latency / Natural / Linear | Low Latency | — | Zero-latency biquads / matched min-phase / linear-phase FIR (latency reported to host) |
| `eq_oversample` | Oversampling | Choice | Off / 2x / 4x | Off | — | For "Oxford-grade" top-end; adds latency in the oversampled band |
| `eq_scale` | Display Range | Choice | 3 / 6 / 12 / 30 | 12 | dB | Pro-Q mastering (3/6) vs mixing (12/30) ranges |
| `eq_gain_q_link` | Gain-Q Link | Bool | off / on | off | — | Analog-style gain↔Q interaction |
| `eq_phase_invert` | Phase Invert | Bool | off / on | off | — | Output polarity flip |
| `eq_piano` | Piano Display | Bool | off / on | off | — | Frequency scale snaps to musical notes |
| `eq_band_count` | Band Count | Int | 1 … 24 | 8 | — | Number of active band slots (managed) |

### 1.2 Band model (each band, 1…24)

Bands live in the processor state (a `ValueTree`), so preset save/load captures the full
24-band definition. Host automation exposes the per-band subset below, indexed `Band N`.

| Field | Name | Type | Range | Default | Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `enabled` | Band On | Bool | off / on | on | — | |
| `type` | Shape | Choice | Bell / Notch / Low Shelf / High Shelf / Low Cut / High Cut / Band Pass / Tilt Shelf / Flat Tilt / All Pass | Bell | — | Pro-Q4 shape set |
| `freq` | Frequency | Float | 20 … 20000 | 1000 | Hz | Log |
| `gain` | Gain | Float | −30 … +30 | 0.0 | dB | |
| `q` | Q | Float | 0.025 … 40 | 1.0 | — | Bandwidth / shelf slope parameter |
| `slope` | Slope | Choice | 12 / 24 / 36 / 48 | 12 | dB/oct | Cut filter slope (12-48, every node) |
| `dynamic` | Dynamic | Bool | off / on | off | — | Makes the band a dynamic-EQ band |
| `dyn_mode` | Dyn Mode | Choice | Compress / Expand | Compress | — | |
| `dyn_detector` | Dyn Detector | Choice | Peak / RMS | Peak | — | Shared-engine detector |
| `dyn_threshold` | Dyn Threshold | Float | −60 … 0 | −24 | dB | |
| `dyn_range` | Dyn Range | Float | 0 … 30 | 12 | dB | Max gain change the dynamic band applies |
| `dyn_attack` | Dyn Attack | Float | 0.1 … 1000 | 10 | ms | Log |
| `dyn_release` | Dyn Release | Float | 10 … 5000 | 150 | ms | Log |
| `dyn_adaptive` | Dyn Adaptive Release | Bool | off / on | on | — | Oxford-style program-dependent release (heavy GR hangs, light GR lets go) |
| `placement` | Channel | Choice | Stereo / L / R / Mid / Side | Stereo | — | Per-band M/S or L/R processing |
| `solo` | Solo | Bool | off / on | off | — | Intelligent band solo |

Default band set (8): Low Cut (off) · Low Shelf 120 Hz · Bell 300 Hz · Bell 1 kHz ·
Bell 3 kHz · Bell 8 kHz · High Shelf 12 kHz · High Cut (off).

## Section 2: Flush Match — gain staging hero

| ID | Name | Type | Range | Default | Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `input_gain` | Input Trim | Float | −24 … +24 | 0.0 | dB | |
| `flush_mode` | Flush Match | Choice | Off / Match Input / Target | Match Input | — | |
| `flush_speed` | Match Timing | Choice | Slow (Bus) / Medium / Fast | Slow (Bus) | — | Bus-safe default |
| `flush_reference` | Loudness Reference | Choice | Mid (mono-sum) / Stereo (Mid+Side) | Mid (mono-sum) | — | |
| `match_target` | Target Level | Float | −30 … 0 | −18 | dB | |
| `output_gain` | Output Trim | Float | −24 … +24 | 0.0 | dB | |

## Section 3: Compressor (broadband instance of the shared Dynamics Engine)

| ID | Name | Type | Range | Default | Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `comp_enabled` | Comp On/Off | Bool | off / on | on | — | |
| `comp_threshold` | Threshold | Float | −60 … 0 | −18 | dB | |
| `comp_ratio` | Ratio | Float | 1.0 … 20.0 | 4.0 | :1 | Log |
| `comp_attack` | Attack | Float | 0.1 … 100 | 10.0 | ms | Log |
| `comp_release` | Release | Float | 10 … 2000 | 150 | ms | Log |
| `comp_detector` | Detector | Choice | Peak / RMS | RMS | — | Shared-engine detector |
| `comp_adaptive` | Adaptive Release | Bool | off / on | on | — | Oxford-style transparent release (the "chef's kiss") |
| `comp_makeup` | Makeup | Float | −24 … +24 | 0.0 | dB | |
| `comp_auto_makeup` | Auto Makeup | Bool | off / on | on | — | |

## Section 4: Preset Browser

| ID | Name | Type | Range | Default | Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `preset_index` | Preset | Choice | (factory + user list) | 0 | — | Current preset; name shown in the browser |
| `preset_ab` | A/B Compare | Choice | A / B | A | — | Two-state compare with copy |

Factory categories: Clean Up · Bass · Drums · Vocals · Rap/RnB · Mastering · FX.
User presets are saved to the OS user-presets directory. (Preset payloads are stored in
plugin state so every band, dynamic setting, and Flush Match mode is captured.)

## Section 5: Display / Settings

| ID | Name | Type | Range | Default | Unit | Notes |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `meter_mode` | Meter Style | Choice | Bar (Vertical) / VU (Needle) | Bar (Vertical) | — | I/O meters only |
| `analyzer_on` | Analyzer | Bool | off / on | on | — | |
| `analyzer_speed` | Analyzer Speed | Choice | Slow / Medium / Fast | Medium | — | Averaging |
| `analyzer_range` | Analyzer Range | Choice | 60 / 90 / 120 | 90 | dB | Vertical range |
| `analyzer_tilt` | Analyzer Tilt | Float | 0 … 6 | 4.5 | dB/oct | Pink-noise tilt |
| `analyzer_freeze` | Analyzer Freeze | Bool | off / on | off | — | Peak-hold freeze |
| `ui_scale` | UI Scale | Float | 0.5 … 2.0 | 1.0 | × | Settings-modal control (Pro-Q4 style) |
| `fps_mode` | UI Refresh | Choice | 60 / 120 / Uncapped | Uncapped | — | Render loop target; uncapped ≈ host-limited, aimed ≥120 Hz |
| `theme_accent` | Accent | Choice | Blue / Cyan / Orange / Violet | Blue | — | Neon glow accent |

## Metering (computed — not user parameters)

| ID | Description | Unit |
| :--- | :--- | :--- |
| `meter_in_peak` | Input peak | dBFS |
| `meter_in_rms` | Input RMS | dBFS |
| `meter_out_peak` | Output peak | dBFS |
| `meter_out_rms` | Output RMS | dBFS |
| `meter_gr` | Compressor gain reduction | dB |
| `meter_flush_delta` | Auto gain Flush is applying | dB |
| `meter_mid` | Mid (L+R) level | dBFS |
| `meter_side` | Side (L−R) level | dBFS |
| `meter_correlation` | Phase correlation (−1 … +1) | — |
| `meter_latency` | Current reported latency | ms |

> Spectrum analyzer magnitudes are pushed to the UI over a lock-free buffer (display-only).

> **Automation surface:** global + per-band core controls (Freq/Gain/Q/On/Dynamic + dynamic
> params) for 24 bands + compressor + Flush Match + display. The complete 24-band definition
> (shapes, slopes, placement, solo) lives in state so presets capture everything.
