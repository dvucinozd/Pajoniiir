#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "dj_link_cache.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#define fsync _commit
#endif

bool dj_link_normalize_path(const char *in, char out[256])
{
    if (!in || !out) return false;
    size_t n=0; out[n++]='/';
    while (*in) {
        while (*in=='/' || *in=='\\') ++in;
        const char *p=in;
        while (*in && *in!='/' && *in!='\\') {
            if ((unsigned char)*in<32 || *in==':') return false;
            ++in;
        }
        size_t len=(size_t)(in-p);
        if (!len || (len==1 && *p=='.')) continue;
        if (len==2 && p[0]=='.' && p[1]=='.') return false;
        if (n>1) { if (n>=255) return false; out[n++]='/'; }
        if (len>255-n) return false;
        memcpy(out+n,p,len); n+=len;
    }
    out[n]=0; return n>1;
}
bool dj_link_content_identity(const uint8_t export_digest[32],
    const uint8_t audio_digest[32], const char *path, uint64_t size,
    uint64_t mtime, media_persistent_id_t *out)
{
    char normal[256]; uint8_t proof[32]; media_sha256_ctx_t h;
    if (!out || !export_digest || !audio_digest || !dj_link_normalize_path(path,normal)) return false;
    media_sha256_init(&h);
    static const char domain[]="pajoniiir.link.content.v1";
    media_sha256_update(&h,domain,sizeof(domain)-1);
    media_sha256_update(&h,export_digest,32); media_sha256_update(&h,audio_digest,32);
    media_sha256_final(&h,proof);
    /* Preserve all 64 bits of sub-second timestamp through the existing ID
     * derivation's byte representation, without narrowing to time_t. */
    if (mtime>INT64_MAX) return false;
    return media_persistent_id_derive(proof,normal,size,(int64_t)mtime,out);
}
static bool current(dj_link_cache_t *c) { return !c->io.current || c->io.current(c->io.ctx); }
static bool lock(dj_link_cache_t *c) { return current(c) && (!c->io.begin || c->io.begin(c->io.ctx)); }
static void unlock(dj_link_cache_t *c) { if (c->io.end) c->io.end(c->io.ctx); }
static bool cleanup_lock(dj_link_cache_t *c)
{
    if(c->io.cleanup_begin) {c->io.cleanup_begin(c->io.ctx);return true;}
    return !c->io.begin || c->io.begin(c->io.ctx);
}
static void hex(const media_persistent_id_t *id, char out[65])
{
    static const char chars[]="0123456789abcdef";
    for (unsigned i=0;i<32;++i) {out[2*i]=chars[id->bytes[i]>>4];out[2*i+1]=chars[id->bytes[i]&15];}
    out[64]=0;
}
static bool extension(const char *s)
{
    return s && (!strcmp(s,"MP3") || !strcmp(s,"WAV") || !strcmp(s,"FLAC") ||
        !strcmp(s,"PDB") || !strcmp(s,"DAT") || !strcmp(s,"EXT") || !strcmp(s,"JPG"));
}
static bool paths(dj_link_cache_t *c,const media_persistent_id_t *id,const char *ext,
    char path[DJ_LINK_CACHE_PATH],char manifest[DJ_LINK_CACHE_PATH])
{
    if (!id || !id->valid || !extension(ext)) return false;
    char key[65];hex(id,key);
    int n=snprintf(path,DJ_LINK_CACHE_PATH,"%s/%s.%s",c->root,key,ext);
    int m=snprintf(manifest,DJ_LINK_CACHE_PATH,"%s/%s.%s.manifest",c->root,key,ext);
    return n>0 && n<DJ_LINK_CACHE_PATH && m>0 && m<DJ_LINK_CACHE_PATH;
}
static bool pinned(dj_link_cache_t *c,const char *name)
{
    if(c->active.valid) {
        char key[65];hex(&c->active,key);
        if(!strncmp(name,key,64) && name[64]=='.')return true;
    }
    for (unsigned i=0;i<2;++i) if (c->pins[i].valid) {
        char key[65];hex(&c->pins[i],key);
        if (!strncmp(name,key,64) && name[64]=='.') return true;
    }
    return false;
}
/* Enumerate one entry at a time. No directory-sized allocation; pruning is
 * outside the audio task and every operation yields the filesystem gate. */
