#pragma once

/* Logging is discarded, not redirected: several suites assert on their own
 * stdout, and firmware log lines interleaved into that would be noise. */

#define ESP_LOGE(...) do { } while (0)
#define ESP_LOGW(...) do { } while (0)
#define ESP_LOGI(...) do { } while (0)
#define ESP_LOGD(...) do { } while (0)
#define ESP_LOGV(...) do { } while (0)

#define ESP_EARLY_LOGE(...) do { } while (0)
#define ESP_EARLY_LOGW(...) do { } while (0)
#define ESP_EARLY_LOGI(...) do { } while (0)

#define ESP_LOG_BUFFER_HEX(...)  do { } while (0)
#define ESP_LOG_BUFFER_HEXDUMP(...) do { } while (0)
