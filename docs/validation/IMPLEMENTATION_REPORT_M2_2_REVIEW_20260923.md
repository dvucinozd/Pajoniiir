# Izvješće o implementaciji popravaka nakon M2.2 reviewa

Datum: 2026-09-23. Status: **software candidate installed; focused OTA smoke
PASS; full hardware acceptance pending**.

## 1. Kandidat i granice

| Stavka | Vrijednost |
| --- | --- |
| Grana | `codex/review-m2-2-fixes` |
| Polazni commit | `ac237380af39bc7823f32a0b353a5e3d81fc27bf` |
| Produkcijski tag | immutable `M2.2` na `2c2ec32c253d368765123d7bbf8d37389b790b55` |
| Firmware candidate commit | `aab234058173f5c7783e1cd08c6bcec56ffeb85e` |
| Ugrađena verzija | `M2.2-2-gaab2340` |
| ESP-IDF | `v6.0.2` |
| Candidate binary | 2.504.672 B |
| Binary SHA-256 | `3d11627bcd5a78c8209a3e17daeb68eb2ac3b523cc5d28cb7671d75ed66da579` |
| Signed bundle SHA-256 | `0ecb3d737cba3d7588b09c6d4b261a200791edcbf62692867a705000834c7c4a` |
| OTA prostor | 1.165.344 B slobodno, 40% |

Kandidat je lokalno commitan, full-clean izgrađen i potpisan ključem `rel-001`.
Bundle i vanjski manifest neovisno su verificirani committed javnim ključem.
Instaliran je lokalnim signed push OTA putem na `ota_1`. Commit nije pushan,
release tag nije izrađen i javni OTA kanal nije promijenjen.

## 2. Implementirani popravci

### R01 — PCM timeline publication

Rijetki 64-bitni cursor publication sada u firmware buildu koristi modul-lokalni
`portMUX_TYPE` oko slijeda odd version -> epoch/low -> even version. Normalni
per-frame SPSC put ostao je bez mutexa. Postojećih 309 timeline testova, uključujući
wrap i reset/handoff slučajeve, prolazi. Forced-preemption mjerenje i trajanje
critical sectiona na P4 ostaju hardware gate; host test sam ne dokazuje scheduler.

### F01 — trajni media identitet, hot cues i cache v3

- Dodan je `media_persistent_id_t`: SHA-256 domene, digest cijelog `export.pdb`,
  točna USB-relative putanja, veličina audio datoteke i mtime.
- PDB se hashira streamingom jednom po gradnji kataloga uz media gate i provjeru
  generation/stat prije objave. Audio identitet nastaje u load workeru prije
  ANLZ/cache lookupa i prenosi se kroz loaded-track/deck strukture.
- Novi NVS namespace `hotcue_v2` koristi kratki ključ, ali blob nosi puni digest,
  verziju, duljinu, masku, osam slotova i CRC-32. Puni digest sprječava tihi kratki
  key collision. Legacy `hotcue` ostaje netaknut i ne učitava se automatski.
- Metadata cache je podignut na `/sd/trackcache/v3` i provjerava puni identitet.
  V2 je čist cache miss.
- SHA known-answer, derivation, round-trip, invalid blob i collision regresije
  uključene su u glavni host runner.

### F02 i F09 — ANLZ parser

Parser prvo radi jedan cjelovit bounded section walk do deklariranog kraja.
Odbija partial tail, nevaljane envelope duljine i dvosmislene duplicate slotove.
PCOB/PCPT raspored usklađen je sa stvarnim offsetima; prolaze se sve PCOB sekcije,
memory liste se validiraju bez mapiranja na padove, a cue/loop podaci objavljuju
se tek nakon uspješne validacije. Lokalni format dokument je ispravljen. ANLZ
suite sada ima 41/41 prolaznih testova.

### F03 — redoslijed web kontrola

Svaka kontinuirana kontrola ima jedan in-flight zahtjev i jednu najnoviju pending
vrijednost. Stari timer više ne može naknadno poslati stariju vrijednost. Browser
contract test pokriva kontrolirani raspored i potvrđuje da je zadnja vrijednost
konačna. Ukupno prolazi 9/9 web testova.

### F04 — trenutni MAIN meter

Audio engine objavljuje zaseban `main_meter_peak` iz konačnog `master_out` bloka
tek nakon uspješnog sink writea, uz sigurno rukovanje `INT16_MIN` i decay na
tišini. Kumulativni limiter peak ostaje dijagnostički podatak. Web VU koristi
novi meter. Host regresija pokriva nulu, puni finalni mix i release na tišini.

### F05 — koherentan Library odgovor

