# Architecture

## High-Level Flow

```text
Pioneer DDJ-FLX4
    |
    | USB MIDI
    v
ESP32-S3 control-board-s3
    |
    | 0xA5 UART semantic events
    v
ESP32-P4 main-deck-p4
    |
    | dual decode + mixer
    v
Master output + cue/PFL output
```

## ESP32-S3 Responsibilities

The S3 firmware should be renamed conceptually from panel controller to FLX4
controller, but the directory can stay `control-board-s3` until code movement
is justified.

Responsibilities:

- enumerate the DDJ-FLX4 through ESP-IDF USB host;
- parse class-compliant USB MIDI packets;
- translate MIDI status/midino/value triples into semantic events;
- combine MSB/LSB pairs for 14-bit controls before forwarding when practical;
- send heartbeat frames to the P4;
- receive P4 LED/state frames;
- emit FLX4 MIDI LED feedback.

The S3 must not:

- decide whether a deck is playing;
- calculate audio position;
- apply mixer gains itself;
- invent current/next track state.

## ESP32-P4 Responsibilities

The P4 remains authoritative for performance state.

Responsibilities:

- load Rekordbox tracks and analysis data from USB media;
- own two `deck_core` state instances;
- own the mixer state: channel faders, crossfader, pregain, EQ/filter when
  implemented, cue/PFL selection;
- decode audio and write master/cue buffers to hardware;
- render UI state;
- send LED feedback commands to the S3.

Current P4 audio ownership rule:

- each deck owns its own engine state, preload buffer, decode runtime, PCM ring,
  resampler, lifecycle status, and last-error state;
- one shared firmware output service owns codec open/close and consumes both
  deck PCM rings through the output mixer;
- stopping or reloading one deck must not close the codec while another deck is
  still loaded or playing;
- USB removal uses `audio_engine_stop_all()` to tear down both decks and the
  shared output service.

## Data Flow

1. FLX4 sends a MIDI event, for example `0x90 0x0B 0x7F` for Deck 1 Play.
2. S3 MIDI host parses it and maps it to a deck-aware event.
3. S3 sends a `control_link` UART frame to P4.
4. P4 updates the target deck state through `deck_core`.
5. P4 calls audio engine/mixer APIs.
6. P4 sends LED feedback back over `control_link`.
7. S3 emits the matching MIDI LED message to the FLX4.

## Main Code Surfaces

Inherited files that will be touched early:

- `firmware/control-board-s3/main/app_main.c`
- `firmware/control-board-s3/components/control_link/`
- `firmware/control-board-s3/components/midi_compat/`
- `firmware/main-deck-p4/components/control_link/`
- `firmware/main-deck-p4/components/deck_core/`
- `firmware/main-deck-p4/components/audio_engine/`
- `firmware/main-deck-p4/components/ui/`

Expected new S3 component:

```text
firmware/control-board-s3/components/flx4_midi_host/
  include/flx4_midi_host.h
  include/flx4_map.h
  flx4_midi_host.c
```

Expected P4 additions:

```text
firmware/main-deck-p4/components/mixer/
  include/mixer.h
  mixer.c
```

## State Ownership

The most important architectural rule is simple: MIDI is an input transport, not
state. The FLX4 mapping file tells us what the controller sends and accepts; it
does not define the playback model.

`deck_core` and the audio engine on P4 own the actual state.
