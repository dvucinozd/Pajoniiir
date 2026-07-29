# ESP-IDF 6.0.2 migration — code review i remediation plan

Datum pregleda: **2026-07-29**

Pregledani commit:
`f8d60836b559dc7bf1191627584bb9a5239eb3b5`
(`origin/migration/esp-idf-6.0.2`)

Naknadno revalidirani master:
`65ecc56357ea436087fa61d841367b7357fdc557`
(`origin/master`, 2026-07-29)

Vrsta pregleda: read-only statički i dinamički pregled firmwarea, host testova,
CI-ja, konfiguracije i aktivne dokumentacije.

## Sažetak

ESP-IDF 6.0.2 migracija uspješno se konfigurira i builda za oba targeta, a
postojeći S3/P4 host testovi, petominutni dual-deck Master Tempo soak i UI
simulator prolaze. Migracija je software-integrirana, ali još nije
production-ready.

Pregled nije pronašao potvrđeni P0 build ili boot blocker. Pronađena su tri P1
funkcionalna/concurrency problema:

1. disconnect sekundarnog MSC uređaja može ukloniti aktivni USB uređaj;
2. control-link button/state edge događaj i dalje se može odbaciti nakon
   bounded timeouta, a postojeći test ne puni queue;
3. UI, USB i deck taskovi dijele nekonzistentno library/loaded-track stanje bez
   odgovarajuće sinkronizacije.

Uz njih ostaju P2 rizici u ANLZ snapshot kopiranju, globalnom audio locku,
OTA/monitor statusu, USB recovery lifecycleu, parcijalnoj inicijalizaciji i
production sigurnosti.

Revalidacija nakon naknadnih mergeova u `origin/master` pokazala je:

- nijedan od tri P1 nalaza nije popravljen;
- popravljen je P2 dio audio/media bottlenecka: library više ne drži
  `media_io_gate` kroz cijeli PDB catalog walk;
- globalni audio lock nije promijenjen, ali je dodan detaljan deferred plan;
- security rizici nisu mitigirani, ali su sada eksplicitno dokumentirani;
- behavior testovi i test runner su znatno poboljšani;
- CI branch triggeri su popravljeni, dok Docker/action pinning ostaje otvoren.

Naknadni remediation status istoga dana:

- P1-1 USB accepted-device/session ownership je software zatvoren;
- USB recovery lifecycle je vezan uz session epoch i ponovno se armira nakon
  disconnecta;
- P1-3 deck/library ownership i lossy UI event zastavice su software zatvoreni;
- P1-2 durable control-link state reconciliation je software zatvoren na S3 i
  P4; fizički reconnect/saturation smoke ostaje otvoren jer hardware nije
  dostupan;
- sva tri P1 nalaza time su software zatvorena, ali Milestone A nije hardware
  prihvaćen.

## Odnos prema ranijem remediation auditu

`docs/fixevi-remediation-audit.md` je započeo kao audit ranije remediation
grane i sada služi kao zbirni status popravaka i preostalih gateova. Ovaj
dokument zadržava nalaze na točno navedenom migration commitu te uz njih
bilježi naknadni implementacijski status.

Posebno, raniji dokument označava gubitak control queue release događaja kao
zatvoren za primarni defekt. Aktualni pregled potvrđuje da implementacija i
dalje ima bounded timeout nakon kojeg vraća neuspjeh i bilježi drop. Test
nazvan kao full-queue provjera puni queue s dva događaja dubine dva, ali ne
šalje treći događaj koji bi izvršio timeout/drop put. Zbog toga se taj nalaz u
ovom dokumentu ponovno vodi kao P1 dok se ne uvede durable state
reconciliation ili drugi mehanizam koji garantira konačno stanje.

## Master revalidacija nakon behavior-gate rada

Usporedba `f8d60836` s `origin/master@65ecc563` obuhvatila je sve putanje
povezane s nalazima. Izravna firmware promjena relevantna ovom auditu postoji
samo u `library.c`.

| Područje | Status na `origin/master@65ecc563` | Dokaz |
| --- | --- | --- |
| USB disconnect ownership | **Otvoreno** | `usb_storage.c` je identičan auditiranom migration commitu |
| Control-link edge delivery | **Otvoreno** | implementacija i problematični test su identični |
| UI/library/deck shared state | **Otvoreno** | `ui_library.c` i `deck_core.c` nisu promijenjeni |
| ANLZ deep-copy snapshot | **Software zatvoreno; HW soak pending** | immutable/versioned refcount snapshot; reader acquire više ne alocira |
| Library `media_io_gate` scope | **Zatvoreno u sourceu; HW pending** | gate se otpušta između PDB redaka; dodani behavioral testovi |
| Globalni audio lock | **Otvoreno; planirano** | dodan `AUDIO_ENGINE_PER_DECK_LOCK_PLAN.md`, bez source promjene |
| OTA status concurrency | **Software zatvoreno; HW pending** | atomic operation gate i koherentan bounded status/offer snapshot |
| Monitor PCM concurrency | **Software zatvoreno; HW soak pending** | atomic enable/counters i versioned format snapshot |
| USB recovery lifetime | **Otvoreno** | `usb_storage.c` nije promijenjen |
| AP/API credential hardening | **Prihvaćen rizik, nije mitigiran** | hardkodirani `Pajoniiir` PSK ostaje |
| Secure Boot/flash encryption | **Procijenjeno, nije uključeno** | dokumentiran partition-table/bootloader blocker |
| Partial-init cleanup | **Software popravljeno; IDF failure injection/HW pending** | profile manager i monitor I2S imaju staged init i potpuni rollback |
| LRU wraparound | **Software zatvoreno** | `uint64_t` stamp i regresija preko povijesne `UINT32_MAX` granice |
| Build warning cleanup | **Otvoreno** | source i FATFS Kconfig nisu promijenjeni |
| CI branch coverage | **Zatvoreno** | workflow sada pokriva `fix/**`, `test/**`, `docs/**`, `ci/**`, `migration/**` |
| CI supply-chain pinning | **Otvoreno** | Docker tag i `actions/*@v4` ostaju mutable |
| Release dokumentacija | **Djelomično** | Risk Register je usklađen; README i Documentation Status nisu |

Aktualni P4 host suite nakon mergeova prolazi. Novi library suite izvršava 253
provjere i potvrđuje per-row media-gate scope i prekid catalog walka kada testni
mount postane nedostupan.

Control-link suite također formalno prolazi 62 provjere, ali taj PASS ne zatvara
P1 nalaz. Test nazvan `a button release survives a full queue` koristi queue
dubine dva i šalje samo press i release. Queue se tek popuni; ne šalje se treći
edge koji bi izvršio bounded-timeout/drop put.

## Opseg pregleda

Pregledani su:

- migration diff i aktualno stanje cijelog repozitorija;
- S3 control-board firmware;
- P4 playback, audio, library, UI, USB, OTA i monitoring komponente;
- shared `control_link` put;
- host testovi i njihovi fake RTOS/backend modeli;
- CI workflow i dependency lockovi;
- generirani S3/P4 `sdkconfig`;
- migration, release, risk i validation dokumentacija.

Pregled nije uključivao fizičko flashanje, mjerenje na osciloskopu/logic
analyzeru, slušni test ili dugotrajni test na P4/S3 uređajima.

