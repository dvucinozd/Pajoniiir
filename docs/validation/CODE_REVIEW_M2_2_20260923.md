# Pregled produkcijskog koda M2.2 i plan popravaka

Datum: 2026-09-23. Status: review završen; popravci su implementirani na grani
`codex/review-m2-2-fixes`, a hardware prihvat još nije proveden. Rezultati su u
[implementacijskom izvješću](IMPLEMENTATION_REPORT_M2_2_REVIEW_20260923.md).

## 1. Zaključak i granice zaključka

Pregled je utvrdio **devet nalaza**: jedan P1, šest P2 i dva P3. Pet
nalaza imaju dodatnu izvršenu reprodukciju nad produkcijskim C/JS kodom;
četiri su utvrđena praćenjem poziva, stanja i ugovora API-ja. Zasebno je opisan
potencijalni real-time zastoj koji ima visok prioritet za provjeru na P4,
ali u ovom pregledu nije reproduciran na uređaju.

Postojeći host testovi, P4 build, UI screenshot gate i virtualni audio soak
prolaze. To je dobra regresijska osnova, ali dodatne reprodukcije pokazuju da
ne pokrivaju nekoliko stvarnih rubnih slučajeva i jedan pogrešan binarni format.

Najvažniji popravak je identitet trajno spremljenih hot cue točaka: dva različita
Rekordbox izvoza mogu dijeliti numerički track ID, pa se spremljeni cue druge
pjesme može primijeniti na trenutno učitanu pjesmu. Slijede ispravan ANLZ cue
parser i redoslijed web kontrolnih naredbi.

Nije utvrđen reproduciran P0, zaobilaženje OTA potpisa ili dokaz da sadašnji
produkcijski uređaj ima audio prekide. Review nije potvrda odsutnosti drugih
grešaka. Nisu provedeni novi fizički USB/audio/OTA testovi ni slušno prihvaćanje.

## 2. Točan predmet pregleda

| Stavka | Vrijednost |
| --- | --- |
| Checkout | `D:\Documents\DDJ-FFL4`, `master` |
| HEAD | `ac237380af39bc7823f32a0b353a5e3d81fc27bf` |
| `git describe` | `M2.2-1-gac23738` |
| Produkcijski tag | `M2.2`, commit `2c2ec32c253d368765123d7bbf8d37389b790b55` |
| Razlika HEAD prema M2.2 | 11 dokumentacijskih datoteka; firmware isti |
| Početno radno stablo | čisto |
| ESP-IDF | provjeren `ESP-IDF v6.0.2` |
| Promjene tijekom pregleda | ovo izvješće i tri reprodukcijska izvora; bez firmware popravaka |

Aktualni release dokument je
[M2.2 production release](M2_2_PRODUCTION_RELEASE_20260923.md).
Stariji lokalni navod M2.1 nije korišten kao dokaz aktualne verzije.
Nije obavljen commit, push, flash, OTA instalacija ni promjena produkcijskog kanala.

## 3. Obuhvat i metoda

Obavljen je pregled arhitekture i inventara repozitorija, zatim detaljno praćenje
kritičnih tokova i njihovih testova. Obuhvat je širok, ali nije pošteno opisati ga
kao ručno dokazivanje ispravnosti svakog retka svake biblioteke.

| Područje | Pregledani tokovi i granice |
| --- | --- |
| Audio engine / DSP | producer-consumer lifecycle, PCM timeline, output ownership, seek/EOF, scratch/censor, keylock, limiter telemetrija, MAIN/cue sink |
| Deck / kontrole | state i event put, browse/load, trajni hot cues, MIDI mapping/profile dispatch, LED ownership |
| USB | host topology/recovery, MSC lifecycle, controller binding, UAC konfiguracija i accounting; postojeće regression coverage |
| Library / medij | PDB/ANLZ parseri, identitet zapisa, cache, row-order/generation, metadata consumers i storage gate |
| UI / web | LVGL task ownership, load/metadata put, status/control/library API, browser throttling i VU |
| Postavke / OTA | NVS trajnost, Wi-Fi probe status, pull manifest i admission, signed OTA granice |
| Build / CI / testovi | IDF pin, dependency lock i patch hooks, host runner, simulator, soak i release dokumentacija |

