/* One-shot loading experiment. Defer readiness only.
 * Native simulation/physics, input, time messages and receive acknowledgments continue.
 * A local missing player is NOT proof of server-side protection. */
#ifndef SBPE_LOADING_PROBE_H
#define SBPE_LOADING_PROBE_H
#include <fcntl.h>
#include <errno.h>
enum {PROBE_IDLE,PROBE_ARMED,PROBE_HOLD,PROBE_RECONNECT,PROBE_RESULT,PROBE_WAIT};
enum {PROBE_ANY,PROBE_RUSH,PROBE_BOSS,PROBE_STAGED};
enum {PROBE_STAGE_NONE,PROBE_STAGE_EARLY,PROBE_STAGE_PENDING,PROBE_STAGE_PREVIEW};
static int probe_preset=PROBE_ANY;
static int (*probe_group_arm)(int);
static int probe_group_local;
static int loading_probe_installed,probe_f8,probe_f9,probe_f10;
static int probe_gui_visible=1;
/* Optional client-status section and controls below this panel. */
static int (*probe_clients_height)(void),(*probe_below_height)(void);
static struct WorldClient *(*probe_world)(void)=world_client;
static struct GameClient *(*probe_game)(void)=game_client;
static double (*probe_clock)(void)=monotime;
static void (*probe_original_send)(struct ServerConnection*,int);
static void (*probe_original_proto)(struct ServerConnection*,int,const void*);
static int (*probe_reconnect_request)(void*,int (*)(void*));
static void (*probe_reconnect_cancel)(void*,int (*)(void*));
static struct {
    int state,socket,world_t0,world_t1,advances,objects0,objects1,player_before,spawn_logged;
    int client_t0,client_t1,time_messages,input_messages,create_logged,post_closed;
    int reconnect_on_release,reconnect_issued,reloaded,mission_retained,manual,force_reconnect,recoveries;
    int preset,ready_type,asset_finish,stage;
    int staged_fallback,room_preview;
    unsigned asset_headers,world_headers,asset_empty_calls,asset_proto_calls,asset_unbound_calls,phase_trace_logs;
    double early_seconds,early_connection_started;
    uint64_t time_ms;
    uint32_t world,game;
    struct WorldClient *wc;struct ServerConnection *connection;struct GameClient *gc;
    double armed_at,started,deadline,released,last_sample,last_post_sample,seconds,reconnect_at,last_position_sample;
    char zone[64],result[160];
} loading_probe;
static FILE *probe_record;
static char probe_record_path[4096];static unsigned probe_record_serial;static int probe_record_failed;
static void probe_json_string(FILE *file,const char *s){
    fputc('"',file);for(const unsigned char *p=(const void*)s;*p;++p){
        if(*p=='"'||*p=='\\'){fputc('\\',file);fputc(*p,file);}
        else if(*p<32||*p>=127)fprintf(file,"\\u%04x",*p);else fputc(*p,file);
    }fputc('"',file);
}
static void probe_record_close(void){
    if(probe_record){fprintf(probe_record,"{\"event\":\"record_closed\"}\n");fclose(probe_record);probe_record=NULL;}
    if(sbpe_log_mirror)sbpe_log_mirror=NULL;
}
static void probe_record_write(uint64_t epoch,const char *format,va_list args){
    if(!probe_record)return;
    if(strncmp(format,"Loading test:",13)&&strncmp(format,"Spawn ",6)&&strncmp(format,"Network ",8)&&strncmp(format,"Receive evidence:",17)&&strncmp(format,"Connection lost:",16))return;
    char message[2048];vsnprintf(message,sizeof(message),format,args);
    fprintf(probe_record,"{\"event\":\"log\",\"epoch_ms\":%llu,\"message\":",(unsigned long long)epoch);probe_json_string(probe_record,message);fputs("}\n",probe_record);
    if(fflush(probe_record)||ftell(probe_record)>8*1024*1024){probe_record_failed=1;probe_record_close();}
}
static int probe_record_begin(void){
    probe_record_close();probe_record_path[0]=0;probe_record_failed=0;
    const char *root=getenv("SBPE_ROOT");
#ifdef SBPE_TEST
    if(!root||!*root)return 1; /* ordinary fixtures do not create recordings */
#endif
    if(!root||!*root)return 0;
    char directory[4096];int n=snprintf(directory,sizeof(directory),"%s/loading-tests",root);
    if(n<0||(size_t)n>=sizeof(directory)||(mkdir(directory,0700)&&errno!=EEXIST))return 0;
    struct timespec wall;clock_gettime(CLOCK_REALTIME,&wall);struct tm local;localtime_r(&wall.tv_sec,&local);char stamp[48];strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&local);
    n=snprintf(probe_record_path,sizeof(probe_record_path),"%s/loading-test-%s-%03ld-%d-%u.jsonl",directory,stamp,wall.tv_nsec/1000000,getpid(),++probe_record_serial);
    if(n<0||(size_t)n>=sizeof(probe_record_path))return 0;
    int fd=open(probe_record_path,O_CREAT|O_EXCL|O_WRONLY,0600);if(fd<0)return 0;
    probe_record=fdopen(fd,"w");if(!probe_record){close(fd);return 0;}
    fprintf(probe_record,"{\"event\":\"test_begin\",\"schema\":1,\"epoch_ms\":%llu,\"scope\":\"%s\",\"manual_finish\":%s}\n",
        (unsigned long long)((uint64_t)wall.tv_sec*1000+wall.tv_nsec/1000000),loading_probe.manual?"any_map":"normal_forest_ice",loading_probe.manual?"true":"false");
    if(fflush(probe_record)){probe_record_close();return 0;}
    sbpe_log_mirror=probe_record_write;return 1;
}
static int probe_manual_mode(void){return !strcmp(getoptv("plugin_loading_probe","mode","normal"),"manual");}
static int probe_lobby(const struct ClientWorld *cw){return cw&&(in_zone((void*)cw,"lobby")||in_zone((void*)cw,"home"));}
static int probe_target(struct ClientWorld *cw){
    if(!cw||cw->asWorld.props.safe||cw->asWorld.props.gdv)return 0;
    char zone[64],music[64];read_string(cw->asWorld.props.zone,zone,sizeof(zone));read_string(cw->asWorld.props.music,music,sizeof(music));
    return !strcmp(zone,"forest")||!strcmp(music,"forest")||
        !strcmp(zone,"ice")||!strcmp(music,"ice")||
        !strcmp(zone,"ice_boss")||!strcmp(music,"ice_boss");
}
static int probe_boss(struct ClientWorld *cw){
    if(!cw)return 0;char zone[64],music[64];
    read_string(cw->asWorld.props.zone,zone,sizeof(zone));read_string(cw->asWorld.props.music,music,sizeof(music));
    return strstr(zone,"_boss")||strstr(music,"_boss");
}
static struct ServerConnection *probe_connection(void){
    struct GameClient *gc=probe_game();if(gc)return gc->serverConnection;
    struct WorldClient *wc=probe_world();return wc?wc->serverConnection:NULL;
}
static int probe_same_connection(void){
    struct ServerConnection *c=probe_connection();
    return c&&c==loading_probe.connection&&c->state==CONNECTED&&c->socket==loading_probe.socket&&
        (!loading_probe.gc||probe_game()==loading_probe.gc);
}
static int probe_preview_pending(void){return loading_probe.stage==PROBE_STAGE_PENDING&&loading_probe.asset_finish&&(loading_probe.preset==PROBE_STAGED||loading_probe.room_preview);}
/* Bounded metadata only. A valid header is not proof of payload dispatch.
   Observe readiness attempts even if the current GameClient lookup fails. */
static int probe_phase_tracing(void){
    return loading_probe_installed&&loading_probe.manual&&loading_probe.preset==PROBE_STAGED&&
        (loading_probe.stage==PROBE_STAGE_EARLY||loading_probe.stage==PROBE_STAGE_PENDING);
}
static void loading_probe_receive_header(const void *connection,unsigned type,unsigned length){
    if(!probe_phase_tracing())return;
    if(type==MT_LOAD_PVID)++loading_probe.asset_headers;
    else if(type==MT_CREATE_WORLD)++loading_probe.world_headers;
    else if(type!=MT_CHANGE_SERVER)return;
    if(loading_probe.phase_trace_logs<16){
        ++loading_probe.phase_trace_logs;
        LOG("Loading test: phase trace received header type=%u length=%u current_connection=%s; header only, payload not inspected.",type,length,connection==probe_connection()?"YES":"NO");
    }
}
static void probe_trace_asset_send(struct ServerConnection *connection,int type,int protobuf){
    if(type!=MT_PVID_LOADED||!probe_phase_tracing())return;
    int bound=connection&&connection==probe_connection()&&connection->state==CONNECTED;
    if(protobuf)++loading_probe.asset_proto_calls;else ++loading_probe.asset_empty_calls;
    if(!bound)++loading_probe.asset_unbound_calls;
    if(loading_probe.phase_trace_logs<16){
        ++loading_probe.phase_trace_logs;
        LOG("Loading test: phase trace asset-ready attempt encoding=%s bound_current_connection=%s; state=%d stage=%d.",protobuf?"protobuf":"empty",bound?"YES":"NO",loading_probe.state,loading_probe.stage);
    }
}
static int probe_world_t(struct ClientWorld *cw){
    /* ClientWorld::serverDT reads ForeignSubWorld::maxT at +0x178. This is
       received server time, unlike locally interpolated platform positions. */
    int t=0;if(cw&&cw->serverSubWorld)read_memory((uint8_t*)cw->serverSubWorld+0x178,&t,4);return t;
}
static int probe_objects(struct ClientWorld *cw){return cw&&cw->serverSubWorld?vector_count(cw->serverSubWorld->asSubWorldImpl.objs.vec,sizeof(struct SortedVecElement)):0;}
static int probe_client_t(struct ClientWorld *cw){return cw&&cw->mySubWorld?cw->mySubWorld->asNativeSubWorld.asSubWorldImpl.asSubWorld.t:0;}
/* Passive companion to the web recorder. Also observes ordinary entries when
   F9 is never pressed. Frame bounds are observations, not packet timestamps. */
