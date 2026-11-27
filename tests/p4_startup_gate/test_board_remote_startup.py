"""Execute the actual common startup tail and JC1060 network provider with stubs."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
app = (root/'firmware/p4-core/components/p4_app/app_main.c').read_text(encoding='utf-8')
jc = (root/'firmware/main-deck-jc1060/main/app_main.c').read_text(encoding='utf-8')
web = (root/'firmware/p4-core/components/web_server/web_server.c').read_text(encoding='utf-8')
build = Path(__file__).parent/'build/remote-startup'; build.mkdir(parents=True,exist_ok=True)
def function(source, marker):
    start=source.index(marker); opening=source.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
tail=app[app.index('    esp_err_t remote_rc = ESP_OK;'):app.index('\nvoid pajoniiir_common_app_main(void)')]
prelude=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "p4_startup_gate.h"
#include "web_api_helpers.h"
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_NOT_SUPPORTED 2
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define CONFIG_DDJ_OTA_STARTUP_TIMEOUT_TEST 0
#define SERVICE_LOG_P4_OTA_FAILED 1
#define SERVICE_LOG_ERROR 1
#define pdMS_TO_TICKS(x) (x)
typedef int esp_err_t;
typedef struct {bool required;int (*start)(void);bool (*ready)(void);} p4_app_network_provider_t;
typedef bool (*web_server_address_probe_fn)(char*,size_t);
typedef struct {uint32_t addr;} ip_addr_t, esp_ip4_addr_t;
typedef struct {ip_addr_t ip;} esp_netif_ip_info_t;
typedef struct {bool up;ip_addr_t ip;} esp_netif_t;
static esp_netif_t eth;
static uint32_t s_ap_ipv4;
static bool have_eth,web_running,wifi_ready,pending;
static unsigned marked,rejected,restarts,starts,critical;
static int start_result;
static uint64_t now;
static web_server_address_probe_fn s_address_probe;
static esp_netif_t *board_ethernet_netif(void) {return have_eth?&eth:NULL;}
static bool esp_netif_is_netif_up(esp_netif_t *n) {return n->up;}
static int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip) {ip->ip=n->ip;return ESP_OK;}
static const char *esp_ip4addr_ntoa(const ip_addr_t *ip,char *out,size_t n) {
 snprintf(out,n,"%u.%u.%u.%u",ip->addr>>24,(ip->addr>>16)&255,(ip->addr>>8)&255,ip->addr&255);return out;
}
static void web_server_set_address_probe(web_server_address_probe_fn p) {s_address_probe=p;}
static bool web_server_is_running(void) {return web_running;}
static int web_server_start(void) {assert(!marked);++starts;web_running=start_result==0;return start_result;}
typedef struct {bool rollback_pending;} firmware_health_info_t;
typedef struct {unsigned critical_allocation_failures;} firmware_resources_t;
static void firmware_resources_phase(const char *p) {(void)p;}
static int firmware_health_get_info(firmware_health_info_t *h) {h->rollback_pending=pending;return ESP_OK;}
static void firmware_resources_snapshot(firmware_resources_t *r) {r->critical_allocation_failures=critical;}
static uint64_t esp_timer_get_time(void) {return now;}
static void vTaskDelay(unsigned n) {now+=(uint64_t)n*1000;}
static bool wifi_link_is_active(void) {return wifi_ready;}
static const char *esp_err_to_name(int e) {(void)e;return "error";}
static void service_log_event(int a,int b,unsigned c,uint32_t d,uint32_t e,uint32_t f,uint32_t g,const char *h) {(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
static void service_log_sync(void) {}
static int firmware_health_reject_pending(void) {++rejected;return ESP_OK;}
static void esp_restart(void) {++restarts;}
static int firmware_health_mark_ready(void) {++marked;return ESP_OK;}
'''
code=prelude+function(jc,'static bool remote_address(')+'\n'+function(jc,'static esp_err_t remote_start(')+'\n'+function(jc,'static bool remote_ready(')+'\n'
code+=function(web,'void web_server_set_ap_ipv4(')+'\n'
code+=function(web,'static void current_ap_ipv4(')+'\n'
code+=function(web,'static bool current_host_allowed(')+'\n'
code+='static void startup(const p4_app_network_provider_t *provider,bool boot_network_required) { const uint64_t boot_started_us=now;\n'+tail
code+=r'''
static void reset(void) {pending=true;marked=rejected=restarts=starts=critical=0;now=1000;wifi_ready=false;start_result=0;web_running=false;s_address_probe=NULL;have_eth=true;web_server_set_ap_ipv4(0);eth.up=true;eth.ip.addr=0xc0a86453;}
int main(void) {
 const p4_app_network_provider_t provider={true,remote_start,remote_ready};char address[16];
 reset();startup(&provider,false);assert(starts==1 && marked==1 && !rejected);
 current_ap_ipv4(address);assert(!strcmp(address,"192.168.100.83"));
 reset();start_result=ESP_FAIL;startup(&provider,false);assert(!marked && rejected==1 && restarts==1 && now>=60001000);
 reset();eth.ip.addr=0;startup(&provider,false);assert(!marked && rejected==1);
 reset();eth.up=false;startup(&provider,false);assert(!marked && rejected==1);
 reset();have_eth=false;startup(&provider,false);assert(!marked && rejected==1);
 reset();critical=1;startup(&provider,false);assert(!marked && rejected==1);
 reset();startup(NULL,false);assert(marked==1 && !rejected && !starts); /* Wi-Fi not requested */
 reset();startup(NULL,true);assert(!marked && rejected==1); /* requested AP missing */
 reset();wifi_ready=true;startup(NULL,true);assert(marked==1 && !rejected);
 reset();pending=false;start_result=ESP_FAIL;startup(&provider,false);assert(!rejected && marked==1); /* accepted/factory image */
 reset();const p4_app_network_provider_t optional={false,remote_start,remote_ready};start_result=ESP_FAIL;startup(&optional,false);assert(marked==1 && !rejected);
 reset();web_server_set_ap_ipv4(0xc0a80701);
 current_ap_ipv4(address);assert(!strcmp(address,"192.168.7.1"));
 startup(&provider,false);
 assert(current_host_allowed("192.168.7.1:80"));
 assert(current_host_allowed("192.168.100.83:8080"));
 assert(current_host_allowed("pajoniiir.local"));
 assert(!current_host_allowed("192.168.4.1"));
 assert(!current_host_allowed("192.168.100.83.evil"));
 assert(!current_host_allowed("192.168.100.83:abc"));
 assert(!current_host_allowed("pajoniiir.local.evil"));
 eth.up=false;assert(!current_host_allowed("192.168.100.83"));
 assert(current_host_allowed("192.168.7.1"));
 eth.up=true;web_server_set_ap_ipv4(0);current_ap_ipv4(address);assert(!strcmp(address,"192.168.100.83"));
 assert(!current_host_allowed("192.168.7.1"));
 puts("PASS production board remote startup before health acceptance: Ethernet HTTP/DHCP/down/failure/timeouts, Wi-Fi and optional/factory policy");
}
'''
c=build/'startup.c';c.write_text(code)
exe=build/('startup.exe' if os.name=='nt' else 'startup')
gcc=shutil.which('gcc');assert gcc
subprocess.run([gcc,'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
 '-I'+str(root/'firmware/common/firmware_health/include'),
 '-I'+str(root/'firmware/p4-core/components/web_server/include'),str(c),
 str(root/'firmware/p4-core/components/web_server/web_api_helpers.c'),
 str(root/'firmware/common/firmware_health/p4_startup_gate.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
