# Preporuke za daljnji razvoj DDJ-FFL4

Pregledom kompletnog koda repozitorija, uočio sam sljedeća ključna područja na koja vrijedi usmjeriti daljnji razvoj i optimizaciju, u skladu s dokumentiranim planom.

## 1. Implementacija transparentnog master limitera (Soft-Clip)

Trenutni *audio mixer* u datoteci `firmware/main-deck-p4/components/audio_engine/audio_mixer.c` radi tvrdo zbrajanje uz sirovo odsijecanje (hard clipping) kada signal prijeđe `32767` ili padne ispod `-32768`:

```c
float mixed = ((float)deck1 * clamp_gain(deck1_gain)) +
              ((float)deck2 * clamp_gain(deck2_gain));

if (mixed > 32767.0f) return 32767;
if (mixed < -32768.0f) return -32768;
return (int16_t)(mixed >= 0.0f ? mixed + 0.5f : mixed - 0.5f);
```

**Preporuka:**
Prema Phase 4 u `DEVELOPMENT_PLAN.md`, potrebno je uvesti *transparent limiter/soft-clip stage*. Treba osigurati da normalna razina jednog decka ostane nepromijenjena (kako ne bi došlo do opadanja glasnoće). Umjesto hard limitera iznad *ceilinga*, predlažem primjenu nelinearne prijelazne funkcije (npr. *tanh* ili jednostavne kubične aproksimacije) koja zahvaća samo kada vršne vrijednosti oba zbrojena decka premaše unaprijed definirani prag (npr. iznad `~28000`), dok ostaje potpuno linearna unutar sigurnog raspona. Ovo će drastično poboljšati percipiranu kvalitetu zvuka pri preklapanju basova dvaju deckova.

## 2. Implementacija VU metara i LED Feedbacka

U `docs/DDJ_FLX4_MIDI_MAP.md` zapisano je da VU metri za Channel 1 i 2 zahtijevaju *Control Change (CC)* MIDI slanje natrag u S3:
* Channel 1: `0xB0 / 0x02` (vrijednost od 0 do 127)
* Channel 2: `0xB1 / 0x02` (vrijednost od 0 do 127)

Trenutno `control_link.h` podržava samo tip `CTRL_TYPE_LED` (vrijednost 0, 1 ili 2 za treperenje).

**Preporuka:**
*   Proširiti `control_link` protokol kako bi P4 mogao slati analogne povratne informacije (poput VU metara) natrag na S3, a ne samo on/off stanja gumba. Može se definirati novi tip `CTRL_TYPE_VU_METER` (npr. `0x83`).
*   P4 `audio_engine` bi u sklopu `audio_mixer_mix_stereo` trebao pratiti kratkoročne vršne amplitude (RMS ili peak s brzim attackom i sporim releasom) te tu informaciju mapirati u `0..127` prostor.
*   S3 preuzima tu vrijednost i putem `flx4_midi_host_send_packet` šalje natrag u MIDI obliku (`0xB0` za Deck 1 CC poruku, te `0x02` za VU metar).

## 3. Optimizacija performansi i stabilnosti UI-ja

Dokumentirane napomene iz lipnja pokazuju velik napredak u nultoj kopiji (zero-copy scroll) i I8-to-RGB565 *caching* mehanizmima (`ui_overview_wave_cache`). Usprkos tome, LVGL re-render šiljci i dalje se kreću od 30-35 ms.

**Preporuka:**
*   S obzirom na to da su UI *taskovi* pinnani na **Core 1** a obrada mreže na **Core 0**, provjerite može li se LVGL *draw task* dodatno de-prioritizirati kada `deck_core` ili audio zadaci postanu opterećeni zbog učitavanja trake.
*   Kako je već navedeno za audio učitavanje (PQTZ buffer alokacije prebačene na PSRAM), preporuča se isto napraviti i sa svim privremenim LVGL *draw bufferima* ako PSRAM protočnost zadovoljava grafičke potrebe.

## 4. Rješavanje zaostalih Smart CFX i Smart Fader modova

Ovi kontroleri (`0x96/0x00` i `0x96/0x01`) mapirani su s FLX4 i prenose se u P4 unutar sistemskog domene (`CTRL_ID_SMART_CFX`).

**Preporuka:**
Prije dodavanja naprednih mix funkcija s EQ/Filterima (Phase 7), vrijedi definirati kakvo točno softversko ponašanje očekujete od "Smart CFX-a" u P4 i kako će to ući u `audio_mixer.c`. Može se uvesti odvojeni *DSP task* koji vrši multi-band kompresiju ili dodaje filter sweep kada se stisne tipka za "Smart" mod.
