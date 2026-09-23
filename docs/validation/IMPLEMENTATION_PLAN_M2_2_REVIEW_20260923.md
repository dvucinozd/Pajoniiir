# Implementacijski plan popravaka nakon M2.2 reviewa

Datum: 2026-09-23. Status: software implementacija završena na grani
`codex/review-m2-2-fixes`; hardware prihvat i release koraci su otvoreni.

Stvarno izvedene promjene, odstupanja od plana, testovi i otvoreni gateovi vode
se u [implementacijskom izvješću](IMPLEMENTATION_REPORT_M2_2_REVIEW_20260923.md).
Ovaj dokument ostaje detaljna projektna specifikacija i kriterij prihvata.

Izvor nalaza: [CODE_REVIEW_M2_2_20260923.md](CODE_REVIEW_M2_2_20260923.md).
Polazni source: `ac237380af39bc7823f32a0b353a5e3d81fc27bf`, firmware jednak M2.2.
Svaki paket mora prije rada provjeriti aktualni HEAD i radno stablo; ovaj plan
ne daje ovlast za prepisivanje naknadnih korisničkih promjena.

## 1. Cilj, potvrđene odluke i redoslijed

Cilj je zatvoriti F01–F09 i razriješiti R01, uz očuvan dual-deck playback,
MAIN/cue audio, FLX4 operator put, signed OTA i postojeće prihvaćene release
granice. Prihvat je vezan uz konkretan commit i binary SHA-256.

Korisnik je potvrdio dvije odluke:

- Legacy hot-cue zapisi ostaju sačuvani, ali ih novi firmware ne učitava
  automatski. Ručno pridruživanje starih zapisa nije dio ovog popravka.
- Novi cue identitet vrijedi za isti nepromijenjeni Rekordbox export i putanju.
  Prenosivost kroz rename/re-export nije zahtjev. Ne uvodi se puno hashiranje
  audio datoteke pri svakom učitavanju.

Ostali odabrani defaults:

- Zadržati postojeći numerički `track_key` za aktualni katalog/UI/HTTP i dodati
  zaseban trajni identitet. Ne pretvarati sve MIDI/UI događaje u SHA identitete.
- Zadržati postojeći oblik `/api/library`; popraviti konzistentnost snapshotom.
- F02 obuhvaća postojeći PCOB/PCPT ugovor. PCO2/PCP2, boje, komentari i memory-cue
  UI nisu novi featurei ovog korektivnog ciklusa; nepodržani valjani tagovi se
  strukturno validiraju i preskaču.
- Nema promjene particija, IDF verzije, USB topologije ni production waivera.
- Nova grana za implementaciju: `codex/review-m2-2-fixes`; ne mijenjati release
  tagove. Commit/push/PR i objava idu prema zasebnoj autorizaciji u toj fazi.

| Paket | Obuhvat | Preduvjet | Isporuka |
| --- | --- | --- | --- |
| P0 | baseline, test fixturei i mjerenja | čisto utvrđen source | ponovljive regresije i baseline latencije |
| P1 | R01 timeline napredovanje | P0 | provjera scheduler rasporeda, uska zaštita ako put nije već zaštićen |
| P2 | F01 trajni identitet/hot cues/cache | P0 | novi izolirani store bez nesigurne legacy migracije |
| P3 | F02 + F09 ANLZ | P2 za cache integraciju | stvarni PCPT raspored i puni section walk |
| P4 | F03 web naredbe | P0 | posljednja vrijednost ostaje konačna |
| P5 | F05 library snapshot | P0 | konzistentan JSON i pouzdan refresh |
| P6 | F04 MAIN meter | P1 | trenutna audio razina uz očuvanu dijagnostiku |
| P7 | F06 NVS konfiguracija | P0 | jedna cjelovita konfiguracija, istinit clear rezultat |
| P8 | F07 JSON + F08 status operacije | P7 za integracijske testove | strogi manifest i status zadnje operacije |
| P9 | integracija i fizički prihvat | P1–P8 | kvalificiran kandidat; objava nije automatska |

Rad voditi navedenim redom u zasebnim pregledljivim commitima. Svaki commit treba
imati pripadnu regresiju i prolaz relevantnih testova; failing baseline testove
prvo demonstrirati lokalno pa ih predati zajedno s popravkom, bez namjerno crvenog
glavnog CI-ja. Nije planirana delegacija na druge agente.