Uvozni vendor kod, ESP-IDF i kodeci nisu predmet potpunog neovisnog sigurnosnog
audita. Nisu provedeni coverage mjerenje, opći fuzzing kampanja, novi clean CI
build na drugom računalu, browser E2E na telefonu ni fizičko fault injection
testiranje. UI simulator provjerava LVGL, ne web JavaScript. Host stubs ne
dokazuju ponašanje ESP32-P4 schedulera ili trajnost stvarnog NVS-a.

## 4. Sažetak nalaza

P1: prioritetan popravak zbog pogrešnog operator rezultata. P2: konkretna greška
za sljedeći korektivni release. P3: niži utjecaj, popraviti uz pripadni modul.
Prioritet nije procjena učestalosti na sadašnjem uređaju.

| ID | Prioritet | Nalaz | Dokaz |
| --- | --- | --- | --- |
| F01 | P1 | Hot cue ključ kolidira između nezavisnih kolekcija | izvršena reprodukcija ključa + praćenje NVS/seek puta |
| F02 | P2 | ANLZ parser preskače valjane PCPT cue zapise | izvršen vanjski strukturiran fixture |
| F03 | P2 | Web klizač može naknadno poslati staru vrijednost | izvršen produkcijski JS s kontroliranim timerom |
| F04 | P2 | Web VU prikazuje povijesni maksimum | praćenje producer/API/consumer semantike |
| F05 | P2 | Library HTTP odgovor može miješati generacije | analiza međusobno odvojenih snapshot/row poziva |
| F06 | P2 | OTA postavke nisu jedna NVS transakcija; clear skriva grešku | aplikacijski kod + lokalna IDF 6.0.2 implementacija |
| F07 | P2 | Pull manifest prihvaća nevaljan JSON | izvršen produkcijski parser |
| F08 | P3 | Stari OTA status prekriva noviji connection probe | praćenje trajnog stanja i izbora statusa |
| F09 | P3 | ANLZ prihvaća neispravan završetak nakon svih traženih tagova | izvršen produkcijski parser |
| R01 | provjeriti kao P1 | Preemptirani seqlock writer može zaustaviti audio reader | statički raspored izvršavanja; bez P4 reprodukcije |

### Naknadno otkriven production incident — OTA reboot panic

Read-only pregled aktualnog uređaja nakon reviewa pronašao je crash dump taska
`ota_reboot`. Service journal pokazuje da je neposredno nakon verificiranog M2.2
push OTA prijelaza boot 489 krenuo s reset razlogom `PANIC`; sljedeći hladni
bootovi bili su uredni. Simbolizacija PC `0x4FF0CE88` i RA `0x4FF00B70` prema
točno tagiranom M2.2 sourceu mapira ih na `Cache_WriteBack_All()` i
`esp_restart_noos_inner()`. MTVAL `0x3FF1009C` je cache sync register.

To je zaseban potvrđen incident izvan početnih devet nalaza. Korektivni kandidat
pinira push/pull/validation restart taskove na core 0, povećava mali helper stack
i više ne pokušava izravni restart iz nepoznatog handler corea kada stvaranje
helper taska ne uspije. Sama simbolizacija ne dokazuje na kojem je coreu M2.2
helper tada radio; zato se popravak mora potvrditi ponovljenim signed OTA i
validation reboot ciklusima na uređaju prije zatvaranja incidenta.

## 5. Detaljni nalazi

### F01 — Različite pjesme mogu dijeliti spremljene hot cues

**Lokacije:** `components/library/library.c:181-187`,
`components/hot_cue_store/hot_cue_store.c:9-15,99-143`,
`components/deck_core/deck_core.c:673-779` pod `firmware/main-deck-p4`.

`library_track_key()` za svaki nenulti `track_id` vraća upravo taj broj, bez
identiteta kolekcije, medija ili datoteke. NVS koristi ključ `hc%08lx` u jednom
namespaceu. Generation provjera štiti aktualni katalog, ali nije dio tog
trajnog identiteta.

**Okidač:** na kolekciji A spremiti cue pjesme ID 42, zatim učitati drugu pjesmu
iz nezavisne kolekcije B s istim ID 42. To može nastati i nakon zamjene USB-a,
ponovnog izvoza ili promjene baze. Dvije putanje u reprodukciji bile su različite.

