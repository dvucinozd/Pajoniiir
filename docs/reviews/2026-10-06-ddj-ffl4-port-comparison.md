# PAJONIIIR-M3 naspram DDJ-FFL4 — pregled porta i plan nastavka

Datum: 2026-10-06. Opseg: pregled koda i dokumentacije, usporedba lokalnih
repozitorija, host regresije i buildovi. Firmware nije mijenjan ni instaliran.

## Zaključak

**M3 jest funkcionalan port velikog dijela zajedničke P4 DJ jezgre, ali nije
funkcionalno izjednačen s aktualnim DDJ-FFL4 projektom.** Razlika ima tri uzroka:

1. Namjerne prilagodbe drugoj ploči, zaslonu, USB portovima i napajanju.
2. Ranije prepoznati, ali nedovršeni prijenosi dijagnostike, controller profila
   i dijela USB reliability mehanizama.
3. Noviji razvoj izvora: precizniji transport, Rekordbox parseri, playliste,
   artwork, memory cueovi, trajni identitet medija i dodatni board target.

Osnovni playback, mixer, efekti, scratch, Master Tempo, FLX4 MIDI/LED/UAC,
Wi-Fi, lokalni web UI i potpisani OTA nisu izostavljeni. Međutim, pregled je
otkrio i konkretne greške: pogrešan PDB naslov, pogrešan downbeat te preskakanje
standardnog PCPT Hot Cue zapisa. Sve tri razlike reproducirane su izvršavanjem
produkcijskih modula obaju projekata na istim sintetičkim ulazima.

Najveći prioritet imaju izolacija OTA paketa prema M3 hardveru, izolacija
trajnih cueova između knjižnica i zaštita decka koji svira od zamjene trake.
Vizualne nadogradnje i proširenje na druge kontrolere dolaze nakon toga.

## 1. Točna baza usporedbe

| Stavka | PAJONIIIR-M3 | DDJ-FFL4 / Pajoniiir |
| --- | --- | --- |
| Checkout | `D:\AI\PAJONIIIR-M3` | `D:\Documents\DDJ-FFL4` |
| Pregledani HEAD | `e95417c4e2fea007d2c1dcb693790914692c62ba` | `9af99cd234e775521f3ec83c0104f5c6a0c72920` |
| Stanje checkouta | `codex/overall-review-fixes` | detached HEAD |
| `git describe` | `M3-51-beta.1-9-ge95417c` | `M2.5-1-g9af99cd2` |
| Remote `master`, provjeren `git ls-remote` | `b3e2bee5ded0a836906ab6f689d79a6e6b49d541` | jednak pregledanom HEAD-u |
| Remote review grana | jednaka pregledanom HEAD-u | nije predmet pregleda |
| Objavljeni / rollback zapis u lokalnim dokumentima | `M3-51-gafb2099` | M2.5, frozen source `20f1c3f04a615209ae25e9bbdae649d0f5b44e8d` |
| Posljednji zapis prihvaćenog M3 bench imagea | `M3-51-beta.1-6-g483063f`, `ota_1` | zasebna ploča; rezultat se ne prenosi na M3 |

Oba stabla bila su čista na početku pregleda. Remote SHA-ovi provjereni su bez
promjene grana. Nije provjeravan živi OTA kanal niti aktualno stanje uređaja.
Podaci o objavljenim ili instaliranim slikama ovdje su podaci lokalnih release
i validation zapisa, a ne današnja fizička opažanja.

**M3 `master` zaostaje šest commitova za pregledanom granom.** To uključuje
`b0ea030` lifecycle/controller/library popravke, `ccebdb9` FIR reuse,
`483063f` stabilni 48-kHz shared output, prihvat tog clocka, reliability monitor
i zapis 60-minutnog soaka. Novi rad treba temeljiti na pregledanom stanju;
sam prelazak na današnji `master` vratio bi stariju implementaciju.

Izvori statusa:
[M3 status](../DOCUMENTATION_STATUS.md),
[M3 remediation](2026-09-07-review-remediation.md),
[DDJ status](D:/Documents/DDJ-FFL4/docs/DOCUMENTATION_STATUS.md),
[DDJ integracijski ledger](D:/Documents/DDJ-FFL4/docs/FORK_IMPROVEMENTS.md).

### Metoda i granice

Uspoređeni su Git-tracked izvori, javni API-ji, build konfiguracija,
runtime putovi za load/cue/USB/OTA, test runneri i datirani acceptance zapisi.
Generirani buildovi i managed komponente nisu korišteni kao popis funkcija.
Nedostatak datoteke sam po sebi nije proglašen nedostatkom funkcije: primjerice,
M3 `p4_flx4_host` pokriva više funkcija koje izvor raspoređuje u više komponenti.