## 2. P0 — Reprodukcije i početno mjerenje

1. Sačuvati postojeće review izvore kao dokaz; njihove scenarije prenijeti u
   normalne assertion testove produkcijskih modula. Probe exit 0 nije PASS.
2. Zabilježiti broj host suiteova/assertiona, postojeći UI baseline, IDF 6.0.2
   build, dependency lock i binary budget. Ne ponavljati audit cijelog repoa.
3. Dopuniti test harness samo potrebnim kontrolama: virtualni JS clock/fetch,
   NVS write-through fault injection, pause hook u cursor publication i
   kontrolirani sort/clear između catalog poziva.
4. Pripremiti dvije sintetičke kolekcije s istim `track_id`, različitim PDB-om i
   putanjama; stvarni mali export za cue A/C i loop, legalno dostupan audio za
   real-file decode test. Stvarni korisnički sadržaj ne commitati bez potrebe.
5. Prije identity promjene izmjeriti mount/import i cold/warm LOAD za 191 i
   maksimalno 1024 zapisa, uz sviranje drugog decka. Prikupiti p50/p95/max iz
   najmanje 30 učitavanja po scenariju i postojeće deadline/data-loss brojače.

Ako fizički fixture/uređaj nije dostupan, software implementacija može završiti,
ali P9 ostaje NOT RUN, ne PASS. To ne opravdava spuštanje testnih pragova.

## 3. P1 — R01: zaštita napredovanja PCM timelinea

### Ugovor i promjena

Normalni per-frame low-32-bit SPSC put ostaje bez mutexa i 64-bitnih atomic
helpera. Rijetki epoch/low/version publication mora biti neprekinjiv od taska
koji ga čita na istom coreu.

1. Popisati sve write/oldest/play writers, task/core i postojeću zaštitu.
   Posebno obuhvatiti wrap u push/pop, keylock epoch prijelaz, reset/drop-newest,
   cue preroll te scratch povrat iz playing/paused stanja.
2. Dodati test hook neposredno nakon odd-version upisa u host/test konfiguraciji.
   Modelirati writer prioriteta 5 i reader prioriteta 6 na istom coreu. Watchdog
   testa mora prijaviti izostanak napredovanja bez vješanja cijelog host runnera.
3. Za trenutno nezaštićeni `cursor_store_absolute` u firmware varijanti koristiti
   kratki FreeRTOS `portMUX_TYPE` critical section oko odd → epoch/low → even.
   Jedan modul-lokalni mux dovoljan je za rijetke publication operacije. U njemu
   nema I/O-a, logiranja, alokacije, čekanja niti poziva druge zaključane usluge.
   Host varijanta mora modelirati zabranu preemptiona, a ne praznim stubom
   automatski proglasiti regresiju riješenom.
4. Uskladiti lock-order: postojeći ring-flush critical section smije obuhvatiti
   cursor publication; obrnuti redoslijed nije dopušten. Mux i cursor state
   moraju biti u memoriji prikladnoj za ovu kratku kritičnu sekciju.
5. Zadržati single-writer ownership: decode posjeduje write/oldest, output play;
   vanjski playhead handoff dopušten je samo dok output po postojećem
   scratch/preroll stanju ne pomiče play cursor. Te uvjete dokazati testovima.
   Ako test pokaže konkurentan vanjski i output play writer, taj handoff prenijeti
   postojećim output-command obrascem na granicu bloka prije promjene playhead-a;
   ne proširivati zaštitu na cijeli DSP blok ili per-frame mutex.
6. Ako aktualni source prije implementacije već isključuje opisani raspored,
   ne dodavati suvišnu zaštitu: zabilježiti točan guard i predati test koji ga
   dokazuje. Ne zatvarati R01 samo zato što običan soak nije proizveo WDT.

### Testovi i izlazni kriteriji

- Cursor postavljen nekoliko frameova prije `UINT32_MAX`, oba decka, različiti
  odnosi write/play/oldest, census kroz wrap bez gubitka monotonosti.
- Preemption zahtjev nakon odd storea izvrši reader tek nakon even publication;
  drugi core dobije koherentan snapshot, bez kombinacije novog epoch/starog low.