**Rezultat reprodukcije:** `keyA=42 keyB=42 equal=1`. Reprodukcija izvršava pravi
`library_track_key`; sama NVS razmjena i fizički skok nisu izvršeni. Kod storea
oba ključa mapira u istu stavku, a deck za učitani blob poziva seek na spremljeni
`pos_ms`. Posljedica može biti skok na tuđi cue ili prepisivanje/brisanje tuđih
točaka. Ovo nije isto što i namjerno dijeljenje cue točaka iste pjesme između deckova.

**Popravak:** uvesti stabilan persistent identity koji razlikuje kolekcije i
različite datoteke; uz kratki storage ključ provjeravati puni identitet u blobu.
Ne osloniti se samo na 32-bitni hash putanje. Odrediti pravila za rename,
re-export, kopiju istog medija i zamjenu datoteke na istoj putanji. Legacy cue
blob ne pridruživati novoj pjesmi ako se identitet ne može dokazati; bez masovnog
brisanja postojećih podataka.

**Prihvat:** dvije kolekcije s istim ID-jem ostaju izolirane kroz save/load/clear,
restart i zamjenu USB-a; ista pjesma na oba decka dijeli cue prema namjeri;
legacy migracija ne proizvodi pogrešno pridruživanje. Provjeriti i
`track_meta_cache`, koji također koristi numerički ključ; njegova dodatna
size/mtime validacija nije globalni identitet. Produkcijsko pisanje cachea je
isključeno, pa je taj sekundarni rizik uvjetovan postojećim cacheom.

### F02 — Pogrešna interpretacija PCPT zapisa

**Lokacije:** `components/library/rekordbox_anlz.c:387-427`,
`tests/anlz/test_anlz.c:137-159`, `docs/rekordbox-format-analysis.md:207-216`.

Parser tretira `buf[0]` kao tip, `buf[1]` kao indeks, a offsete 4 i 8 kao vremena.
PCPT je zaseban mali tag s vlastitim zaglavljem. Početak `PCPT` znači da se
slovo `C` čita kao indeks 67, pa zapis pada na `index >= ANLZ_MAX_CUES`.
Hot cue broj je na offsetu `0x0c`, tip na `0x1c`, vrijeme na `0x20`, a loop kraj
na `0x24`. To je uspoređeno s primarnim reverse-engineering izvorom
[Deep Symmetry ANLZ analysis](https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/anlz.html).

**Reprodukcija:** sintetički binarni fixture složen prema vanjskoj strukturi
sadrži hot cue A na 12.345 s; stvarni parser vraća `rc=0 cues=0`. Fixture nije
stvarni korisnikov Rekordbox export, pa taj hardware/import test ostaje otvoren.
Postojeći testovi i lokalni format dokument opisuju isti pojednostavljeni,
pogrešni raspored kao parser; zato se međusobno potvrđuju.

**Utjecaj:** izgubljeni uvezeni cue/loop podaci i markeri koje koriste
`ui_performance_tabs.c` i `ui_overview.c`. Ovaj nalaz ne dokazuje neispravnost
zasebnog lokalnog FLX4 hot-cue storea.

**Popravak:** parsirati i provjeriti PCPT header/entry length, broj i vrstu PCOB
zapisa te mapirati cue broj u interni slot. Proći sve relevantne PCOB sekcije,
jer memory i hot cues mogu biti odvojeni; samo popraviti četiri offseta nije
dovoljno. Dogovoriti podršku/preferenciju PCO2/PCP2 prema stvarnim exportima,
bez nekontroliranog proširenja scopea. Ispraviti lokalni opis formata.

**Prihvat:** nezavisni fixturei za cue/loop, praznu memory sekciju prije hot-cue
sekcije, više sekcija, nevaljane duljine i granične indekse; najmanje jedan
stvarni export provjeren u Rekordboxu i na P4 s očekivanim vremenima.

### F03 — Odgođeni timer poništi zadnju vrijednost klizača

**Lokacija:** `components/web_server/web/app.js:40-57`; volume, crossfader i
pitch koriste isti `throttledSend`.

