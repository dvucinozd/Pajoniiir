#include "audio_pcm_timeline.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE s_cursor_publish_mux = portMUX_INITIALIZER_UNLOCKED;
#define CURSOR_PUBLISH_ENTER() taskENTER_CRITICAL(&s_cursor_publish_mux)
#define CURSOR_PUBLISH_EXIT()  taskEXIT_CRITICAL(&s_cursor_publish_mux)
#else
#define CURSOR_PUBLISH_ENTER() do { } while (0)
#define CURSOR_PUBLISH_EXIT()  do { } while (0)
#endif

#if defined(ESP_PLATFORM) && defined(CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION) && CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION
#include <limits.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "service_log.h"

#define TIMELINE_QUALIFICATION_ITERATIONS 100u
#define TIMELINE_QUALIFICATION_CAPACITY   8u

static const char *const TAG = "pcm_timeline_qual";
static TaskHandle_t s_qualification_reader;
static TaskHandle_t s_qualification_writer;
static audio_pcm_timeline_t s_qualification_timeline;
static int16_t s_qualification_storage[TIMELINE_QUALIFICATION_CAPACITY * 2u];
static uint32_t s_qualification_hook_enabled;
static uint32_t s_qualification_measure_enabled;
static uint32_t s_qualification_reader_runs;
static uint32_t s_qualification_failures;
static uint32_t s_publish_measurements;
static uint32_t s_publish_max_us;

static bool qualification_hook_enabled(void)
{
    return __atomic_load_n(&s_qualification_hook_enabled,
                           __ATOMIC_ACQUIRE) != 0u;
}

static bool qualification_measure_enabled(void)
{
    return __atomic_load_n(&s_qualification_measure_enabled,
                           __ATOMIC_ACQUIRE) != 0u;
}

static bool qualification_target_version(const uint32_t *version)
{
    return version == &s_qualification_timeline.oldest_version ||
           version == &s_qualification_timeline.play_version ||
           version == &s_qualification_timeline.write_version;
}

static void qualification_fail(void)
{
    (void)__atomic_add_fetch(&s_qualification_failures, 1u,
                             __ATOMIC_RELAXED);
}

static void qualification_after_odd_store(const uint32_t *version)
{
    TaskHandle_t reader = __atomic_load_n(&s_qualification_reader,
                                           __ATOMIC_ACQUIRE);
    if (qualification_target_version(version) &&
        qualification_hook_enabled() && reader) {
        xTaskNotifyGive(reader);
    }
}

static void note_publish_duration(uint32_t elapsed_us)
{
    s_publish_measurements++;
    if (elapsed_us > s_publish_max_us) s_publish_max_us = elapsed_us;
}
#else
#define qualification_after_odd_store(version) do { (void)(version); } while (0)
#endif

static uint64_t cursor_load(const uint32_t *epoch,
                            const uint32_t *low,
                            const uint32_t *version)
{
    for (;;) {
        uint32_t before = __atomic_load_n(version, __ATOMIC_ACQUIRE);
        if ((before & 1u) != 0u) continue;
        uint32_t epoch_value = __atomic_load_n(epoch, __ATOMIC_RELAXED);
        uint32_t low_value = __atomic_load_n(low, __ATOMIC_ACQUIRE);
        uint32_t after = __atomic_load_n(version, __ATOMIC_ACQUIRE);
        if (before == after) {
            return ((uint64_t)epoch_value << 32) | low_value;
        }
    }
}

