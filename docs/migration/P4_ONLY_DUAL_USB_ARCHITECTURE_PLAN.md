# Pajoniiir ESP32-P4-only Dual USB Host Migration Plan

Status: P4-only source transition implemented; hardware acceptance open
Created: 2026-08-04
Working branch: `feat/p4-dual-usb-host`
Baseline commit: `c7a7cc59cf344ae4ed213ad0c0c3770f69077fca`
Target SDK: ESP-IDF `6.0.2`
USB component baseline: `espressif/usb 1.5.0`, `espressif/usb_host_msc 1.2.0`

Software progress (2026-08-27): P4 directly owns controller MIDI/audio,
semantic events, profiles and LEDs. Active P4 CMake, CI, UI/web status and OTA
packaging no longer include S3 UART, heartbeat, debug AP, profile transfer,
monitor PCM or firmware reporting. `firmware/control-board-s3` remains only as
historical/reference source.

Hardware progress: an initial wired smoke at `fc03034` passed feature-image
flash/boot and USB0 Rekordbox-library loading with 191 tracks on 2026-08-09. A
later bench run enumerated USB0 storage and USB1 FLX4 together, but the power
path remained electrically unqualified and produced brownouts. Direct-path
functional acceptance and the combined matrix remain open; see
[`../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md`](../validation/P4_DUAL_USB_INITIAL_WIRED_SMOKE_20260809.md).

## 1. Decision Summary

Pajoniiir remains one project and one repository. The existing two-processor
architecture is migrated incrementally to an ESP32-P4-only product.

The final hardware contract is:

| ESP32-P4 USB controller | Fixed role | USB classes | Product responsibility |
| --- | --- | --- | --- |
| USB0 High-Speed | Rekordbox media | Mass Storage Class | Mount the music drive at `/usb`, read Rekordbox data, analysis files and audio |
| USB1 Full-Speed | DJ controller | USB MIDI initially; USB Audio in a later gated phase | Receive controls, send LED feedback and provide FLX4 headphone/cue output |

The project does **not** become a dual-MSC system. Only USB0 owns a filesystem.
USB1 owns the composite DJ-controller device and is never mounted through VFS.

The ESP32-S3 firmware remains available as the known-good reference and rollback
path until every P4-only acceptance gate in this document passes. The stable
`master` architecture is not removed or rewritten during the early migration
phases.

## 2. Current Repository Findings

The plan is based on the current implementation, not on a clean-sheet design.

### 2.1 P4 storage currently owns the complete USB Host Library

`firmware/main-deck-p4/components/usb_storage/usb_storage.c` currently:

- calls `usb_host_install()` itself;
- starts the USB library daemon task;
- installs the MSC class driver;
- owns one storage session, one MSC handle and one mount;
- mounts one device at `/usb`;
- ignores a second MSC device;
- performs global root-port power cycles during recovery;
- publishes mount state to the application callback.

This component cannot remain the owner of the complete USB library once USB MIDI
is added to the P4. Host ownership must first be extracted into a central P4
component.

### 2.2 S3 MIDI is already a mature reusable implementation

`firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c` already
contains:

- USB-MIDI packet parsing;
- safe configuration-descriptor walking;
- MIDIStreaming IN/OUT endpoint discovery;
- asynchronous USB host client handling;
- nonblocking device teardown;
- MIDI IN transfer rearming;
- bounded MIDI OUT queueing;
- LED/VU output prioritisation;
- connection-state publication;
- FLX4 USB Audio interface configuration hooks.

This logic should be moved and adapted, not rewritten without evidence.

### 2.3 S3 `app_main` owns translation and reconciliation logic

`firmware/control-board-s3/main/app_main.c` currently contains application logic
that must become a P4-local component:

- raw MIDI to semantic-event translation;
- high-rate control coalescing;
- relative jog delta accumulation;
- held-state reconciliation;
- controller reconnect snapshot replay;
- dynamic controller-profile runtime selection;
- forwarding semantic events through the UART `control_link`.

The USB client and the semantic translator must be separated during migration so
that transport ownership does not leak into deck/application ownership.

### 2.4 P4 already owns product state

The P4 already owns:

- both deck states;
- playback and decode;
- mixer, EQ, filters and Beat FX;
- Rekordbox library;
- UI and touchscreen;
- master and cue mix generation;
- controller-profile registry;
- authoritative LED state.

The migration therefore replaces transports and removes a processor boundary. It
does not redesign the deck, playback or UI model.

### 2.5 P4 `control_link` mixes semantic vocabulary and wire transport

