# NECL Bench Summary (publishable)

Generated: `2026-09-17 15:39:43 +0200`

## Rules (read first)

| Label | Meaning | Use in claims? |
|-------|---------|----------------|
| **Deployable** | `-DNEC_LITE_NO_MALLOC` — Stream Bitpack/Zero/STORE | **Yes — board / SDK** |
| **Host** | Full choose-best (`0xCE`/`0xCF`/`0xCB`, stream+RC) | Gateway only — label clearly |

Ratio = coded/orig × 100 (lower is better). All rows roundtrip-verified.

## Deployable vs Host

| Corpus | Deployable | Host | Δpp | Notes |
|--------|----------:|-----:|----:|-------|
| `ticks` | **100.8%** | 24.5% | -76.3 | TICK8: deployable expands (no RC on MCU) |
| `timeseries_i16` | **59.5%** | 58.7% | -0.8 | smooth AR(1) int16 |
| `nab_machine_temp.i16le` | **60.4%** | 59.5% | -0.8 | vertical: machine temp |
| `nab_ambient_temp.i16le` | **59.9%** | 58.9% | -1.0 | vertical: ambient temp / ROI |
| `nab_ec2_cpu.i16le` | **31.9%** | 19.1% | -12.8 | plateaus — LZ peer territory |
| `intel_lab_temp.i16le` | **49.2%** | 37.3% | -11.9 | long series; host uses stream+RC |
| `joint_7axis_1khz_10s.i16le` | **32.3%** | 29.3% | -2.9 | synthetic 7-axis @ 1 kHz, planar layout (robotics proxy) |

## Peers on focus corpora (Host harness `codec_ratio`)

_Peer table uses the **host** `nec_lite` binary (may include compact/LZ). Compare deployable column above for board claims._

| Corpus | NECL host | heatshrink | Sprintz | lz4 | miniz |
|--------|----------:|-----------:|--------:|----:|------:|
| `ticks` | 24.5% | 51.2% | — | 59.8% | 40.1% |
| `timeseries_i16` | 58.7% | 104.2% | 58.7% | 100.4% | 76.0% |
| `nab_machine_temp.i16le` | 59.5% | 103.7% | 59.5% | 100.4% | 81.8% |
| `nab_ambient_temp.i16le` | 58.9% | 101.8% | 58.9% | 100.3% | 76.8% |
| `nab_ec2_cpu.i16le` | 19.1% | 14.5% | 30.7% | 43.4% | 12.2% |
| `intel_lab_temp.i16le` | 37.3% | 59.0% | 48.4% | 67.5% | 44.1% |
| `joint_7axis_1khz_10s.i16le` | 29.3% | 103.0% | 31.5% | 74.0% | 55.4% |

## Flash (freestanding `.text`)

| Target | nec_lite | heatshrink | lz4 | miniz |
|--------|--------:|-----------:|----:|------:|
| cortex-m3 | 4920 | 2168 | 4308 | 4116 |
| cortex-m0 | 5384 | 2236 | 6488 | 4828 |

Library heap: **0**. `scratch_bound(n)`: **0**.

## Host timing proxy (`timeseries_16k`)

| Codec | Enc MB/s | Dec MB/s |
|-------|---------:|---------:|
| `nec_lite` | 558.5 | 478.4 |
| `heatshrink` | 15.3 | 94.1 |
| `lz4` | 9492.7 | 72004.6 |
| `sprintz_d` | 844.7 | 562.0 |

_Host x86_64 proxy — **not** MCU cycles / µJ._

## Robotics proxy — payload model (not bus %)

Corpus `joint_7axis_1khz_10s.i16le`: deployable **32.3%**.

- Raw payload @ 1 kHz × 7 × int16: **14000 B/s**
- NECL deployable body (same rate): **~4519 B/s**
- Body savings: **~68%** of sample bytes

CAN/EtherCAT frame headers, PDO slots and worst-case STORE still apply. Do **not** quote this as “bus load −X%” without a schedule model.

## Reproduce

```bash
bash scripts/publish_bench.sh
# → bench/published/
```

Methodology: [`docs/BENCH.md`](../../docs/BENCH.md).

