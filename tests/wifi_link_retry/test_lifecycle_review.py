"""Execute production probe admission and worker retry paths with fault injection."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'firmware/p4-core/components/wifi_link/wifi_link.c').read_text(encoding='utf-8')
def function(signature, text=source):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "wifi_link_control.h"
#include "wifi_link_retry.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define APP_SETTINGS_OTA_SSID_CAP 33
#define WIFI_TRANSITION_OWNER_NONE 0
#define WIFI_TRANSITION_OWNER_PROBE 1
#define WIFI_TRANSITION_OWNER_CONTROL 2
#define WIFI_LINK_PROBE_RUNNING 1
#define WIFI_LINK_PROBE_FAILED 2
#define WIFI_LINK_MODE_ERROR 1
#define SERVICE_LOG_WIFI_ENABLE_REQ 1
#define SERVICE_LOG_WIFI_STARTED 2
#define SERVICE_LOG_WIFI_FAILED 3
#define SERVICE_LOG_WIFI_STOPPED 4
#define SERVICE_LOG_INFO 1
#define SERVICE_LOG_ERROR 2
#define MALLOC_CAP_INTERNAL 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define portMAX_DELAY UINT32_MAX
static bool s_active, s_sta_mode, s_probe_running, s_desired, s_worker_running;
static int s_ctrl_lock, owner, spawn_count, releases, start_calls, stop_calls;
static bool stop_during_acquire;
static struct { char address[16]; } s_probe;
static unsigned delays[10], delay_count;
static int wifi_transition_lease_owner(void) { return owner; }
static int wifi_transition_lease_acquire(int who) {
    if (owner) return 1;
    owner=who;
    if (stop_during_acquire) s_active=false;
    return ESP_OK;
}
static void wifi_transition_lease_release(int who) { assert(owner==who); owner=0; ++releases; }
static void app_settings_ota_get_ssid(char *out, size_t cap) { assert(cap>4); strcpy(out,"ssid"); }
static void probe_note(int state, int error, const char *text) { (void)state;(void)error;(void)text; }
static void wifi_link_probe_task(void *arg) { (void)arg; }
static int xTaskCreate(void (*task)(void*),const char *name,unsigned stack,void *arg,unsigned prio,void *handle)
{ (void)task;(void)name;(void)stack;(void)arg;(void)prio;(void)handle;++spawn_count;return pdPASS; }
static void xSemaphoreTake(int sem,unsigned ticks) { (void)sem;(void)ticks; }
static void xSemaphoreGive(int sem) { (void)sem; }
static void vTaskDelay(unsigned ticks) { assert(delay_count<10);delays[delay_count++]=ticks; }
static void vTaskDelete(void *task) { (void)task; }
static unsigned uxTaskGetStackHighWaterMark(void *task) { (void)task;return 100; }
#define service_log_event(...) ((void)0)
#define service_log_sync() ((void)0)
static void status_publish(int mode,int error,bool active) { (void)mode;(void)error;(void)active; }
static int start_under_lease(void) { ++start_calls; if(start_calls<=4)return ESP_ERR_NO_MEM;s_active=true;return ESP_OK; }
static int wifi_link_stop(void) { ++stop_calls;s_active=false;s_desired=true;return ESP_OK; }
static void stop_under_lease(void) { s_active=false; }
static struct board_caps { bool ethernet; } caps;
static const struct board_caps *board_capabilities_get(void) { return &caps; }
static unsigned dns_stops, web_stops;
static void dns_server_stop(void) { ++dns_stops; }
static void web_server_stop(void) { ++web_stops; }
typedef struct { uint32_t addr; } esp_ip4_addr_t;
static uint32_t s_ap_ipv4;
static int fake_netif, *s_ap_netif = &fake_netif;
static unsigned destroyed;
static bool (*s_address_probe)(char*, size_t);
static void current_ap_ipv4(char out[16]);
static bool ethernet_address(char *out, size_t length) {
    snprintf(out, length, "10.0.0.44");
    return true;
}
static void esp_ip4addr_ntoa(const esp_ip4_addr_t *ip, char *out, size_t length) {
    snprintf(out, length, "192.168.4.%u", (unsigned)ip->addr);
}
static void esp_netif_destroy_default_wifi(int *netif) {
    char address[16];
    assert(netif == &fake_netif);
    current_ap_ipv4(address);
    assert(!strcmp(address, "10.0.0.44")); /* HTTP during destruction */
    ++destroyed;
}
'''
web = (ROOT/'firmware/p4-core/components/web_server/web_server.c').read_text(encoding='utf-8')
code += function('void web_server_set_ap_ipv4(', web)
code += function('static void current_ap_ipv4(', web)
code += function('static void stop_ap_netif(void)')
code += function('static void stop_ap_services(void)')
code += function('esp_err_t wifi_link_probe_start(void)')
code += function('static void wifi_link_worker(void *arg)')
code += r'''
int main(void) {
    char address[16];
    s_address_probe = ethernet_address;
    web_server_set_ap_ipv4(9);
    current_ap_ipv4(address);
    assert(!strcmp(address, "192.168.4.9"));
    stop_ap_netif();
    assert(!s_ap_netif && destroyed == 1);
    caps.ethernet=true;stop_ap_services();
    assert(dns_stops==1 && web_stops==0);
    caps.ethernet=false;stop_ap_services();
    assert(dns_stops==2 && web_stops==1);
    s_active=true; stop_during_acquire=true;
    assert(wifi_link_probe_start()==ESP_ERR_INVALID_STATE);
    assert(!owner && !s_probe_running && !spawn_count && releases==1);
    stop_during_acquire=false;s_active=true;
    assert(wifi_link_probe_start()==ESP_OK && spawn_count==1 && owner==WIFI_TRANSITION_OWNER_PROBE);
    wifi_transition_lease_release(owner);s_probe_running=false;
    s_active=false;s_desired=true;s_worker_running=true;
    wifi_link_worker(NULL);
    assert(start_calls==5 && stop_calls==1 && s_active && !s_worker_running && !owner);
    assert(delay_count==3 && delays[0]==1000 && delays[1]==2000 && delays[2]==1000);
}
'''
out = ROOT/'.cache/wifi_lifecycle_review'
out.mkdir(parents=True, exist_ok=True)
cfile = out/'test.c'
cfile.write_text(code, encoding='utf-8')
exe = out/('test.exe' if os.name == 'nt' else 'test')
subprocess.run([shutil.which('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-variable', '-I'+str(ROOT/'firmware/p4-core/components/wifi_link/include'),
    str(cfile), str(ROOT/'firmware/p4-core/components/wifi_link/wifi_link_retry.c'),
    str(ROOT/'firmware/p4-core/components/wifi_link/wifi_link_control.c'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
print('PASS production Wi-Fi probe lease race and same-worker fresh retry budget')
