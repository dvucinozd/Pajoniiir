# Shared P4 board policies

All three products link the same audio/deck/library/controller/UI/web/OTA modules.
`board_adapter` exposes capability and peripheral contracts. BSP selection is
checked at configure/link and by `tools/check_board_build.py` against the built
component graph and application descriptor. Dependency versions remain pinned.

| Policy | JC4880 | JC1060 | M3 |
| --- | --- | --- | --- |
| Entrypoint/project | main-deck-p4 | main-deck-jc1060 | main-deck-m3 |
| BSP | bsp_jc4880 | bsp_jc1060 | bsp_p4_m3 |
| Geometry | 800 x 480 | 1024 x 600 | 800 x 480 |
| Storage / controller root | 0 / 1 | 1 / 0 | 0 / 1 |
| Physical controller port | board USB1 | board USB0 | USB2 FS |
| Physical storage port | board USB0 | board USB1 | USB3 HS |
| MAIN / cue default | PCM5102A / FLX4 | controller USB / controller USB | PCM5102A / FLX4 |
| MAIN rate policy | existing source-rate policy | existing USB policy | fixed 48 kHz |
| FIR cache / dense correlation | off / off | off / off | on / on |
| Panel waveform policy | existing cadence/order | existing cadence/order | waveform first / top to bottom |
| Wi-Fi capability/default | available, default OFF | unavailable | available, default ON, WPA2 APSTA |
| Ethernet provider | unavailable | board Ethernet | disabled, pins reserved for I2S |
| Link service compiled | experiment only | yes, runtime default OFF | experiment only |
| Recorder / alternative UI | experiment only | experiment only | experiment only |

M3 keeps DSI506/DYL0023 1 lane / 800 Mbps, RGB888, 27.777 MHz,
HFP/HSW/HBP 59/2/45, VFP/VSW/VBP 109/2/22, burst sync pulses/no frame ACK,
native 0 degree PPA blit. FT5426 is 100 kHz at 0x38, swap_xy=0,
mirror_x=1, mirror_y=1; backlight remains I2C. PCM5102A uses GPIO1/2/3.
Microphone, NS4150 and Ethernet remain disabled. C6 reset/SDIO lifecycle and the
shared SDMMC resource are board-owned. USB1 is CH340C supply/flash/debug.

The M3 product simulator preserves 648 x 141 main and 392 x 45 full-track
waveforms; artwork fits within the title strip. Baselines cover all five zooms,
library hierarchy, memory/local/source cues, Settings and screensaver restoration.
Their rendering PASS does not establish physical VFP timing or touch acceptance.

Experiments use explicit CMake flags and fresh SDKCONFIG files:
`PAJONIIIR_STORAGE_EXPERIMENT=ON`, `PAJONIIIR_DJ_UI_PREVIEW=ON`,
`PAJONIIIR_LINK_EXPERIMENT=ON`. They are not ordinary OTA packages. M3 Link binds
STA only; it requires configuring STA and a separate network physical acceptance.
Recorder requires suitable mounted SD storage; availability is not assumed.

Resource budgets and persistent journals use shared production implementations:
two-slot integrity-checked retained audio-WDT/library-load records, bounded
service journal rotation, allocation-failure counters and self-sampled task stack
minima. Audio steady state never writes NVS. `/api/resources` distinguishes
internal/DMA/PSRAM heap classes; PSRAM is not a substitute for internal DMA margin.