Brojke inventara na pregledanim SHA-ovima:

| Mjera | M3 | DDJ |
| --- | ---: | ---: |
| Tracked datoteke u cijelom repozitoriju | 627 | 987 |
| Direktoriji komponenti pod `main-deck-p4/components` | 24 | 34 |
| Direktoriji pod `tests` | 96 | 134 |
| `==> run` markeri izvršenog host runnera | 84 | 137 |
| `==> static` markeri izvršenog host runnera | 246 | 265 |

Među 239 zajedničkih C/H putanja pod `main-deck-p4`, 143 su tekstualno jednake
nakon normalizacije line endingova, a 96 se razlikuje. To dokazuje veliku
zajedničku bazu i značajno razilaženje; **nije postotak završenosti porta**.
Brojevi test direktorija i runner markera također nisu broj neovisnih dokaza
ispravnosti. Dio runnera čini provjera izvornog teksta.

## 2. Što je preneseno i treba sačuvati

| Područje | Stvarno stanje M3 |
| --- | --- |
| Standalone dual-deck | P4 posjeduje playback, deck state, position, decode i mixer. |
| FLX4 | Izravni MIDI IN/OUT, semantic mapping, LED snapshot i UAC1 headphone put. |
| Audio formati | MP3, WAV i FLAC putovi postoje; detaljna kombinacija svih formata nije fizički testirana u ovom pregledu. |
| Mixer i DSP | Trim, EQ, channel faderi, crossfader, PFL/cue/master routing i limiter. |
| Transport | Play/pause, jednostavni CUE, jog/scratch, pitch, Master Tempo, loopovi, Beat Jump i Sync. |
| Efekti | Filter/Echo/Flanger/Delay, pad FX, Smart CFX/Fader i gapless slip Censor put. |
| Knjižnica | PDB/ANLZ, title/artist/BPM/key polja, sortiranje, paginacija i metadata cache. |
| UI | Overview, Library, Hot Cues, Settings, screensaver, touch/backlight i pet waveform zoomova. |
| Mreža | C6 ESP-Hosted Wi-Fi, SoftAP, web kontrola, mDNS/DNS i servisni STA/APSTA tok. |
| OTA | Potpisani lokalni push, pull/update politika, SHA/manifest provjere i bootloader rollback. |
| Testovi | Puni host runner, UI simulator, DSP soak i produkcijski USB lifecycle callback harness. |
| Reliability | Read-only monitor s `Observe`, `TimingSoak` i `UsbRecovery` modovima. |

M3 već ima vlastite kvalitetne popravke. `usb_storage` koristi callback koji
objavljuje željeno stanje i storage task kao vlasnika mount/unmount resursa.
Semantic input ima deferred buffer, coalescing kontinuiranih kontrola i
oporavak momentary statea nakon overflowa. Output bookkeeping koristi
try-lock i transport epoch, umjesto čekanja na engine mutex u steady-state
putu. Library HTTP već radi konzistentan snapshot generacije. Ti dijelovi
nisu prazni i ne treba ih ponovno implementirati zato što donor ima drugi naziv.

M3 ima hardverski prihvaćene prilagodbe koje treba zadržati: DSI506/FT5426,
0° PPA blit, waveform-first redoslijed, top-to-bottom dual redraw, stabilni
48-kHz PCM5102A output i posebni C6 SDIO/Settings lifecycle. Prenositi samo
opravdane promjene preko tih ugovora, uz regresije.

## 3. Stvarne funkcionalne razlike

