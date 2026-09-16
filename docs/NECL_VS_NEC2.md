# NECL vs NEC2

Zwei Container, zwei Binaries, kein Mix. `weight_id` entscheidet.

Datenblatt Rev 1.1: [`NECL_Technical_Datasheet.html`](NECL_Technical_Datasheet.html).

| | **NECL** (`libnec_lite`) | **NEC2** (`libnec.so` / Mojo) |
|---|---|---|
| Magic | `NEC1` | `NEC1` |
| `weight_id` | `0x4E45434C` (`NECL`) | `0x4E454332` (`NEC2`) |
| Encoder | C, Delta+Nibble-RC, FLAG_STREAM | Mojo Hybrid (N-Gramm + INT8 + Arithmetic) |
| Decoder | dieselbe C-Library | Mojo / `nec_decompress_buffer` |
| Heap in der Library | **0** | Session ≈ **16 MiB** N-Gramm |
| Ausgabe | **n + 32 + 2·⌈n/256⌉ + 16** | hart **n+32** (STORE-Bypass) |
| Runtime | keine (freestanding) | Mojo (`libKGEN*`, AsyncRT) |
| RAM-Modell | **Stream:** `scratch_bound=0`, `enc_t` 1384 B + `dec_t` 1176 B (M3) | Session-Tabellen |
| Ziel | MCU / Gateway / RTOS | Linux-Host, Prototyp, schwere Payloads |
| Cross-Decode | NEC2-Decoder **lehnt NECL ab** | NECL-Decoder **lehnt NEC2 ab** |

## Cortex-M, gemessen (inkl. heatshrink)

`arm-none-eabi-gcc` 14.2.1, `-Os -mthumb -ffreestanding`. Flash = `size -A` `.text` (kein libc). heatshrink v0.4.1, `W=8 L=4`. nec_lite: Stream-Harness n=2048; Peers weiter 128-Byte-Block.

**Cortex-M3** Flash / Library-BSS

- nec_lite: **4032** / **2560** (enc 1384 + dec 1176)
- heatshrink enc+dec: **2168** / 1856 (enc 1554 + dec 302)
- miniz 3.0.2: **4116** / 0
- lz4 1.10: **4308** / 0

**Cortex-M0**

- nec_lite: **4652** / 2560
- heatshrink: **2236** / 1856
- miniz: **4828** / 0
- lz4: **6488** / 0

Repro: `bash nec-core/scripts/mcu_build.sh`  
M0: `MCU_CPU=cortex-m0 bash nec-core/scripts/mcu_build.sh`  
Ratio: `bash nec-core/scripts/codec_ratio.sh`  
QEMU: `bash nec-core/scripts/mcu_qemu.sh`  
Flash: `bash nec-core/scripts/mcu_flash.sh`

## RAM: scratch_bound = 0

nec_lite **streamt**. Chunks sind 256 Byte, self-describing (`FLAG_STREAM`), Delta-Zustand über Chunk-Grenzen. Der Caller stellt `nec_lite_enc_t` / `nec_lite_dec_t` (M3: 1384+1176 B). `scratch` darf NULL sein. Harness-Puffer `src`/`coded` skalieren weiter mit dem Test, die Library nicht.

heatshrink **streamt** mit kleinerem Flash (2168 vs 4032) und 1856 B Fenster-BSS.

lz4/miniz in dieser Harness bleiben Block-APIs.

## Ratio (Host, Roundtrip)

`sprintz_d` = Sprintz-Delta Paper-Alg. 1 skalar (kein FIRE, kein Huffman). DRH = Delta+PackBits+statisches Huffman. Beide nur auf int16. UCR: TRAIN+TEST, min/max→int16.

| Corpus | nec_lite | sprintz_d | drh | heatshrink | lz4 | miniz |
|---|---:|---:|---:|---:|---:|---:|
| harness128 (FE_I16) | 35,2% | 82,8% | **11,7%** | 112,5% | 101,6% | 108,6% |
| timeseries_i16 160 KB | 71,4% | **58,7%** | 74,9% | 104,2% | 100,4% | 76,0% |
| ticks 144 KB | **24,5%** | — | — | 51,2% | 59,8% | 40,1% |
| logline_16k | 59,2% | — | — | 10,3% | **0,6%** | **0,6%** |
| random_16k | 101,0% | — | — | 112,3% | 100,4% | **100,1%** |
| NAB machine temp | 73,1% | **59,5%** | 75,7% | 103,7% | 100,4% | 81,8% |
| NAB ambient temp | 72,7% | **58,9%** | 74,9% | 101,8% | 100,3% | 76,8% |
| NAB EC2 CPU | 24,1% | 30,7% | 47,8% | 14,5% | 43,4% | **12,2%** |
| Melbourne daily min | 73,8% | 68,8% | 80,2% | 89,9% | 96,1% | **67,7%** |
| Intel Berkeley temp 800 KB | **40,0%** | 48,4% | 57,8% | 59,0% | 67,5% | 44,1% |
| UCR ECG200 | 98,5% | **86,0%** | 93,5% | 111,9% | 100,4% | 97,5% |
| UCR Coffee | 93,7% | **78,8%** | 88,2% | 112,1% | 100,4% | 99,1% |
| UCR GunPoint | 81,7% | **66,6%** | 79,2% | 109,4% | 100,4% | 95,2% |
| UCR MoteStrain | 86,5% | **75,9%** | 82,8% | 109,3% | 99,7% | 91,6% |
| UCR ItalyPowerDemand | 100,6% | **96,4%** | 99,1% | 108,0% | 100,4% | 98,0% |

## Was das nicht sagt

- Peer im Segment bleibt **heatshrink** (Flash). Gegen **Sprintz-Delta** verliert NECL UCR und AR/NAB-Temperatur; NECL bleibt vorne auf Intel-Berkeley-Temp und Ticks.
- Logzeilen und glatte CPU-Sägezähne bleiben LZ.
- STORE nach fehlgeschlagenem RC hat das Nibble-Modell doppelt aktualisiert (UCR-Roundtrip tot). Encoder rollt das Modell jetzt zurück.
- QEMU Cortex-M3: 2048→694, Roundtrip ok. DWT nicht emuliert. **Kein Board am USB** — Timing/Energie auf Silizium offen.
- Host-Proxy derselben 2048-Byte-Sequenz: ~395 µs encode / ~408 µs decode (x86_64 -O2).

## Aufruf

```c
/* Konstantes Arbeitsset, IO in beliebigen Scheiben */
nec_lite_enc_t enc;
nec_lite_enc_init(&enc, NEC_LITE_FE_I16, sink, ctx);
nec_lite_enc_push(&enc, samples, n);
nec_lite_enc_finish(&enc);

/* Block-API: scratch darf NULL sein */
size_t out = nec_lite_compress_bound(n);
nec_lite_compress(src, n, dst, &out, NULL, 0, NEC_LITE_FE_I16);
```

Gateway: `nec-gateway compress --frontend i16|tick8 --input in.bin --output out.nec`
