# AGENTS.md

## Uloga

Djeluj kao senior embedded C / ESP-IDF / real-time audio inzenjer za projekt
DDJ-FFL4. Ovo nije NovaPlayout/Avalonia projekt.

Komunikacija s korisnikom neka bude na hrvatskom jeziku. Kod, nazivi datoteka,
commit poruke, C simboli i tehnička dokumentacija mogu ostati na engleskom ako
je to prirodnije za firmware projekt.

## Projekt

DDJ-FFL4 je fork-style port projekta `dvucinozd/CDJ100S-XXX`.

Cilj je standalone dual-deck DJ sustav:

- Pioneer DDJ-FLX4 je operator surface.
- ESP32-S3 je USB MIDI host i MIDI-to-control-link prevoditelj.
- ESP32-P4 JC4880P443C_I_W je autoritativni playback/UI/audio engine.
- Postojeci `0xA5` UART `control_link` ostaje interna komunikacija izmedu S3 i
  P4.

Projekt je prerastao uvezeni single-deck CDJ100S baseline. Trenutni `master`
ima funkcionalan dual-deck FLX4 put, vinyl/scratch, Master Tempo, dualni
MAIN/cue audio i P4/S3 OTA. I dalje ga ne tretiraj kao production-ready bez
provjere aktualnih rizika, buildova i relevantnog hardware smoke testa.

## Najvaznije putanje

```text
D:\Documents\DDJ-FFL4
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
  docs\reference\CDJ100S-XXX-README.md
  firmware\control-board-s3
  firmware\main-deck-p4
  tests
```

Prije vecih promjena procitaj relevantne dokumente iz `docs\` i postojece
komponente koje diras. Mixxx XML se smatra provjerenim i autoritativnim izvorom
MIDI adresa za DDJ-FLX4 kontrole (sva dosadašnja mapiranja su se pokazala 100%
točnima). Fizički raw MIDI capture više nije preduvjet za razvoj, te se
preostale kontrole mogu implementirati izravno iz XML reference.

## ESP-IDF okruzenje

Lokalni ESP-IDF je instaliran ovdje:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
```

Provjera:

```powershell
idf.py --version
```

Zadnje provjereno okruzenje:

- ESP-IDF v5.5
- Python: `C:\Espressif\python_env\idf5.5_py3.11_env\Scripts`
- Git iz Espressif toolchaina: `C:\Espressif\tools\idf-git\2.44.0\cmd`
- Host-test GCC: `C:\msys64\ucrt64\bin`

Napomena: `idf.py` nije nuzno dostupan prije pokretanja
`Initialize-Idf.ps1`.

Za host testove koji traze `gcc`/`make`, ako nisu vec u `PATH`, koristi:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
```

## Build naredbe

S3 firmware:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py build
```

P4 firmware:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py build
```

Zadnja poznata provjera na bootstrap commitu:

- `firmware\control-board-s3`: `idf.py build` prolazi.
- `firmware\main-deck-p4`: `idf.py build` prolazi.

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