static struct {
    struct WorldClient *wc;struct ClientWorld *cw;struct ServerConnection *connection;
    uint32_t game,world;int socket,active,present;
    uint64_t previous_epoch;double started,last_position;
} loading_observation;
static uint64_t probe_epoch_ms(void){
    struct timespec wall;clock_gettime(CLOCK_REALTIME,&wall);
    return (uint64_t)wall.tv_sec*1000+(uint64_t)wall.tv_nsec/1000000;
}
static void probe_observe_frame(struct WorldClient *wc,double now){
    struct ClientWorld *cw=wc?wc->clientWorld:NULL;
    struct ServerConnection *connection=wc?wc->serverConnection:NULL;
    if(!getbool("plugin_loading_probe","observe_spawns",1)||!cw||!connection||
       connection->state!=CONNECTED||(!probe_target(cw)&&!(loading_probe.manual&&loading_probe.state!=PROBE_IDLE&&!loading_probe.post_closed))){
        memset(&loading_observation,0,sizeof(loading_observation));return;
    }
    uint64_t epoch=probe_epoch_ms();
    if(!loading_observation.active||loading_observation.wc!=wc||loading_observation.cw!=cw||
       loading_observation.connection!=connection||loading_observation.socket!=connection->socket||
       loading_observation.game!=cw->asWorld.props.gameid||loading_observation.world!=cw->asWorld.id){
        memset(&loading_observation,0,sizeof(loading_observation));
        loading_observation.wc=wc;loading_observation.cw=cw;loading_observation.connection=connection;
        loading_observation.socket=connection->socket;loading_observation.game=cw->asWorld.props.gameid;
        loading_observation.world=cw->asWorld.id;loading_observation.active=1;loading_observation.started=now;
        loading_observation.last_position=now-1;
        LOG("Spawn observation: world begin epoch_ms=%llu game=%u world=%u own_t=%d.",(unsigned long long)epoch,loading_observation.game,loading_observation.world,probe_client_t(cw));
    }
    if(now-loading_observation.started>=90)return; /* bounded per-world capture */
    struct WorldObject player;int present=cw->player&&read_memory(cw->player,&player,sizeof(player));
    if(present&&!loading_observation.present){
        LOG("Spawn observation: local player present epoch_ms=%llu previous_epoch_ms=%llu game=%u world=%u object=%u own_t=%d xmp=%d ymp=%d first_sample=%s.",
            (unsigned long long)epoch,(unsigned long long)loading_observation.previous_epoch,loading_observation.game,loading_observation.world,
            player.objId,probe_client_t(cw),player.props.xmp,player.props.ymp,loading_observation.previous_epoch?"no":"yes");
    }else if(!present&&loading_observation.present){
        LOG("Spawn observation: local player absent epoch_ms=%llu game=%u world=%u.",(unsigned long long)epoch,loading_observation.game,loading_observation.world);
    }
    if(present&&now-loading_observation.last_position>=.1){
        loading_observation.last_position=now;
        LOG("Spawn position: epoch_ms=%llu game=%u world=%u own_t=%d xmp=%d ymp=%d.",(unsigned long long)epoch,loading_observation.game,loading_observation.world,probe_client_t(cw),player.props.xmp,player.props.ymp);
    }
    loading_observation.present=present;loading_observation.previous_epoch=epoch;
}
static int probe_same_world(struct WorldClient *wc){
    return wc&&wc==loading_probe.wc&&wc->clientWorld&&wc->serverConnection==loading_probe.connection&&
        wc->serverConnection->state==CONNECTED&&wc->serverConnection->socket==loading_probe.socket&&
        wc->clientWorld->asWorld.id==loading_probe.world&&wc->clientWorld->asWorld.props.gameid==loading_probe.game;
}
/* Camera changes exist only during drawing. Never substitute a player, alter
   world coordinates, or hold the simulation. Native updateOffsets(NULL, dt)
   follows targetPos; all camera fields are restored before the next update. */
_Static_assert(offsetof(struct WorldView,worldBounds)==0x68,"camera bounds ABI");
_Static_assert(offsetof(struct WorldView,offset)==0xbc,"camera offset ABI");
_Static_assert(offsetof(struct WorldView,targetPos)==0xd4,"camera target ABI");
static struct {
    int active,applied,dx,dy,fast;unsigned captured;
    double x,y,last,hold_started;
    uint32_t game,zone,seed,world;int floor;
    struct WorldView *view;
    struct Rectangle bounds;
    struct Point offset,curr,goal,target;
    bool initialized;
} probe_camera;
static int probe_camera_available(struct WorldView *view){
    struct WorldClient *wc=probe_world();
    return loading_probe_installed&&getbool("plugin_loading_probe","enabled",0)&&
        loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_WORLD_CREATED&&
        probe_same_world(wc)&&!wc->clientWorld->player&&wc->worldView&&
        (!view||wc->worldView==view)&&wc->worldView->worldBounds.w>0&&wc->worldView->worldBounds.h>0;
}
/* active is the user's selection. A lost view suspends drawing while the
 * same manual hold recovers; old view pointers are never reused. */