static void cursor_store_absolute(uint32_t *epoch,
                                  uint32_t *low,
                                  uint32_t *version,
                                  uint64_t value)
{
    /* A higher-priority reader on the same core must not preempt after the odd
     * version is published: it would spin forever while the writer cannot run.
     * This section is reached only on a 32-bit cursor wrap or an explicit
     * reposition, and contains four bounded atomic stores. */
#if defined(ESP_PLATFORM) && defined(CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION) && CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION
    const bool measure_publish = qualification_target_version(version) &&
                                 qualification_measure_enabled();
    int64_t started_us = 0;
    uint32_t elapsed_us = 0u;
#endif
    CURSOR_PUBLISH_ENTER();
#if defined(ESP_PLATFORM) && defined(CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION) && CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION
    if (measure_publish) started_us = esp_timer_get_time();
#endif
    (void)__atomic_add_fetch(version, 1u, __ATOMIC_ACQ_REL);
    qualification_after_odd_store(version);
    __atomic_store_n(epoch, (uint32_t)(value >> 32), __ATOMIC_RELAXED);
    __atomic_store_n(low, (uint32_t)value, __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(version, 1u, __ATOMIC_RELEASE);
#if defined(ESP_PLATFORM) && defined(CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION) && CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION
    if (measure_publish) {
        elapsed_us = (uint32_t)(esp_timer_get_time() - started_us);
    }
#endif
    CURSOR_PUBLISH_EXIT();
#if defined(ESP_PLATFORM) && defined(CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION) && CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION
    if (measure_publish) note_publish_duration(elapsed_us);
#endif
}

static void cursor_store_next(uint32_t *epoch,
                              uint32_t *low,
                              uint32_t *version,
                              uint32_t current)
{
    uint32_t next = current + 1u;
    if (next != 0u) {
        __atomic_store_n(low, next, __ATOMIC_RELEASE);
        return;
    }
    uint64_t absolute = cursor_load(epoch, low, version) + 1u;
    cursor_store_absolute(epoch, low, version, absolute);
}

void audio_pcm_timeline_init(audio_pcm_timeline_t *t, int16_t *storage,
                             uint32_t capacity_frames)
{
    if (!t) return;
    t->frames = storage;
    /* Modular low-word distance is unambiguous only while every retained span
     * is strictly below half the uint32_t sequence space. */
    t->capacity = capacity_frames < 0x80000000u ? capacity_frames : 0u;
    t->generation = 0u;
    audio_pcm_timeline_reset(t);
}

void audio_pcm_timeline_reset(audio_pcm_timeline_t *t)
{
    if (!t) return;
    t->oldest_seq = 0u;
    t->play_seq = 0u;
    t->write_seq = 0u;
    t->oldest_epoch = 0u;
    t->play_epoch = 0u;
    t->write_epoch = 0u;
    t->oldest_version = 0u;
    t->play_version = 0u;
    t->write_version = 0u;
    t->play_index = 0u;
    t->write_index = 0u;
    t->generation++;
    if (t->generation == 0u) t->generation = 1u;
}

bool audio_pcm_timeline_push(audio_pcm_timeline_t *t, int16_t left, int16_t right)
{
    if (!t || !t->frames || t->capacity == 0u) return false;

    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_RELAXED);
    uint32_t oldest_seq = __atomic_load_n(&t->oldest_seq, __ATOMIC_RELAXED);
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    uint32_t used = write_seq - oldest_seq;
    if (used >= t->capacity) {
        /* Never overwrite the next frame normal playback still needs. */
        if ((uint32_t)(play_seq - oldest_seq) == 0u) return false;
        cursor_store_next(&t->oldest_epoch, &t->oldest_seq,
                          &t->oldest_version, oldest_seq);
    }

    uint32_t index = t->write_index;
    t->frames[index * 2u] = left;
    t->frames[index * 2u + 1u] = right;
    if (++index >= t->capacity) index = 0u;
    t->write_index = index;
    cursor_store_next(&t->write_epoch, &t->write_seq,
                      &t->write_version, write_seq);
    return true;
}

