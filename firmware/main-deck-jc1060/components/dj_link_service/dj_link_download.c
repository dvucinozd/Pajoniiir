/* NFS codec/PDB walker from kayrozen 428b97dd (MIT). Transaction, identity,
 * SD admission and load lifecycle integration are Pajoniiir implementations. */
#include "dj_link_download.h"
#include "dj_link_cache.h"
#include "dj_link_pdb.h"
#include "dj_link_anlz.h"
#include "djlink/nfs.h"
#include "djlink/dbserver.h"
#include "board_ethernet.h"
#include "board_adapter.h"
#include "sd_io_gate.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define CACHE_ROOT "/sd/djlcache-v1"
#define ANALYSIS_LIMIT (1024u*1024u)
typedef struct {
    dj_link_remote_track_t ref;
    dj_link_download_io_t io;
    dj_link_cache_t cache;
    dj_link_cache_txn_t txn;
    djlink_nfs_t nfs;
    dj_link_peer_t peer;
    int fd;
    uint64_t limit;
    uint8_t window[4*DJLINK_NFS_READ_DEFAULT];
    uint8_t packet[2048];
    uint8_t page[DJ_LINK_PDB_PAGE_MAX];
    size_t staged;
} download_t;
static uint32_t millis(void) {return (uint32_t)(esp_timer_get_time()/1000);}
static bool valid(void *ctx)
{
    download_t *d=ctx;dj_link_peer_t peer;
    return bsp_sd_is_mounted() && (!d->io.current || d->io.current(d->io.ctx)) &&
        dj_link_service_source(d->ref.peer,d->ref.source_epoch,&peer) && peer.ip==d->peer.ip;
}
static bool gate(void *ctx) {(void)ctx;return sd_io_gate_try_begin(50);}
static void cleanup_gate(void *ctx) {(void)ctx;sd_io_gate_begin();}
static void ungate(void *ctx) {(void)ctx;sd_io_gate_end();}
static bool free_bytes(void *ctx,uint64_t *bytes)
{
    if (!valid(ctx) || !gate(ctx)) return false;
    bsp_sd_status_t s;bool ok=bsp_sd_get_status(&s)==ESP_OK && s.mounted;
    if(ok)*bytes=s.free_bytes;
    ungate(ctx);return ok;
}
static int send_packet(void *ctx,uint32_t ip,uint16_t port,const uint8_t *bytes,size_t n)
{
    download_t *d=ctx;struct sockaddr_in addr={.sin_family=AF_INET,
        .sin_addr.s_addr=htonl(ip),.sin_port=htons(port)};
    return sendto(d->fd,bytes,n,0,(struct sockaddr *)&addr,sizeof(addr))==(int)n ? 0:-1;
}
static int open_file(void *ctx,uint32_t size)
{
    download_t *d=ctx;return dj_link_cache_begin(&d->cache,&d->txn,size,d->limit)?0:-1;
}
static int write_file(void *ctx,uint32_t off,const uint8_t *bytes,size_t len)
{
    download_t *d=ctx;
    return off==d->txn.written && dj_link_cache_write(&d->txn,bytes,len)?0:-1;
}
static void progress(void *ctx,uint32_t done,uint32_t total)
{
    download_t *d=ctx;if(d->io.progress)d->io.progress(d->io.ctx,done,total);
}
static bool fetch(download_t *d,const char *path,uint64_t limit)
{
    d->limit=limit;dj_link_cache_abort(&d->txn);memset(&d->txn,0,sizeof(d->txn));
    const djlink_nfs_io_t io={.ctx=d,.send=send_packet,.open=open_file,.write=write_file,.progress=progress};
    const djlink_nfs_fetch_cfg_t cfg={.host_ip=d->peer.ip,
        .export_path=d->ref.slot==DJLINK_SLOT_SD?DJLINK_NFS_EXPORT_SD:DJLINK_NFS_EXPORT_USB,
        .path=path,.charset=DJLINK_NFS_NAMES_UTF16LE,
        .portmap_port=d->ref.slot==DJLINK_SLOT_LAPTOP?50111:0,
        .read_size=DJLINK_NFS_READ_DEFAULT,.window=4,.window_buf=d->window,
        .window_buf_len=sizeof(d->window),.max_size=(uint32_t)limit,.xid_seed=esp_random()};
    if (!valid(d) || djlink_nfs_fetch(&d->nfs,&cfg,&io,millis())!=DJLINK_NFS_E_NONE) return false;
    while (djlink_nfs_state(&d->nfs)==DJLINK_NFS_BUSY && valid(d)) {
        struct sockaddr_in src;socklen_t len=sizeof(src);
        int n=recvfrom(d->fd,d->packet,sizeof(d->packet),0,(struct sockaddr *)&src,&len);
        if(n>0 && n<(int)sizeof(d->packet) && ntohl(src.sin_addr.s_addr)==d->peer.ip) {
            uint16_t port=ntohs(src.sin_port);
            if(port==111 || port==50111 || port==d->nfs.mount_port || port==d->nfs.nfs_port)
                djlink_nfs_on_datagram(&d->nfs,d->packet,(size_t)n,millis());
        } else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) break;
        djlink_nfs_poll(&d->nfs,millis());vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!valid(d) || djlink_nfs_state(&d->nfs)!=DJLINK_NFS_DONE) {
        djlink_nfs_cancel(&d->nfs);dj_link_cache_abort(&d->txn);return false;
    }
    return dj_link_cache_seal(&d->txn);
}
static bool read_pdb(void *ctx,uint32_t offset,uint8_t *dst,size_t len)
{
    FILE *f=ctx;bool ok=true;
    for(size_t done=0;done<len && ok;) {
        size_t n=len-done>4096?4096:len-done;
        if(!sd_io_gate_try_begin(50))return false;
        ok=fseek(f,(long)(offset+done),SEEK_SET)==0 && fread(dst+done,1,n,f)==n;
        sd_io_gate_end();done+=n;
    }
    return ok;
}
static bool network(download_t *d)
{
    esp_netif_t *netif=board_ethernet_netif();char iface[IFNAMSIZ]={0};esp_netif_ip_info_t info;
    if (!netif || esp_netif_get_netif_impl_name(netif,iface)!=ESP_OK ||
        esp_netif_get_ip_info(netif,&info)!=ESP_OK || !info.ip.addr) return false;
    d->fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(d->fd<0)return false;
    struct ifreq bindif={0};memcpy(bindif.ifr_name,iface,sizeof(bindif.ifr_name));
    struct timeval timeout={.tv_usec=20000};
    struct sockaddr_in local={.sin_family=AF_INET,.sin_addr.s_addr=info.ip.addr};
    return !setsockopt(d->fd,SOL_SOCKET,SO_BINDTODEVICE,&bindif,sizeof(bindif)) &&
        !setsockopt(d->fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)) &&
        !setsockopt(d->fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) &&
        !bind(d->fd,(struct sockaddr *)&local,sizeof(local));
}
static esp_err_t parse_artifact(download_t *d,const char *path,anlz_metadata_t *meta,bool ext)
{
    if(!valid(d) || !gate(d))return ESP_ERR_INVALID_STATE;
    FILE *f=fopen(path,"rb");long size=f && !fseek(f,0,SEEK_END)?ftell(f):-1;
    if(f)rewind(f);
    ungate(d);if(!f)return ESP_ERR_NOT_FOUND;
    uint8_t *bytes=size>0 && size<=ANALYSIS_LIMIT?heap_caps_malloc((size_t)size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    bool ok=bytes!=NULL;
    for(size_t off=0;ok && off<(size_t)size;) {
        size_t n=(size_t)size-off>4096?4096:(size_t)size-off;
        if(!valid(d) || !gate(d)){ok=false;break;}
        ok=fread(bytes+off,1,n,f)==n;ungate(d);off+=n;
    }
    sd_io_gate_begin();fclose(f);sd_io_gate_end();
    FILE *ram=ok?fmemopen(bytes,(size_t)size,"rb"):NULL;
    esp_err_t rc=ram?(ext?anlz_parse_ext_stream(ram,meta):anlz_parse_dat_stream(ram,meta)):ESP_FAIL;
    if(ram)fclose(ram);
    heap_caps_free(bytes);return rc;
}
static bool write_asset(void *ctx,const void *bytes,size_t n)
{
    download_t *d=ctx;const uint8_t *p=bytes;
    while(n) {
        size_t count=n<4096-d->staged?n:4096-d->staged;
        memcpy(d->page+d->staged,p,count);d->staged+=count;p+=count;n-=count;
        if(d->staged==4096) {
            if(!dj_link_cache_write(&d->txn,d->page,d->staged))return false;
            d->staged=0;
        }
    }
    return true;
}
static bool seal_asset(download_t *d)
{
    bool ok=!d->staged || dj_link_cache_write(&d->txn,d->page,d->staged);
    d->staged=0;return ok && dj_link_cache_seal(&d->txn);
}
static bool count_asset(void *ctx,const void *bytes,size_t n)
{
    (void)bytes;*(size_t *)ctx+=n;return true;
}
static bool begin_asset(download_t *d,size_t n)
{
    /* A previous optional asset may have failed while its writer was open. */
    dj_link_cache_abort(&d->txn);d->staged=0;
    return dj_link_cache_begin(&d->cache,&d->txn,n,ANALYSIS_LIMIT);
}
static bool store_blob(download_t *d,const media_persistent_id_t *id,const char *ext,
    const uint8_t *bytes,size_t n,char path[DJ_LINK_CACHE_PATH])
{
    return n && begin_asset(d,n) &&
        dj_link_cache_write(&d->txn,bytes,n) && dj_link_cache_seal(&d->txn) &&
        dj_link_cache_commit(&d->txn,id,ext,path);
}
static void db_artwork(download_t *d,media_loaded_track_t *loaded)
{
    if(!d->ref.track.artwork_id || !valid(d))return;
    uint8_t *bytes=heap_caps_malloc(65536,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!bytes)return;
    size_t n=0;
    if(dj_link_service_read_asset(d->ref.peer,d->ref.source_epoch,d->ref.slot,
        d->ref.track.artwork_id,DJLINK_DB_TYPE_ARTWORK_REQUEST,bytes,65536,&n,valid,d)==ESP_OK)
        (void)store_blob(d,&loaded->persistent_id,"JPG",bytes,n,loaded->artwork_path);
    heap_caps_free(bytes);
}
static void db_analysis(download_t *d,media_loaded_track_t *loaded)
{
    const size_t cap=DJ_LINK_ANLZ_WAVE_MAX+DJ_LINK_ANLZ_GRID_MAX+8192+DJ_LINK_ANLZ_COLOR_BLOB_MAX;
    uint8_t *buffer=heap_caps_calloc(1,cap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buffer)return;
    uint8_t *wave=buffer,*grid=wave+DJ_LINK_ANLZ_WAVE_MAX,*cues=grid+DJ_LINK_ANLZ_GRID_MAX,*color=cues+8192;
    size_t wn=0,gn=0,cn=0,col_n=0;bool cues_answered=false;
    uint8_t peer=d->ref.peer,slot=d->ref.slot;uint64_t epoch=d->ref.source_epoch;uint32_t id=d->ref.track.rekordbox_id;
    (void)dj_link_service_read_asset(peer,epoch,slot,id,DJLINK_DB_TYPE_WAVEFORM_REQUEST,wave,DJ_LINK_ANLZ_WAVE_MAX,&wn,valid,d);
    (void)dj_link_service_read_asset(peer,epoch,slot,id,DJLINK_DB_TYPE_BEATGRID_REQUEST,grid,DJ_LINK_ANLZ_GRID_MAX,&gn,valid,d);
    cues_answered=dj_link_service_read_asset(peer,epoch,slot,id,DJLINK_DB_TYPE_CUES_EXT_REQUEST,cues,8192,&cn,valid,d)==ESP_OK;
    (void)dj_link_service_read_asset(peer,epoch,slot,id,DJ_LINK_DB_TYPE_ANLZ_TAG_REQUEST,color,DJ_LINK_ANLZ_COLOR_BLOB_MAX,&col_n,valid,d);
    const uint8_t *ce=NULL;size_t ce_n=0;(void)dj_link_anlz_color_entries(color,col_n,&ce,&ce_n);
    if(valid(d)) {
        size_t n=0;
        if(dj_link_anlz_write_dat(count_asset,&n,loaded->audio_path,grid,gn,wave,wn,cues_answered?cues:NULL,cn) &&
            begin_asset(d,n) &&
            dj_link_anlz_write_dat(write_asset,d,loaded->audio_path,grid,gn,wave,wn,cues_answered?cues:NULL,cn) &&
            seal_asset(d))
            (void)dj_link_cache_commit(&d->txn,&loaded->persistent_id,"DAT",loaded->dat_path);
        n=0;d->staged=0;
        if(wn && dj_link_anlz_write_ext(count_asset,&n,wave,wn,ce,ce_n) &&
            begin_asset(d,n) &&
            dj_link_anlz_write_ext(write_asset,d,wave,wn,ce,ce_n) && seal_asset(d))
            (void)dj_link_cache_commit(&d->txn,&loaded->persistent_id,"EXT",loaded->ext_path);
    }
    dj_link_cache_abort(&d->txn);
    heap_caps_free(buffer);
}
esp_err_t dj_link_download_prepare(const dj_link_remote_track_t *ref,
    const media_persistent_id_t pins[2],const dj_link_download_io_t *io,
    media_catalog_track_t *item,media_loaded_track_t *loaded,anlz_metadata_t *meta)
{
    if(!ref || !io || !item || !loaded || !meta || !ref->track.rekordbox_id ||
        (ref->slot!=DJLINK_SLOT_SD && ref->slot!=DJLINK_SLOT_USB && ref->slot!=DJLINK_SLOT_LAPTOP)) return ESP_ERR_INVALID_ARG;
    memset(item,0,sizeof(*item));memset(loaded,0,sizeof(*loaded));memset(meta,0,sizeof(*meta));
    if(!sd_io_gate_reserve(SD_ACTIVITY_DOWNLOAD))return ESP_ERR_INVALID_STATE;
    esp_err_t rc=ESP_FAIL;download_t *d=heap_caps_calloc(1,sizeof(*d),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!d){sd_io_gate_release(SD_ACTIVITY_DOWNLOAD);return ESP_ERR_NO_MEM;}
    d->fd=-1;d->ref=*ref;d->io=*io;
    dj_link_pdb_track_t track={0};uint8_t export_hash[32]={0},session_nonce[32];
    /* NFS attributes/PDB digest cannot prove volume identity. Treat every
     * fresh mount/load as a new session, including after reboot/reconnect.
     * Source-local edits cannot be migrated between such unidentified media. */
    esp_fill_random(session_nonce,sizeof(session_nonce));
    const dj_link_cache_io_t cio={.ctx=d,.current=valid,.begin=gate,.cleanup_begin=cleanup_gate,
        .end=ungate,.free_bytes=free_bytes};
    if (!dj_link_service_source(ref->peer,ref->source_epoch,&d->peer) ||
        !valid(d) || !network(d) || !dj_link_cache_init(&d->cache,CACHE_ROOT,&cio,pins)) goto done;
    if (ref->slot==DJLINK_SLOT_LAPTOP) {
        size_t n;
        if (dj_link_service_read_asset(ref->peer,ref->source_epoch,ref->slot,ref->track.rekordbox_id,
            0,track.file_path,sizeof(track.file_path),&n,valid,d)!=ESP_OK) goto done;
        /* No export identity available: no hit before full audio verification. */
    } else {
        if (!fetch(d,"PIONEER/rekordbox/export.pdb",DJ_LINK_PDB_LIMIT) &&
            !fetch(d,".PIONEER/rekordbox/export.pdb",DJ_LINK_PDB_LIMIT)) goto done;
        memcpy(export_hash,d->txn.digest,32);
        media_persistent_id_t pdb_id={.valid=true};memcpy(pdb_id.bytes,export_hash,32);
        char pdb_path[DJ_LINK_CACHE_PATH];uint32_t size=(uint32_t)d->txn.expected;
        if(!dj_link_cache_commit(&d->txn,&pdb_id,"PDB",pdb_path) || !gate(d))goto done;
        FILE *f=fopen(pdb_path,"rb");ungate(d);if(!f)goto done;
        dj_link_pdb_result_t found=dj_link_pdb_find_track(read_pdb,f,size,ref->track.rekordbox_id,d->page,sizeof(d->page),&track);
        sd_io_gate_begin();fclose(f);sd_io_gate_end();
        if(found!=DJ_LINK_PDB_FOUND || !valid(d))goto done;
    }
    char normal[256],ext[8];
    if(!dj_link_normalize_path(track.file_path,normal))goto done;
    dj_link_pdb_extension(normal,ext,sizeof(ext));
    if(strcmp(ext,"MP3") && strcmp(ext,"WAV") && strcmp(ext,"FLAC")) {rc=ESP_ERR_NOT_SUPPORTED;goto done;}
    if(!fetch(d,normal,DJ_LINK_CACHE_BUDGET))goto done;
    uint64_t mtime=(uint64_t)d->nfs.attr.mtime_s*1000000+d->nfs.attr.mtime_us;
    media_sha256_ctx_t identity;
    media_sha256_init(&identity);media_sha256_update(&identity,export_hash,32);
    media_sha256_update(&identity,session_nonce,32);media_sha256_final(&identity,export_hash);
    if(!dj_link_content_identity(export_hash,d->txn.digest,normal,d->txn.expected,mtime,&loaded->persistent_id) ||
        !dj_link_cache_commit(&d->txn,&loaded->persistent_id,ext,loaded->audio_path))goto done;
    d->cache.active=loaded->persistent_id;
    loaded->source=MEDIA_SOURCE_DJ_LINK;
    memcpy(&loaded->track_key,loaded->persistent_id.bytes,sizeof(loaded->track_key));
    if(!loaded->track_key)loaded->track_key=1;
    item->track_key=loaded->track_key;item->rekordbox_track_id=ref->track.rekordbox_id;
    const char *title=track.title[0]?track.title:ref->track.title;
    if(!title[0])title=strrchr(normal,'/')+1;
    snprintf(item->title,sizeof(item->title),"%s",title);
    snprintf(item->artist,sizeof(item->artist),"%s",ref->track.artist);
    item->bpm=(uint16_t)(((track.bpm100?track.bpm100:ref->track.bpm100)+50)/100);
    item->duration_ms=(uint32_t)(track.duration_s?track.duration_s:ref->track.duration_s)*1000;
    loaded->bpm=item->bpm;loaded->duration_ms=item->duration_ms;
    /* Missing analysis never blocks audio. Every asset is attached only to
     * the confirmed full audio identity; no old track-id sidecars are used. */
    if(track.anlz_path[0] && valid(d)) {
        if(fetch(d,track.anlz_path,ANALYSIS_LIMIT))
            (void)dj_link_cache_commit(&d->txn,&loaded->persistent_id,"DAT",loaded->dat_path);
        char *dot=strrchr(track.anlz_path,'.');
        if(dot && strlen(dot)==4) {
            memcpy(dot,".EXT",5);
            if(fetch(d,track.anlz_path,ANALYSIS_LIMIT))
                (void)dj_link_cache_commit(&d->txn,&loaded->persistent_id,"EXT",loaded->ext_path);
        }
    }
    if(!loaded->dat_path[0] && valid(d))db_analysis(d,loaded);
    db_artwork(d,loaded);
    if(loaded->dat_path[0] && valid(d)) {
        if(parse_artifact(d,loaded->dat_path,meta,false)==ESP_OK && loaded->ext_path[0])
            (void)parse_artifact(d,loaded->ext_path,meta,true);
        /* Reject analysis for another track, even when the remote PDB points
         * at a valid-looking DAT. DB-generated DAT uses the verified local path. */
        char analysis_audio[256];
        if(meta->audio_path[0] && strcmp(meta->audio_path,loaded->audio_path) &&
            (!dj_link_normalize_path(meta->audio_path,analysis_audio) || strcmp(analysis_audio,normal)))
            {anlz_free(meta);memset(meta,0,sizeof(*meta));}
    }
    loaded->bpm=meta->bpm?meta->bpm:loaded->bpm;
    loaded->analysis_span_ms=anlz_analysis_span_ms(loaded->duration_ms,meta->waveform_span_ms);
    memcpy(loaded->waveform_low,meta->waveform_low,sizeof(loaded->waveform_low));loaded->has_waveform=meta->has_waveform_low;
    memcpy(loaded->pvbr,meta->vbr,sizeof(loaded->pvbr));loaded->has_pvbr=meta->has_vbr;
    rc=valid(d)?ESP_OK:ESP_ERR_INVALID_STATE;
done:
    dj_link_cache_abort(&d->txn);if(d->fd>=0)close(d->fd);heap_caps_free(d);
    if(rc!=ESP_OK)anlz_free(meta);
    sd_io_gate_release(SD_ACTIVITY_DOWNLOAD);return rc;
}
