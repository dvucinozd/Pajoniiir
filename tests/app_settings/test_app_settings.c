/*
 * Backlight persistence is the one setting driven by a continuous control, so it
 * is the one setting whose write pattern matters: committing every slider sample
 * would put an NVS write per VALUE_CHANGED on the LVGL task.
 *
 * These tests run the real app_settings.c against the fake RTOS from
 * tests/support/rtos, so the debounce worker is a real task body entered under
 * test control, and against a counting NVS fake, so "how many writes reached
 * flash" is directly observable.
 */
#include "app_settings.h"
#include "fake_rtos.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

test_nvs_state_t g_test_nvs;

static int s_failures;
static int s_checks;
#define CHECK(x) do {                                                    \
    s_checks++;                                                          \
    if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); s_failures++; } \
} while (0)

/* The worker body is an infinite loop, so the test drives one debounce cycle
 * through the seam instead of entering the task body. Same code path the worker
 * runs, minus the loop that a scheduler would otherwise have to break out of. */
void app_settings_test_run_debounce_cycle(void);
void app_settings_test_reset(void);

static void run_worker_once(void)
{
    app_settings_test_run_debounce_cycle();
}

/* app_settings.c keeps file statics that outlive fake_rtos_reset(), so both
 * sides have to be returned to their power-on state between cases. */
static void reset_all(void)
{
    fake_rtos_reset();
    test_nvs_reset();
    app_settings_test_reset();
}

static void test_setter_before_worker_persists_synchronously(void)
{
    printf("== a set before the worker exists still reaches NVS ==\n");
    reset_all();

    /* No app_settings_init(), so no worker: the value must not be lost. */
    app_settings_set_backlight(42u);
    CHECK(g_test_nvs.set_u8_calls == 1u);
    CHECK(g_test_nvs.commit_calls == 1u);
    CHECK(g_test_nvs.last_value == 42u);
    CHECK(app_settings_get().backlight_pct == 42u);
}

static void test_failed_write_is_not_published(void)
{
    printf("== a failed NVS write does not update the published snapshot ==\n");
    reset_all();

    app_settings_set_backlight(30u);
    CHECK(app_settings_get().backlight_pct == 30u);

    g_test_nvs.fail_next_set = 1;
    app_settings_set_backlight(90u);
    /* The write was attempted and refused, so the snapshot must still read the
     * last value that actually made it to flash. */
    CHECK(app_settings_get().backlight_pct == 30u);
}

static void test_value_is_clamped(void)
{
    printf("== out-of-range percentages clamp to 100 ==\n");
    reset_all();
    app_settings_set_backlight(200u);
    CHECK(g_test_nvs.last_value == 100u);
    CHECK(app_settings_get().backlight_pct == 100u);
}

static void test_slider_burst_produces_one_write(void)
{
    printf("== a slider burst commits once, with the final value ==\n");
    reset_all();
    CHECK(app_settings_start_backlight_worker() == ESP_OK);
    CHECK(fake_rtos_task_exists("settings_nvs"));

    const uint32_t writes_before = g_test_nvs.set_u8_calls;

    /* Twenty VALUE_CHANGED events, as a drag across the slider produces. */
    for (uint8_t pct = 40u; pct < 60u; ++pct) {
        app_settings_set_backlight(pct);
    }

    /* None of them wrote: the worker owns the commit now. */
    CHECK(g_test_nvs.set_u8_calls == writes_before);

    run_worker_once();

    /* Exactly one write, carrying the value the slider settled on. */
    CHECK(g_test_nvs.set_u8_calls == writes_before + 1u);
    CHECK(g_test_nvs.last_value == 59u);
    CHECK(app_settings_get().backlight_pct == 59u);
}

static void test_worker_start_is_idempotent(void)
{
    printf("== starting the worker twice does not create a second one ==\n");
    reset_all();
    CHECK(app_settings_start_backlight_worker() == ESP_OK);
    CHECK(fake_rtos_live_tasks() == 1u);
    CHECK(app_settings_start_backlight_worker() == ESP_OK);
    CHECK(fake_rtos_live_tasks() == 1u);
}

static void test_init_loads_then_starts_the_worker(void)
{
    printf("== init loads the stored value and arms the worker with it ==\n");
    reset_all();
    g_test_nvs.stored_backlight = 77u;
    g_test_nvs.has_stored_backlight = 1;

    CHECK(app_settings_init() == ESP_OK);
    CHECK(app_settings_get().backlight_pct == 77u);
    CHECK(fake_rtos_task_exists("settings_nvs"));

    /* The worker was primed with the loaded value, so a debounce cycle with no
     * slider activity must not rewrite a different one. */
    const uint32_t writes_before = g_test_nvs.set_u8_calls;
    run_worker_once();
    CHECK(g_test_nvs.set_u8_calls == writes_before);
    CHECK(app_settings_get().backlight_pct == 77u);
}

static void test_other_settings_still_write_immediately(void)
{
    printf("== only backlight is debounced; other setters commit at once ==\n");
    reset_all();
    CHECK(app_settings_start_backlight_worker() == ESP_OK);

    const uint32_t writes_before = g_test_nvs.set_u8_calls;
    app_settings_set_cue_mode(1u);
    CHECK(g_test_nvs.set_u8_calls == writes_before + 1u);
    CHECK(app_settings_get().cue_mode == 1u);

    app_settings_set_wifi_remote(1u);
    CHECK(g_test_nvs.set_u8_calls == writes_before + 2u);
    CHECK(app_settings_get().wifi_remote == 1u);

    /* A repeat of an unchanged value writes nothing at all. */
    app_settings_set_cue_mode(1u);
    CHECK(g_test_nvs.set_u8_calls == writes_before + 2u);
}

int main(void)
{
    test_setter_before_worker_persists_synchronously();
    test_failed_write_is_not_published();
    test_value_is_clamped();
    test_slider_burst_produces_one_write();
    test_worker_start_is_idempotent();
    test_init_loads_then_starts_the_worker();
    test_other_settings_still_write_immediately();

    printf("TESTS_RUN=%d\n", s_checks);
    if (s_failures == 0) {
        puts("app_settings tests passed");
        return 0;
    }
    printf("app_settings tests FAILED (%d)\n", s_failures);
    return 1;
}
