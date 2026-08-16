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

Status: **SOFTWARE FIXED; HW PENDING**

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

#### Implementirano 2026-08-16

Per-deck non-recursive lifecycle mutex sada obuhvaća cijeli LOAD/STOP i
task create/join transakciju. Monotone runtime i javne session generacije
sprječavaju da stari task ili UI completion objavi ili zaustavi noviju sesiju.
USB removal, push OTA i pull OTA koriste globalni LOAD admission barrier koji
ostaje zatvoren kroz teardown i prijelaz, uz kontrolirani resume na grešci.
UI koristi zaseban monotoni `load_id`, cancellation-aware single-flight gate i
nelossy completion queue; otkazani worker mora predati i očistiti vlastitu
session generaciju prije novog LOAD-a.

Behavioral host test zaustavlja LOAD između internog stopa i binda te potvrđuje
da konkurentni STOP ne može prerano vratiti uspjeh. Dodatni testovi potvrđuju
stale-session conditional STOP, globalni transition barrier, task-context
generation invalidaciju i cancellation semantics UI gatea. P4 host suite,
exact screenshot UI E2E i ESP-IDF v6.0.2 build prolaze. Preostaje fizička
50-ciklusna USB/EJECT/OTA matrica i potvrda da nema živih taskova nakon STOP-a.

### CR-20260816-P1-02 — S3 producer drain/requeue mijenja FIFO redoslijed

Status: **SOFTWARE FIXED; HW PENDING**

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

Status: **SOFTWARE FIXED; HW PENDING**

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

#### Implementirano 2026-08-16

Commiti `7f10aa1` i `aac07bc` uvode descriptor-prošireni nonzero 32-bitni
`connection_epoch`, koherentan S3 USB connection context i epoch-bound dynamic
runtime. Disconnect briše dynamic runtime, built-in snapshot, scheduler i held
state, a P4 šalje `PROFILE_CLEAR`, invalidira queued stare descriptor reportove
i deduplicira transfer samo unutar istog epocha. Built-in input/output map
dopušten je samo za descriptor-potvrđeni FLX4; drugi uređaji ostaju u karanteni
do aktivacije profila za aktualni VID/PID/epoch.

Behavioral regresije pokrivaju epoch increment i disconnect kontekst, descriptor
wire round-trip, stale/same-epoch identity rejection i `UINT32_MAX` wrap,
generički output drop, legalni `0x8n` Note Off te reentrant snapshot callback
koji bi pao da se callback još izvršava pod runtime mutexom. Puni S3 i P4 host
suiteovi prolaze. S3 i P4 ESP-IDF v6.0.2 buildovi prolaze; imagei su `0xf3c20`
(5% app particije slobodno) i `0x251220` (42% slobodno). `dependencies.lock`
nije promijenjen i `git diff --check` prolazi.

Preostaje fizički FLX4 ↔ drugi MIDI uređaj swap/reconnect/late-transfer smoke.
Descriptor payload je istodobno promijenjen na oba targeta; mješovita stara/nova
verzija sigurno degradira generički profile put do nadogradnje oba targeta i ne
smije se koristiti kao dokaz generičke hardware podrške.

### CR-20260816-P1-04 — TRIM dolazi nakon int16 EQ clippinga

Status: **SOFTWARE FIXED; HW PENDING**

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

#### Implementirano 2026-08-16

Commit `762cbe3` uvodi eksplicitni `audio_dsp_frame_t` i drži oba kanala kao
single-precision float od source/resamplera do izlaznog sinka. Channel TRIM se
primjenjuje prije EQ-a; EQ, channel filter, Pad FX, Beat FX filter, flanger i
echo/delay više ne vraćaju `int16_t` međurezultat. Channel fader/crossfader
primjenjuju se nakon DSP-a, oba decka se zbrajaju u wide formatu, a master
volume/trim i jedini MAIN soft limiter dolaze neposredno prije sink konverzije.
PFL koristi isti post-TRIM/post-DSP frame prije channel fadera; headphone sink
ima samo završnu PCM saturaciju i ne mijenja MAIN limiter telemetriju. VU meter
čita wide post-TRIM/post-DSP peak pa interni overload više nije skriven ranom
saturacijom.

