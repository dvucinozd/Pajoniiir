/*
 * Host tests for the bounded microSD I/O arbiter.
 *
 * Covers the pure admission policy (which operation classes are deferred while
 * recording) and the standalone gate's mutual-exclusion and recorder-active
 * flag behavior.
 */
#include "sd_io_gate.h"

#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
static unsigned owners, collisions;
static void *reservation_worker(void *arg)
{
    sd_io_activity_t activity = (sd_io_activity_t)(uintptr_t)arg;
    for (unsigned i = 0; i < 10000; ++i) {
        while (!sd_io_gate_reserve(activity)) {}
        if (__atomic_add_fetch(&owners, 1u, __ATOMIC_SEQ_CST) != 1u)
            __atomic_add_fetch(&collisions, 1u, __ATOMIC_RELAXED);
        __atomic_sub_fetch(&owners, 1u, __ATOMIC_SEQ_CST);
        sd_io_gate_release(activity);
    }
    return NULL;
}

static int s_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);                 \
            s_failures++;                                                      \
        }                                                                      \
    } while (0)

static void test_admit_idle(void)
{
    printf("== admit: recorder idle ==\n");
    /* Nothing is deferred when the recorder is not active. */
    CHECK(sd_io_gate_admit(SD_IO_CLASS_RECORDER, false));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_META_CACHE, false));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_PROFILE_INSTALL, false));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_SERVICE_LOG, false));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_FREE_SPACE, false));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_PROFILE_UPLOAD, false));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_LOG_DOWNLOAD, false));
}

static void test_admit_recording(void)
{
    printf("== admit: recorder active ==\n");
    /* Bounded fast operations still proceed while recording. */
    CHECK(sd_io_gate_admit(SD_IO_CLASS_RECORDER, true));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_META_CACHE, true));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_PROFILE_INSTALL, true));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_SERVICE_LOG, true));
    CHECK(sd_io_gate_admit(SD_IO_CLASS_FREE_SPACE, true));
    /* Heavy optional admin work is deferred. */
    CHECK(!sd_io_gate_admit(SD_IO_CLASS_PROFILE_UPLOAD, true));
    CHECK(!sd_io_gate_admit(SD_IO_CLASS_LOG_DOWNLOAD, true));
}

static void test_gate_mutex(void)
{
    printf("== gate mutual exclusion (standalone) ==\n");
    CHECK(sd_io_gate_init() == ESP_OK);

    CHECK(sd_io_gate_try_begin(10u));       /* first acquire succeeds */
    CHECK(!sd_io_gate_try_begin(10u));      /* already held -> busy */
    sd_io_gate_end();
    CHECK(sd_io_gate_try_begin(10u));       /* released -> acquirable again */
    sd_io_gate_end();
}

static void test_recorder_flag(void)
{
    printf("== recorder-active flag ==\n");
    sd_io_gate_init();
    CHECK(!sd_io_gate_recorder_active());
    sd_io_gate_set_recorder_active(true);
    CHECK(sd_io_gate_recorder_active());
    /* The flag drives the admission policy for the heavy classes. */
    CHECK(!sd_io_gate_admit(SD_IO_CLASS_PROFILE_UPLOAD, sd_io_gate_recorder_active()));
    sd_io_gate_set_recorder_active(false);
    CHECK(!sd_io_gate_recorder_active());
    CHECK(sd_io_gate_admit(SD_IO_CLASS_PROFILE_UPLOAD, sd_io_gate_recorder_active()));
}

int main(void)
{
    CHECK(!sd_io_gate_reserve(SD_ACTIVITY_NONE));
    CHECK(sd_io_gate_reserve(SD_ACTIVITY_DOWNLOAD));
    CHECK(!sd_io_gate_reserve(SD_ACTIVITY_RECORDER));
    sd_io_gate_release(SD_ACTIVITY_RECORDER);
    CHECK(sd_io_gate_activity() == SD_ACTIVITY_DOWNLOAD);
    sd_io_gate_release(SD_ACTIVITY_DOWNLOAD);
    CHECK(sd_io_gate_reserve(SD_ACTIVITY_RECORDER));
    CHECK(sd_io_gate_recorder_active());
    CHECK(!sd_io_gate_admit(SD_IO_CLASS_TRACK_DOWNLOAD, true));
    CHECK(!sd_io_gate_reserve(SD_ACTIVITY_DOWNLOAD));
    sd_io_gate_release(SD_ACTIVITY_RECORDER);
    CHECK(sd_io_gate_activity() == SD_ACTIVITY_NONE);
    pthread_t recorder, download;
    CHECK(pthread_create(&recorder, NULL, reservation_worker, (void *)(uintptr_t)SD_ACTIVITY_RECORDER) == 0);
    CHECK(pthread_create(&download, NULL, reservation_worker, (void *)(uintptr_t)SD_ACTIVITY_DOWNLOAD) == 0);
    CHECK(pthread_join(recorder, NULL) == 0 && pthread_join(download, NULL) == 0);
    CHECK(!owners && !collisions && sd_io_gate_activity() == SD_ACTIVITY_NONE);
    printf("=== sd_io_gate tests ===\n");
    test_admit_idle();
    test_admit_recording();
    test_gate_mutex();
    test_recorder_flag();

    if (s_failures == 0) {
        printf("sd_io_gate tests passed\n");
        return 0;
    }
    printf("sd_io_gate tests FAILED (%d)\n", s_failures);
    return 1;
}