The current P4 `control_link` header contains both:

- reusable semantic IDs, deck IDs, LED IDs and event structures;
- S3-specific UART framing, heartbeat, bulk transfer and firmware-report types.

These concerns must be split. P4-local controller events must not pass through a
fake UART frame merely to reuse existing handlers.

### 2.6 FLX4 USB headphones are part of the required final product

The current S3 does more than MIDI. It also hosts the FLX4 USB Audio playback
interface and receives P4 cue PCM over the P4-to-S3 audio link. Removing the S3
without replacing this path would regress the validated headphone/cue function.

For that reason S3 retirement is blocked until direct P4-to-FLX4 USB Audio has
passed its own hardware acceptance phase.

## 3. Target Architecture

```text
                            ESP32-P4

                   +-------------------------+
USB0 HS ---------->|                         |
Rekordbox drive    |  usb_host_manager       |
                   |  - one Host Library     |
                   |  - USB0 + USB1 enabled  |
                   |  - daemon ownership     |
                   +-----------+-------------+
                               |
               +---------------+----------------+
               |                                |
               v                                v
       usb_storage client              controller_usb_host client
       - MSC only                      - FLX4 composite device
       - fixed USB0 route              - fixed USB1 route
       - FATFS `/usb`                  - MIDI IN / MIDI OUT
       - media recovery                - later USB Audio OUT
               |                                |
               v                                v
       Rekordbox library               controller_runtime
       audio sources                   - profile selection
                                       - MIDI map
                                       - coalescing
                                       - held-state reconcile
                                       - semantic events
                                                |
                                                v
                                       controller_event_bus
                                                |
                                                v
                    deck_core / mixer / UI / LED snapshot
                                                |
                                                v
                                       MIDI LED feedback

Cue/headphone PCM after USB Audio migration:

audio_output_mixer -> bounded cue PCM ring -> FLX4 USB Audio OUT endpoint
```

## 4. Non-Negotiable Architecture Rules

### 4.1 One USB Host Library instance

The P4 installs the USB Host Library exactly once. MSC, MIDI and USB Audio are
clients of that instance. No class component may independently call
`usb_host_install()` or own a second daemon.

### 4.2 Fixed port roles

Initial product firmware uses fixed routing:

```text
USB0 / root port 0 / HS -> MSC Rekordbox drive
USB1 / root port 1 / FS -> DJ controller composite device
```

Automatic class-based port swapping, hubs and arbitrary multi-device routing are
out of scope for the first production implementation.

### 4.3 Class validation remains mandatory

A physical port assignment is not sufficient by itself. Before claiming a
device, the firmware must also validate:

- MSC interface for USB0;
- Audio class + MIDIStreaming subclass and valid IN/OUT endpoints for USB1;
- expected interface and endpoint bounds;
- descriptor lengths before every read;
- supported transfer types and nonzero endpoint MPS.

### 4.4 No global USB recovery after both ports are active

The current storage recovery calls the global root-port power API. Once both
ports are enabled, a storage recovery operation must not disconnect the DJ
controller, and a controller recovery operation must not interrupt playback.

A per-port recovery facility is therefore an implementation prerequisite. If
public `esp-usb` APIs do not provide the required root-port identity and power
control, the project uses the `dvucinozd/esp-usb` fork with the smallest additive
API extension possible. Private HCD/HAL rewrites are forbidden unless a measured
upstream defect proves they are necessary.

### 4.5 USB callbacks never perform application work

USB callbacks may only:

- update bounded state;
- submit/rearm transfers;
- publish a level-state change;
- wake the owning task.

They must not mount FATFS, scan Rekordbox data, update LVGL, execute deck actions
or wait for unrelated locks.

### 4.6 Local semantic events replace UART frames

USB MIDI input is translated into the same semantic meaning currently used by
the P4, but it is sent through a local typed API or queue. The P4 must not encode
and decode a synthetic `0xA5` frame internally.

### 4.7 P4 remains authoritative

MIDI and controller profiles describe input/output transport. They do not own
playback state. Deck, mixer, effects, library and LED state remain P4-owned.

### 4.8 S3 deletion is the final step

The following are not deleted until the final retirement gate:

- `firmware/control-board-s3/`;
- P4/S3 protocol documentation;
- S3 host tests;
- S3 OTA and validation records;
- existing known-good release references.

They may be marked transitional on the migration branch, but they remain
available for comparison and rollback.

## 5. Proposed P4 Component Boundaries

### 5.1 New `usb_host_manager`

Proposed path:

```text
firmware/main-deck-p4/components/usb_host_manager/
  CMakeLists.txt
  idf_component.yml
  include/usb_host_manager.h
  usb_host_manager.c
  usb_host_port_state.c
  usb_host_recovery.c
```

Responsibilities:

- install `usb_host_config_t` once with both P4 USB peripherals enabled;
- own the daemon task and `usb_host_lib_handle_events()` loop;
- expose readiness to class clients;
- track root-port state and diagnostics;
- coordinate shutdown only for tests;
- provide per-port recovery requests;
- prevent simultaneous conflicting recovery operations;
- expose counters for connect, disconnect, reset and recovery failures.

It does **not** parse MIDI, mount media or call application callbacks.

### 5.2 Refactored `usb_storage`

The existing component remains responsible for one Rekordbox media source but no
longer owns the Host Library.

Responsibilities after refactor:

- install and own the MSC class client;
- accept only a device routed to USB0;
- maintain one storage session;
- mount at `/usb`;
- preserve current stable-connect and mount-retry logic;
- preserve FAT32/exFAT and superfloppy/MBR/GPT support;
- request USB0-only recovery through `usb_host_manager`;
- maintain the media I/O gate;
- publish mount/unmount level state.

The mount path stays `/usb`; `/usb0` is unnecessary because there is only one
filesystem source.

### 5.3 New `controller_usb_host`

Proposed path:

```text
firmware/main-deck-p4/components/controller_usb_host/
  CMakeLists.txt
  include/controller_usb_host.h
  controller_usb_host.c
  usb_midi_packet.c
  usb_midi_descriptors.c
  usb_midi_tx_queue.c
```

Initial source is the validated S3 `flx4_midi_host` implementation, split into
portable units.

Responsibilities:

- register one asynchronous Host Library client;
- accept only the controller routed to USB1;
- open and validate the composite device;
- claim MIDIStreaming interface;
- manage MIDI IN and OUT transfers;
- publish raw MIDI messages and connection level state;
- accept bounded outgoing 4-byte USB-MIDI packets;
- expose descriptor identity and capabilities;
- request USB1-only recovery;
- coordinate later MIDI and Audio interface claims on the same device.

It does not map MIDI values to deck actions.

### 5.4 New or moved `controller_runtime`

Proposed path:

```text
firmware/main-deck-p4/components/controller_runtime/
  CMakeLists.txt
  include/controller_runtime.h
  controller_runtime.c
  controller_event_bus.c
  control_state_reconciler.c
  flx4_map.c
  flx4_led_midi.c
```

Responsibilities:

- select built-in or data-driven controller profile;
- map raw MIDI to typed semantic events;
- coalesce high-rate absolute values;
- accumulate relative jog deltas;
- retain FIFO semantics for commands;
- reconcile held controls and force releases on disconnect;
- replay known state after reconnect;
- convert P4 LED snapshots into MIDI OUT packets;
- expose profile/controller status to the UI and web API.

### 5.5 Semantic API split

The existing shared meanings should move out of the UART component into a
transport-neutral component, for example:

```text
firmware/common/controller_events/
  include/controller_events.h
  controller_events.c
```

Move or define here:

- `ctrl_deck_t`;
- semantic control IDs;
- LED IDs;
- typed controller-event structures;
- pad and extended-action encoding helpers;
- pure mapping/validation helpers.

Keep in the legacy `control_link` component only:

- `0xA5` framing;
- sequence/checksum handling;
- `0xA6` bulk frames;
- S3 heartbeat and firmware reports;
- UART send/receive tasks.

During transition both paths may consume the same transport-neutral semantic
header.

### 5.6 Direct P4 `flx4_usb_audio`

The existing S3 component should later be ported with minimal algorithmic
change:

- retain descriptor parsing;
- retain interface/alternate-setting selection;
- retain packet sizing and sample-rate handling;
- retain bounded isochronous transfer scheduling;
- replace `p4_audio_link` as the PCM producer;
- consume the P4 cue mix directly through a bounded ring or callback owned by the
  audio engine/output service;
- share the same USB device and client lifecycle as MIDI where required.

The MIDI-only phases must not pretend that S3 removal is complete while this path
is absent.

## 6. Task and Concurrency Model

Recommended production tasks:

| Task | Owner | Primary responsibility |
| --- | --- | --- |
| `usb_hostd` | `usb_host_manager` | Host Library events for both controllers |
| existing MSC background task | MSC component | MSC class-driver events |
| `usb_store` | `usb_storage` | mount/unmount and storage reconciliation |
| `usb_midi` | `controller_usb_host` | USB client events, MIDI IN/OUT and device teardown |
| `ctrl_runtime` | `controller_runtime` | MIDI mapping, coalescing and semantic dispatch |
| existing deck task/queue | `deck_core` | product control effects |
| existing audio tasks | `audio_engine` | decode, mix and output |
| later USB-audio pump | controller/audio boundary | FLX4 cue PCM transfer scheduling |

Concurrency rules:

1. Device connect/disconnect is maintained as level state plus generation/epoch,
   not as a single lossy queue edge.
2. Each class owns its handles and transfer objects.
3. Host manager owns only global/per-port Host Library state.
4. Disconnect blocks new media reads immediately, then waits for existing gate
   holders before unmount.
5. Controller disconnect forces all held controls to released state.
6. MIDI OUT teardown first stops producers, then drains or resets the bounded
   queue, then frees completed transfers.
7. UI updates are scheduled into LVGL context.
8. Audio USB callbacks never acquire broad deck or UI locks.
9. No blocking filesystem work occurs in USB daemon/client callbacks.
10. Per-port recovery requests are serialized by the host manager but affect
    only their requested port.

## 7. Dependency Strategy

### 7.1 First prove the official baseline

The initial dual-controller spike uses the currently pinned versions:

```yaml
espressif/usb: "==1.5.0"
espressif/usb_host_msc: "==1.2.0"
idf: "==6.0.2"
```

The spike determines whether the public APIs provide enough information and
control for:

- enabling USB0 and USB1 simultaneously;
- identifying the root port of a discovered device;
- routing MSC and MIDI to fixed ports;
- recovering one port without resetting the other.

### 7.2 Use the fork only for proven API gaps

If the current public API is insufficient, update both USB dependencies to exact
commit references from `dvucinozd/esp-usb`. Both the core USB component and MSC
component must resolve from the same fork commit.

Expected minimal additions, if required:

- expose `root_port_id`/`port_id` in relevant public device information;
- include `port_id` in MSC connect/disconnect or device-info data;
- add a per-port root-power/recovery API while preserving the existing global
  API;
- add dual-port and independent-recovery tests.

Do not fork private internals merely to mirror the original task document. The
existing upstream dual-host implementation remains the base.

### 7.3 Lock reproducibility

After dependency changes:

- regenerate `dependencies.lock`;
- commit the lock file;
- record the exact fork commit in this document or a validation record;
- never track a moving branch for release builds.

## 8. Migration Phases

## Phase 0 — Baseline and migration branch

Status: complete for branch and plan creation.

Tasks:

- create `feat/p4-dual-usb-host` from current `master`;
- record baseline commit and component versions;
- create this implementation plan;
- leave `master` unchanged;
- avoid opening a production merge PR before the spike is proven.

Exit criteria:

- branch exists at the recorded baseline;
- plan is committed;
- no firmware behavior has changed.

## Phase 1 — Minimal P4 dual-host hardware spike

Goal: prove both P4 USB controllers can operate simultaneously before moving
application behavior.

Create a deliberately small P4 test configuration or isolated test application
that:

- installs one USB Host Library instance;
- enables USB0 and USB1 through `peripheral_map`;
- runs one daemon task;
- registers an MSC client and a raw asynchronous USB client;
- enumerates the Rekordbox stick on USB0;
- enumerates the FLX4 or another USB-MIDI fixture on USB1;
- logs device address, speed, VID/PID, root port and interfaces;
- reads raw MIDI packets without mapping them;
- confirms the MSC device remains accessible while MIDI traffic is active.

No playback, UI, LED or profile changes are required in this phase.

Exit criteria:

- both devices enumerate in one boot;
- each device is assigned to the intended physical port;
- MSC read test and MIDI input operate concurrently for at least 30 minutes;
- removing either device does not unregister the other;
- reconnecting either device restores only that class;
- measured logs identify any missing public API before a fork change is made.

Stop condition:

- do not begin broad Pajoniiir refactoring if simultaneous enumeration is not
  stable.

## Phase 2 — Central P4 USB host ownership

Goal: make the production P4 app use one Host Library owner without changing
visible product behavior.

Tasks:

- add `usb_host_manager`;
- move `usb_host_install()` and daemon loop out of `usb_storage.c`;
- configure both controllers but initially allow only MSC product behavior;
- make `usb_storage` wait for host-manager readiness;
- preserve `/usb`, storage callbacks and current retry behavior;
- add host-manager diagnostics;
- add host tests for manager state and recovery arbitration.

Exit criteria:

- P4 builds under ESP-IDF 6.0.2;
- Rekordbox mount/library/playback behavior matches the branch baseline;
- USB Host Library is installed exactly once;
- storage no longer calls `usb_host_install()`;
- no S3 behavior has yet been removed.

## Phase 3 — P4 raw USB-MIDI host

Goal: move the USB transport from S3 to P4 without changing deck actions.

Tasks:

- port descriptor, packet and endpoint helpers from `flx4_midi_host`;
- add `controller_usb_host` as a second client of `usb_host_manager`;
- route and accept only USB1;
- open the controller and claim MIDIStreaming interface;
- receive and log USB-MIDI packets;
- implement bounded MIDI OUT transfer support but keep product LEDs disabled;
- publish controller connected/disconnected level state and descriptor identity;
- retain S3 code in the repository for comparison.

Exit criteria:

- FLX4 MIDI IN remains active for 30 minutes;
- all required controls produce the same raw USB-MIDI bytes as the known S3 path;
- MIDI transfer rearming survives transient submit failures;
- disconnect teardown completes without stale transfer or handle;
- USB0 storage and playback remain active throughout controller activity.

## Phase 4 — P4-local semantic controller runtime

Goal: remove UART from the controller input path while preserving behavior.

Tasks:

- create the transport-neutral `controller_events` API;
- port `flx4_map` and controller-profile runtime logic to P4;
- move coalescing, jog accumulation, held-state reconciliation and snapshot replay
  out of S3 `app_main` into `controller_runtime`;
- publish typed semantic events directly to the existing deck control queue or a
  narrow adapter;
- compare P4-local semantic traces against S3-generated traces;
- retain existing deck/mixer handlers unchanged where possible.

Compatibility rule:

- semantic IDs may remain numerically stable during migration;
- UART frame structures and heartbeat state must not become required by the
  local path.

Exit criteria:

- Play, Cue, Load, Browse, jog, pitch, mixer, PFL, pads and effects behave the
  same through the P4-local path;
- high-rate controls remain bounded;
- discrete commands retain FIFO meaning;
- held controls recover to the correct final level after queue pressure;
- disconnect forces all held states released;
- no synthetic UART encode/decode exists in the local path.

## Phase 5 — P4 MIDI OUT and LED feedback

Goal: reproduce the validated physical LED behavior directly from P4.

Tasks:

- port/reuse FLX4 LED MIDI mapping;
- connect P4-owned LED snapshot generation to `controller_runtime`;
- send bounded USB-MIDI OUT packets through `controller_usb_host`;
- preserve VU packet drop-priority behavior under queue pressure;
- send a complete authoritative snapshot after controller reconnect;
- test P4 reboot while the controller remains powered;
- test controller reboot/reconnect while P4 playback continues.

Exit criteria:

- all currently accepted LEDs match the S3 reference behavior;
- reconnect restores current deck/mixer LED state without operator action;
- stale queued LED packets do not leak into a later connection;
- MIDI IN latency remains acceptable while LED/VU traffic is active;
- USB0 playback remains uninterrupted.

## Phase 6 — Local controller-profile activation

Goal: eliminate S3 profile transfer while preserving data-driven controller
support.

Tasks:

- keep profile discovery and storage on P4/microSD;
- make P4 `controller_profile_manager` activate profiles locally;
- adapt `controller_profile_runtime` to the P4 component boundary;
- preserve built-in FLX4 map as fallback;
- preserve current profile validation, version and CRC checks;
- define compatibility handling for existing S3CP profile files;
- remove runtime dependence on `0xA6` profile transfer only after local activation
  is hardware-tested;
- update web status fields to report local activation state.

Exit criteria:

- built-in FLX4 map works;
- installed FLX4 profile works;
- independent generic profile fixture passes host tests;
- corrupt or incompatible profiles fail safely and use fallback;
- no controller-profile functionality requires an online S3.

## Phase 7 — Direct P4-to-FLX4 USB Audio

Goal: replace the P4-to-S3 PCM link and S3 USB Audio streamer without losing cue
headphones.

Tasks:

- port `flx4_uac_descriptors`, `flx4_uac_packetizer` and `flx4_usb_audio` to P4;
- coordinate MIDI and Audio interface claims on the same FLX4 device lifecycle;
- expose a bounded cue PCM producer from the P4 audio engine/output mixer;
- remove the I2S transport framing from the active path;
- support the validated FLX4 channel mapping for headphones;
- preserve 44.1 kHz and 48 kHz operation;
- handle sample-rate changes without ring drift or overrun;
- collect queue/ring/isochronous transfer diagnostics;
- keep master PCM5102A output active simultaneously.