## Izvršene provjere

| Provjera | Rezultat |
| --- | --- |
| ESP-IDF verzija | `ESP-IDF v6.0.2` |
| S3 host suite | PASS |
| P4 host suite | PASS |
| Dual-deck Master Tempo virtualni soak | PASS |
| UI simulator navigacija | PASS |
| Sedam egzaktnih screenshot baselineova | PASS |
| Čisti S3 build | PASS |
| Čisti P4 build | PASS |
| `git diff --check` | PASS |
| Praćene lokalne promjene nakon pregleda | Nema |
| Odstupanje od remote migration commita | `0 / 0` |

Build rezultati:

- S3 image: `0x0eb2d0`, približno 51% slobodno u najmanjoj app particiji;
- P4 image: `0x24ed80`, približno 42% slobodno u najmanjoj app particiji;
- P4 bootloader: `0x5af0`, približno 5% slobodnog bootloader prostora.

Master Tempo soak:

- svaki deck obradio je 14.400.000 virtualnih frameova;
- source drift je 0;
- frequency error je 0,156% i 0,119%;
- nisu detektirani clickovi ni clipped sampleovi;
- mixed peak je 18.752;
- host CPU vrijeme bilo je približno 19,1 sekundi.

Ovi rezultati potvrđuju software behavior u host modelima. Ne potvrđuju P4 CPU
budget, I2S deadline, USB latency, DSI/PPA ponašanje ni kvalitetu zvuka na
stvarnom hardveru.

---

## P1-1 — Disconnect nepovezanog USB uređaja može ukloniti aktivni medij

Status na `origin/master@65ecc563`: **OTVORENO**

Primarne lokacije:

- `firmware/main-deck-p4/components/usb_storage/usb_storage.c:86-117`
- `firmware/main-deck-p4/components/usb_storage/usb_storage.c:211-223`
- `firmware/main-deck-p4/main/app_main.c:262-271`

### Nalaz

Connect događaj koristi adresu uređaja i odbija drugi uređaj dok je primary
uređaj aktivan. Svaki disconnect događaj, međutim, bezuvjetno poziva:

```c
publish_desired_connection(false, 0u);
```

MSC connect događaj sadrži adresu, dok disconnect događaj identificira uređaj
handleom. Trenutačni kod ne uspoređuje disconnect handle s trenutno
prihvaćenim `s_msc_dev`.

### Reprodukcijski slijed

1. uređaj A se spoji i mounta;
2. uređaj B se spoji, ali je odbijen jer A već postoji;
3. B se odspoji;
4. disconnect B bezuvjetno briše desired state uređaja A;
5. storage task može unmountati A;
6. app callback zaustavlja playback kao da je aktivni medij uklonjen.

### Utjecaj

- neočekivani prekid reprodukcije;
- nestanak library mounta;
- pogrešno ponašanje na hubovima i pri connect/disconnect bounceu.

### Preporučeni popravak

Uvesti eksplicitni session objekt koji sadrži:

- state;
- prihvaćenu adresu;
- prihvaćeni MSC handle;
- generation/epoch;
- mount status.

Disconnect smije mijenjati primary state samo kada event handle odgovara
vlasničkom handleu. Zakašnjeli open/mount callback mora sadržavati generation
i ne smije ponovno aktivirati staru sesiju.

### Obvezni testovi

- A connect/mount/disconnect;
- A mount, B connect odbijen, B disconnect, A ostaje montiran;
- A mount, B odbijen, A disconnect, A se uklanja;
- disconnect tijekom openinga;
- zakašnjeli mount-complete nakon disconnecta;
- višestruki disconnect istog handlea;
- brzi connect/disconnect bounce;
- mount i unmount failure.

### Acceptance kriterij

App dobiva media-removed samo za trenutno aktivni session. Disconnect
sekundarnog ili ignoriranog uređaja nikada ne mijenja primary mount.

### Implementacijski status na `codex/fix-usb-storage-ownership`

Status: **SOFTWARE POPRAVLJENO; HARDWARE ACCEPTANCE OSTAJE OTVOREN**

Implementiran je zaseban, host-testabilan `usb_storage_session` state machine.
Session sada koherentno posjeduje accepted adresu, opaque MSC handle, mount
status i monoton epoch. Produkcijski `usb_storage.c`:

- prihvaća samo jedan primary session;
- veže handle tek nakon uspješnog `msc_host_install_device()`;
- ignorira disconnect čiji handle nije accepted handle aktivnog sessiona;
- prihvaća disconnect tijekom opening faze, prije nego što je handle moguće
  objaviti storage tasku;
- epochom odbija zakašnjeli bind ili mount-complete stare sesije;
- nakon mount failurea otpušta handle i dopušta retry unutar istog epocha;
- zadržava postojeći bounded exponential mount backoff.

Novi `tests/usb_storage_session/test_usb_storage_session.c` izvršava 63 provjere
i pokriva primary lifecycle, odbijeni secondary connect/disconnect, disconnect
tijekom openinga, stale completion, duplicate događaje, mount retry i stale
callback prethodne sesije. Test je uključen u puni P4 host runner.

Validacija na ovoj grani:

- `tests/run_p4_host_tests.ps1`: **PASS**, uključujući svih 63 novih provjera;
- P4 `idf.py build` na ESP-IDF v6.0.2: **PASS**;
- `main-deck-p4.bin`: `0x24ee80` bajtova, `0x1b1180` bajtova (42%) slobodno u
  najmanjoj app particiji.

MSC driver u normalnom toku ne objavljuje disconnect za sekundarni uređaj koji
aplikacija nikada nije instalirala. Ownership provjera je ipak namjerno
defenzivna: ako strani handle dođe do callbacka, ne smije promijeniti primary
session. Dok session još nema bound handle, disconnect se tretira kao prekid
primary openinga jer u toj fazi aplikacija nema drugi pouzdan identitet.

Preostali gateovi prije hardware zatvaranja nalaza:

- A i B preko stvarnog USB huba, uključujući disconnect B dok A reproducira;
- disconnect A tijekom install/mount faze;
- connect/disconnect bounce i 50–100 reconnect ciklusa;
- stvarni mount i unmount failure te provjera da nema use-after-unmounta;
- potvrda da se media-removed callback objavljuje samo za accepted session.

---

## P1-2 — Control-link state edge događaji mogu se izgubiti

Status na `origin/master@65ecc563`: **OTVORENO**

Status nakon remediation implementacije 2026-07-29:
**SOFTWARE ZATVORENO; HARDWARE RECONNECT/SATURATION SMOKE PENDING**

Primarne lokacije:

- `firmware/common/control_state_reconciler/include/control_state_reconciler.h`
- `firmware/control-board-s3/main/app_main.c`
- `firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c`
- `firmware/main-deck-p4/components/control_link/control_link_uart.c`
- `tests/control_state_reconciler/test_control_state_reconciler.c`
- `tests/control_link_uart/test_control_link_uart.c`
- `tests/flx4_midi_host/test_flx4_midi_host.c`

### Nalaz

State edge koristi bounded čekanje. Nakon timeouta enqueue vraća `false`, a
caller povećava drop counter. To nije lossless semantika.

Release događaj može se izgubiti za held kontrole kao što su scratch touch,
Censor, roll, Pad FX ili SHIFT. Tada P4 može ostati u latchanom stanju.