Media catalog nudi acquire/release snapshot čiji generation, count i redovi
pripadaju istoj publikaciji. HTTP handler snapshotira prije slanja i otpušta ga
na svim obrađenim izlazima; ne drži catalog lock tijekom mrežnog prijenosa.
Browser dodatno ignorira kasni odgovor starijeg request sequencea.

### F06 — atomarna OTA konfiguracija

SSID, password i URL pohranjuju se u jednom versioned `ota_cfg_v2` NVS blobu s
magic/version/size poljima i CRC-om. Set i clear su serijalizirani write mutexom,
RAM snapshot se objavljuje tek nakon uspješnog durable upisa, a clear zapisuje
praznu tombstone konfiguraciju i vraća stvarni `esp_err_t`. Legacy trojka se
učitava samo kada v2 ne postoji i tada se pokušava migracija. Wi-Fi, web i pull
OTA koriste jedan koherentan getter; password se briše iz lokalnih snapshotova
nakon uporabe. App-settings suite prolazi 63 testa.

### F07 — strogi manifest JSON

Allocation-free parser sada validira cijeli JSON dokument, root/trailing sadržaj,
delimiter brojeva, dubinu do 8 i duplicate obvezne ključeve na odgovarajućoj
razini. Dodane su regresije za nedostajuću završnu zagradu, garbage nakon broja,
duplicate release i trailing bytes. Postojeći size/SHA/newer-only/potpisni ugovor
nije oslabljen.

### F08 — status zadnje prihvaćene probe operacije

`app_main` pod kratkim muxom pamti je li zadnja prihvaćena operacija link probe
ili OTA check. Status više ne preferira zauvijek stari OTA rezultat nakon novog
link probea. Implementiran je potreban redoslijed bez uvođenja novog javnog API
polja za operation ID; puna konkurentna operation-ID abstrakcija iz plana ostaje
moguće kasnije učvršćenje ako API bude proširivan.

### Naknadni OTA-reboot incident

Read-only status i 441.393 B service journala s uređaja pokazali su:

- verificirani M2.2 upload na bootu 488;
- prvi M2.2 boot 489 s reset razlogom `PANIC`;
- crash task `ota_reboot`, PC `0x4FF0CE88`, RA `0x4FF00B70`,
  MCAUSE `0x7`, MTVAL `0x3FF1009C`;
- uredne kasnije hladne bootove 490 i 491.

Privremeni clean build točno tagiranog M2.2 sourcea mapirao je PC na
`Cache_WriteBack_All()` i RA na `esp_restart_noos_inner()`. Push OTA i validation
reboot helper sada su pinirani na core 0 i imaju 4 KiB stack; pull OTA install
task, koji završava istim restartom, također je piniran na core 0. Ako helper
task nije moguće stvoriti, handler vraća grešku i ostavlja uređaj živim umjesto
izravnog restarta iz nepoznatog handler corea.

Ovo je ciljana mitigacija izvedena iz stvarnog dumpa, ali dump ne sadrži core ID.
Prvi ponovljeni signed push OTA na kandidatu završio je urednim `SW` resetom bez
novog `PANIC` boota. Stari dump namjerno je ostavljen kao dokaz. Incident ostaje
otvoren do dodatnih ponavljanja, pull OTA i validation reboot matrice.

## 3. Izvršena software verifikacija

| Gate | Rezultat |
| --- | --- |
| `tests/run_p4_host_tests.ps1` | PASS nakon završne OTA-reboot izmjene |
| Web contract | 9/9 PASS |
| PCM timeline | 309 PASS |
| ANLZ | 41/41 PASS |
| App settings | 63 PASS |
| Media identity | PASS |
| Audio DSP PC corpus | 411 PASS / 0 FAIL |
| UI simulator E2E | 7/7 točnih baseline screenshotova; bez promjene baselinea |
| Dual-deck keylock soak | 300 s virtualno; drift 0/0, click 0, clipping 0 |
| ESP-IDF build | PASS, `ESP-IDF v6.0.2` |
| Pre-freeze incremental binary budget | PASS, 2.504.624 B, 40% slobodno |
| Exact-commit `build_signed` | PASS, 2.504.672 B, SHA-256 `3d11627b...da579` |
| Signed package verification | PASS, `rel-001`; bundle SHA-256 `0ecb3d73...c7c4a` |
| Signed push OTA | PASS; `ota_0` M2.2 -> `ota_1` `M2.2-2-gaab2340` |
| Reboot result | PASS; boot 492, reset `SW`, bez novog `PANIC` boota |
| Validation reboot matrix | PARTIAL; 7 journalom potvrđenih `SW` bootova (493-496, 502-504), bez novog panic dumpa |
| Dual-deck FLX4 MAIN/cue smoke | PASS; operator potvrdio čist MAIN i PFL D1 |
| Dual-deck runtime counters | PASS; PCM 0/0, output-late 0, USB drop/overflow/packet failure/lost 0, `data_loss=false` |
| Dependency lock | nepromijenjen |
| `git diff --check` | PASS |

