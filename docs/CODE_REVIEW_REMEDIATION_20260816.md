# Pajoniiir code review i remediation plan — 2026-08-16

Status dokumenta: **aktualni otvoreni audit i remediation tracker**

Auditirani commit: `10c91c2aa536be3852cdd6a41e831088d85625d7`

Auditirana grana: `master`, jednaka `origin/master` u trenutku pregleda.

Obvezni toolchain: **ESP-IDF v6.0.2**

Ovaj dokument je nasljednik povijesnog
[`fixevi-remediation-audit.md`](fixevi-remediation-audit.md) closeouta. Ne poništava njegove zatvorene nalaze,
ali bilježi nove probleme pronađene pregledom aktualnog stabla. Dated build i
hardware validation zapisi ostaju dokaz onoga što je tada stvarno izvršeno;
zeleni build ili host suite sami po sebi ne zatvaraju concurrency, fault-path
ili hardware nalaz iz ovog dokumenta.

## 1. Sažetak odluke

Aktualni source se čisto kompajlira i svi pokrenuti automatizirani testovi
prolaze. Unatoč tome, stablo se ne treba proglasiti production-ready dok nisu
zatvoreni svi P1 nalazi, razriješeni primjenjivi P2 release gateovi iz odjeljka
6.1 i izvršeni pripadajući hardware gateovi.

Nakon deduplikacije audit je pronašao:

| Prioritet | Broj | Tumačenje |
| --- | ---: | --- |
| P0 | 0 | Nema pronađenog neposrednog katastrofalnog kvara |
| P1 | 6 | Release blocker; može narušiti autoritativno stanje, audio integritet, OTA politiku ili hardware-safe boot |
| P2 | 16 | Važan reliability, availability, security ili real-time problem |
| P3 | 4 | Hardening, formalni data-race ili testni dug |

Najvažniji zaključci:

1. P4 nema jednog ownera za cijelu LOAD/STOP/EJECT/USB/OTA lifecycle
   transakciju, pa zaustavljena sesija može ponovno nastati nakon uspješnog
   STOP-a.
2. S3 MIDI producer čita i vraća elemente u shared queue, čime u stvarnom
   multicore interleavingu može promijeniti FIFO redoslijed diskretnih naredbi.
3. Aktivni controller profil i snapshot nisu vezani uz VID/PID i USB
   connection epoch.
4. EQ hard-clippa u `int16_t` prije nego TRIM može smanjiti signal.
5. P4 pull OTA ne veže potpisanu verziju bundlea uz release oglašen u channelu.
6. Umirovljeni speaker PA put može podići GPIO11 tijekom normalnog boota.

## 2. Statusi nalaza

Svaki nalaz u ovom dokumentu smije koristiti samo jedan od sljedećih statusa:

- **OPEN** — potvrđen problem; popravak nije integriran.
- **IN PROGRESS** — implementacija postoji na radnoj grani, ali obvezni gateovi
  još nisu svi prošli.
- **SOFTWARE FIXED; HW PENDING** — reprodukcijski i regresijski testovi, host
  suite i obvezni buildovi prolaze, ali fizički acceptance još nije izvršen.
- **CLOSED** — kodni problem i svi obvezni software/hardware gateovi su
  završeni s datiranim dokazom.
- **ACCEPTED RISK** — vlasnik proizvoda eksplicitno je prihvatio preostali
  rizik; to nije isto što i popravak.
- **PROVISIONING DECISION** — zatvaranje zahtijeva proizvodni key/eFuse proces,
  a ne običan source commit.

Statusni prijelaz koji tvrdi implementaciju ili popravak — **IN PROGRESS**,
**SOFTWARE FIXED; HW PENDING** ili **CLOSED** — mora imati:

1. commit ili PR koji nosi popravak;
2. test koji reproducira stari problem i prolazi na popravku;
3. popis pokrenutih gateova s exit kodom;
4. datirani hardware dokaz kada je hardware dio acceptance kriterija;
5. napomenu o preostalom riziku.

**ACCEPTED RISK** umjesto toga zahtijeva imenovanog vlasnika odluke, datum,
obrazloženje, opseg i eksplicitno opisan preostali rizik. **PROVISIONING
DECISION** zahtijeva zaseban decision/provisioning zapis s vlasnikom, key/eFuse
postupkom, recovery planom i acceptance kriterijima; nijedan od ta dva statusa
ne smije se prikazati kao implementirani popravak.

## 3. Opseg pregleda

Pregledani su:

- S3 USB MIDI host lifecycle, mapping, profile runtime, LED output, queueing i
  `0xA5/0xA6` UART transport;
- S3 Debug AP, signed push OTA, boot health i rollback;
- P4 deck/audio lifecycle, decoder/cache/timeline, scratch, mixer, EQ, I2S i
  cue/PFL put;
- P4 Library/LVGL load worker i USB removal;
- P4 pull OTA channel, signed manifest i boot activation;
- P4 BSP audio routing i NVS startup postavke;
- controller profile converter i njegova integracija;
- host testovi, UI simulator, signing suite, dependency lockovi i ESP-IDF
  buildovi;
- relevantni arhitektonski, wiring, OTA, risk i release dokumenti.

Pregled nije zamjena za fizički FLX4/P4/S3 acceptance. Nisu provedeni stvarni
USB replug interleaving, I2S fault injection, UART multicore faultovi, OTA
power-loss, slow-client AP test ni višesatni hardware audio soak.

