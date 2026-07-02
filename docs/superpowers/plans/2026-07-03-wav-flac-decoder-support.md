# Implementacijski plan V2 (usklađen): WAV + FLAC u `ESP32-DDJ-FLX4`

Repozitorij: `https://github.com/dvucinozd/ESP32-DDJ-FLX4`

> **Napomena o ovoj verziji.** Ispravljena verzija originalnog WAV/FLAC plana,
> uparena sa stvarnim audio engineom na `master` (2026-07-03). Decoder-abstraction
> smjer (WAV → FLAC → MP3 refaktor) je zadržan jer je arhitekturno zdrav, ali su
> ispravljene neusklađenosti s kodom:
>
> 1. **PCM ring API.** Ne postoji `audio_pcm_ring_write(...)`; stvarno je
>    `audio_pcm_ring_push(ring, int16_t l, int16_t r)` → `bool` (SPSC, non-blocking).
> 2. **Load potpis.** Stvarno je `audio_engine_deck_load(uint8_t deck, const char
>    *mp3_path, const uint32_t *pvbr_400, uint32_t duration_ms)`, ne
>    `audio_engine_load_for_deck(int, const char*)`.
> 3. **Rekordbox/ANLZ sprega.** Load put je vezan uz PVBR seek tablicu i ANLZ
>    beatgrid/duration. Dodana je cijela sekcija kako WAV/FLAC dobivaju
>    duration/seek/beatgrid (i gdje graciozno degradiraju).
> 4. **`media_io_gate`.** File IO mora ići kroz `media_io_gate_begin/end` zbog
>    kontencije s USB MSC/library, ne goli `fopen`/`fread`.
> 5. **Dinamički output rate.** Projekt rekonfigurira I2S clock po sample rateu
>    pjesme i mixa 44.1/48 kroz per-deck resampler; `CONFIG_AUDIO_FIXED_OUTPUT_
>    SAMPLE_RATE` je izbačen jer proturječi tome.
> 6. **Izlaz.** MAIN je PCM5102A (RCA); cue/monitor su FLX4 USB slušalice preko
>    USB Audio Classa. **ES8311 je izbačen** iz projekta.

---

## 1. Cilj i stvarni pipeline

Stvarno trenutno stanje (provjereno u kodu):

```text
MP3 (Rekordbox USB) -> media_io_gate -> progressive preload u PSRAM (audio_fw_preload)
   -> minimp3 decode task -> audio_pcm_ring (SPSC, po-frame) -> audio_resampler
   -> audio_output_mixer -> {PCM5102A RCA MAIN, FLX4 USB slušalice cue}
```

Cilj:

```text
audio file (.mp3/.wav/.flac)
  -> format detection (header, ekstenzija fallback)
  -> decoder abstraction (audio_decoder_t + ops)
  -> MP3 decoder / WAV parser / FLAC decoder
  -> audio_pcm_ring  (ISTI ring, ista SPSC push semantika)
  -> audio_resampler -> audio_output_mixer -> MAIN + cue
```

Ključno pravilo (nepromijenjeno iz originala, i dalje vrijedi): WAV/FLAC ne
dodavati kao `if format` grane u `audio_engine.c`, nego kroz tanki decoder layer.

---

## 2. NOVO: Rekordbox/ANLZ sprega (najvažnija ispravka)

P4 load put nije format-agnostičan `fopen`. `audio_engine_deck_load` prima:

```c
esp_err_t audio_engine_deck_load(uint8_t deck,
                                 const char *mp3_path,
                                 const uint32_t *pvbr_400,   // MP3 PVBR seek tablica
                                 uint32_t duration_ms);      // iz ANLZ beat-grida
```

- **`pvbr_400`** je MP3-specifičan (400-entry byte-seek tablica iz Rekordboxa).
  Za WAV/FLAC se **ne koristi** — proslijediti `NULL`; seek ide native
  (WAV: byte offset; FLAC: `drflac_seek_to_pcm_frame`).
