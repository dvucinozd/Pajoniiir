#include "fake_rtos.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_RTOS_MAX_TASKS 16

/* ── tick and critical sections ──────────────────────────────────────────── */

static TickType_t s_ticks;
static int        s_critical_depth;

void fake_rtos_critical_enter(void) { s_critical_depth++; }
void fake_rtos_critical_exit(void)  { if (s_critical_depth > 0) s_critical_depth--; }
int  fake_rtos_critical_depth(void) { return s_critical_depth; }

TickType_t xTaskGetTickCount(void) { return s_ticks; }
void       taskYIELD(void) { }

uint32_t fake_rtos_tick_count(void) { return s_ticks; }

void fake_rtos_advance_ticks(uint32_t ticks)
{
    s_ticks += ticks;
}

void vTaskDelay(TickType_t ticks)
{
    s_ticks += ticks;
}

/* ── queues ──────────────────────────────────────────────────────────────── */

struct fake_queue {
    unsigned capacity;
    unsigned item_size;
    unsigned count;
    unsigned head;      /* index of the oldest item */
    int      rejections;
    unsigned char *storage;
    struct fake_queue *next;
};

static struct fake_queue *s_queues;

static unsigned char *queue_slot(struct fake_queue *q, unsigned logical_index)
{
    const unsigned slot = (q->head + logical_index) % q->capacity;
    return q->storage + (size_t)slot * q->item_size;
}

QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{
    if (length == 0u || item_size == 0u) return NULL;
    struct fake_queue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->storage = calloc(length, item_size);
    if (!q->storage) {
        free(q);
        return NULL;
    }
    q->capacity = length;
    q->item_size = item_size;
    q->next = s_queues;
    s_queues = q;
    return q;
}

void vQueueDelete(QueueHandle_t queue)
{
    if (!queue) return;
    struct fake_queue **link = &s_queues;
    while (*link && *link != queue) link = &(*link)->next;
    if (*link) *link = queue->next;
    free(queue->storage);
    free(queue);
}

/* No scheduler exists, so a wait cannot be satisfied by another task running
 * meanwhile: a full queue fails immediately whatever the timeout. This is
 * deliberate — it turns an unbounded portMAX_DELAY wait into a visible test
 * failure rather than a hang. */
int xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    (void)ticks;
    if (!queue || !item) return pdFALSE;
    if (queue->count == queue->capacity) {
        queue->rejections++;
        return pdFALSE;
    }
    memcpy(queue_slot(queue, queue->count), item, queue->item_size);
    queue->count++;
    return pdTRUE;
}

int xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    return xQueueSendToBack(queue, item, ticks);
}

int xQueueSendToFront(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    (void)ticks;
    if (!queue || !item) return pdFALSE;
    if (queue->count == queue->capacity) {
        queue->rejections++;
        return pdFALSE;
    }
    queue->head = (queue->head + queue->capacity - 1u) % queue->capacity;
    memcpy(queue_slot(queue, 0u), item, queue->item_size);
    queue->count++;
    return pdTRUE;
}

int xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    if (!queue || !item) return pdFALSE;
    if (queue->count == 0u) {
        return xQueueSendToBack(queue, item, 0u);
    }
    memcpy(queue_slot(queue, queue->count - 1u), item, queue->item_size);
    return pdTRUE;
}

int xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    (void)ticks;
    if (!queue || !item || queue->count == 0u) return pdFALSE;
    memcpy(item, queue_slot(queue, 0u), queue->item_size);
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    return pdTRUE;
}

int xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticks)
{
    (void)ticks;
    if (!queue || !item || queue->count == 0u) return pdFALSE;
    memcpy(item, queue_slot(queue, 0u), queue->item_size);
    return pdTRUE;
}

unsigned uxQueueMessagesWaiting(QueueHandle_t queue)
{
    return queue ? queue->count : 0u;
}

unsigned uxQueueSpacesAvailable(QueueHandle_t queue)
{
    return queue ? (queue->capacity - queue->count) : 0u;
}

void xQueueReset(QueueHandle_t queue)
{
    if (!queue) return;
    queue->count = 0u;
    queue->head = 0u;
    queue->rejections = 0;
}

int fake_queue_send_rejections(QueueHandle_t queue)
{
    return queue ? queue->rejections : 0;
}

/* ── semaphores ──────────────────────────────────────────────────────────── */

struct fake_semaphore {
    unsigned count;
    unsigned max;
    unsigned recursion;
    int      recursive;
    struct fake_semaphore *next;
};

static struct fake_semaphore *s_semaphores;

static SemaphoreHandle_t semaphore_create(unsigned max, unsigned initial, int recursive)
{
    struct fake_semaphore *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->max = max;
    s->count = initial;
    s->recursive = recursive;
    s->next = s_semaphores;
    s_semaphores = s;
    return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)          { return semaphore_create(1u, 1u, 0); }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return semaphore_create(1u, 1u, 1); }
SemaphoreHandle_t xSemaphoreCreateBinary(void)         { return semaphore_create(1u, 0u, 0); }

SemaphoreHandle_t xSemaphoreCreateCounting(unsigned max, unsigned initial)
{
    return semaphore_create(max, initial, 0);
}

void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    if (!sem) return;
    struct fake_semaphore **link = &s_semaphores;
    while (*link && *link != sem) link = &(*link)->next;
    if (*link) *link = sem->next;
    free(sem);
}

int xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    (void)ticks;
    if (!sem || sem->count == 0u) return pdFALSE;
    sem->count--;
    return pdTRUE;
}