Sve source putanje i brojevi redaka u nalazima navedeni su relativno prema
korijenu repozitorija na auditiranom commitu.

## 4. Verifikacijski baseline

Sljedeće je prošlo na auditiranom commitu:

| Provjera | Rezultat |
| --- | --- |
| `idf.py --version` | `ESP-IDF v6.0.2` |
| Izolirani clean S3 build | PASS; image `0xeb6c0`, 51% najmanjeg OTA slota slobodno |
| Izolirani clean P4 build | PASS; image `0x250090`, 42% najmanjeg app slota slobodno |
| P4 bootloader | PASS; `0x5af0`, samo `0x510` odnosno približno 5% slobodno |
| `tests/run_s3_host_tests.ps1` | PASS |
| `tests/run_p4_host_tests.ps1` | PASS |
| OTA signing Python suite | PASS, 6/6 |
| Headless LVGL/UI simulator E2E | PASS; svi exact screenshot baselineovi |
| Audio PCM timeline | PASS, 229 provjera |
| Compressed cache | PASS, 68 provjera |
| Scratch/output mixer/key-lock fokusirani testovi | PASS |
| 300 s dual-deck key-lock soak | PASS; drift 0, clipped samples 0 |
| `git diff --check` | PASS |
| Dependency lock diff | Nema promjene |

Prvi in-tree S3 build pokušaj naišao je na ignored CMake cache iz ESP-IDF v5.5.
Izolirani clean v6.0.2 build prošao je, pa taj prvi rezultat nije compile kvar
izvornog koda.

## 5. P1 nalazi i detaljni put popravka

### CR-20260816-P1-01 — P4 audio/deck lifecycle nije serijaliziran

Status: **OPEN**

Primarne lokacije:

- `firmware/main-deck-p4/components/audio_engine/audio_engine.c:3282-3311`
- `firmware/main-deck-p4/components/audio_engine/audio_engine.c:3407-3458`
- `firmware/main-deck-p4/components/audio_engine/audio_engine.c:3591-3678`
- `firmware/main-deck-p4/components/audio_engine/audio_engine.c:3917-3965`
- `firmware/main-deck-p4/components/ui/ui_library.c:564-674`
- `firmware/main-deck-p4/components/ui/ui_library.c:684-720`
- `firmware/main-deck-p4/components/deck_core/deck_core.c:1860-1867`
- `firmware/main-deck-p4/main/app_main.c:262-268`
- `firmware/main-deck-p4/components/web_server/web_server.c:564-568`

#### Kvar

LOAD, STOP, EJECT, USB removal i OTA stop mogu se izvršavati iz različitih
taskova. Postojeći lockovi štite pojedine kratke mutacije, ali ne cijelu
lifecycle transakciju. Moguć je slijed u kojem LOAD završi interni stop, drugi
task izvrši EJECT/STOP i vrati uspjeh, a LOAD nakon toga objavi novi runtime i
pokrene decoder taskove.

UI sloj dodatno na USB removal događaju bezuvjetno briše single-flight busy
stanje iako load worker još može biti živ. Budući da rezultat koristi
jednoslotni `xQueueOverwrite()`, stari worker može pregaziti ili prerano
završiti noviji load.

#### Implementacijski put

1. Uvesti jednog lifecycle ownera po decku:
   - preferirano per-deck control actor s queueom naredbi;
   - prihvatljiv kratkoročni korak je per-deck non-recursive lifecycle mutex
     koji obuhvaća cijeli LOAD/STOP/EJECT, uključujući task create/join.
2. U runtime dodati monotoni `session_generation`:
   - povećati ga prije početka svake nove load sesije i pri svakom force-stopu;
   - kopirati generaciju u immutable task context;
   - task prije svake objave statea ili rezultata provjerava da je generacija
     još aktivna.
3. Dodati globalni stop barrier za OTA i USB removal:
   - blokirati prihvat novih LOAD naredbi;
   - invalidirati sve aktivne generacije;
   - signalizirati stop svim taskovima;
   - joinati taskove;
   - tek zatim vratiti uspjeh pozivatelju.
4. UI load mora dobiti zaseban monotoni `load_id`. Samo rezultat čiji
   `load_id` odgovara aktivnom zahtjevu smije:
   - promijeniti deck/UI state;
   - uključiti ili isključiti gumbe;
   - očistiti `track_load_busy`.
5. USB removal ne smije lažno označiti worker završenim. Treba postaviti
   cancellation/epoch, a busy stanje očistiti tek kada worker objavi završetak
   iste generacije.
6. Teardown redoslijed standardizirati: block new work → invalidate generation
   → signal stop → wake blocking resource → join → release buffers/context →
   publish stopped state.

#### Obvezni testovi

- Hook između internog stopa i bindanja novog runtimea.
- Hook prije i poslije svakog `xTaskCreate`.
- EJECT tijekom LOAD-a.
- OTA `stop_all` tijekom LOAD-a.
- USB remove tijekom LOAD-a, zatim reconnect i novi LOAD prije završetka starog
  workera.
- Stari rezultat ne smije promijeniti UI niti očistiti busy stanje novog load-a.
- STOP se ne smije vratiti dok postoji task prethodne generacije.

#### Acceptance

