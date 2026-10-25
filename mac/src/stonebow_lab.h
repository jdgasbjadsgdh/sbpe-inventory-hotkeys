/* Stonebow launch experiment. Native hooks are passive until a trial is
 * explicitly selected. All game calls run on its own thread. Metadata uses
 * a bounded SPSC queue; the disk writer never holds up a game callback.
 * A local createObj event is NOT confirmation of server-accepted damage. */
#ifndef SBPE_STONEBOW_LAB_H
#define SBPE_STONEBOW_LAB_H
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>

/* Generated C inheritance cannot represent C++ tail-padding reuse. Keep
 * dedicated native accessors here rather than changing shared ABI structs. */
static int bow_cd(struct Gun *g){int v;memcpy(&v,(uint8_t*)g+0xb8,4);return v;}
static void bow_set_cd(struct Gun *g,int v){memcpy((uint8_t*)g+0xb8,&v,4);}
static int bow_heat(struct Gun *g){int v;memcpy(&v,(uint8_t*)g+0xb4,4);return v;}
static struct Action *bow_action(struct PlayerCharacter *pc){struct Action *v;memcpy(&v,(uint8_t*)pc+0x1b0,8);return v;}
static int bow_ammo(struct PlayerCharacter *pc){int v;memcpy(&v,(uint8_t*)pc+0x2d4,4);return v;}
_Static_assert(offsetof(struct Item,slotPos)==0x58,"Item slot ABI");
_Static_assert(offsetof(struct Gun,bullets)==0xc0,"Gun bullets ABI");
_Static_assert(offsetof(struct ItemDescription,cooldown)==0x88,"Descriptor cooldown ABI");
enum {BOW_PASSIVE,BOW_CYCLE,BOW_RESET,BOW_DIRECT};
enum {BOW_SESSION,BOW_USE_BEFORE,BOW_USE_AFTER,BOW_SHOOT_BEFORE,BOW_SHOOT_AFTER,
      BOW_PROJECTILE,BOW_MOVE_BEFORE,BOW_MOVE_AFTER,BOW_SAMPLE,BOW_TRIAL,BOW_STOP};
static const char *bow_events[]={"session","use_before","use_after","shoot_before","shoot_after",
    "projectile_local","move_before","move_after","sample","trial","stop"};
struct BowEvent {
    uint64_t wall_ms,mono_us,seq; uint32_t event,mode,item,action,shot,world,player,obj,source,caller;
    int sim_t,server_t,cooldown,heat,slot,type,base_cooldown,anim_speed,cooldown_boost;
    int anim,pose,posetime,ammo,buffer,bullets,a,b,remaining;
};
#define BOW_QUEUE 4096
#define BOW_MAX_RECORDS 12000
static struct {
    struct BowEvent events[BOW_QUEUE];
    atomic_uint head,tail,dropped,status,stop; /* status: 1 opening, 2 writing, 3 saved, 4 error */
    pthread_t thread; int joinable; char path[4096];
} bow_writer;
struct BowIdentity {const void *ptr;unsigned id;};
struct BowAction {const void *ptr;unsigned action,item,shot;};
static struct {
    int installed,visible,recording,mode,remaining,use_depth,injecting,cycle_pending,cycle_wait;
    struct WorldClient *wc;struct ClientWorld *cw;struct PlayerCharacter *pc;struct ServerConnection *connection;
    uint32_t world,player,game;int socket;struct ClientSubWorld *own;
    struct Gun *first,*second;int inventory_slot;
    unsigned identity_next,identity_used,action_used,seq,shot,active_shot,active_item,created,emitted,attempts;
    unsigned last_emitted_shot,cycle_emissions,launch_groups;int last_emit_sim;
    struct BowIdentity identities[256];struct BowAction actions[64];
    double started,deadline,last_sample,last_press,last_emit,min_gap,cycle_deadline;
    int last_cooldown;char result[120];
} bow_lab;
static double (*bow_clock)(void)=monotime;
static struct WorldClient *(*bow_world)(void)=world_client;
static struct Item *(*bow_get_item)(struct PlayerCharacter*,int);
static void (*bow_modify)(struct WorldClient*,const struct ModifyInv*);
static void (*bow_original_shoot)(struct Gun*,int);
static void (*bow_original_use)(struct Gun*,unsigned);
static unsigned char (*bow_original_move)(struct PlayerCharacter*,int,int);
static void (*bow_original_create)(struct ClientWorld*,struct ClientObject*,int);
static unsigned char (*bow_native_can_action)(struct PlayerCharacter*);
static int bow_direct_enabled(void){return bow_native_can_action&&getbool("plugin_stonebow_lab","direct_fire_enabled",0);}
static unsigned char bow_game_can_action(struct PlayerCharacter *pc){
    /* Use the same virtual slot as Gun::doUse and ClientObject::doAction,
       preserving per-shell overrides rather than calling one base method. */
    void **vtable=pc?*(void***)pc:NULL;
    return vtable&&vtable[0x148/8]?((unsigned char(*)(struct PlayerCharacter*))vtable[0x148/8])(pc):0;
}
static int bow_character(struct PlayerCharacter *pc){
    const char *name=class_name(pc);
    return !strcmp(name,"Wildfire")||!strcmp(name,"Duskwing")||!strcmp(name,"Ironclad")||
        !strcmp(name,"Fabricator")||!strcmp(name,"Ancient")||!strcmp(name,"PlayerCharacter");
}