static bool scan(dj_link_cache_t *c,uint64_t *used,char candidate[DJ_LINK_CACHE_PATH])
{
    *used=0;candidate[0]=0;
    if (!lock(c)) return false;
    DIR *dir=opendir(c->root);unlock(c); if (!dir) return false;
    bool ok=true;
    for (;;) {
        if (!lock(c)) {ok=false;break;}
        struct dirent *e=readdir(dir);
        if (!e) {unlock(c);break;}
        char path[DJ_LINK_CACHE_PATH];
        int n=snprintf(path,sizeof(path),"%s/%s",c->root,e->d_name);
        struct stat st;
        if (n>0 && n<(int)sizeof(path) && !stat(path,&st) && S_ISREG(st.st_mode)) {
            *used+=(uint64_t)st.st_size;
            if (!candidate[0] && strcmp(e->d_name,"download.part") && !pinned(c,e->d_name))
                memcpy(candidate,path,(size_t)n+1);
        }
        unlock(c);
    }
    /* Cleanup must be possible after cancellation; never leak a FAT handle. */
    if (cleanup_lock(c)) {closedir(dir);unlock(c);} else closedir(dir);
    return ok;
}
bool dj_link_cache_init(dj_link_cache_t *c,const char *root,const dj_link_cache_io_t *io,
    const media_persistent_id_t pins[2])
{
    if (!c || !root || strlen(root)>=sizeof(c->root) || !io ||
        (io->begin && !io->cleanup_begin)) return false;
    memset(c,0,sizeof(*c));strcpy(c->root,root);c->io=*io;c->budget=DJ_LINK_CACHE_BUDGET;
    if (pins) memcpy(c->pins,pins,sizeof(c->pins));
    if (!lock(c)) return false;
#ifdef _WIN32
    bool ok=mkdir(root)==0 || errno==EEXIST;
#else
    bool ok=mkdir(root,0775)==0 || errno==EEXIST;
#endif
    unlock(c);return ok;
}
bool dj_link_cache_begin(dj_link_cache_t *c,dj_link_cache_txn_t *t,uint64_t expected,uint64_t limit)
{
    if (!c || !t || !expected || expected>limit || expected>c->budget) return false;
    memset(t,0,sizeof(*t));t->cache=c;t->expected=expected;
    snprintf(t->part,sizeof(t->part),"%s/download.part",c->root);
    /* Recover an interrupted single-writer download. No incomplete hit. */
    if (!lock(c)) return false;
    (void)remove(t->part);unlock(c);
    for (;;) {
        uint64_t used,free=UINT64_MAX;char victim[DJ_LINK_CACHE_PATH];
        if (!scan(c,&used,victim) || (c->io.free_bytes && !c->io.free_bytes(c->io.ctx,&free))) return false;
        if (used+expected+128<=c->budget && free>=expected+DJ_LINK_CACHE_RESERVE+128) break;
        if (!victim[0] || !lock(c)) return false;
        bool ok=remove(victim)==0;unlock(c);if (!ok) return false;
    }
    if (!lock(c)) return false;
    t->file=fopen(t->part,"wb");unlock(c);media_sha256_init(&t->hash);return t->file!=NULL;
}
bool dj_link_cache_write(dj_link_cache_txn_t *t,const void *bytes,size_t len)
{
    if (!t || !t->file || !bytes || len>t->expected-t->written) return false;
    const uint8_t *p=bytes;
    while(len) {
        size_t n=len>4096?4096:len;
        if(!lock(t->cache))return false;
        bool ok=fwrite(p,1,n,t->file)==n;unlock(t->cache);
        if(!ok)return false;
        media_sha256_update(&t->hash,p,n);t->written+=n;p+=n;len-=n;
    }
    return true;
}
bool dj_link_cache_seal(dj_link_cache_txn_t *t)
{
    if (!t || !t->file || t->written!=t->expected || !lock(t->cache)) return false;
    bool ok=fflush(t->file)==0;
    if (ok) ok=fsync(fileno(t->file))==0;
    if (fclose(t->file)) ok=false;
    t->file=NULL;unlock(t->cache);
    if (!ok || !current(t->cache)) return false;
    media_sha256_final(&t->hash,t->digest);
    /* Hash bytes read back after flush/close, not merely the network bytes
     * supplied to fwrite. A failed/short/corrupt write cannot be published. */
    if(!lock(t->cache))return false;
    FILE *f=fopen(t->part,"rb");unlock(t->cache);if(!f)return false;
    media_sha256_ctx_t verify;media_sha256_init(&verify);
    uint8_t bytes[4096],digest[32];uint64_t done=0;
    while(ok && done<t->expected) {
        if(!lock(t->cache)){ok=false;break;}
        size_t n=t->expected-done>sizeof(bytes)?sizeof(bytes):(size_t)(t->expected-done);
        ok=fread(bytes,1,n,f)==n;unlock(t->cache);
        if(ok){media_sha256_update(&verify,bytes,n);done+=n;}
    }
    if(cleanup_lock(t->cache)) {
        if(ok)ok=fgetc(f)==EOF;
        if(fclose(f))ok=false;
        unlock(t->cache);
    } else {fclose(f);ok=false;}
    media_sha256_final(&verify,digest);
    t->sealed=ok && current(t->cache) && !memcmp(t->digest,digest,32);
    return t->sealed;
}
static void le64(uint8_t *p,uint64_t v) {for (unsigned i=0;i<8;++i) p[i]=(uint8_t)(v>>(i*8));}
static uint64_t rd64(const uint8_t *p) {uint64_t v=0;for (unsigned i=0;i<8;++i)v|=(uint64_t)p[i]<<(i*8);return v;}
/* Stable on-disk format: magic/version/completion, full ID, size, SHA, CRC
 * via SHA-256 of the preceding bytes. No C struct padding/endianness. */
