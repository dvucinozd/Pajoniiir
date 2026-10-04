#include <assert.h>
#include <string.h>
#include "runtime_platform.h"
#include "audio_recorder.c"
static unsigned ticks, bits, writes, finalized, aborted, allocations;
static bool fail_alloc, fail_task, fail_wait;
static uint64_t free_bytes = 512ull * 1024 * 1024;
static esp_err_t write_error, finalize_error;
static void (*pending_writer)(void *);
int64_t esp_timer_get_time(void) { return (int64_t)ticks * 1000; }
uint32_t esp_random(void) { return 1; }
void *heap_caps_malloc(size_t n,unsigned c) { (void)c; if(fail_alloc) return NULL; ++allocations; return malloc(n); }
void heap_caps_free(void *p) { if(p) --allocations; free(p); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1,sizeof(int)); }
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t t) { (void)t; if(*(int *)s) return 0; *(int *)s=1; return 1; }
void xSemaphoreGive(SemaphoreHandle_t s) { *(int *)s=0; }
EventGroupHandle_t xEventGroupCreate(void) { return &bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t s,EventBits_t b) { (void)s; return bits|=b; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t s,EventBits_t b) { (void)s; unsigned old=bits; bits &= ~b; return old; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t s,EventBits_t b,int c,int a,TickType_t t) {
    (void)s; (void)c; (void)a; if(fail_wait) {ticks+=t; return 0;}
    if(!(bits & b) && pending_writer) pending_writer(NULL);
    return bits & b;
}
int xTaskCreate(void (*fn)(void *),const char *n,unsigned st,void *p,unsigned pr,TaskHandle_t *out) {
    (void)n;(void)st;(void)p;(void)pr;
    if(fail_task) return 0;
    pending_writer=fn; *out=(void *)1; return 1;
}
void vTaskDelay(TickType_t t) { ticks+=t; }
void vTaskDelete(TaskHandle_t t) { (void)t; }
TickType_t xTaskGetTickCount(void) { return ticks; }
esp_err_t nvs_open(const char *n,unsigned m,nvs_handle_t *h) { (void)n;(void)m;(void)h;return ESP_FAIL; }
esp_err_t nvs_get_u32(nvs_handle_t h,const char *n,uint32_t *v) {(void)h;(void)n;(void)v;return ESP_FAIL;}
esp_err_t nvs_set_u32(nvs_handle_t h,const char *n,uint32_t v) {(void)h;(void)n;(void)v;return ESP_FAIL;}
esp_err_t nvs_commit(nvs_handle_t h) {(void)h;return ESP_FAIL;}
void nvs_close(nvs_handle_t h) {(void)h;}
void service_log_event(int a,int b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g,const char *h) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;
}
esp_err_t audio_recorder_sink_prepare(uint64_t *b) {*b=free_bytes;return ESP_OK;}
esp_err_t audio_recorder_sink_open(audio_recorder_sink_t *s,uint32_t r,uint32_t b,uint32_t n) {
    (void)b;(void)n; s->is_open=true;s->sample_rate=r;return ESP_OK;
}
esp_err_t audio_recorder_sink_write_block(audio_recorder_sink_t *s,const int16_t *p,uint32_t n,uint32_t r) {
    (void)s;(void)p;(void)n;(void)r;++writes;return write_error;
}
esp_err_t audio_recorder_sink_checkpoint(audio_recorder_sink_t *s) {(void)s;return ESP_OK;}
esp_err_t audio_recorder_sink_finalize(audio_recorder_sink_t *s) {s->is_open=false;++finalized;return finalize_error;}
esp_err_t audio_recorder_sink_abort(audio_recorder_sink_t *s) {s->is_open=false;++aborted;return ESP_OK;}
esp_err_t audio_recorder_sink_recover_orphans(void) {return ESP_OK;}
esp_err_t audio_recorder_sink_free_bytes(uint64_t *b) {*b=free_bytes;return ESP_OK;}
void audio_recorder_sink_write_cost(uint32_t *a,uint32_t *b) {*a=0;*b=0;}
uint32_t audio_recorder_sink_fsync_max_us(void) {return 0;}
static void retired(void) {
    assert(!s_slots && !s_writer && !allocations);
    assert(sd_io_gate_activity()==SD_ACTIVITY_NONE);
}
int main(void) {
    int16_t pcm[512]={0};
    assert(audio_recorder_init()==ESP_OK);
    assert(sd_io_gate_reserve(SD_ACTIVITY_DOWNLOAD));
    assert(audio_recorder_start(48000)==ESP_ERR_INVALID_STATE && !allocations);
    sd_io_gate_release(SD_ACTIVITY_DOWNLOAD);
    fail_alloc=true; assert(audio_recorder_start(48000)==ESP_ERR_NO_MEM); fail_alloc=false; retired();
    free_bytes=1; assert(audio_recorder_start(48000)==ESP_ERR_NO_MEM); retired(); free_bytes=512ull*1024*1024;
    fail_task=true; assert(audio_recorder_start(48000)==ESP_ERR_NO_MEM); fail_task=false; retired();
    unsigned initial_final=finalized, initial_abort=aborted;
    assert(audio_recorder_start(48000)==ESP_OK);
    assert(audio_recorder_init()==ESP_OK && audio_recorder_get_state()==AUDIO_RECORDER_RECORDING);
    assert(audio_recorder_start(48000)==ESP_ERR_INVALID_STATE);
    unsigned pushed=0; while(audio_recorder_push_master(pcm,256,48000)) assert(++pushed<10);
    assert(!audio_recorder_push_master(pcm,256,48000)); /* Admission closed on first loss. */
    pending_writer(NULL);
    assert(audio_recorder_get_state()==AUDIO_RECORDER_ERROR);
    assert(!sd_io_gate_reserve(SD_ACTIVITY_DOWNLOAD));
    assert(audio_recorder_stop()==ESP_ERR_NO_MEM);
    assert(finalized==initial_final && aborted==initial_abort+1); retired();
    assert(audio_recorder_start(44100)==ESP_OK);
    assert(audio_recorder_push_master(pcm,256,44100));
    write_error=ESP_FAIL; pending_writer(NULL);
    assert(audio_recorder_stop()==ESP_FAIL); write_error=ESP_OK; retired();
    assert(audio_recorder_start(48000)==ESP_OK);
    assert(audio_recorder_push_master(pcm,256,48000));
    fail_wait=true; assert(audio_recorder_stop()==ESP_ERR_TIMEOUT);
    assert(s_slots && allocations && !sd_io_gate_reserve(SD_ACTIVITY_DOWNLOAD));
    fail_wait=false; assert(audio_recorder_stop()==ESP_ERR_TIMEOUT); retired();
    assert(audio_recorder_start(48000)==ESP_OK);
    assert(audio_recorder_push_master(pcm,256,48000));
    assert(audio_recorder_stop()==ESP_OK && writes>0 && finalized==initial_final+1); retired();
    finalize_error=ESP_FAIL; assert(audio_recorder_start(48000)==ESP_OK);
    assert(audio_recorder_stop()==ESP_FAIL); retired();
    free(s_control_lock);
    puts("PASS actual recorder overflow, START/STOP ownership, download exclusion, disk-full, IO/finalize faults and timeout retention");
}
