# NECL (`nec_lite`)

Freestanding **C** codec for small MCUs (Cortex-M0/M3): delta frontend + adaptive
nibble range coder, stream + block APIs, **no malloc in the library**.

Peer in the flash segment is **[heatshrink](https://github.com/atomicobject/heatshrink)** —
not lz4/zstd. NECL is an honest **MCU byte container** for int16 / tick-like
streams, not a claim to beat Sprintz or general LZ on every corpus.

| | |
|---|---|
| Language | C11, freestanding-friendly |
| `weight_id` | `NECL` (`0x4E45434C`) |
| Library heap | 0 |
| License | MIT |

## What you get

- `nec_lite` encoder/decoder + delta frontend
- Stream API (`nec_lite_enc_push` / finish) and block API
- Host tests + `nec-gateway` CLI
- Ratio harness vs heatshrink / lz4 / miniz / DRH / Sprintz-delta (scalar)
- MCU size scripts (fetches xpack `arm-none-eabi-gcc` locally, no sudo)
- Technical datasheet: [`docs/NECL_Technical_Datasheet.html`](docs/NECL_Technical_Datasheet.html)

## What this is not

- **Not NEC2** (host hybrid / Mojo) — separate product
- Not a lossless Sprintz replacement on UCR / smooth AR temperature series
- Flash is in the same ballpark as heatshrink; heatshrink often wins ratio and
  constant window-RAM tradeoffs

## Quick start (host)

```bash
# Roundtrip tests + gateway binary
bash scripts/test_lite.sh

# Optional: download public sensor corpora, then ratio table
bash scripts/fetch_sensor_data.sh   # network
bash scripts/codec_ratio.sh
```

Minimal embed:

```c
#include "nec_lite.h"

size_t out = nec_lite_compress_bound(n);
nec_lite_compress(src, n, dst, &out, NULL, 0, NEC_LITE_FE_I16);
```

Stream sketch:

```c
nec_lite_enc_t enc;
nec_lite_enc_init(&enc, NEC_LITE_FE_I16, sink, ctx);
nec_lite_enc_push(&enc, samples, n);
nec_lite_enc_finish(&enc);
```

## MCU flash compare

```bash
# Needs curl; downloads xpack toolchain into build/toolchains/ if missing
bash scripts/mcu_build.sh
# MCU_CPU=cortex-m0 bash scripts/mcu_build.sh
```

Results land in `build/mcu-size.json`. Details and caveats:
[`docs/NECL_VS_NEC2.md`](docs/NECL_VS_NEC2.md).

## Layout

```
include/nec_lite.h     public API
native/                delta frontends
lite/                  codec + gateway + ratio bench helpers
mcu/                   freestanding harness + vendor peers
scripts/               host + MCU tooling
docs/                  datasheet + notes
tests/                 unit/roundtrip tests
```

Vendor peers (bench only): heatshrink (ISC), lz4 (BSD-2), miniz (MIT) —
see `mcu/vendor/ORIGIN.txt`.

## License

MIT — see [LICENSE](LICENSE).