- Reset/drop-newest, cue preroll, scratch begin/end, paused scratch, censor i
  keylock ostaju funkcionalno jednaki, s očuvanim ring bounds.
- Na P4 izmjeriti maksimalno trajanje nove publication sekcije: cilj <10 us;
  prekoračenje zahtijeva analizu i doradu prije releasea, ne povećanje praga.
- Instrumentirani test build je zaseban dokaz za forced schedule. Završni soak
  mora koristiti release candidate bez test hookova.

## 4. P2 — F01: trajni identitet, novi hot-cue store i cache

### Identitet

Dodati `media_persistent_id_t` s 32 bajta SHA-256 i eksplicitnim `valid` stanjem
u loaded-track podatke. `track_key` ostaje nepromijenjen session/catalog token.

Odabrana definicija:

```text
export_id = SHA256(cijeli sadržaj export.pdb)
persistent_id = SHA256(
    domain "pajoniiir.hotcue.v2" +
    export_id +
    length-prefixed točna USB-relative audio putanja +
    uint64 veličina audio datoteke + int64 mtime
)
```

Kodirati duljine kao uint16 little-endian, a ostala integer polja little-endian,
bez struct paddinga. Domain se uključuje bez NUL terminatora; digest ima fiksnih
32 bajta. Putanja koristi postojeći UTF-8 prikaz iz kataloga, bez
`/usb` mount prefiksa; nema case-foldinga ni nove Unicode normalizacije.
Ista byte-for-byte kopija exporta na drugom USB-u ima isti identitet po namjeri.
Promjena PDB sadržaja invalidira trajne cue identitete cijelog exporta; to je
konzervativna posljedica korisnikova odabira, jasno navedena u release notes.

Hash PDB-a računati jednom pri izgradnji kataloga, streamingom kroz 8 KiB
buffer i postojeći media gate. Gate otpuštati između čitanja; hash računati izvan
gatea. Provjeriti mount/generation i size/mtime prije/poslije; pri disconnectu ili
promjeni fajla odbaciti gradnju. Objaviti digest zajedno s katalogom pod istim
lockom, nikad uz tuđi generation. Koristiti IDF SHA-256 implementaciju iza malog
adaptera; host test stvarnog hasha mora imati standardne known-answer vektore.

Audio `stat` i finalni digest izračunati u load workeru prije ANLZ/cache lookup-a
i prije objave loaded-track stanja, uz ponovnu generation provjeru. Identitet
proslijediti metadata resolveru eksplicitnim load contextom, bez mutable globalnog
"trenutnog" identiteta koji bi dva decka mogla zamijeniti. Nema dodatnog audio čitanja u callbacku,
LVGL tasku, pad akciji ili audio output tasku. Ako identitet nije dostupan,
playback smije nastaviti, ali persistent cue load/save/clear mora vratiti jasan
unavailable status; nikad fallback na stari numerički ključ.

Granica: ovo je identitet nepromijenjenog exporta, ne kriptografski dokaz audio
sadržaja. Ručna zamjena audio fajla uz isti path/size/mtime i nepromijenjen PDB
nije podržani workflow; puni content hash korisnik nije odabrao.

### Pohrana i consumers

- Novi NVS namespace `hotcue_v2`; legacy `hotcue` se ne dira.
- Storage key: `h` + prvih 14 hex znakova digesta, unutar NVS granice 15 znakova.
  Blob uvijek sadrži cijeli 32-byte digest, version, payload length, valid mask,
  osam slotova i CRC-32/ISO-HDLC nad eksplicitno serijaliziranim zapisom bez CRC
  polja. Integeri su little-endian; test pinna točan golden byte zapis.
- Pri kratkom key sudaru s različitim punim digestom vratiti collision error;
  ne učitati, prepisati ili obrisati tuđi blob. Ne uvoditi automatsku evikciju.
- `hot_cue_store_load/save/clear` primaju persistent ID. Provjeriti verziju,
  duljinu, digest, CRC, masku, tip i loop granice prije uporabe.
- Deck mask cache, invalidation i dijeljenje cueova između deckova uspoređuju
  puni persistent ID. Nova pjesma istog numeričkog ID-ja ne smije koristiti
  masku ili blob prethodne. Spremanje cuea ostaje izvan audio callbacka.