Grana `elapsed >= minInterval` šalje novu vrijednost, ali ne uklanja postojeći
timer ni `pending`. Ako browser odgodi dospjeli timer, noviji input može proći
prije njega. Stari callback potom šalje prethodnu vrijednost.

**Izvršena reprodukcija:** t=1000 šalje 100; t=1040 odgađa 200; t=1100 novi input
šalje 300, a zatim stari timer šalje 200. Dobiven redoslijed je
`[100,300,200]`. To je test stvarne funkcije u Node VM-u s kontroliranim
rasporedom; nije mjerenje učestalosti u mobilnom browseru.

**Utjecaj:** konačna vrijednost na uređaju može odstupati od zadnjeg položaja
kontrole. Za crossfader/volume/pitch to je operativna greška, ne samo prikaz.

**Popravak:** pri neposrednom slanju otkazati/invalidateati staro pending slanje;
osigurati da po kontroli vrijedi zadnja vrijednost. Odvojeno provjeriti redoslijed
istodobnih HTTP zahtjeva; samo timer fix nije dokaz mrežnog redoslijeda.

**Prihvat:** fake-clock test navedenog rasporeda, brzi input burst, odgođeni timer,
usporeni odgovori i fizički konačni volume/crossfader/pitch nakon otpuštanja klizača.

### F04 — VU pokazuje peak od reseta statistike

**Lokacije:** `components/audio_engine/audio_engine.c:522-544`,
`components/web_server/web_server.c:1784`, `components/web_server/web/app.js:359-380`.

Producer samo povećava `peak_input_abs`; API ga izlaže kao `limiter_peak`, a web
ga koristi za trenutno osvjetljenje VU segmenata i dB tekst. Nakon glasnog bloka
tišina ne spušta prikaz jer povijesni maksimum ostaje do reseta statistike.
Osim vremenske semantike, riječ je o input peaku limitera, što treba razlikovati
od konačnog MAIN izlaza.

**Popravak:** zadržati kumulativnu dijagnostiku i uvesti zaseban periodični meter
snapshot jasno odabranog signalnog mjesta, s definiranom attack/release ili
windowed peak/RMS semantikom. HTTP čitanje ne smije resetirati globalne brojače.

**Prihvat:** signal → tišina vrati prikaz na dno unutar dokumentiranog vremena;
dva web klijenta međusobno ne mijenjaju meter; trajni limiter overload brojači
ostaju točni. Potvrditi indikator usporedbom s fizičkim MAIN izlazom.

### F05 — Miješanje redaka različitih generacija kataloga

**Lokacije:** `components/web_server/web_server.c:1818-1909`,
`components/library/library.c:879-927` i catalog row/generation accessors.

Handler zasebno čita count, jednom generation, zatim svaki redak zasebnim
pozivom i između njih šalje mrežne chunkove. Sortiranje/publikacija može
promijeniti redoslijed između redaka. Primjer: stari redoslijed A,B; pošalje se
A; sort promijeni B,A; zatim se kao red 1 ponovno pošalje A. JSON nosi jednu
generaciju, ali redovi nisu nužno njezin snapshot.

**Utjecaj:** duplikati, izostanci i zastarjeli izbor; `/api/load` generation
provjera ublažava posljedicu odbijanjem zastarjelog zahtjeva. Ovdje nije dokazan
load pogrešne pjesme kroz taj zaštićeni endpoint.

**Popravak:** bounded snapshot ili paginacija vezana za generation s ponovnim
čitanjem na promjenu. Ne držati library mutex tijekom mrežnog slanja. Kod chunked
odgovora nije dovoljno otkriti grešku tek nakon slanja HTTP headera i pokušati
vratiti drugi status; definirati pre-send snapshot ili prekid/retry protokol.

**Prihvat:** deterministički sort/reload između dva reda, disconnect usred
prijenosa i spori klijent; svaki dovršen odgovor ima konzistentan count,
generation i skup redaka, ili klijent jasno ponavlja čitanje.

### F06 — Djelomično spremljena OTA konfiguracija i lažni uspjeh clear operacije

**Lokacije:** `components/app_settings/app_settings.c:371-443`,
`components/web_server/web_server.c:607-611`.

