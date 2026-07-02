# Implementacijski plan V3: S3 GPIO48 WLED status (usklađen s kodom)

Repozitorij: `https://github.com/dvucinozd/ESP32-DDJ-FLX4`

> **Napomena o ovoj verziji.** Ovo je ispravljena verzija plana V2, uparena sa
> stvarnim stanjem firmvera na `master` (2026-07-03). Sadržajno je isti cilj
> (GPIO48 kao servisni/dijagnostički RGB, bez playback statusa), ali su
> ispravljene tri netočne pretpostavke o projektu:
>
> 1. **USB MIDI veza se NE detektira TinyUSB device callbackovima.** S3 je USB
>    **host** za FLX4 preko ESP-IDF host stacka; `tud_mount_cb`/`tud_umount_cb`
>    se ne okidaju za FLX4. Koristi se postojeći connection-state u
>    `flx4_midi_host` (`publish_connection_state()`).
> 2. **GPIO48 kao adresabilni RGB je hardverska pretpostavka koju treba
>    potvrditi.** Ovo je custom panel ploča s diskretnim LED-icama na GPIO
>    33/34/38 (`panel_leds.c`), a ne goli DevKitC-1. Dodan je Korak 0.
> 3. **P4 link watchdog na S3 ne postoji** i uklapa se u postojeći RX put
>    (`handle_p4_frame()` u `control_link_uart.c`), ne u novu izmišljenu funkciju.

WLED **ne prikazuje** playback: track loaded, playing, paused, stopped,
cue/PFL, deck state, audio underrun kao playback.

WLED prikazuje: S3 boot/init, USB MIDI (FLX4 host) stanje, P4 UART link stanje,
korisničku input aktivnost, MIDI aktivnost, UART aktivnost, calibration mode,
warning/error/fatal.

---

## 0. Korak 0 — HARDVERSKA PROVJERA (obavezno prije koda)

Ova ploča **nije** goli ESP32-S3-DevKitC-1. Ima fizički panel s diskretnim
LED-icama:

```c
// firmware/control-board-s3/components/panel_io/panel_leds.c
#define PIN_LED_CUE  GPIO_NUM_33
#define PIN_LED_PLAY GPIO_NUM_34
#define PIN_LED_BEAT GPIO_NUM_38
```

Zauzeti GPIO na S3: 2–18, 21, 33, 34, 38, 39. **GPIO48 je slobodan u kodu**, ali
prije implementacije treba fizički potvrditi:

```text
[ ] Postoji li na ploči adresabilni RGB (WS2812/SK6812) LED?
[ ] Je li spojen baš na GPIO48 (a ne 38/48 kako neki DevKitC-1 klonovi rade)?
[ ] Ako NEMA adresabilnog RGB-a: plan se mijenja na obični GPIO + PWM RGB ili
    jednobojni LED, a `led_strip` se NE koristi.
```

Ako se GPIO48 adresabilni RGB ne potvrdi, ostatak plana (led_strip/RMT) ne vrijedi.
Sav GPIO48 broj ide kroz Kconfig (`CONFIG_STATUS_LED_GPIO`) da se lako promijeni.

---

## 1. Cilj

GPIO48 RGB = servisni indikator: je li S3 živ, radi li FLX4 USB veza, radi li
P4 UART link, ima li korisničke/MIDI/UART aktivnosti, je li kalibracija aktivna,
postoji li sistemska greška. Playback ostaje na displayu/panel LED-icama.

---

## 2. Status mapa

| Status | Boja / efekt | Značenje |
|---|---|---|
| Boot | bijeli kratki flash | S3 firmware se pokrenuo |
| Idle OK | slabo zeleno stalno | S3 radi normalno |
| FLX4 USB connected | plavo pulsiranje | FLX4 host veza aktivna |
| FLX4 USB disconnected | žuto sporo blinkanje | FLX4 nije spojen |
| P4 link OK | cijan stalno | UART veza prema P4 radi |
| P4 link lost | žuto sporo blinkanje | nema komunikacije s P4 |
| Control activity | kratki zeleni flash | tipka/enkoder/jog/pitch |
| MIDI activity | kratki plavi flash | MIDI event poslan FLX4-u |
| UART activity | kratki cijan flash | UART frame poslan/primljen |
| Calibration mode | ljubičasto pulsiranje | aktivna kalibracija |
| Warning | narančasto sporo blinkanje | uređaj može nastaviti |
| Error | crveno sporo blinkanje | greška traži pažnju |
| Fatal | crveno brzo blinkanje | kritična greška |

