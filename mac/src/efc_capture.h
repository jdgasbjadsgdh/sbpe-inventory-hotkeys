/* Native Mac EFC evidence recorder, v3. User-requested retail capture.
 * Included by the UUID-pinned SBPE Mac runtime. No gameplay writes.
 * Incoming world messages are copied BEFORE dispatch; disk work is asynchronous.
 */
#ifndef SBPE_EFC_CAPTURE_H
#define SBPE_EFC_CAPTURE_H
#if !defined(SBPE_TEST) || defined(EFC_CAPTURE_TEST)
#include <pthread.h>
#include <stdatomic.h>
#include <dirent.h>
#include <errno.h>
#include <zlib.h>
#include <sys/mman.h>
#include <sys/statvfs.h>

#define EFC_QUEUE_MAX (128U*1024U*1024U)
#define EFC_MAX_RECORD (32U*1024U*1024U)
#define EFC_OBJECT_MAX 100000
#define EFC_FRAME 0x100
#define EFC_DRAWS 0x101
#define EFC_SHEET 0x102
#define EFC_HITBOXES 0x103
/* Background evidence (USER-REQUESTED 2026-10-22: "edit the plugin so the
   background can be captured correctly"): every draw call of one rendered
   frame -- backdrop tiles, scatter, edges, fills, triangles, in the v1 EFF
   format -- 0.7 s into each room and every 3 s after. A forced EFC_FRAME
   with the same frame's camera and canvas comes just before each. */
