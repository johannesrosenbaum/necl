# NECL Platz-1-Report

Generated: 2026-09-17 15:39:31

## Ratio (Suite)

| Corpus | NECL | Sprintz | heatshrink | lz4 | miniz | Best |
|--------|-----:|--------:|-----------:|----:|------:|------|
| `ticks` |  **24.5%** | — | 51.2% | 59.8% | 40.1% | `nec_lite` |
| `ticks_16k` |  **24.6%** | — | 51.8% | 63.8% | 39.8% | `nec_lite` |
| `timeseries_i16` |  **58.7%** | 58.7% | 104.2% | 100.4% | 76.0% | `nec_lite` |
| `nab_machine_temp.i16le` |  **59.5%** | 59.5% | 103.7% | 100.4% | 81.8% | `nec_lite` |
| `nab_ambient_temp.i16le` |  **58.9%** | 58.9% | 101.8% | 100.3% | 76.8% | `nec_lite` |
| `nab_ec2_cpu.i16le` | 19.1% | 30.7% | 14.5% | 43.4% |  **12.2%** | `miniz` |
| `melbourne_daily_min_temp.i16le` | 68.8% | 68.8% | 89.9% | 96.1% |  **67.7%** | `miniz` |
| `intel_lab_temp.i16le` |  **37.3%** | 48.4% | 59.0% | 67.5% | 44.1% | `nec_lite` |
| `ucr_Coffee.i16le` |  **74.5%** | 78.8% | 112.1% | 100.4% | 99.1% | `nec_lite` |
| `ucr_GunPoint.i16le` |  **63.3%** | 66.6% | 109.4% | 100.4% | 95.2% | `nec_lite` |
| `ucr_MoteStrain.i16le` |  **74.9%** | 75.9% | 109.3% | 99.7% | 91.6% | `nec_lite` |
| `ucr_ECG200.i16le` |  **85.8%** | 86.0% | 111.9% | 100.4% | 97.5% | `nec_lite` |
| `harness128` | 82.8% | 82.8% | 112.5% | 101.6% | 108.6% | `drh` |
| `random_16k` | 101.0% | — | 112.3% | 100.4% |  **100.1%** | `miniz` |

## Scorecard (Host-Suite `codec_ratio`)

- vs **heatshrink** (Markt-Peer): **13/14** besser
- vs **Sprintz-Delta** (Algo-Peer, Remis ±0,05 pp): **11/11** ≥

_Achtung: `nec_lite` in `codec_ratio` ist der **Host**-Build (Kompakt/LZ möglich)._

## Deploybar vs Host (Ablation)

| Corpus | Deploybar (`NO_MALLOC`) | Host choose-best | Δpp |
|--------|------------------------:|-----------------:|----:|
| `ticks` | **100.8%** | 24.5% | -76.3 |
| `timeseries_i16` | **59.5%** | 58.7% | -0.8 |
| `nab_machine_temp.i16le` | **60.4%** | 59.5% | -0.8 |
| `nab_ambient_temp.i16le` | **59.9%** | 58.9% | -1.0 |
| `nab_ec2_cpu.i16le` | **31.9%** | 19.1% | -12.8 |
| `intel_lab_temp.i16le` | **49.2%** | 37.3% | -11.9 |
| `joint_7axis_1khz_10s.i16le` | **32.3%** | 29.3% | -2.9 |

_deployable = Stream Bitpack/Zero/STORE under NEC_LITE_NO_MALLOC; host = FE_I16 choose-best (may use 0xCE/0xCF/0xCB or stream+RC)._

> **Hinweis:** `ticks` expandiert unter `NO_MALLOC`, weil Bitpack nur für int16 greift und Nibble-RC auf dem MCU abgeschaltet ist — Host ~20 %. Deploybares Tick-Produkt braucht eigenen Pfad (nicht im Ambient-ROI).

## ROI-Skizze (Vertical Temperaturfühler)

- 500 Fühler, Sample/300s, Batch 12 → TX alle 60 min, deploybar 59.9% Rest → ca. 5620 € gespart über 5 J (Daten + Batterie-Service, Modell).
- Details: `build/mcu/roi-sensor.md` (Ratio 59.86%, Quelle ablation-deployable.json (NO_MALLOC))

## Flash / RAM (Cortex-M)

### cortex-m3

| Codec | Flash (.text) | Library-State |
|-------|--------------:|--------------:|
| nec_lite | 4920 | 2568 |
| heatshrink | 2168 | 1856 |
| lz4 | 4308 | 0 |
| miniz | 4116 | 0 |

### cortex-m0

| Codec | Flash (.text) | Library-State |
|-------|--------------:|--------------:|
| nec_lite | 5384 | 2568 |
| heatshrink | 2236 | 1856 |
| lz4 | 6488 | 0 |
| miniz | 4828 | 0 |

## Host-Timing-Proxy

_Median Enc/Dec auf `timeseries_16k` (Host-CPU, kein MCU-Zyklus)._

| Codec | Enc MB/s | Dec MB/s | coded |
|-------|---------:|---------:|------:|
| `nec_lite` | 558.5 | 478.4 | 9627 |
| `heatshrink` | 15.3 | 94.1 | 17060 |
| `lz4` | 9492.7 | 72004.6 | 16450 |
| `sprintz_d` | 844.7 | 562.0 | 9627 |

- **tsz:** `skipped_no_cargo`

## QEMU MCU-Bench (Cortex-M3)

- ok=1 n=2048 coded=652 cy_enc=0 cy_dec=0
- _DWT cy_* oft 0 unter QEMU; Roundtrip ok=1 ist der echte MCU-Smoke-Test_

## Peers außerhalb der C-Harness

- **tsz** (Rust, MIT/Apache): Cortex-M / BLE-Paket-Bitpack — separat evaluieren,
  nicht als C-Vendor in `codec_ratio` verdrahtet (braucht `cargo`).

Siehe [docs/PLATZ1.md](../docs/PLATZ1.md).