| Funkcija / mehanizam u aktualnom izvoru | M3 | Ocjena |
| --- | --- | --- |
| Ispravljeni PDB title indeks | čita indeks 18 umjesto 17 | potvrđena greška |
| PQTZ downbeat 1–4 | tumači kao faze 0–3 | potvrđena greška |
| Standardni tagged PCPT cue import | čita pogrešne byte pozicije | potvrđena greška |
| Odvojeni memory cueovi i memory loopovi | nema odvojene kolekcije/API-ja | nedostaje |
| Uvezene Hot Cue točke u istom pad action putu kao lokalne | UI prikaz i lokalni pad store nisu potpuna zajednička banka | nepotpuno |
| Per-slot local override / deletion tombstone / restore source | cijeli lokalni `valid_mask` overlay | slabiji model |
| Persistent media identity | key je prvenstveno Rekordbox `track_id` | nedostaje izolacija knjižnica |
| LOAD LOCK, default ON | postoji worker/single-flight lock, nema operatorske playing zabrane | nedostaje |
| CDJ CUE press/hold/release i PLAY handoff | CUE vraća na cue i pauzira | pojednostavljeno |
| VINYL/CDJ jog mode | scratch postoji, zaseban `jog_cdj_mode` ne postoji | nedostaje noviji mode |
| Točniji MP3 seek/frame index/PVBR validation | raniji PVBR/estimate put | zaostaje |
| Odvojeni analysis span i dekodirana dužina | jedan metadata time base za raniji put | zaostaje |
| Cue preroll s rezervom za decoder batch | rezervira do cijelog forward capa | rizik zastoja |
| Decoder-owned loop prefix i resize plan | osnovni loopovi i postojeći popravci | zaostaje u naprednim prijelazima |
| Hijerarhijske Rekordbox playliste/folderi i redoslijed | flat track Library | nedostaje |
| Cover artwork, bounded JPEG worker, thumbnail cache i fallback logo | nema aktualnog artwork sustava | nedostaje |
| PWV4 color preview | PWAV/PWV3 postoje; PWV4 nije u parseru | nedostaje |
| Controller profili S3CP v2/v3/v4 | hardcoded FLX4 mapper | nedostaje |
| Upload/compiler/profile manager i connection-epoch activation | nema tog sustava/API-ja | nedostaje |
| Durable held-state reconciler i absolute replay | M3 ima overflow recovery, nema punog donor mehanizma | djelomično različito |
| Shared USB manager, topology/recovery arbiter i detaljni brojači | stariji host ownership bez cijelog paketa | selektivni kandidat |
| MSC byte bound 8 KiB i driver teardown/idle patch | do 64 sektora; samo FIFO source patch | kandidat za provjeru |
| Persistent audio-WDT i library-load journali | runtime log/counters i coredump opcija | nedostaje |
| Startup-ready čekanje za traženi AP/HTTP prije OTA potvrde | potvrđuje nakon pokretanja asinkronih servisa | slabiji gate |
| Allocation failure / stack / internal-DMA largest-block budgeti | postoje osnovni heap i phase brojači | slabija telemetrija |
| Board adapter i JC1060 target | M3 BSP je vlastita izvedba | arhitektonska nadogradnja, ne obvezna funkcija |
| Pro DJ Link / Ethernet NFS / network sync | nema; Ethernet je namjerno isključen | zaseban razvojni smjer |
| Recorder | komponenta i API postoje, product default OFF | nije izostavljena produkcijska funkcija |

## 4. Prioritetni nalazi s dokazima i prijedlogom popravka

P1 ovdje znači mogućnost zamjene pogrešnog firmwarea, prekida reprodukcije ili
narušavanja izolacije trajnih podataka. P2 znači potvrđenu funkcionalnu grešku
ili važan nedostatak razvojnih provjera. Feature razlika sama po sebi nije bug.

### P01 — P1: OTA ne razlikuje M3 od JC4880 slike

Oba entrypointa zovu se `main-deck-p4`; M3 uploader i image provjera očekuju taj
isti naziv. Oba repozitorija imaju jednak javni release ključ, SHA-256
`125EF6EB776729E3018ECC564317254FEBEC4A9A2BF34892E99D57F10BC02E43`.
Manifest provjerava target/chip/project/size i potpis, ali ne identitet M3 ploče.
Lokalni push služi i kao rollback put, pa se ne može osloniti na newer-only
pull politiku kao zaštitu od zamjene proizvoda.

Posljedica: potpis i naziv projekta sami ne dokazuju kompatibilnost s M3 BSP-om,
USB routingom i GPIO-ima. Postojeći kriteriji nemaju eksplicitnu logičku zabranu
cross-board paketa. Stvarna instalacija pogrešnog paketa nije pokušavana;
zaključak se odnosi na admission provjere, ne na potvrđen cross-flash incident.

Dokazi: M3 `p4_ota.c:27,210`, `web_server.c:461`, oba `CMakeLists.txt` i oba
`firmware/common/ota_manifest/keys/ddj_ota_release_public.der`.

Prijedlog: jedinstveni M3 project/board compatibility identitet kroz build,
signed manifest, image descriptor, push/pull validaciju i packaging. Predvidjeti
jedan prijelazni M3 updater jer stari image trenutno odbija novi project name.
Prijelaz ne smije trajno dopustiti sve stare `main-deck-p4` pakete. Acceptance:
validni M3 update/rollback prolaze; JC4880 i JC1060 paketi odbijaju se prije
promjene boot particije.

### P02 — P1: Hot Cue podaci nisu izolirani između USB knjižnica

`library_track_key()` vraća `track_id` ako nije nula, a NVS ključ je
`hc%08lx`. Putanja/hash koristi se samo kao fallback kada `track_id` ne postoji.
Generacija kataloga štiti aktualne zahtjeve, ali nije dio trajnog NVS identiteta.

