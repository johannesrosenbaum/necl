# NECL published benches

Snapshot generated `2026-09-17 15:39:43 +0200`.

| File | Content |
|------|---------|
| [SUMMARY.md](SUMMARY.md) | Human-readable scorecard |
| `ablation-deployable.json` | Deployable (`NO_MALLOC`) vs host |
| `ratio.json` | Full peer suite (host `nec_lite`) |
| `mcu-size.json` | Cortex-M flash |
| `host-timing.json` | Host MB/s proxy |
| `platz1-report.md` | Full Platz-1 report |
| `roi-sensor.md` | Example ROI (ambient vertical) |

**Claim rule:** Board/SDK numbers = **Deployable** column only.

Regenerate: `bash scripts/publish_bench.sh`