SSID, password i URL upisuju se kroz tri `nvs_set_str` poziva. RAM se mijenja
tek kada svi uspiju, ali NVS commit ne daje transakciju preko tri ključa.
To je provjereno i u instaliranom IDF 6.0.2: `components/nvs_flash/src/`
`nvs_handle_simple.cpp`, `set_string()` poziva `writeItem()`, a `commit()` nema
rollback tih prethodnih upisa.

**Okidač:** nakon prvog uspješnog upisa drugi zakaže ili se uređaj ugasi.
Sljedeći boot može kombinirati novi SSID sa starom lozinkom/URL-om, iako je API
prethodno javio neuspjeh. `app_settings_ota_clear()` uz to vraća `void`; nakon
NVS greške handler ipak odgovara `ok:true,cleared:true`.

**Popravak:** jedna versioned konfiguracijska stavka s validacijom, odnosno
dual-slot/generation mehanizam ako ga zahtijeva točno odabrani NVS ugovor;
provjeriti failure/power-loss ponašanje tog rješenja. Migrirati postojeće ključeve
bez gubitka valjane konfiguracije. Clear mora vratiti status, a HTTP odgovor
mora odgovarati stvarnom rezultatu. Sačuvati pravilo NULL password = zadrži.

**Prihvat:** fault injection na svakom koraku i reboot daju cijelu staru ili
cijelu novu konfiguraciju; nikada mješavinu. Neuspjeli clear vraća grešku i
točan naknadni GET. Jedan fizički kontrolirani power-loss test potvrđuje host model.

### F07 — Manifest parser ne provjerava cjelovit JSON dokument

**Lokacija:** `components/p4_ota_pull_core/p4_ota_pull_manifest.c:33-48,72-95,138-198`.

Pretraga ključeva i čitanje broja nisu zamjena za JSON sintaksu. Izvršeni testovi
vraćaju uspjeh za `size:200garbage` uz duplicirani `release`, te za dokument bez
završne vitičaste zagrade vanjskog objekta. Oba se interpretiraju kao M2.3/200.

**Utjecaj:** pogrešno generiran ili oštećen channel manifest može biti prikazan
kao prihvatljiva ponuda umjesto jasne greške. Ovo nije dokaz zaobilaženja SHA-256,
signed-package provjere ili autorizacije instalacije.

**Popravak:** bounded JSON parser/tokenizer s provjerom cijelog dokumenta,
točnih tipova i granica brojeva, duplicate-key politike i lokacije obveznih
polja u odgovarajućim objektima. Sačuvati postojeći URL, version i size/SHA ugovor.

**Prihvat:** valid manifest prolazi; truncated root, trailing garbage, integer
suffix/overflow, duplicate obvezni ključevi i krivi nesting padaju; escapeovi
se obrađuju prema JSON pravilima. Potpisne i newer-only regresije ostaju zelene.

### F08 — Rezultat novog Wi-Fi probea skriva stari OTA rezultat

**Lokacija:** `main/app_main.c:205-248` i status lifecycle u `p4_ota_pull`.

Nakon prvog OTA checka stanje ostaje različito od IDLE. `app_probe_status()` tada
uvijek preferira OTA status, dok novi mode=0 pokreće samo `wifi_link_probe_start()`.
Komentar o zadnjoj pokrenutoj operaciji nije implementiran. Novi test veze može
završiti, a korisnik i dalje vidi rezultat prethodnog OTA checka.

**Popravak:** eksplicitno evidentirati aktivnu/zadnju operaciju i njezin ID ili
epoch; status povezati s tom operacijom, bez uništavanja još valjane OTA ponude.

**Prihvat:** check→probe, failed check→successful probe i probe→check prikazuju
zadnju zatraženu operaciju i točnu adresu/stanje; paralelni start ostaje odbijen
ili serijaliziran prema postojećem admission ugovoru.

### F09 — Nevalidirani završetak ANLZ datoteke

**Lokacija:** `components/library/rekordbox_anlz.c:126-196,500-554`.

Svaki `walk_sections_for_tag()` završava kada nađe traženi tag. Ako se svih pet
traženih tagova nalazi prije oštećenog završetka, nijedan prolaz ne dođe do njega.
Fixture s jednim dodatnim bajtom unutar deklarirane PMAI duljine vraća `rc=0`.