- P4 host suite i UI simulator prolaze.
- P4 clean ESP-IDF v6.0.2 build prolazi.
- Fizički: najmanje 50 USB remove/reconnect ciklusa tijekom load/playbacka,
  EJECT/LOAD burst i OTA stop s oba decka aktivna.
- Nema živog decoder/output taska nakon potvrđenog STOP-a.

### CR-20260816-P1-02 — S3 producer drain/requeue mijenja FIFO redoslijed

Status: **OPEN**

Primarne lokacije:

- `firmware/control-board-s3/main/app_main.c:272-312`
- `firmware/control-board-s3/main/app_main.c:314-343`
- `firmware/control-board-s3/main/app_main.c:375-388`
- `docs/CONTROL_LINK_PROTOCOL.md:140-148`

#### Kvar

Kada je queue puna, producer poziva `xQueueReceive()`, drži pročitane elemente u
lokalnom stashu i ponovno ih šalje. Consumer na drugoj jezgri u tom prozoru
može izvaditi kasniju diskretnu naredbu. Primjer `Play, Cue, fader` može na
wireu postati `Cue, Play`.

#### Implementacijski put

1. Ukloniti svaki producer-side `xQueueReceive()` nad shared transport queueom.
2. Podijeliti događaje prema semantici:
   - diskretni edgeovi: strogi bounded FIFO;
   - held-state vrijednosti: postojeći durable desired-state reconciler;
   - apsolutne kontinuirane kontrole: latest-value slot + dirty bit;
   - relativne jog delte: saturirajući per-control akumulator.
3. Jedini consumer/translator task radi scheduling:
   - prvo nužni held-state release/reconciliation;
   - zatim FIFO diskretne naredbe;
   - zatim pending kontinuirane vrijednosti prema bounded budgetu.
4. Dodati telemetriju za FIFO full, coalesced continuous, saturated jog i
   maksimalnu queue dubinu.

#### Obvezni testovi

- Fake-FreeRTOS test koji namjerno prekida producer nakon prvog queue pristupa i
  pokreće consumer.
- Nizovi Play/Cue, Load/Cue, loop enter/exit i hot-cue press/release moraju
  zadržati FIFO redoslijed.
- Continuous burst smije koalescirati međuvrijednosti, ali posljednja vrijednost
  mora stići.
- Held release se ne smije izgubiti pod capacity+1 i stalled-link scenarijem.

#### Acceptance

- Oba host suitea prolaze jer se mijenja shared control-link ponašanje.
- Oba firmware targeta buildaju se s ESP-IDF v6.0.2.
- Fizički FLX4 burst/stalled-link/reconnect smoke bez obrnutih diskretnih
  naredbi i bez latchanog statea.

### CR-20260816-P1-03 — Profil nije vezan uz USB identitet i connection epoch

Status: **OPEN**

Primarne lokacije:

- `firmware/control-board-s3/main/app_main.c:120-127`
- `firmware/control-board-s3/main/app_main.c:183-194`
- `firmware/control-board-s3/main/app_main.c:352-367`
- `firmware/control-board-s3/components/controller_profile_runtime/controller_profile_runtime.c:70-75`
- `firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c:850-918`
- `firmware/main-deck-p4/components/controller_profile_manager/controller_profile_manager.c:1027-1050`

#### Kvar

Nakon disconnecta aktivni profil i snapshot ostaju u memoriji. Ako se priključi
drugi ili nepodržani uređaj, njegovi MIDI paketi mogu biti mapirani kroz profil
prethodnog uređaja, a reconnect može replayati stare fader/knob vrijednosti.

#### Implementacijski put

1. U S3 uvesti connection context:
   `{connected, vid, pid, interface_id, connection_epoch}`.
2. USB owner povećava epoch na svaku novu prihvaćenu konekciju.
3. Na `DEV_GONE`:
   - deaktivirati dynamic profil;
   - invalidirati built-in i dynamic input snapshot;
   - objaviti sve potrebne held releases;
   - označiti LED desired state kao nedostupan, ne slati vendor fallback.
4. Profile activation frame i spremljeni runtime moraju nositi očekivani
   VID/PID i connection epoch.
5. Do uspješne aktivacije profila za isti epoch ulaz ostaje u karanteni.
6. Built-in FLX4 map aktivirati samo nakon descriptor potvrde FLX4 VID/PID-a.
7. P4 na disconnect šalje `PROFILE_CLEAR` i uklanja stari connection/profile
   binding.

#### Obvezni testovi

- Controller A s profilom → disconnect → nepodržani B.
- A → disconnect → B prije nego P4 stigne poslati novi profil.
- Stari profile frame koji kasni nakon novog epocha mora biti odbijen.
- Reconnect istog FLX4 uređaja mora dobiti čist snapshot, bez vrijednosti stare
  sesije.

#### Acceptance

- S3/P4 profile, runtime, parity i control-link suiteovi prolaze.
- Fizička zamjena FLX4 ↔ drugi MIDI uređaj ne smije proizvesti semantički
  control event prije ispravnog profile bindinga.
- `generic_midi_ci` ostaje host fixture, ne hardware dokaz generičke podrške.

### CR-20260816-P1-04 — TRIM dolazi nakon int16 EQ clippinga

Status: **OPEN**

Primarne lokacije:

- `firmware/main-deck-p4/components/audio_engine/audio_eq.c:24-39`
- `firmware/main-deck-p4/components/audio_engine/audio_eq.c:127-138`
- `firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c:201-225`
- `firmware/main-deck-p4/components/audio_engine/audio_engine.c:4787-4818`
- `firmware/main-deck-p4/components/audio_engine/include/audio_output_mixer.h:24-53`

#### Kvar

EQ može boostati signal iznad `int16_t` raspona i odmah ga hard-clampati. TRIM
se primjenjuje tek kasnije, pa samo stišava već izobličeni signal. Master
limiter i njegova telemetrija ne vide stvarni pre-clamp overload.

#### Implementacijski put

1. Razdvojiti gainove u eksplicitni signal chain:
   source → TRIM/pre-gain → EQ → filter/FX → channel fader/crossfader →
   summing → master trim/limiter → sink conversion.
2. Deck DSP frame držati u `float` ili najmanje `int32_t` formatu.
3. Ukloniti interni `int16_t` clamp iz EQ/filter/FX međukoraka.
4. Definirati headroom i jedino mjesto završne saturacije.
5. PFL uskladiti sa željenom post-trim/pre-fader semantikom.
6. Limiter telemetry računati nad stvarnim pre-limit float/int32 signalom.

#### Obvezni testovi

- Full-scale sine za svaki zasebni EQ band boost.
- Sva tri banda na maksimumu.
- Multitone i dva glasna decka.
- Spuštanje TRIM-a mora ukloniti pre-DSP clipping, ne samo smanjiti izlaznu
  amplitudu.
- PFL mora reagirati na TRIM, ali ne na channel fader ako je tako definiran
  proizvodni signal chain.

#### Acceptance

- Audio EQ/output mixer testovi i 300 s dual-deck soak prolaze.
- P4 clean build prolazi.
- Fizički slušni test i limiter/peak telemetrija s glasnim materijalom ne
  pokazuju flat-top clipping prije master limitera.

### CR-20260816-P1-05 — P4 signed bundle nije vezan uz channel release

Status: **OPEN**

Primarne lokacije:

- `firmware/main-deck-p4/components/p4_ota_pull/p4_ota_pull.c:208-233`
- `firmware/main-deck-p4/components/p4_ota_pull/p4_ota_pull.c:325-345`
- `firmware/main-deck-p4/components/p4_ota_pull/p4_ota_pull.c:487-490`
- `firmware/main-deck-p4/components/p4_ota/p4_ota.c:208-219`

#### Kvar

Channel release prolazi newer-only provjeru, ali signed
`manifest.version` nije uspoređen s ponuđenim releaseom. Channel može oglasiti
novu verziju, a usmjeriti uređaj na legitimno potpisani stariji bundle.
Image-to-manifest provjera prolazi i downgrade je moguć unatoč newer-only
namjeri.

#### Implementacijski put

1. Kopirati ponuđeni release u immutable install task context.
2. Nakon signature/manifest provjere, ali prije `p4_ota_begin()`:
   - zahtijevati exact byte/string equality između offer releasea i
     `manifest.version`;
   - ponovno usporediti signed verziju s running verzijom i zahtijevati
     `NEWER`.
3. Zamijeniti bounded prefix `strncmp` exact-length usporedbom.
4. Publishing alat treba izvesti channel release iz već potpisanog bundlea,
   umjesto da prihvati dva neovisna version inputa.
5. Razmotriti potpisivanje channel manifesta ako publishing threat model to
   zahtijeva.

#### Obvezni testovi

- New channel + stari valjano potpisani bundle: reject prije erasea.
- Channel/bundle version prefix mismatch: reject.
- Jednaka running verzija: reject.
- Starija signed verzija: reject.
- Ispravna novija verzija: postojeći happy path ostaje PASS.

#### Acceptance

- P4 host OTA suite, signing suite i build prolaze.
- Hardware pull OTA potvrđuje novi image.
- Mismatch test potvrđuje da aktivna boot particija nije promijenjena.

### CR-20260816-P1-06 — Retired speaker PA aktivira se pri bootu

Status: **OPEN**

Primarne lokacije:

- `firmware/main-deck-p4/main/app_main.c:342-344`
- `firmware/main-deck-p4/components/app_settings/app_settings.c:18-35`
- `firmware/main-deck-p4/components/app_settings/app_settings.c:113-121`
- `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c:113-116`
- `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c:415-421`
- `firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880.c:513-555`
- `docs/HARDWARE_WIRING.md:55-60`

#### Kvar

Default ili stari NVS `audio_out=0` bira speaker route. Iako je ES8311 product
put isključen, route logika i dalje može podići PA GPIO11. To krši dokumentirani
safe-off invariant umirovljenog speaker/codec puta.

#### Implementacijski put

1. GPIO11 konfigurirati low što ranije u BSP bootu, prije učitavanja NVS-a.
2. Product default postaviti na PCM5102A/RCA.
3. Kada je `CONFIG_BSP_PCM5102A_MAIN_OUT=y` i ES8311 isključen:
   - compile-time ukloniti speaker route iz dozvoljenih izlaza;
   - `bsp_audio_set_output(SPEAKER)` mora vratiti jasan error bez promjene GPIO-a.
4. Uvesti NVS schema/version migration:
   - stare speaker vrijednosti mapirati na RCA;
   - commitati migriranu sigurnu vrijednost;
   - logirati jednokratnu migraciju.
5. UI ne smije prikazivati retired speaker opciju u product konfiguraciji.

#### Obvezni testovi