static unsigned bow_identity(const void *p){
    if(!p)return 0;
    for(unsigned i=0;i<bow_lab.identity_used;++i)if(bow_lab.identities[i].ptr==p)return bow_lab.identities[i].id;
    unsigned id=++bow_lab.identity_next;
    if(bow_lab.identity_used<256)bow_lab.identities[bow_lab.identity_used++]=(struct BowIdentity){p,id};
    return id;
}
static unsigned bow_action_id(struct Action *p){
    if(!p)return 0;unsigned n=bow_lab.action_used<64?bow_lab.action_used:64;
    for(unsigned i=0;i<n;++i){struct BowAction *a=&bow_lab.actions[(bow_lab.action_used-1-i)%64];if(a->ptr==p)return a->action;}
    return bow_identity(p);
}
static void bow_write_event(FILE *f,const struct BowEvent *e){
    fprintf(f,"{\"event\":\"%s\",\"seq\":%llu,\"wall_ms\":%llu,\"mono_us\":%llu,"
        "\"mode\":%u,\"item\":%u,\"action\":%u,\"shot\":%u,\"world\":%u,\"player\":%u,\"object\":%u,\"source\":%u,\"caller\":%u,"
        "\"sim_t\":%d,\"server_t\":%d,\"cooldown\":%d,\"heat\":%d,\"slot\":%d,\"type\":%d,\"base_cooldown\":%d,\"anim_speed_boost\":%d,\"cooldown_boost\":%d,"
        "\"anim\":%d,\"pose\":%d,\"posetime\":%d,\"ammo\":%d,\"input_buffer\":%d,\"bullets\":%d,\"a\":%d,\"b\":%d,\"remaining\":%d}\n",
        bow_events[e->event],(unsigned long long)e->seq,(unsigned long long)e->wall_ms,(unsigned long long)e->mono_us,
        e->mode,e->item,e->action,e->shot,e->world,e->player,e->obj,e->source,e->caller,
        e->sim_t,e->server_t,e->cooldown,e->heat,e->slot,e->type,e->base_cooldown,e->anim_speed,e->cooldown_boost,
        e->anim,e->pose,e->posetime,e->ammo,e->buffer,e->bullets,e->a,e->b,e->remaining);
}
static void *bow_writer_run(void *unused){
    (void)unused;
    int fd=open(bow_writer.path,O_WRONLY|O_CREAT|O_EXCL,0600);
    FILE *f=fd>=0?fdopen(fd,"w"):NULL;if(!f){if(fd>=0)close(fd);atomic_store(&bow_writer.status,4);return NULL;}
    fprintf(f,"{\"schema\":1,\"plugin\":\"stonebow-lab\",\"build\":\"2026.10.23-stonebow.2\",\"mode_names\":[\"passive\",\"cycle\",\"reset\",\"direct\"],\"local_observations_only\":true}\n");
    atomic_store(&bow_writer.status,2);unsigned saved=0;int failed=0;
    for(;;){
        unsigned tail=atomic_load_explicit(&bow_writer.tail,memory_order_relaxed);
        unsigned head=atomic_load_explicit(&bow_writer.head,memory_order_acquire);
        while(tail!=head){bow_write_event(f,&bow_writer.events[tail%BOW_QUEUE]);++tail;++saved;atomic_store_explicit(&bow_writer.tail,tail,memory_order_release);}
        if(ferror(f)){failed=1;break;}
        if(atomic_load_explicit(&bow_writer.stop,memory_order_acquire)&&tail==atomic_load_explicit(&bow_writer.head,memory_order_acquire))break;
        if(fflush(f)){failed=1;break;}
        struct timespec nap={0,20000000};nanosleep(&nap,NULL);
    }
    if(!failed)fprintf(f,"{\"event\":\"writer_summary\",\"records\":%u,\"dropped\":%u}\n",saved,atomic_load(&bow_writer.dropped));
    if(fclose(f))failed=1;atomic_store(&bow_writer.status,failed?4:3);return NULL;
}
static int bow_push(struct BowEvent e){
    if(!bow_lab.recording||atomic_load(&bow_writer.status)==4)return 0;
    e.seq=++bow_lab.seq;
    if(e.seq>BOW_MAX_RECORDS){atomic_fetch_add(&bow_writer.dropped,1);return 0;}
    unsigned head=atomic_load_explicit(&bow_writer.head,memory_order_relaxed);
    if(head-atomic_load_explicit(&bow_writer.tail,memory_order_acquire)>=BOW_QUEUE){atomic_fetch_add(&bow_writer.dropped,1);return 0;}
    bow_writer.events[head%BOW_QUEUE]=e;atomic_store_explicit(&bow_writer.head,head+1,memory_order_release);return 1;
}
static int bow_is_stonebow(struct Gun *g,struct PlayerCharacter *pc){
    return g&&g->asItem.user==pc&&g->asItem.desc&&g->asItem.desc->type==144&&!strcmp(class_name(g),"Gun");
}
static int bow_context_matches(struct WorldClient *wc){
    return wc&&wc==bow_lab.wc&&wc->clientWorld==bow_lab.cw&&wc->serverConnection==bow_lab.connection&&
        wc->serverConnection&&wc->serverConnection->state==CONNECTED&&wc->clientWorld->mySubWorld&&
        wc->serverConnection->socket==bow_lab.socket&&wc->clientWorld->mySubWorld==bow_lab.own&&wc->clientWorld->asWorld.props.gameid==bow_lab.game&&
        wc->clientWorld->asWorld.id==bow_lab.world&&wc->clientWorld->player==(struct Player*)bow_lab.pc&&
        ((struct WorldObject*)bow_lab.pc)->objId==bow_lab.player;
}
static int bow_context(void){return bow_lab.recording&&bow_context_matches(bow_world());}
static int bow_focused(void){return !get_keyboard_focus||(window_ptr&&get_keyboard_focus()==*window_ptr);}
static int bow_can_test(void){
    struct Stage *st=stage_ptr?*stage_ptr:NULL;
    return bow_context()&&!paused&&!active_controls&&(!st||!st->textInputTarget)&&bow_focused()&&
        !probe_camera.active&&loading_probe.state!=PROBE_HOLD&&loading_probe.state!=PROBE_WAIT&&loading_probe.state!=PROBE_RECONNECT&&
        ((struct WorldObject*)bow_lab.pc)->props.hitpoints>0&&bow_lab.cw->inputBuffer>=250&&getbool("plugin_stonebow_lab","enabled",0);
}
static struct BowEvent bow_event(unsigned kind,struct Gun *g){
    struct BowEvent e={0};struct timespec now;clock_gettime(CLOCK_REALTIME,&now);
    e.wall_ms=(uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;e.mono_us=(uint64_t)(bow_clock()*1e6);
    e.event=kind;e.mode=bow_lab.mode;e.remaining=bow_lab.remaining;e.world=bow_lab.world;e.player=bow_lab.player;
    e.sim_t=probe_client_t(bow_lab.cw);e.server_t=probe_world_t(bow_lab.cw);e.buffer=bow_lab.cw->inputBuffer;
    struct ClientObject *pc=(struct ClientObject*)bow_lab.pc;struct Action *action=bow_action(bow_lab.pc);
    e.action=bow_action_id(action);e.anim=action?action->animId:-1;
    e.pose=pc->asNativeObject.asWorldObject.props.pose;e.posetime=pc->asNativeObject.asWorldObject.props.posetime;e.ammo=bow_ammo(bow_lab.pc);
    if(g){e.item=bow_identity(g);e.slot=g->asItem.slotPos;e.type=g->asItem.desc->type;e.base_cooldown=g->asItem.desc->cooldown;
        e.anim_speed=g->asItem.animSpeedBoost;e.cooldown_boost=g->asItem.cooldownBoost;e.cooldown=bow_cd(g);e.heat=bow_heat(g);e.bullets=vector_count(g->bullets,sizeof(struct ObjKey));}
    return e;
}
static void bow_trial_cancel(const char *why){
    if(bow_lab.mode||bow_lab.cycle_pending||bow_lab.cycle_wait){if(bow_context()){struct BowEvent e=bow_event(BOW_TRIAL,NULL);e.a=-1;bow_push(e);}
        snprintf(bow_lab.result,sizeof(bow_lab.result),"%s",why);}
    bow_lab.mode=BOW_PASSIVE;bow_lab.remaining=0;bow_lab.cycle_pending=bow_lab.cycle_wait=0;
}
static void bow_stop(const char *why){
    bow_trial_cancel(why);
    if(bow_lab.recording){if(bow_context())bow_push(bow_event(BOW_STOP,NULL));bow_lab.recording=0;atomic_store_explicit(&bow_writer.stop,1,memory_order_release);}
    snprintf(bow_lab.result,sizeof(bow_lab.result),"%s",why);
}
static void bow_shutdown(void){
    /* UI/world objects may already have been destroyed during exit. */
    bow_lab.recording=0;bow_lab.mode=BOW_PASSIVE;atomic_store_explicit(&bow_writer.stop,1,memory_order_release);
    if(bow_writer.joinable){pthread_join(bow_writer.thread,NULL);bow_writer.joinable=0;}
}
static int bow_start(void){
    if(bow_lab.recording)return bow_context();
    if(!bow_lab.installed||!getbool("plugin_stonebow_lab","enabled",0))return 0;
    unsigned status=atomic_load(&bow_writer.status);
    if(bow_writer.joinable){if(status!=3&&status!=4){snprintf(bow_lab.result,sizeof(bow_lab.result),"Finishing previous log; try again.");return 0;}pthread_join(bow_writer.thread,NULL);bow_writer.joinable=0;}
    struct WorldClient *wc=bow_world();struct PlayerCharacter *pc=wc&&wc->clientWorld?(void*)wc->clientWorld->player:NULL;
    if(!wc||!pc||!wc->serverConnection||wc->serverConnection->state!=CONNECTED||!wc->clientWorld->mySubWorld||
       !bow_get_item||!bow_character(pc)){
        snprintf(bow_lab.result,sizeof(bow_lab.result),"Spawn with Stonebow equipped.");return 0;
    }
    struct Gun *g=(void*)bow_get_item(pc,0x100);
    if(!bow_is_stonebow(g,pc)){snprintf(bow_lab.result,sizeof(bow_lab.result),"Equip Stonebow in the main slot first.");return 0;}
    const char *root=getenv("SBPE_ROOT");char dir[4096];struct timespec stamp;clock_gettime(CLOCK_REALTIME,&stamp);
    if(!root||snprintf(dir,sizeof(dir),"%s/stonebow-tests",root)>=(int)sizeof(dir)||(mkdir(dir,0700)&&errno!=EEXIST)||
       snprintf(bow_writer.path,sizeof(bow_writer.path),"%s/stonebow-%lld-%09ld-%d.jsonl",dir,(long long)stamp.tv_sec,stamp.tv_nsec,getpid())>=(int)sizeof(bow_writer.path)){
        snprintf(bow_lab.result,sizeof(bow_lab.result),"Cannot create stonebow-tests folder.");return 0;
    }
    int installed=bow_lab.installed,visible=bow_lab.visible;memset(&bow_lab,0,sizeof(bow_lab));bow_lab.installed=installed;bow_lab.visible=visible;
    bow_lab.wc=wc;bow_lab.cw=wc->clientWorld;bow_lab.pc=pc;bow_lab.connection=wc->serverConnection;bow_lab.world=wc->clientWorld->asWorld.id;bow_lab.player=((struct WorldObject*)pc)->objId;
    bow_lab.socket=wc->serverConnection->socket;bow_lab.own=wc->clientWorld->mySubWorld;bow_lab.game=wc->clientWorld->asWorld.props.gameid;
    bow_lab.started=bow_clock();bow_lab.last_press=-100;bow_lab.last_sample=-100;
    atomic_store(&bow_writer.head,0);atomic_store(&bow_writer.tail,0);atomic_store(&bow_writer.dropped,0);atomic_store(&bow_writer.stop,0);atomic_store(&bow_writer.status,1);
    if(pthread_create(&bow_writer.thread,NULL,bow_writer_run,NULL)){atomic_store(&bow_writer.status,4);snprintf(bow_lab.result,sizeof(bow_lab.result),"Could not start log writer.");return 0;}
    bow_writer.joinable=1;bow_lab.recording=1;bow_push(bow_event(BOW_SESSION,g));
    snprintf(bow_lab.result,sizeof(bow_lab.result),"Recording normal shots (2 minute maximum).");LOG("Stonebow lab recording: %s",bow_writer.path);return 1;
}
static void bow_arm(int mode){
    if(mode<BOW_CYCLE||mode>BOW_DIRECT)return;
    if(mode==BOW_DIRECT&&!bow_direct_enabled()){
        bow_trial_cancel("Direct fire disabled after native-action crash.");
        snprintf(bow_lab.result,sizeof(bow_lab.result),"Direct fire disabled; use baseline or reset.");return;
    }
    if(!bow_start())return;
    bow_trial_cancel("Previous trial ended");
    if(!bow_can_test()){snprintf(bow_lab.result,sizeof(bow_lab.result),"Focus the game, close chat, and finish loading.");return;}
    bow_lab.first=(void*)bow_get_item(bow_lab.pc,0x100);bow_lab.second=NULL;
    if(!bow_is_stonebow(bow_lab.first,bow_lab.pc)){snprintf(bow_lab.result,sizeof(bow_lab.result),"Equip Stonebow before a trial.");return;}
    if(mode==BOW_CYCLE){
        for(int i=0;i<32;++i){struct Gun *g=(void*)bow_get_item(bow_lab.pc,0xf00+i);
            if(g!=bow_lab.first&&bow_is_stonebow(g,bow_lab.pc)){bow_lab.second=g;bow_lab.inventory_slot=0xf00+i;break;}}
        if(!bow_lab.second||!bow_modify){snprintf(bow_lab.result,sizeof(bow_lab.result),"Cycle needs a second Stonebow in inventory.");return;}
    }
    bow_lab.mode=mode;bow_lab.remaining=3;bow_lab.deadline=bow_clock()+20;bow_lab.last_press=-100;bow_lab.cycle_emissions=0;
    struct BowEvent e=bow_event(BOW_TRIAL,bow_lab.first);e.a=mode;e.b=bow_lab.inventory_slot;bow_push(e);
    snprintf(bow_lab.result,sizeof(bow_lab.result),mode==BOW_CYCLE?"Fire 3 shots; waits for each actual launch.":"Press main attack 3 times; each press is logged.");
}
static void bow_shoot(struct Gun *g,int arg){
    int tracked=bow_context()&&bow_is_stonebow(g,bow_lab.pc);unsigned previous_shot=bow_lab.active_shot,previous_item=bow_lab.active_item,shot=0;
    /* ClientObject::doAction aborts when canDoAction is false. Emitting a
       projectile does NOT end its GunAction. Only injected calls are gated;
       every ordinary native call is still forwarded unchanged. */
    if(bow_lab.injecting&&(!tracked||!bow_native_can_action||!bow_native_can_action(bow_lab.pc))){
        if(tracked){struct BowEvent e=bow_event(BOW_TRIAL,g);e.a=-2;bow_push(e);}
        return;
    }
    struct Action *before=tracked?bow_action(bow_lab.pc):NULL;
    if(tracked){shot=++bow_lab.shot;bow_lab.active_shot=shot;bow_lab.active_item=bow_identity(g);
        struct BowEvent e=bow_event(BOW_SHOOT_BEFORE,g);e.shot=shot;e.source=bow_lab.injecting?2:bow_lab.use_depth?1:0;
        uintptr_t caller=(uintptr_t)__builtin_return_address(0);
        e.caller=caller>=PTR(0x100000000)&&caller<PTR(0x1001e0000)?(uint32_t)(caller-PTR(0x100000000)):0;bow_push(e);}
    bow_original_shoot(g,arg);
    if(tracked&&bow_context()){
        bow_lab.last_cooldown=bow_cd(g);
        struct Action *action=bow_action(bow_lab.pc);
        int created=action&&action!=before&&!strcmp(class_name(action),"GunAction");
        if(created){
            unsigned item=bow_identity(g),aid=++bow_lab.identity_next;
            bow_lab.actions[bow_lab.action_used++%64]=(struct BowAction){action,aid,item,shot};++bow_lab.created;
        }
        struct BowEvent e=bow_event(BOW_SHOOT_AFTER,g);e.shot=shot;e.source=created;bow_push(e);
    }
    bow_lab.active_shot=previous_shot;bow_lab.active_item=previous_item;
}
static void bow_use(struct Gun *g,unsigned arg){
    int tracked=bow_context()&&bow_is_stonebow(g,bow_lab.pc);
    if(!tracked){bow_original_use(g,arg);return;}
    struct BowEvent e=bow_event(BOW_USE_BEFORE,g);e.a=(int)arg;bow_push(e);++bow_lab.attempts;
    if(bow_lab.mode&&!bow_can_test())bow_trial_cancel("Trial stopped: focus, loading or input budget changed.");
    if(bow_lab.mode&&bow_clock()>bow_lab.deadline)bow_trial_cancel("Trial expired; recording continues.");
    if(bow_lab.mode&&g!=bow_lab.first&&(bow_lab.mode!=BOW_CYCLE||g!=bow_lab.second))bow_trial_cancel("Weapon changed; trial stopped.");
    if(bow_lab.mode&&g->asItem.slotPos!=0x100)bow_trial_cancel("Weapon unequipped; trial stopped.");
    int trial=bow_lab.mode;
    if(trial==BOW_DIRECT&&!bow_direct_enabled()){bow_trial_cancel("Direct fire disabled; normal firing restored.");trial=BOW_PASSIVE;}
    if((trial==BOW_RESET||trial==BOW_DIRECT)&&bow_lab.remaining>0&&bow_clock()-bow_lab.last_press>=.15){
        /* Native eligibility includes both the active action and terrain.
           Never cancel an action or bypass this invariant to accelerate fire. */
        if(trial==BOW_DIRECT&&!bow_native_can_action(bow_lab.pc)){
            e=bow_event(BOW_TRIAL,g);e.a=-2;bow_push(e);
            snprintf(bow_lab.result,sizeof(bow_lab.result),"Attack still active or terrain blocks firing.");
            bow_original_use(g,arg);
        }
        else {
            bow_lab.last_press=bow_clock();--bow_lab.remaining;e=bow_event(BOW_TRIAL,g);e.a=trial;e.b=bow_cd(g);bow_push(e);
            ++bow_lab.use_depth;
            if(trial==BOW_RESET){if(bow_cd(g)>0)bow_set_cd(g,0);bow_original_use(g,arg);}
            else{bow_lab.injecting=1;bow_shoot(g,0);bow_lab.injecting=0;}
            --bow_lab.use_depth;
            if(!bow_lab.remaining)bow_trial_cancel("Trial complete; recording continues.");
        }
    }else{++bow_lab.use_depth;bow_original_use(g,arg);--bow_lab.use_depth;}
    if(bow_context())bow_push(bow_event(BOW_USE_AFTER,g));
}
static unsigned char bow_move(struct PlayerCharacter *pc,int from,int to){
    int tracked=bow_context()&&pc==bow_lab.pc;struct Gun *g=tracked?(void*)bow_get_item(pc,0x100):NULL;
    if(tracked){struct BowEvent e=bow_event(BOW_MOVE_BEFORE,bow_is_stonebow(g,pc)?g:NULL);e.a=from;e.b=to;bow_push(e);}
    unsigned char result=bow_original_move(pc,from,to);
    if(tracked&&bow_context()){g=(void*)bow_get_item(pc,0x100);struct BowEvent e=bow_event(BOW_MOVE_AFTER,bow_is_stonebow(g,pc)?g:NULL);e.a=from;e.b=to;e.source=result;bow_push(e);}
    return result;
}
static void bow_create(struct ClientWorld *cw,struct ClientObject *obj,int arg){
    /* Observe after native registration; preserve the native ownership path. */
    bow_original_create(cw,obj,arg);
    if(!bow_context()||cw!=bow_lab.cw||!obj)return;
    const char *cl=class_name(obj);if(strcmp(cl,"Bullet")&&strcmp(cl,"BounceBullet"))return;
    char vid[32];read_string(obj->asNativeObject.asWorldObject.props.vid,vid,sizeof(vid));if(strcmp(vid,"stonebow"))return;
    struct BowEvent e=bow_event(BOW_PROJECTILE,NULL);e.obj=obj->asNativeObject.asWorldObject.objId;e.a=arg;
    e.item=bow_lab.active_item;e.shot=bow_lab.active_shot;
    struct Action *action=bow_action(bow_lab.pc);
    if(!e.shot){unsigned n=bow_lab.action_used<64?bow_lab.action_used:64;
        for(unsigned i=0;i<n;++i){struct BowAction *a=&bow_lab.actions[(bow_lab.action_used-1-i)%64];if(a->ptr==action){
            e.item=a->item;e.shot=a->shot;break;}}}
    bow_push(e);++bow_lab.emitted;
    if(e.shot&&e.shot!=bow_lab.last_emitted_shot){
        if(bow_lab.launch_groups){double gap=(e.sim_t-bow_lab.last_emit_sim)/1000.0;if(gap>=0&&(bow_lab.launch_groups==1||gap<bow_lab.min_gap))bow_lab.min_gap=gap;}
        ++bow_lab.launch_groups;bow_lab.last_emit_sim=e.sim_t;
    }
    if(e.shot){bow_lab.last_emitted_shot=e.shot;
        if(bow_lab.mode==BOW_CYCLE&&e.shot!=bow_lab.cycle_emissions){
            bow_lab.cycle_emissions=e.shot;if(--bow_lab.remaining>0)bow_lab.cycle_pending=1;
            else bow_trial_cancel("Cycle complete; recording continues.");
        }
    }
}
static void bow_frame(void){
    if(!bow_lab.installed)return;
    if(!bow_lab.recording){
        if(atomic_load(&bow_writer.status)==3&&!strcmp(bow_lab.result,"Stopped; saving log."))snprintf(bow_lab.result,sizeof(bow_lab.result),"Log saved in stonebow-tests/.");
        return;
    }
    if(!bow_context()){bow_stop("Map or connection changed; log saved.");return;}
    if(!getbool("plugin_stonebow_lab","enabled",0)){bow_stop("Stonebow test disabled; log saved.");return;}
    if(atomic_load(&bow_writer.status)==4){bow_stop("Log writer failed; test stopped.");return;}
    double now=bow_clock();
    if(now-bow_lab.started>=120||bow_lab.seq>=BOW_MAX_RECORDS-1){bow_stop("Recording limit reached; log saved.");return;}
    if(bow_lab.mode&&(!bow_can_test()||now>bow_lab.deadline))bow_trial_cancel("Trial ended; recording continues.");
    if(bow_lab.mode==BOW_DIRECT&&!bow_direct_enabled())bow_trial_cancel("Direct fire disabled; normal firing restored.");
    if(bow_lab.mode==BOW_CYCLE){
        struct Gun *main=(void*)bow_get_item(bow_lab.pc,0x100),*inv=(void*)bow_get_item(bow_lab.pc,bow_lab.inventory_slot);
        int pair=(main==bow_lab.first&&inv==bow_lab.second)||(main==bow_lab.second&&inv==bow_lab.first);
        if(!pair)bow_trial_cancel("Equipment changed; cycle stopped.");
        else if(bow_lab.cycle_wait){
            struct Gun *expected=bow_lab.remaining==2?bow_lab.second:bow_lab.first;
            if(main==expected){bow_lab.cycle_wait=0;snprintf(bow_lab.result,sizeof(bow_lab.result),"Bow switched; fire the next shot.");}
            else if(now>bow_lab.cycle_deadline)bow_trial_cancel("Inventory swap timed out; cycle stopped.");
        }else if(bow_lab.cycle_pending){
            bow_lab.cycle_pending=0;bow_lab.cycle_wait=1;bow_lab.cycle_deadline=now+2;
            struct ModifyInv request={0};request.classptr=(void*)PTR(0x1001f2210);request.command=MODINV_MOVE;request.slotpos=0x100;request.targetslotpos=bow_lab.inventory_slot;request._has_bits[0]=7;
            bow_modify(bow_lab.wc,&request);
        }
    }
    if(now-bow_lab.last_sample>=.1){bow_lab.last_sample=now;
        struct Gun *g=(void*)bow_get_item(bow_lab.pc,0x100);if(bow_is_stonebow(g,bow_lab.pc))bow_push(bow_event(BOW_SAMPLE,g));
        if(bow_lab.second){struct Gun *stored=(void*)bow_get_item(bow_lab.pc,bow_lab.inventory_slot);if(bow_is_stonebow(stored,bow_lab.pc))bow_push(bow_event(BOW_SAMPLE,stored));}
    }
}

static int bow_rects(struct ProbeRect *panel,struct ProbeRect buttons[6],struct ProbeRect *tab){
    if(physical_w<400||physical_h<400)return 0;
    *panel=(struct ProbeRect){12,physical_h>600?220:20,352,354};*tab=(struct ProbeRect){12,panel->y,160,28};
    for(int i=0;i<6;++i)buttons[i]=(struct ProbeRect){panel->x+12+(i%2)*170,panel->y+126+(i/2)*58,158,26};return 1;
}
static unsigned bow_mouse_captured;static int bow_mouse_action=-1,bow_mouse_open;
static int bow_mouse(int x,int y,int button,int down,int ww,int hh){
    if(button<0||button>7)return 0;unsigned bit=1U<<button;int captured=!!(bow_mouse_captured&bit);
    if(!bow_lab.installed||!getbool("plugin_stonebow_lab","enabled",0)||paused||active_controls||ww<=0||hh<=0){
        if(!down&&captured){bow_mouse_captured&=~bit;bow_mouse_action=-1;return 1;}return 0;}
    struct ProbeRect panel,buttons[6],tab;if(!bow_rects(&panel,buttons,&tab))return 0;
    x=scaled_mouse(x,physical_w,ww);y=scaled_mouse(y,physical_h,hh);
    if(down){if(!probe_inside(bow_lab.visible?panel:tab,x,y))return 0;
        bow_mouse_captured|=bit;bow_mouse_open=bow_lab.visible;bow_mouse_action=-1;
        if(!button){if(!bow_lab.visible)bow_mouse_action=6;else for(int i=0;i<6;++i)if(probe_inside(buttons[i],x,y))bow_mouse_action=i;}return 1;}
    if(!captured)return 0;bow_mouse_captured&=~bit;int action=bow_mouse_action;bow_mouse_action=-1;
    if(button||bow_mouse_open!=bow_lab.visible)return 1;
    if(action==6&&probe_inside(tab,x,y))bow_lab.visible=1;
    else if(action>=0&&action<6&&probe_inside(buttons[action],x,y)){
        if(action==0){bow_trial_cancel("Baseline selected");if(bow_start())snprintf(bow_lab.result,sizeof(bow_lab.result),"Recording normal shots (2 minute maximum).");}
        else if(action==1)bow_stop("Stopped; saving log.");
        else if(action>=2&&action<=4)bow_arm(action-1);
        else {bow_trial_cancel("Panel closed; trial stopped.");bow_lab.visible=0;}
    }return 1;
}
static void bow_overlay(void){
    if(!bow_lab.installed||!getbool("plugin_stonebow_lab","enabled",0)||active_controls||paused)return;
    struct ProbeRect p,b[6],tab;if(!bow_rects(&p,b,&tab))return;
    int w=*canvas_w,h=*canvas_h;*canvas_w=physical_w;*canvas_h=physical_h;
    if(!bow_lab.visible){probe_gui_button(tab,"Stonebow test",0xff1c3944);}
    else{
        fill_rect(p.x,p.y,p.w,p.h,0xed172630,BLENDMODE_BLEND);fill_rect(p.x,p.y,p.w,2,0xff53d7c8,BLENDMODE_BLEND);
        probe_gui_text("Stonebow test",p.x+12,p.y+12,16,0xffffffff,0,0);char s[160];
        snprintf(s,sizeof(s),"%s | attempts %u | launches %u",bow_lab.recording?"RECORDING":"IDLE",bow_lab.attempts,bow_lab.launch_groups);probe_gui_text(s,p.x+12,p.y+40,12,0xffa4e8bd,0,0);
        snprintf(s,sizeof(s),"Last cooldown: %d ms | min sim gap: %.0f ms",bow_lab.last_cooldown,bow_lab.min_gap*1000);probe_gui_text(s,p.x+12,p.y+61,11,0xffc8d7e4,0,0);
        probe_gui_text("Use your normal main-attack key.",p.x+12,p.y+82,12,0xffffffff,0,0);
        snprintf(s,sizeof(s),"%.54s",bow_lab.result);probe_gui_text(s,p.x+12,p.y+103,10,0xffffd185,0,0);
        const char *labels[]={"Record baseline","Stop and save","Cycle two bows","Reset cooldown x3",bow_direct_enabled()?"Direct fire x3":"Direct fire: off","Close panel"};
        const char *help[]={"Record normal firing.","End trial and save log.","Needs 2 bows; 3 launches.","Next 3 attack presses.",bow_direct_enabled()?"Keeps native action checks.":"Disabled after crash.","Stops trial; log stays on."};
        for(int i=0;i<6;++i){probe_gui_button(b[i],labels[i],bow_lab.mode==i-1&&i>=2&&i<=4?0xff286a68:0xff30434e);probe_gui_text(help[i],b[i].x,b[i].y+31,10,0xffb5c8d8,0,0);}
        probe_gui_text("Trials expire after 20 seconds. No automatic attacks.",p.x+12,p.y+306,10,0xffc8d7e4,0,0);
        probe_gui_text("Local launches need a second-client damage check.",p.x+12,p.y+324,10,0xffc8d7e4,0,0);
        if(atomic_load(&bow_writer.dropped))probe_gui_text("Log incomplete: events dropped.",p.x+12,p.y+340,10,0xffff7070,0,0);
    }
    *canvas_w=w;*canvas_h=h;
}

#if !defined(SBPE_TEST) || defined(STONEBOW_HOOK_TEST)
#include <sys/mman.h>
static int network_patch_apply(uint8_t*,const uint8_t*,size_t,const uint8_t*,size_t);
static void network_guard_jump(uint8_t*,uintptr_t);
static const uint8_t bow_shoot_bytes[20]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xf8,1,0,0};
static const uint8_t bow_use_bytes[16]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0x83,0xbb,0xb8,0,0,0,0};
static const uint8_t bow_move_bytes[17]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x78};
static const uint8_t bow_create_bytes[14]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50};
static int bow_hook(uint8_t *site,const uint8_t *bytes,size_t n,void *replacement,void **original){
    if(memcmp(site,bytes,n))return 0;long page=sysconf(_SC_PAGESIZE);if(page<=0)return 0;
    uint8_t *tr=mmap(NULL,(size_t)page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(tr==MAP_FAILED)return 0;
    memcpy(tr,bytes,n);network_guard_jump(tr+n,(uintptr_t)site+n);
    if(mprotect(tr,(size_t)page,PROT_READ|PROT_EXEC)){munmap(tr,(size_t)page);return 0;}
    *original=tr;uint8_t code[14];network_guard_jump(code,(uintptr_t)replacement);
    if(network_patch_apply(site,bytes,n,code,sizeof(code)))return 1;
    *original=NULL;munmap(tr,(size_t)page);return 0;
}
static void install_stonebow_lab(void){
    if(!getbool("plugin_stonebow_lab","enabled",0))return;
    uint8_t *shoot=(void*)PTR(0x10003f680),*use=(void*)PTR(0x10003f620),*move=(void*)PTR(0x1000266b0),*create=(void*)PTR(0x100015910);
    if(memcmp(shoot,bow_shoot_bytes,sizeof(bow_shoot_bytes))||memcmp(use,bow_use_bytes,sizeof(bow_use_bytes))||
       memcmp(move,bow_move_bytes,sizeof(bow_move_bytes))||memcmp(create,bow_create_bytes,sizeof(bow_create_bytes))){LOG("Stonebow lab: native signatures differ; disabled.");return;}
    bow_get_item=(void*)PTR(0x100026d90);bow_modify=(void*)PTR(0x1000f56c0);bow_native_can_action=bow_game_can_action;
    int ok=bow_hook(shoot,bow_shoot_bytes,sizeof(bow_shoot_bytes),bow_shoot,(void**)&bow_original_shoot);
    if(ok)ok=bow_hook(use,bow_use_bytes,sizeof(bow_use_bytes),bow_use,(void**)&bow_original_use);
    if(ok)ok=bow_hook(move,bow_move_bytes,sizeof(bow_move_bytes),bow_move,(void**)&bow_original_move);
    if(ok)ok=bow_hook(create,bow_create_bytes,sizeof(bow_create_bytes),bow_create,(void**)&bow_original_create);
    if(!ok){LOG("Stonebow lab: hook installation incomplete; experiments disabled; installed wrappers only forward.");return;}
    bow_lab.installed=1;bow_lab.visible=getbool("plugin_stonebow_lab","gui_visible",1);atexit(bow_shutdown);
    snprintf(bow_lab.result,sizeof(bow_lab.result),"Equip Stonebow, then record a baseline.");
    LOG("Stonebow lab available, trials OFF; bounded launch recorder; Direct fire disabled by default; native per-shell action checks enforced.");
}
#else
static void install_stonebow_lab(void){}
#endif
#endif
