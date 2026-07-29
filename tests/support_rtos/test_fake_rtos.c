/*
 * The fake RTOS is shared infrastructure: every suite built on it inherits its
 * bugs, and a fake that quietly lies produces green tests for broken firmware.
 * So it gets the same treatment as production code.
 */
#include "fake_rtos.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static int s_failures;
static int s_checks;
#define CHECK(x) do {                                                    \
    s_checks++;                                                          \
    if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); s_failures++; } \
} while (0)

static void test_queue_is_fifo_and_bounded(void)
{
    printf("== queue keeps FIFO order and a real capacity ==\n");
    fake_rtos_reset();
    QueueHandle_t q = xQueueCreate(3u, sizeof(int));
    CHECK(q != NULL);
    CHECK(uxQueueMessagesWaiting(q) == 0u);

    for (int i = 1; i <= 3; ++i) CHECK(xQueueSend(q, &i, 0u) == pdTRUE);
    CHECK(uxQueueMessagesWaiting(q) == 3u);
    CHECK(uxQueueSpacesAvailable(q) == 0u);

    /* Full queue rejects, and the rejection is observable. */
    int overflow = 4;
    CHECK(xQueueSend(q, &overflow, 0u) == pdFALSE);
    CHECK(fake_queue_send_rejections(q) == 1);

    /* An unbounded wait must fail rather than hang: there is no scheduler that
     * could drain the queue while this call blocks. */
    CHECK(xQueueSend(q, &overflow, portMAX_DELAY) == pdFALSE);

    int out = 0;
    for (int i = 1; i <= 3; ++i) {
        CHECK(xQueueReceive(q, &out, 0u) == pdTRUE);
        CHECK(out == i);
    }
    CHECK(xQueueReceive(q, &out, 0u) == pdFALSE);
    vQueueDelete(q);
    CHECK(fake_rtos_live_queues() == 0u);
}

static void test_queue_peek_front_and_overwrite(void)
{
    printf("== peek does not consume, send-to-front and overwrite behave ==\n");
    fake_rtos_reset();
    QueueHandle_t q = xQueueCreate(4u, sizeof(int));

    int a = 10, b = 20, out = 0;
    CHECK(xQueueSend(q, &a, 0u) == pdTRUE);
    CHECK(xQueuePeek(q, &out, 0u) == pdTRUE);
    CHECK(out == 10);
    CHECK(uxQueueMessagesWaiting(q) == 1u);   /* peek must not consume */

    CHECK(xQueueSendToFront(q, &b, 0u) == pdTRUE);
    CHECK(xQueueReceive(q, &out, 0u) == pdTRUE);
    CHECK(out == 20);                          /* jumped the queue */

    /* Overwrite replaces the newest item rather than appending. */
    int c = 30;
    CHECK(xQueueOverwrite(q, &c) == pdTRUE);
    CHECK(uxQueueMessagesWaiting(q) == 1u);
    CHECK(xQueueReceive(q, &out, 0u) == pdTRUE);
    CHECK(out == 30);

    /* Wrap-around: fill, drain and refill past the physical end of storage. */
    for (int i = 0; i < 4; ++i) CHECK(xQueueSend(q, &i, 0u) == pdTRUE);
    for (int i = 0; i < 3; ++i) CHECK(xQueueReceive(q, &out, 0u) == pdTRUE);
    int wrapped = 99;
    CHECK(xQueueSend(q, &wrapped, 0u) == pdTRUE);
    CHECK(xQueueReceive(q, &out, 0u) == pdTRUE);
    CHECK(out == 3);
    CHECK(xQueueReceive(q, &out, 0u) == pdTRUE);
    CHECK(out == 99);

    vQueueDelete(q);
}