bool audio_pcm_timeline_read(const audio_pcm_timeline_t *t, uint64_t seq,
                             audio_mixer_frame_t *out)
{
    if (!t || !t->frames || !out || t->capacity == 0u) {
        return false;
    }
    uint64_t oldest_seq = audio_pcm_timeline_oldest_seq(t);
    uint64_t write_seq = audio_pcm_timeline_write_seq(t);
    if (seq < oldest_seq || seq >= write_seq) {
        return false;
    }
    /* play_seq/play_index are owned by the same output task that performs
     * random key-lock reads. Use that stable physical anchor rather than the
     * producer-owned eviction cursor; the retained window is at most one
     * capacity wide, so at most one wrap correction is required. */
    uint64_t play_seq = audio_pcm_timeline_play_seq(t);
    int64_t index_from_play = (int64_t)t->play_index +
                              ((int64_t)seq - (int64_t)play_seq);
    if (index_from_play < 0) index_from_play += t->capacity;
    if (index_from_play >= t->capacity) index_from_play -= t->capacity;
    uint32_t index = (uint32_t)index_from_play;
    out->left = t->frames[index * 2u];
    out->right = t->frames[index * 2u + 1u];
    /* The producer may evict and overwrite this exact slot after our first
     * range check. Discard a possibly torn frame if its sequence expired while
     * it was being copied; callers already treat false as unavailable PCM. */
    return seq >= audio_pcm_timeline_oldest_seq(t);
}

bool audio_pcm_timeline_read_output_owner(const audio_pcm_timeline_t *t,
                                          uint64_t seq,
                                          audio_mixer_frame_t *out)
{
    if (!t || !t->frames || !out || t->capacity == 0u) return false;

    const uint32_t seq_low = (uint32_t)seq;
    const uint32_t oldest = __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
    const uint32_t write = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    const uint32_t retained = write - oldest;
    if ((uint32_t)(seq_low - oldest) >= retained) return false;

    /* play_seq/play_index are mutated only by this caller's output task. The
     * retained span is below 2^31, so the signed modular delta identifies the
     * same physical slot even while the low sequence word wraps. */
    const int32_t delta = (int32_t)(seq_low - t->play_seq);
    int64_t index = (int64_t)t->play_index + delta;
    if (index < 0) index += t->capacity;
    if (index >= t->capacity) index -= t->capacity;
    out->left = t->frames[(uint32_t)index * 2u];
    out->right = t->frames[(uint32_t)index * 2u + 1u];

    /* The producer can evict the copied slot concurrently. Refuse it if the
     * eviction cursor moved beyond seq while the two samples were copied. */
    const uint32_t oldest_after = __atomic_load_n(&t->oldest_seq,
                                                   __ATOMIC_ACQUIRE);
    return (uint32_t)(seq_low - oldest_after) < t->capacity;
}

bool audio_pcm_timeline_pop(audio_pcm_timeline_t *t, audio_mixer_frame_t *out)
{
    if (!t || !out) {
        return false;
    }
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_RELAXED);
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    /* Producer can evict only frames strictly before play_seq, therefore the
     * output owner never needs to load oldest_seq in this per-frame path. */
    if ((uint32_t)(write_seq - play_seq) == 0u) return false;
    uint32_t index = t->play_index;
    out->left = t->frames[index * 2u];
    out->right = t->frames[index * 2u + 1u];
    if (++index >= t->capacity) index = 0u;
    t->play_index = index;
    cursor_store_next(&t->play_epoch, &t->play_seq,
                      &t->play_version, play_seq);
    return true;
}

bool audio_pcm_timeline_set_playhead(audio_pcm_timeline_t *t, uint64_t seq)
{
    if (!t || t->capacity == 0u || seq < audio_pcm_timeline_oldest_seq(t) ||
        seq > audio_pcm_timeline_write_seq(t)) return false;
    uint64_t write_seq = audio_pcm_timeline_write_seq(t);
    uint64_t frames_back = write_seq - seq;
    if (frames_back > t->capacity) return false;
    uint32_t rewind = (uint32_t)frames_back;
    uint32_t index = t->write_index >= rewind
        ? t->write_index - rewind
        : t->write_index + t->capacity - rewind;
    t->play_index = index;
    cursor_store_absolute(&t->play_epoch, &t->play_seq,
                          &t->play_version, seq);
    return true;
}