Regresije pokrivaju full-scale-ish sine za low/mid/high boost, sva tri EQ banda
na maksimumu, dva glasna multitone decka, TRIM uklanjanje pre-DSP overlouda,
PFL neovisnost o zatvorenom channel faderu, master trim nakon zbroja te wide
delay/flanger headroom bez internog clampanja. Puni P4 host suite prolazi
(audio engine 383/383), 300 s dual-deck soak prolazi s driftom 0, bez clippinga
i clickova, a P4 ESP-IDF v6.0.2 build daje image `0x252d80` uz 42% slobodne
najmanje app particije. `dependencies.lock` nije promijenjen i
`git diff --check` prolazi.

Preostaje fizički slušni/telemetry test s glasnim realnim materijalom i
istodobnim MAIN/PFL izlazom; zato nalaz nije hardware zatvoren.

### CR-20260816-P1-05 — P4 signed bundle nije vezan uz channel release

Status: **SOFTWARE FIXED; HW PENDING**

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

Status: **SOFTWARE FIXED; HW PENDING**

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
| CR-20260816-P2-01 | **SOFTWARE FIXED; HW PENDING** | Non-FLX4 profil dobiva FLX4 fallback LED poruke za nemapirane LED-ove (`control_link_uart.c:103-120`, `flx4_led_midi.c:121-163`). | Aktivni non-FLX4 profil mora biti autoritativan; missing output mapping znači drop. Fallback dopustiti samo uz potvrđen FLX4 VID/PID ili eksplicitni compatibility flag. | Generic fixture s unmapped LED-ovima ne šalje nijedan paket; FLX4 parity ostaje PASS. |
| CR-20260816-P2-02 | **SOFTWARE FIXED; HW PENDING** | Legalni `0x8n` Note Off s nenultom release velocity ne može se ispravno mapirati (`controller_profile.c:174-187`). | Normalizirati `0x8n` u odgovarajući `0x9n` s velocity 0 prije mappinga ili proširiti schema edge semantikom. | Press `0x90/0x7f` + release `0x80/0x40` mora završiti released stateom za Cue, jog touch i pad. |
| CR-20260816-P2-03 | **SOFTWARE FIXED; HW PENDING** | Svi S3 `0xA5`/`0xA6` send API-ji prolaze kroz jedan statički mutex koji u istoj kritičnoj sekciji rezervira sequence, gradi cijeli frame i izvršava UART write. | `control_link_tx_serializer` je jedini sequence owner; neuspjeli write namjerno troši sequence kako bi P4 telemetry vidio gubitak. | Pthread forced-interleaving test potvrđuje wire redoslijed i monotoni sequence; oba host suitea i S3 v6.0.2 build prolaze. Preostaje fizički multicore UART burst/stall smoke. |
| CR-20260816-P2-04 | **SOFTWARE FIXED; HW PENDING** | Connection state koristi desired/sent/dirty model; i connected i disconnected stanje periodički se replayaju iz USB-owner taska, a UART kvar ostavlja dirty. | USB edge samo mijenja desired state i callback; slanje i retry ostaju u USB owneru. Descriptor ima zaseban retry flag kako periodični disconnect replay ne bi stvarao lažni descriptor promet. | Host test gubi disconnect send i potvrđuje samoispravak bez replug ciklusa; S3 host/build prolaze. Preostaje fizički UART fault/recovery smoke. |
| CR-20260816-P2-05 | **SOFTWARE FIXED; HW PENDING** | Svaki non-VU LED ima durable desired/known/dirty slot; queue-full ostavlja slot dirty, a stale completion ne briše noviju vrijednost. USB OUT zadržava isti transfer payload kroz synchronous submit i retryable completion kvar. | Bounded S3 flush retrya non-VU state; VU ostaje best-effort/latest-only. `midi_out_retry_state` vodi submit/completion/disconnect brojače. | Queue-full/latest-wins, stale-completion, mark-all-dirty i submit/completion retry testovi prolaze; oba host suitea i S3 build prolaze. Preostaje fizički FLX4 LED submit-fault/reconnect resync smoke. |
| CR-20260816-P2-06 | **SOFTWARE FIXED; HW PENDING** | Built-in snapshot ima data race, dynamic snapshot drži mutex kroz UART write (`app_main.c:75-86`, `:120-127`, `:352-367`, `controller_profile_runtime.c:124-133`). | Pod kratkim lockom kopirati bounded event snapshot, otključati, zatim slati. Jedan owner za built-in map/snapshot je preferiran. | Svaki replay odgovara jednoj koherentnoj zaključanoj ili versioned kopiji statea; UART callback se izvršava nakon otključavanja, a USB callback latency ostaje bounded. |

