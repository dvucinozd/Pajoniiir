# `fixevi.md` — završni remediation audit

Auditirani dokument: `fixevi(1).md`, izvorni read-only pregled commita
`RC1-262-g3f75afe`.

Auditirana grana: `fix/release-blockers-and-concurrency`.

Naknadni redovi i dokazi u ovom dokumentu ažuriraju se kako se pojedini
paketi iz ESP-IDF 6.0.2 migration plana implementiraju; povijesni audit commit
iznad ostaje referenca za izvorni skup nalaza.

Statusi:

- **Zatvoreno** — primarni defekt je uklonjen i pokriven postojećim host/CI gateom.
- **Zatvoreno; HW/stress pending** — izvorni kodni defekt je uklonjen, ali test koji
  zahtijeva stvarni uređaj, realni fixture ili dugotrajno opterećenje još nije izveden.
- **Djelomično** — implementiran je kratkoročni ili primarni fix, ali dokument traži i
  dodatni dugoročni/refaktorski korak.
- **Otvoreno** — nalaz nije riješen ovom granom.
- **Provisioning odluka** — namjerno nije automatski uključeno jer je nepovratno ili
  traži proizvodni proces i upravljanje ključevima.

## P0

| # | Nalaz | Status | Dokaz |
|---|---|---|---|
| 1 | Čisti P4 build cilja pogrešnu reviziju silicija | **Zatvoreno** | `sdkconfig.defaults` vraća pre-v3 selector; CI provjerava selector i `REV_MIN_FULL=100` prije builda. |
| 1a | ESP-IDF 6 ESP-Hosted SDIO inicijalizira jedini P4 SDMMC controller prije `app_main()`, pa drugi globalni init za microSD ostavlja `/sd` offline | **Zatvoreno; HW potvrđeno** | BSP za Hosted/IDF6 ponovno koristi inicijalizirani controller za slot 0 i zadržava slot-aware deinit. P4 host gate i IDF6 build prolaze; puni COM15 flash `RC2-3-g136aad7` 2026-08-02 boota IDF v6.0.2 i montira 59.688 MB SDHC karticu u 4-bit modu. Vidi `validation/P4_IDF6_SDMMC_SMOKE_20260802.md`. |

## P1

| # | Nalaz | Status | Dokaz / preostali gate |
|---|---|---|---|
| 2 | Sort/load može učitati pogrešnu pjesmu | **Zatvoreno** | `track_key + generation`, `load_by_identity()`, `409` na stale generaciju i sort/load mutex. |
| 3 | Load-error može leakati PSRAM i ostaviti stare taskove | **Zatvoreno; HW/stress pending** | Svaki load bezuvjetno stop/join-a prethodnu sesiju; teardown više ne ovisi o `loaded`; PSRAM buffer ima jednog ownera. Test 100 realnih oštećenih MP3/WAV/FLAC retryja nije izveden. |
| 4 | ANLZ borrowed pointer UAF i nekonzistentan loaded-track ownership | **Software zatvoreno; HW/stress pending** | `deck_core` posjeduje koherentan key/generation/BPM/duration i immutable/versioned refcount ANLZ snapshot. Reader acquire ne alocira; 20.000 publish/40.000 reader stress ne miješa generacije. Compact deck snapshot izostavlja high-resolution waveform. Load/Beat Jump/Sync/Loop stress na uređaju nije izveden. |
| 5 | Pun control queue može izgubiti release | **Zatvoreno; HW/stress pending** | Zajednički 54-slot held-state reconciler na S3 i P4 trajno čuva zadnji press/release za jog touch, Shift, Censor, Pad FX i roll bez producer draina/reordera shared queuea. Capacity+1, disconnect, heartbeat i P4 reboot host regresije prolaze (`control_link_uart` 110 i reconciler 32 provjere); fizički stalled-link/reconnect smoke nije izveden. |
| 6 | S3 disconnect/heartbeat race | **Zatvoreno** | Connection state je atomican; heartbeat samo traži refresh, a USB owner šalje stanje i descriptor. |
| 7 | S3 `/events` blokira web server | **Zatvoreno** | Bounded SSE snapshot response zatvara zahtjev; EventSource se reconnecta. |
| 8 | S3 AP start failure beskonačno retrya | **Zatvoreno** | Failure-latched `ERROR`; novi pokušaj traži eksplicitni OFF→ON edge. |