- Legacy zapisi nisu automatski migrirani niti se ponovno upisuju. U release
  notes objasniti potrebu ponovnog postavljanja lokalnih hot cueova i da stari
  podaci nisu obrisani. Ne dodavati novi migracijski UI u ovom ciklusu.

### Metadata cache

Podići cache format na v3 i namespace direktorija na `/sd/trackcache/v3/`.
Ključ direktorija je puni hex digest; header provjerava puni identitet,
parser revision i postojeće DAT/EXT size/mtime signature. Legacy v2 je cache miss;
ne čita se i ne briše. Produkcijsko automatsko pisanje cachea ostaje isključeno.
Testni/servisni write put mora koristiti isti novi identitet.

F02 parser promjena dobiva novu parser revision vrijednost; cache nastao starim
parserom nikad ne smije prikriti popravljeni uvoz cueova.

### Prihvat

- Isti track ID, različiti exporti: save/load/clear potpuno odvojeni.
- Isti export i putanja: cueovi prežive reboot, remount i kopiju istog exporta.
- Rename ili promjena PDB-a: nema automatskog nasljeđivanja.
- Ista pjesma na oba decka: maska i cue izmjene ostaju konzistentne.
- Prisilni kratki hash sudar: nema gubitka ili pogrešnog učitavanja podataka.
- Legacy-only uređaj: normalan boot/playback, novi store prazan, stari bajtovi
  netaknuti. NVS-full/save failure: nema lažnog prikaza uspješnog spremanja.
- Catalog build/reload uz drugi aktivni deck: nema novih data-loss događaja;
  p95 LOAD dodatak bez novog importiranja <=10% ili 20 ms, što je veće, prema P0.
  Hash import vrijeme prijaviti zasebno; ne skrivati ga u LOAD metrici.

## 5. P3 — F02/F09: puni ANLZ walk i ispravan PCPT

Zamijeniti zasebno traženje svakog taga jednim potpunim section walkom. Koristiti
lokalni parser context s file/section granicama i short-read stanjem, umjesto
oslanjanja na implicitno stanje između prolaza.

1. Validirati PMAI header, declared length prema stvarnoj datoteci i svaki
   `header_size <= section_size <= remaining`. Zbrojeve računati bez overflowa.
   Završetak mora biti točno na deklariranoj granici; nepotpuni tail je greška.
2. Valjani nepoznati tag preskočiti po duljini. Ne koristiti byte-scan recovery
   koji bi našao slučajni marker unutar oštećenih podataka. Uskladiti zastarjeli
   header komentar koji spominje takav fallback.
3. PCOB header: pročitati vrstu i declared count. Memory sekcije strukturno
   provjeriti, ali ih ne mapirati u osam hot-cue slotova. Proći sve PCOB sekcije.
4. PCPT: provjeriti magic, duljine i dostupnost polja; hot cue broj na `0x0c`,
   tip na `0x1c`, početak na `0x20`, loop kraj na `0x24`. A=1 mapirati u slot 0.
   Slotovi izvan 1–8 se preskaču uz dijagnostiku nakon strukturne validacije.
5. Loop mora imati end > start; duplicirani podržani slotovi unutar hot-cue
   sekcija su dvosmisleni i odbacuju taj DAT. Izlazne cueove sortirati po slotu.
   Prvi valjani singleton PPTH/PQTZ/PWAV ostaje odabrani sadržaj, ali sve kasnije
   sekcije i dalje prolaze boundary/short-read provjeru; ne prekidati walk rano.
6. Graditi privremeni metadata objekt i objaviti ga tek nakon potpunog uspjeha.
   Zadržati postojeću razliku: nevaljan novi DAT daje prazan novi rezultat i
   ne prenosi metapodatke prethodne pjesme; nevaljan optional EXT ostavlja
   već valjan DAT netaknut. Ne tumačiti reviewovu napomenu o čuvanju prethodnog
   metadata kao dozvolu da prethodna pjesma ostane prikazana na novom loadu.
7. Ispraviti lokalni format dokument i fixture builder. Nezavisni golden PCPT
   byte niz držati odvojen od parserovih offset konstanti.

