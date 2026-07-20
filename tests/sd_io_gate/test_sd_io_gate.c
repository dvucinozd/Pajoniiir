/*
 * Host tests for the bounded microSD I/O arbiter.
 *
 * Covers the pure admission policy (which operation classes are deferred while
 * recording) and the standalone gate's mutual-exclusion and recorder-active
 * flag behavior.
 */
#include "sd_io_gate.h"

#include <stdio.h>

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