#define EFC_DRAW_MAX 60000
#define EFC_DRAW_FIRST_NS 700000000ULL
#define EFC_DRAW_EVERY_NS 3000000000ULL
struct EfcDraw { int32_t a[15]; };
_Static_assert(sizeof(struct EfcDraw)==60,"EFC draw format");
_Static_assert(sizeof(struct ActiveHitbox)==28,"EFC active hitbox format");
struct EfcRecord { uint32_t kind,length; uint64_t seq,ns,visit; };
_Static_assert(sizeof(struct EfcRecord)==32,"EFC record format");
struct EfcNode { struct EfcNode *next; struct EfcRecord h; unsigned char data[]; };
struct EfcObject {
    uint32_t owner,id,source; int32_t xmp,ymp,wmp,hmp,z,facing;
    uint32_t shape,terrain,ownertype,pose; int32_t posetime,hp,maxhp,damage,hitboxdamage,armor,lifetime;
    uint32_t flags; char vid[160];
};
_Static_assert(sizeof(struct EfcObject)==244,"EFC object format");
struct EfcFrame {
    uint32_t world,game,zone; int32_t floor,gdv,t,dt; uint32_t count,flags;
    int32_t bounds[4],camera[2],canvas[2]; char zone_name[64],music[64];
};
_Static_assert(sizeof(struct EfcFrame)==196,"EFC frame format");
struct EfcSheet { uint32_t sheet,page; char pack[128],desc[128],image[128]; };
static pthread_mutex_t efc_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t efc_cond=PTHREAD_COND_INITIALIZER;
static struct EfcNode *efc_head,*efc_tail;
static size_t efc_queued;
static pthread_t efc_thread;
static atomic_int efc_running,efc_failed,efc_stopping;
static atomic_ullong efc_written,efc_lost;
static uint64_t efc_seq,efc_visit;
static uint64_t efc_frames,efc_packets;
static int efc_started,efc_active;
static uint64_t efc_last_frame_ns;
static void *efc_last_boxes;static size_t efc_last_boxbytes;static int efc_boxes_seen;
static uint32_t efc_game,efc_zone;
static int (*efc_lz4)(const char*,char*,int,int);
static int efc_start_session(void);
static char efc_dir[4096],efc_assets[4096],efc_asset_cache[4096];
static struct EfcObject *efc_frame_objects;static size_t efc_frame_capacity;
static void (*efc_original_dispatch)(void*,int,const char*,int);
static void (*efc_original_texture)(unsigned,int,int,int,int,int,int,int,int,int,int,int,int,uint32_t,int);
static void (*efc_original_fill)(int,int,int,int,uint32_t,int);
static void (*efc_original_tri)(int,int,int,int,int,int,uint32_t,int);
static struct EfcDraw *efc_draws;static unsigned efc_ndraws;
static int efc_draw_hooked,efc_draw_due,efc_draw_armed,efc_draw_overflow;
static uint64_t efc_visit_ns,efc_last_draw_ns;
static atomic_ullong efc_draw_frames,efc_draw_overflows;
/* Defined by the existing guarded native patcher later in sbpe.c. */
static void network_guard_jump(uint8_t*,uintptr_t);
static int network_patch_apply(uint8_t*,const uint8_t*,size_t,const uint8_t*,size_t);
static uint64_t efc_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;}
static void efc_fail(void){atomic_store(&efc_failed,1);atomic_fetch_add(&efc_lost,1);}
static int efc_enqueue(uint32_t kind,const void *data,size_t n){
    if(!atomic_load(&efc_running)||atomic_load(&efc_failed))return 0;
    if(n>EFC_MAX_RECORD||(!data&&n)){efc_fail();return 0;}
    struct EfcNode *node=malloc(sizeof(*node)+n);if(!node){efc_fail();return 0;}
    node->next=NULL;node->h=(struct EfcRecord){kind,(uint32_t)n,++efc_seq,efc_ns(),efc_visit};
    if(n)memcpy(node->data,data,n);
    pthread_mutex_lock(&efc_mutex);
    if(efc_queued+sizeof(*node)+n>EFC_QUEUE_MAX){pthread_mutex_unlock(&efc_mutex);free(node);efc_fail();return 0;}
    if(efc_tail)efc_tail->next=node;else efc_head=node;efc_tail=node;efc_queued+=sizeof(*node)+n;
    pthread_cond_signal(&efc_cond);pthread_mutex_unlock(&efc_mutex);return 1;
}
static int efc_write_all(FILE *f,const void *p,size_t n){return fwrite(p,1,n,f)==n;}
static void efc_status(int closed,unsigned asset_count,unsigned asset_errors){
    char tmp[4200],dest[4200];snprintf(tmp,sizeof(tmp),"%s/status.json.tmp",efc_dir);snprintf(dest,sizeof(dest),"%s/status.json",efc_dir);
    FILE *f=fopen(tmp,"w");if(!f)return;
    fprintf(f,"{\"schema\":3,\"closed_cleanly\":%s,\"failed\":%s,\"records_written\":%llu,\"lost_records\":%llu,\"asset_files\":%u,\"asset_errors\":%u,\"draw_snapshots\":%llu,\"draw_overflows\":%llu}\n",closed?"true":"false",atomic_load(&efc_failed)?"true":"false",(unsigned long long)atomic_load(&efc_written),(unsigned long long)atomic_load(&efc_lost),asset_count,asset_errors,(unsigned long long)atomic_load(&efc_draw_frames),(unsigned long long)atomic_load(&efc_draw_overflows));
    if(fclose(f)==0)rename(tmp,dest);
}
static unsigned efc_asset_errors,efc_asset_count;
static void efc_copy_assets(void){
    DIR *d=opendir(efc_assets);if(!d){++efc_asset_errors;return;}struct dirent *e;
    while((e=readdir(d))){
        const char *name=e->d_name;size_t len=strlen(name);
        if((strncmp(name,"cave.",5)&&strncmp(name,"cave-s.",7)&&strncmp(name,"base.",5))||name[0]=='.'||len<5||(!strstr(name,".png")&&!strstr(name,".bpb")))continue;
        char from[4400],to[4400],cached[4400],temp[4440];snprintf(from,sizeof(from),"%s/%s",efc_assets,name);snprintf(to,sizeof(to),"%s/textures/%s",efc_dir,name);snprintf(cached,sizeof(cached),"%s/%s",efc_asset_cache,name);snprintf(temp,sizeof(temp),"%s.part",cached);
        struct stat st,ds;if(stat(from,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>128*1024*1024)continue;
        if(!stat(to,&ds)&&ds.st_size==st.st_size)continue;
        if(!stat(cached,&ds)&&ds.st_size==st.st_size){if(!link(cached,to))++efc_asset_count;else ++efc_asset_errors;continue;}
        FILE *in=fopen(from,"rb"),*out=fopen(temp,"wb");int ok=in&&out;char buf[65536];size_t n;
        if(ok){while((n=fread(buf,1,sizeof(buf),in)))if(!efc_write_all(out,buf,n)){ok=0;break;}if(ferror(in))ok=0;}
        if(in)fclose(in);if(out&&fclose(out))ok=0;
        if(ok&&!rename(temp,cached)&&!link(cached,to))++efc_asset_count;else{unlink(temp);++efc_asset_errors;}
    }closedir(d);
}
static void *efc_writer(void *unused){
    (void)unused;char path[4200];snprintf(path,sizeof(path),"%s/capture.sbc.gz",efc_dir);gzFile f=gzopen(path,"wb1");
    if(!f||gzwrite(f,"SBEFC001",8)!=8){if(f)gzclose(f);efc_fail();efc_status(0,0,0);return NULL;}
    uint64_t last_flush=0,last_scan=0;
    for(;;){
        pthread_mutex_lock(&efc_mutex);
        if(!efc_head&&!atomic_load(&efc_stopping)){
            struct timespec wake;clock_gettime(CLOCK_REALTIME,&wake);++wake.tv_sec;
            pthread_cond_timedwait(&efc_cond,&efc_mutex,&wake);
        }
        struct EfcNode *node=efc_head;if(node){efc_head=node->next;if(!efc_head)efc_tail=NULL;efc_queued-=sizeof(*node)+node->h.length;}
        int done=atomic_load(&efc_stopping)&&!efc_head&&!node;pthread_mutex_unlock(&efc_mutex);
        if(node){
            if(gzwrite(f,&node->h,sizeof(node->h))!=sizeof(node->h)||gzwrite(f,node->data,node->h.length)!=(int)node->h.length)efc_fail();
            else atomic_fetch_add(&efc_written,1);free(node);
        }
        uint64_t now=efc_ns();
        if(now-last_flush>1000000000ULL||done){
            if(gzflush(f,Z_SYNC_FLUSH)!=Z_OK)efc_fail();efc_status(0,efc_asset_count,efc_asset_errors);last_flush=now;
            struct statvfs space;if(!statvfs(efc_dir,&space)&&(uint64_t)space.f_bavail*space.f_frsize<512ULL*1024*1024)efc_fail();
        }
        /* Scan on idle or shutdown; never let asset copying block network/render callbacks. */
        if(now-last_scan>5000000000ULL){efc_copy_assets();last_scan=now;}
        if(done)break;
    }
    if(gzclose(f)!=Z_OK)efc_fail();efc_status(1,efc_asset_count,efc_asset_errors);return NULL;
}
static void efc_shutdown(void){
    if(!efc_started)return;atomic_store(&efc_running,0);atomic_store(&efc_stopping,1);
    pthread_mutex_lock(&efc_mutex);pthread_cond_signal(&efc_cond);pthread_mutex_unlock(&efc_mutex);pthread_join(efc_thread,NULL);efc_started=0;
    free(efc_frame_objects);efc_frame_objects=NULL;efc_frame_capacity=0;
    free(efc_last_boxes);efc_last_boxes=NULL;efc_last_boxbytes=0;efc_boxes_seen=0;
    efc_draw_armed=efc_draw_due=0;free(efc_draws);efc_draws=NULL;
}
/* Read only the bounded protobuf envelope needed to gate capture. Unknown fields
   are skipped by their wire type; no C++ object is constructed or altered. */
struct EfcWire { const unsigned char *p,*end; };
static int efc_varint(struct EfcWire *w,uint64_t *out){
    *out=0;for(unsigned shift=0;shift<70;shift+=7){if(w->p>=w->end)return 0;unsigned byte=*w->p++;if(shift<64)*out|=(uint64_t)(byte&127)<<shift;if(!(byte&128))return 1;}return 0;
}
static int efc_field(struct EfcWire *w,unsigned *field,unsigned *wire,uint64_t *value,struct EfcWire *bytes){
    uint64_t tag,n;if(!efc_varint(w,&tag)||!(tag>>3))return 0;*field=(unsigned)(tag>>3);*wire=(unsigned)(tag&7);*value=0;bytes->p=bytes->end=NULL;
    if(*wire==0)return efc_varint(w,value);
    if(*wire==2){if(!efc_varint(w,&n))return 0;}else if(*wire==1)n=8;else if(*wire==5)n=4;else return 0;
    if(n>(uint64_t)(w->end-w->p))return 0;bytes->p=w->p;bytes->end=w->p+(size_t)n;w->p=bytes->end;return 1;
}
static int efc_equal(struct EfcWire bytes,const char *text){return bytes.p&&(size_t)(bytes.end-bytes.p)==strlen(text)&&!memcmp(bytes.p,text,strlen(text));}
/* EFC rooms: elite (gdv 1) zones or music named cave... -- the boss room is
   cave_boss/cave_boss (game/data/world/cave_boss); the other rooms' names are
   not in our data, so every cave-named elite zone qualifies. */
static int efc_prefix(struct EfcWire bytes,const char *text){size_t n=strlen(text);return bytes.p&&(size_t)(bytes.end-bytes.p)>=n&&!memcmp(bytes.p,text,n);}
static int efc_gate(const char *data,int length,int prior){
    if(!data||length<4||length>(int)EFC_MAX_RECORD||!efc_lz4)return 0;
    const unsigned char *p=(const void*)data;uint32_t n=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
    if(!n||n>EFC_MAX_RECORD)return 0;unsigned char *out=malloc(n);if(!out)return 0;
    int expanded=efc_lz4(data+4,(char*)out,length-4,(int)n);if(expanded!=(int)n){free(out);return 0;}
    struct EfcWire w={out,out+n},props={0},bytes;unsigned field,wire;uint64_t value;int valid=1;
    while(w.p<w.end){if(!efc_field(&w,&field,&wire,&value,&bytes)){valid=0;break;}if(field==2&&wire==2)props=bytes;}
    int gdv=-1;uint32_t game=0,zone=0;struct EfcWire zn={0},music={0};w=props;
    while(valid&&w.p&&w.p<w.end){if(!efc_field(&w,&field,&wire,&value,&bytes)){valid=0;break;}
        if(field==1&&wire==0)zone=(uint32_t)value;if(field==2&&wire==0)game=(uint32_t)value;
        if(field==3&&wire==2)zn=bytes;if(field==5&&wire==0)gdv=(int)value;if(field==7&&wire==2)music=bytes;
    }
    int cave=efc_prefix(zn,"cave")||efc_prefix(music,"cave");
    int side=prior&&game==efc_game&&zone==efc_zone&&game!=0&&(!zn.p||zn.p==zn.end||efc_equal(zn,"shop"));
    int yes=valid&&props.p&&((gdv==1&&cave)||side);
    if(yes){efc_game=game;efc_zone=zone;}free(out);return yes;
}
static void efc_dispatch(void *handlers,int type,const char *data,int length){
    if(type==10){
        int active=efc_gate(data,length,efc_active);efc_active=active;
        if(!active){efc_shutdown();efc_visit=0;}
        else if((efc_started||efc_start_session())&&!atomic_load(&efc_failed)){
            ++efc_visit;efc_last_frame_ns=0;efc_boxes_seen=0;efc_visit_ns=efc_ns();efc_last_draw_ns=0;if(efc_enqueue(10,data,(size_t)length))++efc_packets;
        }
    }else if(efc_active&&(type==12||type==37||type==38||type==21)){
        if(length>=0&&(uint32_t)length<=EFC_MAX_RECORD){if(efc_enqueue((uint32_t)type,data,(size_t)length))++efc_packets;}
        else efc_fail();
    }
    efc_original_dispatch(handlers,type,data,length);
}
static int efc_vector_count(struct STDVector v,size_t size){
    uintptr_t a=(uintptr_t)v.start,b=(uintptr_t)v.finish,e=(uintptr_t)v.endOfStorage;
    if(!a&&!b&&!e)return 0;
    if(!a||b<a||e<b||!size||(b-a)%size||(b-a)/size>EFC_OBJECT_MAX)return -1;
    return (int)((b-a)/size);
}
static unsigned efc_sheet_seen[8192];
static void efc_texture_map(void){
#ifndef EFC_CAPTURE_TEST
    /* TextureManager::textures is the first field; Texture::sheets is +0x40,
       verified in this Mac build's constructors (not the Windows layout). */
    struct STDVector textures;if(!read_memory((void*)PTR(0x1001ff4f0),&textures,sizeof(textures)))return;
    int n=efc_vector_count(textures,sizeof(void*));if(n<0||n>4096)return;
    for(int i=0;i<n;++i){struct Texture *p,t;if(!read_memory((void**)textures.start+i,&p,8)||!read_memory(p,&t,sizeof(t)))continue;
        int pages=efc_vector_count(t.sheets,4);if(pages<0||pages>4096)continue;
        for(int page=0;page<pages;++page){struct EfcSheet row={0};if(!read_memory((unsigned*)t.sheets.start+page,&row.sheet,4)||!row.sheet)continue;
            
            row.page=(unsigned)page;read_string(t.pvidDesc.name,row.pack,sizeof(row.pack));
            if(strcmp(row.pack,"cave")&&strcmp(row.pack,"cave-s")&&strcmp(row.pack,"base"))continue;
            read_string(t.pvidDesc.descid,row.desc,sizeof(row.desc));
            if(page<t.pvidDesc.imageids.current_size&&t.pvidDesc.imageids.current_size<=4096){struct STDString *s;if(read_memory(t.pvidDesc.imageids.elements+page,&s,8))read_string(s,row.image,sizeof(row.image));}
            uint32_t hash=2166136261U;for(size_t k=4;k<sizeof(row);++k)hash=(hash^((const unsigned char*)&row)[k])*16777619U;if(!hash)hash=1;
            if(row.sheet<8192&&efc_sheet_seen[row.sheet]==hash)continue;
            if(row.pack[0]&&row.image[0]&&efc_enqueue(EFC_SHEET,&row,sizeof(row))&&row.sheet<8192)efc_sheet_seen[row.sheet]=hash;
        }
    }
#endif
}
static int efc_push_object(struct WorldObject *o,uint32_t owner,uint32_t source,size_t *count){
    if(!o)return 1;if(*count>=EFC_OBJECT_MAX){efc_fail();return 0;}
    if(*count==efc_frame_capacity){size_t cap=efc_frame_capacity?efc_frame_capacity*2:1024;if(cap>EFC_OBJECT_MAX)cap=EFC_OBJECT_MAX;
        void *next=realloc(efc_frame_objects,cap*sizeof(*efc_frame_objects));if(!next){efc_fail();return 0;}efc_frame_objects=next;efc_frame_capacity=cap;}
    struct WorldObject copy;if(!read_memory(o,&copy,sizeof(copy))){efc_fail();return 0;}
    struct Properties *p=&copy.props;struct EfcObject *r=efc_frame_objects+(*count)++;memset(r,0,sizeof(*r));
    r->owner=owner;r->id=copy.objId;r->source=source;r->xmp=p->xmp;r->ymp=p->ymp;r->wmp=p->wmp;r->hmp=p->hmp;r->z=p->z;r->facing=p->facing;
    r->shape=p->shapetype;r->terrain=p->terraintype;r->ownertype=p->ownertype;r->pose=p->pose;r->posetime=p->posetime;r->hp=p->hitpoints;r->maxhp=p->maxhitpoints;
    r->damage=p->damage;r->hitboxdamage=p->hitboxdamage;r->armor=p->armor;r->lifetime=p->lifetime;
    r->flags=copy.isLive|(p->invulnerable<<1)|(p->isshield<<2)|(p->draw<<3)|(p->spawn<<4);read_string(p->vid,r->vid,sizeof(r->vid));return 1;
}
static int efc_subworld(struct SubWorldImpl *sub,uint32_t owner,uint32_t source,size_t *count){
    if(!sub)return 1;struct STDVector v;if(!read_memory(&sub->objs.vec,&v,sizeof(v))){efc_fail();return 0;}
    int n=efc_vector_count(v,sizeof(struct SortedVecElement));if(n<0||n>EFC_OBJECT_MAX){efc_fail();return 0;}
    if(!n&&v.start!=v.finish){efc_fail();return 0;}
    for(int i=0;i<n;++i){struct SortedVecElement pair;if(!read_memory((struct SortedVecElement*)v.start+i,&pair,sizeof(pair))||!efc_push_object(pair.obj,owner,source,count))return 0;}return 1;
}
static void efc_frame(struct ClientWorld *cw,struct WorldView *wv,int dt){
    efc_draw_due=0;
    if(efc_active&&efc_started&&(!cw||!wv)){efc_active=0;efc_shutdown();efc_visit=0;return;}
    if(!efc_active||!atomic_load(&efc_running)||atomic_load(&efc_failed)||!cw||!wv)return;
    char zn[64],music[64];read_string(cw->asWorld.props.zone,zn,sizeof(zn));read_string(cw->asWorld.props.music,music,sizeof(music));
    int cave=!strncmp(zn,"cave",4)||!strncmp(music,"cave",4);
    int side=cw->asWorld.props.gameid==efc_game&&cw->asWorld.props.zoneid==efc_zone&&efc_game&&(!zn[0]||!strcmp(zn,"shop"));
    if(!((cw->asWorld.props.gdv==1&&cave)||side)){efc_active=0;efc_shutdown();efc_visit=0;return;}
    uint64_t now=efc_ns();
    int snap=efc_draws&&efc_visit_ns&&now-efc_visit_ns>=EFC_DRAW_FIRST_NS&&(!efc_last_draw_ns||now-efc_last_draw_ns>=EFC_DRAW_EVERY_NS);
    if(!snap&&efc_last_frame_ns&&now-efc_last_frame_ns<50000000ULL)return;efc_last_frame_ns=now;
    size_t count=0;
    if(!efc_subworld(cw->serverSubWorld?&cw->serverSubWorld->asSubWorldImpl:NULL,1,1,&count))return;
    if(!efc_subworld(cw->mySubWorld?&cw->mySubWorld->asNativeSubWorld.asSubWorldImpl:NULL,cw->mySubWorld?cw->mySubWorld->asNativeSubWorld.asSubWorldImpl.asSubWorld.worldId:0,2,&count))return;
    struct STDVector v=cw->clientSubWorlds.vec;int n=efc_vector_count(v,sizeof(struct SortedVecElement));
    if(n<0||n>EFC_OBJECT_MAX||(!n&&v.start!=v.finish)){efc_fail();return;}
    for(int i=0;i<n;++i){struct SortedVecElement pair;if(!read_memory((struct SortedVecElement*)v.start+i,&pair,sizeof(pair))||!efc_subworld(pair.obj?&((struct ForeignSubWorld*)pair.obj)->asSubWorldImpl:NULL,pair.key,3,&count))return;}
    struct EfcFrame h={0};h.world=cw->asWorld.id;h.game=cw->asWorld.props.gameid;h.zone=cw->asWorld.props.zoneid;h.floor=cw->asWorld.props.floor;h.gdv=cw->asWorld.props.gdv;h.t=wv->lastSubWorldT;h.dt=dt;h.count=(uint32_t)count;h.flags=(paused?1:0)|(!efc_visit?2:0);
    memcpy(h.bounds,&wv->worldBounds,sizeof(h.bounds));memcpy(h.camera,&wv->offset,sizeof(h.camera));h.canvas[0]=canvas_w?*canvas_w:0;h.canvas[1]=canvas_h?*canvas_h:0;
    read_string(cw->asWorld.props.zone,h.zone_name,sizeof(h.zone_name));read_string(cw->asWorld.props.music,h.music,sizeof(h.music));
    size_t bytes=sizeof(h)+count*sizeof(struct EfcObject);unsigned char *buf=malloc(bytes);if(!buf){efc_fail();return;}memcpy(buf,&h,sizeof(h));if(count)memcpy(buf+sizeof(h),efc_frame_objects,count*sizeof(struct EfcObject));
    if(efc_enqueue(EFC_FRAME,buf,bytes)){++efc_frames;if(snap){efc_draw_due=1;efc_last_draw_ns=now;}}free(buf);
    struct STDVector boxes=cw->activeHitboxes.hitboxes;int nb=efc_vector_count(boxes,sizeof(struct ActiveHitbox));
    if(nb<0){efc_fail();return;}
    size_t boxbytes=(size_t)nb*sizeof(struct ActiveHitbox);void *boxdata=boxbytes?malloc(boxbytes):NULL;
    if(boxbytes&&(!boxdata||!read_memory(boxes.start,boxdata,boxbytes))){free(boxdata);efc_fail();return;}
    if(!efc_boxes_seen||boxbytes!=efc_last_boxbytes||(boxbytes&&memcmp(efc_last_boxes,boxdata,boxbytes))){
        if(efc_enqueue(EFC_HITBOXES,boxdata,boxbytes)){
            free(efc_last_boxes);efc_last_boxes=boxdata;efc_last_boxbytes=boxbytes;efc_boxes_seen=1;boxdata=NULL;
        }
    }
    free(boxdata);
    static uint64_t last_map;if(efc_ns()-last_map>1000000000ULL){efc_texture_map();last_map=efc_ns();}
}
/* Draw calls go to the client unchanged; only an armed frame is copied. */
static void efc_add_draw(const int32_t a[15]){
    if(!efc_draw_armed||!efc_draws)return;
    if(efc_ndraws>=EFC_DRAW_MAX){efc_draw_overflow=1;return;}
    memcpy(efc_draws[efc_ndraws++].a,a,sizeof(efc_draws[0].a));
}
static void efc_texture(unsigned sheet,int sx,int sy,int sw,int sh,int dx,int dy,int dw,int dh,int angle,int cx,int cy,int flip,uint32_t color,int blend){
    int32_t a[15]={(int32_t)sheet,sx,sy,sw,sh,dx,dy,dw,dh,angle,cx,cy,flip,(int32_t)color,blend};efc_add_draw(a);
    efc_original_texture(sheet,sx,sy,sw,sh,dx,dy,dw,dh,angle,cx,cy,flip,color,blend);
}
static void efc_fill(int x,int y,int w,int h,uint32_t color,int blend){
    int32_t a[15]={-1,x,y,w,h,(int32_t)color,blend};efc_add_draw(a);efc_original_fill(x,y,w,h,color,blend);
}
static void efc_tri(int x1,int y1,int x2,int y2,int x3,int y3,uint32_t color,int blend){
    int32_t a[15]={-2,x1,y1,x2,y2,x3,y3,(int32_t)color,blend};efc_add_draw(a);efc_original_tri(x1,y1,x2,y2,x3,y3,color,blend);
}
/* Around the client's own Stage draw only: SBPE's overlays are not copied. */
static void efc_begin_draw(void){
    efc_ndraws=0;efc_draw_overflow=0;
    efc_draw_armed=efc_draw_due&&efc_active&&efc_draws&&atomic_load(&efc_running)&&!atomic_load(&efc_failed);efc_draw_due=0;
}
static void efc_end_draw(void){
    if(!efc_draw_armed)return;efc_draw_armed=0;
    if(efc_draw_overflow){atomic_fetch_add(&efc_draw_overflows,1);return;}
    if(efc_ndraws&&efc_enqueue(EFC_DRAWS,efc_draws,(size_t)efc_ndraws*sizeof(*efc_draws)))atomic_fetch_add(&efc_draw_frames,1);
}
static void efc_overlay(void){
    if(!efc_active||!efc_started||!canvas_w||!canvas_h)return;
    char message[160];snprintf(message,sizeof(message),atomic_load(&efc_failed)?"EFC CAPTURE STOPPED - check status.json":"EFC capture: %llu rooms / %llu frames / %llu backgrounds",(unsigned long long)efc_visit,(unsigned long long)efc_frames,(unsigned long long)atomic_load(&efc_draw_frames));
    text_style(message,*canvas_w-8,*canvas_h-48,14,atomic_load(&efc_failed)?0xffff4040:0xff60ffff,"TenbyFive",1,1);
}
static int efc_hook(uint8_t *site,const uint8_t *expected,size_t n,void *callback,void **original){
    if(memcmp(site,expected,n))return 0;size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *tr=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(tr==MAP_FAILED)return 0;
    memcpy(tr,expected,n);network_guard_jump(tr+n,(uintptr_t)site+n);
    if(mprotect(tr,page,PROT_READ|PROT_EXEC)){munmap(tr,page);return 0;}
    uint8_t jump[14];network_guard_jump(jump,(uintptr_t)callback);*original=tr;
    if(network_patch_apply(site,expected,n,jump,sizeof(jump)))return 1;*original=NULL;munmap(tr,page);return 0;
}
static int efc_start_session(void){
    atomic_store(&efc_failed,0);atomic_store(&efc_stopping,0);atomic_store(&efc_written,0);atomic_store(&efc_lost,0);
    efc_asset_errors=efc_asset_count=0;efc_seq=efc_frames=efc_packets=efc_last_frame_ns=0;efc_boxes_seen=0;memset(efc_sheet_seen,0,sizeof(efc_sheet_seen));
    atomic_store(&efc_draw_frames,0);atomic_store(&efc_draw_overflows,0);efc_visit_ns=efc_last_draw_ns=0;
    if(efc_draw_hooked&&!efc_draws)efc_draws=malloc(EFC_DRAW_MAX*sizeof(*efc_draws));
    const char *root=getenv("SBPE_ROOT");if(!root)return 0;
    struct timespec now;clock_gettime(CLOCK_REALTIME,&now);struct tm tm;localtime_r(&now.tv_sec,&tm);char stamp[64];strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&tm);
    char parent[4096],textures[4200];snprintf(parent,sizeof(parent),"%s/efc_captures",root);
    if(mkdir(parent,0700)&&errno!=EEXIST){LOG("EFC capture: cannot create output directory.");return 0;}
    snprintf(efc_asset_cache,sizeof(efc_asset_cache),"%s/texture-cache",parent);
    if(mkdir(efc_asset_cache,0700)&&errno!=EEXIST){LOG("EFC capture: cannot create texture cache.");return 0;}
    snprintf(efc_dir,sizeof(efc_dir),"%s/%s-%ld-%d",parent,stamp,now.tv_nsec,getpid());
    if(mkdir(efc_dir,0700)){LOG("EFC capture: cannot create session directory.");return 0;}
    snprintf(textures,sizeof(textures),"%s/textures",efc_dir);if(mkdir(textures,0700))return 0;
    const char *home=getenv("HOME");char candidate[4096];
    const char *paths[]={"../../../data/texture","data/texture",NULL};
    for(int i=0;paths[i];++i)if(realpath(paths[i],candidate)){struct stat st;if(!stat(candidate,&st)&&S_ISDIR(st.st_mode)){copystr(efc_assets,sizeof(efc_assets),candidate);break;}}
    if(!efc_assets[0]&&home)snprintf(efc_assets,sizeof(efc_assets),"%s/Library/Application Support/Steam/steamapps/common/StarBreak/data/texture",home);
    atomic_store(&efc_running,1);
    if(pthread_create(&efc_thread,NULL,efc_writer,NULL)){atomic_store(&efc_running,0);LOG("EFC capture: writer thread unavailable.");return 0;}
    efc_started=1;LOG("EFC capture started: %s",efc_dir);return 1;
}
static void install_efc_capture(void){
    if(!getbool("plugin_efc_capture","enabled",0))return;
    efc_lz4=(void*)PTR(0x1001874a0);
    static const uint8_t dispatch[15]={0x55,0x48,0x89,0xe5,0x53,0x49,0x89,0xd0,0x4c,0x8b,0x57,8,0x4d,0x85,0xd2};
    if(!efc_hook((void*)PTR(0x1000044d0),dispatch,sizeof(dispatch),(void*)efc_dispatch,(void**)&efc_original_dispatch)){LOG("EFC capture: dispatch bytes did not match; recorder inactive.");return;}
    /* The v1 EFF recorder's renderer hooks (texture, rectangle, triangle). */
    static const uint8_t draw[17]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18};
    static const uint8_t fill_prologue[14]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50};
    if(efc_hook((void*)PTR(0x1000fe170),draw,sizeof(draw),(void*)efc_texture,(void**)&efc_original_texture)&&
       efc_hook((void*)PTR(0x1000fc5d0),fill_prologue,sizeof(fill_prologue),(void*)efc_fill,(void**)&efc_original_fill)&&
       efc_hook((void*)PTR(0x1000fca50),draw,sizeof(draw),(void*)efc_tri,(void**)&efc_original_tri))efc_draw_hooked=1;
    else LOG("EFC capture: draw hooks unavailable; recording without background snapshots.");
    atexit(efc_shutdown);
    LOG("EFC recorder armed%s; stays inactive outside Elite Fungus Cave. Bottom-right indicator during capture.",efc_draw_hooked?" with background snapshots":"");
}
#else
static void install_efc_capture(void){}
static void efc_frame(struct ClientWorld *cw,struct WorldView *wv,int dt){(void)cw;(void)wv;(void)dt;}
static void efc_begin_draw(void){}
static void efc_end_draw(void){}
static void efc_overlay(void){}
#endif
#endif