- **`duration_ms`** dolazi iz ANLZ beat-grida (Rekordbox analiza). Za analizirane
  fajlove (učitane s Rekordbox USB-a) ANLZ postoji i po path-u se veže. Za
  WAV/FLAC bez ANLZ-a, duration se uzima iz dekodera: `total_frames / sample_rate`.
- **Beatgrid / BPM / waveform** dolaze isključivo iz ANLZ-a. WAV/FLAC bez ANLZ-a
  **nemaju** beatgrid → sync, beat-match guide linije i Overview waveform
  **graciozno degradiraju** (pozicija i transport rade; BPM sync/quantize/beat
  jump su onemogućeni dok nema beatgrida).

**Odluka koju plan mora eksplicitno donijeti prije implementacije:**

```text
[ ] Scenarij A: WAV/FLAC se učitavaju SAMO iz Rekordbox knjižnice (imaju ANLZ).
    -> puna funkcionalnost; load put ostaje library-driven, samo dekoder je nov.
[ ] Scenarij B: WAV/FLAC iz proizvoljnih foldera (bez ANLZ).
    -> transport radi, ali sync/beatgrid/waveform su isključeni za te trackove;
       UI mora to jasno pokazati ("no beatgrid").
```

Preporuka: krenuti sa Scenarijem A (najmanje promjena u library/UI putu), pa
Scenarij B kao zasebna faza uz UI oznaku.

---

## 3. Formati po fazama

### Faza 1 (minimalno stabilno)
- **MP3**: postojeći minimp3 put.
- **WAV**: RIFF/WAVE, PCM integer, 1–2 kanala, 16-bit, LE. Mono → duplicirati u
  stereo. (24/32/float su Faza 2, u Fazi 1 se odbijaju.)
- **FLAC**: native stream, 1–2 kanala, 16 ili 24-bit. 24-bit → 16-bit (`>> 8`).

### Faza 2: WAV 24/32-bit + float + extensible, FLAC seek/metadata, tagovi.
### Faza 3: chunked cache, waveform/peak (ako nema ANLZ), background seek index.

---

## 4. Struktura datoteka

```text
firmware/main-deck-p4/components/audio_engine/
  include/{audio_format.h, audio_decoder.h, audio_wav_decoder.h, audio_flac_decoder.h}
  audio_format.c
  audio_decoder.c
  audio_wav_decoder.c
  audio_flac_decoder.c
  third_party/dr_libs/dr_flac.h        # ili managed component
```

---

## 5–8. Format detekcija + decoder interface

`audio_format.h/.c` i `audio_decoder.h/.c` — **nepromijenjeni u odnosu na
original** (header detekcija RIFF/fLaC/ID3/0xFFE, `audio_detect_format_from_path`
fallback, `audio_decoder_t` s ops tablicom i `read_pcm_s16` interleaved stereo).
Interleaved s16 stereo izlaz se poklapa s ring semantikom (L/R int16 par).

Jedina izmjena: u `audio_decoder_open` MP3 grana vraća `ESP_ERR_NOT_SUPPORTED`
za sada (MP3 ostaje u `audio_engine.c`), a WAV/FLAC granaju u svoje dekodere —
ali **cijeli file IO ide kroz `media_io_gate`** (vidi §9).

---

## 9. NOVO: file IO kroz `media_io_gate`

Audio engine već koristi `media_io_gate` ([audio_engine.c](firmware/main-deck-p4/components/audio_engine/audio_engine.c))
za kontenciju s USB MSC/library. WAV/FLAC dekoderi ne smiju golim `fopen`/`fread`
zaobići taj gate. Stvarni API:

```c
// media_io_gate.h
void media_io_gate_begin(void);
bool media_io_gate_try_begin(uint32_t timeout_ms);
void media_io_gate_end(void);
```

