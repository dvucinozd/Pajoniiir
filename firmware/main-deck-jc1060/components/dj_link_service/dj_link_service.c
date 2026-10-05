#include "dj_link_service.h"
#include "dj_link_udp.h"
#include "dj_link_tcp.h"
#include "dj_link_sync.h"
#include "deck_core.h"
#include "audio_engine.h"
#include "board_ethernet.h"
#include "app_settings.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char *TAG = "dj_link";
static TaskHandle_t s_task;
static SemaphoreHandle_t s_snapshot_lock;
static dj_link_discovery_t *s_model, *s_snapshot;
static SemaphoreHandle_t s_browse_lock;
static dj_link_browse_t *s_browse;
static dj_link_peer_track_t *s_rows;
static dj_link_tcp_t s_tcp;
static dj_link_sync_t s_sync;
static portMUX_TYPE s_clock_lock=portMUX_INITIALIZER_UNLOCKED;
static deck_net_clock_t s_clock;
static deck_net_local_snapshot_t s_local[2];
static deck_sink_latency_t s_calibration;
static int8_t s_local_master=-1;
static uint32_t s_clock_ms;
static bool s_link_enabled;
static uint8_t s_clock_command; /* 1 follow, 2/3 request local master */
static bool clock_read(deck_net_clock_t *clock,int8_t *master,deck_sink_latency_t *calibration)
{
    portENTER_CRITICAL(&s_clock_lock);
    *clock=s_clock;*master=s_local_master;*calibration=s_calibration;
    bool enabled=s_link_enabled;uint32_t published=s_clock_ms;
    portEXIT_CRITICAL(&s_clock_lock);
    if ((uint32_t)((uint32_t)(esp_timer_get_time()/1000)-published)>120) {
        clock->valid=false;*master=-1;
    }
    return enabled;
}
static void clock_publish(const deck_net_local_snapshot_t local[2])
{
    portENTER_CRITICAL(&s_clock_lock);memcpy(s_local,local,sizeof(s_local));portEXIT_CRITICAL(&s_clock_lock);
}
static void clock_follow(void)
{
    portENTER_CRITICAL(&s_clock_lock);s_clock_command=1;s_clock.valid=false;portEXIT_CRITICAL(&s_clock_lock);
}
static void clock_master(uint8_t deck)
{
    if (deck>1) return;
    portENTER_CRITICAL(&s_clock_lock);s_clock_command=(uint8_t)(deck+2);portEXIT_CRITICAL(&s_clock_lock);
}
bool dj_link_service_set_sink_latency(uint8_t sink,uint32_t rate,uint32_t us,bool measured)
{
    if (sink>1 || (rate!=44100 && rate!=48000) || us>500000) return false;
    portENTER_CRITICAL(&s_clock_lock);
    s_calibration=(deck_sink_latency_t){.sink=sink,.sample_rate=rate,.latency_us=us,.measured=measured};
    portEXIT_CRITICAL(&s_clock_lock);return true;
}
static const deck_core_network_ops_t s_clock_ops={
    .read=clock_read,.publish=clock_publish,.follow=clock_follow,.master=clock_master};