> "USB MIDI" iz V2 je preimenovan u "FLX4 USB" jer se odnosi na host vezu prema
> kontroleru, ne na TinyUSB device.

---

## 3. Prioriteti

```text
P0 FATAL
P1 ERROR
P2 WARNING
P3 P4 LINK LOST
P4 FLX4 USB DISCONNECTED
P5 CALIBRATION MODE
P6 ACTIVITY FLASH
P7 P4 LINK OK / FLX4 USB CONNECTED
P8 IDLE OK
```

Pravila: fatal se nikad ne pregazi flashom; error se ne pregazi MIDI/control
flashom; link-lost/disconnected imaju prednost nad idle; flash je kratak i vraća
se na bazni status; playback statusi ne postoje.

---

## 4. Komponenta `status_led`

```text
firmware/control-board-s3/components/status_led/
├── CMakeLists.txt
├── Kconfig
├── include/status_led.h
└── status_led.c
```

Koristi: GPIO48 (Kconfig), ESP-IDF `led_strip` (RMT backend), FreeRTOS queue+task.

---

## 5. `status_led.h`

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_LED_BOOT = 0,
    STATUS_LED_IDLE_OK,
    STATUS_LED_FLX4_CONNECTED,
    STATUS_LED_FLX4_DISCONNECTED,
    STATUS_LED_P4_LINK_OK,
    STATUS_LED_P4_LINK_LOST,
    STATUS_LED_CONTROL_ACTIVITY,
    STATUS_LED_MIDI_ACTIVITY,
    STATUS_LED_UART_ACTIVITY,
    STATUS_LED_CALIBRATION_MODE,
    STATUS_LED_WARNING,
    STATUS_LED_ERROR,
    STATUS_LED_FATAL,
} status_led_state_t;

typedef enum {
    STATUS_LED_PATTERN_OFF = 0,
    STATUS_LED_PATTERN_SOLID,
    STATUS_LED_PATTERN_SLOW_BLINK,
    STATUS_LED_PATTERN_FAST_BLINK,
    STATUS_LED_PATTERN_PULSE,
    STATUS_LED_PATTERN_FLASH_ONCE,
} status_led_pattern_t;

typedef struct { uint8_t r, g, b; } status_led_rgb_t;

esp_err_t status_led_init(void);
esp_err_t status_led_set_state(status_led_state_t state);
esp_err_t status_led_flash(status_led_state_t state, uint32_t duration_ms);
status_led_state_t status_led_get_state(void);

void status_led_notify_boot(void);
void status_led_notify_idle_ok(void);
void status_led_notify_flx4_connection(bool connected);   /* umjesto usb_mounted */
void status_led_notify_p4_link(bool connected);
void status_led_notify_control_activity(void);
void status_led_notify_midi_activity(void);
void status_led_notify_uart_activity(void);
void status_led_notify_calibration(bool active);
void status_led_notify_warning(void);
void status_led_notify_error(void);
void status_led_notify_fatal(void);

#ifdef __cplusplus
}
#endif
```

---

## 6. `status_led.c` — kostur (s ispravcima)

Includeovi i konstante kao u V2. **Ispravak 1: forward-deklaracija taska**
(init poziva `xTaskCreate(status_led_task, ...)` prije nego je task definiran):

```c
#include "status_led.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

static void status_led_task(void *arg);   /* forward decl — bio problem u V2 */

#define STATUS_LED_TASK_STACK 3072
#define STATUS_LED_TASK_PRIO  3
#define STATUS_LED_QUEUE_LEN  12
#define STATUS_LED_FLASH_MIN_GAP_US 50000

static const char *TAG = "status_led";
static led_strip_handle_t s_strip = NULL;
static TaskHandle_t  s_task  = NULL;
static QueueHandle_t s_queue = NULL;
static volatile status_led_state_t s_current_state = STATUS_LED_BOOT;
static int64_t s_last_control_flash_us, s_last_midi_flash_us, s_last_uart_flash_us;

typedef struct {
    status_led_state_t state;
    uint32_t duration_ms;
    bool is_flash;
} status_led_cmd_t;
```

**Ispravak 2: brightness se stvarno primjenjuje** (u V2 je helper postojao ali
se nije koristio). Skaliranje ide u `apply_rgb`:

```c
static uint8_t status_led_scale(uint8_t v)
{
    return (uint8_t)(((uint16_t)v * CONFIG_STATUS_LED_BRIGHTNESS) / 100);
}