**Test matrica:** A/C slotovi, single cue/loop, prazna memory sekcija prije
hot-cue sekcije, više PCOB sekcija, slot 8 i >8, duplicate slot, nevaljan loop,
count mismatch, kratki PCPT, nevaljani headeri, trailing byte, truncated tail,
valjani nepoznati tag i arbitrary tag order. Testirati DAT i EXT transakcijsku
semantiku, allocation failure i leak balance. Stvarni export mora dati vremena
jednaka Rekordbox prikazu; UI marker i Hot Cues ekran provjeriti na P4.

## 6. P4/P5/P6 — Web naredbe, biblioteka i trenutni meter

### F03 — Sender s jednom aktivnom naredbom po kontroli

Po ključu kontrole držati `pendingValue`, `timer`, `inFlight`, zadnji trenutak
slanja i generation timera. Vrijeme mjeriti monotonim `performance.now()`.

- Svaki input zamjenjuje pending posljednjom vrijednošću. Najviše jedan HTTP
  zahtjev po kontroli smije biti in-flight; različite kontrole ostaju nezavisne.
- Slati najranije nakon postojećih 90 ms od prethodnog slanja i tek kada prethodni
  zahtjev završi. Neposredno slanje otkazuje/invalidatea prethodni timer.
- Nakon završetka zahtjeva poslati najnoviji pending prema intervalu; callback
  starog timera ne smije slati ništa. `pointerup/change` osigurava trailing send,
  bez dupliciranja identične već potvrđene vrijednosti.
- Mrežnu grešku prikazati kao neuspjeh sinkronizacije i osvježiti autoritativni
  status. Ne ponavljati stare naredbe automatski nakon reconnecta. Uncertain
  timeout ne smije otvoriti put staroj pending vrijednosti; isprazniti red i
  zahtijevati novi input nakon uspješnog status refresha.

Dodati Node test stvarne funkcije: 100→300→200 reprodukcija mora završiti na 300;
burst, zakašnjeli timer, spori odgovori, greška, final release i dvije kontrole.
Test mora provjeriti pozive i maksimalni in-flight broj, ne samo završni DOM.

### F05 — Snapshot prije prvog HTTP chunka

Dodati owned snapshot sučelje na catalog/library granici: generation, count i
kompaktni redovi potrebni postojećem JSON-u. Library kopira sve redove iz
jednog order/store stanja pod svojim recursive mutexom; ne raditi 1024 kopije
velikog `library_track_t` objekta.

- Najviše 1024 reda, jedan buffer <=256 KiB u PSRAM-u; alocirati prije zaključavanja
  library mutexa. Ako nema memorije, vratiti 503 prije slanja headera. Ne trošiti
  veliki internal-heap fallback i ne slati djelomično valjanu listu.
- Count, generation i tekstovi kopiraju se u istoj zaključanoj operaciji. Nema
  filesystem/network I/O-a pod tim lockom. Poslije oslobađanja locka snapshot
  se može slati sporom klijentu bez blokiranja UI sortiranja.
- Svaki exit put oslobađa snapshot. Postojeći JSON i stale-load 409 ostaju.
- `fetchLibrary()` mora vraćati Promise, provjeriti `response.ok` i koristiti
  request sequence kako kasni stari odgovor ne bi zamijenio noviji prikaz.
  Odbijeni stale LOAD osvježava biblioteku, ali ne ponavlja LOAD automatski.

Testirati sort/clear/remount prije kopiranja, tijekom kontroliranog snapshot
puta i tijekom HTTP slanja; svaki dovršen odgovor ima jedan konzistentan
generation. Testirati OOM, send failure/disconnect i out-of-order fetch odgovor.
Na punom katalogu izmjeriti lock hold: cilj <5 ms; ako ga kopiranje prelazi,
optimizirati kopiju kompaktnog retka prije prihvata, ne držati lock tijekom JSON-a.

### F04 — Zaseban post-limiter MAIN meter

U audio outputu iz konačnog `master_out` bloka izmjeriti stereo maksimum
apsolutne vrijednosti. Ispravno obraditi `INT16_MIN`. Objavljivati samo blokove
prihvaćene na MAIN output putu; to je digitalna razina, ne dokaz analognog izlaza.

- Producer održava attack-immediate i release 24 dB/s envelope. Za sample rate
  i block size faktor releasea precomputeati; nema `pow/log` u per-sample petlji.
- Publikacija meter vrijednosti i ticka ide kratkim koherentnim snapshotom bez
  alokacija, I/O-a ili resetiranja limiter brojača.