#### Implementirano 2026-08-16 — S3 transport i output konvergencija

Commit `6e4459a` zatvara software dio CR-20260816-P2-03 do P2-05. Novi
serializer serijalizira sequence allocation, frame build i cijeli UART write za
sve fixed i bulk frameove. Connection state i non-VU LED feedback više nisu
jednokratni edgeovi: desired state ostaje dirty do potvrđenog enqueue/write
koraka, a USB OUT zadržava već dequeued transfer buffer do uspješnog completiona.

Puni S3 i P4 host runneri prolaze, uključujući forced two-producer UART test,
connection lost-frame test, LED queue-full/stale-completion test i USB
submit/completion retry state test. S3 ESP-IDF v6.0.2 build prolazi s imageom
`0xec370` i 51% slobodnog najmanjeg OTA slota. Prvi clean pokušaj imao je
tranzijentni GCC ICE u nepromijenjenom IDF `esp_lcd_panel_rgb.c`; ponovljeni
inkrementalni compile istog objekta i cijeli build prošli su. Oba dependency
locka ostala su nepromijenjena. Statusi ostaju **SOFTWARE FIXED; HW PENDING**
do fizičkog UART/USB/FLX4 fault i reconnect smokea.

### 6.3 P4 audio/UI

| ID | Status | Nalaz i dokaz | Popravak | Obvezni gate |
| --- | --- | --- | --- | --- |
| CR-20260816-P2-07 | **OPEN** | Pitch resampler koristi softverski `double` po sampleu (`audio_resampler.c:16-56`); P4 disassembly sadrži `*df2` helpere. | Float ili Q-format phase accumulator; step računati po bloku/promjeni pitcha. CI object/assembly gate zabranjuje `*df2` u RT DSP objektima. | Drift/quality suite, 300 s soak, P4 hardware deadline mjerenje. |
| CR-20260816-P2-08 | **SOFTWARE FIXED; HW PENDING** | I2S write je koristio `portMAX_DELAY`, a pozicija se mogla objaviti i kada konfigurirani sink nije prihvatio blok. | `audio_output_sink` ograničava svaki driver poziv na jedan period i najviše tri short-write pokušaja, vodi zasebne call/short/timeout/error/failure brojače i nastavlja samo od neupisanog sufiksa. Pozicija se objavljuje tek kada svaki konfigurirani sink potvrdi blok; kvar zaustavlja output u eksplicitnom error stanju. STOP disablea PCM5102 kanal radi wakeupa, a sljedeći LOAD ga reconfigurira i ponovno enablea. | Host fault injection za puni/kratki/zero-progress/timeout/error write prolazi; P4 host suite i v6.0.2 build prolaze. Preostaje fizički blokirani-I2S/STOP/reload test i potvrda da nema taska nakon STOP-a. |
| CR-20260816-P2-09 | **SOFTWARE FIXED; HW PENDING** | FLAC backend fault pretvarao se u običan EOF. | Cache stream objavljuje monotoni fault epoch i točan byte offset za zero/partial read prije deklariranog kraja. FLAC init/read/seek razlikuju pravi EOF od backend kvara, zadržavaju stari decoder do uspješnog replacement-open/seek retryja i koriste postojeći bounded media-read fault budget. | Page-boundary partial-read regresija potvrđuje fault epoch, uspješan retry i zaseban pravi EOF. Preostaje realni FLAC USB read-fault fixture na P4. |
| CR-20260816-P2-10 | **SOFTWARE FIXED; HW PENDING** | 32-bit timeline nije bio wrap-safe. | Per-frame RV32 hot path ostaje 32-bit, a rijetki wrap koristi epoch + versioned koherentni 64-bit snapshot. Retained span je strogo manji od `2^31`, dostupnost koristi modularnu udaljenost, random read fizički anchor i scratch origin ostaju 64-bitni. | Timeline suite ima 289 provjera i seedove uz `UINT32_MAX` za push/pop/read/seek/drop-newest; 300 s dual-deck soak prolazi bez drifta, clicka ili clippinga. Preostaje višednevni/on-device umjetno seedani wrap acceptance. |
| CR-20260816-P2-11 | **SOFTWARE FIXED; HW PENDING** | Scratch fast re-grab imao je control/output data race nad gainom i phaseom. | Control/lifecycle task objavljuje samo packed command+epoch CAS-om; output task na block boundaryju jedini mijenja gain i handoff phase. Runtime reset također objavljuje command umjesto plain-field upisa. | Deterministički test potvrđuje da publisher ne mijenja amplitudu, output primjenjuje unity re-grab i zadnji re-grab pobjeđuje neprimijenjeni release. Preostaje fizički brzi re-grab/STOP/LOAD slušni stress. |
| CR-20260816-P2-12 | **SOFTWARE FIXED; HW PENDING** | Load-worker više ne alocira completion objekt: cijeli bounded rezultat živi na fiksnom 16 KiB task stacku, a `_Static_assert` jamči da zauzima najviše polovinu stacka. Svaki normalni worker exit zato može queueati rezultat i vratiti UI state čak i kada su internal heap i PSRAM iscrpljeni. | Uklonjene su obje result-allocation failure grane; request/queue/task-create failurei i dalje se sinkrono prikazuju iz LVGL taska i vraćaju gumbe. | Source gate zabranjuje povratak `calloc` completiona; UI load-gate 16/16, puni P4 host suite, exact screenshot E2E i P4 v6.0.2 build prolaze. Preostaje fizički heap-pressure LOAD smoke. |

