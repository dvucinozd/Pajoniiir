#include "audio_recorder_finalize.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    bool ok[4];
    char calls[8];
    unsigned count;
} fixture_t;

static bool step(fixture_t *f, unsigned index, char name)
{
    f->calls[f->count++] = name;
    f->calls[f->count] = '\0';
    return f->ok[index];
}
static bool patch(void *p) { return step(p, 0u, 'P'); }
static bool sync_step(void *p) { return step(p, 1u, 'S'); }
static bool close_step(void *p) { return step(p, 2u, 'C'); }
static bool publish(void *p) { return step(p, 3u, 'R'); }

static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)

static fixture_t good(void)
{
    fixture_t f = { .ok = { true, true, true, true } };
    return f;
}

int main(void)
{
    fixture_t f = good();
    audio_recorder_finalize_result_t r = audio_recorder_finalize_run(
        &f, patch, sync_step, close_step, publish, true);
    CHECK(r.failed_stage == AUDIO_RECORDER_FINALIZE_STAGE_NONE);
    CHECK(r.closed && r.published && strcmp(f.calls, "PSCR") == 0);

    const audio_recorder_finalize_stage_t durability_failure_stages[] = {
        AUDIO_RECORDER_FINALIZE_STAGE_PATCH,
        AUDIO_RECORDER_FINALIZE_STAGE_SYNC,
        AUDIO_RECORDER_FINALIZE_STAGE_CLOSE,
    };
    for (unsigned fail = 0u; fail < 3u; ++fail) {
        f = good();
        f.ok[fail] = false;
        r = audio_recorder_finalize_run(&f, patch, sync_step, close_step, publish, true);
        CHECK(r.failed_stage == durability_failure_stages[fail]);
        CHECK(r.closed && !r.published);
        CHECK(strcmp(f.calls, "PSC") == 0); /* never rename a failed .part */
    }

    f = good();
    f.ok[3] = false;
    r = audio_recorder_finalize_run(&f, patch, sync_step, close_step, publish, true);
    CHECK(r.failed_stage == AUDIO_RECORDER_FINALIZE_STAGE_PUBLISH);
    CHECK(r.closed && !r.published && strcmp(f.calls, "PSCR") == 0);

    f = good();
    r = audio_recorder_finalize_run(&f, patch, sync_step, close_step, publish, false);
    CHECK(r.failed_stage == AUDIO_RECORDER_FINALIZE_STAGE_NONE);
    CHECK(r.closed && !r.published && strcmp(f.calls, "PSC") == 0);

    if (failures == 0) puts("audio_recorder_finalize tests passed");
    return failures ? 1 : 0;
}