- Status API dobiva aditivna polja `main_meter_peak`, `main_meter_age_ms` i
  `main_meter_valid`; `limiter_peak` ostaje kumulativna dijagnostika.
- Web koristi samo novi meter; floor -60 dBFS i niže prikazuje kao tišinu.
  Snapshot stariji od 500 ms prikazuje kao nedostupan/ugašen, bez lažnog živog VU-a.
  Nedostajuća nova polja u starijem firmwareu ne smiju se zamijeniti lifetime peakom.

Testirati oba kanala, full scale/INT16_MIN, limiter output, silence decay,
stop/sink fault, dva HTTP klijenta i očuvanje dijagnostičkih brojača. Od 0 dBFS
do floor prikaz mora pasti najkasnije za 2,6 s kontinuirane tišine; stopped output
postaje stale najkasnije za 500 ms plus jedan postojeći polling interval.

## 7. P7/P8 — OTA konfiguracija, manifest i status operacije

### F06 — Jedna versioned NVS konfiguracija

U postojećem namespaceu `cdjcfg` uvesti blob ključ `ota_cfg_v2`. Eksplicitno
serijalizirati magic, version, length, SSID/password/URL length-prefixed sadržaj
i CRC-32/ISO-HDLC bez CRC polja, s little-endian integerima i uint16 duljinama.
Ne pohranjivati raw C struct niti lozinku uključivati u opće settings
logove. Maksimalne duljine ostaju postojeće 32/64/160 znakova.

- Set/clear serijalizirati posebnim settings write mutexom. Pod njim napraviti
  merge s trenutnom konfiguracijom, validaciju, jedan `nvs_set_blob`, commit i
  read-back provjeru. RAM objaviti koherentno tek kada je durable rezultat poznat.
- `NULL password` zadržava postojeću; prazan password je prazna lozinka.
  Odbiti predugačke vrijednosti umjesto tihog truncationa.
- Boot: ako v2 postoji i valjan je, koristi se samo on. Ako je prisutan ali
  nevaljan, ne vraćati se na legacy kredencijale; OTA konfiguracija unavailable.
  Ako v2 ne postoji, učitati legacy trojku i pokušati jednokratno zapisati v2.
  Pri neuspjeloj migraciji zadržati pročitanu legacy konfiguraciju za taj boot,
  označiti migraciju nedovršenom i ponovno pokušati na sljedećem bootu.
- Nakon verificiranog v2 upisa ukloniti legacy OTA ključeve. Nedovršen cleanup
  ponoviti na idućem bootu; postojeći v2 uvijek ima prednost. Ne dirati druge
  postavke ni hot-cue namespaceove.
- Clear zapisuje valjani prazan v2 blob kao trajnu tombstone vrijednost i potom
  uklanja legacy ključeve. Ne brisati v2 ključ, jer bi to omogućilo resurrection
  starih kredencijala. `app_settings_ota_clear()` vraća `esp_err_t`.
- HTTP vraća 200/cleared samo kada zapis i cleanup uspiju. Ako cleanup zakaže
  nakon što je prazna konfiguracija već durable, vratiti grešku s jasnim stanjem
  da je aktivna konfiguracija prazna, a cleanup nedovršen; GET mora točno odraziti
  sadašnje stanje. Nije moguće jamčiti da svaki prijavljeni I/O error znači da
  se baš nijedan durable bajt nije promijenio; zato se rezultat read-backa ne skriva.
- Dodati koherentan interni getter cijele OTA konfiguracije za Wi-Fi/OTA workere;
  odvojeni SSID/password pozivi ne smiju sastaviti dvije verzije tijekom izmjene.
  Mrežni API nikad ne vraća password. Na rollbacku na M2.2 može trebati ponovno
  upisati Wi-Fi postavke; forward/rollback ne smije prepisivati v2 store.

Host NVS fake mora spremati uspješan set odmah, kao stvarni IDF put, i moći
zakazati svaki sljedeći korak. Testirati invalid CRC/version/length, prazan blob,
legacy migraciju, reboot između upisa/cleanup koraka, NVS-full, read-back failure,
istodobni set/clear i čuvanje passworda. Power-loss rezultat mora biti cijeli
stari ili novi zapis, ili jasno unavailable pri stvarnoj storage korupciji;
nikad mješavina polja niti lažni uspjeh.