static void test_semaphore_counts_are_real(void)
{
    printf("== binary, mutex and counting semaphores keep real counts ==\n");
    fake_rtos_reset();

    SemaphoreHandle_t binary = xSemaphoreCreateBinary();
    CHECK(uxSemaphoreGetCount(binary) == 0u);      /* starts empty */
    CHECK(xSemaphoreTake(binary, 0u) == pdFALSE);
    CHECK(xSemaphoreGive(binary) == pdTRUE);
    CHECK(xSemaphoreTake(binary, 0u) == pdTRUE);
    CHECK(xSemaphoreGive(binary) == pdTRUE);
    CHECK(xSemaphoreGive(binary) == pdFALSE);      /* cannot exceed max */

    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    CHECK(uxSemaphoreGetCount(mutex) == 1u);       /* starts available */
    CHECK(xSemaphoreTake(mutex, 0u) == pdTRUE);
    CHECK(xSemaphoreTake(mutex, 0u) == pdFALSE);   /* not recursive */
    CHECK(xSemaphoreGive(mutex) == pdTRUE);

    SemaphoreHandle_t counting = xSemaphoreCreateCounting(3u, 2u);
    CHECK(uxSemaphoreGetCount(counting) == 2u);
    CHECK(xSemaphoreTake(counting, 0u) == pdTRUE);
    CHECK(xSemaphoreTake(counting, 0u) == pdTRUE);
    CHECK(xSemaphoreTake(counting, 0u) == pdFALSE);

    vSemaphoreDelete(binary);
    vSemaphoreDelete(mutex);
    vSemaphoreDelete(counting);
    CHECK(fake_rtos_live_semaphores() == 0u);
}

static void test_recursive_mutex_tracks_depth(void)
{
    printf("== recursive mutex nests and only releases at depth zero ==\n");
    fake_rtos_reset();
    SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();

    CHECK(xSemaphoreTakeRecursive(m, 0u) == pdTRUE);
    CHECK(xSemaphoreTakeRecursive(m, 0u) == pdTRUE);
    CHECK(xSemaphoreTakeRecursive(m, 0u) == pdTRUE);
    CHECK(fake_semaphore_recursion_depth(m) == 3u);
    CHECK(uxSemaphoreGetCount(m) == 0u);           /* held throughout */

    CHECK(xSemaphoreGiveRecursive(m) == pdTRUE);
    CHECK(xSemaphoreGiveRecursive(m) == pdTRUE);
    CHECK(uxSemaphoreGetCount(m) == 0u);           /* still held at depth 1 */
    CHECK(xSemaphoreGiveRecursive(m) == pdTRUE);
    CHECK(fake_semaphore_recursion_depth(m) == 0u);
    CHECK(uxSemaphoreGetCount(m) == 1u);           /* released */

    CHECK(xSemaphoreGiveRecursive(m) == pdFALSE);  /* unpaired give */
    vSemaphoreDelete(m);
}

/* ── task and notification model ─────────────────────────────────────────── */

static int s_worker_entries;
static unsigned s_worker_observed_notifications;

static void counting_worker(void *arg)
{
    (void)arg;
    s_worker_entries++;
    s_worker_observed_notifications += ulTaskNotifyTake(1 /*clear*/, 0u);
}

static void self_deleting_worker(void *arg)
{
    (void)arg;
    s_worker_entries++;
    vTaskDelete(NULL);
}

static void test_tasks_run_only_when_pumped(void)
{
    printf("== task bodies run only when pumped, and self-delete ends them ==\n");
    fake_rtos_reset();
    s_worker_entries = 0;

    TaskHandle_t handle = NULL;
    CHECK(xTaskCreate(counting_worker, "counter", 2048u, NULL, 3u, &handle) == pdPASS);
    CHECK(handle != NULL);
    CHECK(fake_rtos_task_exists("counter"));
    CHECK(s_worker_entries == 0);                  /* creation does not run it */

    CHECK(fake_rtos_pump(1u) == 1u);
    CHECK(s_worker_entries == 1);
    CHECK(fake_rtos_pump(2u) == 2u);
    CHECK(s_worker_entries == 3);

    fake_rtos_reset();
    s_worker_entries = 0;
    CHECK(xTaskCreate(self_deleting_worker, "once", 2048u, NULL, 3u, NULL) == pdPASS);
    CHECK(fake_rtos_pump(5u) == 1u);               /* dead after the first pass */
    CHECK(s_worker_entries == 1);
    CHECK(!fake_rtos_task_exists("once"));
    CHECK(fake_rtos_live_tasks() == 0u);
}