static bool clock_send(void *ctx,uint16_t port,uint32_t ip,const uint8_t *buf,size_t len)
{
    dj_link_udp_t *t=ctx;
    return dj_link_udp_send(t,port,ip?ip:t->broadcast_ip,buf,len);
}
static void clock_tick(dj_link_udp_t *transport,uint32_t now,bool enabled)
{
    deck_net_local_snapshot_t local[2];uint8_t command;
    portENTER_CRITICAL(&s_clock_lock);
    memcpy(local,s_local,sizeof(local));command=s_clock_command;s_clock_command=0;
    portEXIT_CRITICAL(&s_clock_lock);
    for (unsigned i=0;i<2;++i) {
        uint32_t elapsed=now-local[i].captured_ms;
        if (elapsed>120) {local[i].grid=false;local[i].playing=false;}
        else if (local[i].grid && local[i].playing && !local[i].hold)
            local[i].bar=fmodf(local[i].bar+elapsed*local[i].bpm*(1+local[i].pitch/100)/60000,4);
    }
    dj_link_sync_tick(&s_sync,s_model,local,now,NULL,NULL);
    if (enabled) {
        if (command==1) dj_link_sync_follow(&s_sync);
        else if (command>=2) (void)dj_link_sync_master(&s_sync,s_model,command-2,now,clock_send,transport);
    }
    dj_link_sync_tick(&s_sync,s_model,local,now,clock_send,transport);
    portENTER_CRITICAL(&s_clock_lock);
    s_clock=s_sync.clock;s_local_master=s_sync.local_master;
    s_link_enabled=enabled;s_clock_ms=now;
    portEXIT_CRITICAL(&s_clock_lock);
}
static uint32_t now_ms(void);
static struct {
    bool active, sent, done, answered;
    uint32_t id;
    uint16_t request;
    uint8_t *dst;
    size_t cap,len;
} s_asset; /* protected by browse lock; buffer owned by waiting load worker */
static void asset_path(void *ctx,uint32_t id,const char *path)
{
    (void)ctx;
    if (!s_asset.active || s_asset.request || s_asset.id!=id) return;
    size_t n=strlen(path);
    s_asset.answered=n>0 && n<s_asset.cap;
    if (s_asset.answered) {memcpy(s_asset.dst,path,n+1);s_asset.len=n;}
    s_asset.done=true;
}
static void asset_blob(void *ctx,uint32_t id,uint16_t request,size_t len,bool answered)
{
    (void)ctx;
    if (!s_asset.active || s_asset.id!=id || s_asset.request!=request) return;
    s_asset.len=len;s_asset.answered=answered;s_asset.done=true;
}
bool dj_link_service_source(uint8_t peer,uint64_t epoch,dj_link_peer_t *out)
{
    if (!out || !s_snapshot_lock || xSemaphoreTake(s_snapshot_lock,pdMS_TO_TICKS(20))!=pdTRUE) return false;
    bool found=false;
    if (dj_link_discovery_number(s_snapshot,0)) for (unsigned i=0;i<DJ_LINK_PEERS;++i) {
        const dj_link_peer_t *p=&s_snapshot->peers[i];
        if (p->present && p->number==peer && p->source_epoch==epoch) {*out=*p;found=true;break;}
    }
    xSemaphoreGive(s_snapshot_lock);return found;
}
esp_err_t dj_link_service_read_asset(uint8_t peer,uint64_t epoch,uint8_t slot,
    uint32_t id,uint16_t request,void *dst,size_t cap,size_t *len,
    bool (*current)(void *),void *ctx)
{
    if (!dst || !cap || !len || !id || !s_browse_lock) return ESP_ERR_INVALID_ARG;
    *len=0;
    dj_link_peer_t source;
    if (!dj_link_service_source(peer,epoch,&source)) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_browse_lock,pdMS_TO_TICKS(40))!=pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_asset.active) {xSemaphoreGive(s_browse_lock);return ESP_ERR_INVALID_STATE;}
    /* Incoming requests may address a source other than the visible menu.
     * Reuse the sole session rather than opening a second dbserver client. */
    if (s_browse->status.peer_number!=peer || s_browse->status.source_epoch!=epoch ||
        s_browse->status.slot!=slot || s_browse->db.phase==DJ_LINK_DB_FAILED ||
        s_browse->db.phase==DJ_LINK_DB_IDLE) {
        xSemaphoreTake(s_snapshot_lock,portMAX_DELAY);
        dj_link_browse_start(s_browse,s_snapshot,peer,epoch,slot,DJ_LINK_DB_MENU_ALL_TRACKS,0,now_ms());
        xSemaphoreGive(s_snapshot_lock);
    }
    s_asset.active=true;s_asset.sent=s_asset.done=s_asset.answered=false;
    s_asset.id=id;s_asset.request=request;s_asset.dst=dst;s_asset.cap=cap;s_asset.len=0;
    xSemaphoreGive(s_browse_lock);
    uint32_t start=now_ms();esp_err_t rc=ESP_ERR_TIMEOUT;
    while ((uint32_t)(now_ms()-start)<30000 && (!current || current(ctx))) {
        if (xSemaphoreTake(s_browse_lock,pdMS_TO_TICKS(20))==pdTRUE) {
            if (s_asset.done) {rc=s_asset.answered?ESP_OK:ESP_ERR_NOT_FOUND;*len=s_asset.len;}
            else if (s_browse->db.phase==DJ_LINK_DB_FAILED ||
                     s_browse->status.state==DJ_LINK_BROWSE_UNAVAILABLE) rc=ESP_ERR_INVALID_STATE;
            xSemaphoreGive(s_browse_lock);
            if (rc!=ESP_ERR_TIMEOUT) break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    /* Must finish detaching before caller may free its destination. */
    xSemaphoreTake(s_browse_lock,portMAX_DELAY);
    dj_link_db_cancel_blob(&s_browse->db);
    if (!request) {s_browse->db.path_id=0;}
    memset(&s_asset,0,sizeof(s_asset));
    xSemaphoreGive(s_browse_lock);
    return current && !current(ctx) ? ESP_ERR_INVALID_STATE : rc;
}
typedef struct {
    uint64_t id, epoch;
    uint32_t menu_id;
    uint8_t peer, slot, menu;
} browse_command_t;
static QueueHandle_t s_commands, s_loads;
static uint64_t s_command_id;
static portMUX_TYPE s_command_lock = portMUX_INITIALIZER_UNLOCKED;
uint64_t dj_link_service_browse(uint8_t peer, uint64_t epoch, uint8_t slot,
    dj_link_db_menu_t menu, uint32_t menu_id)
{
    if (!s_commands || !peer || !epoch || menu > DJ_LINK_DB_MENU_PLAYLIST) return 0;
    portENTER_CRITICAL(&s_command_lock);
    uint64_t id = ++s_command_id;
    portEXIT_CRITICAL(&s_command_lock);
    browse_command_t cmd = {.id=id,.epoch=epoch,.menu_id=menu_id,.peer=peer,.slot=slot,.menu=(uint8_t)menu};
    return xQueueSend(s_commands,&cmd,0) == pdTRUE ? id : 0;
}
void dj_link_service_cancel_browse(void)
{
    if (!s_commands) return;
    /* Cancel supersedes pending menus rather than failing behind a full queue. */
    xQueueReset(s_commands);
    browse_command_t cmd = {0}; (void)xQueueSend(s_commands,&cmd,0);
}
bool dj_link_service_page(uint64_t request_id, uint32_t first, dj_link_browse_status_t *status,
    dj_link_peer_track_t rows[DJ_LINK_BROWSE_PAGE_ROWS])
{
    if (!status || !rows || !s_browse_lock || xSemaphoreTake(s_browse_lock,0)!=pdTRUE) return false;
    if (s_browse->status.request_id!=request_id) {
        xSemaphoreGive(s_browse_lock); return false;
    }
    (void)dj_link_browse_page(s_browse,first,status,rows);
    xSemaphoreGive(s_browse_lock); return true;
}
bool dj_link_service_take_load(dj_link_incoming_load_t *out)
{
    if (!out || !s_loads || xQueueReceive(s_loads,out,0)!=pdTRUE ||
        xSemaphoreTake(s_snapshot_lock,0)!=pdTRUE) return false;
    bool current=dj_link_browse_load_current(s_snapshot,out,(uint32_t)(esp_timer_get_time()/1000));
    xSemaphoreGive(s_snapshot_lock); return current;
}
static portMUX_TYPE s_summary_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    dj_link_phase_t phase;
    uint8_t numbers[2], peers;
    bool ready, socket_error;
} s_summary;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }
static void publish(bool socket_error)
{
    uint8_t peers = 0;
    for (unsigned i = 0; i < DJ_LINK_PEERS; ++i) peers += s_model->peers[i].present;
    portENTER_CRITICAL(&s_summary_lock);
    s_summary.phase = s_model->phase; s_summary.peers = peers;
    s_summary.numbers[0] = dj_link_discovery_number(s_model, 0);
    s_summary.numbers[1] = dj_link_discovery_number(s_model, 1);
    s_summary.ready = true; s_summary.socket_error = socket_error;
    portEXIT_CRITICAL(&s_summary_lock);
    if (xSemaphoreTake(s_snapshot_lock, 0) == pdTRUE) {
        *s_snapshot = *s_model;
        xSemaphoreGive(s_snapshot_lock);
    }
}
bool dj_link_service_snapshot(dj_link_discovery_t *out)
{
    if (!out || !s_snapshot_lock || xSemaphoreTake(s_snapshot_lock, 0) != pdTRUE) return false;
    *out = *s_snapshot; xSemaphoreGive(s_snapshot_lock); return true;
}
void dj_link_service_format_status(char *out, size_t cap)
{
    if (!out || !cap) return;
    portENTER_CRITICAL(&s_summary_lock);
    const dj_link_phase_t phase = s_summary.phase;
    const uint8_t a = s_summary.numbers[0], b = s_summary.numbers[1], peers = s_summary.peers;
    const bool ready = s_summary.ready, error = s_summary.socket_error;
    portEXIT_CRITICAL(&s_summary_lock);
    portENTER_CRITICAL(&s_clock_lock);
    uint8_t master=s_clock.player;bool fresh=s_clock.valid;
    deck_sink_latency_t latency=s_calibration;
    portEXIT_CRITICAL(&s_clock_lock);
    bool calibrated=latency.measured && latency.sink==(uint8_t)audio_engine_get_main_sink() &&
        latency.sample_rate==audio_engine_get_output_sample_rate();
    if (!app_settings_get().dj_link) snprintf(out, cap, "DJ LINK: OFF");
    else if (!ready || error) snprintf(out, cap, "DJ LINK: SERVICE ERROR");
    else if (phase == DJ_LINK_WAIT_IP) snprintf(out, cap, "DJ LINK: WAIT ETHERNET IP");
    else if (phase == DJ_LINK_OBSERVER) snprintf(out, cap, "DJ LINK: OBSERVER / NO PAIR");
    else if (phase == DJ_LINK_ACTIVE) snprintf(out, cap, "DJ LINK: #%u/#%u (%u peers) M:%u%s / LATENCY %s",
        a,b,peers,master,fresh?"":" WAIT",calibrated?"MEASURED":"UNMEASURED");
    else snprintf(out, cap, "DJ LINK: CLAIMING");
}
static void worker(void *unused)
{
    (void)unused;
    const uint16_t ports[3] = {50000,50001,50002};
    dj_link_udp_t transport = {.fd={-1,-1,-1}};
    uint32_t opened_ip = 0, opened_mask = 0, retry_at = 0;
    bool socket_error = false;
    dj_link_datagram_t packet;
    uint8_t tx[DJLINK_MAX_PACKET];
    for (;;) {
        esp_netif_t *netif = board_ethernet_netif();
        uint32_t now = now_ms();
        uint8_t mac[6] = {0}; esp_netif_ip_info_t info = {0};
        bool enabled = app_settings_get().dj_link != 0;
        bool available = enabled && netif && esp_netif_is_netif_up(netif) &&
            esp_netif_get_mac(netif, mac) == ESP_OK &&
            esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr && info.netmask.addr;
        uint32_t ip = available ? ntohl(info.ip.addr) : 0;
        uint32_t mask = available ? ntohl(info.netmask.addr) : 0;
        if (!available || (opened_ip && (opened_ip != ip || opened_mask != mask))) {
            dj_link_udp_close(&transport); opened_ip = opened_mask = 0;
            dj_link_discovery_configure(s_model, enabled, 0, mac, now);
            socket_error = false; retry_at = now;
        }
        if (available && !opened_ip && (int32_t)(now-retry_at) >= 0) {
            char name[IFNAMSIZ] = {0};
            /* This exact Ethernet netif owns every inbound/outbound socket.
             * No default route, AP, Wi-Fi netif or INADDR_ANY fallback binding. */
            if (esp_netif_get_netif_impl_name(netif, name) == ESP_OK &&
                dj_link_udp_open(&transport, name, ip, mask, ports) &&
                dj_link_tcp_bind(&s_tcp,name,ip)) {
                opened_ip = ip; opened_mask = mask; socket_error = false;
                dj_link_discovery_configure(s_model, true, ip, mac, now);
                ESP_LOGI(TAG, "Ethernet transport %s opened; claiming two players", name);
            } else {
                dj_link_udp_close(&transport); dj_link_tcp_close(&s_tcp);
                socket_error = true; retry_at = now + 2000;
                dj_link_discovery_configure(s_model, true, 0, mac, now);
                ESP_LOGW(TAG, "Ethernet transport refused: errno=%d", errno);
            }
        }
        if (opened_ip) {
            for (unsigned i = 0; i < 12; ++i) {
                int received = dj_link_udp_receive(&transport, &packet, i == 0 ? 40 : 0);
                if (received < 0) { socket_error = true; break; }
                if (!received) break;
                if (packet.port==DJLINK_PORT_BEAT && dj_link_sync_ingest(&s_sync,s_model,
                    packet.source_ip,packet.bytes,packet.len,now_ms(),clock_send,&transport)) continue;
                dj_link_incoming_load_t load;
                if (packet.port == DJLINK_PORT_STATUS &&
                    dj_link_browse_parse_load(s_model,packet.source_ip,packet.bytes,packet.len,&load)) {
                    /* No network ACK: the LVGL owner must accept a complete
                     * load through the shared gate first (J supplies audio). */
                    load.received_ms=now_ms(); (void)xQueueSend(s_loads,&load,0);
                } else (void)dj_link_discovery_ingest(s_model, packet.port, packet.bytes,
                    packet.len, packet.source_ip, now_ms());
            }
            now = now_ms();
            for (unsigned i = 0; i < 2 && !socket_error; ++i) {
                unsigned deck;
                int len = dj_link_discovery_poll(s_model, now, tx, sizeof(tx), &deck);
                if (len > 0 && !dj_link_udp_broadcast(&transport, tx, (size_t)len)) socket_error = true;
            }
            if (socket_error) {
                dj_link_udp_close(&transport); opened_ip = opened_mask = 0; retry_at = now+2000;
                dj_link_discovery_configure(s_model, enabled, 0, mac, now);
            }
        }
        dj_link_discovery_tick(s_model, now_ms());
        clock_tick(&transport,now_ms(),enabled);
        xSemaphoreTake(s_browse_lock,portMAX_DELAY);
        browse_command_t command;
        if (xQueueReceive(s_commands,&command,0)==pdTRUE) {
            if (s_asset.active) {s_asset.done=true;s_asset.answered=false;}
            if (!command.peer) dj_link_browse_cancel(s_browse);
            else {
                if (!dj_link_browse_start(s_browse,s_model,command.peer,command.epoch,
                        command.slot,(dj_link_db_menu_t)command.menu,command.menu_id,now_ms())) {
                    dj_link_browse_cancel(s_browse);
                    s_browse->status.state=DJ_LINK_BROWSE_UNAVAILABLE;
                    snprintf(s_browse->status.error,sizeof(s_browse->status.error),"SOURCE UNAVAILABLE");
                }
                s_browse->status.request_id=command.id;
            }
        }
        if (s_asset.active && !s_asset.sent && !s_asset.done &&
            s_browse->db.phase!=DJ_LINK_DB_FAILED && s_browse->db.phase!=DJ_LINK_DB_IDLE) {
            s_asset.sent=s_asset.request
                ? dj_link_db_want_blob(&s_browse->db,s_asset.request,s_asset.id,
                    s_asset.dst,s_asset.cap,now_ms())
                : dj_link_db_want_path(&s_browse->db,s_asset.id,now_ms());
            if (!s_asset.sent) s_asset.done=true;
        }
        /* Invalidate before delivering socket bytes and before publishing rows. */
        if (dj_link_browse_validate(s_browse,s_model)) dj_link_tcp_poll(&s_tcp,now_ms());
        dj_link_browse_poll(s_browse,s_model,now_ms());
        xSemaphoreGive(s_browse_lock);
        publish(socket_error);
        vTaskDelay(pdMS_TO_TICKS(opened_ip ? 1 : 40));
    }
}
esp_err_t dj_link_service_init(void)
{
    if (s_task) return ESP_OK;
    if (!board_ethernet_netif()) return ESP_ERR_INVALID_STATE;
    s_model = heap_caps_calloc(1, sizeof(*s_model), MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_snapshot = heap_caps_calloc(1, sizeof(*s_snapshot), MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_browse = heap_caps_calloc(1,sizeof(*s_browse),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_rows = heap_caps_calloc(DJ_LINK_DB_TRACK_LIMIT,sizeof(*s_rows),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_snapshot_lock = xSemaphoreCreateMutex();
    s_browse_lock = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(4,sizeof(browse_command_t));
    s_loads = xQueueCreate(1,sizeof(dj_link_incoming_load_t));
    if (!s_model || !s_snapshot || !s_snapshot_lock || !s_browse || !s_rows ||
        !s_browse_lock || !s_commands || !s_loads) goto fail;
    dj_link_db_io_t io = {.connect=dj_link_tcp_connect,.send=dj_link_tcp_send,
        .close=dj_link_tcp_close,.path=asset_path,.blob=asset_blob,.ctx=&s_tcp};
    dj_link_browse_init(s_browse,s_rows,&io); dj_link_tcp_init(&s_tcp,&s_browse->db);
    dj_link_discovery_init(s_model); *s_snapshot = *s_model;
    dj_link_sync_init(&s_sync);
    if (xTaskCreatePinnedToCoreWithCaps(worker, "dj_link_eth", 6144, NULL, 2, &s_task, 0,
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT) != pdPASS) goto fail;
    deck_core_set_network_ops(&s_clock_ops);
    return ESP_OK;
fail:
    if (s_snapshot_lock) vSemaphoreDelete(s_snapshot_lock);
    if (s_browse_lock) vSemaphoreDelete(s_browse_lock);
    if (s_commands) vQueueDelete(s_commands);
    if (s_loads) vQueueDelete(s_loads);
    heap_caps_free(s_browse); heap_caps_free(s_rows);
    heap_caps_free(s_model); heap_caps_free(s_snapshot);
    s_snapshot_lock = NULL; s_model = s_snapshot = NULL;
    s_browse_lock=NULL; s_browse=NULL; s_rows=NULL; s_commands=s_loads=NULL;
    return ESP_ERR_NO_MEM;
}