Postojeći test nazvan kao full-queue test koristi queue dubine dva, ali prije
čitanja šalje samo dva događaja. Ne šalje treći događaj koji bi stvarno pogodio
full-queue/backpressure put. Fake RTOS ne modelira scheduler ni čekanje
consumera.

### Preporučeni popravak

Najprije klasificirati evente:

1. kontinuirane vrijednosti — dopušten latest-value/coalescing;
2. held state — mora postojati durable desired-state reconciliation;
3. diskretne komande — trebaju sequence i pouzdanu command isporuku.

Za held state u receiveru održavati:

```text
desired_state_bits
delivered_state_bits
dirty_state_bits
```

Parser uvijek ažurira desired state. Ako je queue pun, dirty stanje ostaje
spremljeno i consumer ga nakon draina mora uskladiti. S3 reconnect treba poslati
puni state snapshot kako bi P4 uklonio stale held state.

Za diskretne komande koristiti zaseban command ring i sequence-gap telemetry.
Ako receiver-side zaštita nije dovoljna, proširiti postojeći `0xA5` protokol
kompatibilnim ACK/retry ili resync mehanizmom.

### Implementirani popravak

- Zajednički header-only `control_state_reconciler` klasificira 54 nezavisna
  fizička held levela: Deck 1/2 jog touch, Shift, Censor, Pad FX 1/2 padove i
  shifted Beat Loop roll padove.
- I S3 translator i P4 receiver za svaki slot čuvaju observed, desired,
  scheduled i dirty stanje. Kad je queue pun, najnovije željeno stanje ostaje
  dirty i šalje se čim consumer oslobodi kapacitet; producer više ne drenira,
  ne premeće i ne izbacuje tuđe queue elemente.
- Held flush ima prednost nad kontinuiranim vrijednostima. Apsolutne vrijednosti
  i dalje koriste latest-value coalescing, relativni jog/browse akumulaciju, a
  diskretne komande ostaju FIFO jer njihovo sažimanje mijenja semantiku.
- Heartbeat/P4 reboot ponovno šalje poznate held levele. Identičan snapshot
  unutar istog P4 lifetimea se ne duplicira, dok ga novi P4 s praznim
  reconcilerom prihvaća.
- FLX4 disconnect najprije pretvara sva opažena held stanja u release, a zatim
  objavljuje disconnected state. Time scratch, Shift, Censor, Pad FX i roll ne
  mogu ostati latchani samo zato što je kontroler nestao.
- Sequence-gap telemetry ostaje zaštita za diskretne komande. ACK/retry za
  proizvoljne ponovljene command događaje ostaje buduće protocol hardening, ne
  dio ovog held-state popravka.

### Obvezni testovi

- [x] stvarno napuniti queue i poslati događaj preko kapaciteta;
- [x] press/release i zadnja vrijednost nakon zasićenja;
- [x] stalled consumer i flush nakon draina;
- [x] SHIFT kombinacije;
- [x] scratch touch i jog burst;
- [x] nezavisni Censor, Pad FX i roll dirty slotovi;
- [x] više promjena iste kontrole prije draina;
- [x] sequence gap;
- [x] S3 disconnect dok je held state aktivan;
- [x] heartbeat/reconnect resync, duplicate suppression i P4 reboot replay.

Izvorni queue-depth-2 test prvo je korigiran da pošalje treći held događaj i na
starom kodu je reproducirao kvar (`n == 1` assertion). Nakon popravka
`control_link_uart` izvršava 110 provjera, čisti reconciler 32 provjere, a
`flx4_midi_host` potvrđuje da USB owner objavljuje samo stvarne connection
edgeove. Kompletni S3 i P4 host suiteovi te oba ESP-IDF 6.0.2 firmware builda
prolaze.

### Acceptance kriterij

Nijedna held kontrola ne može ostati latchana zbog punog queuea. Test koji
tvrdi da provjerava puni queue mora stvarno poslati više događaja od
kapaciteta. Software kriterij je ispunjen; fizička provjera sa stvarnim FLX4,
S3 i P4 ostaje otvorena.

---

## P1-3 — UI/library/deck shared-state raceovi

Status nakon remediation implementacije 2026-07-29:
**SOFTWARE ZATVORENO; HARDWARE/SOAK PENDING**

Primarne lokacije:

- `firmware/main-deck-p4/components/deck_core/deck_loaded_track_store.c`
- `firmware/main-deck-p4/components/deck_core/deck_core.c`
- `firmware/main-deck-p4/components/ui/ui_event_counter.c`
- `firmware/main-deck-p4/components/ui/ui_library.c`
- `firmware/main-deck-p4/main/app_main.c`
- `tests/deck_loaded_track_store/test_deck_loaded_track_store.c`
- `tests/ui_event_counter/test_ui_event_counter.c`
- `tests/deck_core_dual/test_deck_core_dual.c`

### Nalaz

`volatile` zastavice `s_library_needs_refresh` i `s_usb_removed_pending` nisu
thread-safe sinkronizacija. Novi događaj može stići između readerovaog read i
clear koraka te biti izgubljen.

USB task dodatno mijenja obični `s_library_page_cache_valid`, iako cache koristi
LVGL task.

Loaded-track key, valid flag, media i povezani BPM/ANLZ podaci objavljuju se kao
odvojena shared polja. Writer može započeti zamjenu dok je `valid` još true, a
deck task može pročitati kombinaciju starog i novog tracka.

### Mogući utjecaj

- hot cue spremljen pod pogrešnim track ključem;
- beat jump ili sync koriste pogrešan beatgrid/BPM;
- stale UI nakon USB removea;
- rijetki, timing-ovisni problemi koje sekvencijalni host test ne vidi.

### Preporučeni popravak

`deck_core` treba biti vlasnik koherentnog loaded-track snapshota:

```c
typedef struct {
    uint32_t generation;
    bool valid;
    uint8_t deck;
    track_key_t track_key;
    media_id_t media;
    float bpm;
    anlz_snapshot_handle_t anlz;
} deck_loaded_track_snapshot_t;
```

UI/library šalje jednu queue poruku s kompletnim podatkom. Deck task objavljuje
read-only status snapshot. USB remove također ide kao command decku.

Library refresh i USB remove signalizirati task notification bitovima ili
queueom. Library page cache smije mijenjati samo LVGL task.

### Implementirani popravak

- `deck_core` sada posjeduje jedan koherentan snapshot po decku: generation,
  media generation, track key, duration, BPM i vlastiti deep-clone ANLZ objekt
  objavljuju se ili brišu u jednoj writer transakciji. Deck snapshot namjerno
  ne kopira high-resolution UI waveform do 128 KiB jer ga deck logika ne koristi.
- Firmware read/write put koristi statički FreeRTOS mutex s priority
  inheritanceom; host concurrency model koristi atomski reader/writer guard.
  Stari ANLZ oslobađa se tek nakon završetka zaštićene zamjene.
- USB remove nakon `library_clear()` podiže media-generation floor i atomarno
  invalidira oba deck snapshota. Zakašnjeli rezultat starog loadanja zato se ne
  može ponovno objaviti.