static void test_notifications_are_per_task(void)
{
    printf("== notifications land per task and clear as FreeRTOS does ==\n");
    fake_rtos_reset();
    s_worker_entries = 0;
    s_worker_observed_notifications = 0u;

    TaskHandle_t a = NULL;
    TaskHandle_t b = NULL;
    CHECK(xTaskCreate(counting_worker, "a", 1024u, NULL, 1u, &a) == pdPASS);
    CHECK(xTaskCreate(counting_worker, "b", 1024u, NULL, 1u, &b) == pdPASS);

    /* A notification aimed at one task must not be visible to the other — this
     * is the property that catches a component signalling the wrong task. */
    CHECK(xTaskNotifyGive(a) == pdPASS);
    CHECK(xTaskNotifyGive(a) == pdPASS);
    CHECK(fake_task_notification_count(a) == 2u);
    CHECK(fake_task_notification_count(b) == 0u);

    CHECK(fake_rtos_run_task_once("a"));
    CHECK(s_worker_observed_notifications == 2u);
    CHECK(fake_task_notification_count(a) == 0u);  /* clear_on_exit zeroed it */

    CHECK(fake_rtos_run_task_once("b"));
    CHECK(s_worker_observed_notifications == 2u);  /* b had nothing pending */

    CHECK(!fake_rtos_run_task_once("nonexistent"));
}

static void test_tick_only_moves_under_test_control(void)
{
    printf("== the tick is driven by the test, not by the host clock ==\n");
    fake_rtos_reset();
    CHECK(xTaskGetTickCount() == 0u);
    CHECK(fake_rtos_tick_count() == 0u);

    fake_rtos_advance_ticks(50u);
    CHECK(xTaskGetTickCount() == 50u);

    vTaskDelay(pdMS_TO_TICKS(25u));
    CHECK(xTaskGetTickCount() == 75u);

    fake_rtos_reset();
    CHECK(xTaskGetTickCount() == 0u);
}

static void test_critical_sections_balance(void)
{
    printf("== critical section depth is observable for balance assertions ==\n");
    fake_rtos_reset();
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    CHECK(fake_rtos_critical_depth() == 0);
    portENTER_CRITICAL(&mux);
    CHECK(fake_rtos_critical_depth() == 1);
    portENTER_CRITICAL(&mux);
    CHECK(fake_rtos_critical_depth() == 2);
    portEXIT_CRITICAL(&mux);
    portEXIT_CRITICAL(&mux);
    CHECK(fake_rtos_critical_depth() == 0);
}

static void test_reset_releases_everything(void)
{
    printf("== reset drops every object so suites cannot leak into each other ==\n");
    fake_rtos_reset();
    (void)xQueueCreate(2u, sizeof(int));
    (void)xSemaphoreCreateMutex();
    (void)xTaskCreate(counting_worker, "leaky", 1024u, NULL, 1u, NULL);
    CHECK(fake_rtos_live_queues() == 1u);
    CHECK(fake_rtos_live_semaphores() == 1u);
    CHECK(fake_rtos_live_tasks() == 1u);

    fake_rtos_reset();
    CHECK(fake_rtos_live_queues() == 0u);
    CHECK(fake_rtos_live_semaphores() == 0u);
    CHECK(fake_rtos_live_tasks() == 0u);
}

int main(void)
{
    test_queue_is_fifo_and_bounded();
    test_queue_peek_front_and_overwrite();
    test_semaphore_counts_are_real();
    test_recursive_mutex_tracks_depth();
    test_tasks_run_only_when_pumped();
    test_notifications_are_per_task();
    test_tick_only_moves_under_test_control();
    test_critical_sections_balance();
    test_reset_releases_everything();

    printf("TESTS_RUN=%d\n", s_checks);
    if (s_failures == 0) {
        puts("fake RTOS model tests passed");
        return 0;
    }
    printf("fake RTOS model tests FAILED (%d)\n", s_failures);
    return 1;
}
