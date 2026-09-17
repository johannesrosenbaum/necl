# NECL (`nec_lite`)

Freestanding **C** codec for small MCUs (Cortex-M0/M3): delta / FIRE frontend,
stream bitpack (+ host choose-best), **no library malloc** on the device path.

Lab peers: **[heatshrink](https://github.com/atomicobject/heatshrink)**, Sprintz-delta
(scalar), lz4, miniz. NECL is an **MCU-oriented byte container** for smooth int16 /
tick-like streams — not a general LZ replacement.

| | |
|---|---|
| Language | C11, freestanding-friendly |
| `weight_id` | `NECL` (`0x4E45434C`) |
| Library heap | 0 (`NEC_LITE_NO_MALLOC`) |
| License | **MIT** |

## Two builds (important)

| Label | Flag | Use |
|-------|------|-----|
| **Deployable** | `-DNEC_LITE_NO_MALLOC` | Board/SDK claims |
| **Host** | default | Gateway choose-best (`0xCE`/`0xCF`/`0xCB`, stream+RC) |

Cite board numbers from the **Deployable** column only:
[`bench/published/SUMMARY.md`](bench/published/SUMMARY.md).

## What you get

- `nec_lite` encoder/decoder + delta/FIRE frontends
- Stream API (`nec_lite_enc_push` / finish) and block API
- Host tests + `nec-gateway` CLI
- Ratio / ablation / MCU size harnesses
- Published benches + methodology ([`docs/BENCH.md`](docs/BENCH.md))

## What this is not

- **Not NEC2** (Mojo host hybrid) — separate experiment
- Not “always better than heatshrink/miniz” (plateau corpora differ)
- Host compact/LZ ratios are **not** the flashed MCU path

## Quick start (host)

```bash
bash scripts/test_lite.sh

# Optional corpora + full publishable snapshot
bash scripts/fetch_sensor_data.sh   # network
bash scripts/publish_bench.sh       # → bench/published/
```

Minimal embed:

```c
#include "nec_lite.h"

size_t out = nec_lite_compress_bound(n);
nec_lite_compress(src, n, dst, &out, NULL, 0, NEC_LITE_FE_I16);
```

## MCU flash

```bash
bash scripts/mcu_build.sh
# MCU_CPU=cortex-m0 bash scripts/mcu_build.sh
```

Typical freestanding `.text` (see latest `bench/published/SUMMARY.md`): ~5 KB on
Cortex-M3. Toolchain is fetched into `build/toolchains/` (gitignored).

## Layout

```
include/nec_lite.h     public API
native/                delta frontends
lite/                  codec + gateway + benches
mcu/                   freestanding harness + vendor peers
bench/published/       frozen scorecard (regenerate with publish_bench.sh)
scripts/               host + MCU tooling
docs/                  BENCH, PLATZ1, datasheet
tests/                 roundtrip tests
```

Vendor peers (bench only): heatshrink (ISC), lz4 (BSD-2), miniz (MIT) —
see `mcu/vendor/ORIGIN.txt`.

## License

MIT — see [LICENSE](LICENSE).
