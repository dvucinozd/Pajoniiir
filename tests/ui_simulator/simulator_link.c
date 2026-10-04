/* UI boundary mock: the actual codec/transport/cache have separate portable
 * and real socket sanitizer suites. No socket work runs in LVGL tests. */
#include "dj_link_service.h"
#include <string.h>
#include <stdio.h>
#include "lvgl.h"
static dj_link_browse_status_t s_status;
static bool s_current=true, s_incoming;
static uint32_t s_ready_at;
static uint32_t s_first;
static bool s_waiting_command;
static dj_link_incoming_load_t s_load;
void ui_link_mock_available(bool available) { s_current=available; ++s_status.revision; }
void ui_link_mock_empty(void) { s_status.state=DJ_LINK_BROWSE_READY; s_status.count=s_status.total=0; ++s_status.revision; }
void ui_link_mock_error(void) {
    s_status.state=DJ_LINK_BROWSE_FAILED; s_status.count=0; ++s_status.revision;
    snprintf(s_status.error,sizeof(s_status.error),"PARTIAL LIST");
}
void ui_link_mock_load(uint8_t deck) { s_incoming=true; s_load.deck=deck; }
uint32_t ui_link_mock_visible_first(void) { return s_first; }
void ui_link_mock_pending(bool pending) { s_waiting_command=pending; }
bool dj_link_service_snapshot(dj_link_discovery_t *out)
{
    memset(out,0,sizeof(*out)); out->phase=DJ_LINK_ACTIVE; out->numbers[0]=3; out->numbers[1]=4;
    if (s_current) out->peers[0]=(dj_link_peer_t){.present=true,.number=1,
        .device_type=3,.ip=0xc0a80102,.source_epoch=12};
    return true;
}
uint64_t dj_link_service_browse(uint8_t peer,uint64_t epoch,uint8_t slot,dj_link_db_menu_t menu,uint32_t id)
{
    if (!s_current || peer!=1 || epoch!=12 || slot!=DJLINK_SLOT_LAPTOP) return 0;
    s_status.request_id++; s_status.source_epoch=epoch; s_status.peer_number=peer;
    s_status.slot=slot; s_status.menu=(uint8_t)menu; s_status.menu_id=id;
    s_status.state=DJ_LINK_BROWSE_LOADING; s_status.count=s_status.received=0;
    s_status.total=menu==DJ_LINK_DB_MENU_ALL_TRACKS ? 2023 : menu==DJ_LINK_DB_MENU_FOLDER ? 2 : 3;
    ++s_status.revision; s_ready_at=lv_tick_get()+200; return s_status.request_id;
}
void dj_link_service_cancel_browse(void) { s_status.state=DJ_LINK_BROWSE_IDLE; }
bool dj_link_service_take_load(dj_link_incoming_load_t *out)
{ if (!s_incoming) return false; *out=s_load; s_incoming=false; return true; }
bool dj_link_service_page(uint64_t request_id,uint32_t first,dj_link_browse_status_t *status,dj_link_peer_track_t rows[8])
{
    /* Firmware checks the command ID under the cache mutex before touching
     * output bytes. Simulate a worker still finishing an earlier menu. */
    if (s_waiting_command || s_status.request_id!=request_id) return false;
    s_first=first; memset(rows,0,8*sizeof(*rows));
    if (!s_current) {
        s_status.state=DJ_LINK_BROWSE_UNAVAILABLE; s_status.count=0;
        snprintf(s_status.error,sizeof(s_status.error),"SOURCE UNAVAILABLE");
    } else if ((int32_t)(lv_tick_get()-s_ready_at)<0) { s_status.received=64; }
    else if (s_status.state==DJ_LINK_BROWSE_LOADING) {
        s_status.state=DJ_LINK_BROWSE_READY;
        s_status.count=s_status.total>2000 ? 2000 : s_status.total;
        ++s_status.revision;
    }
    *status=s_status;
    if (s_status.state!=DJ_LINK_BROWSE_READY) return true;
    for (uint32_t i=0;i<8 && first+i<s_status.count;++i) {
        rows[i].rekordbox_id=first+i+1; rows[i].has_detail=true;
        rows[i].bpm100=12345; rows[i].duration_s=245;
        snprintf(rows[i].title,sizeof(rows[i].title),"Remote track %u",first+i+1);
        snprintf(rows[i].artist,sizeof(rows[i].artist),"Network artist");
        if (s_status.menu==DJ_LINK_DB_MENU_FOLDER) {
            rows[i].kind=i ? DJ_LINK_PEER_ROW_PLAYLIST : DJ_LINK_PEER_ROW_FOLDER;
            snprintf(rows[i].title,sizeof(rows[i].title),"%s",i ? "Set playlist" : "Subfolder");
            rows[i].rekordbox_id=s_status.menu_id+first+i+1;
        }
    }
    return true;
}