- Static/source gate da nijedan product boot put ne poziva PA enable kada je
  ES8311 isključen.
- NVS migration test za praznu, staru i nevaljanu vrijednost.
- BSP test da speaker request vraća grešku i ostavlja PA low.

#### Acceptance

- P4 host suite i build prolaze.
- Fizički logic-analyzer/DMM dokaz da GPIO11 ostaje low od reseta kroz puni
  boot, Settings restore, playback, USB reconnect i OTA reboot.

## 6. P2 tracker i put popravaka

### 6.1 P2 release disposition

Pojam “primjenjivi P2 release gate” u ovom dokumentu znači:

- **Core release gateovi:** CR-20260816-P2-03, CR-20260816-P2-04,
  CR-20260816-P2-05, CR-20260816-P2-06, CR-20260816-P2-08,
  CR-20260816-P2-09, CR-20260816-P2-10, CR-20260816-P2-11,
  CR-20260816-P2-12, CR-20260816-P2-13 i CR-20260816-P2-14 moraju biti
  zatvoreni prije novog production kandidata.
- **Uvjetni feature gateovi:** CR-20260816-P2-01 i CR-20260816-P2-02 moraju
  biti zatvoreni prije oglašavanja ili omogućavanja generičkog non-FLX4
  controller puta. CR-20260816-P2-16 mora biti zatvoren prije distribucije ili
  produkcijske uporabe web-profile convertera. Alternativa je eksplicitno
  uklanjanje/karantena tih featurea iz production scopea.
- **Mjereni real-time gate:** CR-20260816-P2-07 mora biti popravljen ili
  zatvoren datiranim P4 hardware deadline mjerenjem koje dokazuje dovoljnu
  marginu u najgorem dual-deck scenariju. Zeleni PC soak nije dovoljan.
- **Security decision gate:** CR-20260816-P2-15 mora biti popravljen ili
  formalno prihvaćen prema pravilima za **ACCEPTED RISK**, uz zaseban
  provisioning plan prije tvrdnje da je release security-provisioned.

### 6.2 S3 MIDI/profile/control-link

| ID | Status | Nalaz i dokaz | Popravak | Obvezni gate |
| --- | --- | --- | --- | --- |
| CR-20260816-P2-01 | **OPEN** | Non-FLX4 profil dobiva FLX4 fallback LED poruke za nemapirane LED-ove (`control_link_uart.c:103-120`, `flx4_led_midi.c:121-163`). | Aktivni non-FLX4 profil mora biti autoritativan; missing output mapping znači drop. Fallback dopustiti samo uz potvrđen FLX4 VID/PID ili eksplicitni compatibility flag. | Generic fixture s unmapped LED-ovima ne šalje nijedan paket; FLX4 parity ostaje PASS. |
| CR-20260816-P2-02 | **OPEN** | Legalni `0x8n` Note Off s nenultom release velocity ne može se ispravno mapirati (`controller_profile.c:174-187`). | Normalizirati `0x8n` u odgovarajući `0x9n` s velocity 0 prije mappinga ili proširiti schema edge semantikom. | Press `0x90/0x7f` + release `0x80/0x40` mora završiti released stateom za Cue, jog touch i pad. |
| CR-20260816-P2-03 | **OPEN** | UART sequence rezervira se prije stvarne TX serializacije (`control_link_uart.c:57-75`). | Jedan TX-owner task ili globalni lock preko sequence allocationa, frame builda i cijelog UART writea za sve A5/A6 API-je. | Two-producer forced-interleaving test; wire sequence uvijek monotona modulo 256. |
| CR-20260816-P2-04 | **OPEN** | Disconnect false šalje se samo kao jednokratni edge (`flx4_midi_host.c:460-509`, `:917-918`). | Desired/sent/dirty model i periodični replay connected i disconnected stanja; send failure ostavlja dirty. | Izgubljeni disconnect frame mora se sam ispraviti bez novog replug ciklusa. |
| CR-20260816-P2-05 | **OPEN** | Non-VU LED state nestaje na queue-full/USB-submit grešci (`flx4_midi_host.c:559-591`, `:1188-1201`). | Durable desired LED state po `(id, deck)`; USB-owner retry. VU ostaje latest-only. Dodati retry/drop brojače. | Submit fail i queue-full test završavaju točnim Play/Cue/Sync LED stanjem. |
| CR-20260816-P2-06 | **OPEN** | Built-in snapshot ima data race, dynamic snapshot drži mutex kroz UART write (`app_main.c:75-86`, `:120-127`, `:352-367`, `controller_profile_runtime.c:124-133`). | Pod kratkim lockom kopirati bounded event snapshot, otključati, zatim slati. Jedan owner za built-in map/snapshot je preferiran. | Svaki replay odgovara jednoj koherentnoj zaključanoj ili versioned kopiji statea; UART callback se izvršava nakon otključavanja, a USB callback latency ostaje bounded. |

### 6.3 P4 audio/UI