### F07 — Cjelovit bounded JSON parser

Zadržati allocation-free core i postojeće result enum vrijednosti. Ukloniti
depth-blind `seek_key` i implementirati bounded recursive-descent tokenizer/parser
s najviše 8 razina i 4096 input bajtova, bez dinamičkih alokacija.

- Validirati cijelu JSON gramatiku do kraja; poslije root objekta dopušten je
  samo whitespace. Nema komentara, trailing comma, partial root ni numeric suffixa.
- `schema_version` i `release` moraju biti izravna root polja; `p4` izravni root
  objekt; `url`, `size`, `sha256` njegova izravna polja. Redoslijed nije bitan.
- Prepoznati ključ u istom objektu smije se pojaviti samo jednom, uključujući
  ekvivalentan escaped zapis imena. Nepoznata polja smiju se preskočiti tek nakon
  pune sintaksne validacije njihove vrijednosti i depth/size provjere.
- Podržati JSON string escapeove, validirati UTF-8 i surrogate parove. Poznata
  polja nakon dekodiranja prolaze postojeće duljinske i semantičke provjere;
  odbiti decoded NUL/control znakove. URL i SHA ostaju ograničeni postojećim ugovorom.
- Size je pozitivan uint32 zapisan JSON integerom; overflow, minus, fraction,
  exponent i leading-zero zapis nisu prihvatljivi za to polje. Schema mora biti 1.
- Validan dokument bez `p4` vraća NO_TARGET; nevaljan dokument bez `p4` mora
  vratiti MALFORMED. Output se popunjava iz privremenog objekta samo na potpuni OK.
- Ne mijenjati HTTPS, offer TTL, newer-only, size/SHA ni ECDSA provjeru paketa.

Testirati sve review primjere, svaki prefiks/truncation valjanog manifesta,
trailing garbage, braces unutar stringa, escapeove, duplicate ključ, pogrešan
nesting/tip, granice duljina/uint32/depth i važeći output publish skripte.
Provesti bounded mutacijski corpus nad parserom uz ASan/UBSan u Linux host jobu;
seed corpus i fiksni seed moraju biti ponovljivi, bez zahtjeva za trajnim fuzz servisom.

### F08 — Status eksplicitno odabrane operacije

Adapter drži `operation_id` i `kind` (`link`, `check`, `install`) pod kratkim
lockom. Start pozive serijalizirati zasebnim mutexom; posljednja prihvaćena
operacija postaje izvor statusa. Odbijeni start ne mijenja vidljivu operaciju.

- Prije prihvata novog starta primijeniti postojeću globalnu busy/admission
  provjeru; dva različita workera ne smiju krenuti istodobno kroz race.
- Snapshot statusa mora povezati operation ID i pripadni backend rezultat;
  ako se operation promijeni tijekom čitanja, ponoviti snapshot.
- API aditivno izlaže kind/ID uz postojeće state/detail/address. UI može
  odbaciti zakašnjeli odgovor starije operacije.
- Link probe ne resetira sačuvanu OTA ponudu niti produljuje njezin TTL. Instalacija
  i dalje provjerava vlastiti offer token/verziju/freshness neovisno o prikazu.
- Na bootu nema aktivne operacije; nakon završetka ostaje rezultat zadnje
  prihvaćene operacije do novog starta.

Testovi: check→probe, failed check→successful probe, probe→check, check→install,
odbijen busy start, konkurentni startovi, zakašnjeli HTTP status i istekao offer.
Business logiku adaptera izdvojiti u mali testabilni modul; app_main ostaje wiring.

## 8. P9 — Integracija, prihvat i objava

### Software gates

Za svaki paket izvršiti njegove ciljane testove i P4 build ako dira firmware.
Na integriranom kandidatu obavezno:

1. Cijeli `tests/run_p4_host_tests.ps1`, uključujući nove JS/NVS/catalog/parser
   regresije u postojećem runneru; bez tihog skipa alata u CI-ju.
2. Windows PowerShell 5.1 i PowerShell 7 runner smoke te postojeći Linux CI.
   Ne duplicirati workflow: proširiti postojeći host job samo novim testovima.
3. ESP-IDF v6.0.2 clean build u postojećem CI jobu i lokalni P4 build; dependency
   lock nepromijenjen osim ako postoji izričito opravdana dependency promjena.