bool audio_pcm_timeline_set_playhead_output_owner(audio_pcm_timeline_t *t,
                                                  uint64_t seq)
{
    if (!t || t->capacity == 0u) return false;
    const uint32_t seq_low = (uint32_t)seq;
    const uint32_t oldest = __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
    const uint32_t write = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    const uint32_t retained = write - oldest;
    if ((uint32_t)(seq_low - oldest) > retained) return false;

    const int32_t delta = (int32_t)(seq_low - t->play_seq);
    int64_t index = (int64_t)t->play_index + delta;
    if (index < 0) index += t->capacity;
    if (index >= t->capacity) index -= t->capacity;
    t->play_index = (uint32_t)index;

    const uint32_t epoch = (uint32_t)(seq >> 32);
    if (epoch == t->play_epoch) {
        __atomic_store_n(&t->play_seq, seq_low, __ATOMIC_RELEASE);
    } else {
        cursor_store_absolute(&t->play_epoch, &t->play_seq,
                              &t->play_version, seq);
    }
    return true;
}

bool audio_pcm_timeline_set_playhead_frames_back(audio_pcm_timeline_t *t,
                                                 uint32_t frames_back)
{
    if (!t) return false;
    uint64_t write_seq = audio_pcm_timeline_write_seq(t);
    uint32_t used = audio_pcm_timeline_used_frames(t);
    if (used == 0u || frames_back >= used) return false;
    uint64_t newest = write_seq - 1u;
    uint64_t target = newest - frames_back;
    return audio_pcm_timeline_set_playhead(t, target);
}

uint64_t audio_pcm_timeline_oldest_seq(const audio_pcm_timeline_t *t)
{
    return t ? cursor_load(&t->oldest_epoch, &t->oldest_seq,
                           &t->oldest_version) : 0u;
}

uint64_t audio_pcm_timeline_play_seq(const audio_pcm_timeline_t *t)
{
    return t ? cursor_load(&t->play_epoch, &t->play_seq,
                           &t->play_version) : 0u;
}

uint64_t audio_pcm_timeline_write_seq(const audio_pcm_timeline_t *t)
{
    return t ? cursor_load(&t->write_epoch, &t->write_seq,
                           &t->write_version) : 0u;
}

uint32_t audio_pcm_timeline_history_frames(const audio_pcm_timeline_t *t)
{
    if (!t) return 0u;
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    uint32_t oldest_seq = __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
    return play_seq - oldest_seq;
}

uint32_t audio_pcm_timeline_future_frames(const audio_pcm_timeline_t *t)
{
    if (!t) return 0u;
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    return write_seq - play_seq;
}

uint32_t audio_pcm_timeline_used_frames(const audio_pcm_timeline_t *t)
{
    if (!t) return 0u;
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_ACQUIRE);
    uint32_t oldest_seq = __atomic_load_n(&t->oldest_seq, __ATOMIC_ACQUIRE);
    return write_seq - oldest_seq;
}

uint32_t audio_pcm_timeline_generation(const audio_pcm_timeline_t *t)
{
    return t ? t->generation : 0u;
}

uint32_t audio_pcm_timeline_drop_newest(audio_pcm_timeline_t *t, uint32_t frames)
{
    if (!t || !t->frames || t->capacity == 0u || frames == 0u) return 0u;
    /* Producer-side rewind of the forward runway. play_seq is the floor: frames
     * at or before it have been handed to the output and are not ours to take
     * back. History below play_seq is untouched, so scratch keeps its window.
     * The caller must exclude the consumer while this runs. */
    uint32_t write_seq = __atomic_load_n(&t->write_seq, __ATOMIC_RELAXED);
    uint32_t play_seq = __atomic_load_n(&t->play_seq, __ATOMIC_ACQUIRE);
    uint32_t runway = write_seq - play_seq;
    if (frames > runway) frames = runway;
    if (frames == 0u) return 0u;
    uint32_t index = t->write_index;
    index = index >= frames ? index - frames : index + t->capacity - frames;
    t->write_index = index;
    uint64_t write_absolute = audio_pcm_timeline_write_seq(t);
    cursor_store_absolute(&t->write_epoch, &t->write_seq,
                          &t->write_version, write_absolute - frames);
    return frames;
}

