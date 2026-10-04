#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
static bool fail_sync, fail_publish;
static unsigned publications;
static int64_t timer;
static int test_fsync(int fd) { (void)fd; return fail_sync ? -1 : 0; }
static int test_rename(const char *from, const char *to) {
    ++publications;
    return fail_publish ? -1 : rename(from, to);
}
#define fsync test_fsync
#define rename test_rename
#ifdef _WIN32
#include <direct.h>
static int test_mkdir(const char *path, unsigned mode) { (void)mode; return _mkdir(path); }
#define mkdir test_mkdir
#endif
#include "audio_recorder_sink.c"
#undef fsync
#undef rename
#ifdef _WIN32
#undef mkdir
#endif
int64_t esp_timer_get_time(void) { timer += 100; return timer; }
esp_err_t esp_vfs_fat_info(const char *p,uint64_t *total,uint64_t *freeb) {
    (void)p; *total = *freeb = 1ull << 30; return ESP_OK;
}
void service_log_event(int a,int b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g,const char *h) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;
}
static const char *part = "test_take.wav.part", *recovered = "test_take.recovered.wav";
static void fixture(bool valid) {
    remove(part); remove(recovered);
    FILE *fp = fopen(part, "wb"); assert(fp);
    uint8_t hdr[44]; audio_recorder_wav_build_header(hdr, 48000, 0);
    if (!valid) hdr[22] = 1;
    assert(fwrite(hdr,1,44,fp) == 44);
    const char payload[11] = {0}; assert(fwrite(payload,1,11,fp) == 11); assert(fclose(fp) == 0);
    publications = 0;
}
int main(void) {
    sd_io_gate_init();
    fixture(true); fail_sync = true;
    assert(!recover_one(part) && publications == 0);
    struct stat st; assert(stat(part,&st) == 0 && stat(recovered,&st) != 0);
    fail_sync = false; assert(recover_one(part) && publications == 1);
    assert(stat(part,&st) != 0 && stat(recovered,&st) == 0 && st.st_size == 52);
    FILE *fp = fopen(recovered,"rb"); uint8_t hdr[44]; assert(fread(hdr,1,44,fp) == 44); fclose(fp);
    assert(hdr[40] == 8 && hdr[4] == 44); /* Complete frames only, correct RIFF/data sizes. */
    fixture(false); assert(!recover_one(part) && publications == 0);
    fixture(true); fail_publish = true;
    assert(!recover_one(part) && publications == 1 && stat(part,&st) == 0);
    fail_publish = false;
    fp = fopen(recovered,"wb"); assert(fp); fputs("existing",fp); fclose(fp);
    publications = 0; assert(!recover_one(part) && publications == 0);
    assert(stat(recovered,&st) == 0 && st.st_size == 8);
    assert(audio_recorder_sink_fsync_max_us() == 100);
    remove(part); remove(recovered);
    puts("PASS actual sink recovery: sync/rename failures retain partial, corrupt header rejected, frame truncation, no overwrite");
}