static void status_led_apply_rgb(status_led_rgb_t c)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0,
                        status_led_scale(c.r),
                        status_led_scale(c.g),
                        status_led_scale(c.b));
    led_strip_refresh(s_strip);
}

static void status_led_off(void)
{
    if (s_strip) led_strip_clear(s_strip);
}
```

Init (`led_strip_new_rmt_device` na `CONFIG_STATUS_LED_GPIO`, queue, task, boot),
boja-mapa, pattern-mapa, priority helper i task petlja ostaju kao u V2, ali:
- enumovi `USB_MIDI_*` → `FLX4_*`,
- `STATUS_LED_GPIO` konstanta → `CONFIG_STATUS_LED_GPIO`.

---

## 7. Integracija — S3 `app_main` (stvarna struktura)

S3 `app_main` ([main/app_main.c](firmware/control-board-s3/main/app_main.c)) već
poziva `control_link_init()`, `flx4_midi_host_init()`, `calibration_init()`,
`panel_io_init()`, heartbeat task. Dodati:

```c
#include "status_led.h"

// na početku app_main:
esp_err_t led_rc = status_led_init();      // interno zove notify_boot()
if (led_rc != ESP_OK) {
    ESP_LOGW(TAG, "status_led_init: %s", esp_err_to_name(led_rc));
}

// nakon što su control_link/flx4_host/calibration/panel spremni:
status_led_notify_idle_ok();
```

---

## 8. FLX4 USB veza — ISPRAVLJENO (bez TinyUSB device callbackova)

S3 je host; veza se već prati u `flx4_midi_host.c` u
`publish_connection_state(bool connected)` (linija ~370), koja šalje
`CTRL_FLX4_CONNECTED/DISCONNECTED` P4-u. Tu se dodaje LED hook:

```c
// firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c
#include "status_led.h"

static void publish_connection_state(bool connected)
{
    if (!should_publish_connection_state(connected)) {
        return;
    }
    status_led_notify_flx4_connection(connected);   // <— dodano
    const int16_t value = connected ? CTRL_FLX4_CONNECTED : CTRL_FLX4_DISCONNECTED;
    /* ... postojeće slanje P4-u ... */
}
```

Ako `flx4_midi_host` ne smije ovisiti o `status_led` (slojevitost), umjesto
direktnog poziva izложити slabu (weak) notify funkciju ili callback koji
`app_main` registrira. **`tud_mount_cb`/`tud_umount_cb`/`tinyusb_midi_send` se NE
koriste** — obrisati tu sekciju iz V2.

---

## 9. Control input aktivnost — `panel_io`

Kad `panel_io`/router detektira input (button/encoder/jog/pitch/browse):

```c
status_led_notify_control_activity();   // ima ugrađen throttling
```

Ne zvati `status_led_flash()` direktno po jog ticku.

---

## 10. MIDI aktivnost — slanje FLX4-u

Kad S3 uspješno pošalje MIDI event FLX4-u (LED feedback out putem host OUT
endpointa u `flx4_midi_host`/`control_link`):

```c
if (send_rc == ESP_OK) status_led_notify_midi_activity();
else                   status_led_notify_warning();
```

---

## 11. UART / P4 link — ISPRAVLJENO (uklopljeno u postojeći RX)

### 11.1 UART aktivnost + P4 watchdog timestamp

S3 već ima `handle_p4_frame()` u
[control_link_uart.c:76](firmware/control-board-s3/components/control_link/control_link_uart.c#L76).
Tu se ažurira timestamp i, na prvi ispravan frame nakon tišine, javlja link OK:

```c
// firmware/control-board-s3/components/control_link/control_link_uart.c
#include "status_led.h"
#include "esp_timer.h"

static int64_t s_last_p4_rx_us = 0;
static bool    s_p4_link_connected = false;