#if defined(ESP_PLATFORM) && defined(CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION) && CONFIG_AUDIO_PCM_TIMELINE_QUALIFICATION
static void qualification_reader_task(void *arg)
{
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!qualification_hook_enabled()) break;

        /* Inspect the versions before using the seqlock readers. A scheduler
         * regression must be reported, not turn the qualification task into
         * an intentional infinite spin while its lower-priority writer waits. */
        const uint32_t oldest_version = __atomic_load_n(
            &s_qualification_timeline.oldest_version, __ATOMIC_ACQUIRE);
        const uint32_t play_version = __atomic_load_n(
            &s_qualification_timeline.play_version, __ATOMIC_ACQUIRE);
        const uint32_t write_version = __atomic_load_n(
            &s_qualification_timeline.write_version, __ATOMIC_ACQUIRE);
        if (((oldest_version | play_version | write_version) & 1u) != 0u) {
            qualification_fail();
        } else {
            (void)audio_pcm_timeline_oldest_seq(&s_qualification_timeline);
            (void)audio_pcm_timeline_play_seq(&s_qualification_timeline);
            (void)audio_pcm_timeline_write_seq(&s_qualification_timeline);
        }
        (void)__atomic_add_fetch(&s_qualification_reader_runs, 1u,
                                 __ATOMIC_RELAXED);
        TaskHandle_t writer = __atomic_load_n(&s_qualification_writer,
                                               __ATOMIC_ACQUIRE);
        if (writer) xTaskNotifyGive(writer);
    }
    __atomic_store_n(&s_qualification_reader, NULL, __ATOMIC_RELEASE);
    vTaskDelete(NULL);
}

static bool qualification_wait_for_reader(void)
{
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0u) {
        qualification_fail();
        return false;
    }
    return true;
}

static void qualification_seed(uint64_t base)
{
    audio_pcm_timeline_init(&s_qualification_timeline,
                            s_qualification_storage,
                            TIMELINE_QUALIFICATION_CAPACITY);
    s_qualification_timeline.oldest_seq = (uint32_t)base;
    s_qualification_timeline.play_seq = (uint32_t)base;
    s_qualification_timeline.write_seq = (uint32_t)base;
    s_qualification_timeline.oldest_epoch = (uint32_t)(base >> 32);
    s_qualification_timeline.play_epoch = (uint32_t)(base >> 32);
    s_qualification_timeline.write_epoch = (uint32_t)(base >> 32);
}

