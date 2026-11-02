# JC1060 first-mount SD cold-power experiment

Default: **OFF**, pending Kayrosen's exact-image hardware acceptance. This is a
selective adaptation of donor commits `981225e5` and `1e288dbd`, not a claim that
the donor's reported 11/11 mounts qualify the canonical image. JC1060 Wi-Fi
policy is unchanged and remains a separate feature decision.

The 2026-11-03 completion program prepares this experiment for the local
operator's JC1060/FLX4 bench. Ordinary firmware retains OFF. For a clean
experiment build, `tools/package_ota_release.ps1 -Project main-deck-jc1060
-BuildName <experiment-build> -SdColdPowerCandidate` requires the enabled flag,
runs the explicit board verifier and emits a separate `-sd-cold-power` package
directory with a signed `experimental_sd_cold_power` manifest field. The public
channel generator rejects this class. This is local test delivery, not hardware
acceptance or a change to ordinary defaults. Optional JC1060 Wi-Fi is now a
separate default-OFF capability; original donor observations remain historical.

The single startup call to `bsp_sd_init()` owns SDMMC slot 0, LDO4 and
GPIO43/44/39/40/41/42. ESP-Hosted owns slot 1 when enabled. The experiment runs
before FATFS initializes slot 0, never from runtime recovery, and never resets
or deinitializes the shared SDMMC host. Preserve that sole-owner startup
contract; the helper is not a thread-safe public hotplug API. Rail sharing or
another slot-0 owner on a board revision invalidates this experiment.

Sequence: acquire the rail, check release, clamp all six pins low, wait 150 ms,
restore all pins, reacquire the rail, wait 150 ms, then use the existing mount
and retry logic. Every fallible operation is checked. A release failure retains
its handle and prevents pin writes. A partial GPIO failure still restores every
pin. Any cleanup failure prevents power-on and mount. Results are latched for
the boot; a retry cannot repeat the cold-power operation. USB playback stays
available if `/sd` is unavailable, matching the existing BSP failure policy.

Build with IDF6.0.2 and a **separate build directory and fresh sdkconfig**:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py -C firmware/main-deck-jc1060 -B "$PWD/.cache/jc1060-sd-power/build" `
  -DSDKCONFIG="$PWD/.cache/jc1060-sd-power/sdkconfig" `
  -DPAJONIIIR_SD_COLD_POWER_EXPERIMENT=ON build
python tools/check_board_build.py --project main-deck-jc1060 `
  --build .cache/jc1060-sd-power/build --experimental-sd-power
```

CMake rejects a config/experiment mismatch. The verifier also requires explicit
experiment admission, and rejects the option on M3/JC4880. No dependency lock,
production sdkconfig default, release, OTA or installed image is changed.

Software checks execute the production sequence with callback fault injection.
Required future physical coverage: identify rail/pin wiring; cold and warm
resets with the same card; mount/read/write after each reset; Hosted/Wi-Fi and
USB service continuity; failure behavior with no card. Keep original donor
records separate from those future canonical results.
