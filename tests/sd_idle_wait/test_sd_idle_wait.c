#include "sd_idle_wait.h"
#include <assert.h>
#include <stdio.h>
typedef struct { int64_t time; unsigned probes, yields, ready_after; esp_err_t error; } fake_t;
static int64_t now(void *p) { return ((fake_t *)p)->time; }
static bool ready(uint32_t s) { return s == 1; }
static esp_err_t probe(void *p, uint32_t *s) {
    fake_t *f=p; ++f->probes; f->time += 20; *s=f->probes >= f->ready_after;
    return f->error;
}
static void yield(void *p) { fake_t *f=p; ++f->yields; f->time += 1000; }
int main(void) {
    sd_idle_wait_ops_t ops={now,ready,probe,yield};
    fake_t f={.ready_after=4};
    assert(sd_idle_wait_run(&f, 1, 5000, &ops)==ESP_OK && f.probes==0);
    assert(sd_idle_wait_run(&f, 0, 5000, &ops)==ESP_OK);
    assert(f.probes==4 && f.yields==3 && f.time==3080);
    f=(fake_t){.ready_after=100};
    assert(sd_idle_wait_run(&f,0,2000,&ops)==ESP_ERR_TIMEOUT);
    assert(f.probes==2 && f.yields==2); /* No command after deadline. */
    f=(fake_t){.ready_after=100,.error=ESP_ERR_INVALID_RESPONSE};
    assert(sd_idle_wait_run(&f,0,5000,&ops)==ESP_ERR_INVALID_RESPONSE && f.probes==1);
    assert(sd_idle_wait_run(&f,0,0,&ops)==ESP_ERR_TIMEOUT);
    assert(sd_idle_wait_run(&f,0,5000,NULL)==ESP_ERR_INVALID_ARG);
    sd_idle_wait_stats_t stats; sd_idle_wait_get_stats(&stats);
    assert(stats.waits==4 && stats.polls==7 && stats.timeouts==2 && stats.errors==1);
    assert(stats.max_wait_us==3080);
    sd_idle_wait_get_stats(NULL);
    puts("PASS bounded SD idle polling, immediate ready, deadlines, errors and counters");
}