Primjer: traka ID 123 s medija A i sasvim druga traka ID 123 s medija B čitaju
isti lokalni Hot Cue zapis. To je izravna posljedica key ugovora; u ovom pregledu
nije provedena fizička zamjena dvaju USB medija. Metadata cache ima provjere
size/mtime i ne treba ga poistovjetiti s jednostavnijim NVS cue ključem.

Dokazi: M3 `library.c:174`, `hot_cue_store.c:52,142`, `deck_core.c:698`.
Donor: `media_identity`, `hot_cue_store`, `media_catalog` i `track_meta_cache`.

Prijedlog: odvojiti session-local row key od persistent identity; koristiti
digest izvoza + relativnu putanju + obilježja datoteke prema provjerenom donor
ugovoru. Migrirati stari NVS format samo kada je pripadnost zapisa dokaziva;
ambigvitet ne smije automatski preseliti cue na novu traku. Acceptance: različite
knjižnice sa zajedničkim `track_id` ostaju izolirane, a isti medij nakon reboota
zadržava svoje cueove.

### P03 — P1: nema operatorskog LOAD LOCK-a

M3 `ui_library_load_track_identity_for_deck()` provjerava key/generation/deck i
single-flight admission. Worker zatim resetira deck i učitava novu traku.
Nema provjere zabrane zamjene decka koji svira. `s_track_load_lock` je
synchronization primitive za worker state, a ne DJ LOAD LOCK.

Dokazi: M3 `ui_library.c:650,1871`, FLX4 LOAD put u `deck_core.c` i web
`/api/load` koji koristi isti Library admission. Donor ima `deck_load_lock`,
`deck_core_load_allowed` i ponovnu provjeru u asinkronom workeru.

Prijedlog: default ON postavka te provjera touch/FLX4/shifted/web ulaza i
provjera neposredno prije destruktivnog worker reset/load koraka. Acceptance:
D1 PLAY + LOAD odbija zamjenu i ne prekida D1 zvuk; D2 STOP + LOAD prolazi;
PLAY koji stigne između admissiona i workera također mora biti pokriven.

### P04 — P2: standardni PCPT Hot Cue zapis tiho se preskače

M3 `parse_pcob()` čita `entry_type=buf[0]`, `index=buf[1]` i vremena iz `buf[4]`
/ `buf[8]`. Tagged 56-byte PCPT zapis na početku ima `PCPT` i veličine; donor
čita hot-cue number iz offseta 12, tip iz 28, a vremena iz 32/36. M3 zato vidi
`index='C'`, odbaci zapis kao out-of-range i ipak vrati uspjeh.

Usporedni probe: isti DAT s PPTH i jednim Hot Cue A na 1234 ms dao je
`M3: rc=0 cue_count=0`, `DDJ: rc=0 cue_count=1 first_cue_ms=1234`.
Postojeći M3 `tests/anlz/test_anlz.c` koristi staru pojednostavljenu cue
strukturu, pa suite potvrđuje vlastitu pretpostavku umjesto standardnog layouta.

Prijedlog: standardni PCOB/PCPT parser, odvojene memory i hot-cue liste,
count/length/type provjere te fixtureovi s pravim tag/header poljima. Nakon toga
ujediniti uvezene cueove i lokalne per-slot overrideove, uključujući loop cue
recall, deletion tombstone i restore source. Samo prikaz točke na touch UI-ju
ne dokazuje da fizički pad radi s istom bankom.

### P05 — P2: downbeat i beat dot faza pomaknuti su

PQTZ parser sprema sirovu fazu; M3 koristi `%4` i downbeat na nuli. Aktualni
donor koristi PQTZ broj beata 1–4, downbeat 1, te UI indeks `phase-1`.

Probe produkcijskih `ui_beat_indicator` i `ui_overview_grid` modula:

| PQTZ faza | M3 downbeat / line width | DDJ downbeat / line width |
| --- | --- | --- |
| 1 | false / 1 px | true / 2 px |
| 4 | true / 2 px | false / 1 px |

Dokazi: M3 `ui_beat_indicator.c:172`, `ui_overview_grid.c:50` i oba grid puta
u `ui_overview_renderer.c`. Popravak mora obuhvatiti sve konzumente i test
fixtureove. Fallback bez ANLZ grid-a smije zadržati svoj interni 0–3 model;
ne treba globalno pretvoriti svaki interni phase u 1–4.

### P06 — P2: PDB title čita pogrešan indeks

M3 `STR_IDX_TITLE=18`; donor `STR_IDX_TITLE=17`. Isti PDB fixture s različitim
validnim stringovima na 17/18/19 daje `M3: title=WRONG INDEX 18`,
`DDJ: title=Tagged title`. Postojeći filename fallback ne pomaže kada je
string na pogrešnom indeksu neprazan i valjan.