- Load worker invalidira stari deck snapshot prije audio loadanja te nakon
  loadanja ponovno provjerava catalog generation. Ako je USB/catalog promijenjen,
  odmah gasi novostvorenu audio sesiju, bez čekanja sljedećeg LVGL ticka.
- `deck_core` više ne poziva weak UI gettere za track key, BPM ili ANLZ.
  Uklonjeni su i neupotrebljavani javni borrowed-pointer UI getteri.
- `s_library_needs_refresh` i `s_usb_removed_pending` zamijenjeni su atomskim
  monotonic event brojačima. LVGL task potvrđuje samo generaciju koju je stvarno
  obradio, pa request pristigao tijekom obrade ostaje pending.
- Library page cache invalidira isključivo LVGL task.
- Isti ownership model koristi i headless UI simulator.

### Obvezni testovi

- [x] load A pa odmah B;
- [x] USB remove/stale clear odbija zakašnjeli load stare generacije;
- [x] paralelni reader i 20.000 zamjena ne miješaju BPM/ANLZ generacije;
- [x] hot cue u invalidnom replace intervalu ne koristi prethodni track key;
- [x] ANLZ A nikada nije u snapshotu tracka B;
- [x] novi refresh event tijekom obrade starog ostaje pending;
- [x] burst requesti se koalesciraju bez gubitka zadnje generacije;
- [x] snapshot generation raste monotono kroz 20.000 publish transakcija;
- [x] reader ne vidi `valid=true` uz parcijalna polja.

Software validacija: kompletan `tests/run_p4_host_tests.ps1`, oba
`deck_core_dual` moda, headless LVGL navigacija i svi screenshot baselineovi te
P4 `idf.py build` na ESP-IDF v6.0.2 prolaze. Hardver nije bio dostupan, pa
fizički USB remove tijekom stvarnog decode/load prozora, FLX4 hot-cue/Sync/Beat
Jump interleaving i dugotrajni dual-deck soak ostaju acceptance gateovi.

### Acceptance kriterij

Nema običnih writable cross-task globalnih struktura u library/deck ownership
putu. Svaka promjena ide queueom, notificationom ili dokumentiranim koherentnim
snapshot primitiveom.

---

## P2-1 — ANLZ safe snapshot uzrokuje velike deep-copy alokacije

Status na `origin/master@65ecc563`: **OTVORENO**

Status nakon remediation implementacije 2026-07-29:
**SOFTWARE ZATVORENO; HARDWARE LOAD/ACTION SOAK PENDING**

Primarne lokacije:

- `firmware/main-deck-p4/components/ui/ui_deck_anlz_store.c:191-221`
- `firmware/main-deck-p4/components/library/rekordbox_anlz.c:607-645`
- `firmware/main-deck-p4/components/library/include/rekordbox_anlz.h:72`
- `firmware/main-deck-p4/components/deck_core/deck_core.c:800-805`

### Nalaz

Svaki snapshot getter duboko kopira dinamičke beatgrid i waveform buffere.
Maksimalni dopušteni ANLZ može zahtijevati približno 640 KiB kopiranja i
alokacija. Getter se koristi u beat jump, nearest beat, loop, sync i UI
putovima.

### Rizik

- PSRAM i heap fragmentacija;
- latency spike;
- OOM fallback;
- dodatni stall deck taska;
- pojačavanje control queue pritiska.

### Preporučeni popravak

Uvesti immutable, ref-counted i versioned snapshot:

```c
anlz_snapshot_t *anlz_snapshot_acquire(...);
void anlz_snapshot_release(anlz_snapshot_t *);
```

Beatgrid/hot-cue podatke odvojiti od velikog waveform payloada. Getter ne smije
alocirati. Novi snapshot treba objaviti atomskim pointer swapom pod kratkim
lockom, a stari osloboditi tek kada zadnji reader otpusti referencu.

### Testovi

- reader drži stari snapshot dok writer objavi novi;
- više readera;
- OOM pri kreiranju novog snapshota;
- maksimalni beat count i waveform;
- 10.000 acquire/release/swap iteracija;
- heap floor ne pada kontinuirano;
- nema use-after-free ili double-free.

### Implementirani popravak

- Novi `anlz_snapshot_t` je immutable objekt s jedinstvenom nenultom verzijom
  i atomskim refcountom. Kreiranje radi jedan deep copy prije objave;
  `retain`/`release` i svi store `acquire` pozivi ne alociraju.
- `deck_loaded_track_store` objavljuje kompaktni snapshot s beatgridom, cues,
  VBR podacima i low-resolution waveformom, ali bez do 128 KiB high-resolution
  UI waveforma. Beat Jump, Quantize, Beat Loop i Beat Sync sada drže referencu
  samo tijekom izračuna umjesto da za svaku akciju kloniraju beatgrid.
- UI store objavljuje puni snapshot jednom po loadu. Frame context,
  Performance/Hot Cue prikaz i Overview drže eksplicitne reference; Overview
  zadržava vlastitu referencu za LVGL event callbackove između frameova.
  Time je uklonjen i raniji implicitni task-local double-buffer lifetime.
- Writer prvo izgradi novi snapshot, zatim pod kratkim gateom zamijeni pointer.
  Stari objekt se oslobađa tek nakon zadnjeg readera. OOM pri kreiranju ostavlja
  prethodno objavljeni store snapshot netaknut.
- Host regresije potvrđuju dva istodobna readera preko writer swapa, različite
  verzije, OOM bez parcijalne zamjene, maksimalnih `UINT16_MAX` beatova i
  `ANLZ_WAVEFORM_HIGH_MAX` waveforma te 10.000 acquire/release iteracija bez
  dodatnog `anlz_clone()` poziva. Postojeći concurrent deck test dodatno
  izvršava 20.000 publish i 40.000 reader iteracija bez miješanja generacija.
- Puni P4 host suite i ESP-IDF v6.0.2 P4 build prolaze. Bez dostupnog hardvera
  nisu izvedeni USB reload, Beat Jump/Sync/Loop akcijski soak ni provjera
  dugoročnog PSRAM heap floora na stvarnom P4.

---

## P2-2 — Globalni audio lock ostaje real-time bottleneck

Status na `origin/master@65ecc563`:

- library `media_io_gate` scope: **POPRAVLJENO U SOURCEU; HW PENDING**
- globalni `AE_LOCK`: **OTVORENO; IMPLEMENTACIJA ODGOĐENA**

Primarne lokacije:

- `firmware/main-deck-p4/components/audio_engine/audio_engine.c:895-900`
- `firmware/main-deck-p4/components/audio_engine/audio_engine.c:2390-2402`
- `firmware/main-deck-p4/components/library/library.c:281-330`

### Izvorni nalaz

Jedan globalni recursive `AE_LOCK` štiti velik dio audio enginea. Decoder
zagrijava cache prije ulaska, ali decode ostaje pod lockom. Seek ili prediction
miss može završiti backend readom dok output čeka isti lock.

Na auditiranom migration commitu library je držao `media_io_gate` kroz cijeli
PDB open/read-all/close ciklus.

### Promjena na masteru

`library.c` sada uzima gate zasebno za PDB open, svaki row read i close. Time
audio compressed-cache miss može ući između redaka umjesto da čeka cijeli
katalog. Behavioral testovi potvrđuju da jedan gate span ne pokriva više od
jednog retka te da se walk prekida kada testni mount nestane.