static int probe_camera_sync(void){
    if(!probe_camera.active)return 0;
    struct WorldClient *wc=probe_world();
    if(!loading_probe_installed||!getbool("plugin_loading_probe","enabled",0)||
       (loading_probe.state!=PROBE_HOLD&&loading_probe.state!=PROBE_WAIT)||
       loading_probe.started!=probe_camera.hold_started||
       (probe_same_world(wc)&&wc->clientWorld->player)){
        probe_camera.active=0;probe_camera.view=NULL;return 0;
    }
    if(!probe_camera_available(NULL)){
        probe_camera.view=NULL;probe_camera.last=probe_clock();return 0;
    }
    struct ClientWorld *cw=wc->clientWorld;struct WorldView *v=wc->worldView;
    if(v!=probe_camera.view||cw->asWorld.id!=probe_camera.world){
        struct WorldProperties *p=&cw->asWorld.props;
        if(probe_camera.game!=p->gameid||probe_camera.zone!=p->zoneid||probe_camera.floor!=p->floor||probe_camera.seed!=p->seed){
            probe_camera.x=v->offset.x;probe_camera.y=v->offset.y;
        }
        probe_camera.view=v;probe_camera.world=cw->asWorld.id;probe_camera.game=p->gameid;
        probe_camera.zone=p->zoneid;probe_camera.floor=p->floor;probe_camera.seed=p->seed;
        probe_camera.last=probe_clock();
    }return 1;
}
static void probe_camera_toggle(void){
    if(probe_camera.active){probe_camera.active=0;LOG("Loading test: free camera disabled.");return;}
    if(!probe_camera_available(NULL)){
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Free camera needs a loaded map with spawn held.");return;
    }
    struct WorldView *v=probe_world()->worldView;
    probe_camera.active=1;probe_camera.view=v;probe_camera.x=v->offset.x;probe_camera.y=v->offset.y;
    struct ClientWorld *cw=probe_world()->clientWorld;probe_camera.hold_started=loading_probe.started;
    probe_camera.world=cw->asWorld.id;probe_camera.game=cw->asWorld.props.gameid;probe_camera.zone=cw->asWorld.props.zoneid;
    probe_camera.floor=cw->asWorld.props.floor;probe_camera.seed=cw->asWorld.props.seed;
    probe_camera.last=probe_clock();probe_camera.dx=probe_camera.dy=probe_camera.fast=0;
    LOG("Loading test: local free camera enabled; WASD/arrows move, Shift speeds up. Readiness unchanged.");
}
static void probe_camera_keys(const uint8_t *keys,int count,int allowed){
#define CAM_KEY(k) ((k)<count&&keys[k])
    probe_camera.dx=allowed?(!!(CAM_KEY(7)||CAM_KEY(79))-!!(CAM_KEY(4)||CAM_KEY(80))):0;
    probe_camera.dy=allowed?(!!(CAM_KEY(22)||CAM_KEY(81))-!!(CAM_KEY(26)||CAM_KEY(82))):0;
    probe_camera.fast=allowed&&(CAM_KEY(225)||CAM_KEY(229));
#undef CAM_KEY
}
static int probe_camera_consumes(struct Stage *st,int down,int key,int was_down){
    static const int codes[]={97,100,115,119,0x4000004f,0x40000050,0x40000051,0x40000052};
    for(unsigned i=0;i<sizeof(codes)/sizeof(*codes);++i)if(key==codes[i]){
        unsigned bit=1U<<i;
        if(!down){int captured=!!(probe_camera.captured&bit);probe_camera.captured&=~bit;return captured;}
        if(probe_camera.captured&bit)return 1;
        if(!was_down&&probe_camera.active&&probe_camera_available(NULL)&&!st->textInputTarget&&!active_controls){probe_camera.captured|=bit;return 1;}
    }
    return 0;
}
static double probe_camera_axis(double value,int start,int size,int viewport){
    if(size<=viewport)return start+(size-(double)viewport)/2;
    return clamp(value,start,(double)start+size-viewport);
}
static void probe_camera_begin(struct WorldView *v){
    if(!probe_camera_sync()||v!=probe_camera.view)return;
    double now=probe_clock(),dt=clamp(now-probe_camera.last,0,.1);probe_camera.last=now;
    double speed=clamp(getnum("plugin_loading_probe","camera_speed",900),50,6000)*(probe_camera.fast?3:1);
    if(probe_camera.dx&&probe_camera.dy)speed/=sqrt(2.0);
    if(!paused){probe_camera.x+=probe_camera.dx*speed*dt;probe_camera.y+=probe_camera.dy*speed*dt;}
    int width=v->asUIElement.w,height=v->asUIElement.h;
    probe_camera.x=probe_camera_axis(probe_camera.x,v->worldBounds.x,v->worldBounds.w,width);
    probe_camera.y=probe_camera_axis(probe_camera.y,v->worldBounds.y,v->worldBounds.h,height);
    probe_camera.bounds=v->playerBounds;probe_camera.initialized=v->offsetsInitialized;
    probe_camera.offset=v->offset;probe_camera.curr=v->currOffset;probe_camera.goal=v->goalOffset;probe_camera.target=v->targetPos;
    v->playerBounds=v->worldBounds;
    v->offset=v->currOffset=v->goalOffset=(struct Point){(int)lround(probe_camera.x),(int)lround(probe_camera.y)};
    v->targetPos=(struct Point){v->offset.x+width/2,v->offset.y+height/2};v->offsetsInitialized=1;
    probe_camera.applied=1;
}
static void probe_camera_end(void){
    if(!probe_camera.applied)return;probe_camera.applied=0;
    struct WorldClient *wc=probe_world();
    if(!probe_same_world(wc)||wc->worldView!=probe_camera.view){probe_camera.view=NULL;probe_camera_sync();return;}
    struct WorldView *v=wc->worldView;
    v->playerBounds=probe_camera.bounds;v->offsetsInitialized=probe_camera.initialized;
    v->offset=probe_camera.offset;v->currOffset=probe_camera.curr;v->goalOffset=probe_camera.goal;v->targetPos=probe_camera.target;
}
static void probe_wait_for_world(const char *why){
    loading_probe.state=PROBE_WAIT;
    snprintf(loading_probe.result,sizeof(loading_probe.result),"%s",loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY?"Connection lost; early wait continues. F10 previews.":"Connection lost; hold remains active. F10 to spawn.");
    LOG("Loading test: %s; manual hold retained across native recovery. Fresh readiness stays blocked until F10/Reconnect or Cancel.",why);
}
static int probe_reconnect_begin(void *connection){
    if(loading_probe.state!=PROBE_RECONNECT||loading_probe.reconnect_issued||connection!=loading_probe.connection)return 0;
    struct WorldClient *wc=probe_world();
    if(!probe_same_world(wc)){
        loading_probe.state=PROBE_RESULT;loading_probe.post_closed=1;
        LOG("Loading test: queued reconnect canceled because world/connection changed during native dispatch.");return 0;
    }
    if(wc->clientWorld->player&&!loading_probe.force_reconnect){
        loading_probe.state=PROBE_RESULT;loading_probe.player_before=1;loading_probe.released=probe_clock();
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Character appeared before reconnect; ordinary readiness released.");
        LOG("Loading test: character appeared before queued reconnect; original readiness released, no forced reconnect.");
        probe_original_send(wc->serverConnection,MT_WORLD_CREATED);return 0;
    }
    loading_probe.reconnect_issued=1;loading_probe.reconnect_at=probe_clock();
    LOG("Loading test: clean reconnect issued after native dispatch; held readiness discarded, no held character state reused.");return 1;
}
static void probe_release(const char *why){
    if(loading_probe.state!=PROBE_HOLD){if(loading_probe.state!=PROBE_RECONNECT)loading_probe.state=PROBE_IDLE;return;}
    if(loading_probe.ready_type==MT_PVID_LOADED){
        if(!probe_same_connection()){probe_wait_for_world("early hold connection changed before manual finish");return;}
        loading_probe.state=PROBE_RESULT;loading_probe.asset_finish=1;loading_probe.post_closed=1;loading_probe.released=probe_clock();
        if((loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY)||loading_probe.room_preview){
            loading_probe.stage=PROBE_STAGE_PENDING;
            snprintf(loading_probe.result,sizeof(loading_probe.result),"Early hold ended; next map will remain unspawned.");
            LOG("Loading test: early->preview requested (%s); total early hold %.3fs, current connection %.3fs, recoveries=%d. Asset-ready sent once; WORLD_CREATED will remain held. Preview can have a shorter connection lifetime; final reconnect may select a different room.",why,probe_clock()-loading_probe.started,probe_clock()-loading_probe.early_connection_started,loading_probe.recoveries);
            probe_original_send(loading_probe.connection,MT_PVID_LOADED);return;
        }
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Rush test: continuing fresh map loading.");
        LOG("Loading test: early rush hold finished manually after %.3fs; asset readiness sent once on current connection; no old world readiness replay or forced reconnect. Awaiting server-selected CreateWorld.",probe_clock()-loading_probe.started);
        probe_original_send(loading_probe.connection,MT_PVID_LOADED);return;
    }
    struct WorldClient *wc=probe_world();int valid=probe_same_world(wc);double now=probe_clock();
    if(valid){
        loading_probe.client_t1=probe_client_t(wc->clientWorld);
        int t=probe_world_t(wc->clientWorld);
        if(t!=loading_probe.world_t1){++loading_probe.advances;loading_probe.world_t1=t;}
        loading_probe.objects1=probe_objects(wc->clientWorld);
    }
    int reconnect=valid&&(loading_probe.manual||(loading_probe.reconnect_on_release&&!loading_probe.player_before&&!wc->clientWorld->player));
    loading_probe.force_reconnect=loading_probe.manual;
    loading_probe.state=reconnect?PROBE_RECONNECT:PROBE_RESULT;loading_probe.released=now;
    snprintf(loading_probe.result,sizeof(loading_probe.result),"%s; server time advanced: %s; local player before ready: %s",why,loading_probe.advances?"YES":"NO",loading_probe.player_before?"YES":"NO");
    LOG("Loading test: %s; hold %.3fs; zone=%s world=%u game=%u maxT=%d->%d objects=%d->%d; ready=%s.",
        loading_probe.result,now-loading_probe.started,loading_probe.zone,loading_probe.world,loading_probe.game,
        loading_probe.world_t0,loading_probe.world_t1,loading_probe.objects0,loading_probe.objects1,valid?(reconnect?"discarded for fresh reconnect":"sent once"):"discarded (connection/world changed)");
    LOG("Loading test: own clock %d->%d; native time messages=%d / %llu ms; native input messages forwarded=%d.",loading_probe.client_t0,loading_probe.client_t1,loading_probe.time_messages,(unsigned long long)loading_probe.time_ms,loading_probe.input_messages);
    /* State changes before sending: the saved acknowledgment cannot re-enter
       the hold. Never dereference/send through an old connection after a DC. */
    /* Native addPlayerInput already updates local input state before send.
       Never suppress only its wire half, replay old events, or manufacture an
       unchanged input that the stock client would have rejected. */
    if(reconnect){
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Hold ended; reconnecting for a fresh spawn.");
        if(probe_reconnect_request&&probe_reconnect_request(wc->serverConnection,probe_reconnect_begin))return;
        if(loading_probe.manual){
            loading_probe.state=PROBE_HOLD;snprintf(loading_probe.result,sizeof(loading_probe.result),"Reconnect unavailable; still holding. Try F10 again.");
            LOG("Loading test: manual reconnect queue unavailable; holding old readiness without replay; no connection closed.");return;
        }
        /* No request was accepted. Retain the old tested release path rather
           than leave an unready session stuck; never claim synchronized play. */
        LOG("Loading test: clean reconnect queue unavailable; ordinary readiness released instead; synchronization unverified.");
        loading_probe.state=PROBE_RESULT;
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Reconnect unavailable; ordinary ready sent. Synchronization unverified.");
    }
    if(valid)probe_original_send(wc->serverConnection,MT_WORLD_CREATED);
}
static void loading_probe_send_proto(struct ServerConnection *connection,int type,const void *message){
    probe_trace_asset_send(connection,type,1);
    if(loading_probe_installed&&loading_probe.state==PROBE_HOLD&&connection==loading_probe.connection&&probe_same_world(probe_world())){
        if(type==MT_PLAYER_INPUT)++loading_probe.input_messages;
        if(type==MT_TIME_PASSES&&message){
            int dt=0;memcpy(&dt,(const uint8_t*)message+8,4);
            if(dt>0){++loading_probe.time_messages;loading_probe.time_ms+=(unsigned)dt;}
        }
    }
    probe_original_proto(connection,type,message);
}
static void loading_probe_received(void *client,const void *decoded){
    if(!loading_probe_installed||loading_probe.create_logged||loading_probe.post_closed||
       (loading_probe.state!=PROBE_HOLD&&loading_probe.state!=PROBE_RECONNECT&&loading_probe.state!=PROBE_RESULT)||client!=loading_probe.wc||!probe_same_world(probe_world()))return;
    const struct Delta *delta=decoded;
    if((delta->_has_bits[0]&15)!=8||!delta->createplayer)return;
    const struct CreatePlayer *p=delta->createplayer;struct ClientWorld *cw=loading_probe.wc->clientWorld;
    loading_probe.create_logged=1;
    LOG("Loading test: CreatePlayer received before apply; own_t=%d maxT=%d total=0x%x spawn_object=%u/%u position=%s teleport=%d.",probe_client_t(cw),probe_world_t(cw),cw->total,cw->spawnObj.worldId,cw->spawnObj.objId,(p->_has_bits[0]&0x20)?"explicit server position":"spawn object",p->teleport);
    if((p->_has_bits[0]&0x20)&&p->pos)LOG("Loading test: explicit server spawn xmp=%d ymp=%d.",p->pos->xmp,p->pos->ymp);
}
static void loading_probe_connection_lost(const void *connection,const char *reason){
    if(!loading_probe_installed||connection!=loading_probe.connection||loading_probe.post_closed||
       (loading_probe.state!=PROBE_HOLD&&loading_probe.state!=PROBE_RECONNECT&&loading_probe.state!=PROBE_RESULT))return;
    if(loading_probe.state==PROBE_RECONNECT&&!loading_probe.reconnect_issued){
        loading_probe.reconnect_issued=1;loading_probe.reconnect_at=probe_clock();
        LOG("Loading test: native connection loss preceded queued reconnect; observing its normal reload instead.");
    }
    struct WorldClient *wc=probe_world();int valid=probe_same_world(wc);
    LOG("Loading test: native connection loss %s at %.3fs; reason=%s; maxT=%d own_t=%d local_player=%s CreatePlayer_received=%s.",
        loading_probe.state==PROBE_HOLD?"during readiness hold":loading_probe.state==PROBE_RECONNECT?"during fresh reconnect":"after readiness release",
        probe_clock()-(loading_probe.state==PROBE_HOLD?loading_probe.started:loading_probe.released),reason,
        valid?probe_world_t(wc->clientWorld):loading_probe.world_t1,valid?probe_client_t(wc->clientWorld):loading_probe.client_t1,
        valid&&wc->clientWorld->player?"present":"absent",loading_probe.create_logged?"YES":"NO");
    if(loading_probe.manual&&loading_probe.state==PROBE_HOLD){
        probe_wait_for_world("natural connection loss during hold");
    }
}
static void probe_arm(void){
    if(!probe_group_local&&probe_group_arm&&probe_group_arm(probe_preset))return;
    struct WorldClient *wc=probe_world();
    int manual=probe_manual_mode();
    if(!wc||!wc->clientWorld||!wc->serverConnection||wc->serverConnection->state!=CONNECTED||(!manual&&!probe_lobby(wc->clientWorld))){snprintf(loading_probe.result,sizeof(loading_probe.result),"Enter a connected game before arming.");LOG("Loading test: arm when connected%s.",manual?"; the next map load is held":" in Eschaton or home, then enter normal Fire Forest or Frozen Blight");return;}
    memset(&loading_probe,0,sizeof(loading_probe));loading_probe.manual=manual;loading_probe.state=PROBE_ARMED;loading_probe.armed_at=probe_clock();
    loading_probe.preset=manual?probe_preset:PROBE_ANY;
    if(loading_probe.preset==PROBE_STAGED){loading_probe.stage=PROBE_STAGE_EARLY;loading_probe.early_seconds=clamp(getnum("plugin_loading_probe","stage_early_seconds",360),1,1800);}
    if(manual){loading_probe.game=wc->clientWorld->asWorld.props.gameid;loading_probe.world=wc->clientWorld->asWorld.id;}
    loading_probe.reconnect_on_release=manual||getbool("plugin_loading_probe","reconnect_on_release",1);
    if(loading_probe.reconnect_on_release&&!probe_reconnect_request){
        loading_probe.state=PROBE_IDLE;LOG("Loading test: cannot arm clean reconnect without the verified native I/O recovery hook.");return;
    }
    if(!probe_record_begin()){loading_probe.state=PROBE_IDLE;probe_record_failed=1;snprintf(loading_probe.result,sizeof(loading_probe.result),"Auto-save unavailable; test not armed.");LOG("Loading test: cannot create persistent recording; test not armed.");return;}
    if(manual){
        if(loading_probe.preset==PROBE_STAGED){
            snprintf(loading_probe.result,sizeof(loading_probe.result),"Staged: early hold %.0fs, then map preview.",loading_probe.early_seconds);
            LOG("Loading test: staged loading armed; early asset-ready hold for %.0fs, then WORLD_CREATED preview hold. F10 advances early or reconnects from preview; Cancel finishes the experiment. Arm before the map/boss entry to compare hold lifetime. No guaranteed mission retention.",loading_probe.early_seconds);return;
        }
        const char *name=loading_probe.preset==PROBE_RUSH?"RUSH (earlier asset-ready hold; world-ready fallback if absent)":loading_probe.preset==PROBE_BOSS?"BOSS (intermediate rooms pass)":"ANY next map";
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Armed: %s",loading_probe.preset==PROBE_RUSH?"early hold before next rush or boss entry":loading_probe.preset==PROBE_BOSS?"Skip boss (next boss room)":"any next map");
        LOG("Loading test: armed once for %s; no arm or hold timer; F9 cancels unused arm, F10 manually finishes. Log saves automatically.",name);return;
    }
    loading_probe.seconds=clamp(getnum("plugin_loading_probe","hold_seconds",5),1,30);
    LOG("Loading test: armed once for normal Fire Forest or Frozen Blight; %.1fs hold; finish=%s; F10 ends hold/cancels arm. Observer must check character presence and shared position.",loading_probe.seconds,loading_probe.reconnect_on_release?"fresh native reconnect":"ordinary readiness release");
}
static void probe_manual_reconnect(void){
    if(loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY&&loading_probe.state==PROBE_ARMED){
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Armed; enter the next map before previewing.");return;
    }
    if(probe_preview_pending()){
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Waiting for map preview. Cancel to finish normally.");return;
    }
    if(loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY&&loading_probe.state==PROBE_WAIT){
        loading_probe.state=PROBE_RESULT;loading_probe.stage=PROBE_STAGE_PENDING;loading_probe.asset_finish=1;loading_probe.post_closed=1;loading_probe.released=probe_clock();
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Preview requested; waiting for a fresh connection.");
        LOG("Loading test: staged preview requested during transport recovery after %.3fs total early wait; next asset readiness passes, next world readiness remains held; no stale connection used.",probe_clock()-loading_probe.started);return;
    }
    if(loading_probe.state==PROBE_RECONNECT||loading_probe.asset_finish)return;
    if(loading_probe.state==PROBE_WAIT){
        struct WorldClient *wc=probe_world();
        if(!wc||!wc->clientWorld||!wc->serverConnection||wc->serverConnection->state!=CONNECTED){
            loading_probe.state=PROBE_RECONNECT;loading_probe.reconnect_issued=1;
            loading_probe.released=loading_probe.reconnect_at=probe_clock();
            snprintf(loading_probe.result,sizeof(loading_probe.result),"F10 accepted; next native recovery may spawn.");
            LOG("Loading test: manual finish while transport recovering; next fresh readiness authorized, no extra connection closed.");return;
        }
        /* The user explicitly finishes a hold; ordinary reconnect below can
           target the current world even if its readiness has not arrived yet. */
    }
    if(loading_probe.state==PROBE_HOLD){probe_release("manual reconnect");return;}
    struct WorldClient *wc=probe_world();struct ClientWorld *cw=wc?wc->clientWorld:NULL;
    if(!cw||!wc->serverConnection||wc->serverConnection->state!=CONNECTED||!probe_reconnect_request){snprintf(loading_probe.result,sizeof(loading_probe.result),"No active game connection to reconnect.");LOG("Loading test: no connected world/recovery hook; manual reconnect not requested.");return;}
    int had_log=probe_record!=NULL;
    memset(&loading_probe,0,sizeof(loading_probe));loading_probe.manual=1;loading_probe.force_reconnect=1;loading_probe.reconnect_on_release=1;
    loading_probe.state=PROBE_RECONNECT;loading_probe.wc=wc;loading_probe.connection=wc->serverConnection;loading_probe.socket=wc->serverConnection->socket;
    loading_probe.world=cw->asWorld.id;loading_probe.game=cw->asWorld.props.gameid;
    read_string(cw->asWorld.props.zone,loading_probe.zone,sizeof(loading_probe.zone));if(!loading_probe.zone[0])read_string(cw->asWorld.props.music,loading_probe.zone,sizeof(loading_probe.zone));
    loading_probe.started=loading_probe.released=probe_clock();
    if(!had_log&&!probe_record_begin())probe_record_failed=1;
    snprintf(loading_probe.result,sizeof(loading_probe.result),"Manual reconnect requested; server selects the fresh spawn.");
    LOG("Loading test: manual reconnect requested during ordinary play; zone=%s world=%u game=%u local_player=%s.",loading_probe.zone,loading_probe.world,loading_probe.game,cw->player?"present":"absent");
    if(!probe_reconnect_request(loading_probe.connection,probe_reconnect_begin)){
        loading_probe.state=PROBE_RESULT;loading_probe.post_closed=1;
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Reconnect queue unavailable; no connection closed.");LOG("Loading test: manual reconnect queue unavailable; no connection closed.");
    }
}
static int probe_room_preview_available(void){
    return loading_probe.manual&&!loading_probe.asset_finish&&
        (loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT)&&
        ((loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY)||
         (loading_probe.preset==PROBE_RUSH&&!loading_probe.room_preview&&loading_probe.ready_type==MT_PVID_LOADED));
}
static void probe_preview_room(void){
    if(!probe_room_preview_available())return;
    if(loading_probe.preset==PROBE_STAGED){probe_manual_reconnect();return;}
    loading_probe.room_preview=1;
    if(loading_probe.state==PROBE_HOLD&&!probe_same_connection())probe_wait_for_world("room preview requested after early connection loss");
    if(loading_probe.state==PROBE_WAIT){
        loading_probe.state=PROBE_RESULT;loading_probe.stage=PROBE_STAGE_PENDING;loading_probe.asset_finish=1;
        loading_probe.post_closed=1;loading_probe.released=probe_clock();
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Room preview requested; waiting for fresh server map.");
        LOG("Loading test: rush room preview requested during recovery; next asset readiness passes and world readiness remains held; no stale readiness sent.");return;
    }
    probe_release("rush room preview requested");
}
static void probe_cancel(void){
    if(loading_probe.preset==PROBE_STAGED||loading_probe.room_preview){
        int pending=loading_probe.stage==PROBE_STAGE_PENDING&&loading_probe.asset_finish;
        loading_probe.stage=PROBE_STAGE_NONE;loading_probe.room_preview=0;
        if(pending){
            snprintf(loading_probe.result,sizeof(loading_probe.result),"Preview canceled; next map may finish loading.");
            LOG("Loading test: staged preview canceled while waiting for map; next ordinary readiness authorized, no saved readiness replayed.");return;
        }
        if(loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_PVID_LOADED&&!probe_same_connection())probe_wait_for_world("cancel requested after early connection loss");
    }
    if(loading_probe.state==PROBE_ARMED){loading_probe.state=PROBE_IDLE;LOG("Loading test: canceled before entry.");}
    else if(loading_probe.state==PROBE_HOLD)probe_release("hold canceled with fresh reconnect");
    else if(loading_probe.state==PROBE_WAIT)probe_manual_reconnect();
}
static void loading_probe_keys(const uint8_t *keys,int n,int allowed){
    if(!loading_probe_installed)return;
    probe_camera_keys(keys,n,allowed);
    int enabled=getbool("plugin_loading_probe","enabled",0);
    if(edge_key(keys,n,"F8",&probe_f8,allowed&&(enabled||connectivity_ui_hidden))){
        if(connectivity_ui_hidden){connectivity_ui_hidden=0;probe_gui_visible=1;}
        else probe_gui_visible=!probe_gui_visible;
    }
    if(edge_key(keys,n,"F10",&probe_f10,allowed)){
        if(enabled&&probe_manual_mode())probe_manual_reconnect();
        else if(loading_probe.state==PROBE_ARMED||loading_probe.state==PROBE_HOLD){
        if(loading_probe.state==PROBE_HOLD)probe_release("manual release");else{loading_probe.state=PROBE_IDLE;LOG("Loading test: canceled before entry.");}
        }
    }
    if(edge_key(keys,n,"F9",&probe_f9,allowed&&enabled&&!paused)){
        if(loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT){if(!loading_probe.manual)probe_release("manual release");}
        else if(loading_probe.state==PROBE_ARMED){loading_probe.state=PROBE_IDLE;LOG("Loading test: disarmed.");}
        else if(loading_probe.state!=PROBE_RECONNECT&&!loading_probe.asset_finish)probe_arm();
    }
}
static int loading_probe_consumes(struct Stage *st,int key){
    if(!loading_probe_installed||st->textInputTarget||active_controls)return 0;
    int enabled=getbool("plugin_loading_probe","enabled",0);
    return ((key==0x40000041||key==0x40000042)&&enabled)||
        (key==0x40000043&&((enabled&&probe_manual_mode())||loading_probe.state==PROBE_ARMED||loading_probe.state==PROBE_HOLD)); /* F8/F9/F10 */
}
static void loading_probe_send(struct ServerConnection *connection,int type){
    probe_trace_asset_send(connection,type,0);
    if(type==MT_PVID_LOADED&&loading_probe.manual&&((loading_probe.preset==PROBE_RUSH&&!loading_probe.room_preview)||(loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY))&&
       (loading_probe.state==PROBE_ARMED||loading_probe.state==PROBE_WAIT||(loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_PVID_LOADED))){
        if(connection==probe_connection()&&connection->state==CONNECTED){
            int again=loading_probe.state!=PROBE_ARMED,reconnected=loading_probe.state==PROBE_WAIT||
                (loading_probe.state==PROBE_HOLD&&!probe_same_connection());
            loading_probe.state=PROBE_HOLD;loading_probe.ready_type=MT_PVID_LOADED;loading_probe.gc=probe_game();
            loading_probe.connection=connection;loading_probe.socket=connection->socket;loading_probe.wc=NULL;
            if(!again)loading_probe.started=probe_clock();loading_probe.last_sample=probe_clock();
            if(!again||reconnected)loading_probe.early_connection_started=probe_clock();
            if(reconnected&&loading_probe.preset==PROBE_STAGED)++loading_probe.recoveries;
            if(loading_probe.preset==PROBE_STAGED)snprintf(loading_probe.result,sizeof(loading_probe.result),"Early wait: %.0fs, then map preview.",loading_probe.early_seconds);
            else snprintf(loading_probe.result,sizeof(loading_probe.result),"Rush: waiting before map creation. F10 to load.");
            LOG("Loading test: %s early rush PVID_LOADED hold; WorldClient present=%s. Server routing unverified; no world-ready/clock/input modifications.",again?"retained":"started",probe_world()?"YES":"NO");return;
        }
    }
    if(type!=MT_WORLD_CREATED){probe_original_send(connection,type);return;}
    probe_observe_frame(probe_world(),probe_clock());
    if(loading_probe.asset_finish){
        struct WorldClient *wc=probe_world();struct ClientWorld *cw=wc?wc->clientWorld:NULL;
        int verified=cw&&wc->serverConnection==connection&&connection->state==CONNECTED&&
            (!probe_preview_pending()||connection==probe_connection());
        if(verified){
            LOG("Loading test: CreateWorld after early rush finish %.3fs later; origin game=%u world=%u; selected game=%u world=%u floor=%d zoneid=%u seed=%u. Room catch-up requires observer confirmation.",probe_clock()-loading_probe.released,loading_probe.game,loading_probe.world,cw->asWorld.props.gameid,cw->asWorld.id,cw->asWorld.props.floor,cw->asWorld.props.zoneid,cw->asWorld.props.seed);
            loading_probe.asset_finish=0;loading_probe.ready_type=MT_WORLD_CREATED;
            loading_probe.wc=wc;loading_probe.connection=connection;loading_probe.socket=connection->socket;
            loading_probe.game=cw->asWorld.props.gameid;loading_probe.world=cw->asWorld.id;loading_probe.released=probe_clock();loading_probe.post_closed=0;
        }
        if((loading_probe.preset==PROBE_STAGED||loading_probe.room_preview)&&loading_probe.stage==PROBE_STAGE_PENDING&&verified){
            loading_probe.stage=PROBE_STAGE_PREVIEW;loading_probe.state=PROBE_ARMED;
            LOG("Loading test: staged preview received server-selected map; boss=%s lobby=%s game=%u world=%u. Player readiness remains blocked; this does not prove mission retention.",probe_boss(cw)?"YES":"NO",probe_lobby(cw)?"YES":"NO",cw->asWorld.props.gameid,cw->asWorld.id);
        }else{
            if(probe_preview_pending()){
                LOG("Loading test: staged preview readiness had no verified current world; stale/invalid readiness discarded, preview remains pending.");return;
            }
            probe_original_send(connection,type);return;
        }
    }
    if(loading_probe.state==PROBE_RECONNECT){
        struct WorldClient *wc=probe_world();struct ClientWorld *cw=wc?wc->clientWorld:NULL;
        if(!loading_probe.reconnect_issued&&probe_same_world(wc))return; /* duplicate old held ready */
        if(cw&&wc->serverConnection==connection&&connection->state==CONNECTED){
            char zone[64];read_string(cw->asWorld.props.zone,zone,sizeof(zone));
            if(!zone[0])read_string(cw->asWorld.props.music,zone,sizeof(zone));
            loading_probe.mission_retained=cw->asWorld.props.gameid==loading_probe.game&&!strcmp(zone,loading_probe.zone)&&!probe_lobby(cw);
            LOG("Loading test: fresh CreateWorld ready %.3fs after hold end; previous world/game=%u/%u zone=%s; new world/game=%u/%u zone=%s; same mission identity=%s. Server selects spawn; checkpoint agreement still needs observer.",
                probe_clock()-loading_probe.released,loading_probe.world,loading_probe.game,loading_probe.zone,
                cw->asWorld.id,cw->asWorld.props.gameid,zone,loading_probe.mission_retained?"YES":"NO");
            loading_probe.wc=wc;loading_probe.connection=connection;loading_probe.socket=connection->socket;
            loading_probe.world=cw->asWorld.id;loading_probe.game=cw->asWorld.props.gameid;
            LOG("Loading test: fresh world before ready own_t=%d maxT=%d local_player=%s.",probe_client_t(cw),probe_world_t(cw),cw->player?"present":"absent");
            loading_probe.state=PROBE_RESULT;loading_probe.reloaded=1;loading_probe.create_logged=loading_probe.spawn_logged=loading_probe.post_closed=0;
            loading_probe.released=loading_probe.last_post_sample=probe_clock();
            snprintf(loading_probe.result,sizeof(loading_probe.result),"Fresh load: %s. Teammate must verify position.",loading_probe.mission_retained?"mission retained":"mission changed");
        }else{loading_probe.state=PROBE_RESULT;loading_probe.post_closed=1;LOG("Loading test: readiness after reconnect had no valid connected world; native handling retained.");}
        probe_original_send(connection,type);return; /* fresh load never held/rearmed */
    }
    if(loading_probe.state==PROBE_HOLD){
        if(loading_probe.ready_type==MT_PVID_LOADED){
            if(loading_probe.preset==PROBE_STAGED){loading_probe.staged_fallback=2;loading_probe.stage=PROBE_STAGE_PREVIEW;LOG("Loading test: staged early hold could not continue: server created world before early readiness was released; explicit world-ready fallback, six-minute early-hold hypothesis not established.");}
            LOG("Loading test: server created a world despite early rush hold; falling back to world readiness. Loading-less hypothesis not established on this entry.");
            probe_wait_for_world("world arrived before early hold finish");
        }
        if(probe_same_world(probe_world())){
            if(loading_probe.connection==connection)return;
            probe_original_send(connection,type);return;
        }
        if(loading_probe.manual)probe_wait_for_world("connection/world changed before release");
        else probe_release("world changed before release");
    }
    int recovering=loading_probe.state==PROBE_WAIT&&loading_probe.manual;
    if((loading_probe.state!=PROBE_ARMED&&!recovering)||!getbool("plugin_loading_probe","enabled",0)){
        probe_original_send(connection,type);return;
    }
    struct WorldClient *wc=probe_world();struct ClientWorld *cw=wc?wc->clientWorld:NULL;
    if(!cw||wc->serverConnection!=connection||connection->state!=CONNECTED||(!loading_probe.manual&&probe_lobby(cw))){
        probe_original_send(connection,type);return;
    }
    if(loading_probe.manual&&cw->player){
        if(recovering){LOG("Loading test: local player already present on recovery; duplicate ready discarded, F10 still required to finish.");return;}
        if((loading_probe.preset==PROBE_STAGED||loading_probe.room_preview)&&loading_probe.stage==PROBE_STAGE_PREVIEW){
            loading_probe.state=PROBE_RESULT;loading_probe.post_closed=1;loading_probe.stage=PROBE_STAGE_NONE;
            loading_probe.room_preview=0;
            snprintf(loading_probe.result,sizeof(loading_probe.result),"Player already present; preview hold not established.");
            LOG("Loading test: staged preview could not start because local player was already present; ordinary current readiness retained.");
        }
        probe_original_send(connection,type);return;
    } /* existing world's ready; wait for next map */
    if(loading_probe.manual&&!recovering&&loading_probe.preset==PROBE_BOSS&&!probe_boss(cw)){
        LOG("Loading test: Skip boss still armed; intermediate world=%u floor=%d ready passed.",cw->asWorld.id,cw->asWorld.props.floor);
        probe_original_send(connection,type);return;
    }
    if(loading_probe.manual&&!recovering&&loading_probe.preset==PROBE_RUSH&&!loading_probe.room_preview)
        LOG("Loading test: earlier asset-ready phase absent on this entry; rush fallback holds world readiness. Cross-room catch-up remains unverified.");
    if(loading_probe.manual&&loading_probe.preset==PROBE_RUSH&&!loading_probe.room_preview)
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Early-hold fallback: map loaded. F10 reconnects.");
    if(!loading_probe.manual&&(probe_clock()-loading_probe.armed_at>90||!probe_target(cw)||cw->player)){
        LOG("Loading test: skipped; target must be normal Fire Forest or Frozen Blight with no local player. One-shot disarmed.");
        loading_probe.state=PROBE_IDLE;probe_original_send(connection,type);return;
    }
    loading_probe.state=PROBE_HOLD;loading_probe.wc=wc;loading_probe.connection=connection;loading_probe.socket=connection->socket;
    if(loading_probe.preset==PROBE_STAGED||loading_probe.room_preview){
        if(loading_probe.stage==PROBE_STAGE_EARLY){
            loading_probe.staged_fallback=1;
            LOG("Loading test: staged early hold not established; preview fallback started immediately, six-minute timer did not run. Valid headers asset/world=%u/%u; asset-ready attempts empty/protobuf=%u/%u, unbound=%u. Distinguish skipped asset phase from missed binding with this trace.",loading_probe.asset_headers,loading_probe.world_headers,loading_probe.asset_empty_calls,loading_probe.asset_proto_calls,loading_probe.asset_unbound_calls);
        }
        loading_probe.stage=PROBE_STAGE_PREVIEW;
        snprintf(loading_probe.result,sizeof(loading_probe.result),"Preview: %s. F10 reconnects to spawn.",probe_lobby(cw)?"ESCHATON; mission lost":probe_boss(cw)?"boss room":"other map; check zone");
    }
    loading_probe.ready_type=MT_WORLD_CREATED;
    loading_probe.world=cw->asWorld.id;loading_probe.game=cw->asWorld.props.gameid;
    read_string(cw->asWorld.props.zone,loading_probe.zone,sizeof(loading_probe.zone));if(!loading_probe.zone[0])read_string(cw->asWorld.props.music,loading_probe.zone,sizeof(loading_probe.zone));
    if(!recovering)loading_probe.started=probe_clock();
    else{++loading_probe.recoveries;LOG("Loading test: recovered world readiness re-held (%d recoveries); elapsed %.3fs; no player readiness released.",loading_probe.recoveries,probe_clock()-loading_probe.started);}
    loading_probe.last_sample=probe_clock();loading_probe.deadline=loading_probe.manual?0:loading_probe.started+loading_probe.seconds;
    loading_probe.create_logged=0;
    loading_probe.world_t0=loading_probe.world_t1=probe_world_t(cw);loading_probe.objects0=loading_probe.objects1=probe_objects(cw);
    loading_probe.client_t0=loading_probe.client_t1=probe_client_t(cw);
    if(loading_probe.manual)LOG("Loading test: holding WORLD_CREATED until manual F10 reconnect; zone=%s world=%u game=%u maxT=%d objects=%d own_t=%d; physics/time/input continue; local player absent.",loading_probe.zone,loading_probe.world,loading_probe.game,loading_probe.world_t0,loading_probe.objects0,loading_probe.client_t0);
    else LOG("Loading test: holding WORLD_CREATED for %.1fs; maxT=%d objects=%d own_t=%d; native physics/time/input continue; local player absent.",loading_probe.seconds,loading_probe.world_t0,loading_probe.objects0,loading_probe.client_t0);
}
static void loading_probe_frame(void){
    if(!loading_probe_installed)return;double now=probe_clock();struct WorldClient *wc=probe_world();
    probe_camera_sync();
    probe_observe_frame(wc,now);
    if(loading_probe.state==PROBE_ARMED&&!getbool("plugin_loading_probe","enabled",0)){loading_probe.state=PROBE_IDLE;LOG("Loading test: disabled; unused arm canceled.");}
    if(loading_probe.state==PROBE_ARMED&&!loading_probe.manual&&now-loading_probe.armed_at>90){loading_probe.state=PROBE_IDLE;LOG("Loading test: unused arm expired.");}
    if(loading_probe.state==PROBE_WAIT&&!getbool("plugin_loading_probe","enabled",0))probe_cancel();
    if(loading_probe.state==PROBE_WAIT&&loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY&&now-loading_probe.started>=loading_probe.early_seconds)probe_manual_reconnect();
    if(loading_probe.state==PROBE_HOLD){
        if(loading_probe.ready_type==MT_PVID_LOADED){
            if(!probe_same_connection())probe_wait_for_world("early rush connection changed");
            else if(!getbool("plugin_loading_probe","enabled",0))probe_cancel();
            else if(loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY&&now-loading_probe.started>=loading_probe.early_seconds)probe_release("staged early timer completed");
            else if(now-loading_probe.last_sample>=1){loading_probe.last_sample=now;LOG("Loading test: early rush hold %.2fs; WorldClient present=%s; no world readiness released.",now-loading_probe.started,probe_world()?"YES":"NO");}
            return;
        }
        if(!probe_same_world(wc)){
            if(loading_probe.manual)probe_wait_for_world("connection/world changed");else probe_release("connection/world changed");return;
        }
        struct ClientWorld *cw=wc->clientWorld;int t=probe_world_t(cw);
        if(t!=loading_probe.world_t1){++loading_probe.advances;loading_probe.world_t1=t;}
        loading_probe.objects1=probe_objects(cw);
        loading_probe.client_t1=probe_client_t(cw);
        if(cw->player){loading_probe.player_before=1;
            if(loading_probe.manual){loading_probe.state=PROBE_RESULT;loading_probe.released=now;snprintf(loading_probe.result,sizeof(loading_probe.result),"Local player appeared early. F10 can request a fresh reconnect.");LOG("Loading test: local player appeared during manual hold; old readiness discarded without replay or automatic reconnect.");}
            else probe_release("local player appeared before ready; hold ended");return;}
        if(!getbool("plugin_loading_probe","enabled",0)){probe_release("test disabled");return;}
        if(!loading_probe.manual&&now>=loading_probe.deadline){probe_release("automatic release");return;}
        if(now-loading_probe.last_sample>=1){loading_probe.last_sample=now;LOG("Loading test: holding %.2fs; maxT=%d objects=%d own_t=%d local_player=absent.",now-loading_probe.started,t,loading_probe.objects1,loading_probe.client_t1);}
    }
    if(loading_probe.state==PROBE_RECONNECT){
        if(now-loading_probe.released>=30){
            loading_probe.state=PROBE_RESULT;loading_probe.post_closed=1;
            snprintf(loading_probe.result,sizeof(loading_probe.result),"No fresh world observed within 30 seconds.");
            LOG("Loading test: fresh-world reconnect not observed within 30s; one-shot finished, no repeated reconnect or old ready replay.");
        }
        return;
    }
    if(loading_probe.state==PROBE_RESULT&&!loading_probe.spawn_logged&&probe_same_world(wc)&&wc->clientWorld->player){
        loading_probe.spawn_logged=1;LOG("Loading test: first local player observed %.3fs after ready release; own_t=%d game=%u world=%u.",now-loading_probe.released,probe_client_t(wc->clientWorld),loading_probe.game,loading_probe.world);
        struct WorldObject player;
        if(read_memory(wc->clientWorld->player,&player,sizeof(player)))LOG("Loading test: local spawn xmp=%d ymp=%d object=%u.",player.props.xmp,player.props.ymp,player.objId);
    }
    if(loading_probe.state==PROBE_RESULT&&!loading_probe.post_closed){
        if(!probe_same_world(wc)){loading_probe.post_closed=1;LOG("Loading test: connection/world changed %.3fs after release; local player had appeared=%s. Could be a transition or reconnect.",now-loading_probe.released,loading_probe.spawn_logged?"YES":"NO");return;}
        double age=now-loading_probe.released;
        if(age>=60){loading_probe.post_closed=1;LOG("Loading test: same world/connection retained for 60s after release; remote visibility still requires observer confirmation.");}
        else if(now-loading_probe.last_post_sample>=5){loading_probe.last_post_sample=now;LOG("Loading test: post-release %.1fs; received maxT=%d own_t=%d local_player=%s.",age,probe_world_t(wc->clientWorld),probe_client_t(wc->clientWorld),wc->clientWorld->player?"present":"absent");}
    }
}
struct ProbeRect {int x,y,w,h;};
#define PROBE_BUTTONS 8
/* Keep the full control set usable on short windows; client rows below use
   the same height so their reconnect buttons never overlap the help text. */