| ID | Status | Nalaz i dokaz | Popravak | Obvezni gate |
| --- | --- | --- | --- | --- |
| CR-20260816-P2-07 | **OPEN** | Pitch resampler koristi softverski `double` po sampleu (`audio_resampler.c:16-56`); P4 disassembly sadrži `*df2` helpere. | Float ili Q-format phase accumulator; step računati po bloku/promjeni pitcha. CI object/assembly gate zabranjuje `*df2` u RT DSP objektima. | Drift/quality suite, 300 s soak, P4 hardware deadline mjerenje. |
| CR-20260816-P2-08 | **OPEN** | I2S write koristi `portMAX_DELAY` i PCM je već potrošen prije provjere (`audio_engine.c:2733-2744`, `:3013-3052`). | Bounded timeout 2–3 perioda, sink-specific success, short-write/error counters, controlled ERROR/reinit; STOP mora disableati/probuditi kanal. | Fault injection za error, short write i trajno blokiran write; nema taska nakon STOP-a. |
| CR-20260816-P2-09 | **OPEN** | FLAC backend fault pretvara se u običan EOF (`audio_engine.c:1499-1502`, `:1581-1593`). | Callback context objavljuje fault epoch/status; razlikovati EOF od short read; koristiti isti bounded retry koncept kao WAV/MP3. | FLAC zero/partial read preko page granice završava retryjem ili MEDIA READ ERR, ne tihim EOF-om. |
| CR-20260816-P2-10 | **OPEN** | 32-bit timeline nije wrap-safe (`audio_pcm_timeline.c:49-160`). | Modularne unsigned-distance usporedbe uz span manji od `2^31` i fizički anchor, ili epoch + 32-bit cursor. | Testovi seedani uz `UINT32_MAX` za push/pop/read/seek/loop/key-lock. |
| CR-20260816-P2-11 | **OPEN** | Scratch fast re-grab ima control/output data race nad gainom i phaseom (`audio_engine.c:2578-2638`, `:4034-4054`). | Control objavljuje samo command/epoch; output task je jedini owner gain/phase/scratch statea i primjenjuje promjenu na block boundaryju. | Deterministički re-grab tijekom FADE_OUT-a bez amplitude skoka. |
| CR-20260816-P2-12 | **OPEN** | OOM pri alokaciji UI load-resulta ostavlja gumbe disabled (`ui_library.c:569-578`, `:842-849`). | Prealokirani/fiksni mali result token; svaki worker exit mora poslati završni rezultat, a LVGL task vraća UI state. | Injection pada obje alokacije i provjera da su gumbi ponovno omogućeni uz vidljivu grešku. |

### 6.4 OTA, sigurnost i tooling

| ID | Status | Nalaz i dokaz | Popravak | Obvezni gate |
| --- | --- | --- | --- | --- |
| CR-20260816-P2-13 | **OPEN** | S3 OTA upload nema ukupni deadline ni minimalni throughput (`s3_debug_ap.c:329-455`). | Apsolutni wrap-safe deadline, progress window, rani max Content-Length, manifest-first receive i brzo zatvaranje sesije. | Fake slow-client od jednog bajta po timeoutu mora završiti u bounded vremenu; server ostaje dostupan. |
| CR-20260816-P2-14 | **OPEN** | S3 markira image VALID bez potvrđenog P4↔S3 prometa (`app_main.c:394-435`, `firmware_health.c:76-97`). | Uvesti eksplicitni P4→S3 health ACK/heartbeat ili boot challenge-response, jer postojeći heartbeat ide S3→P4. Dvostupanjski gate zahtijeva taj odgovor i critical-task liveness prije mark-valid; inače slijedi restart bez potvrde. | `init OK, no link traffic`, oba redoslijeda bootanja i izgubljeni/prerano poslani ACK moraju završiti rollbackom ili bounded ponavljanjem handshakea; odsutan FLX4 nije failure uvjet. |
| CR-20260816-P2-15 | **OPEN** | Debug AP koristi javni statični PSK i nema operator autentikaciju (`s3_debug_ap.h:9-11`, `RISK_REGISTER.md`). | Per-device PSK ili kratkotrajni maintenance token na P4 UI-ju; fizička rollback potvrda; AP idle timeout, rate limiting i PMF/WPA3 gdje je podržano. | Neautenticirani klijent ne može mutirati stanje; servisni signed rollback ostaje moguć uz fizičku autorizaciju. |
| CR-20260816-P2-16 | **OPEN** | `convert_web_profile.py` gubi nulu, ne parsira hex range, pogrešno spaja deck LED adrese i mapira key-lock u tempo range (`:263-264`, `:324-342`, `:547-594`). | `first_present()` umjesto `or` fallbacka, `int(value, 0)`, potpuna schema/range provjera, odbijanje nepredstavljive semantike, bez implicitnih FLX4 adresa; izlaz obavezno provući kroz `compile_profile`. | Zero/hex/deck2-only/mismatched-address/key-lock fixturei; converter test uključen u S3 host runner/CI. |

## 7. P3 tracker

| ID | Status | Nalaz | Popravak i gate |
| --- | --- | --- | --- |
| CR-20260816-P3-01 | **OPEN** | Djelomičan Debug AP netif init može ostaviti `s_ap_netif` postavljen uz ugašen DHCP (`s3_debug_ap.c:538-603`). | Staged local ownership; global objaviti nakon punog uspjeha. Failure injection za svaki init korak i uspješan OFF→ON retry. |
| CR-20260816-P3-02 | **OPEN** | Neuspjela alokacija profile runtime mutexa tiho prelazi u unlocked rad (`controller_profile_runtime.c:16-35`). | Koristiti `StaticSemaphore_t` ili vratiti `esp_err_t` i kontrolirano zaustaviti startup; allocation-failure test. |
| CR-20260816-P3-03 | **OPEN** | `s_headphone_mode` i limiter snapshot imaju preostale cross-core plain-field raceove (`audio_engine.c:354-358`, `:4869-5051`). | Packed atomic za routing i sequence/seqlock snapshot za limiter telemetriju; threaded consistency test. |
| CR-20260816-P3-04 | **OPEN** | Produkcijski USB lifecycle, OTA crypto/state machine i HTTP handler nisu ponašajno izvršeni u host suiteu (`run_s3_host_tests.ps1:500-663`). | Fake USB/FreeRTOS/`esp_ota_*`/partition/PSA sloj koji gradi pravi production source; svaki negativni put potvrđuje abort i da boot partition nije promijenjena. |