## P2

| Nalaz | Status | Dokaz / preostali gate |
|---|---|---|
| Destruktivni load-error ostavlja stari UI | **Zatvoreno** | Stale/error rezultat objavljuje empty deck state i čisti title/waveform/ANLZ/highlight. |
| Torn `deck_core` snapshot i cross-task reset | **Zatvoreno** | Actor-owned mutacije, sinkroni reset command i koherentni published snapshot. |
| Master Tempo cross-task DSP reset | **Zatvoreno** | Command epoch primjenjuje output task na granici audio bloka. |
| S3 audio ring torn overwrite | **Zatvoreno** | SPSC drop-newest kada je ring pun. |
| UAC bira nepodržani 24-bit format | **Zatvoreno** | Selector zahtijeva podržane kanale, 16-bit/2 B, stopu i packet size unutar MPS-a. |
| MIDI IN/OUT iz različitih interface/alt kandidata | **Zatvoreno** | Candidate se resetira po interfaceu; par mora biti iz istog `(interface, alt)` i MPS mora biti nenula. |
| Metadata cache/profile install zaobilaze `sd_io_gate` | **Zatvoreno** | Cache i profile storage operacije koriste gate; recorder state se ponovno provjerava nakon ulaska. |
| Profile manager drži mutex preko SD I/O-a | **Zatvoreno** | Descriptor ide u queue; scan/install se izvršavaju bez manager mutexa, zatim kratki registry swap. |
| `service_log_sync()` nije writer barijera | **Zatvoreno** | Queue-ordered sync item, ACK nakon `fsync`, sole `FILE*` owner i status snapshot. |
| Brightness commit na svaki slider event | **Zatvoreno** | Live backlight + 500 ms debounce worker izvan LVGL taska. |
| Settings ignorira NVS greške i dijeli OTA stringove | **Zatvoreno** | Provjera set/commit rezultata; RAM snapshot se objavljuje tek nakon uspjeha i pod lockom. |
| Pull-OTA start/status ima cross-task race | **Zatvoreno; HW pending** | Atomic single-winner operation gate serijalizira check/install start; status, offer, TTL i progress objavljuju se kao jedan bounded `portMUX` snapshot, a mreža/flash ostaju izvan locka. Task-create failure i retry pokriva host gate test; stvarni AP→STA→AP failure/retry smoke ostaje. |
| Monitor PCM format/enable/statistika nisu koherentni | **Zatvoreno; HW soak pending** | Atomic enable gate, versioned 32-bit format snapshot, monotoni atomski brojači i postojeći SPSC queue ownership uklanjaju torn readove. Host test pokriva format, CRC i saturation; fokusirani RC2/IDF6 smoke 2026-08-02 potvrdio je čujan FLX4 CUE/MONITOR uz MAIN izlaz, ali stvarni numerički P4→S3 I2S soak ostaje. |
| Parcijalni profile-manager/monitor-I2S init ostavlja resurse | **Software popravljeno; failure-injection/HW pending** | Profile manager rollback briše oba taska, oba queuea, semafor, mutex i callback; monitor staged init objavljuje I2S singleton tek nakon taskova te na kvaru briše taskove i kanal. Source gateovi i P4 build prolaze, ali dinamički IDF failure-injection fixture i stvarni retry još nisu izvedeni. |
| ANLZ getter radi velike deep-copy alokacije | **Software zatvoreno; HW heap/action soak pending** | Deck i UI store objavljuju immutable refcount snapshot; acquire/release ne alocira. OOM, stari reader preko swapa, više readera, maksimalni beatgrid + 128 KiB waveform i 10.000 acquire/release host regresija prolaze. P4 USB reload i Beat Jump/Sync/Loop heap-floor soak ostaje. |
| OTA POST prihvaća parcijalni recv | **Zatvoreno; fragmentation fixture pending** | Receive petlja čita do `content_len`. Poseban 1–3 byte HTTP integration fixture nije dodan. |
| OTA GET ne escapea JSON | **Zatvoreno** | SSID, URL, detail i address prolaze kroz `web_api_json_escape`. |
| Web loop zaobilazi `deck_core` | **Zatvoreno** | Web loop/exit se pretvaraju u autoritativne deck evente. |
| Probe i pull-OTA paralelno mijenjaju Wi-Fi stack | **Zatvoreno** | Centralni `wifi_transition_lease` rezervira probe ili OTA prijelaz prije stvaranja workera. |
| USB mount nema retry / disconnect se može izgubiti | **Zatvoreno; HW pending** | Desired/current state, task notification + periodični reconciliation i eksponencijalni bounded mount retry. Replug i fault-injection test na stvarnom P4 ostaju. |
| Library refresh/USB remove koriste lossy `volatile` zastavice | **Zatvoreno; HW pending** | Atomski event-generation brojači čuvaju request pristigao tijekom obrade; samo LVGL task invalidira page cache. USB remove podiže deck media floor, a load worker nakon audio loadanja ponovno provjerava generation i gasi stale sesiju. |
| ANLZ short-read postaje valjani cache | **Zatvoreno; fuzz pending** | Exact-read marker, bounded section walk, privremeni objekt i publish tek nakon pune validacije. Fuzz/truncation corpus još nije kompletan. |
| PDB duration se zamijeni zadnjim beatom | **Zatvoreno** | Javni `library_load_anlz()` čuva svaki nonzero PDB/audio duration; beatgrid duration ostaje fallback kada duration nedostaje. |
| PVBR 32-bit overflow | **Zatvoreno** | `uint64_t` račun i clamp na `duration_ms`. |
| Javni AP PSK i CSRF marker umjesto autentikacije | **Otvoreno** | Potrebna je proizvodna odluka: per-device PSK ili fizički prikazan jednokratni token, fizička OTA potvrda i PMF/WPA3 politika. |
| Secure boot i flash encryption nisu uključeni | **Provisioning odluka** | Ne uključivati običnim source commitom. Potreban je dokumentiran key/provisioning/recovery proces i potvrda nepovratnih eFuse koraka. |