static int probe_gui_base_height(void){return physical_h<506?388:416;}
static int probe_gui_anchor_y=200;
static int probe_gui_rects(struct ProbeRect *panel,struct ProbeRect buttons[PROBE_BUTTONS],struct ProbeRect *tab){
    if(physical_w<280||physical_h<418)return 0;
    int clients=probe_gui_visible&&probe_clients_height?probe_clients_height():0;
    int below=probe_below_height?probe_below_height():0;
    int base=probe_gui_base_height();
    *panel=(struct ProbeRect){physical_w-380,probe_gui_visible?200:probe_gui_anchor_y,368,base+clients};
    if(panel->x<10)panel->x=10;if(panel->x+panel->w>physical_w-10)panel->w=physical_w-panel->x-10;
    int visible=probe_gui_visible?panel->h:28;
    if(panel->y+visible+below>physical_h-10)panel->y=physical_h-visible-below-10;
    if(panel->y<10)panel->y=10;
    if(probe_gui_visible)probe_gui_anchor_y=panel->y;
    int width=(panel->w-36)/2;
    int pitch=(base-236)/4;
    /* Put Preview next to Rush in the visible flow, keeping action IDs stable. */
    const int order[PROBE_BUTTONS]={0,1,7,2,4,6,3,5};
    for(int slot=0;slot<PROBE_BUTTONS;++slot)buttons[order[slot]]=(struct ProbeRect){panel->x+12+(slot%2)*(width+12),panel->y+82+(slot/2)*pitch,width,26};
    *tab=(struct ProbeRect){physical_w-252,panel->y,240,28};if(tab->x<10)tab->x=10;
    return 1;
}
static int probe_inside(struct ProbeRect r,int x,int y){return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}
static struct ProbeRect probe_hide_ui_rect(struct ProbeRect panel){return (struct ProbeRect){panel.x+panel.w-112,panel.y+9,100,24};}
static unsigned probe_gui_captured;static int probe_gui_pressed=-1,probe_gui_was_open;
static int loading_probe_mouse(int x,int y,int button,int down,int window_w,int window_h){
    if(button<0||button>=8)return 0;unsigned bit=1U<<button;
    int captured=!!(probe_gui_captured&bit);
    if(connectivity_ui_hidden||!loading_probe_installed||!getbool("plugin_loading_probe","enabled",0)||active_controls||window_w<=0||window_h<=0){
        if(!down&&captured){probe_gui_captured&=~bit;probe_gui_pressed=-1;return 1;}return 0;
    }
    struct ProbeRect panel,buttons[PROBE_BUTTONS],tab;if(!probe_gui_rects(&panel,buttons,&tab))return 0;
    x=scaled_mouse(x,physical_w,window_w);y=scaled_mouse(y,physical_h,window_h);
    if(down){
        if(!probe_inside(probe_gui_visible?panel:tab,x,y))return 0;
        probe_gui_captured|=bit;probe_gui_was_open=probe_gui_visible;probe_gui_pressed=-1;
        if(button==0){if(probe_gui_visible&&probe_inside(probe_hide_ui_rect(panel),x,y))probe_gui_pressed=PROBE_BUTTONS+1;
            else if(!probe_gui_visible)probe_gui_pressed=PROBE_BUTTONS;
            else for(int i=0;i<PROBE_BUTTONS;++i)if(probe_inside(buttons[i],x,y))probe_gui_pressed=i;}
        return 1;
    }
    if(!captured)return 0; /* an outside game press must receive its release */
    probe_gui_captured&=~bit;int action=probe_gui_pressed;probe_gui_pressed=-1;
    if(button||probe_gui_was_open!=probe_gui_visible)return 1;
    if(action==PROBE_BUTTONS+1&&probe_gui_visible&&probe_inside(probe_hide_ui_rect(panel),x,y))connectivity_ui_hidden=1;
    else if(action==PROBE_BUTTONS&&probe_inside(tab,x,y))probe_gui_visible=1;
    else if(action>=0&&action<PROBE_BUTTONS&&probe_inside(buttons[action],x,y)){
        if(action<2){if(!paused&&!loading_probe.asset_finish&&loading_probe.state!=PROBE_HOLD&&loading_probe.state!=PROBE_WAIT&&loading_probe.state!=PROBE_RECONNECT){probe_preset=action==0?PROBE_RUSH:PROBE_BOSS;set_option("plugin_loading_probe","mode","manual");probe_arm();}}
        else if(action==2)probe_manual_reconnect();
        else if(action==3)probe_cancel();else if(action==4)probe_camera_toggle();else if(action==5)probe_gui_visible=0;
        else if(action==6){if(!paused&&!loading_probe.asset_finish&&loading_probe.state!=PROBE_HOLD&&loading_probe.state!=PROBE_WAIT&&loading_probe.state!=PROBE_RECONNECT){probe_preset=PROBE_STAGED;set_option("plugin_loading_probe","mode","manual");probe_arm();}}
        else if(action==7)probe_preview_room();
    }
    return 1;
}
static const char *probe_state_label(void){
    if(probe_preview_pending())return "PREVIEW PENDING";
    if(loading_probe.preset==PROBE_RUSH&&loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_PVID_LOADED)return "EARLY HOLD";
    if(loading_probe.room_preview&&loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_WORLD_CREATED)return "ROOM PREVIEW";
    if(loading_probe.preset==PROBE_RUSH&&!loading_probe.room_preview&&loading_probe.ready_type==MT_WORLD_CREATED){
        if(loading_probe.state==PROBE_HOLD)return "HOLDING / FALLBACK";
        if(loading_probe.state==PROBE_WAIT)return "WAITING / FALLBACK";
    }
    if(loading_probe.preset==PROBE_STAGED&&loading_probe.staged_fallback){
        if(loading_probe.state==PROBE_HOLD)return "HOLDING / FALLBACK";
        if(loading_probe.state==PROBE_WAIT)return "WAITING / FALLBACK";
    }
    static const char *labels[]={"IDLE","ARMED","HOLDING","RECONNECTING","RESULT","WAITING / HELD"};
    return loading_probe.state>=PROBE_IDLE&&loading_probe.state<=PROBE_WAIT?labels[loading_probe.state]:"IDLE";
}
/* LoadPVid supplies no live floor/world. Display only a current server-created
   world whose readiness is still held; never label the origin as destination. */
