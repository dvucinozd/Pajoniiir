# AGENTS.md

## Uloga

Djeluj kao senior embedded C / ESP-IDF / real-time audio inzenjer za projekt
Pajoniiir. Ovo nije NovaPlayout/Avalonia projekt.

Komunikacija s korisnikom neka bude na hrvatskom jeziku. Kod, nazivi datoteka,
commit poruke, C simboli i tehnička dokumentacija mogu ostati na engleskom ako
je to prirodnije za firmware projekt.

## Projekt

Cilj je standalone dual-deck DJ sustav:

- Pioneer DDJ-FLX4 je operator surface.
- ESP32-S3 je USB MIDI host i MIDI-to-control-link prevoditelj.
- ESP32-P4 JC4880P443C_I_W je autoritativni playback/UI/audio engine.
- Postojeci `0xA5` UART `control_link` ostaje interna komunikacija izmedu S3 i
  P4.

Trenutni `master` ima funkcionalan dual-deck FLX4 put, vinyl/scratch, Master
Tempo, dualni MAIN/cue audio, Beat FX Filter/Echo/Flanger/Delay i P4/S3 OTA.
Pull OTA koristi newer-only politiku, desetominutni offer TTL, provjeru
channel size/SHA-256, `pajoniiir.local` i dinamički Host allow-list; lokalni
potpisani push OTA ostaje servisni rollback put. Controller browse/load UI
naredbe izvršavaju se isključivo iz `ui_update()` u LVGL tasku. Uz FLX4
fixture postoji i `controllers/generic_midi_ci` za host provjeru generičkog
profile puta, ali ne predstavlja hardware prihvat stvarnog non-FLX4 uređaja.
I dalje ga ne tretiraj kao production-ready bez provjere aktualnih rizika,
buildova i relevantnog hardware smoke testa.

## Najvaznije putanje

```text
<repo-root>
  README.md
  docs\PROJECT_OVERVIEW.md
  docs\ARCHITECTURE.md
  docs\DDJ_FLX4_MIDI_MAP.md
  docs\CONTROL_LINK_PROTOCOL.md
  docs\HARDWARE_WIRING.md
  docs\DEVELOPMENT_PLAN.md
  docs\STARTUP_CHECKLIST.md
  docs\RISK_REGISTER.md
  docs\DOCUMENTATION_STATUS.md
  docs\OTA-UPDATE.md
  docs\reference\Pioneer-DDJ-FLX4.midi.xml
  firmware\control-board-s3
  firmware\main-deck-p4
  controllers\generic_midi_ci
  tests
```

Prije vecih promjena procitaj relevantne dokumente iz `docs\` i postojece
komponente koje diras. Mixxx XML se smatra provjerenim i autoritativnim izvorom
MIDI adresa za DDJ-FLX4 kontrole (sva dosadašnja mapiranja su se pokazala 100%
točnima). Fizički raw MIDI capture više nije preduvjet za razvoj, te se
preostale kontrole mogu implementirati izravno iz XML reference.

## ESP-IDF okruzenje

Projekt se razvija na dva Windows računala. Na računalu s klasičnom
Espressif instalacijom inicijaliziraj ESP-IDF ovako:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
```

Na računalu s ESP-IDF v5.5.4 profilom koristi:

```powershell
. C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1
```

Nakon bilo koje od te dvije inicijalizacije provjeri:

```powershell
idf.py --version
```

Podržana i provjerena okruženja:

- ESP-IDF v5.5, Python:
  `C:\Espressif\python_env\idf5.5_py3.11_env\Scripts`
- ESP-IDF v5.5.4, IDF: `C:\esp\v5.5.4\esp-idf`, Python:
  `C:\Espressif\tools\python\v5.5.4\venv`
- Git iz Espressif toolchaina: `C:\Espressif\tools\idf-git\2.44.0\cmd`
- Host-test GCC: `C:\msys64\ucrt64\bin`

Napomena: `idf.py` nije nužno dostupan prije pokretanja odgovarajuće
inicijalizacijske skripte.

Za host testove koji traze `gcc`/`make`, ako nisu vec u `PATH`, koristi:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

## Build naredbe

S3 firmware:

```powershell
# Najprije inicijaliziraj jedno od podržanih ESP-IDF okruženja.
$repoRoot = git rev-parse --show-toplevel
Set-Location "$repoRoot\firmware\control-board-s3"
idf.py build
```

P4 firmware:

```powershell
# Najprije inicijaliziraj jedno od podržanih ESP-IDF okruženja.
$repoRoot = git rev-parse --show-toplevel
Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py build
```

Zadnja poznata clean release provjera:

- `RC1-259-gdaf4639`, 2026-07-26, ESP-IDF 5.5.4;
- oba izolirana `build_signed` targeta prolaze;
- tocne velicine i SHA-256 zapisi su u
  `docs\validation\CLEAN_RELEASE_RC1_259_BUILD.md`.

Za dugi deterministicki dual-deck Master Tempo PC regression koristi:

```powershell
.\tests\audio_keylock_soak\run_audio_keylock_soak.ps1
```

Zadani run simulira pet minuta oba decka i provjerava drift, pitch, DSP
finite-state, velike sample skokove i clipping. To nije zamjena za P4 CPU/I2S
deadline mjerenje ili slusni hardware acceptance.

## Git i build artefakti

Repo koristi `.gitignore` za ESP-IDF artefakte:

- `build/`
- `managed_components/`
- `dependencies.lock`
- `sdkconfig`
- `sdkconfig.old`

Ne commitaj generirane build direktorije ili lokalni `sdkconfig` osim ako
korisnik eksplicitno trazi drugacije.

Branch prefix za agent promjene je `codex/`.

Canonical repo je `https://github.com/dvucinozd/Pajoniiir.git`, preimenovan sa
starog `ESP32-DDJ-FLX4`. Stari URL jos redirecta, ali novi cloneovi i lokalni
`origin` trebaju koristiti canonical URL. Audit i ciscenje od 2026-07-26
potvrdili su da postoji samo `master` grana — lokalno i na `origin`; sve tada
prisutne pomocne grane bile su potpuno mergane, imale su 0 jedinstvenih
commitova i obrisane su. Jedinstveni odbaceni rad se ne brise nego arhivira pod
anotiranim tagom `attic/*` (npr. `attic/phase-8-status-led-policy` = GPIO48
WS2812 RGB status-LED policy engine, superseded XIAO GPIO21 jednobojnim LED-om).
Prije brisanja bilo koje grane pokreni `git branch --no-merged master`, provjeri
`git rev-list --count master..<branch>` i po potrebi tagiraj u `attic/*`.

## Arhitektonska pravila

- P4 je autoritativan za playback state, deck state, audio position, mixer
  state i LED odluke.
- S3 smije citati FLX4 MIDI, normalizirati input i slati semanticke evente.
- S3 ne smije odlucivati je li deck stvarno playing, current/next, cue state
  ili audio position.
- MIDI je transport/input mapping, ne state model.
- Zadrzi `0xA5` frame za MVP osim ako stvarno blokira implementaciju.
- Prva firmware faza je `flx4_midi_host` raw MIDI capture na S3, prije
  promjene P4 dual-deck logike.

## DDJ-FLX4 MVP kontrole

MVP kontrole su u potpunosti potvrđene raw MIDI captureom i implementirane u firmwareu. Preostale kontrole iz proširenog inventara u `docs/DDJ_FLX4_MIDI_MAP.md` uvode se izravno iz Mixxx XML-a. Fizički smoke capture radi se kao naknadni test prihvaćanja, a ne kao preduvjet za kodiranje.

Primarni mapping dokument je:

```text
docs\DDJ_FLX4_MIDI_MAP.md
```

Izvorni XML je:

```text
docs\reference\Pioneer-DDJ-FLX4.midi.xml
```

## Verifikacija prije zavrsetka

Prije tvrdnje da je posao gotov:

1. Pokreni relevantnu provjeru.
2. Procitaj exit code i bitan output.
3. Navedi sto je proslo, a sto nije pokrenuto.

Za dokumentacijske promjene minimalno:

```powershell
git diff --check
git status --short
```

Za firmware promjene pokreni barem build target koji je diran. Ako promjena
dotice shared protokol ili oba targeta, pokreni oba builda.

## Stil rada

- Koristi `rg` / `rg --files` za pretragu.
- Koristi `apply_patch` za rucne izmjene datoteka.
- Ne revertaj korisnicke promjene bez izricitog zahtjeva.
- Ne cisti masovno upstream whitespace samo radi estetike; uvezeni baseline
  treba ostati lako usporediv s izvorom.
- Ako mijenjas dokumentaciju o fazama, uskladi `README.md`,
  `docs\DEVELOPMENT_PLAN.md` i `docs\STARTUP_CHECKLIST.md` kad je relevantno.
