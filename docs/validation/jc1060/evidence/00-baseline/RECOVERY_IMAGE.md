# Known-good recovery image (fork-ref v343)

- Source: `~/Pajoniiir-fork-ref/firmware/main-deck-jc1060/build/` (fork-ref HEAD `92374f35`, uncommitted v341-v343 changes not verified)
- App: `pajoniiir_deck.bin`, version 343, built 2026-10-05 19:44:01, ESP-IDF v6.0.2, 3054704 bytes
- SHA-256: `8e875aac3e337dcc0bb273b136fd754b30f0dcaae05af8de8358394c7f7654e9`
- Matches the image running on the unit (UART: `running pajoniiir_deck 343 slot=ota_0 addr=0x20000 size=0x300000 state=valid`)
- Flash layout (`flash_args`): bootloader 0x2000, partition table 0x8000, ota_data 0x10000, app 0x20000; dio / 80m / 16MB
- Caveat: bootloader.bin (2026-09-23) and partition-table.bin (2026-09-25) are older than the app; the on-flash copies are what boot today.
- Not yet run: flash command / boot-mode entry for rollback (to confirm before 0.2).
