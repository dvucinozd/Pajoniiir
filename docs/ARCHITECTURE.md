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
- coalesce high-rate jog and analog values locally so stale motion does not
  flood the UART queue;
- publish DDJ-FLX4 USB connection/disconnection state to the P4;
- send heartbeat frames to the P4;
- receive P4 LED/state frames;
- emit FLX4 MIDI LED feedback using XML/official-list output addresses.

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
- own controller behavior that changes playback state, including Hot Cue,
  Loop, Beat Jump, Tempo Range, and the current one-shot Beat Sync/paused-deck
  phase-align behavior;
- decode audio and write master/cue buffers to hardware;
- render UI state;
- send LED feedback commands to the S3;
- force a P4-owned LED snapshot after an FLX4 reconnect so physical LEDs
  recover without S3 owning playback state.

Current P4 audio ownership rule:

- each deck owns its own engine state, preload buffer, decode runtime, PCM ring,
  resampler, lifecycle status, and last-error state;
- one shared firmware output service owns codec open/close and consumes both
  deck PCM rings through the output mixer;
- the LVGL task is pinned to CPU1, while the P4 audio loader, decode, and shared
  output tasks are pinned to CPU0 so UI rendering and real-time audio do not
  share the same core;
- when `CONFIG_BSP_PCM5102A_MAIN_OUT` is enabled, the shared output service
  reconfigures the PCM5102A I2S1 clock to the loaded track sample rate before
  starting playback; the ES8311 monitor path and PCM5102A main path must stay
  sample-rate aligned;
- the audio engine exposes a non-boosting software master trim scalar
  (`0.0–1.0`, default `1.0`) before the output mixer/limiter path. The P4
  Settings screen exposes it as a conservative preset cycle (`0 dB`, `-3 dB`,
  `-6 dB`) so limiter activity can be reduced without changing deck fader or
  crossfader semantics;
- limiter telemetry is accumulated in the audio mixer snapshot as cumulative
  limited sample counts, positive/negative overload counts, and peak pre-limit
  input. The P4 status indicator reports `CLIP n` only when the limited-sample
  counter increases, so normal transport status remains stable when no new
  clipping occurs;
- the shared output service relies on codec/I2S write pacing and does not add a
  second FreeRTOS delay after each output block;
- MP3 preload uses smaller read chunks while audio output is active, and MP3
  seek table construction publishes the finished table with a short lock so
  loader/index work cannot hold the audio engine mutex for the full scan;
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
8. If the FLX4 disconnects/reconnects, S3 publishes connection state and P4
   republishes the current P4-owned LED snapshot.

The MIDI map is not an authority for behavior. `docs/reference/Pioneer-DDJ-FLX4.midi.xml`
is the proven source for input status/midino values, and
`docs/reference/DDJ-FLX4_MIDI_message_List.md` is the additional official
reference for output LEDs and known XML/official-list conflicts. P4 behavior is
implemented explicitly in the owning P4 component.

Current S3 firmware modes:

- default DDJ-FLX4 host mode: USB MIDI host for raw logging, translator input,
  connection-state publication, and MIDI LED output;
- DDJ-FLX4 translator mode: raw MIDI input mapped to deck-aware `0xA5`
  semantic frames, enabled by `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4`;
- inherited CDJ panel compatibility mode: direct GPIO panel input plus TinyUSB
  MIDI device compatibility when `CONFIG_DDJ_FLX4_HOST_MODE` is disabled.

## Main Code Surfaces

Inherited files that will be touched early:

- `firmware/control-board-s3/main/app_main.c`
- `firmware/control-board-s3/components/control_link/`
- `firmware/control-board-s3/components/midi_compat/`
- `firmware/main-deck-p4/components/control_link/`
- `firmware/main-deck-p4/components/deck_core/`
- `firmware/main-deck-p4/components/audio_engine/`
- `firmware/main-deck-p4/components/ui/`

Current S3 FLX4 component:

```text
firmware/control-board-s3/components/flx4_midi_host/
  include/flx4_midi_host.h
  include/flx4_map.h
  flx4_midi_host.c
  flx4_map.c
```

Current P4 mixer/audio surfaces live in `audio_engine` helpers such as
`audio_output_mixer`, deck-local runtime/preload/task-context modules, and the
shared output service. A separate `mixer/` component is not currently required.

## State Ownership

The most important architectural rule is simple: MIDI is an input transport, not
state. The FLX4 mapping file tells us what the controller sends and accepts; it
does not define the playback model.

`deck_core` and the audio engine on P4 own the actual state.
