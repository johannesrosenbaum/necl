# NECL (`nec_lite`)

Freestanding **C** codec for small MCUs (Cortex-M0/M3): delta frontend + adaptive nibble range coder, optional stream API, **no malloc in the library**.

Peer in the flash segment is **[heatshrink](https://github.com/atomicobject/heatshrink)** — not lz4/zstd. NECL is an honest **MCU byte container** for int16 / tick-like streams, not a claim to beat Sprintz or general LZ on every corpus.

## What you get
- `nec_lite` encoder/decoder (C, freestanding-friendly)
- Stream + block APIs (`weight_id` `NECL`)
- Host ratio harness vs heatshrink / lz4 / miniz (and optional Sprintz-delta bench)
- Technical datasheet (HTML/PDF)

## What this is not
- Not NEC2 (host hybrid / Mojo — separate codebase)
- Not a lossless Sprintz replacement on UCR/smooth AR temperature
- Flash is in the same ballpark as heatshrink; heatshrink often wins ratio and window-RAM tradeoffs

## Quick start
(… Build-Befehle, die du nach dem Push anpasst …)

## License
MIT