**Utjecaj:** nepotpuna strukturalna validacija i tihi prihvat oštećenog metadata
fajla. Nije demonstriran out-of-bounds pristup ili rušenje zbog tog jednog bajta.

**Popravak i prihvat:** jedan cjelovit bounded section walk do deklariranog
kraja, pa parsiranje potrebnih sadržaja ili validacija prije njihove publikacije.
Testirati nepotpuni header na kraju, nevaljanu duljinu, valjani nepoznati tag i
više istovrsnih tagova. Pri grešci sačuvati prethodno valjano metadata stanje.

## 6. R01 — Prioritetna dodatna provjera audio timeline seqlocka

**Lokacije:** `components/audio_engine/audio_pcm_timeline.c:3-41`,
`components/audio_engine/audio_engine.c:1459-1472,3086-3088,4225-4228`.

Reader beskonačno vrti petlju dok je cursor version neparan. Writer ga prvo
postavi na neparan, upiše cursor, pa vrati na paran. `ae_output` ima prioritet 6,
a `ae_decode` 5; oba su na coreu 0. Ako output preuzme CPU nakon prvog writer
upisa i čita baš taj cursor, writer nižeg prioriteta ne može završiti dok reader
vrti petlju. Time atomici sami ne garantiraju real-time napredovanje.

Konkretna statička putanja je 32-bitni wrap write/oldest cursora u decode pushu,
uz output čitanje tih 64-bitnih cursora pri obradi novog censor zahtjeva.
Pri 48 kHz 2^32 frameova je približno 24,9 sati bez resetiranja cursor baze;
uobičajeni load/seek resetovi to često spriječe. To nije tvrdnja da uređaj staje
nakon 24,9 sati. Druga mjesta apsolutnog playhead upisa također treba provjeriti
uz stvarnu task ownership/critical-section zaštitu. Neki reset/flush putovi već
jesu zaštićeni i ne smiju se pogrešno proglasiti nezaštićenima.

**Status dokaza:** dopušten nepovoljan raspored vidi se iz koda; nije izvedena
P4 preemption reprodukcija i nije utvrđena učestalost. Ne računati ga među devet
potvrđenih funkcionalnih nalaza niti prijaviti kao postojeći terenski WDT incident.

**Sljedeći korak:** kontrolirani hook prisili preemption nakon odd storea, s
cursorom neposredno prije wrapa i aktivnim output/censor putem. Ne čekati 25 sati
da bi se pogodio rub. Ako se raspored potvrdi, onemogućiti preemption writer
sekcije odgovarajućom kratkom zaštitom ili promijeniti ownership/publication
protokol. Ne uvoditi mutex čekanje u per-sample audio put niti neograničeni spin.

**Prihvat:** prisilni raspored završi, nema WDT-a ni povećanja deadline/data-loss
brojača; cursor ostaje monoton i coherent. Potom oba decka, scratch/censor/seek
i Master Tempo na stvarnom P4 uz operatorovo slušno prihvaćanje.

## 7. Izvršene provjere

| Provjera | Rezultat i ograničenje |
| --- | --- |
| `tests/run_p4_host_tests.ps1` | exit 0; 91 `run` i 250 `static` oznaka u logu; to nisu brojevi pojedinačnih assertiona |
| Real MP3/PDB opcionalni testovi | preskočeni jer runneru nisu predane stvarne datoteke; eksplicitno prijavljeno u logu |
| P4 `idf.py build` | exit 0, IDF 6.0.2; incremental build postojećeg 6.0.2 build direktorija |
| UI simulator E2E | exit 0; 7/7 točnih screenshot baseline hashova; bez promjene baselinea |
| Dual-deck keylock soak | exit 0; 300 s virtualnog audija, drift oba decka 0 frameova, clipping 0 |
| Dodatni parser probe | PCPT cue nedostaje; nevaljan ANLZ tail i nevaljani manifesti prihvaćeni |
| Dodatni JS probe | potvrđen redoslijed 100→300→200 |
| Dodatni identity probe | potvrđen isti ključ 42 za dvije različite putanje |
| Dokumentacija i radno stablo | `check_documentation.ps1` i `git diff --check` prošli; dodani samo izvješće i tri probe izvora |