#### Implementirano 2026-08-16 — P4 audio fault paths

Commit `3674ca9` zatvara source dio CR-20260816-P2-08 do P2-11. Puni P4
host runner prolazi, uključujući `audio_engine` 389/389, timeline 289/289,
FLAC preload/retry i novi output-sink fault-injection test. P4 ESP-IDF v6.0.2
build prolazi s imageom `0x2538b0` i 42% slobodnog najmanjeg app slota.
Deterministički 300 s dual-deck soak prolazi s driftom 0, bez clickova i bez
clippinga (`host_cpu_time=1.974 s`). `dependencies.lock` ostao je nepromijenjen.

Status ostaje **SOFTWARE FIXED; HW PENDING** jer host callback ne može dokazati
ponašanje stvarnog I2S drivera pri disableu, fizički FLAC USB kvar, višednevni
timeline wrap ni slušni scratch re-grab pod stvarnim P4 deadline opterećenjem.

### 6.4 OTA, sigurnost i tooling

| ID | Status | Nalaz i dokaz | Popravak | Obvezni gate |
| --- | --- | --- | --- | --- |
| CR-20260816-P2-13 | **SOFTWARE FIXED; HW PENDING** | S3 OTA prije alokacije odbija bundle veći od maksimalne signed image veličine, zatim prvo prima i verificira manifest/header. Wrap-safe guard nameće ukupni rok od 180 s te svakih 10 s traži barem 4096 B napretka; timeout/slow path prije i nakon `s3_ota_begin()` brzo zatvara zahtjev, a započeti OTA abortira. | Pure guard test pokriva 1 B/s slow client, dovoljan napredak, ukupni deadline i `UINT32_MAX` wrap; signing 6/6, S3 host suite i build prolaze. | Preostaje fizički HTTP slow/fragmented-client test koji nakon 408 potvrđuje da server odmah prima novi zahtjev i da boot particija nije promijenjena. |
| CR-20260816-P2-14 | **SOFTWARE FIXED; HW PENDING** | Pending S3 image sada prvo čeka stvarni start UART RX, heartbeat, translator, USB host-library i MIDI-client taskova, zatim svakih 500 ms šalje svježi `0x86` challenge. P4 ga iz UART RX taska vraća kao `0x87`; prerani, pogrešan ili odsutan ACK ne može potvrditi image, a 30 s timeout restarta još-pending slot. | Pure gate odbija `init OK/no traffic`, prerani i pogrešni ACK; production P4 UART test dokazuje exact echo bez deck-queue eventa. Odsutan FLX4 nije failure uvjet. | Preostaje fizički test oba boot redoslijeda, prekinutog P4→S3 voda, izgubljenog ACK-a i potvrđenog rollbacka. |
| CR-20260816-P2-15 | **OPEN** | Debug AP koristi javni statični PSK i nema operator autentikaciju (`s3_debug_ap.h:9-11`, `RISK_REGISTER.md`). | Per-device PSK ili kratkotrajni maintenance token na P4 UI-ju; fizička rollback potvrda; AP idle timeout, rate limiting i PMF/WPA3 gdje je podržano. | Neautenticirani klijent ne može mutirati stanje; servisni signed rollback ostaje moguć uz fizičku autorizaciju. |
| CR-20260816-P2-16 | **CLOSED** | Converter koristi `first_present()` pa nula ostaje valjana, sve numeričke forme prolaze kroz base-0/range validaciju, key-lock i nepoznati eksplicitni semantic ID odbijaju se, a pad input više ne izmišlja vendor LED adrese. Deck-only i različite Deck 1/2 LED adrese emitiraju se kao zasebni točni outputi. | Svaki rezultat obavezno prolazi stvarni `compile_profile()` prije povrata; compiler sada prihvaća eksplicitni output `deck: 0/1`. | Šest fixturea pokriva zero, hex range, deck2-only, različite adrese, key-lock te deck/MIDI range; test je u punom S3 runneru i svi committed profile fixturei ostaju byte-identični. |

