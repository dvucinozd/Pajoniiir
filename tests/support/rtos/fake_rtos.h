#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Deterministic single-threaded model of the FreeRTOS primitives the firmware
 * depends on. Nothing pre-empts. A registered task body runs only when the test
 * pumps it, and the tick only moves when the test moves it, so a run is
 * reproducible and a failure is reproducible with it.
 *
 * The model deliberately does not emulate priorities or true concurrency: those
 * belong to hardware soak testing. What it does emulate exactly is the part
 * firmware logic actually reasons about — queue ordering and capacity, blocking
 * with a timeout, semaphore counts, recursive mutex ownership, and per-task
 * notification slots.
 */

/* ── control ─────────────────────────────────────────────────────────────── */

/* Drop all queues, semaphores, tasks and notifications; reset the tick to 0. */
void fake_rtos_reset(void);

/* Advance the tick (and the esp_timer timeline with it). Any task blocked with
 * a timeout that expires becomes runnable. */
void fake_rtos_advance_ticks(uint32_t ticks);

uint32_t fake_rtos_tick_count(void);

/* Enter every live task body once per round, in creation order, for at most
 * `max_rounds` rounds; stops early once no task is alive. Returns how many task
 * bodies were entered in total.
 *
 * Bodies are called directly on the caller's stack — there is no context switch
 * — so a task written as an infinite `for(;;)` loop must not be pumped. Drive
 * those with fake_rtos_run_task_once() against a body compiled to make one pass,
 * or restructure the body so the loop condition is testable. A body that calls
 * vTaskDelete(NULL) marks itself dead and is skipped from the next round, which
 * is how a worker that exits terminates the pump. */
uint32_t fake_rtos_pump(uint32_t max_rounds);

/* Enter one specific task body once, by the name it was created with. Returns
 * false when no such task exists. */
bool fake_rtos_run_task_once(const char *name);

/* Number of live objects, for leak assertions in teardown. */
size_t fake_rtos_live_queues(void);
size_t fake_rtos_live_semaphores(void);
size_t fake_rtos_live_tasks(void);

/* True when a task with this name was created and has not been deleted. */
bool fake_rtos_task_exists(const char *name);