static void handle_p4_frame(const uint8_t *f)
{
    s_last_p4_rx_us = esp_timer_get_time();
    if (!s_p4_link_connected) {
        s_p4_link_connected = true;
        status_led_notify_p4_link(true);
    }
    status_led_notify_uart_activity();
    /* ... postojeća obrada frame-a (LED naredbe itd.) ... */
}
```

### 11.2 Periodička provjera timeouta

Postojeći `heartbeat_task` (šalje S3 heartbeat P4-u) je prirodno mjesto da
periodički provjeri i P4→S3 tišinu:

```c
void control_link_check_p4_timeout(void)   // zvati iz heartbeat_task petlje
{
    int64_t now = esp_timer_get_time();
    if (s_p4_link_connected &&
        now - s_last_p4_rx_us > (int64_t)CONFIG_STATUS_LED_P4_TIMEOUT_MS * 1000) {
        s_p4_link_connected = false;
        status_led_notify_p4_link(false);
    }
}
```

---

## 12. UART protokol — sistemski statusi od P4 (opcionalno)

Ako P4 kasnije šalje sistemske statuse S3-u, `CTRL_TYPE_SYSTEM_STATUS 0x20`
**ne kolidira** s postojećima (`0x01` BUTTON, `0x02` ENCODER, `0x03` PITCH,
`0x04` HEARTBEAT, `0x81` LED, `0x82` STATE). Alternativno se može reciklirati
rezervirani `CTRL_TYPE_STATE 0x82`. `SYS_STATUS_*` handler i P4 slanje kao u V2;
`AUDIO_WARNING` je opće upozorenje, ne playback status.

---

## 13. Kconfig

```text
menu "Status LED"

config STATUS_LED_ENABLE
    bool "Enable GPIO48 status RGB LED"
    default y

config STATUS_LED_GPIO
    int "Status LED GPIO"
    default 48

config STATUS_LED_BRIGHTNESS
    int "Status LED brightness percent"
    default 30
    range 1 100

config STATUS_LED_P4_TIMEOUT_MS
    int "P4 link timeout in ms"
    default 2000

endmenu
```

---

## 14. CMake + dependency

```cmake
# components/status_led/CMakeLists.txt
idf_component_register(
    SRCS "status_led.c"
    INCLUDE_DIRS "include"
    REQUIRES led_strip esp_timer
)
```

Dodati u S3 `main/idf_component.yml` (uz postojeći `espressif/esp_tinyusb`):

```yaml
dependencies:
  espressif/led_strip: "^3.0.0"   # uskladiti s ESP-IDF v5.5
```

---

## 15. Test plan (sažeto)

1. GPIO test: `status_led_set_state(STATUS_LED_ERROR)` → crveno blinka. Ako ne:
   provjeriti Korak 0 (postoji li RGB na 48), RGB/GRB redoslijed, target S3.
2. Boot: bijeli flash → zeleno idle → cijan (P4 OK) → plavo pulsiranje (FLX4
   connected).
3. Input: tipka/enkoder → zeleni flash; brzi jog → nema zatrpavanja (throttle).
4. FLX4 replug: iskopčaj FLX4 → žuto blinkanje; ukopčaj → plavo pulsiranje
   (potvrđuje da hook na `publish_connection_state` radi, ne TinyUSB).
5. P4 link: prekini P4 → nakon `CONFIG_STATUS_LED_P4_TIMEOUT_MS` žuto blinkanje.
6. Warning/error/fatal: activity flash ne smije pregaziti.

---

## 16. Redoslijed implementacije

0. **Potvrditi GPIO48 adresabilni RGB (Korak 0).**
1. `status_led` komponenta + `led_strip` dependency + Kconfig.
2. `status_led_init()` u `app_main`, test jednom bojom.
3. Boje/patterni/queue/task (s forward-decl i brightness scale).
4. `panel_io` → `notify_control_activity()`.
5. MIDI send → `notify_midi_activity()`.
6. `handle_p4_frame()` → `notify_uart_activity()` + P4 watchdog timestamp.
7. `heartbeat_task` → `control_link_check_p4_timeout()`.
8. FLX4 veza → hook u `publish_connection_state()`.
9. Calibration → `notify_calibration()`.
10. (Opc.) P4 sistemski statusi preko `CTRL_TYPE_SYSTEM_STATUS`.
11. `PINOUT.md` update.

---

## 17. Sažetak ispravaka vs V2

| # | V2 (netočno) | V3 (ispravljeno) |
|---|---|---|
| 1 | `tud_mount_cb`/`tud_umount_cb` za USB MIDI | hook na `publish_connection_state()` (host stack) |
| 2 | GPIO48 = onboard RGB (pretpostavka) | Korak 0 hardverska provjera; custom panel ima LED na 33/34/38 |
| 3 | novi izmišljeni P4 watchdog | uklopljen u `handle_p4_frame()` + `heartbeat_task` |
| 4 | task korišten prije definicije | forward-deklaracija `status_led_task` |
| 5 | brightness helper neiskorišten | primijenjen u `apply_rgb` |
| 6 | `tinyusb_midi_send` primjer | MIDI OUT ide kroz host/`control_link` put |