#### Implementirano 2026-08-16 — S3 OTA upload i AP init availability

Commit `d951105` zatvara software dio CR-20260816-P2-13 i P3-01. OTA receive
više ne može beskonačno držati jedini HTTP server task sporim dotokom: prije
flash begin-a vrijede rani Content-Length limit, signed-manifest-first provjera,
180 s apsolutni rok i 10 s/4096 B progress prozor. AP netif se objavljuje tek
nakon uspješnog DHCP stop → IP config → DHCP start niza; svaki parcijalni kvar
uništava lokalni candidate, a eksplicitni OFF→ON edge može pokrenuti čist retry.

Puni S3 host suite, slow-client/tick-wrap guard, svi netif step-failure testovi,
OFF→ON retry, OTA signing 6/6 i S3 ESP-IDF v6.0.2 build prolaze. Image je
`0xec650`, 51% najmanjeg OTA slota ostaje slobodno, a dependency lock je
nepromijenjen. Hardware HTTP/DHCP fault smoke ostaje acceptance gate.

#### Implementirano 2026-08-16 — S3 bidirectional boot health

Commit `6200e6f` zatvara software dio CR-20260816-P2-14. S3 pending image više
ne može postati VALID samo na temelju lokalnih init povratnih vrijednosti.
Dvostupanjski gate najprije traži da su kritični control/USB taskovi zaista
ušli u run loop, a zatim exact P4 odgovor na svježi per-boot challenge. Pogrešan
ili prerani odgovor se odbacuje; challenge se ponavlja 500 ms do ukupno 30 s, a
timeout restarta image bez `mark-valid` poziva. FLX4 uređaj ne mora biti spojen.

Oba puna host suitea prolaze, uključujući 8/8 pure gate provjera i izvršni P4
UART challenge/ACK test. Oba ESP-IDF v6.0.2 builda prolaze: S3 image je
`0xecb40` uz 51% slobodnog najmanjeg OTA slota, P4 `0x253880` uz 42% slobodno;
dependency lockovi nisu promijenjeni. Fizički cross-board ACK-loss/rollback
test ostaje hardware gate.

