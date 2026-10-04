#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_err.h"
typedef unsigned TickType_t;
typedef unsigned EventBits_t;
typedef void *TaskHandle_t;
typedef void *EventGroupHandle_t;
typedef void *SemaphoreHandle_t;
typedef unsigned nvs_handle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY (~0u)
#define pdMS_TO_TICKS(x) (x)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define CONFIG_AUDIO_RECORDER_RING_INTERNAL 0
#define CONFIG_AUDIO_RECORDER_RING_BYTES 2048
#define NVS_READWRITE 1
#define ESP_LOGI(tag,...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
enum { SERVICE_LOG_RECORDING_FAILED, SERVICE_LOG_RECORDING_SD_STALL,
       SERVICE_LOG_RECORDING_STARTED, SERVICE_LOG_RECORDING_STOPPED,
       SERVICE_LOG_ERROR, SERVICE_LOG_WARN, SERVICE_LOG_INFO };
void service_log_event(int a,int b,unsigned c,unsigned d,unsigned e,unsigned f,unsigned g,const char *h);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
void *heap_caps_malloc(size_t bytes, unsigned caps);
void heap_caps_free(void *p);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
void xSemaphoreGive(SemaphoreHandle_t s);
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t s, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t s, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t s,EventBits_t b,int clear,int all,TickType_t ticks);
int xTaskCreate(void (*fn)(void *),const char *n,unsigned stack,void *p,unsigned prio,TaskHandle_t *out);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);
esp_err_t nvs_open(const char *,unsigned,nvs_handle_t *);
esp_err_t nvs_get_u32(nvs_handle_t,const char *,uint32_t *);
esp_err_t nvs_set_u32(nvs_handle_t,const char *,uint32_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