Dokazi: M3 `rekordbox_pdb.c:104,531`; donor title fixture u `test_pdb.c:110`.
Prijedlog: prenijeti korekciju i independent-field fixtureove, uključujući
UTF-16/non-ASCII i prazan/nevaljan naslov. Zatim usporediti s pravim exportom.

### P07 — P2: seek/cue/duration put zaostaje; preroll ima konkretan rizik

M3 PVBR admission prihvaća tablicu čim bilo koji entry nakon prvog nije nula.
Nema donor provjere monotoniciteta, file bounds, ID3-relative origin/frame
alignment i potpunog MP3 frame indexa. Seek pozicija u API-ju zato nije dovoljan
dokaz položaja zvučnog onseta.

Preroll rezervira do 2000 ms forward PCM-a, dok dekoder prije sljedećeg batcha
traži prostor za puni `MINIMP3_MAX_SAMPLES_PER_FRAME`. Na 44,1 kHz cap je
88200 frameova; 76 batchova po 1152 daju 87552, a preostalih 648 nije dovoljno
za sljedeći batch. Preroll target tako može ostati nedostignut. To je analiza
firmware grane i batch granice, ne fizički reproduciran zastoj u ovoj sesiji.
Donor `audio_cue_preroll` ostavlja rezervu za batch i rješava kratki EOF.

Dokazi: M3 `audio_engine.c:638,2694,2758,3941`. Donor moduli:
`audio_pvbr_validation.h`, `audio_seek_skip`, `audio_track_length`,
`audio_cue_preroll`, `audio_loop_prefix`, `audio_loop_resize`.

Prijedlog: odvojeni mali paketi: PVBR validation; preroll admission/EOF;
MP3 seek/frame index; session-bound duration uz fixed analysis span;
loop prefix/resize. M3 FIR/MT/output-clock ugovore zadržati i testirati.
Acceptance uključuje PCM onset i stvarne MP3/WAV/FLAC CUE/loop prijelaze,
ne samo položaj iz `/api/status`.

### P08 — P2: OTA image potvrđuje se prije završetka traženog mrežnog starta

M3 poziva `wifi_link_request_enable(true)` i inicijalizira USB putove, zatim
na kraju `app_main()` poziva `firmware_health_mark_ready()`. Wi-Fi/web start
je asinkron; završetak `app_main()` ne dokazuje da je traženi AP/HTTP dostupan.
Osnovni rollback postoji i ne treba ga proglasiti izostavljenim.

Dokazi: M3 `app_main.c:433,446`; donor pending-image petlja s
`p4_startup_gate` i `firmware_resources` prije potvrde slike.

Prijedlog: prenijeti bounded pending-image readiness politiku, prilagođenu
M3 C6 lifecycleu i spremljenom Wi-Fi stanju. Provjeriti timeout/rollback i
allocation failure scenarije. Ne zahtijevati umetnuti FLX4 ili USB medij za
svaki boot bez definirane product politike.

### P09 — P2: push CI ne obuhvaća `codex/**`

Workflow pokriva `master`, `fix/**`, `test/**`, `docs/**`, `ci/**` i
`migration/**`, a AGENTS propisuje `codex/` i aktualna review grana koristi
upravo taj prefiks. Push na nju nije obuhvaćen tim filterom. Pull-request i
manual triggeri postoje; nalaz ne znači da CI nikad ne može provjeriti granu.

Dokaz: `.github/workflows/esp-idf-6-migration.yml:5` i lokalne/remote grane.
Prijedlog: dodati `codex/**` ili sve razvojne grane te provjeriti stvarni run
na točnom SHA-u. Ne zatvarati ovaj gate samo zato što je lokalni suite zelen.

### P10 — P2: plan porta i handoff dokumentacija ne opisuju aktualni cilj

Faza 9 pinana je na donor `adb63148...` od 2026-08-29. Ima dobre reference,
ali nije inventar aktualnog izvora. Na tom snapshotu profili su bili v2;
današnji parser podržava v2/v3/v4. Nove funkcije iz paketa A–L nisu u starom
port planu. S druge strane, dio ownership/queue popravaka iz plana već postoji
u M3 te ga ne treba ponovno brojiti kao potpuno izostavljen.

`AGENTS.md` i `STARTUP_CHECKLIST.md` i dalje ističu raniji `M3-51/ota_0`
handoff, dok `DOCUMENTATION_STATUS.md` i timing validation navode noviji
bench kandidat `483063f/ota_1`. Oba zapisa imaju vrijednost, ali rollback
baseline, reviewed source i posljednja instalacija moraju biti jasno odvojeni.