## 8. Arhitektonska poboljšanja nakon obveznih nalaza

Ove promjene ne treba spajati u P1 PR-ove bez mjerenja; služe kao sljedeći
korak nakon correctness popravaka.

### 8.1 Per-deck audio ownership

Globalni `AE_LOCK` i shared MP3 scratchpad još serijaliziraju oba decoder taska
i dio output bookkeepinga. Nakon lifecycle popravka:

1. uvesti per-deck decode state i scratchpad;
2. odvojiti control/lifecycle lock od DSP/data locka;
3. decoder → PCM koristiti jasni SPSC ownership;
4. zajedničku telemetriju objavljivati atomikom ili versioned snapshotom;
5. refaktor opravdati stvarnim P4 worst-case deadline mjerenjem.

Referenca: `docs/AUDIO_ENGINE_PER_DECK_LOCK_PLAN.md`.

### 8.2 Jedinstveni PFL/MAIN signal-chain ugovor

Dokumentirati i testirati:

- TRIM/pre-gain prije EQ-a;
- PFL post-trim, post-EQ i pre-fader;
- MAIN post-fader/crossfader;
- master limiter samo na MAIN summingu;
- gdje se mjere deck VU, PFL peak i master limiter telemetry.

### 8.3 Durable control-state konvergencija

Held state, connection state i non-VU LED state trebaju koristiti isti
desired/sent/dirty koncept. Time se smanjuje broj jednokratnih edgeova koji se
ne mogu oporaviti nakon UART ili USB greške. VU i high-rate kontinuirane
vrijednosti ostaju latest-only.

### 8.4 Production security

Secure Boot i flash encryption ostaju **PROVISIONING DECISION**, ne običan
source checkbox. Potrebno je definirati:

- proizvodni signing-key custody i rotaciju;
- per-device credential provisioning;
- žrtveni board za eFuse/provisioning probu;
- factory recovery i RMA proceduru;
- odnos servisnog signed rollbacka prema fizičkoj autorizaciji;
- release manifest i budući SPDX/CycloneDX SBOM alat koji je verzijski pinan.

### 8.5 Bootloader headroom

P4 bootloader ima približno 5% slobodnog prostora. Svaka nova bootloader,
rollback, secure-boot ili recovery funkcija mora imati size gate. Ne dodavati
bootloader feature bez:

1. size delta izvještaja;
2. provjere granica particije;
3. wired recovery testa;
4. procjene prostora za Secure Boot/flash-encryption konfiguraciju.

## 9. Preporučeni redoslijed implementacije

Svaki red predstavlja zaseban, pregledljiv PR ili vrlo jasno odvojen commit
paket. Veliki cross-subsystem PR koji istodobno mijenja lifecycle, DSP, OTA i
transport nije prihvatljiv jer otežava dokazivanje regresije.

| Redoslijed | Paket | Sadržaj | Izlazni gate |
| ---: | --- | --- | --- |
| 0 | Reprodukcijski testovi | Dodati failing testove za svaki P1 prije funkcionalne izmjene | Testovi potvrđeno padaju na auditiranom commitu |
| 1 | Hardware-safe boot | CR-20260816-P1-06: PA low, safe default, NVS migration, compile-time route guard | P4 host/build + fizički GPIO low |
| 2 | OTA version binding | CR-20260816-P1-05: offer ↔ signed manifest equality i signed newer-only recheck | P4 OTA/signing/build + mismatch hardware test |
| 3 | P4 lifecycle ownership | CR-20260816-P1-01: per-deck owner/generation, stop barrier, UI load ID | P4 host/UI/build + USB/EJECT/OTA hardware matrix |
| 4 | S3 queue ownership | CR-20260816-P1-02 i CR-20260816-P2-03: bez producer draina, jedan TX ordering owner | Oba host suitea/builda + FLX4 burst/link smoke |
| 5 | Controller connection epoch | CR-20260816-P1-03, CR-20260816-P2-01, CR-20260816-P2-02 i CR-20260816-P2-06 | Profile/USB tests + fizička device-swap provjera |
| 6 | Audio precision/signal chain | CR-20260816-P1-04 i PFL ugovor | Audio suite/soak/build + slušni/telemetry test |
| 7 | Audio fault recovery | CR-20260816-P2-08, CR-20260816-P2-09, CR-20260816-P2-10 i CR-20260816-P2-11 | Fault injection, wrap testovi i hardware I2S test |
| 8 | State convergence | CR-20260816-P2-04 i CR-20260816-P2-05 | Lost-frame/submit-failure recovery + LED smoke |
| 9 | OTA health/availability | CR-20260816-P2-13, CR-20260816-P2-14 i CR-20260816-P3-01 | Slow-client, rollback i DHCP retry test |
| 10 | Profile tooling | CR-20260816-P2-16 i compiler round-trip gate | Converter fixturei u CI-u |
| 11 | RT performance | CR-20260816-P2-07, per-deck lockovi samo prema mjerenju | Bez `*df2`, drift PASS, hardware deadline margin |
| 12 | Production hardening | CR-20260816-P2-15, Secure Boot/flash encryption, credentials | Dokumentiran provisioning i security acceptance |
| 13 | Full acceptance | Višesatni USB/audio/UI/control/OTA soak | Datirani validation zapis bez otvorenog P1 i s razriješenim svim primjenjivim P2 gateovima iz 6.1 |

