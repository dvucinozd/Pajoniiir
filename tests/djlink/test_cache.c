#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "dj_link_cache.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
static bool available=true;
static bool gate_available=true;
static unsigned cleanups;
static bool begin(void *p) {(void)p;return gate_available;}
static void cleanup_begin(void *p) {(void)p;cleanups++;}
static void end(void *p) {(void)p;}
static uint64_t space=UINT64_MAX;
static bool current(void *p) {(void)p;return available;}
static bool free_bytes(void *p,uint64_t *out) {(void)p;*out=space;return true;}
static void clear(const char *root)
{
    DIR *d=opendir(root);if(!d)return;struct dirent *e;char p[272];
    while((e=readdir(d))) if(e->d_name[0]!='.') {
        snprintf(p,sizeof(p),"%s/%s",root,e->d_name);assert(!remove(p));
    }
    closedir(d);assert(!rmdir(root));
}
static void commit(dj_link_cache_t *c,const media_persistent_id_t *id,const char *ext,const char *data,char *out)
{
    dj_link_cache_txn_t t;assert(dj_link_cache_begin(c,&t,strlen(data),1024));
    assert(dj_link_cache_write(&t,data,strlen(data)));assert(dj_link_cache_seal(&t));
    assert(dj_link_cache_commit(&t,id,ext,out));
}
int main(void)
{
    const char *root="cache-test-files";clear(root);
    uint8_t export_hash[32]={1},a_hash[32],b_hash[32];
    media_sha256("AAA",3,a_hash);media_sha256("BBB",3,b_hash);
    media_persistent_id_t a,b,same;char path[272],normal[256];
    assert(dj_link_content_identity(export_hash,a_hash,"//Contents/./track.MP3",3,123,&a));
    assert(dj_link_content_identity(export_hash,a_hash,"Contents\\track.MP3",3,123,&same));
    assert(media_persistent_id_equal(&a,&same));
    /* Same IP/player/rekordbox id/path/extension/size/mtime and even PDB.
     * Complete audio proof separates replacement media and all sidecars. */
    assert(dj_link_content_identity(export_hash,b_hash,"/Contents/track.MP3",3,123,&b));
    assert(!media_persistent_id_equal(&a,&b));
    assert(!dj_link_normalize_path("/Contents/../outside",normal));
    assert(!dj_link_normalize_path("C:\\track.MP3",normal));
    char long_path[300];memset(long_path,'x',299);long_path[299]=0;
    assert(!dj_link_normalize_path(long_path,normal));
    dj_link_cache_io_t io={.current=current,.free_bytes=free_bytes,.begin=begin,
        .cleanup_begin=cleanup_begin,.end=end};dj_link_cache_t c;
    assert(dj_link_cache_init(&c,root,&io,NULL));
    commit(&c,&a,"MP3","AAA",path);commit(&c,&a,"DAT","A cues",NULL);commit(&c,&a,"JPG","A art",NULL);
    assert(!dj_link_cache_hit(&c,&b,"MP3",path));
    assert(!dj_link_cache_hit(&c,&b,"DAT",path));assert(!dj_link_cache_hit(&c,&b,"JPG",path));
    commit(&c,&b,"MP3","BBB",path);assert(dj_link_cache_hit(&c,&b,"MP3",path));
    FILE *f=fopen(path,"rb");char data[4]={0};assert(f && fread(data,1,3,f)==3);fclose(f);assert(!strcmp(data,"BBB"));
    f=fopen(path,"wb");assert(f && fwrite("BAD",1,3,f)==3);fclose(f);
    assert(!dj_link_cache_hit(&c,&b,"MP3",NULL)); /* same-length corruption */
    commit(&c,&b,"MP3","BBB",path);
    char meta[300];snprintf(meta,sizeof(meta),"%s.manifest",path);f=fopen(meta,"r+b");
    assert(f);assert(!fseek(f,8,SEEK_SET));assert(fputc(0x55,f)!=EOF);fclose(f);
    assert(!dj_link_cache_hit(&c,&b,"MP3",NULL)); /* wrong ID/checksum */
    commit(&c,&b,"MP3","BBB",path);
    dj_link_cache_txn_t t;assert(dj_link_cache_begin(&c,&t,6,1024));
    assert(dj_link_cache_write(&t,"AAA",3));assert(!dj_link_cache_seal(&t));
    assert(!dj_link_cache_commit(&t,&a,"MP3",NULL));dj_link_cache_abort(&t);
    assert(dj_link_cache_hit(&c,&a,"MP3",NULL)); /* cancelled leaves prior deck */
    assert(dj_link_cache_begin(&c,&t,3,1024));assert(dj_link_cache_write(&t,"AAA",3));
    assert(!close(fileno(t.file))); /* force write/flush failure on the real FILE */
    assert(!dj_link_cache_seal(&t));assert(!dj_link_cache_commit(&t,&a,"MP3",NULL));dj_link_cache_abort(&t);
    assert(dj_link_cache_begin(&c,&t,3,1024));available=false;
    assert(!dj_link_cache_write(&t,"AAA",3));gate_available=false;unsigned before=cleanups;
    dj_link_cache_abort(&t);assert(cleanups==before+1 && !t.file);
    available=gate_available=true;
    /* Power-loss states: orphan data without completion, .part leftovers. */
    snprintf(meta,sizeof(meta),"%s.manifest",path);assert(!remove(meta));
    assert(!dj_link_cache_hit(&c,&b,"MP3",NULL));
    snprintf(meta,sizeof(meta),"%s/download.part",root);f=fopen(meta,"wb");assert(f);fputs("partial",f);fclose(f);
    commit(&c,&b,"MP3","BBB",path);assert(access(meta,0)!=0);
    space=DJ_LINK_CACHE_RESERVE;assert(!dj_link_cache_begin(&c,&t,3,1024));space=UINT64_MAX;
    /* Keep a loaded deck even when pruning exhausts everything else. */
    commit(&c,&a,"MP3","AAA",path);c.pins[0]=a;c.budget=120;
    assert(!dj_link_cache_begin(&c,&t,3,1024));c.budget=DJ_LINK_CACHE_BUDGET;
    assert(dj_link_cache_hit(&c,&a,"MP3",NULL));
    c.pins[0].valid=false;c.active=a;c.budget=120;
    assert(!dj_link_cache_begin(&c,&t,3,1024));c.budget=DJ_LINK_CACHE_BUDGET;
    assert(dj_link_cache_hit(&c,&a,"MP3",NULL));
    assert(!dj_link_cache_begin(&c,&t,DJ_LINK_CACHE_BUDGET+1,DJ_LINK_CACHE_BUDGET));
    clear(root);puts("dj_link_cache: identity A/B, integrity, interruption, space and pins PASS");return 0;
}