Obrazac u dekoderu (i za open i za svaki blok čitanja):

```c
media_io_gate_begin();
size_t n = fread(buf, 1, want, w->fp);
media_io_gate_end();
```

Za FLAC callback (`flac_on_read`/`flac_on_seek`) isto — omotati `fread`/`fseek`
gateom, ili (bolje) čitati u većim blokovima da se gate ne uzima po sitnom I/O.

---

## 10. WAV decoder — ispravci

Parser strukture, LE helperi, header parsing, open, seek, close: **kao u
originalu** (RIFF/WAVE, `fmt `/`data`, padding, `audio_format==1`, 16-bit,
`total_frames = data_size / block_align`). Dvije izmjene:

**Ispravak A — bufferirano čitanje (ne po frameu).** Original radi `fread` od
2–4 bajta po frameu (syscall po sampleu). Umjesto toga čitati blok pa dekodirati
iz buffera:

```c
static esp_err_t wav_read_pcm_s16(audio_decoder_t *dec, int16_t *out,
                                  size_t frames_requested, size_t *frames_read)
{
    wav_decoder_impl_t *w = dec->impl;
    *frames_read = 0;

    uint8_t blk[512 * 4];                 // do 512 stereo frameova po pozivu
    size_t remaining = frames_requested;
    size_t produced = 0;

    while (remaining > 0 && w->current_frame < w->total_frames) {
        size_t batch = remaining < 512 ? remaining : 512;
        size_t want  = batch * w->block_align;

        media_io_gate_begin();
        size_t got = fread(blk, 1, want, w->fp);
        media_io_gate_end();

        size_t got_frames = got / w->block_align;
        if (got_frames == 0) break;

        for (size_t i = 0; i < got_frames; i++) {
            const uint8_t *p = blk + i * w->block_align;
            if (w->channels == 1) {
                int16_t s = (int16_t)rd_u16le(p);
                out[(produced + i) * 2 + 0] = s;
                out[(produced + i) * 2 + 1] = s;
            } else {
                out[(produced + i) * 2 + 0] = (int16_t)rd_u16le(p + 0);
                out[(produced + i) * 2 + 1] = (int16_t)rd_u16le(p + 2);
            }
        }
        produced += got_frames;
        remaining -= got_frames;
        w->current_frame += got_frames;
    }

    *frames_read = produced;
    return produced > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}
```

**Ispravak B — `ftello`/64-bit offset** za velike WAV (>2 GB rijetko, ali
`data_offset`/seek koristiti `long`/`off_t` konzistentno; original to već flagira).

---

## 11. FLAC decoder — ispravci

`dr_flac` state, callbacks, open, read, seek, close: **kao u originalu**, uz:

- **Managed component umjesto ručnog vendoranja** (preporuka za IDF v5.5):
  ```yaml
  # components/audio_engine/idf_component.yml
  dependencies:
    # ili vendorati dr_flac.h u third_party/ i #define DR_FLAC_IMPLEMENTATION
  ```
  Ako se vendoruje, `#define DR_FLAC_IMPLEMENTATION` točno u **jednom** `.c`.
- **Seek callback potpis ovisi o verziji** `dr_flac` (`drflac_seek_origin_current`
  vs noviji enumi, 32-bit `int offset`). Prije koda potvrditi verziju i uskladiti
  potpise; za velike fajlove testirati 64-bit seek.
- **`media_io_gate`** oko `fread`/`fseek` u callbackovima (§9).
- Mono FLAC → duplicirati u stereo (kao original); `tmp[256]` po mogućnosti u
  decoder state (heap), ne na stacku decode taska.

---

## 12. NOVO: uklapanje u `audio_engine.c` (stvarni model)

### 12.1 Stvarni load potpis i put