To je stvarni popravak jednog dijela nalaza. I dalje ostaje hardware provjera s
realnim FATFS/VFS/MSC lifetimeom i aktivnim dual-deck playbackom.

Globalni `AE_LOCK` ostaje nepromijenjen. Novi
`docs/AUDIO_ENGINE_PER_DECK_LOCK_PLAN.md` ispravno dokumentira predložene
per-deck lockove, shared lock i zabranu nested lockova, ali implementacija je
odgođena dok ne bude dostupan board ili firmware-config host harness.

### Preporučeni plan

Prije refaktora dodati metrike:

- maksimalno vrijeme držanja `AE_LOCK`;
- vrijeme čekanja output taska;
- backend read pod lockom;
- cache hit/miss;
- PCM ring low-water mark;
- I2S underrun;
- trajanje USB readova i `media_io_gate`.

Ciljna arhitektura:

```text
USB/cache I/O
    -> per-deck decoder task
    -> per-deck PCM SPSC ring
    -> real-time output/mixer
    -> I2S
```

Output task ne smije čitati USB, parsirati PDB, alocirati veliku memoriju ili
čekati decoder backend. PDB čitanje treba podijeliti u chunkove i između njih
otpustiti gate.

### Acceptance kriterij

`audio_engine_locked_backend_read_count()` ostaje 0 u steady-state
reprodukciji, nema I2S underruna u hardware stress scenariju i output execution
vrijeme ima dokumentiranu deadline marginu.

---

## P2-3 — OTA status ima cross-task data race

Status na `origin/master@65ecc563`: **OTVORENO**

Status nakon remediation implementacije 2026-07-29:
**SOFTWARE ZATVORENO; HARDWARE OTA FAILURE/RETRY SMOKE PENDING**

Primarne lokacije:

- `firmware/main-deck-p4/components/p4_ota_pull/p4_ota_pull.c`
- `firmware/main-deck-p4/components/p4_ota_pull_core/include/p4_ota_pull_gate.h`
- `tests/p4_ota_pull_gate/test_p4_ota_pull_gate.c`

### Nalaz

Globalni status i `volatile bool s_running` čitaju se i mijenjaju iz
worker/web konteksta bez mutexa ili atomskog state transitiona.

### Preporučeni popravak

Uvesti stanje:

```text
IDLE -> STARTING -> RUNNING -> APPLYING -> SUCCEEDED/FAILED
```

Start radi atomic compare-and-transition. Cijeli status objavljuje se kao
koherentan snapshot pod mutexom ili seqlockom. Mrežni I/O ne smije se izvršavati
dok je status mutex zaključan.

### Testovi

- dva istodobna start poziva;
- status read tijekom updatea;
- task-create failure;
- network failure;
- retry nakon greške;
- reader nikada ne vidi nemoguću kombinaciju statusnih polja.

### Implementirani popravak

- `p4_ota_pull_gate` koristi atomic compare-and-exchange tako da samo jedan
  check/install start može rezervirati OTA operaciju. Task-create, lease i
  network failure putovi otpuštaju gate pa je retry moguć.
- Cijeli operator-facing status, install offer, TTL i progress objavljuju se pod
  kratkim `portMUX` critical-sectionom. Getter vraća jednu koherentnu kopiju;
  URL/SHA/size kopiraju se u workerov lokalni snapshot prije mrežnog I/O-a.
- Ni Wi-Fi prijelaz, HTTP/TLS, flash write ni task creation ne izvršavaju se dok
  je state critical-section zaključan.
- Task-create failure objavljuje završni FAILED snapshot prije otpuštanja gatea,
  tako da zakašnjeli failure writer ne može pregaziti status novog pokušaja.
- Host test potvrđuje single-winner acquire i retry nakon releasea; kompletni P4
  host suite i ESP-IDF 6.0.2 P4 build prolaze.

---

## P2-4 — Monitor PCM konfiguracija i statistika nisu koherentne

Status na `origin/master@65ecc563`: **OTVORENO**

Status nakon remediation implementacije 2026-07-29:
**SOFTWARE ZATVORENO; HARDWARE MONITOR-LINK SOAK PENDING**

Primarna lokacija:

- `firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c`
- `tests/monitor_pcm_link/test_monitor_pcm_link.c`

### Nalaz

Queue counteri koriste atomike, ali enabled flag, format/config i dio statistike
ostaju obični shared globali između audio, transport i web/status konteksta.

### Preporučeni popravak

- atomic bool za enable;
- versioned/seqlock snapshot za povezanu konfiguraciju;
- atomici za nezavisne monotone brojače;
- jasno definiran owner konfiguracije;
- disable/teardown mora zatvoriti producer gate prije gašenja I2S resursa.

### Implementirani popravak

- Enable je acquire/release atomic producer gate; disabled output write ostaje
  jeftin no-op i ne povećava drop statistiku.
- Sample rate i channel/bit shape objavljuju se kao versioned 32-bit seqlock
  snapshot. Reader prihvaća samo stabilnu parnu verziju, pa ne može spojiti
  sample rate starog formata s kanalima ili bit-depthom novog.
- Submitted/dropped block i submitted-frame brojači su nezavisni monotoni
  atomici. SPSC queue indeksi zadržavaju postojeći acquire/release ownership.
- `monitor_pcm_link_set_format()` je jedini configuration writer, audio output
  je jedini queue producer, a transport task jedini consumer. Disable zatvara
  producer admission prije bilo kakvog vanjskog transport teardowna.
- Host regresija provjerava oba kompletna format snapshota, enable/disable,
  serijalizaciju/CRC i puni queue; kompletni P4 host suite i ESP-IDF 6.0.2 P4
  build prolaze.

---

## P2-5 — USB recovery se trajno isključuje nakon prvog uređaja

Status na `origin/master@65ecc563`: **OTVORENO**

Primarna lokacija:

- `firmware/main-deck-p4/components/usb_storage/usb_storage.c:245-271`

### Nalaz

`s_seen_device` se postavlja nakon prvog connecta i resetira samo pri
inicijalizaciji. Nakon prvog uspješnog sessiona kasniji enumeration stall više
ne pokreće root-port power-cycle recovery.

### Preporučeni popravak

Recovery vezati uz session epoch, ne uz cijeli život firmwarea. Nakon
vlasničkog disconnecta resetirati recovery brojač. Koristiti bounded retry i
backoff, uz fault status nakon maksimalnog broja pokušaja.

### Implementacijski status na `codex/fix-usb-recovery-lifecycle`

Status: **SOFTWARE POPRAVLJENO; HARDWARE TESTOVI ODGOĐENI**

Firmware-lifetime `s_seen_device` latch je uklonjen. Novi
`usb_storage_recovery` state machine prati:

- je li recovery armiran;
- zadnji opaženi USB session epoch;
- je li accepted session trenutno spojen;
- broj izvršenih power-cycle pokušaja;
- tick zadnjeg pokušaja.

Accepted connect zaustavlja recovery. Vlasnički disconnect povećava session
epoch i ponovno armira cijeli brzi recovery budžet. Ako se connect i disconnect
dogode između dva pollanja USB library taska, promjena epocha i dalje ponovno
armira recovery. Ponavljano čitanje istog disconnected epocha ne pomiče
deadline, pa polling ne može beskonačno odgađati pokušaj.