_Static_assert(offsetof(struct WorldProperties,floor)==0x18&&offsetof(struct WorldProperties,title)==0x40,"Mac preview room fields ABI");
static int probe_room_description(char *room,size_t room_size,char *detail,size_t detail_size){
    struct WorldClient *wc=probe_world();
    if(loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_WORLD_CREATED&&probe_same_world(wc)&&!wc->clientWorld->player){
        struct ClientWorld *cw=wc->clientWorld;char title[96];
        read_string(cw->asWorld.props.title,title,sizeof(title));
        if(!title[0])read_string(cw->asWorld.props.zone,title,sizeof(title));
        if(!title[0])read_string(cw->asWorld.props.music,title,sizeof(title));
        snprintf(room,room_size,"Preview: %.28s%s",title[0]?title:"unnamed room",probe_boss(cw)?" [boss]":"");
        if(cw->asWorld.props.floor>=0)snprintf(detail,detail_size,"Floor %lld | map #%u | preview only",(long long)cw->asWorld.props.floor+1,cw->asWorld.id);
        else snprintf(detail,detail_size,"Map #%u | preview only",cw->asWorld.id);
        return 1;
    }
    snprintf(room,room_size,"Room: not sent by server yet");
    snprintf(detail,detail_size,"%s",probe_preview_pending()?"Waiting for a fresh server-selected map":probe_room_preview_available()?"Preview room ends the early wait":"Arm before the rush or boss transition");
    return 0;
}
static void probe_gui_text(const char *value,int x,int y,int size,uint32_t color,double ax,double ay){
    /* Layout uses window coordinates; rasterize at drawable resolution so
       Retina does not halve the apparent label size relative to buttons. */
    draw_cached_text(cache_text_raster(value,(int)lround(size*render_pixel_ratio),color,"TenbyFive"),x,y,ax,ay);
}
static void probe_gui_button(struct ProbeRect r,const char *label,uint32_t color){
    fill_rect(r.x,r.y,r.w,r.h,color,BLENDMODE_BLEND);probe_gui_text(label,r.x+r.w/2,r.y+r.h/2,r.w<145?11:14,0xffffffff,.5,.5);
}
static void loading_probe_overlay(void){
    if(connectivity_ui_hidden||!loading_probe_installed||!getbool("plugin_loading_probe","enabled",0)||active_controls)return;
    struct ProbeRect panel,buttons[PROBE_BUTTONS],tab;if(!probe_gui_rects(&panel,buttons,&tab))return;
    int old_w=*canvas_w,old_h=*canvas_h;*canvas_w=physical_w;*canvas_h=physical_h;char line[256];
    if(!probe_gui_visible){snprintf(line,sizeof(line),"Loading: %s (F8)",probe_state_label());probe_gui_button(tab,line,0xe627394b);}
    else{
        fill_rect(panel.x,panel.y,panel.w,panel.h,0xf0182430,BLENDMODE_BLEND);
        fill_rect(panel.x,panel.y,panel.w,3,0xff07d8bb,BLENDMODE_BLEND);
        probe_gui_text("Loading controls",panel.x+12,panel.y+13,18,0xffffffff,0,0);
        probe_gui_button(probe_hide_ui_rect(panel),"Hide all UI",0xff30434e);
        if(loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT)snprintf(line,sizeof(line),"%s | %.1f seconds held",probe_state_label(),probe_clock()-loading_probe.started);
        else snprintf(line,sizeof(line),"%s",probe_state_label());
        probe_gui_text(line,panel.x+12,panel.y+43,15,0xffffff60,0,0);
        probe_gui_text(probe_manual_mode()?"SC: Rush hold > Preview room > Reconnect":"Normal Forest / Blight | Timed test",panel.x+12,panel.y+63,11,0xffc8d7e4,0,0);
        struct ServerConnection *connection=probe_connection();int connected=connection&&connection->state==CONNECTED;
        int pending=probe_preview_pending();
        int can_arm=connected&&!paused&&!loading_probe.asset_finish&&loading_probe.state!=PROBE_HOLD&&loading_probe.state!=PROBE_WAIT&&loading_probe.state!=PROBE_RECONNECT;
        probe_gui_button(buttons[0],"Rush hold",can_arm?0xff215c53:0xff303a40);probe_gui_button(buttons[1],"Boss hold",can_arm?0xff215c53:0xff303a40);
        probe_gui_button(buttons[2],pending?"Waiting for map":loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY?"Next: preview":loading_probe.ready_type==MT_PVID_LOADED?"Finish loading":"Reconnect",!pending&&(connected||loading_probe.state==PROBE_WAIT)&&loading_probe.state!=PROBE_RECONNECT?0xff315b86:0xff303a40);
        probe_gui_button(buttons[3],"Cancel hold",pending||loading_probe.state==PROBE_ARMED||loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT?0xff49414e:0xff303a40);
        probe_gui_button(buttons[4],probe_camera.active?(probe_camera_available(NULL)?"Free camera: ON":"Free camera: WAIT"):"Free camera",probe_camera.active||probe_camera_available(NULL)?0xff215c53:0xff303a40);probe_gui_button(buttons[5],"Close panel",0xff374755);
        snprintf(line,sizeof(line),"Timed: %.0fs",clamp(getnum("plugin_loading_probe","stage_early_seconds",360),1,1800));
        probe_gui_button(buttons[6],line,can_arm?0xff215c53:0xff303a40);
        probe_gui_button(buttons[7],"Preview room",probe_room_preview_available()?0xff315b86:0xff303a40);
        int info=panel.y+probe_gui_base_height()-140;
        char room[112],detail[112];int showing_room=probe_room_description(room,sizeof(room),detail,sizeof(detail));
        probe_gui_text(room,panel.x+12,info,12,showing_room?0xff88e8bd:0xffc8d7e4,0,0);
        probe_gui_text(detail,panel.x+12,info+18,11,0xffc8d7e4,0,0);
        snprintf(line,sizeof(line),"Loading: %s",loading_probe.ready_type==MT_PVID_LOADED?"waiting before map creation":loading_probe.advances?"world updates observed":"waiting for map");
        if(loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_EARLY)snprintf(line,sizeof(line),"Early hold: %.1f / %.0fs (then preview)",loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT?probe_clock()-loading_probe.started:0,loading_probe.early_seconds);
        if(pending)snprintf(line,sizeof(line),"Preview pending; waiting for server-selected map");
        int fallback=loading_probe.preset==PROBE_STAGED&&loading_probe.staged_fallback&&loading_probe.stage==PROBE_STAGE_PREVIEW&&(loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT);
        if(fallback)snprintf(line,sizeof(line),"Fallback: early wait unavailable; map loaded");
        else if(loading_probe.preset==PROBE_RUSH&&showing_room&&!loading_probe.room_preview)snprintf(line,sizeof(line),"Fallback: early wait unavailable; map loaded");
        else if(loading_probe.room_preview&&showing_room)snprintf(line,sizeof(line),"Preview can time out; reconnect when ready.");
        /* Replace the old diagnostic block with short, task-oriented help. */
        const char *help[PROBE_BUTTONS]={"Wait before map loads.","Hold spawn in next boss.",pending?"Waiting for server map.":loading_probe.ready_type==MT_PVID_LOADED?"Continue loading.":"Fresh reconnect to spawn.","Disarm or finish hold.","WASD/arrows; Shift fast.","Hide UI; hold stays on.","Wait, then show preview.","View map; keep held."};
        const char *compact[PROBE_BUTTONS]={"Wait before entry.","Hold next boss.","Continue / spawn.","End / disarm hold.","WASD; Shift fast.","Hide; hold stays.","Wait, then view.","View; keep held."};
        for(int i=0;i<PROBE_BUTTONS;++i)probe_gui_text(buttons[i].w<145?compact[i]:help[i],buttons[i].x,buttons[i].y+29,buttons[i].w<145?9:10,0xffb5c8d8,0,0);
        probe_gui_text(line,panel.x+12,info+36,11,fallback||showing_room?0xffffb45c:0xffc8d7e4,0,0);
        probe_gui_text(probe_record_failed?"Log save failed; check folder access.":probe_record?"Logs saved automatically in loading-tests/":"Logs save automatically when a hold is armed.",panel.x+12,info+56,11,probe_record_failed?0xffff7070:0xff88e8bd,0,0);
        probe_gui_text("F8 panel/show UI | F9 arm | F10 next/finish",panel.x+12,info+76,11,0xffc8d7e4,0,0);
        probe_gui_text("Server chooses spawn; preview is not reserved.",panel.x+12,info+96,11,0xffc8d7e4,0,0);
        snprintf(line,sizeof(line),"%.48s",loading_probe.result);
        probe_gui_text(line,panel.x+12,info+116,10,0xffc8d7e4,0,0);
    }
    *canvas_w=old_w;*canvas_h=old_h;
}
#if !defined(SBPE_TEST) || defined(LOADING_PROBE_HOOK_TEST)
#include <sys/mman.h>
static void network_guard_jump(uint8_t*,uintptr_t);
static int network_patch_apply(uint8_t*,const uint8_t*,size_t,const uint8_t*,size_t);
static const uint8_t probe_send_original[17]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x48,0x89,0xfb,0x4c,0x8d,0x73,0x58};
static const uint8_t probe_proto_original[17]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x78};
static int loading_probe_proto_hook(uint8_t *site){
    if(memcmp(site,probe_proto_original,sizeof(probe_proto_original)))return 0;long page=sysconf(_SC_PAGESIZE);if(page<=0)return 0;
    uint8_t *tr=mmap(NULL,(size_t)page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(tr==MAP_FAILED)return 0;
    memcpy(tr,probe_proto_original,17);network_guard_jump(tr+17,(uintptr_t)site+17);
    if(mprotect(tr,(size_t)page,PROT_READ|PROT_EXEC)){munmap(tr,(size_t)page);return 0;}
    probe_original_proto=(void*)tr;uint8_t code[14];network_guard_jump(code,(uintptr_t)loading_probe_send_proto);
    if(network_patch_apply(site,probe_proto_original,17,code,sizeof(code)))return 1;
    probe_original_proto=NULL;munmap(tr,(size_t)page);return 0;
}
static int loading_probe_hook(uint8_t *site){
    if(memcmp(site,probe_send_original,sizeof(probe_send_original)))return 0;long page=sysconf(_SC_PAGESIZE);if(page<=0)return 0;
    uint8_t *tr=mmap(NULL,(size_t)page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(tr==MAP_FAILED)return 0;
    memcpy(tr,probe_send_original,17);network_guard_jump(tr+17,(uintptr_t)site+17);
    if(mprotect(tr,(size_t)page,PROT_READ|PROT_EXEC)){munmap(tr,(size_t)page);return 0;}
    probe_original_send=(void*)tr;uint8_t code[14];network_guard_jump(code,(uintptr_t)loading_probe_send);
    if(network_patch_apply(site,probe_send_original,17,code,sizeof(code)))return 1;
    probe_original_send=NULL;munmap(tr,(size_t)page);return 0;
}
static void install_loading_probe(void){
    if(!getbool("plugin_loading_probe","enabled",0))return;
    probe_gui_visible=getbool("plugin_loading_probe","gui_visible",1);atexit(probe_record_close);
    if(!loading_probe_proto_hook((void*)PTR(0x100085db0))){LOG("Loading experiment message diagnostic hook mismatch; test disabled.");return;}
    if(loading_probe_hook((void*)PTR(0x100085d10))){loading_probe_installed=1;LOG("Loading experiment available, UNARMED; mode=%s; F8 panel, F9 arms next map, F10 reconnects. Manual mode has no hold timer; native physics/time/input continue. Test logs save automatically in loading-tests/.",probe_manual_mode()?"any-map manual":"normal Forest/Blight timed");}
    else LOG("Loading experiment hook mismatch; test disabled.");
}
#else
static void install_loading_probe(void){}
#endif
#endif
