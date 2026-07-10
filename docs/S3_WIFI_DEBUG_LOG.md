# S3 Wi-Fi Debug Log

Use this when the XIAO ESP32S3 UART adapter is disconnected because the FLX4
headphones path is physically connected, so the same `P4_AUDIO_LINK` and
`FLX4_USB_AUDIO` counters can be captured without changing the audio path.

There are two options:

1. **Runtime S3 Debug AP (preferred)** — an on-demand SoftAP + live web log
   viewer toggled from the P4 Settings UI at runtime. No custom build required;
   the default S3 firmware ships it (`CONFIG_S3_DEBUG_AP_ENABLED=y`).
2. **Build-time UDP debug log** — a dedicated build that mirrors ESP logs to a
   UDP listener on your PC. Requires the Wi-Fi credentials baked into a local
   sdkconfig fragment (below).

## Runtime S3 Debug AP (Preferred)

The debug AP is OFF after every boot and is controlled entirely from the P4:

1. Flash the default S3 and P4 firmware (no special build flags needed).
2. On the P4, open **Settings** and enable the **S3 DEBUG AP** switch. The label
   tracks the S3 handshake: `OFF` -> `STARTING` -> `ON` (or `ERROR`).
3. Connect a phone/laptop to the open SoftAP **`PajoNiiiR-S3-DEBUG`**.
4. Open **`http://192.168.4.1`** for a read-only live log viewer (Server-Sent
   Events auto-reconnect). It shows the same aggregate `P4_AUDIO_LINK` and
   `FLX4_USB_AUDIO` counters described below.
5. Disable the switch when done; the AP and HTTP server stop.

The switch is not persisted in P4 NVS; P4 also sends OFF at boot so the AP never
lingers after a reboot. The web page is read-only and exposes no firmware
commands. See `docs/CONTROL_LINK_PROTOCOL.md` (`CTRL_ID_S3_DEBUG_AP`) for the
control-link handshake and status enum.

> The open AP has no authentication and consumes RAM/CPU/power while ON, so keep
> it a bench/debug-only tool. FLX4 MIDI, the control link, and P4-to-S3
> headphone audio keep running while it is active.

## Build-time UDP Debug Log (Alternative)

The S3 firmware mirrors normal ESP logs to UDP when
`CONFIG_WIFI_DEBUG_LOG_ENABLED=y`. This needs a dedicated build with Wi-Fi
credentials, so prefer the runtime AP above unless you specifically want the
logs on your PC terminal.

> [!WARNING]
> The build-time UDP log (`wifi_debug_log`, STA mode) and the runtime debug AP
> (`s3_debug_ap`, SoftAP mode) both own the single S3 Wi-Fi radio and are
> **mutually exclusive**. If `wifi_debug_log` is enabled it grabs the radio in
> STA at boot, and the runtime AP will fail to start. `CONFIG_WIFI_DEBUG_LOG_ENABLED`
> defaults to `n`; make sure a stale local `sdkconfig` has not left it `=y`
> (`# CONFIG_WIFI_DEBUG_LOG_ENABLED is not set`) before using the runtime AP.

## Local Config Fragment

Create this ignored local file:

```powershell
Copy-Item firmware\control-board-s3\sdkconfig.wifi_debug_example firmware\control-board-s3\sdkconfig.wifi_debug.local
notepad firmware\control-board-s3\sdkconfig.wifi_debug.local
```

Fill only the local file:

```ini
CONFIG_WIFI_DEBUG_LOG_ENABLED=y
CONFIG_WIFI_DEBUG_LOG_SSID="your-ssid"
CONFIG_WIFI_DEBUG_LOG_PASSWORD="your-password"
CONFIG_WIFI_DEBUG_LOG_HOST="your-pc-ipv4"
CONFIG_WIFI_DEBUG_LOG_PORT=3333
CONFIG_LOG_MAXIMUM_LEVEL_INFO=y
```

Do not commit `sdkconfig.wifi_debug.local`.

## PC Listener

Run this before booting or resetting the S3:

```powershell
@'
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", 3333))
print("listening on UDP 3333")
while True:
    data, addr = sock.recvfrom(4096)
    print(data.decode("utf-8", errors="replace"), end="")
'@ | python -
```

If Windows Firewall prompts for Python, allow private-network UDP traffic.

## Build And Flash

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -B build_wifi_debug `
  -D SDKCONFIG="build_wifi_debug/sdkconfig" `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.wifi_debug.local" `
  build
idf.py -B build_wifi_debug -p COM6 flash
```

Use the actual S3 flash port if it is no longer `COM6`.

## Expected Lines

P4-to-S3 audio-link health:

```text
P4_AUDIO_LINK rx blocks=... gaps=0 crc=0 ring=... underruns=... overruns=...
```

FLX4 USB Audio health:

```text
FLX4_USB_AUDIO tx submitted=... completed=... skipped=... underrun=... bytes=...
```

During normal FLX4 headphone playback, `rx blocks`, `submitted`, and
`completed` should keep increasing. `gaps`, `crc`, `skipped`, and FLX4 USB
`underrun` should stay at zero. `overruns` should stay flat during steady
playback and the ring should not stay pinned at its 4096-frame ceiling. Short
startup underruns are acceptable; sustained growth during audible dropouts, or
a steadily rising `overruns` counter while USB packets still complete, is a
failure to investigate as a producer/consumer rate or drain mismatch.