## P3 / performanse i održavanje

| Nalaz | Status | Sljedeći korak |
|---|---|---|
| Library sort kopira velike track objekte, UI puni 1024×5 ćelija | **Zatvoreno** | Track zapisi su nakon publish-a immutable, sort kopira samo double-buffered `uint16_t` row-order, a LVGL Library prikaz drži jednu stranicu od 8×5 — najviše 40 živih ćelija — s PREV/NEXT navigacijom umjesto do 5120 ćelija. |
| Tri framebuffer buffera bez stvarnog swapa | **Zatvoreno konzervativnim putem; focused HW potvrđen** | BSP i backend sada alociraju/traže samo jedan framebuffer, što odgovara stvarnom partial-LVGL/PPA ponašanju i vraća dvije nepotrebne full-screen PSRAM alokacije. Operator je 2026-08-02 potvrdio fokusirani display/touch/PSRAM-backed UI smoke; budući pravi swap ili dugi display soak ostaju zasebna, mjerena optimizacija/gate. |
| Master Tempo PSRAM hot path | **Software instrumentacija zatvorena; HW mjerenje pending** | Output task već vodi `mix_max_us`, per-phase i worst-group outlier podatke bez dodatne alokacije. Daljnja DSP optimizacija smije se raditi tek nakon stvarnog dual-deck P4 mjerenja; to je fizički acceptance gate, ne nedovršeni source fix. |
| Cijeli komprimirani track mora stati u PSRAM | **Zatvoreno; djelomični HW smoke, stress pending** | MP3/WAV/FLAC koriste seekable bounded cache od 8 × 32 KiB po decku. Fokusirani real-MP3 playback prošao je 2026-08-02. WAV/FLAC nisu testirani: USB audit našao je 68 MP3 i nula fizičkih WAV/FLAC fileova, iako PDB sadrži mrtve retke. Cache miss radi jedan gated `read-at`, FLAC koristi `drflac_open` callbackove, a sustained dual-deck stress i dalje ostaje. |
| Compressed-cache LRU stamp se prevrće nakon `UINT32_MAX` | **Software zatvoreno** | Cache i page stampovi su `uint64_t`; 68-check host suite prelazi povijesnu 32-bitnu granicu i potvrđuje da se izbacuje stvarno najstarija stranica. Real-file USB/audio stress ostaje dio šireg bounded-cache hardware gatea. |
| Projektni ESP-IDF 6 compiler/Kconfig warningi | **Zatvoreno** | Uklonjena su dva mrtva audio dijagnostička helpera, FATFS bool defaulti koriste `default n`, a P4 host suite i firmware build prolaze. Preostali NimBLE `default 0` note dolazi iz pinanog ESP-IDF v6.0.2 sourcea. |
| Legacy/dead ostaci i monoliti | **Zatvoreno** | Uklonjeni su mrtvi `file_buf`/full-track seek-table ostaci; compressed cache, recorder producer gate i finalize transakcija izdvojeni su u male ownership module s čistim host testovima. Uz to su povučena svih deset `#include "<impl>.c"` compilation wrappera: `grep -rn '#include ".*\.c"' firmware/` više ne vraća ništa izvan vendored koda, svaka komponenta buildá datoteku koju joj CMakeLists imenuje, a s njima su nestali `#define` nad `fread`/`fgetc` i nad `vTaskDelete`, C11 6.9.2p3 povreda u `audio_engine`, i legacy tijela koja su ostajala u imageu. Gate u `run_p4_host_tests.ps1` sprječava povratak. |
| Recorder nije spreman za ponovno uključivanje | **Software safety zatvoren; funkcija ostaje disabled do HW fault injectiona** | STOP prvo zatvara producer gate i čeka aktivnog producera, zatim writer drenira zatvoreni ring. Checkpoint/write/finalize greške propagiraju se; `.part` se objavljuje kao obični `.wav` samo nakon uspješnog patch+fsync+close niza. Host fault-injection pokriva svaku fazu. |
| Zastarjeli komentari/dokumentacija | **Zatvoreno** | S3 translator default, P4 revizija, bounded audio cache, paginirana Library tablica i recorder release-gate dokumentacija usklađeni su sa sourceom. |
| Dependency build nije reproducibilan | **Zatvoreno** | Oba `dependencies.lock` filea su commitana i clean CI ih koristi. |
| CI action/image reference su mutable | **Immutable inputi zatvoreni; SBOM pending** | ESP-IDF image koristi v6.0.2 OCI digest, GitHub actioni pune commit SHA-ove, a S3/P4 artefakti nose Git/IDF/lock provenance i firmware SHA-256 manifest. Formalni SPDX/CycloneDX SBOM generator još nije odabran ni pinan. |