Virtualni soak: D1 pitch error 1,868%, peak 9088, max jump 574; D2 0,301%,
peak 9665, max jump 618; mixed peak 18748, clipped 0, detektirani clicks 0.
To su metrika i prihvat postojećeg host gatea, ne tvrdnja o čujnoj transparentnosti.
Host CPU vrijeme 1,145 s nije P4 performance mjerenje. Simulator je emitirao
`ESP_FAIL` redefinition upozorenja zbog stuba/headera; gate je prošao, ali to
ostaje manje održavanje testnog okruženja.

Lokalni P4 binary: **2.493.424 B**, budget **3.670.016 B**, rezerva **1.176.592 B**.
SHA-256: `238d397535901bb8b7bdcc4d2cf27b8b35b71840dc8689f9623835f0ca1747e0`.
To je review build HEAD-a, nije potpisani/tagirani produkcijski artefakt; druga
verzijska oznaka može promijeniti binary i uz isti firmware source.

Lokalni detaljni logovi ostaju u ignoriranom `tmp/review-20260923/`:
`host-tests.log`, `idf-env.log`, `idf-build.log`, `ui-simulator.log`,
`keylock-soak.log`, `parser-repro.log`, `web-repro.log`, `identity-repro.log`.
Oni nisu sadržaj commita. Reprodukcijski izvori su priloženi uz ovo izvješće.

Nisu ponavljani prihvaćeni release waiveri ni mijenjana njihova ocjena: dokumentirani
I/J lifecycle izuzeci, single-board security provisioning, zajednički servisni
Wi-Fi pristup, odgođen backup-key recovery, isključen recorder i host-only
generički controller fixture ostaju u svojim aktualnim risk/release dokumentima.

## 8. Plan popravaka i release uvjeti

### Paket A — Identitet i uvezeni podaci

1. F01: specificirati persistent identity i sigurnu legacy migraciju; prvo dodati
   test s dvije kolekcije istog ID-ja, zatim promijeniti store i sve consumers.
2. F02 + F09: zamijeniti pogrešne fixture pretpostavke, popraviti puni ANLZ walk
   i cue parsing, uskladiti format dokument. Pripremiti stvarne export uzorke.
3. Odvojeno provjeriti metadata cache kompatibilnost i invalidaciju kako stari
   cache ne bi prikrio popravljeni parser. Izbjegavati slijepo brisanje medija/NVS-a.

**Izlaz:** trajni cue podaci se ne miješaju; uvezeni cue/loop i markeri odgovaraju
stvarnom exportu nakon hladnog i toplog učitavanja.

### Paket B — Audio napredovanje, prije odluke o novom releaseu

1. R01: deterministički preemption/wrap eksperiment na P4 i audit writer/reader
   ownershipa svih timeline cursor operacija.
2. Ako se potvrdi, napraviti uski concurrency popravak s ciljanom regresijom.
3. Izmjeriti deadline/data-loss/WDT ponašanje na novom binaryju. Ovaj paket ima
   visok prioritet, ali se firmware ne mijenja samo radi uklanjanja sumnje.

**Izlaz:** dokaz da nepovoljan raspored napreduje ili konkretan provjeren popravak.

### Paket C — Web upravljanje i dijagnostika

1. F03: latest-value kontrolni sender i testovi odgođenih timer/network odgovora.
2. F05: generation-coherent library API bez mrežnog čekanja pod library lockom.
3. F04: zaseban trenutni MAIN meter uz očuvanu kumulativnu dijagnostiku.
4. F08: operacijski ID/epoch za status probe/check/install.

**Izlaz:** browser prikazuje aktualne podatke i zadnja operatorova naredba je
konačna vrijednost uređaja. Provesti desktop i telefon browser test s dva klijenta.

### Paket D — OTA konfiguracija i parser

1. F06: transactional configuration representation, migracija, povrat clear
   statusa i fault-injection testovi.
2. F07: bounded cjelovit JSON parsing uz očuvane postojeće sigurnosne provjere.
3. Testirati restart, pogrešnu lozinku, nedostupan kanal i malformed manifest;
   valjana prethodna konfiguracija mora ostati upotrebljiva.