Prijedlog: nova commit-pinned parity matrica s oznakama implemented/tested/
physical/not-applicable; ažurirati aktivni handoff i reference bez brisanja
datirane povijesti. Današnji uređaj prvo očitati prije promjene installed statea.

## 5. Što nije opravdano proglasiti propuštenim portom

- JC4880 ili JC1060 display/touch init, pinout, FS PHY i power routing ne smiju
  zamijeniti M3 BSP. M3 DSI506 i FT5426 već imaju vlastiti fizički acceptance.
- S3/UART/inter-board PCM bridge nije dio aktualnog izvora ni cilja M3.
- Pro DJ Link živi u dodatnom JC1060/Ethernet targetu, default OFF; JC4880
  production konfiguracija nema Link provider. M3 Ethernet je namjerno OFF.
  Prenošenje Linka zahtijeva novu mrežnu/hardversku odluku i real-peer gate.
- USB MAIN sink i alternativni controller audio formati korisni su za druge
  kontrolere. M3 PCM5102A MAIN + FLX4 cue već zadovoljava prihvaćeni cilj.
- Recorder je default OFF i u aktualnom izvoru. Sama prisutnost koda/API-ja
  nije produkcijski acceptance snimanja niti obvezna parity stavka.
- `dj_ui` preview nije automatski cilj redesign-a. Izvor zadržava prethodni
  product design, a M3 može dobiti playliste/artwork bez zamjene cijelog UI-ja.
- DDJ-400/Hercules/NI/generic profili u stablu dokazuju softversku
  infrastrukturu i fixtureove; ne dokazuju fizičku podršku tih uređaja na M3.
- Coredump i log postoje u M3. Nedostaje kompaktniji reboot-persistent journal,
  a ne sva post-mortem dijagnostika. `.noinit` zapis nije trajni diskovni log
  koji pouzdano preživljava prekid napajanja.

## 6. Predloženi nastavak

### Paket 0 — Učvrstiti bazu i ugovor porta

Polazište M3 je `e95417c`, ne stariji `master`. Sačuvati poznati rollback
artifact, očitati actual installed version/SHA/slot i dovršiti postojeću
Campaign A matricu prije većih promjena USB/audio implementacije. Uskladiti CI
branch filter i dokumentaciju. Integraciju review grane u `master` provesti
kao zasebnu pregledanu odluku, bez resetiranja ili masovnog kopiranja donora.

Nova donor referenca za funkcionalni cilj može biti frozen M2.5 source
`20f1c3f...`, uz dokumentacijski HEAD `9af99cd2...`. Svaki paket treba vlastiti
port commit, testove, M3 acceptance i jasan povratak na prethodnu sliku.

Gate: čist source/branch odnos, zeleni build/host/UI, točan installed baseline,
30-ciklusna evidencija i popis stvarno primjenjivih parity zahtjeva.

### Paket 1 — Zaštita proizvoda i dijagnostika

Riješiti P01 OTA board identity s migracijom, P03 LOAD LOCK, P08 pending
startup-ready gate; prenijeti audio-WDT i library-load journale i osnovne
allocation/stack/internal-DMA reserve brojače. Paket je moguće podijeliti u
neovisne commitove. Journali ne smiju alocirati ili raditi filesystem I/O u
output tasku. Ne uvoditi periodičke skupe PSRAM heap walkove u audio workload.

Gate: cross-board rejection, validni M3 push/pull/rollback, load/PLAY race
testovi, timeout rollback, journal invalid-magic/version testovi i normalni
dual-deck smoke bez novih strict PCM/UAC grešaka.

### Paket 2 — Rekordbox korektnost i trajni cueovi

Prvo P06 naslov i P05 beat faza. Zatim P02 persistent identity s migracijom,
P04 standardni PCOB/PCPT te memory cueovi, source bank + local per-slot
overrides/tombstone/restore source. Uskladiti cache format, serializer i sve
clone/free putove kada se ANLZ struktura proširi.

Gate: independent binary fixtureovi i real export comparison; dva medija s
istim track ID-om; uvoz Hot Cue A i loop cuea; set/delete/reload/reboot;
obnova izvornog cuea i oba decka. Fizički pad i touch moraju koristiti isti
action model. Migraciju persistent statea testirati i pri interrupted writeu.

### Paket 3 — Transport i preciznost zvuka

PVBR bounds i MP3 frame seek, preroll batch rezerva, measured duration odvojena
od analysis span, decoder-owned loop prefix/resize. Nakon toga CDJ CUE
press/hold/release i VINYL/CDJ jog selector. Ne mijenjati sve odjednom:
transport state, timeline i audible onset moraju ostati odvojeno provjerljivi.