## Testovi koji i dalje nisu zamijenjeni CI-em

- flash/boot na fizičkom P4 i S3
- realni MP3/FLAC/WAV decode fixture i realni `export.pdb`
- širi TSan/fuzz corpus izvan novih bounded-cache i recorder fault-injection host testova
- stvarno dual-deck P4 mjerenje keylock/PSRAM deadlinea i odluka treba li DSP optimizaciju
- USB mount/disconnect fault injection, uključujući remove usred stvarnog audio load/decode prozora
- S3/P4/FLX4 held-control saturation, disconnect/reconnect i P4-only reboot smoke
- prikaz bez tearinga nakon single-framebuffer korekcije
- dugotrajni dual-deck audio/control/USB/SD soak, uključujući cache-miss i recorder power-loss fault injection

## Preporučeni nastavak

1. Dovršiti USB reconciliation i bounded-cache stress: single-framebuffer fokusirani prikaz i realni MP3 prošli su, ali treba ispravno re-eksportati fizičke WAV/FLAC fixturee te ih pokrenuti pod sustained dual-deck opterećenjem s brojačima.
2. Izmjeriti Master Tempo/keylock deadline na stvarnom P4 s dva aktivna decka te optimizirati DSP samo ako mjerenja pokažu prekoračenje audio budgeta.
3. Recorder zadržati release-disabled dok microSD i power-loss fault injection ne potvrde STOP drain, zadržavanje neuspjelog `.part` filea i objavu samo potpuno finaliziranog WAV-a.
4. FLX4 MIDI/LED, PCM5102A i fokusirani monitor/headphone put su potvrđeni na RC2/IDF6; dovršiti S3↔P4 reconnect, numeric monitor soak, held-control saturation/reboot, Wi-Fi/OTA exclusion i dugotrajne fizičke gateove.
5. AP autentikaciju, fizičku OTA potvrdu, PMF/WPA3 te Secure Boot/Flash Encryption voditi kao zasebne proizvodne i provisioning odluke.
