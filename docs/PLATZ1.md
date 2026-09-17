# NECL Platz-1-Suite — IoT-Telemetrie-Stream

## Claim (was „Platz 1“ heißt)

Beste **Ratio unter Hard Caps** auf der Suite unten:

| Cap | Wert |
|-----|------|
| Library-Heap | **0** |
| Stream-Scratch | **0** (`scratch_bound`) |
| Flash (Cortex-M3 `.text`, freestanding) | Ziel ≤ ~5 KB |
| Flash (Cortex-M0 `.text`) | Ziel ≤ ~5,5 KB |
| API | Chunk-Stream + Block |

Nicht: beste Ratio auf Canterbury/JSON gegen zstd.

**Zwei Builds, zwei Claims:**

| Label | Build | Was zählt |
|-------|--------|-----------|
| **Deploybar** | `-DNEC_LITE_NO_MALLOC` | Sensor-MCU: Stream Bitpack/Zero/STORE |
| **Host** | ohne `NO_MALLOC` | Gateway: Choose-best inkl. `0xCE`/`0xCB`/RC |

Verkaufs- und ROI-Zahlen fürs Board = **nur Deploybar**.

## Nische (Praxis)

Batteriebetriebener MCU-Sensor mit **langsam driftenden int16-Werten** oder **Event-Ticks**
(Gebäude-/Maschinen-Temperatur, nicht CPU-Last-Plateaus). Geld sitzt in **Funkbytes und Batterie-Service**.

Repro ROI: `bash scripts/ablation_deployable.sh && python3 scripts/roi_sensor.py`

## Offizielle Corpora

| ID | Inhalt | Frontend |
|----|--------|----------|
| `ticks` / `ticks_16k` | u32+i32 Event-Ticks | `TICK8` |
| `timeseries_i16` | synthetische AR(1) int16 | `I16` |
| `nab_*` / `melbourne_*` / `intel_lab_temp` | Sensor int16 LE | `I16` |
| `ucr_*` | UCR-Archive → int16 | `I16` |
| `harness128` | kleines FE_I16-Harness | `I16` |
| `random_16k` | Negativkontrolle | none/store |

Repro:

```bash
bash scripts/fetch_sensor_data.sh   # optional, Netz
bash scripts/codec_ratio.sh         # → build/mcu/ratio.json (Host)
bash scripts/ablation_deployable.sh # → deploybar vs Host
python3 scripts/roi_sensor.py       # → build/mcu/roi-sensor.md
bash scripts/mcu_build.sh           # → build/mcu/mcu-size.json
python3 scripts/platz1_report.py    # → build/mcu/platz1-report.md
```

## Gegner (Rollen)

| Peer | Rolle |
|------|--------|
| **heatshrink** | Markt-/Flash-Peer (MCU, Stream) |
| **Sprintz-Delta** (skalar, Paper-Alg. 1) | Algorithmus-Peer für glatte int16 |
| **lz4 / miniz** | generische Block-Codecs (Flash-Vergleich) |
| **tsz** (Rust) | Nischen-Peer BLE/Cortex-M — *nicht* in der C-Harness; separat evaluieren |
| Roh / nur Delta | „kein Codec“-Baseline |

Sprintz ist **nicht** der alleinige Produktgegner; heatshrink ist der Flash-Konkurrent, den Einkäufer vergleichen.

## Aktueller Stand

- **Deploybar (`NO_MALLOC`):** NAB machine/ambient ~**60 %**, EC2 ~**32 %**, intel ~**49 %**  
  (`bash scripts/ablation_deployable.sh`)
- **Host-Suite:** vs heatshrink 13/14, vs Sprintz 11/11 — enthält Kompakt/LZ, nicht Board
- **Flash M3/M0:** ~4,9 / ~5,4 KB  
- **ROI-Skizze:** `build/mcu/roi-sensor.md`

Voll-Repro: `bash scripts/platz1_bench.sh`

## Was NEC2 hier tut

NEC2 (Mojo-Host) bleibt **Labor**. Gewinner (FIRE, Bitpack, Peak-Ideen) wandern als C nach NECL — nicht 16-MiB-N-Gramm auf M0.

## Nächste Produkt-Schritte

1. ROI-Annahmen mit einem Pilotkunden kalibrieren (€/MB, Service, TX-Energieanteil)  
2. Board-µJ (QEMU `cy_*` ist Proxy; DWT auf echt-M3)  
3. Device-Pfad EC2/intel: schlankes Plateau/RC nur wenn Flash-ROI stimmt  
4. tsz-Vergleich sobald `cargo` verfügbar  
5. Geräteprofil-`weight_id` / FE-Presets in der Gateway-CLI  