#### Implementirano 2026-08-16 — web-profile converter correctness

Commit `8ce687f` zatvara CR-20260816-P2-16. Converter više ne koristi truthy
fallback za numeričke MIDI adrese, pa se `0` ne gubi; decimalni i `0x` rangeovi
se parsiraju jednako i provjeravaju prema MIDI/USB granicama. Nepredstavljivi
key-lock više se ne pretvara u tempo-range. Deck 2-only LED i različiti Deck
1/2 status/data1 parovi ostaju zasebne točne adrese, a implicitno izvođenje
pad LED statusa iz ulazne adrese je uklonjeno.

Converter prije povrata poziva stvarni compiler, a njegov test sa šest ciljnih
fixturea dio je S3 host runnera. Puni S3 suite prolazi; postojeći FLX4 i generic
profile binary fixturei ostali su byte-identični. Promjena je tooling/schema
popravak i nema zaseban fizički gate.

## 7. P3 tracker

| ID | Status | Nalaz | Popravak i gate |
| --- | --- | --- | --- |
| CR-20260816-P3-01 | **SOFTWARE FIXED; HW PENDING** | `s3_debug_ap_netif_stage` drži candidate lokalno kroz create, DHCP stop, IP set i DHCP start; globalni `s_ap_netif` dobiva se tek nakon punog uspjeha, a svaki kvar poziva `esp_netif_destroy_default_wifi()`. | Host failure injection prolazi za create i sva tri init koraka, potvrđuje da ništa nije objavljeno/leakano te da isti runtime nakon uklanjanja kvara uspješno starta. ERROR latch dodatno zahtijeva i testira eksplicitni OFF→ON retry. |
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
| 2026-08-16 | CR-20260816-P1-02, CR-20260816-P1-05, CR-20260816-P1-06 | `38ecc6c` | SOFTWARE FIXED; HW PENDING | P4 host suite PASS; S3 host suite PASS; scheduler 141/141; app-settings 46/46; OTA manifest/signing/packaging PASS; P4 ESP-IDF 6.0.2 clean build PASS (42% free); S3 ESP-IDF 6.0.2 clean dual-OTA build PASS (51% free); `git diff --check` PASS | Fizički GPIO11-low cold/warm boot; signed pull-OTA happy/mismatch provjera bez promjene boot particije; FLX4 burst/stalled-link/reconnect FIFO i held-release smoke |
| 2026-08-16 | CR-20260816-P1-01 | `c826f8f` | SOFTWARE FIXED; HW PENDING | P4 host suite PASS; audio lifecycle 383/383; UI load gate 16/16; UI exact screenshot E2E PASS; P4 ESP-IDF v6.0.2 build PASS, image `0x251120`, 42% free; `git diff --check` PASS | Fizičkih 50 USB remove/reconnect ciklusa tijekom LOAD/playbacka, EJECT/LOAD burst, OTA stop s oba decka i potvrda da nakon STOP-a nema živih taskova |
| 2026-08-16 | CR-20260816-P1-03, CR-20260816-P2-01, CR-20260816-P2-02, CR-20260816-P2-06 | `7f10aa1`, `aac07bc` | SOFTWARE FIXED; HW PENDING | S3 host suite PASS; P4 host suite PASS; descriptor/profile/runtime/parity/output-policy regresije PASS; reentrant snapshot-lock regresija PASS; S3 ESP-IDF v6.0.2 build PASS, image `0xf3c20`, 5% free; P4 ESP-IDF v6.0.2 build PASS, image `0x251220`, 42% free; `git diff --check` PASS; oba `dependencies.lock` nepromijenjena | Fizički FLX4 ↔ drugi MIDI device-swap, reconnect i late-transfer smoke; oba targeta nadograditi kao koordinirani protocol par |
| 2026-08-16 | CR-20260816-P1-04 | `762cbe3` | SOFTWARE FIXED; HW PENDING | P4 host suite PASS; audio engine 383/383; EQ/delay/flanger/output-mixer wide-headroom regresije PASS; 300 s dual-deck soak PASS (drift 0, clipped 0, clicks 0); P4 ESP-IDF v6.0.2 build PASS, image `0x252d80`, 42% free; `git diff --check` PASS; `dependencies.lock` nepromijenjen | Fizički slušni MAIN/PFL test i limiter/peak telemetrija s glasnim realnim materijalom; P4 CPU/I2S deadline mjerenje |
| 2026-08-16 | CR-20260816-P2-08, CR-20260816-P2-09, CR-20260816-P2-10, CR-20260816-P2-11 | `3674ca9` | SOFTWARE FIXED; HW PENDING | P4 host suite PASS; audio engine 389/389; timeline 289/289; output-sink i FLAC fault-injection PASS; 300 s dual-deck soak PASS (drift 0, clipped 0, clicks 0, host CPU 1.974 s); P4 ESP-IDF v6.0.2 build PASS, image `0x2538b0`, 42% free; `git diff --check` PASS; `dependencies.lock` nepromijenjen | Fizički I2S block/disable/STOP/reload, realni FLAC USB fault, seedani/dugi timeline wrap i brzi scratch re-grab slušni stress |
| 2026-08-16 | CR-20260816-P2-03, CR-20260816-P2-04, CR-20260816-P2-05 | `6e4459a` | SOFTWARE FIXED; HW PENDING | S3 host suite PASS; P4 host suite PASS; UART forced-interleaving, lost-disconnect, LED queue-full/latest-wins i USB retry tests PASS; S3 ESP-IDF v6.0.2 build PASS, image `0xec370`, 51% free; `git diff --check` PASS; oba `dependencies.lock` nepromijenjena | Fizički multicore UART burst/stall, izgubljeni connection frame, FLX4 USB submit fault i reconnect LED resync smoke |
| 2026-08-16 | CR-20260816-P2-12 | `c4bfdca` | SOFTWARE FIXED; HW PENDING | P4 host suite PASS; UI load gate 16/16; exact UI screenshot E2E PASS; P4 ESP-IDF v6.0.2 build PASS, image `0x253880`, 42% free; `git diff --check` PASS; `dependencies.lock` nepromijenjen | Fizički LOAD pod kontroliranim internal/PSRAM heap pritiskom i potvrda da UI uvijek vrati gumbe/status |
| 2026-08-16 | CR-20260816-P2-13, CR-20260816-P3-01 | `d951105` | SOFTWARE FIXED; HW PENDING | S3 host suite PASS; slow-client/deadline/tick-wrap i netif step-failure/OFF→ON retry testovi PASS; OTA signing 6/6 PASS; S3 ESP-IDF v6.0.2 build PASS, image `0xec650`, 51% free; `git diff --check` PASS; `dependencies.lock` nepromijenjen | Fizički fragmented/slow HTTP upload i novi request nakon 408; AP DHCP step-failure/retry smoke |
| 2026-08-16 | CR-20260816-P2-14 | `6200e6f` | SOFTWARE FIXED; HW PENDING | S3 host suite PASS; P4 host suite PASS; boot-gate 8/8 i production P4 UART challenge/ACK test PASS; S3 ESP-IDF v6.0.2 build PASS, image `0xecb40`, 51% free; P4 build PASS, image `0x253880`, 42% free; `git diff --check` PASS; oba `dependencies.lock` nepromijenjena | Fizički oba boot redoslijeda, izgubljeni/pogrešni ACK, prekinut reverse UART i rollback bez FLX4 uređaja |
| 2026-08-16 | CR-20260816-P2-16 | `8ce687f` | CLOSED | Converter fixturei 6/6 PASS; puni S3 host suite PASS; stvarni `compile_profile()` gate za svaki rezultat PASS; FLX4/generic committed binary freshness PASS; `py_compile` i `git diff --check` PASS | Nema preostalog source/hardware gatea za converter; stvarni non-FLX4 controller acceptance i dalje je zaseban feature/hardware gate |
