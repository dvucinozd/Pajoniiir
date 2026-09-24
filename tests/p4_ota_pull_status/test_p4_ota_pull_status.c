#include "p4_ota_pull_status.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char detail[96];
    p4_ota_pull_format_failure_detail(detail, sizeof(detail),
                                      "could not reach update server",
                                      "ESP_ERR_HTTP_CONNECT");
    assert(strcmp(detail,
                  "could not reach update server (ESP_ERR_HTTP_CONNECT)") == 0);

    char short_detail[12];
    p4_ota_pull_format_failure_detail(short_detail, sizeof(short_detail),
                                      "read failed", "ESP_ERR_TIMEOUT");
    assert(short_detail[sizeof(short_detail) - 1u] == '\0');
    assert(strncmp(short_detail, "read failed ", sizeof(short_detail) - 1u) == 0);

    puts("p4_ota_pull_status tests passed");
    return 0;
}