## 10. Minimalni Definition of Done za svaki remediation PR

1. Nalaz ima stabilan ID iz ovog dokumenta.
2. Dodan je test koji reproducira kvar, ne samo source-text guard.
3. Dokumentirano je da test pada na starom kodu.
4. Implementacija ima jasno vlasništvo statea i teardown redoslijed.
5. Pokrenut je relevantni puni host suite.
6. P4 promjena zahtijeva P4 clean ESP-IDF v6.0.2 build.
7. S3 ili shared-protocol promjena zahtijeva oba host suitea i oba builda.
8. UI promjena zahtijeva headless exact screenshot gate.
9. Audio DSP/cache/timeline promjena zahtijeva 300 s dual-deck soak.
10. OTA promjena zahtijeva signing suite i assertion da negativni put ne bira
    novu boot particiju.
11. `git diff --check` prolazi.
12. `dependencies.lock` se nije nenamjerno promijenio.
13. Status nalaza i remediation log u ovom dokumentu su ažurirani.
14. Ako je acceptance hardware-specifičan, status ostaje
    **SOFTWARE FIXED; HW PENDING** dok datirani dokaz nije commitan.

## 11. Hardware acceptance matrica

### USB i controller

- cold boot bez FLX4 i s već priključenim FLX4;
- najmanje 50 FLX4 replug ciklusa;
- zamjena FLX4 ↔ drugi MIDI uređaj;
- disconnect tijekom held scratch/Cue/Shift/pad stanja;
- disconnect tijekom profile transfera i input snapshot replaya;
- MIDI burst uz puni UART queue;
- LED submit failure i reconnect resync.

### Audio

- realni MP3, WAV i FLAC fixturei;
- oba decka, key-lock, scratch, sync, hot cue, loop i Beat FX;
- full-scale EQ boost uz različite TRIM vrijednosti;
- I2S short write, timeout i recovery;
- višesatni loop koji prelazi modeliranu timeline wrap granicu ili ekvivalentni
  instrumentirani accelerated test;
- CPU/I2S deadline i lock telemetry pod najgorim dual-deck opterećenjem;
- slušni test clickova, dropova, clippinga i pitch stabilnosti.

### UI i lifecycle

- USB removal tijekom svakog load koraka;
- EJECT/LOAD burst;
- OTA stop tijekom aktivnog loada i playbacka;
- memory-allocation failure za load result;
- stale worker nikad ne mijenja noviji deck/UI state;
- Settings restore i screensaver nakon greške.

### OTA i sigurnost

- channel/signed-version mismatch;
- pogrešan potpis, SHA, size, target, project i version;
- slow-client i fragmentirani upload;
- P4↔S3 link ne proradi nakon S3 OTA-a;
- power loss prije i poslije boot-partition promjene;
- rollback i factory recovery;
- neautenticirani control/OTA zahtjevi;
- AP DHCP init failure i OFF→ON retry.

### Hardware-safe boot

- GPIO11 low od reseta kroz puni boot;
- prazna, stara, nevaljana i migrirana NVS audio-output vrijednost;
- PCM5102A MAIN i FLX4 cue ostaju funkcionalni;
- OTA reboot i factory reset ne vraćaju retired speaker route.

## 12. Release gate

Nova release kandidat verzija ne smije biti označena kao hardware-accepted ili
production-approved dok vrijedi bilo što od sljedećeg:

- otvoren je bilo koji P1 nalaz;
- nije razriješen neki primjenjivi P2 gate prema klasifikaciji u odjeljku 6.1;
- P1 ima samo source-text test bez behavioral reprodukcije;
- relevantan hardware gate nije izvršen;
- P4 bootloader prelazi sigurnu size granicu;
- OTA negative-path test može promijeniti boot partition;
- stari audio/controller task može preživjeti potvrđeni STOP/disconnect;
- retired PA GPIO može postati high;
- production credential/provisioning odluka nije donesena, a release se
  predstavlja kao security-provisioned.

Zeleni build, host suite ili uspješan OTA transport nisu samostalni dokaz
funkcionalnog, hardware ili security acceptancea.

## 13. Remediation log

Ovaj odjeljak ažurirati nakon svakog paketa, bez brisanja povijesti.

| Datum | Nalaz(i) | Commit/PR | Novi status | Pokrenuti gateovi | Preostalo |
| --- | --- | --- | --- | --- | --- |
| 2026-08-16 | Svi CR-20260816 nalazi | `10c91c2aa536be3852cdd6a41e831088d85625d7` | Audit baseline; P1/P2/P3 OPEN | Oba host suitea, oba clean builda, UI E2E, signing, audio soak | Implementacija i navedeni hardware gateovi |