Nakon osam brzih pokušaja recovery prelazi na bounded sporu kadencu od 30
sekundi i jednom bilježi degraded warning. Tick usporedba koristi unsigned
elapsed-time aritmetiku i ostaje ispravna preko FreeRTOS tick wrapa.

Novi `tests/usb_storage_recovery/test_usb_storage_recovery.c` izvršava 88
provjera za cold boot bez sessiona, connect/disconnect, neopaženi kratki
session, ponovljeni snapshot, puni fast-cycle budžet, prijelaz na slow cadence,
reconnect i tick wrap. Test je uključen u puni P4 host runner.

Validacija na ovoj stacked grani:

- `tests/run_p4_host_tests.ps1`: **PASS**, uključujući 63 ownership i 88
  recovery provjera;
- P4 `idf.py build` na ESP-IDF v6.0.2: **PASS**;
- `main-deck-p4.bin`: `0x24f060` bajtova, `0x1b0fa0` bajtova (42%) slobodno u
  najmanjoj app particiji.

Zbog trenutačne nedostupnosti hardvera ovaj status ne potvrđuje ponašanje
stvarnog USB PHY-ja, VBUS power-cyclea, huba ili MSC drivera. Ti gateovi ostaju
eksplicitno odgođeni, a ne označeni kao PASS.

### Hardware test

- cold boot bez USB-a;
- boot s priključenim USB-om;
- reconnect nakon ranije uspješne sesije;
- namjerni enumeration stall;
- najmanje 50 reconnect ciklusa.

---

## P2-6 — Production sigurnost nije zatvorena

Status na `origin/master@65ecc563`:

- AP/API pristup: **RIZIK EKSPLICITNO PRIHVAĆEN, NIJE MITIGIRAN**
- Secure Boot/flash encryption: **PROCJENA DOVRŠENA, IMPLEMENTACIJA BLOKIRANA**

### Nalazi

- S3 debug AP i P4 Wi-Fi link koriste hardkodirani zajednički PSK
  `Pajoniiir`;
- `X-DDJ-Control: 1` je marker, ne autentikacija;
- OTA service network password sprema se kao plaintext u NVS;
- secure boot je isključen;
- flash encryption je isključen.

Potpisani OTA, SHA-256, size provjera i newer-only pravila štite OTA artifact,
ali ne štite control API, credentiale iz flash dumpa ni fizičko reflashing
nepotpisanog firmwarea.

### Preporučeni popravak

1. napisati threat model;
2. razdvojiti development i production security profile;
3. generirati per-device PSK;
4. uvesti fizički pokrenut pairing i random control token;
5. rate-limitati API;
6. za destruktivne OTA operacije koristiti fizičku potvrdu ili kratkotrajni
   nonce;
7. definirati key ownership, backup i recovery;
8. tek nakon provjere factory procesa uključiti secure boot i flash encryption;
9. testirati signed OTA i rollback prije nepovratnog eFuse zaključavanja.

P4 bootloader trenutačno ima približno 5% slobodnog prostora, pa prije
production security uključivanja treba ponovno provjeriti njegovu veličinu.

### Promjena na masteru

`docs/SECURE_BOOT_FLASH_ENCRYPTION_ASSESSMENT.md` sada precizno dokumentira da
trenutačni P4 bootloader ima samo 1.296 bajtova slobodno i da Secure Boot
zahtijeva pomicanje partition table offseta, puni wired flash, odluku o NVS
migraciji i test na žrtvenom boardu.

`docs/RISK_REGISTER.md` hardkodirani WPA2 password sada vodi kao eksplicitno
prihvaćen, nemitigiran rizik. To poboljšava transparentnost release odluke, ali
ne mijenja security posture: hardkodirani PSK i neautenticirani lokalni control
API ostaju.

---

## P2-7 — Parcijalno neuspjela inicijalizacija ostavlja resurse

Status na `origin/master@65ecc563`: **OTVORENO**

Status nakon remediation implementacije 2026-07-29:
**SOFTWARE POPRAVLJENO; IDF FAILURE-INJECTION I HARDWARE RETRY PENDING**

Primarne lokacije:

- `firmware/main-deck-p4/components/controller_profile_manager/controller_profile_manager.c:797-810`
- `firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link_i2s.c:189-197`

### Nalaz

Ako drugi task ne može biti kreiran nakon prvoga, funkcije se vraćaju bez
potpunog čišćenja već kreiranih taskova, queueova ili I2S kanala.

### Preporučeni popravak

Koristiti staged init i jedan cleanup put. Globalni singleton objaviti tek
nakon što su svi koraci uspjeli. Za svaki init korak dodati failure injection i
potvrditi da ponovni `init()` može uspjeti.

### Implementirani popravak

- Controller profile manager sada pamti oba task handlea i ima jedan
  `cpm_runtime_cleanup()` put. Neuspjeh mutexa, semafora, bilo kojeg queuea ili
  bilo kojeg taska briše prethodno stvorene taskove, queueove, semafore i mutex,
  uklanja control-link callback i vraća sve handleove na `NULL`.
- Runtime se označava inicijaliziranim i callback se objavljuje tek nakon što su
  oba taska uspješno stvorena. Ponovljeni poziv nakon uspjeha je idempotentan, a
  poziv nakon neuspjeha ponovno kreće od praznog stanja.
- Monitor PCM transport koristi atomic start reservation. I2S channel ostaje
  lokalni staged resurs dok transport i opcionalni bench task nisu stvoreni.
  Transport task dobiva channel kao argument i zato ne ovisi o prerano
  objavljenom singletonu.
- Jedinstveni monitor cleanup prvo briše eventualne taskove, zatim disablea i
  briše I2S channel te poništava globalni handle. Svaki init/enable/task-create
  failure otpušta i start reservation.
- P4 host suite sadrži source-regression gateove za sve obvezne cleanup
  primitive, a kompletni ESP-IDF 6.0.2 P4 build prolazi. To još nije dinamička
  simulacija svakog IDF allocator/driver failurea; taj failure-injection fixture
  i fizički retry ostaju otvoreni prije punog zatvaranja acceptance kriterija.

---

## P3-1 — LRU timestamp nije wrap-safe

Status na `origin/master@65ecc563`: **OTVORENO**

Status nakon remediation implementacije 2026-07-29:
**SOFTWARE ZATVORENO**

Primarna lokacija:

- `firmware/main-deck-p4/components/audio_engine/audio_compressed_cache.c:31-43`
- `firmware/main-deck-p4/components/audio_engine/include/audio_compressed_cache.h`
- `tests/audio_compressed_cache/test_audio_compressed_cache.c`

### Nalaz

LRU bira numerički najmanji `uint32_t` stamp. Nakon wrapa novi stampovi postaju
mali i mogu biti pogrešno odabrani kao najstariji.

### Preporučeni popravak

Koristiti `uint64_t` stamp ili wrap-safe age/renormalization. Dodati unit test
koji postavlja stamp neposredno ispod `UINT32_MAX` i prelazi wrap.

### Implementirani popravak

- Globalni cache stamp i stamp svake stranice prošireni su na `uint64_t`; victim
  selection uspoređuje isti tip i više nema 32-bitni prijelaz na nulu.