int xSemaphoreGive(SemaphoreHandle_t sem)
{
    if (!sem || sem->count >= sem->max) return pdFALSE;
    sem->count++;
    return pdTRUE;
}

int xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks)
{
    if (!sem) return pdFALSE;
    if (sem->recursion > 0u) {
        sem->recursion++;
        return pdTRUE;
    }
    if (!xSemaphoreTake(sem, ticks)) return pdFALSE;
    sem->recursion = 1u;
    return pdTRUE;
}

int xSemaphoreGiveRecursive(SemaphoreHandle_t sem)
{
    if (!sem || sem->recursion == 0u) return pdFALSE;
    sem->recursion--;
    if (sem->recursion == 0u) return xSemaphoreGive(sem);
    return pdTRUE;
}

unsigned uxSemaphoreGetCount(SemaphoreHandle_t sem)
{
    return sem ? sem->count : 0u;
}

unsigned fake_semaphore_recursion_depth(SemaphoreHandle_t sem)
{
    return sem ? sem->recursion : 0u;
}

/* ── tasks and notifications ─────────────────────────────────────────────── */

struct fake_task {
    void (*body)(void *);
    void *arg;
    char  name[24];
    unsigned notifications;
    int   alive;
};

static struct fake_task  s_tasks[FAKE_RTOS_MAX_TASKS];
static unsigned          s_task_count;
static struct fake_task *s_current_task;

int xTaskCreate(void (*body)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, TaskHandle_t *handle)
{
    (void)stack;
    (void)priority;
    if (s_task_count >= FAKE_RTOS_MAX_TASKS) return pdFAIL;
    struct fake_task *t = &s_tasks[s_task_count++];
    memset(t, 0, sizeof(*t));
    t->body = body;
    t->arg = arg;
    t->alive = 1;
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "");
    if (handle) *handle = t;
    return pdPASS;
}

int xTaskCreatePinnedToCore(void (*body)(void *), const char *name, unsigned stack,
                            void *arg, unsigned priority, TaskHandle_t *handle,
                            int core)
{
    (void)core;
    return xTaskCreate(body, name, stack, arg, priority, handle);
}

void vTaskDelete(TaskHandle_t task)
{
    struct fake_task *t = task ? task : s_current_task;
    if (t) t->alive = 0;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    /* Outside a pumped body the caller is "the test", modelled as task slot 0 so
     * a notification aimed at the current task still has somewhere to land. */
    if (s_current_task) return s_current_task;
    if (s_task_count == 0u) {
        (void)xTaskCreate(NULL, "test_main", 0u, NULL, 0u, NULL);
    }
    return &s_tasks[0];
}

unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 1024u;
}

unsigned ulTaskNotifyTake(int clear_on_exit, TickType_t ticks)
{
    (void)ticks;
    struct fake_task *t = xTaskGetCurrentTaskHandle();
    if (!t || t->notifications == 0u) return 0u;
    const unsigned observed = t->notifications;
    t->notifications = clear_on_exit ? 0u : (t->notifications - 1u);
    return observed;
}

int xTaskNotifyGive(TaskHandle_t task)
{
    if (!task) return pdFAIL;
    task->notifications++;
    return pdPASS;
}

unsigned fake_task_notification_count(TaskHandle_t task)
{
    return task ? task->notifications : 0u;
}

bool fake_rtos_task_exists(const char *name)
{
    if (!name) return false;
    for (unsigned i = 0; i < s_task_count; ++i) {
        if (s_tasks[i].alive && strcmp(s_tasks[i].name, name) == 0) return true;
    }
    return false;
}

bool fake_rtos_run_task_once(const char *name)
{
    if (!name) return false;
    for (unsigned i = 0; i < s_task_count; ++i) {
        struct fake_task *t = &s_tasks[i];
        if (!t->alive || !t->body || strcmp(t->name, name) != 0) continue;
        struct fake_task *previous = s_current_task;
        s_current_task = t;
        t->body(t->arg);
        s_current_task = previous;
        return true;
    }
    return false;
}

uint32_t fake_rtos_pump(uint32_t max_rounds)
{
    uint32_t entered = 0u;
    for (uint32_t round = 0u; round < max_rounds; ++round) {
        int ran_any = 0;
        for (unsigned i = 0; i < s_task_count; ++i) {
            struct fake_task *t = &s_tasks[i];
            if (!t->alive || !t->body) continue;
            struct fake_task *previous = s_current_task;
            s_current_task = t;
            t->body(t->arg);
            s_current_task = previous;
            entered++;
            ran_any = 1;
        }
        if (!ran_any) break;
    }
    return entered;
}

size_t fake_rtos_live_tasks(void)
{
    size_t live = 0;
    for (unsigned i = 0; i < s_task_count; ++i) {
        if (s_tasks[i].alive) live++;
    }
    return live;
}

size_t fake_rtos_live_queues(void)
{
    size_t n = 0;
    for (struct fake_queue *q = s_queues; q; q = q->next) n++;
    return n;
}

size_t fake_rtos_live_semaphores(void)
{
    size_t n = 0;
    for (struct fake_semaphore *s = s_semaphores; s; s = s->next) n++;
    return n;
}

void fake_rtos_reset(void)
{
    while (s_queues) vQueueDelete(s_queues);
    while (s_semaphores) vSemaphoreDelete(s_semaphores);
    memset(s_tasks, 0, sizeof(s_tasks));
    s_task_count = 0u;
    s_current_task = NULL;
    s_ticks = 0u;
    s_critical_depth = 0;
}