Resource review required before implementation:

- internal DMA-capable memory;
- transfer-buffer count and size;
- USB callback CPU time;
- interaction with DSI/PSRAM bandwidth;
- audio output late and underrun counters;
- effect of removing the monitor I2S link on I2S ownership.

Exit criteria:

- MAIN output and FLX4 cue/headphone output run simultaneously;
- Deck 1/Deck 2 PFL and headphone mix behave correctly;
- 44.1 kHz and 48 kHz tracks pass;
- mixed-rate track transitions recover cleanly;
- no sustained USB Audio ring drift, overrun or underrun;
- MIDI input and LED feedback remain responsive during audio streaming;
- 30-minute minimum audio/controller/storage soak passes;
- direct path subjectively matches the accepted S3 cue path.

S3 retirement remains blocked until this phase passes.

## Phase 8 — Independent per-port recovery and fault containment

Goal: ensure either external USB device can fail without taking down the other.

Tasks:

- add or integrate root-port identity for class events;
- replace global storage power cycles with USB0-only recovery;
- add USB1-only controller recovery;
- serialize recovery state within `usb_host_manager`;
- add escalating retry cadence per port;
- expose last failure and recovery counters;
- ensure class handles are closed before port reset;
- explicitly reject global suspend/resume as a recovery substitute.

Fault matrix:

| Fault | Required result |
| --- | --- |
| Remove Rekordbox stick during playback | Audio stops safely, library clears, FLX4 remains connected |
| Reinsert Rekordbox stick | `/usb` remounts, library reloads, FLX4 remains active |
| Remove FLX4 during playback | Playback and UI continue, held controls release, controller state becomes offline |
| Reinsert FLX4 | MIDI/LED and later USB Audio restore without remounting storage |
| USB0 enumeration failure | USB0-only retries; USB1 remains active |
| USB1 enumeration failure | USB1-only retries; USB0 reads continue |
| Malformed MIDI descriptor | Controller rejected safely; storage unaffected |
| Mount failure | Bounded storage retries; controller unaffected |
| P4 software reset with devices powered | Both classes recover without manual unplug where hardware permits |

Exit criteria:

- every matrix row passes repeatedly;
- no recovery API power-cycles both ports;
- no stale handles, callbacks or transfers remain after disconnect;
- diagnostics identify the affected port and class.

## Phase 9 — Host tests, CI and instrumentation

Goal: move coverage from a two-target transport design to a one-target product
without losing regression depth.

Required host/unit tests:

- USB-MIDI CIN parsing;
- descriptor bounds and endpoint selection;
- malformed/truncated descriptor rejection;
- MIDI OUT queue capacity and VU prioritisation;
- connection-state level semantics;
- held-state reconciliation;
- high-rate coalescing and relative jog accumulation;
- semantic mapping equivalence;
- LED packet mapping and reconnect snapshot;
- profile parsing, fallback and local activation;
- USB host-manager state transitions;
- per-port recovery arbitration;
- MSC session epochs and disconnect races;
- media I/O gate behavior.

Required integration/hardware evidence:

- exact firmware commit and dependency lock;
- boot log showing both P4 controllers enabled;
- root-port assignment for both devices;
- simultaneous MSC read + MIDI input;
- simultaneous playback + MIDI + LED traffic;
- simultaneous playback + MIDI + LED + USB Audio;
- disconnect/reconnect matrix;
- heap, DMA heap, PSRAM and task-stack high-water marks;
- audio late/underrun, USB transfer and recovery counters;
- 30-minute minimum soak before S3 retirement;
- longer performance soak before release merge.

CI transition status (2026-08-27):

1. Active CI builds P4 and runs the P4 USB/controller host suite.
2. The S3 runner remains available for manual audits of retained historical
   source but is no longer an active product gate.
3. Release packaging produces only the P4 image and bundle.
4. Group coherent changes to prevent unnecessary Actions queue pressure.

## Phase 10 — Remove active P4/S3 transport dependencies

Goal: make the product operate without S3 while retaining history.

Source status (2026-08-27): **implemented.** The default P4 build uses local
semantic injection and direct USB LED/audio output. UART control-link sources,
heartbeat/debug UI, profile transfer, firmware reporting and monitor PCM are no
longer registered in the active image. Hardware exit criteria remain open.

Tasks after Phases 1–9 pass:

- stop starting P4 UART `control_link` in product configuration;
- remove S3 heartbeat from deck/controller online status;
- replace S3 debug AP controls with P4 equivalents or remove the feature;
- stop sending descriptor/profile/LED data through `0xA6`/`0xA5`;
- stop starting `monitor_pcm_link` and `p4_audio_link` in product configuration;
- update service-log events from “control link” to “controller USB” semantics;
- update `/api/status` and UI status labels;
- keep legacy components temporarily buildable behind explicit migration-only
  configuration if useful for A/B testing.

Exit criteria:

- the complete product boots and operates with no S3 connected;
- no user-visible feature requires S3;
- disabling all legacy link options produces a clean P4 build;
- startup logs contain no “waiting for S3 events” state.

## Phase 11 — S3 retirement and repository cleanup

Goal: remove obsolete production code only after the P4-only path is accepted.

Source/release status (2026-08-27): **partially implemented.** S3 is retired
from active build, CI, package, web/UI and current architecture documentation.
The source directory and protocol/validation history are intentionally retained
until P4-only hardware acceptance and rollback documentation are complete.

Prerequisites:

- dual enumeration accepted;
- Rekordbox mount/playback accepted;
- full MIDI controls accepted;
- LED and reconnect accepted;
- direct USB Audio cue accepted;
- independent recovery accepted;
- disconnect/reconnect matrix accepted;
- minimum 30-minute combined soak accepted;
- release rollback path documented.

Cleanup tasks:

- [x] remove `firmware/control-board-s3/` from active product builds;
- [x] remove dual-target OTA packaging and S3 firmware reporting;
- [x] remove active UART control and P4-to-S3 PCM wiring requirements;
- [x] update root README and architecture diagrams;
- [x] update hardware wiring;
- [x] update development plan and risk register;
- [x] update OTA, startup and validation documentation;
- archive protocol documents as historical where useful instead of erasing design
  history;
- [x] update CI to one firmware target;
- [x] update release packaging and version metadata;
- retain tagged/branch history for the final known-good P4/S3 release.

Exit criteria:

- repository documentation describes only the P4 production hardware;
- one P4 firmware image and one OTA lifecycle remain;
- no active CMake dependency references removed S3 components;
- all P4-only tests and hardware gates pass from a clean checkout.

## 9. Detailed Acceptance Matrix

### 9.1 Enumeration and routing

- [ ] Boot with neither device connected.
- [x] Boot with only USB0 storage connected — initial 191-track library smoke
  passed at `fc03034` on 2026-08-09; playback and reconnect are separate rows.
- [ ] Boot with only USB1 controller connected.
- [ ] Boot with both connected.
- [ ] Connect storage first, then controller.
- [ ] Connect controller first, then storage.
- [ ] Confirm storage is accepted only on USB0.
- [ ] Confirm controller is accepted only on USB1.
- [ ] Confirm wrong-port devices are rejected with a clear diagnostic.

### 9.2 Storage and playback

- [ ] FAT32 superfloppy.
- [ ] FAT32 MBR.
- [ ] exFAT MBR.
- [ ] exFAT GPT.
- [ ] Rekordbox database and analysis load.
- [ ] MP3 playback.
- [ ] WAV playback with valid fixture.
- [ ] FLAC playback with valid fixture.
- [ ] Dual-deck playback.
- [ ] Remove storage during one-deck playback.
- [ ] Remove storage during two-deck playback.
- [ ] Reinsert and reload without reboot.

### 9.3 MIDI input

- [ ] Play/Cue both decks.
- [ ] Load/Browse.
- [ ] Jog bend, scratch and search.
- [ ] Pitch 14-bit handling.
- [ ] Channel faders and crossfader.
- [ ] Trim, EQ and filter.
- [ ] PFL/headphone controls.
- [ ] Pad modes and shifted actions.
- [ ] Beat FX and Smart controls.
- [ ] Sustained jog/analog event flood.
- [ ] Disconnect while controls are held.

### 9.4 MIDI output and state recovery

- [ ] Transport LEDs.
- [ ] PFL and Master Cue LEDs.
- [ ] Sync and loop LEDs.
- [ ] Pad-mode LEDs.
- [ ] Hot Cue and performance-pad LEDs.
- [ ] Beat FX and Smart LEDs.
- [ ] VU feedback under queue pressure.
- [ ] Controller reconnect snapshot.
- [ ] P4 reboot while FLX4 remains powered.

### 9.5 USB Audio cue

