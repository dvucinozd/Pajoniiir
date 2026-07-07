# S3 Wi-Fi UDP Debug Log

Use this when the XIAO ESP32S3 UART adapter is disconnected because the FLX4
headphones path is physically connected. The S3 firmware already mirrors
normal ESP logs to UDP when `CONFIG_WIFI_DEBUG_LOG_ENABLED=y`, so the same
`P4_AUDIO_LINK` and `FLX4_USB_AUDIO` counters can be captured without changing
the audio path.

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
idf.py -B build_flx4_hp_e2e_xiao_wifi `
  -D SDKCONFIG="build_flx4_hp_e2e_xiao_wifi/sdkconfig" `
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.flx4_hp_e2e;sdkconfig.wifi_debug.local" `
  build
idf.py -B build_flx4_hp_e2e_xiao_wifi -p COM6 flash
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
`completed` should keep increasing. `gaps` and `crc` should stay at zero.
Short startup underruns are acceptable; sustained growth during audible
dropouts is a failure to investigate.