**Izlaz:** API istinito prijavljuje rezultat, storage nije djelomično konfiguriran,
nevaljani kanal ne postaje prihvaćena ponuda.

### Paket E — Prihvat kandidata za produkciju

Za svaki paket napraviti zaseban pregledljiv commit/PR i odgovarajuće ciljane
testove; ne spajati sve u jedan veliki refactor. Nakon konačnog integriranog
sourcea ponoviti host suite, P4 build, UI gate i keylock soak te postojeći CI
release ugovor bez nepotrebnog ponavljanja nepromijenjenih jobova.

Konačni release kandidat mora imati zabilježen commit, verziju i SHA-256.
Na **tom binaryju** provesti relevantan dual-deck MAIN/cue i FLX4 smoke,
load/seek/hot-cue/loop/scratch/censor/MT, zamjenu medija, web kontrole i OTA
konfiguraciju/check. Trajanje i matrica ponovnog soaka/lifecycle testa moraju
slijediti opseg stvarnih promjena i postojeći release gate. Prethodni M2.1/M2.2
soak nije dokaz za novi binary.

Slušno prihvaćanje treba dati operator. Novi tag i javni OTA kanal objavljuju se
tek nakon tih dokaza i uobičajenog release odobrenja. Ovaj review sam ne
predstavlja odobrenje objave niti automatski poništava postojeći M2.2 release.

## 9. Ponovljive dodatne reprodukcije

Izvori:

- [C parser probe](review_m2_2_20260923/repro_parsers.c)
- [C identity probe](review_m2_2_20260923/repro_identity.c)
- [JS throttle probe](review_m2_2_20260923/repro_web.mjs)

Pokrenuti iz korijena repozitorija u PowerShellu, s GCC i Nodeom dostupnima.
Ovi istraživački probeovi ispisuju ponašanje; exit 0 znači da su se izvršili,
**ne** da je ispravnost potvrđena. Ne zamjenjuju buduće assertion regresije.

```powershell
$env:Path = "$env:Path;C:\msys64\ucrt64\bin"
New-Item -ItemType Directory -Force tmp/review-20260923 | Out-Null
gcc -std=c11 -DANLZ_STANDALONE_TEST `
  -Ifirmware/main-deck-p4/components/library/include `
  -Ifirmware/main-deck-p4/components/p4_ota_pull_core/include `
  docs/validation/review_m2_2_20260923/repro_parsers.c `
  firmware/main-deck-p4/components/library/rekordbox_anlz.c `
  firmware/main-deck-p4/components/p4_ota_pull_core/p4_ota_pull_manifest.c `
  -o tmp/review-20260923/repro_parsers.exe
if ($LASTEXITCODE -ne 0) { throw 'Parser probe compilation failed' }
Push-Location tmp/review-20260923
try { ./repro_parsers.exe } finally { Pop-Location }
gcc -std=c11 -DLIBRARY_LOAD_TRACE_HOST_TEST -DWIN32 `
  -Itests/support/stubs `
  -Ifirmware/main-deck-p4/components/library/include `
  -Ifirmware/main-deck-p4/components/media_io_gate/include `
  docs/validation/review_m2_2_20260923/repro_identity.c `
  firmware/main-deck-p4/components/library/library.c `
  firmware/main-deck-p4/components/library/library_load_trace.c `
  -o tmp/review-20260923/repro_identity.exe
if ($LASTEXITCODE -ne 0) { throw 'Identity probe compilation failed' }
./tmp/review-20260923/repro_identity.exe
node docs/validation/review_m2_2_20260923/repro_web.mjs
```

Zabilježeni output na pregledanom sourceu:

```text
PCPT_SPEC rc=0 cues=0 (expected 1 at 12345 ms)
ANLZ_TRAILING_PARTIAL rc=0 (expected rejection)
OTA_JSON case=0 rc=0 release=M2.3 size=200
OTA_JSON case=1 rc=0 release=M2.3 size=200
OTA_JSON case=2 rc=0 release=M2.3 size=200
INDEPENDENT_TRACKS keyA=42 keyB=42 equal=1 (expected distinct persistent identities)
THROTTLE_SEND_ORDER ["/fader=100","/fader=300","/fader=200"] (expected final value 300)
```
