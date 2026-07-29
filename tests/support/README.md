# `tests/support` — shared host-test platform fakes

Firmware sources include ESP-IDF and FreeRTOS headers that do not exist on the
PC. Every host suite therefore needs some stand-in. Before this directory each
suite grew its own `stubs/` tree, three of them ended up with near-identical
copies of `FreeRTOS.h`, `semphr.h`, `esp_err.h` and friends, and one suite
(`beat_jump`) was already reaching sideways into another suite's stubs with
`-I../deck_core_dual/stubs`. This is the one place those live now.

## Two levels

### Level 1 — `stubs/` (compile-only)

No-op headers whose only job is to let firmware source compile and link on the
host. Semaphores always succeed, queues never deliver, tasks are never created.
Use this when the suite exercises *logic* that happens to sit behind an RTOS API
it does not care about — the large majority of suites.

```
"-I../support/stubs"
```

### Level 2 — `rtos/` (executable model)

A deterministic, single-threaded model of the FreeRTOS primitives the firmware
actually depends on: queues with real FIFO storage and capacity, counting and
binary semaphores, recursive mutexes with ownership, task notifications, and a
manually advanced tick. Nothing pre-empts; the test drives time and task
execution explicitly, so a run is reproducible.

Use this when the behaviour under test *is* the concurrency: a debounce worker,
a producer/consumer queue discipline, a barrier acknowledgement.

```
"-I../support/rtos", "-I../support/stubs", ... , "../support/rtos/fake_rtos.c"
```

`rtos/` must come first on the include path: it shadows `stubs/freertos/` with
the executable version while `esp_*` headers still resolve to level 1.

## Local overrides

A suite may still keep its own header when the semantics genuinely differ — put
`-Istubs` *before* `-I../support/stubs` and the local copy wins. Two cases exist
today, both deliberate:

- `deck_core_dual/stubs/esp_timer.h` — wall-clock derived, because those tests
  measure real elapsed behaviour rather than a synthetic timeline.
- `deck_core_dual/stubs/audio_engine.h` — a component API fake, not a platform
  fake; it belongs to that suite.

Anything else duplicated here is a bug: fix it in `support/` instead.