- Regresija puni cache od dvije stranice, postavlja LRU redoslijed neposredno
  ispod `UINT32_MAX`, dodirima prelazi nekadašnju wrap granicu i učitava treću
  stranicu. Test zatim potvrđuje da najnovija stranica ostaje cache hit, a
  stvarno najstarija zahtijeva novo backend čitanje.
- Ciljani `audio_compressed_cache` suite prolazi 68 provjera, kompletni P4 host
  suite prolazi, a firmware se kompajlira i linka s ESP-IDF v6.0.2. Dobiveni
  `main-deck-p4.bin` ima `0x250080` bajtova i 42% slobodne najmanje app
  particije.
- Hardware bounded-cache stress s realnim MP3/WAV/FLAC zapisima ostaje zaseban
  acceptance gate za USB latenciju i audio deadline; nije potreban za zatvaranje
  determinističkog LRU arithmetic buga.

---

## P3-2 — Cold configure/build je spor i kompajlira širok LVGL source tree

Status na `origin/master@65ecc563`: **OTVORENO**

Tijekom čistog P4 builda kompajlirani su mnogi očito neaktivni LVGL backendovi,
uključujući Linux, Wayland, X11, Windows, UEFI, SDL/OpenGL, NuttX, glTF i
ThorVG/Lottie putove.

Linker većinu neaktivnog koda odbaci, pa ovo nije automatski runtime-size
problem, ali značajno povećava cold-build vrijeme.

Na korištenom Windows računalu izmjereno je:

- S3 configure približno 6,5 minuta;
- S3 bootloader configure približno 4,7 minuta;
- P4 configure približno 29,6 minuta;
- P4 build 1854 glavna koraka.

### Preporučeni popravak

- izmjeriti configure, LVGL compile i link odvojeno;
- provjeriti može li LVGL komponenta generirati uži source manifest;
- uvesti compiler/managed-component cache keyed dependency lockovima;
- koristiti kraću putanju i Windows long-path podršku;
- odvojiti clean-release od brzog incremental CI workflowa;
- ne patchirati managed LVGL bez pinanog i reproducibilnog patch procesa.

---

## P3-3 — Projektni compiler i Kconfig warningi

Status na `origin/master@65ecc563`: **OTVORENO**

Projektni warningi:

- `bsp_jc4880.c:96`: nekorišten `s_i2s_tx`;
- `audio_engine.c:108`: nekorišten `ae_now_us`;
- `audio_engine.c:1920`: nekorišten `ae_diag_log_memory`;
- FATFS Kconfig bool opcije koriste `default 0` umjesto `default n`.

NimBLE `default 0` poruka dolazi iz ESP-IDF 6.0.2 sourcea i nije projektni
warning.

### Preporučeni popravak

Nekorištene simbole staviti pod odgovarajuće `CONFIG_` uvjete ili ukloniti ako
su legacy. FATFS bool default vrijednosti ispraviti na `y/n`. Nakon čišćenja
razmotriti CI gate koji projektne warninge tretira kao greške.

---

## P3-4 — CI supply-chain pinning

Status na `origin/master@65ecc563`: **DJELOMIČNO**

### Nalaz

CI koristi mutable Docker tag `espressif/idf:v6.0.2` i action reference poput
`actions/checkout@v4`. Dependency lockovi su commitani, što je dobar temelj,
ali runner i action implementacije mogu se promijeniti iza taga.

### Preporučeni popravak

- pinati ESP-IDF image digestom;
- pinati action commit SHA-ove;
- uz SHA ostaviti komentar semantičke verzije;
- artifactu dodati Git commit, IDF verziju, lock hash i firmware SHA-256;
- generirati release manifest/SBOM;
- zadržati novi pattern-based branch coverage.

### Promjena na masteru

Obsolete pojedinačni branch filteri zamijenjeni su obrascima za `fix/**`,
`test/**`, `docs/**`, `ci/**` i `migration/**`, uz `master`. Time je zatvoren
problem da push CI tiho ne radi na novoj radnoj grani.

Docker image i GitHub Actions još nisu pinani digestom/commit SHA-om, pa
supply-chain dio nalaza ostaje otvoren.

---

## P3-5 — Dokumentacija proturječi stvarnom release stanju

Status na `origin/master@65ecc563`: **DJELOMIČNO**

Migration dokument ispravno navodi da je hardware acceptance otvoren i da je
migracija već na masteru. Drugi aktivni dokumenti još opisuju migration granu
kao aktivni development head ili kažu da se ne smije mergeati prije hardware
testa.

Preporučena jedinstvena terminologija:

```text
Build verified
Host regression verified
Software integrated
Hardware smoke verified
Hardware stress verified
Security provisioned
Production approved
```

Trenutačno stanje:

```text
Software integrated on master.
Host and clean-build gates pass.
Hardware and production-security acceptance remain open.
```

Treba uskladiti `README.md`, `ARCHITECTURE.md`, `RISK_REGISTER.md`,
`DOCUMENTATION_STATUS.md`, `STARTUP_CHECKLIST.md`, `AGENTS.md` i CI branch
filtere.

Risk Register i migration dokument sada ispravno navode da je migracija na
masteru uz otvoren hardware acceptance. I dalje su zastarjele najmanje ove
tvrdnje:

- `README.md` migration granu naziva aktivnim development headom;
- `DOCUMENTATION_STATUS.md` govori da hardware gate treba proći prije mergea u
  master.

`AGENTS.md` navodi `C:\Espressif\Initialize-Idf.ps1`, koji na audit računalu ne
postoji. Funkcionalni profil bio je
`C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1`, s IDF instalacijom
pod `C:\esp\v6.0.2\esp-idf`.

---

## Pozitivni nalazi

- P4 minimalna silicon revizija ispravljena je na 1.0;
- oba targeta buildaju se na ESP-IDF 6.0.2;
- dependency lockovi su commitani;
- rollback je uključen;
- OTA potpis, size, SHA-256 i newer-only provjere imaju host pokrivenost;
- Wi-Fi transition lease acquire/release putovi djeluju balansirano;
- controller UI naredbe prolaze kroz LVGL task;
- UI simulator koristi pinani LVGL i egzaktne screenshot baselineove;
- host regresijska pokrivenost je široka;
- nisu pronađeni očiti nezaštićeni `strcpy`, `sprintf` ili `atoi` obrasci u
  projektnom kodu;
- recorder ostaje zadano isključen do hardware prihvata.

## Preporučeni redoslijed implementacije

Svaki veći paket treba biti zaseban PR i imati vlastite behavioral testove.

Behavior/wrapper baseline, library media-gate scope i pattern-based CI branch
triggeri dovršeni su na masteru i više nisu implementacijski koraci.