4. UI simulator 7/7 baselinea. Baseline mijenjati samo ako se namjerno promijeni
   vidljivi prikaz i nakon vizualnog pregleda; novi cue fixture ima zasebne očekivane podatke.
5. Dual-deck keylock virtualni soak od 300 s bez spuštanja postojećih pragova.
6. Real-file MP3/PDB testovi s unaprijed pripremljenim fixtureima; signed OTA
   negative/positive, package validation i postojeći USB software harness gateovi.
7. Dokumentacijski checker, whitespace check, binary budget, source i artefact
   hashovi. Nije dovoljno da izvorni review probe više ne ispisuje stari rezultat.

### Fizička matrica

Testirati na bench uređaju, bez aktivnog produkcijskog nastupa. Zabilježiti
konfiguraciju izlaza, FLX4, USB medije i točan image hash.

| Scenarij | Minimum | Kriterij |
| --- | --- | --- |
| Identity/cues | obje kolekcije istog ID-ja, oba decka, 10 remount/reboot ciklusa | nema tuđih cueova; legacy sačuvan |
| ANLZ | stvarni export cue A/C + loop, cold/warm put | vremena i slotovi jednaki referenci |
| Forced timeline | najmanje 100 prisiljenih wrap/handoff ponavljanja u test buildu | napredovanje, coherent cursor, bez WDT-a |
| Web | desktop + telefon, dva klijenta, brzi slideri i spora mreža | konačni state jednak zadnjem uspješnom inputu |
| Library | 1024 reda, sort/remount uz fetch | bez miješanih redaka i pogrešnog LOAD-a |
| Meter | stereo signal, limiter, silence, stop/fault | definiran decay/stale i netaknuti lifetime counters |
| NVS | kontrolirani restart/power cut na set/clear/migration granicama | cijela konfiguracija ili jasna greška, bez resurrectiona |
| OTA | valid/invalid channel, check/probe slijed, TTL, signed candidate | točan status; ista postojeća sigurnosna pravila |
| Audio soak | najmanje 180 min na završnom neinstrumentiranom binaryju | bez WDT/panica/novog data loss, operator potvrđuje MAIN/cue |

Soak uključuje oba decka, Master Tempo, cue/seek/loop, scratch/censor i web
upravljanje. Uz postojeća release ograničenja ponoviti primjenjivu USB lifecycle
matricu iz release/checklist dokumentacije; raniji waiveri ostaju eksplicitni,
ne prikazuju se kao novi PASS. Output-late brojače analizirati prema postojećim
pragovima i baselineu, ne automatski izjednačavati svaki late događaj s audio gubitkom.

### Dokumentacija i rollout

- Za svaki F-ID upisati commit, ciljanu regresiju i dokaz; R01 mora završiti kao
  dokazano zaštićen ili popravljen, a ne samo "nije se dogodilo".
- Uskladiti README, DEVELOPMENT_PLAN, STARTUP_CHECKLIST, RISK_REGISTER i
  DOCUMENTATION_STATUS s implementacijom i stvarno završenim gateovima.
- Release notes izričito opisuju novi identitet, prestanak automatskog čitanja
  legacy hot cues, cache v3 i moguću ponovnu konfiguraciju Wi-Fi-ja pri rollbacku.
- Software-ready i hardware-accepted vode se odvojeno. Ne označiti release
  production-ready dok P9 fizički kriteriji i slušno prihvaćanje nisu završeni.
- Tek tada pripremiti potpisani release paket za novi immutable tag prema
  postojećem release postupku. Naziv budućeg taga, javna objava i instalacija
  nisu automatske radnje ovog plana; postojeći M2.2 artefakti ostaju nepromijenjeni.

## 9. Definition of Done

Plan je implementiran kada svih devet nalaza ima reprodukciju koja bi pala na
starom sourceu i prolazi na novom, R01 ima dokaz napredovanja, svi relevantni
software gateovi prolaze te su za točan kandidat završeni fizički i slušni gateovi.

Ako hardware nije dostupan, isporuka se zove **software candidate, hardware
acceptance pending**. Izvješće mora navesti preostale testove i točan sljedeći
korak; ne traži se povjerenje u prethodni firmware soak umjesto novog dokaza.