#define MANIFEST_LEN 112u
bool dj_link_cache_hit(dj_link_cache_t *c,const media_persistent_id_t *id,const char *ext,char out[DJ_LINK_CACHE_PATH])
{
    char path[DJ_LINK_CACHE_PATH],meta[DJ_LINK_CACHE_PATH];uint8_t record[MANIFEST_LEN];
    if (!paths(c,id,ext,path,meta) || !lock(c)) return false;
    FILE *f=fopen(meta,"rb");bool ok=f && fread(record,1,sizeof(record),f)==sizeof(record) && fgetc(f)==EOF;
    if (f) fclose(f);
    unlock(c);if (!ok) return false;
    uint8_t hash[32];media_sha256(record,80,hash);
    if (memcmp(record,"DJLC\1\1\0\0",8) || memcmp(record+8,id->bytes,32) || memcmp(hash,record+80,32)) return false;
    uint64_t size=rd64(record+40),done=0;
    if (!size || size>c->budget || !lock(c)) return false;
    f=fopen(path,"rb");unlock(c);if (!f)return false;
    media_sha256_ctx_t h;media_sha256_init(&h);uint8_t block[4096];
    while (done<size && ok) {
        if (!lock(c)) {ok=false;break;}
        size_t n=(size-done)>sizeof(block)?sizeof(block):(size_t)(size-done);
        ok=fread(block,1,n,f)==n;unlock(c);
        if (ok) {media_sha256_update(&h,block,n);done+=n;}
    }
    if (cleanup_lock(c)) {
        if (ok) ok=fgetc(f)==EOF;
        fclose(f);unlock(c);
    } else {fclose(f);ok=false;}
    media_sha256_final(&h,hash);
    ok=ok && current(c) && !memcmp(hash,record+48,32);
    if (ok && out) strcpy(out,path);
    return ok;
}
bool dj_link_cache_commit(dj_link_cache_txn_t *t,const media_persistent_id_t *id,const char *ext,char out[DJ_LINK_CACHE_PATH])
{
    if (!t || !t->sealed || t->file || !t->expected || t->written!=t->expected || !current(t->cache)) return false;
    char path[DJ_LINK_CACHE_PATH],meta[DJ_LINK_CACHE_PATH],temp[DJ_LINK_CACHE_PATH];
    if (!paths(t->cache,id,ext,path,meta)) return false;
    if (dj_link_cache_hit(t->cache,id,ext,NULL) && lock(t->cache)) {
        uint8_t old[MANIFEST_LEN];FILE *f=fopen(meta,"rb");
        bool same=f && fread(old,1,sizeof(old),f)==sizeof(old) && !memcmp(old+48,t->digest,32);
        if (f) fclose(f);
        unlock(t->cache);
        if (same) {if(out)strcpy(out,path);dj_link_cache_abort(t);return true;}
    }
    /* Decks retain deep copies of analysis/artwork; only their open audio
     * artifact must never be replaced. Prune preserves the whole group. */
    if ((!strcmp(ext,"MP3") || !strcmp(ext,"WAV") || !strcmp(ext,"FLAC")) &&
        pinned(t->cache,strrchr(path,'/')+1)) return false;
    snprintf(temp,sizeof(temp),"%s/manifest.part",t->cache->root);
    uint8_t record[MANIFEST_LEN]={0};memcpy(record,"DJLC\1\1\0\0",8);
    memcpy(record+8,id->bytes,32);le64(record+40,t->expected);memcpy(record+48,t->digest,32);
    media_sha256(record,80,record+80);
    if (!lock(t->cache)) return false;
    /* Withdraw old completion before replacing an unpinned artifact. */
    (void)remove(meta);(void)remove(path);
    bool ok=rename(t->part,path)==0;
    FILE *f=ok?fopen(temp,"wb"):NULL;
    ok=f && fwrite(record,1,sizeof(record),f)==sizeof(record) && fflush(f)==0 && fsync(fileno(f))==0;
    if (f && fclose(f)) ok=false;
    if (ok) ok=rename(temp,meta)==0;
    if (!ok) {(void)remove(temp);(void)remove(meta);}
    unlock(t->cache);
    if (ok && current(t->cache)) {if(out)strcpy(out,path);return true;}return false;
}
void dj_link_cache_abort(dj_link_cache_txn_t *t)
{
    if (!t || !t->cache) return;
    if (cleanup_lock(t->cache)) {
        if (t->file) fclose(t->file);
        t->file=NULL;(void)remove(t->part);unlock(t->cache);
    } else if (t->file) {fclose(t->file);t->file=NULL;}
}