static void qualification_writer_task(void *arg)
{
    (void)arg;
    __atomic_store_n(&s_qualification_hook_enabled, 1u, __ATOMIC_RELEASE);

    for (uint32_t i = 0; i < TIMELINE_QUALIFICATION_ITERATIONS; ++i) {
        const uint64_t base = ((uint64_t)i << 32) + (uint64_t)UINT32_MAX - 1u;
        qualification_seed(base);

        if (!audio_pcm_timeline_push(&s_qualification_timeline, 1, -1) ||
            !audio_pcm_timeline_push(&s_qualification_timeline, 2, -2) ||
            !qualification_wait_for_reader()) {
            continue;
        }
        if (audio_pcm_timeline_write_seq(&s_qualification_timeline) != base + 2u) {
            qualification_fail();
        }

        if (!audio_pcm_timeline_set_playhead(&s_qualification_timeline, base) ||
            !qualification_wait_for_reader()) {
            continue;
        }
        if (audio_pcm_timeline_play_seq(&s_qualification_timeline) != base) {
            qualification_fail();
        }
    }

    /* Measure production-shaped wrap and handoff sections without the
     * scheduler hook, then remove all instrumentation from subsequent audio. */
    __atomic_store_n(&s_qualification_hook_enabled, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_qualification_measure_enabled, 1u, __ATOMIC_RELEASE);
    for (uint32_t i = 0; i < TIMELINE_QUALIFICATION_ITERATIONS; ++i) {
        const uint64_t base = ((uint64_t)i << 32) + (uint64_t)UINT32_MAX - 1u;
        qualification_seed(base);
        if (!audio_pcm_timeline_push(&s_qualification_timeline, 1, -1) ||
            !audio_pcm_timeline_push(&s_qualification_timeline, 2, -2) ||
            !audio_pcm_timeline_set_playhead(&s_qualification_timeline, base)) {
            qualification_fail();
        }
    }
    __atomic_store_n(&s_qualification_measure_enabled, 0u, __ATOMIC_RELEASE);

    const uint32_t reader_runs = __atomic_load_n(
        &s_qualification_reader_runs, __ATOMIC_ACQUIRE);
    if (s_publish_measurements != TIMELINE_QUALIFICATION_ITERATIONS * 2u ||
        s_publish_max_us >= 10u ||
        reader_runs != TIMELINE_QUALIFICATION_ITERATIONS * 2u) {
        qualification_fail();
    }

    const uint32_t failures = __atomic_load_n(&s_qualification_failures,
                                               __ATOMIC_ACQUIRE);
    const bool pass = failures == 0u;
    ESP_LOGI(TAG, "%s iterations=%u reader_runs=%u measurements=%u max_us=%u failures=%u",
             pass ? "PASS" : "FAIL",
             (unsigned)TIMELINE_QUALIFICATION_ITERATIONS,
             (unsigned)reader_runs,
             (unsigned)s_publish_measurements,
             (unsigned)s_publish_max_us,
             (unsigned)failures);
    service_log_event(SERVICE_LOG_TIMELINE_QUALIFICATION,
                      pass ? SERVICE_LOG_INFO : SERVICE_LOG_ERROR,
                      4u, TIMELINE_QUALIFICATION_ITERATIONS,
                      reader_runs, s_publish_max_us,
                      failures, pass ? "PASS" : "FAIL");

    TaskHandle_t reader = __atomic_load_n(&s_qualification_reader,
                                           __ATOMIC_ACQUIRE);
    if (reader) xTaskNotifyGive(reader);
    __atomic_store_n(&s_qualification_writer, NULL, __ATOMIC_RELEASE);
    vTaskDelete(NULL);
}

void audio_pcm_timeline_start_qualification(void)
{
    if (__atomic_load_n(&s_qualification_writer, __ATOMIC_ACQUIRE) ||
        __atomic_load_n(&s_qualification_reader, __ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&s_qualification_hook_enabled, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_qualification_measure_enabled, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_qualification_reader_runs, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&s_qualification_failures, 0u, __ATOMIC_RELAXED);
    s_publish_measurements = 0u;
    s_publish_max_us = 0u;

    BaseType_t reader_ok = xTaskCreatePinnedToCore(
        qualification_reader_task, "timeline_q_r", 4096, NULL, 6,
        &s_qualification_reader, 0);
    BaseType_t writer_ok = reader_ok == pdPASS
        ? xTaskCreatePinnedToCore(qualification_writer_task, "timeline_q_w",
                                  4096, NULL, 5,
                                  &s_qualification_writer, 0)
        : pdFAIL;
    if (writer_ok != pdPASS) {
        if (s_qualification_reader) vTaskDelete(s_qualification_reader);
        __atomic_store_n(&s_qualification_reader, NULL, __ATOMIC_RELEASE);
        __atomic_store_n(&s_qualification_writer, NULL, __ATOMIC_RELEASE);
        service_log_event(SERVICE_LOG_TIMELINE_QUALIFICATION,
                          SERVICE_LOG_ERROR, 4u,
                          TIMELINE_QUALIFICATION_ITERATIONS, 0u, 0u, 1u,
                          "task create failed");
    }
}
#else
void audio_pcm_timeline_start_qualification(void)
{
}
#endif