Gate: MP3/WAV/FLAC, 44,1/48 kHz i mixed-rate; nenulti CUE i immediate PLAY;
kratki EOF; loop IN/OUT/half/double/exit; scratch i MT; 300-s host soak;
PCM onset provjera i fizički MAIN/PFL/UI acceptance točno instalirane slike.

### Paket 4 — Knjižnica koju operator vidi

Prenijeti PDB playlist tree/entries/artwork, library/catalog API-je, folder
navigaciju i playlist order, zatim artwork worker/JPEG thumbnails i PWV4.
Sačuvati aktualni 800×480 UI i M3 waveform schedule. Primjenjivati generation
fencing na delayed cover response i bounded PSRAM cache.

Gate: folder/back/browse/load D1/D2; pravi redoslijed izvoza; missing/corrupt/
delayed cover; stale cover nakon reloada i zamjene medija; očuvani internal/DMA
largest blocks; sve waveform zoom razine pod dual-deck playbackom i Wi-Fi.
Update screenshot baselinea tek nakon vizualnog pregleda novog očekivanog UI-ja.

### Paket 5 — USB hardening po mjerenju

Dovršiti Campaign A baseline, usporediti manager/recovery arbiter i driver
teardown/idle patch ugovore. M3 sole-owner mount model već postoji. Trenutni
bound od 64 sektora jest bound, ali na 512-B sektorima dopušta 32 KiB;
donor koristi byte-bound 8 KiB. Mjeriti oba pristupa na M3 USB3 prije promjene.

Gate: driver callback ownership, bounded reads/writes, FAT32/exFAT, oba reda
enumeracije, odvojeni FLX4/MSC reconnect, 191-track library, LED i audio counter
delte. Root recovery mora znati stvarni M3 root; USB oznake na konektoru nisu
driver indeksi. Ne prenositi power-cycle ponašanje bez tog mapiranja.

### Paket 6 — Multi-controller, ako je dio željenog proizvoda

Portati aktualni S3CP v2/v3/v4 skup: compiler, parser, runtime, manager,
storage/upload validator, LED runtime, event/held-state reconciliation i
capability-driven UAC. Dodati M3 board/root adapter, zadržati fallback samo za
točno FLX4 VID/PID. `S3CP` ime ne znači da treba vratiti S3 procesor.

Gate: profil i ugrađeni FLX4 mapping daju jednake semantičke rezultate;
invalid CRC/version/length/VID-PID i interrupted upload ostavljaju prethodni
profil; reconnect ne aktivira profil starog epocha. Za svaki novi stvarni
kontroler zaseban MIDI IN/OUT/LED/reconnect/UAC fizički acceptance.

### Paket 7 — Dugoročno smanjiti razilaženje jezgre

Nakon malih portova odlučiti hoće li M3 ostati samostalni repo s pinanim
adoption paketima ili postati dodatni board target zajedničke P4 jezgre.
Drugi izbor dugoročno smanjuje ponavljanje popravaka, ali zahtijeva novi M3
board descriptor, C6/SDIO lifecycle adapter, 800×480 DSI/PPA schedule i odvojeni
product/OTA identitet. Današnji donor adapter poznaje JC4880/JC1060, ne M3.

Preporuka: prvo mali portovi i prihvat ponašanja; zatim shared-core odluka.
Pro DJ Link, Ethernet i recorder ostaviti zasebnim razvojnim zahtjevima.

## 7. Fizička evidencija koju treba poštovati

M3 zapis od 2026-09-21 dokumentira 3600 s mixed-rate dual-deck MT workloada na
`M3-51-beta.1-6-g483063f`: 13805 valjanih status uzoraka, nulte strict
PCM/UAC/service-log delte i operator-confirmed uredan zvuk, zaslon i kontrole.
Imao je 64 output-late događaja, maksimum 12074 us i klaster šest/minutu.
To ostaje monitoring baseline; nije zero-late rezultat. Nije opravdano mijenjati
audio/USB samo radi tih događaja bez ponovljivog funkcionalnog problema.

Campaign A i dalje traži 10 cold bootova, 10 FLX4 reconnecta sa zaustavljenim
deckovima, 5 USB3-first i 5 FLX4-first ciklusa. Prema aktualnom planu 15 s za
FLX4 i 20 s za library su investigation pragovi; odsutnost oporavka, stale
LED, pogrešna knjižnica, reset/WDT, strict loss ili ručna intervencija su hard
failure uvjeti. Ne dodavati active-removal workload u tu matricu bez zasebnog
definiranog testa.