UI i keylock gateovi izvršeni su nakon funkcionalnih F01-F09 promjena. Završna
naknadna izmjena zahvatila je samo raspored reboot taskova; nakon nje ponovno su
izvršeni cijeli host suite i P4 build.

## 4. Trenutno stanje production uređaja

`pajoniiir.local` je dostupan na `192.168.4.1`. Uređaj prijavljuje
`M2.2-2-gaab2340`, slot `ota_1`, OTA `idle` i prazan `last_error`. Aktualni boot
521 ima reset razlog `POWERON`. USB host je ready, storage je montiran, Library
snapshot ima koherentna 324 retka, FLX4 je aktivan kao `pioneer_ddj_flx4`, daemon
errors i service-log drops su 0.

OTA config GET nakon migracije prijavljuje spremljeni SSID/URL i `has_password`,
ali ne izlaže password. Sačuvani stari M2.2 crash dump nije obrisan; njegov
`ota_reboot` PC/RA i dalje su jednaki izvornom dumpu, pa nema dokaza o novom
panic bootu kandidata.

Nakon spajanja stvarnog DDJ-FLX4 uređaj je potvrdio točni VID/PID
`2B73:0045`, MIDI IN/OUT, USB audio, lokalni runtime i aktivni ugrađeni profil.
Na bootu 521 učitani su `TAINTED DUB - CLIP.mp3` na D1 i `Star Eater.mp3` na
D2. Journal je zabilježio load latencije 182 ms i 105 ms. Namjerno prerani drugi
load vraćen je s `409 Library changed or load busy`, nakon čega je uredno prošao
kad je prvi load završio. Oba decka zatim su radila istodobno uz PFL D1. Tijekom
smokea pozicije oba decka napredovale su, MAIN peak bio je nenulti, a PCM
underrun, output-late, USB headphone dropped blocks, overflow, packet failures i
lost frames ostali su nula. `data_loss` je ostao false. Operator je potvrdio
čist zvuk bez prekida na MAIN izlazu i u slušalicama/PFL D1.

Validation reboot matrica nije završena kao 10/10. Service journal izravno
potvrđuje uredne `SW` bootove 493, 494, 495, 496, 502, 503 i 504. Bootovi 501 i
521 zabilježeni su kao `POWERON`. NVS boot brojač preskače zapise 497-500 i
505-520; to znači da su ti pokušaji barem povećali boot ID, ali journal nema
dovoljno podataka za dokaz reset razloga ili pune inicijalizacije. Tijekom
automatizacije Wi-Fi AP bio je privremeno nedostupan i potreban je bio fizički
power-cycle. Zato je rezultat matrice PARTIAL i incident ostaje otvoren.

## 5. Otvoreni acceptance gateovi

Prije production-ready tvrdnje treba na točno commitiranom i hashiranom kandidatu:

1. Izvesti pull OTA i ponoviti kontroliranu validation reboot matricu uz serijski
   log ili drugi dokaz za svaki boot. Potrebno je najmanje deset uzastopnih
   `SW` ciklusa bez nestanka AP-a, praznina u journalu ili novog panic dumpa.
2. Razjasniti zašto NVS boot brojač ima praznine 497-500 i 505-520 te zašto je
   tijekom matrice AP postao nedostupan. Ne zatvarati OTA reboot incident samo
   na temelju sedam vidljivih prolaza.
3. Na stvarnom Rekordbox exportu potvrditi cue A/C i loop vremena te dva medija
   s istim numeričkim track ID-jem, uključujući remount/reboot.
4. Izmjeriti catalog import i cold/warm LOAD latenciju s novim PDB hashom te
   timeline critical-section maksimum i prisiljeni wrap/handoff raspored.
5. Provesti desktop/telefon web smoke, sporu mrežu, dva klijenta i fizički MAIN
   meter decay.
6. Dual-deck FLX4 MAIN/cue smoke je prošao; preostaje najmanje 180 minuta
   završnog soaka s Master Tempo, seek/cue/loop/scratch/censor scenarijima.
7. Tek nakon tih dokaza odrediti release verziju, push/PR, immutable tag
   i javni OTA rollout. M2.2 tag i objavljeni artefakti ostaju nepromijenjeni.
