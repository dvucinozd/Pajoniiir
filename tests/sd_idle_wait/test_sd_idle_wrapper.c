#include "sd_idle_wait.h"
#include "esp_private/sdmmc_common.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
static unsigned real_calls, probes, yields;
static int64_t timer;
int64_t esp_timer_get_time(void) { return timer; }
void vTaskDelay(unsigned ticks) { assert(ticks == 1); ++yields; timer += 1000; }
esp_err_t __real_sdmmc_wait_for_idle(sdmmc_card_t *c,uint32_t status) {
    assert(c && status == 0); ++real_calls; return ESP_FAIL;
}
esp_err_t sdmmc_send_cmd_send_status(sdmmc_card_t *c,uint32_t *status) {
    assert(!c->spi); *status = ++probes == 2; return ESP_OK;
}
esp_err_t __wrap_sdmmc_wait_for_idle(sdmmc_card_t *, uint32_t);
int main(void) {
    assert(__wrap_sdmmc_wait_for_idle(NULL,0) == ESP_ERR_INVALID_ARG);
    sdmmc_card_t spi = {.spi = true}, sd = {0};
    assert(__wrap_sdmmc_wait_for_idle(&spi,0) == ESP_FAIL && real_calls == 1);
#if CONFIG_PAJONIIIR_SD_IDLE_WAIT
    assert(__wrap_sdmmc_wait_for_idle(&sd,0) == ESP_OK);
    assert(real_calls == 1 && probes == 2 && yields == 1);
    puts("PASS enabled SD wrapper, SPI passthrough and one-tick yielding");
#else
    assert(__wrap_sdmmc_wait_for_idle(&sd,0) == ESP_FAIL);
    assert(real_calls == 2 && !probes && !yields);
    puts("PASS disabled SD wrapper uses original implementation for SD and SPI");
#endif
}