Donor lokalni release zapis navodi M2.5 i fokusirani final-image smoke uz
izričito waived novi 180-minutni soak. Povijesni v91 soak ima prihvaćenu
segmentiranu iznimku. To nije M3 hardverski dokaz niti uninterrupted soak
aktualnog M3 porta. Nova funkcija dobiva fizički PASS samo na svojoj slici.

## 8. Provjere izvršene u ovom pregledu

| Provjera | Rezultat |
| --- | --- |
| M3 `tests/run_p4_host_tests.ps1` | PASS, exit 0 |
| DDJ `tests/run_p4_host_tests.ps1` | PASS, exit 0 |
| M3 `idf.py --version` | ESP-IDF v6.0.2 |
| M3 `idf.py build` | PASS, exit 0; image 2395904 B, 43% slobodno u 4-MiB app particiji |
| DDJ `idf.py --version` | ESP-IDF v6.0.2 |
| DDJ `idf.py build` | PASS, exit 0; image 2576096 B; binary budget PASS |
| M3 UI simulator | PASS, scripted navigation + 7/7 screenshot hashova; baseline nije mijenjan |
| M3 Master Tempo host soak | PASS, 300 virtualnih sekundi; zero sample drift, zero clipping, zero click detections |
| PCPT parity probe | produkcijski M3 parser preskače cue, produkcijski DDJ parser ga uvozi |
| PQTZ parity probe | produkcijski M3 naglašava fazu 4, DDJ fazu 1 |
| PDB title parity probe | M3 bira `WRONG INDEX 18`, DDJ `Tagged title` |
| Remote branch SHA | očitani s `git ls-remote`; gore navedena stanja potvrđena |
| `git diff --check`, status i lock promjene | PASS; oba dependency locka nepromijenjena; jedina izmjena je novi izvještaj u M3 |

Host runneri preskaču opcionalni pravi MP3 decode kada putanja datoteke nije
zadana te pravi PDB import kada nema PDB argumenta. To nije puni real-library
ili audible acceptance. UI screenshot PASS potvrđuje podudaranje s postojećim
fixture/baseline modelom; taj model može sadržavati pogrešnu beat/cue pretpostavku.

Nisu pokrenuti: flash/push/pull OTA, cross-board upload, živi device API,
fizički audio/touch/USB testovi, Campaign A, novi hardware timing soak, donor
JC1060 build i donor UI simulator. Pregled ne mijenja acceptance tih gateova.

Logovi i mali probe ulazi/harnessevi su u lokalnom
`%TEMP%\Pajoniiir-port-review-20261006`. Reprodukcija probeova kompilira samo
`rekordbox_anlz.c`, `ui_beat_indicator.c`, `ui_overview_grid.c` odnosno
`rekordbox_pdb.c`, s njihovim standalone defineovima, istim GCC-om i istim
ulaznim datotekama za oba projekta. Ne koristi zamjensku implementaciju parsera.

Buildovi su obični lokalni `idf.py build` runovi, bez brisanja postojećeg
build/config stanja. Nisu potpisani release artifacti niti dokaz identičnosti
s ranije instaliranim slikama. Build size usporedba ne dokazuje real-time
headroom, slobodan internal/DMA heap ili fizičku audio kvalitetu.

## 9. Pinane donor reference za nastavak

Sljedeće reference odgovaraju pregledanom donor HEAD-u, ne pomičnoj grani:

- [Audio seek/preroll/loop integration](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/audio_engine/audio_engine.c)
- [PDB parser](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/library/rekordbox_pdb.c)
- [ANLZ parser](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/library/rekordbox_anlz.c)
- [Media identity contract](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/media_identity/include/media_identity.h)
- [Persistent source/local cues](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/hot_cue_store/hot_cue_store.c)
- [Deck transport/load admission](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/deck_core/deck_core.c)
- [Library UI, playlists and artwork integration](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/ui/ui_library.c)
- [Controller profile v2/v3/v4 contract](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/controller_profile/include/controller_profile.h)
- [USB recovery arbiter](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/main-deck-p4/components/usb_host_manager/usb_host_recovery_arbiter.c)
- [Pending startup readiness](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/firmware/common/firmware_health/p4_startup_gate.c)
- [Integration/provenance ledger](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/docs/FORK_IMPROVEMENTS.md)
- [M2.5 exact release scope](https://github.com/dvucinozd/Pajoniiir/blob/9af99cd234e775521f3ec83c0104f5c6a0c72920/docs/validation/M2_5_RELEASE_20261006.md)

Pri prijenosu sačuvati module-specific MIT/Apache-2.0 attribution i NOTICE
datoteke. Novi M3 port ne nasljeđuje donor fizički PASS samo zato što je kod
identičan. Faza 9 ostaje korisna povijesna referenca, ali novi rad treba voditi
prema ovoj matrici i točnim budućim port commitovima.