```c
// stvarni potpis — pseudodiff cilja OVO, ne audio_engine_load_for_deck
esp_err_t audio_engine_deck_load(uint8_t deck,
                                 const char *audio_path,     // preimenovano iz mp3_path
                                 const uint32_t *pvbr_400,   // NULL za WAV/FLAC
                                 uint32_t duration_ms)       // ANLZ ili 0
{
    // 1. detektiraj format (header pa ekstenzija) — čitanje kroz media_io_gate
    // 2. MP3  -> postojeći minimp3/progressive-preload put (nepromijenjen)
    // 3. WAV/FLAC -> audio_decoder_open(&rt->decoder, audio_path, NULL, 0)
    //    rt->sample_rate  = rt->decoder.info.sample_rate;
    //    rt->total_frames = rt->decoder.info.total_frames;
    //    if (duration_ms == 0 && rt->sample_rate)
    //        duration_ms = (uint32_t)(rt->decoder.info.total_frames * 1000ULL
    //                                 / rt->sample_rate);
}
```

### 12.2 Postojeći model je "loader + decoder split" (ne čisti full-preload)

Stvarni MP3 put je **progressive preload** ([audio_engine.c:752](firmware/main-deck-p4/components/audio_engine/audio_engine.c#L752)):
loader puni PSRAM buffer, decoder task dekodira iz buffera u ring. WAV/FLAC
imaju dvije opcije — plan mora izabrati:

```text
[ ] Opcija 1 (preporuka za WAV): file-backed streaming producer.
    Novi producer čita direktno iz fajla (kroz media_io_gate) i gura u ISTI
    audio_pcm_ring. WAV je velik pa se ne preloada; sekvencijalno čitanje je jeftino.
[ ] Opcija 2 (za male FLAC): reuse postojećeg preloada u PSRAM pa dr_flac
    memory-backed (drflac_open_memory).
```

Bez obzira na opciju, **izlaz je uvijek isti `audio_pcm_ring`**, pa output/mixer
put ostaje netaknut.

### 12.3 ISPRAVLJENI decode task (točan PCM ring API + backpressure)

Original je koristio nepostojeći `audio_pcm_ring_write(...portMAX_DELAY)`.
Stvarni API je po-frame `bool audio_pcm_ring_push(ring, l, r)` (vraća `false`
kad je pun). Točan producer:

```c
static void ae_stream_decode_task(void *arg)
{
    audio_fw_runtime_t *rt = arg;
    int16_t pcm[512 * 2];

    while (!rt->stop_requested) {
        size_t frames_read = 0;
        esp_err_t rc = audio_decoder_read_pcm_s16(&rt->decoder, pcm, 512, &frames_read);

        if (rc == ESP_ERR_NOT_FOUND || frames_read == 0) { rt->eof = true; break; }
        if (rc != ESP_OK) { rt->decode_error = rc; break; }

        // Po-frame push s backpressureom: ring je SPSC, push vraća false kad je pun.
        for (size_t i = 0; i < frames_read; ) {
            int16_t l = pcm[i * 2 + 0];
            int16_t r = pcm[i * 2 + 1];
            if (audio_pcm_ring_push(rt->pcm_ring, l, r)) {
                i++;
            } else {
                vTaskDelay(pdMS_TO_TICKS(2));   // ring pun -> pusti output da drenira
                if (rt->stop_requested) break;
            }
        }
    }

    audio_decoder_close(&rt->decoder);
    notify_task_done(rt);
    vTaskDelete(NULL);
}
```

> Modelirati po postojećem MP3 decode tasku u `audio_engine.c` (isti prioritet,
> ista SPSC push/backpressure logika, isti `pcm_ring` po decku).

### 12.4 Output/mixer/resampler ne znaju format

Nepromijenjeno pravilo: output task vidi samo `audio_pcm_ring` → resampler →
mixer. Per-deck resampler kompenzira source rate (npr. 44.1) na trenutni output
rate. **Nema `if MP3/WAV/FLAC` u output putu.**

---

## 13. Seek

- **WAV**: `byte = data_offset + frame_index * block_align` (native, precizno).
- **FLAC**: `drflac_seek_to_pcm_frame(...)` (testirati na više fajlova; ako je
  spor, Faza 3 seek index).
- **MP3**: postojeći PVBR seek (`pvbr_400`), nepromijenjeno.
- Nakon seeka resetirati `pcm_ring` decka da se stari sampleovi ne pomiješaju.

---

## 14. Memorijski model

- **MP3**: progressive preload u PSRAM (kao sad).
- **WAV**: file-backed streaming (§12.2 Opcija 1) — velik, ne preloadati.
- **FLAC**: file-backed `dr_flac` callback; mali fajlovi opcionalno memory-backed.

---

## 15. Error handling + UI

Custom kodovi (`ESP_ERR_AUDIO_*`) ili postojeći (`ESP_ERR_NOT_SUPPORTED`,
`ESP_ERR_INVALID_SIZE`, `ESP_ERR_NO_MEM`, `ESP_FAIL`) — kao original. UI poruke:
`UNSUPPORTED FORMAT`, `INVALID WAV/FLAC`, `TRACK TOO LARGE`, `DECODE ERROR`.

**Dodatno (zbog §2):** ako WAV/FLAC nema ANLZ beatgrid, UI mora pokazati
`NO BEATGRID` i onemogućiti sync/quantize/beat-jump kontrole za taj deck, a ne
prikazati lažni BPM.

File browser filter proširiti na `.mp3/.wav/.wave/.flac`. Track info:
`FORMAT / sample rate / channels / bits / duration` (duration iz ANLZ ili
`total_frames/sample_rate`).

---

## 16. CMake

```cmake
idf_component_register(
    SRCS
        "audio_engine.c" "audio_mixer.c" "audio_output_mixer.c" "audio_resampler.c"
        "audio_format.c" "audio_decoder.c" "audio_wav_decoder.c" "audio_flac_decoder.c"
        # ... postojeći: audio_eq.c, audio_filter.c, audio_fw_*.c, audio_pcm_ring.c,
        #     audio_delay_fx.c, audio_pad_fx.c, audio_smart_cfx.c, audio_diag.c,
        #     audio_output_timing.c
    INCLUDE_DIRS "include" "third_party"
    REQUIRES esp_common fatfs media_io_gate      # media_io_gate je stvarna ovisnost
)
```

> Uskladiti s postojećim `CMakeLists.txt` (ne prepisati postojeće SRCS/REQUIRES).

---

## 17. Kconfig (ISPRAVLJENO)

```text
menu "Audio format support"

config AUDIO_ENABLE_WAV
    bool "Enable WAV playback"
    default y

config AUDIO_ENABLE_FLAC
    bool "Enable FLAC playback"
    default y

config AUDIO_WAV_ENABLE_24BIT
    bool "Enable WAV 24-bit PCM support"
    default n

config AUDIO_WAV_ENABLE_FLOAT
    bool "Enable WAV 32-bit float support"
    default n

endmenu
```

> **Izbačeno `CONFIG_AUDIO_FIXED_OUTPUT_SAMPLE_RATE`.** Projekt dinamički
> rekonfigurira I2S clock na sample rate učitane pjesme i mixa 44.1/48 kroz
> per-deck resampler. Fiksni output rate proturječi implementaciji.

---

## 18. Test plan

**WAV**: 44k1/48k stereo 16-bit, 44k1 mono 16-bit, 96k stereo 16-bit → rade;
24-bit/float → odbijaju u Fazi 1.
**FLAC**: 44k1/48k stereo 16-bit, mono 16-bit → rade; 44k1/96k stereo 24-bit →
rade s `>>8` konverzijom; 5.1 → odbija.

**Runtime**: load/play/pause/resume/seek/stop po formatu; miješani decks
(MP3+WAV, WAV+FLAC, FLAC+MP3) uz crossfader/PFL/pitch. **Novo:** za WAV/FLAC bez
ANLZ potvrditi da sync/quantize/beat-jump graciozno izostanu i da UI pokaže
`NO BEATGRID`, a MAIN (PCM5102A RCA) + cue (FLX4 USB slušalice) rade istovremeno.

**Performance**: `heap_caps_get_free_size(MALLOC_CAP_SPIRAM/INTERNAL)`,
`uxTaskGetStackHighWaterMark`, ring fill, decode/output underrun brojači, file
read latencija. Ako FLAC ne stiže real-time: veći ring, viši decode prioritet,
veći read blok, niži sample rate za test.

**Host testovi**: dodati `tests/audio_wav_decoder/`, `tests/audio_flac_decoder/`,
`tests/audio_format/` u istom stilu kao postojeći (`run_p4_host_tests.ps1`).
Format detekcija i WAV parser se testiraju na PC-u bez hardvera.

---

## 19. Redoslijed implementacije

1. `audio_format.{h,c}` + host test detekcije.
2. `audio_decoder.{h,c}` interface.
3. `audio_wav_decoder.{c}` — open + log metadata (kroz `media_io_gate`), host test parsera.
4. WAV → `audio_pcm_ring` (ispravni push/backpressure producer) → output.
5. `dr_flac` (managed/vendor) + `audio_flac_decoder.{c}` — open + log metadata.
6. FLAC → ring → output; real-time test.
7. **Riješiti ANLZ/beatgrid put (§2): Scenarij A pa B.**
8. UI: file browser filter, format info, `NO BEATGRID` oznaka, greške.
9. (Kasnije) MP3 refaktor u `audio_mp3_decoder.c` — isti interface.
10. (Faza 3) chunked cache / seek index / waveform bez ANLZ.

---

## 20. Ključna arhitekturna pravila

1. `audio_engine.c` bez `if format == ...` hrpe — detekcija centralizirana.
2. Svi formati završe u istom `audio_pcm_ring` (SPSC, po-frame `push`).
3. Output/resampler/mixer ne znaju format.
4. File IO uvijek kroz `media_io_gate`.
5. `pvbr_400` je MP3-only; WAV/FLAC seek je native, prosljeđuje se `NULL`.
6. Duration/beatgrid: ANLZ ako postoji, inače duration iz dekodera i **bez**
   beatgrida (sync/quantize/beat-jump degradiraju, UI to pokaže).
7. Izlaz: PCM5102A RCA MAIN + FLX4 USB slušalice cue (ES8311 ne postoji).

---

## 21. Sažetak ispravaka vs original

| # | Original (netočno) | V2 (ispravljeno) |
|---|---|---|
| 1 | `audio_pcm_ring_write(...portMAX_DELAY)` | `audio_pcm_ring_push(l,r)->bool` + backpressure |
| 2 | `audio_engine_load_for_deck(int, const char*)` | `audio_engine_deck_load(uint8_t, const char*, pvbr_400, duration_ms)` |
| 3 | load = format-agnostičan `fopen` | ANLZ/beatgrid/PVBR sprega eksplicitno riješena (§2) |
| 4 | goli `fopen`/`fread` | kroz `media_io_gate_begin/end` |
| 5 | "full preload" pretpostavka | uklopljeno u loader+decoder split / file-backed producer |
| 6 | `CONFIG_AUDIO_FIXED_OUTPUT_SAMPLE_RATE` | izbačeno (dinamički I2S clock + resampler) |
| 7 | izlaz "PCM5102A / ES8311" | PCM5102A RCA MAIN + FLX4 USB cue; ES8311 izbačen |
| 8 | WAV `fread` po frameu | bufferirano čitanje (512-frame blokovi) |
| 9 | ručno vendoranje dr_flac | managed component ili vendor uz verzijski seek oprez |