| Redoslijed | Paket | Status / ovisnost |
| ---: | --- | --- |
| 1 | Ispraviti control-link full-queue test | **implementirano; capacity+1 test reproducirao stari drop i prolazi na popravku** |
| 2 | USB ownership state machine | **software implementirano i validirano na `codex/fix-usb-storage-ownership`; HW hub/reconnect pending** |
| 3 | USB recovery lifecycle | **software implementirano i validirano na `codex/fix-usb-recovery-lifecycle`; hardware unavailable/deferred** |
| 4 | Control-link durable state reconciliation | **software implementirano i validirano na S3/P4; HW reconnect/saturation smoke pending** |
| 5 | Deck/library ownership refaktor | **software implementirano i validirano; P4 USB/audio/FLX4 hardware soak pending** |
| 6 | OTA status i monitor PCM concurrency | **software implementirano i validirano; OTA failure/retry i monitor-link HW soak pending** |
| 7 | Partial-init cleanup | **software implementirano; IDF failure-injection fixture i HW retry pending** |
| 8 | Immutable/refcounted ANLZ snapshot | **software implementirano i host/build validirano; P4 USB/action/heap soak pending** |
| 9 | Audio runtime instrumentacija | prije bilo kakvog lock refaktora |
| 10 | Per-deck audio lockovi | prema dokumentiranom planu; traži bolji harness ili board |
| 11 | Decoder/PCM-ring/output razdvajanje | nakon lock metrika i ANLZ refaktora |
| 12 | LRU wrap i build warning cleanup | **LRU software zatvoren; build warning cleanup ostaje otvoren kao zaseban mali PR** |
| 13 | Production AP/API authentication | traži proizvodnu credential odluku |
| 14 | Partition table i security provisioning plan | prije Secure Boot/flash encryption testa |
| 15 | CI digest/SHA pinning i build optimizacija | nakon stabilnih dependency verzija |
| 16 | Dokumentacijsko usklađivanje | nakon funkcionalnih/security odluka |
| 17 | Hardware acceptance izvještaj | nakon svih P1 i relevantnih P2 popravaka |

## Revidirani plan po milestoneovima

### Milestone A — Correctness baseline

Obuhvat:

- popravak control-link testa tako da stvarno napuni queue;
- USB accepted-handle/session ownership;
- USB recovery epoch i bounded backoff;
- durable held-state reconciliation;
- deck-owned koherentni loaded-track snapshot.

Gate za izlaz:

- [x] sva tri P1 nalaza software zatvorena;
- [x] novi testovi padaju na starom kodu i prolaze na popravku;
- [x] oba host suitea i oba firmware builda prolaze;
- [x] nema latchanog held statea u host reconnect/stall modelu;
- [ ] fizički S3/P4/FLX4 reconnect, reboot i saturation smoke.

### Milestone B — Concurrency hygiene

Obuhvat:

- OTA atomic start i koherentni status snapshot;
- monitor PCM atomic/config snapshot;
- task notification umjesto `volatile` UI zastavica;
- staged init i cleanup za profile manager i monitor I2S.

Gate za izlaz:

- svaki init korak ima failure-injection test;
- nijedan status reader ne vidi parcijalnu strukturu;
- ponovni init nakon svakog simuliranog failurea uspijeva.

### Milestone C — Memory i real-time audio

Obuhvat:

- immutable/refcounted ANLZ;
- audio lock/USB/cache/I2S instrumentacija;
- per-deck lockovi ili decoder-to-PCM SPSC arhitektura;
- LRU wrap popravak;
- realni library refresh tijekom playbacka.

Gate za izlaz:

- ANLZ getter ne alocira;
- deck hot path ne kopira waveform;
- backend read pod output/global lockom ostaje 0 u steady-state testu;
- nema I2S underruna na hardware soaku;
- memory floor ostaje stabilan.

### Milestone D — Production hardening

Obuhvat:

- per-device AP credential;
- stvarna control API autentikacija;
- rate limiting i fizička potvrda destruktivnog OTA-a;
- partition table pomak i NVS migration odluka;
- Secure Boot/flash encryption provisioning na žrtvenom boardu;
- Docker/action pinning i release manifest.

Gate za izlaz:

- neautenticirani klijent ne može mutirati stanje;
- unsigned image je odbijen;
- OTA rollback i recovery ostaju funkcionalni;
- firmware artifact ima reproducibilan dependency i SHA manifest.

### Milestone E — Hardware acceptance i release

Obuhvat:

- USB, audio, UI, FLX4, OTA i security matrice iz ovog dokumenta;
- višesatni dual-deck soak;
- slušni acceptance;
- dokumentacijsko usklađivanje.

Gate za izlaz:

- nema otvorenih P1 nalaza;
- svi obvezni hardware redovi imaju datirani dokaz;
- master se može označiti kao hardware-accepted i production-approved samo ako
  su sigurnosni/provisioning kriteriji također zadovoljeni.

## Minimalni gate za svaki funkcionalni PR

1. novi test mora prvo reproducirati problem;
2. test mora pasti na starom kodu;
3. implementirati popravak;
4. novi i postojeći testovi moraju proći;
5. ako je diran samo P4, pokrenuti P4 host suite i P4 build;
6. ako je diran S3 ili shared protokol, pokrenuti oba host suitea i oba builda;
7. ako je diran UI, pokrenuti UI screenshot gate;
8. ako je diran audio DSP/cache, pokrenuti petominutni dual-deck soak;
9. `git diff --check`;
10. provjeriti da build nije promijenio commitane dependency lockove.

## Preostali hardware acceptance

### USB

- cold boot bez USB-a;
- boot s već priključenim USB-om;
- 50–100 reconnect ciklusa;
- secondary uređaj preko huba;
- disconnect secondary i active uređaja;
- enumeration stall;
- BNA recovery;
- playback tijekom reconnecta.

### Audio

- MP3, WAV i FLAC;
- oba decka i Master Tempo;
- scratch, sync, hot cue i Beat FX;
- ponovljeni seekovi;
- browse/PDB refresh tijekom playbacka;
- sporiji USB medij;
- višesatni soak;
- CPU margin, I2S underrun i lock telemetry;
- slušni prihvat clickova, dropouta i pitch stabilnosti.

### UI

- DSI/PPA cold boot;
- touch rubovi i brzi dodiri;
- paginirana Library navigacija;
- USB remove dok je Library otvoren;
- Settings restore i screensaver;
- fluidity pod punim audio opterećenjem.

### Control link i FLX4

- stvarni FLX4 MIDI IN/OUT i UAC;
- jog/fader/pad burst;
- SHIFT kombinacije;
- scratch press/release pod opterećenjem;
- S3 reset dok P4 reproducira;
- P4 reset dok S3 ostaje aktivan;
- reconnect full-state resync;
- LED feedback.

### OTA i sigurnost

- potpisani S3 i P4 update;
- odbijanje pogrešnog potpisa, SHA-a i sizea;
- rollback;
- prekid napajanja tijekom OTA;
- offer TTL;
- AP/STA transition;
- API authentication failure;
- factory recovery;
- secure-boot/flash-encryption provisioning.

## Konačni Definition of Done

Production release zahtijeva:

- zatvorena sva tri P1 nalaza;
- USB multi-device i reconnect behavioral testove;
- nemogućnost latchanog control statea;
- koherentno deck/library ownership stanje;
- ANLZ getter bez velikih alokacija;
- audio output bez backend čitanja pod globalnim lockom;
- koherentne OTA i monitor status snapshotove;
- bez projektnih compiler/Kconfig warninga;
- zelene S3/P4 host testove, UI simulator i audio soak;
- čiste ESP-IDF 6.0.2 buildove oba targeta;
- izvršenu hardware acceptance matricu;
- provedenu production security/provisioning odluku;
- dokumentaciju koja jasno razlikuje software PASS od hardware i production
  acceptancea.