- [ ] 44.1 kHz.
- [ ] 48 kHz.
- [ ] MAIN and cue simultaneously.
- [ ] Deck 1 PFL.
- [ ] Deck 2 PFL.
- [ ] Both PFL.
- [ ] Headphone mix control.
- [ ] Track sample-rate change.
- [ ] MIDI input during cue stream.
- [ ] LED/VU output during cue stream.
- [ ] Storage read/cache miss during cue stream.
- [ ] Controller disconnect/reconnect during active playback.

### 9.6 Reliability

- [ ] 100 storage connect/disconnect cycles without reboot.
- [ ] 100 controller connect/disconnect cycles without reboot.
- [ ] Alternating per-port recovery cycles.
- [ ] 30-minute minimum full-load soak.
- [ ] Multi-hour performance soak before release.
- [ ] No panic, watchdog reset or brownout.
- [ ] No permanent USB transfer stall.
- [ ] No stale device handle or interface claim.
- [ ] No sustained audio underrun or USB Audio ring drift.
- [ ] No unbounded queue, heap or task-stack growth.

## 10. Risk Register for This Migration

| Risk | Impact | Required mitigation |
| --- | --- | --- |
| Global root-port recovery resets both devices | Playback/controller interruption | Per-port recovery before production dual-host enablement |
| FLX4 composite interface claim conflict | MIDI or Audio fails to open | One coordinated device lifecycle and explicit interface ownership |
| USB Audio + MIDI scheduling load | Input latency or audio underrun | Bounded transfers, diagnostics, DMA-memory budget and hardware soak |
| P4 CPU/interrupt pressure | UI/audio regressions | Measure task/ISR load; avoid callback work; preserve core separation |
| DSI/PSRAM/USB contention | Display flash or audio instability | Hardware instrumentation under simultaneous full load |
| Removal of S3 audio link changes I2S ownership | Startup or output regression | Audit P4 I2S init order and remove link only after direct UAC works |
| Profile format remains S3-specific | Compatibility break | Local compatibility parser and explicit format migration policy |
| Controller disconnect leaves held state | Stuck controls | Level-state reconciler and forced release snapshot |
| Storage disconnect races reads | use-after-unmount | Preserve media I/O gate and epoch-based ownership |
| USB device power draw | Brownout/resets | Hardware VBUS/current validation and reset-reason logging |
| Large rewrite hides regressions | Long unstable branch | Small gated phases and A/B comparison against S3 reference |
| Early S3 deletion removes rollback | Difficult recovery | Do not delete S3 before all retirement prerequisites pass |

## 11. Explicitly Out of Scope for the Initial Migration

- a second MSC/filesystem device;
- dynamic role swapping between USB0 and USB1;
- USB hub support as a product requirement;
- multiple simultaneous DJ controllers;
- arbitrary USB Audio devices;
- USB Audio capture/input from the controller;
- replacing the existing deck, mixer, UI or Rekordbox architecture;
- renumbering every existing semantic ID during transport migration;
- private HCD/HAL rewrites without a reproduced defect;
- merging the branch into `master` before hardware acceptance.

## 12. Recommended Commit Sequence

To keep reviews understandable and avoid excessive GitHub Actions queueing, use
coherent commits rather than many tiny pushes:

1. `docs(migration): define P4-only dual USB architecture plan`
2. `feat(p4-usb): add dual-controller host manager spike`
3. `refactor(p4-usb): move MSC under shared host manager`
4. `feat(p4-midi): port raw USB MIDI host`
5. `feat(p4-control): route local semantic controller events`
6. `feat(p4-led): add direct MIDI feedback and reconnect snapshot`
7. `feat(p4-profiles): activate controller profiles locally`
8. `feat(p4-uac): stream cue mix directly to FLX4 USB Audio`
9. `fix(p4-usb): add independent per-port recovery`
10. `test(p4-only): close dual USB acceptance matrix`
11. `refactor(p4-only): retire active S3 transports`
12. `docs(p4-only): update product architecture and release process`

Each implementation commit must either build independently or clearly state that
it is part of an isolated spike not enabled in the default product
configuration.

## 13. First Executable Engineering Step

The next change after this document is **not** copying the entire S3 firmware
into the P4 build.

The next step is a minimal P4 dual-host spike that proves:

1. one Host Library instance can enable both P4 controllers;
2. USB0 can enumerate/read the Rekordbox MSC device;
3. USB1 can enumerate/read raw MIDI from the FLX4;
4. removing either device leaves the other operational;
5. the public USB API exposes enough port identity and recovery control, or a
   narrowly scoped `esp-usb` fork change is required.

Only after that evidence exists should the production `usb_storage` owner be
refactored and the S3 MIDI stack be migrated.
