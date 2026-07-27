# `fixevi.md` — završni remediation audit

Auditirani dokument: `fixevi(1).md`, izvorni read-only pregled commita
`RC1-262-g3f75afe`.

Auditirana grana: `fix/release-blockers-and-concurrency`.

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

## P1

| # | Nalaz | Status | Dokaz / preostali gate |
|---|---|---|---|
| 2 | Sort/load može učitati pogrešnu pjesmu | **Zatvoreno** | `track_key + generation`, `load_by_identity()`, `409` na stale generaciju i sort/load mutex. |
| 3 | Load-error može leakati PSRAM i ostaviti stare taskove | **Zatvoreno; HW/stress pending** | Svaki load bezuvjetno stop/join-a prethodnu sesiju; teardown više ne ovisi o `loaded`; PSRAM buffer ima jednog ownera. Test 100 realnih oštećenih MP3/WAV/FLAC retryja nije izveden. |
| 4 | ANLZ borrowed pointer UAF | **Zatvoreno; stress pending** | Task-owned klonirani snapshoti i writer/reader guard. Load/Beat Jump/Sync/Loop stress na uređaju nije izveden. |
| 5 | Pun control queue može izgubiti release | **Zatvoreno za primarni defekt** | Button/state edgeovi koriste lossless backpressure; samo kontinuirane vrijednosti se coalescaju; UART producer ne drenira shared queue. Dodatni level-state watchdog iz preporuke nije uveden. |
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
| OTA POST prihvaća parcijalni recv | **Zatvoreno; fragmentation fixture pending** | Receive petlja čita do `content_len`. Poseban 1–3 byte HTTP integration fixture nije dodan. |
| OTA GET ne escapea JSON | **Zatvoreno** | SSID, URL, detail i address prolaze kroz `web_api_json_escape`. |
| Web loop zaobilazi `deck_core` | **Zatvoreno** | Web loop/exit se pretvaraju u autoritativne deck evente. |
| Probe i pull-OTA paralelno mijenjaju Wi-Fi stack | **Zatvoreno** | Centralni `wifi_transition_lease` rezervira probe ili OTA prijelaz prije stvaranja workera. |
| USB mount nema retry / disconnect se može izgubiti | **Zatvoreno; HW pending** | Desired/current state, task notification + periodični reconciliation i eksponencijalni bounded mount retry. Replug i fault-injection test na stvarnom P4 ostaju. |
| ANLZ short-read postaje valjani cache | **Zatvoreno; fuzz pending** | Exact-read marker, bounded section walk, privremeni objekt i publish tek nakon pune validacije. Fuzz/truncation corpus još nije kompletan. |
| PDB duration se zamijeni zadnjim beatom | **Zatvoreno** | Javni `library_load_anlz()` čuva svaki nonzero PDB/audio duration; beatgrid duration ostaje fallback kada duration nedostaje. |
| PVBR 32-bit overflow | **Zatvoreno** | `uint64_t` račun i clamp na `duration_ms`. |
| Javni AP PSK i CSRF marker umjesto autentikacije | **Otvoreno** | Potrebna je proizvodna odluka: per-device PSK ili fizički prikazan jednokratni token, fizička OTA potvrda i PMF/WPA3 politika. |
| Secure boot i flash encryption nisu uključeni | **Provisioning odluka** | Ne uključivati običnim source commitom. Potreban je dokumentiran key/provisioning/recovery proces i potvrda nepovratnih eFuse koraka. |

## P3 / performanse i održavanje

| Nalaz | Status | Sljedeći korak |
|---|---|---|
| Library sort kopira velike track objekte, UI puni 1024×5 ćelija | **Otvoreno** | Immutable track store + mali indeks/handle array i virtualizirana/paginirana tablica. |
| Tri framebuffer buffera bez stvarnog swapa | **Zatvoreno konzervativnim putem; HW pending** | BSP i backend sada alociraju/traže samo jedan framebuffer, što odgovara stvarnom partial-LVGL/PPA ponašanju i vraća dvije nepotrebne full-screen PSRAM alokacije. Budući pravi swap bio bi zasebna, mjerena optimizacija. |
| Master Tempo PSRAM hot path | **Otvoreno za optimizaciju** | Coarse-to-fine search, block-local PCM cache i mjerenje `mix_max_us` na stvarnom dual-deck P4. |
| Cijeli komprimirani track mora stati u PSRAM | **Djelomično** | Kratkoročni largest-free-block preflight i `TRACK TOO LARGE` postoje; rolling compressed page cache nije implementiran. |
| Legacy/dead ostaci i monoliti | **Otvoreno** | Ukloniti potvrđene mrtve API-je i refaktorirati prema task ownershipu, ne samo prema datotekama. |
| Recorder nije spreman za ponovno uključivanje | **Otvoreno; funkcija ostaje disabled** | Producer/STOP handshake, finalize error propagation, `.part` rename samo nakon potpunog uspjeha i fault-injection testovi. |
| Zastarjeli komentari/dokumentacija | **Djelomično** | S3 translator default komentar je ispravljen; `sdkconfig.defaults` već navodi četiri ekrana, 32 MB PSRAM i rev 1.0. `firmware/main-deck-p4/CLAUDE.md` još ima staru tvrdnju `REV_MIN_FULL=0` i mora se uskladiti. |
| Dependency build nije reproducibilan | **Zatvoreno** | Oba `dependencies.lock` filea su commitana i clean CI ih koristi. |

## Testovi koji i dalje nisu zamijenjeni CI-em

- flash/boot na fizičkom P4 i S3
- realni MP3/FLAC/WAV decode fixture i realni `export.pdb`
- sanitizer/TSan/fuzz i ciljane concurrency interleaving probe
- stvarno P4 mjerenje keylock/PSRAM deadlinea
- USB mount/disconnect fault injection
- prikaz bez tearinga nakon single-framebuffer korekcije
- dugotrajni dual-deck audio/control/USB/SD soak

## Preporučeni nastavak

1. Hardverski potvrditi USB reconciliation i single-framebuffer prikaz.
2. Uskladiti preostalu revizijsku uputu u `firmware/main-deck-p4/CLAUDE.md`.
3. Napraviti library indeks/virtualizaciju kao zaseban performance PR.
4. Napraviti Master Tempo mjerni build i optimizirati samo prema stvarnim deadline podacima.
5. Recorder zadržati isključen dok njegov zasebni safety PR ne prođe fault injection.
6. AP autentikaciju i secure-boot/flash-encryption voditi kao zasebne security/provisioning odluke.
