/* Multibox: the main client leads; SBPE instances started from MacSteam
 * Isolation profiles follow. Loopback UDP state and reconnect commands only.
 *  - Every instance publishes its character's health, body box and world.
 *    The leader relays follower states, so every panel lists every instance.
 *  - The leader sends which world actions are held, not raw keys. Each
 *    follower presses its own binding for the action (InputMapping::get()
 *    cik[action], three keycodes at +action*12), so bindings may differ.
 *  - Sync walks followers toward the leader until the boxes overlap again.
 * Health/reconnect rows extend Loading test, with multibox controls below. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define MB_SECTION "plugin_multibox"
#define MB_PEERS 8
#define MB_ACTIONS 14 /* CINPUT_WORLD_LEFT .. CINPUT_WORLD_DASHDOWN */
#define MB_BIT(a) (1u<<(a))
#define MB_WALK_MASK (MB_BIT(CINPUT_WORLD_LEFT)|MB_BIT(CINPUT_WORLD_RIGHT)|MB_BIT(CINPUT_WORLD_UP)|MB_BIT(CINPUT_WORLD_DOWN)|MB_BIT(CINPUT_WORLD_JUMP))
enum {MB_OFF,MB_LEADER,MB_FOLLOWER};
enum {MB_SYNC_NONE,MB_SYNC_DONE,MB_SYNC_TIMEOUT,MB_SYNC_OTHER_MAP,MB_SYNC_CANCELLED};
enum {MB_CAP_RECONNECT=1,MB_CAP_CONTROL=2,MB_CAP_ARM=4,MB_COMMAND_REQUEST=1,MB_COMMAND_ACK=2,MB_COMMAND_ARM=3};
struct MultiboxState {
    char magic[4];uint8_t version,leader,valid,syncing;
    uint32_t instance,held,sync_seq;uint8_t sync_on,sync_result,capabilities,control_off;
    int32_t hp,maxhp,x,y,w,h; /* body box in 1/256 px, y down */
    uint32_t game,zone,level;
    char server[48],name[32];
};
struct MultiboxCommand {char magic[4];uint8_t version,kind,accepted,reserved;uint32_t sender,target,sequence;};
_Static_assert(sizeof(struct MultiboxState)==140&&sizeof(struct MultiboxCommand)==20,"multibox datagram ABI");
struct MultiboxPeer {struct MultiboxState s;struct sockaddr_in addr;int has_addr;double seen;
    uint32_t request_seq;double requested,last_send;int request_pending,request_failed,request_kind,request_preset;};
struct MultiboxRect {int x,y,w,h;};
static struct {
    int inited,role,fd,have_leader,syncing,captured,enabled_override,pending_local;
    double next_send,next_bind,last_leader,sync_until,last_move,jump_until,next_jump;
    uint32_t instance,sync_seq,handled_seq,held,sent_held,mirror,injected,walk,command_seq,last_command;
    int sent_control_off,last_accepted;double local_reconnect_at;
    struct Stage *stage;
    int sync_on,injected_key[MB_ACTIONS];int32_t last_x;uint8_t sync_result;
    struct MultiboxState self,leader_state;
    struct MultiboxPeer peers[MB_PEERS];int npeers;
    struct MultiboxRect panel,button,toggle,connections,previous,next,reconnect_all,arm_all,reconnect[MB_PEERS+2];
    uint32_t targets[MB_PEERS+2],pressed_target;int page,nbuttons,pressed;
} mb={.fd=-1,.enabled_override=-1};
static struct InputMapping *(*mb_input_get)(void);
static double (*mb_clock)(void)=monotime;
static void (*mb_reconnect_action)(void)=probe_manual_reconnect;
static int mb_arm_local(int preset){
    if(paused||!loading_probe_installed||!getbool("plugin_loading_probe","enabled",0)||loading_probe.asset_finish||
       loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT||loading_probe.state==PROBE_RECONNECT||
       (preset<PROBE_ANY||preset>PROBE_STAGED))return 0;
    probe_preset=preset;set_option("plugin_loading_probe","mode","manual");int local=probe_group_local;probe_group_local=1;probe_arm();probe_group_local=local;
    return loading_probe.state==PROBE_ARMED&&loading_probe.preset==preset;
}
static int (*mb_arm_action)(int)=mb_arm_local;
static int mb_local_enabled(void){return mb.enabled_override>=0?mb.enabled_override:getbool(MB_SECTION,"enabled",1);}
static int mb_controls_enabled(void){return !paused&&mb_local_enabled()&&(mb.role!=MB_FOLLOWER||!mb.have_leader||!(mb.leader_state.capabilities&MB_CAP_CONTROL)||!mb.leader_state.control_off);}
static int mb_connection_height(void);
static int mb_below_height(void);

static int multibox_role(void){
    const char *r=getoptv(MB_SECTION,"role","auto");
    if(!strcasecmp(r,"leader"))return MB_LEADER;
    if(!strcasecmp(r,"follower"))return MB_FOLLOWER;
    if(!strcasecmp(r,"off"))return MB_OFF;
    const char *root=getenv("SBPE_ROOT");
    return root&&strstr(root,"/MacSteam Isolation/profiles/")?MB_FOLLOWER:MB_LEADER;
}
/* Profile folder name for followers ("steam-1"), "Main" for the leader. */
static void multibox_label(char *out,size_t n){
    const char *root=getenv("SBPE_ROOT"),*p=root?strstr(root,"/profiles/"):NULL;
    if(mb.role==MB_FOLLOWER&&p){p+=10;const char *end=strchr(p,'/');snprintf(out,n,"%.*s",(int)(end?end-p:(long)strlen(p)),p);}
    else snprintf(out,n,"%s",mb.role==MB_LEADER?"Main":"Follower");
}
static int mb_keyboard_key(int k){return (k>0&&k<0x20000000)||(k>=0x40000000&&k<0x40000400);}
static const int32_t *mb_action_keys(int action){
    struct InputMapping *m=mb_input_get?mb_input_get():NULL;
    return m?&m->cik[action].primary:NULL;
}
static double mb_overlap(const struct MultiboxState *a,const struct MultiboxState *b){
    double w=fmin((double)a->x+a->w,(double)b->x+b->w)-fmax(a->x,b->x),h=fmin((double)a->y+a->h,(double)b->y+b->h)-fmax(a->y,b->y);
    double smaller=fmin((double)a->w*a->h,(double)b->w*b->h);
    return w>0&&h>0&&smaller>0?w*h/smaller:0;
}
static int mb_same_world(const struct MultiboxState *a,const struct MultiboxState *b){
    return a->valid&&b->valid&&a->game==b->game&&a->zone==b->zone&&a->level==b->level&&!strcmp(a->server,b->server);
}
static double mb_threshold(void){return clamp(getnum(MB_SECTION,"overlap",0.5),0.01,1);}

/* Leader: which mirrored actions have a bound keyboard key held right now.
 * event_keys follows native key events and is cleared on focus loss. */
static uint32_t mb_leader_held(struct Stage *st){
    if(!mb_controls_enabled()||!st||st->textInputTarget||active_controls||!scancode_from_key||!getbool(MB_SECTION,"mirror",1))return 0;
    uint32_t held=0;
    for(int a=0;a<MB_ACTIONS;++a){const int32_t *keys=mb_action_keys(a);if(!keys)return 0;
        for(int i=0;i<3;++i)if(mb_keyboard_key(keys[i])){int sc=scancode_from_key(keys[i]);if(sc>0&&sc<(int)sizeof(event_keys)&&event_keys[sc])held|=MB_BIT(a);}}
    return held;
}
/* Follower: press or release this instance's own key for an action. The
 * native handler is called directly, so SBPE's own shortcuts never fire. */
static void mb_press(struct Stage *st,int action,int down){
    int key=0;
    if(down){const int32_t *keys=mb_action_keys(action);if(keys)for(int i=0;i<3&&!key;++i)if(mb_keyboard_key(keys[i]))key=keys[i];
        if(!key)return;mb.injected_key[action]=key;}
    else{key=mb.injected_key[action];mb.injected_key[action]=0;if(!key)return;}
    original_stage_keyboard(st,down,key);
}
static void mb_apply(struct Stage *st,uint32_t desired){
    if(!st)return;uint32_t change=desired^mb.injected;
    for(int a=0;a<MB_ACTIONS;++a)if(change&MB_BIT(a))mb_press(st,a,!!(desired&MB_BIT(a)));
    mb.injected=desired;
}

static void mb_end_sync(uint8_t result){
    if(!mb.syncing)return;mb.syncing=0;mb.walk=0;mb.sync_result=result;
    static const char *names[]={"","reached the main character","timed out","main is on another map","cancelled"};
    LOG("Multibox: sync %s.",names[result<5?result:0]);
}
static void mb_start_sync(double now){
    mb.syncing=1;mb.sync_result=MB_SYNC_NONE;mb.sync_until=now+clamp(getnum(MB_SECTION,"sync_timeout",20),1,300);
    mb.last_x=mb.self.x;mb.last_move=now;mb.jump_until=0;mb.next_jump=now;
    LOG("Multibox: sync requested; walking toward the main character.");
}
static void mb_jump(double now){mb.jump_until=now+0.35;mb.next_jump=now+0.8;}
/* Walk toward the leader's box: hold left/right, jump when the leader is
 * higher or progress stops, hold down when the leader is directly below. */
static void mb_walk(double now){
    mb.walk=0;if(!mb.syncing)return;
    if(now>mb.sync_until){mb_end_sync(MB_SYNC_TIMEOUT);return;}
    if(!mb.have_leader||!mb_same_world(&mb.self,&mb.leader_state)){mb_end_sync(MB_SYNC_OTHER_MAP);return;}
    if(mb_overlap(&mb.self,&mb.leader_state)>=mb_threshold()){mb_end_sync(MB_SYNC_DONE);return;}
    const struct MultiboxState *me=&mb.self,*lead=&mb.leader_state;
    double dx=(lead->x+lead->w/2.0)-(me->x+me->w/2.0),dy=(lead->y+lead->h/2.0)-(me->y+me->h/2.0),near=fmax(me->w,lead->w)*0.25;
    if(dx>near)mb.walk|=MB_BIT(CINPUT_WORLD_RIGHT);else if(dx< -near)mb.walk|=MB_BIT(CINPUT_WORLD_LEFT);
    if(mb.walk){if(abs(me->x-mb.last_x)>256){mb.last_x=me->x;mb.last_move=now;}else if(now-mb.last_move>0.6&&now>=mb.next_jump)mb_jump(now);}
    else{mb.last_x=me->x;mb.last_move=now;}
    if(dy< -me->h*0.75&&now>=mb.next_jump)mb_jump(now);
    if(dy>me->h*0.75&&fabs(dx)<=near*2)mb.walk|=MB_BIT(CINPUT_WORLD_DOWN);
    if(now<mb.jump_until)mb.walk|=MB_BIT(CINPUT_WORLD_JUMP);
}

static void mb_fill_self(struct ClientWorld *cw,const struct HudSnapshot *hud){
    struct MultiboxState *s=&mb.self;memcpy(s->magic,"SBMB",4);s->version=1;s->leader=mb.role==MB_LEADER;s->instance=mb.instance;
    s->capabilities=MB_CAP_RECONNECT|MB_CAP_CONTROL;
    if(loading_probe_installed&&getbool("plugin_loading_probe","enabled",0))s->capabilities|=MB_CAP_ARM;
    s->control_off=!mb_controls_enabled();
    s->valid=0;s->hp=s->maxhp=0;s->game=s->zone=s->level=0;s->server[0]=0;
    if(cw){s->game=cw->asWorld.props.gameid;s->zone=cw->asWorld.props.zoneid;s->level=(uint32_t)cw->asWorld.props.floor;
        char host[160]={0};if(selected_server[0])copystr(host,sizeof(host),selected_server);else if(server_hostname)read_string(server_hostname,host,sizeof(host));
        copystr(s->server,sizeof(s->server),host);}
    struct Properties p;
    if(cw&&cw->player&&hud&&hud->valid&&read_memory(&((struct WorldObject*)cw->player)->props,&p,sizeof(p))&&p.wmp>0&&p.hmp>0){
        s->valid=1;s->hp=hud->hp;s->maxhp=hud->maxhp;s->x=p.xmp;s->y=p.ymp;s->w=p.wmp;s->h=p.hmp;}
    struct PlayerIdentity id;
    if(cw&&cw->player&&player_identity((struct WorldObject*)cw->player,&id)&&id.name[0])copystr(s->name,sizeof(s->name),id.name);
    else multibox_label(s->name,sizeof(s->name));
}
static struct sockaddr_in mb_leader_address(void){
    return (struct sockaddr_in){.sin_len=sizeof(struct sockaddr_in),.sin_family=AF_INET,
        .sin_port=htons((uint16_t)clamp(getnum(MB_SECTION,"port",47810),1024,65535)),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
}
static int mb_open(void){
    int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return -1;
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);fcntl(fd,F_SETFD,FD_CLOEXEC);
    struct sockaddr_in a=mb_leader_address();if(mb.role!=MB_LEADER)a.sin_port=0;
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))){int saved=errno;close(fd);errno=saved;return -1;}
    return fd;
}
static void mb_send(const struct MultiboxState *s,const struct sockaddr_in *to){
    if(mb.fd>=0)sendto(mb.fd,s,sizeof(*s),0,(const struct sockaddr*)to,sizeof(*to));
}
static void mb_broadcast(void){
    if(!mb_controls_enabled()){mb.held=0;mb.sync_on=0;}
    mb.self.control_off=!mb_controls_enabled();
    if(mb.role==MB_LEADER){mb.self.held=mb.held;mb.self.sync_on=(uint8_t)mb.sync_on;mb.self.sync_seq=mb.sync_seq;
        for(int i=0;i<mb.npeers;++i)if(mb.peers[i].has_addr)mb_send(&mb.self,&mb.peers[i].addr);}
    else{mb.self.syncing=(uint8_t)mb.syncing;mb.self.sync_result=mb.sync_result;struct sockaddr_in to=mb_leader_address();mb_send(&mb.self,&to);}
    mb.sent_held=mb.held;mb.sent_control_off=mb.self.control_off;
}
static int mb_same_address(const struct sockaddr_in *a,const struct sockaddr_in *b){return a->sin_family==AF_INET&&b->sin_family==AF_INET&&a->sin_addr.s_addr==b->sin_addr.s_addr&&a->sin_port==b->sin_port;}
static struct MultiboxPeer *mb_peer(uint32_t instance){for(int i=0;i<mb.npeers;++i)if(mb.peers[i].s.instance==instance)return &mb.peers[i];return NULL;}
static void mb_send_command(const struct MultiboxCommand *command,const struct sockaddr_in *to){
    if(mb.fd>=0)sendto(mb.fd,command,sizeof(*command),0,(const struct sockaddr*)to,sizeof(*to));
}
static void mb_reconnect_request(uint32_t instance){
    double now=mb_clock();
    if(instance==mb.instance){if(!mb.pending_local&&(!mb.local_reconnect_at||now-mb.local_reconnect_at>=2)){mb.pending_local=1;mb.local_reconnect_at=now;}return;}
    struct MultiboxPeer *p=mb_peer(instance);
    if(mb.role!=MB_LEADER||!p||!p->has_addr||!(p->s.capabilities&MB_CAP_RECONNECT)||p->request_pending||
       (p->requested&&now-p->requested<2))return;
    if(!++mb.command_seq)++mb.command_seq;
    p->request_seq=mb.command_seq;p->requested=now;p->last_send=0;p->request_pending=1;p->request_failed=0;p->request_kind=MB_COMMAND_REQUEST;p->request_preset=0;
}
static void mb_arm_all(int preset){
    if(mb.role!=MB_LEADER||(preset<PROBE_ANY||preset>PROBE_STAGED))return;
    if(mb_arm_action)mb_arm_action(preset);
    for(int i=0;i<mb.npeers;++i){struct MultiboxPeer *p=&mb.peers[i];
        if(!p->has_addr||!(p->s.capabilities&MB_CAP_ARM)||p->request_pending||(p->requested&&mb_clock()-p->requested<2))continue;
        if(!++mb.command_seq)++mb.command_seq;
        p->request_seq=mb.command_seq;p->requested=mb_clock();p->last_send=0;p->request_pending=1;p->request_failed=0;
        p->request_kind=MB_COMMAND_ARM;p->request_preset=preset;
    }
    static const char *names[]={"Any","Rush","Boss","Timed"};
    LOG("Multibox: %s arm requested for Main and compatible connected clients; active holds are retained.",names[preset]);
}
static int mb_group_arm(int preset){
    if(mb.role!=MB_LEADER||!getbool(MB_SECTION,"apply_to_all",0))return 0;
    mb_arm_all(preset);return 1;
}
static void mb_retry_commands(double now){
    if(mb.role!=MB_LEADER)return;
    for(int i=0;i<mb.npeers;++i){struct MultiboxPeer *p=&mb.peers[i];if(!p->request_pending)continue;
        if(now-p->requested>=2){p->request_pending=0;p->request_failed=1;LOG("Multibox: %s request to %s was not acknowledged; no further retries.",p->request_kind==MB_COMMAND_ARM?"arm":"reconnect",p->s.name);continue;}
        if(!p->last_send||now-p->last_send>=.25){
            struct MultiboxCommand command={.magic="SBMC",.version=1,.kind=p->request_kind?p->request_kind:MB_COMMAND_REQUEST,.reserved=p->request_preset,.sender=mb.instance,.target=p->s.instance,.sequence=p->request_seq};
            mb_send_command(&command,&p->addr);p->last_send=now;
        }
    }
}
static void mb_receive_command(const struct MultiboxCommand *c,const struct sockaddr_in *from){
    if(memcmp(c->magic,"SBMC",4)||c->version!=1||!c->sequence||c->target!=mb.instance)return;
    if(c->kind==MB_COMMAND_ACK&&mb.role==MB_LEADER){
        struct MultiboxPeer *p=mb_peer(c->sender);
        if(p&&p->has_addr&&mb_same_address(from,&p->addr)&&p->request_pending&&p->request_seq==c->sequence){
            p->request_pending=0;p->request_failed=!c->accepted;
            LOG("Multibox: %s %s the %s request.",p->s.name,c->accepted?"received":"rejected",p->request_kind==MB_COMMAND_ARM?"arm":"reconnect");
        }return;
    }
    struct sockaddr_in leader=mb_leader_address();
    if((c->kind!=MB_COMMAND_REQUEST&&c->kind!=MB_COMMAND_ARM)||mb.role!=MB_FOLLOWER||!mb.have_leader||c->sender!=mb.leader_state.instance||!mb_same_address(from,&leader))return;
    if(!mb.last_command||(int32_t)(c->sequence-mb.last_command)>0){
        mb.last_command=c->sequence;mb.last_accepted=1;
        if(c->kind==MB_COMMAND_ARM)mb.last_accepted=mb_arm_action?mb_arm_action(c->reserved):0;
        else{mb.pending_local=1;mb.local_reconnect_at=mb_clock();}
    }
    struct MultiboxCommand ack={.magic="SBMC",.version=1,.kind=MB_COMMAND_ACK,.accepted=c->sequence==mb.last_command?mb.last_accepted:0,.sender=mb.instance,.target=c->sender,.sequence=c->sequence};
    mb_send_command(&ack,from); /* duplicate requests are acknowledged, never re-executed */
}
static struct MultiboxPeer *mb_store_peer(const struct MultiboxState *s,const struct sockaddr_in *from,double now){
    struct MultiboxPeer *p=NULL;
    for(int i=0;i<mb.npeers;++i)if(mb.peers[i].s.instance==s->instance)p=&mb.peers[i];
    if(!p){if(mb.npeers>=MB_PEERS)return NULL;p=&mb.peers[mb.npeers++];memset(p,0,sizeof(*p));
        LOG("Multibox: %s connected.",s->name[0]?s->name:"an instance");}
    p->s=*s;p->seen=now;if(from){p->addr=*from;p->has_addr=1;}
    return p;
}
static void mb_expire(double now){
    for(int i=0;i<mb.npeers;)if(now-mb.peers[i].seen>3){LOG("Multibox: %s disconnected.",mb.peers[i].s.name);mb.peers[i]=mb.peers[--mb.npeers];}else ++i;
    if(mb.have_leader&&now-mb.last_leader>3){mb.have_leader=0;mb.mirror=0;LOG("Multibox: main client lost.");}
}
static void mb_receive(double now){
    for(int count=0;count<256;++count){
        uint8_t bytes[sizeof(struct MultiboxState)+1];struct sockaddr_in from={0};socklen_t length=sizeof(from);
        ssize_t n=recvfrom(mb.fd,bytes,sizeof(bytes),0,(struct sockaddr*)&from,&length);
        if(n<0)break;
        if(length<sizeof(from)||from.sin_family!=AF_INET||from.sin_addr.s_addr!=htonl(INADDR_LOOPBACK))continue;
        if(n==(ssize_t)sizeof(struct MultiboxCommand)){struct MultiboxCommand command;memcpy(&command,bytes,sizeof(command));mb_receive_command(&command,&from);continue;}
        struct MultiboxState in;if(n!=(ssize_t)sizeof(in))continue;memcpy(&in,bytes,sizeof(in));
        if(memcmp(in.magic,"SBMB",4)||in.version!=1||in.instance==mb.instance)continue;
        struct sockaddr_in leader=mb_leader_address();if(mb.role==MB_FOLLOWER&&!mb_same_address(&from,&leader))continue;
        in.name[sizeof(in.name)-1]=0;in.server[sizeof(in.server)-1]=0;
        if(mb.role==MB_FOLLOWER&&in.leader){
            if(!mb.have_leader||in.instance!=mb.leader_state.instance)mb.last_command=0;
            /* A follower joining mid-sync must not start walking on its own. */
            if(!mb.have_leader){mb.handled_seq=in.sync_seq;LOG("Multibox: following %s.",in.name);}
            mb.leader_state=in;mb.have_leader=1;mb.last_leader=now;mb.mirror=in.held;
            if(!mb_controls_enabled()||!in.sync_on){mb.handled_seq=in.sync_seq;mb_end_sync(MB_SYNC_CANCELLED);}
            else if(in.sync_seq!=mb.handled_seq){mb.handled_seq=in.sync_seq;mb_start_sync(now);}
            continue;
        }
        int relay=mb.role==MB_LEADER&&!in.leader;
        if(!mb_store_peer(&in,relay?&from:NULL,now))continue;
        if(relay)for(int i=0;i<mb.npeers;++i)if(mb.peers[i].has_addr&&mb.peers[i].s.instance!=in.instance)mb_send(&in,&mb.peers[i].addr);
    }
}
static int mb_any_syncing(void){for(int i=0;i<mb.npeers;++i)if(mb.peers[i].s.syncing)return 1;return 0;}
static void mb_toggle_sync(void){
    if(mb.role!=MB_LEADER||!mb_controls_enabled())return;
    if(mb_any_syncing()){mb.sync_on=0;LOG("Multibox: sync cancelled.");}
    else{mb.sync_on=1;++mb.sync_seq;LOG("Multibox: sync requested for %d follower(s).",mb.npeers);}
    mb_broadcast();
}
/* Save just the on/off preference without replacing controls or comments. */
static int mb_save_enabled(int enabled){
    struct stat st;if(!config_path[0]||stat(config_path,&st)||st.st_size>2*1024*1024)return 0;
    FILE *in=fopen(config_path,"r");if(!in)return 0;
    char path[sizeof(config_path)+32];snprintf(path,sizeof(path),"%s.multibox.XXXXXX",config_path);
    int fd=mkstemp(path);if(fd<0){fclose(in);return 0;}FILE *out=fdopen(fd,"w");
    if(!out){close(fd);unlink(path);fclose(in);return 0;}
    int ok=!fchmod(fd,st.st_mode&0777),inside=0,written=0,last_newline=1;char *line=NULL;size_t capacity=0;ssize_t length;
    while(ok&&(length=getline(&line,&capacity,in))>=0){
        char *copy=strdup(line);if(!copy){ok=0;break;}char *s=trim(copy);int skip=0;
        if(*s=='['){char *e=strchr(s,']');if(e)*e=0;inside=e&&!strcmp(s+1,MB_SECTION);
            if(inside&&!written){ok=fwrite(line,1,(size_t)length,out)==(size_t)length;if(ok&&line[length-1]!='\n')ok=fputc('\n',out)!=EOF;
                if(ok)ok=fprintf(out,"enabled = %s\n",enabled?"yes":"no")>0;written=1;skip=1;last_newline=1;}
        }else if(inside&&*s!='#'&&*s!=';'){char *e=strchr(s,'=');if(e){*e=0;skip=!strcmp(trim(s),"enabled");}}
        if(!skip){ok=fwrite(line,1,(size_t)length,out)==(size_t)length;last_newline=length&&line[length-1]=='\n';}free(copy);
    }
    if(ferror(in))ok=0;free(line);fclose(in);
    if(ok&&!written){if(!last_newline)ok=fputc('\n',out)!=EOF;if(ok)ok=fprintf(out,"\n[%s]\nenabled = %s\n",MB_SECTION,enabled?"yes":"no")>0;}
    if(fflush(out)||fsync(fd))ok=0;if(fclose(out))ok=0;
    if(ok&&!rename(path,config_path))return 1;unlink(path);return 0;
}
static void mb_set_enabled(int enabled){
    enabled=!!enabled;set_option(MB_SECTION,"enabled",enabled?"yes":"no");
    mb.enabled_override=enabled;
    if(mb_save_enabled(enabled))mb.enabled_override=-1;
    else LOG("Multibox: preference could not be saved; keeping this session %s.",enabled?"on":"off");
    mb.held=mb.mirror=mb.walk=0;mb.sync_on=0;mb_end_sync(MB_SYNC_CANCELLED);
    if(!enabled)mb_apply(mb.stage,0);
    if(mb.fd>=0)mb_broadcast();
    LOG("Multibox: controls %s; health reporting and reconnect controls remain available.",enabled?"enabled":"disabled");
}
/* Protect network updates in any background client, including Main. This
 * requests scheduler activity; it does not synthesize focus or game input. */
static void mb_prevent_app_nap(void){
#ifndef SBPE_TEST
    void *(*get_class)(const char*)=dlsym(RTLD_DEFAULT,"objc_getClass");void *(*selector)(const char*)=dlsym(RTLD_DEFAULT,"sel_registerName");
    void *send=dlsym(RTLD_DEFAULT,"objc_msgSend");if(!get_class||!selector||!send)return;
    void *info=((void*(*)(void*,void*))send)(get_class("NSProcessInfo"),selector("processInfo"));
    void *reason=((void*(*)(void*,void*,const char*))send)(get_class("NSString"),selector("stringWithUTF8String:"),"SBPE network updates");
    if(!info||!reason)return;
    /* NSActivityUserInitiated | NSActivityLatencyCritical */
    void *token=((void*(*)(void*,void*,uint64_t,void*))send)(info,selector("beginActivityWithOptions:reason:"),0x00FFFFFFULL|0xFF00000000ULL,reason);
    if(token){((void*(*)(void*,void*))send)(token,selector("retain"));LOG("Background activity: App Nap protection active for this client.");}
#endif
}

/* Leader: called for every native key event before it reaches the game. */
static void multibox_key(struct Stage *st){
    if(mb.role!=MB_LEADER||mb.fd<0)return;
    mb.held=mb_leader_held(st);if(mb.held!=mb.sent_held)mb_broadcast();
}
static void multibox_frame(struct Stage *st,struct ClientWorld *cw,const struct HudSnapshot *hud){
    double now=mb_clock();mb.stage=st;probe_group_arm=mb_group_arm;probe_clients_height=mb_connection_height;probe_below_height=mb_below_height;
    if(!mb.inited){mb.inited=1;mb.role=multibox_role();mb.instance=arc4random()|1;
#ifdef SBPE_TEST
        mb.role=MB_OFF; /* Other offline fixtures must not discover live clients. */
#endif
#ifndef SBPE_TEST
        if(!mb_input_get)mb_input_get=(void*)PTR(0x1000832e0); /* InputMapping::get() */
#endif
        if(getbool("native","prevent_app_nap",1))mb_prevent_app_nap();
        if(mb.role)LOG("Multibox: %s.",mb.role==MB_LEADER?"main client (leader)":"profile instance (follower)");}
    if(!mb.role)return;
    if(mb.fd<0){if(now<mb.next_bind)return;mb.next_bind=now+2;mb.fd=mb_open();
        if(mb.fd<0){static int reported;if(!reported){LOG("Multibox: could not open UDP port %d (errno %d); retrying.",(int)getnum(MB_SECTION,"port",47810),errno);reported=1;}return;}}
    mb_receive(now);mb_expire(now);mb_fill_self(cw,hud);mb_retry_commands(now);
    int reconnecting=mb.pending_local;
    if(reconnecting){mb.pending_local=0;mb_apply(st,0);mb_end_sync(MB_SYNC_CANCELLED);if(mb_reconnect_action)mb_reconnect_action();}
    if(mb.role==MB_LEADER){mb.held=mb_leader_held(st);if(mb.held!=mb.sent_held||mb.self.control_off!=mb.sent_control_off)mb.next_send=0;}
    else{
        if(!mb_controls_enabled())mb_end_sync(MB_SYNC_CANCELLED);
        mb_walk(now);
        uint32_t desired=mb.have_leader&&mb_controls_enabled()&&!reconnecting?mb.mirror:0;
        if(st&&(st->textInputTarget||active_controls))desired=0; /* typing in this window */
        if(mb.syncing)desired=(desired&~MB_WALK_MASK)|mb.walk;
        mb_apply(st,desired);
    }
    if(now>=mb.next_send){mb.next_send=now+0.1;mb_broadcast();}
}

/* Client health/reconnect rows extend Loading test; controls sit below it.
 * Use its window-coordinate layout and Retina-aware text rasterization. */
static int mb_inside(struct MultiboxRect r,int x,int y){return x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h;}
static int mb_ui_visible(void){
    /* In a very short window its Close
       panel button remains usable; the compact tab then makes room for us. */
    int crowded=loading_probe_installed&&getbool("plugin_loading_probe","enabled",0)&&probe_gui_visible&&physical_w>=280&&physical_h>=418&&physical_h<520;
    return mb.role&&!paused&&!active_controls&&!crowded&&getbool(MB_SECTION,"panel",1)&&physical_w>=260&&physical_h>=120;
}
static int mb_loading_panel(void){return loading_probe_installed&&getbool("plugin_loading_probe","enabled",0)&&physical_w>=280&&physical_h>=418;}
static int mb_list_rows(const struct MultiboxState **rows){
    int n=0;
    if(mb.role==MB_FOLLOWER&&mb.have_leader)rows[n++]=&mb.leader_state;
    rows[n++]=&mb.self;
    for(int i=0;i<mb.npeers&&n<MB_PEERS+2;++i)if(mb.peers[i].s.instance!=mb.instance&&
        !(mb.role==MB_FOLLOWER&&mb.have_leader&&mb.peers[i].s.instance==mb.leader_state.instance))rows[n++]=&mb.peers[i].s;
    return n;
}
static int mb_visible_rows(void){
    const struct MultiboxState *rows[MB_PEERS+2];int total=mb_list_rows(rows);
    int base=mb_loading_panel()?(probe_gui_visible?probe_gui_base_height():28):0;
    int available=(physical_h-20-base-90-26)/38;
    return available<0?0:available<total?available:total;
}
static int mb_connection_height(void){
    if(!mb_ui_visible())return 0;int n=mb_visible_rows();
    return n?26+n*38:16;
}
static int mb_below_height(void){
    if(!mb_ui_visible())return 0;
    return 90+(mb_loading_panel()&&!probe_gui_visible?mb_connection_height():0);
}
static int multibox_rects(int unused){
    (void)unused;if(!mb_ui_visible())return 0;
    int height=mb_connection_height(),x=physical_w-380,y=12,w=368;
    if(x<10)x=10;if(x+w>physical_w-10)w=physical_w-x-10;
    if(mb_loading_panel()){
        struct ProbeRect p,b[PROBE_BUTTONS],tab;if(!probe_gui_rects(&p,b,&tab))return 0;
        x=p.x;w=p.w;y=probe_gui_visible?p.y+probe_gui_base_height():tab.y+tab.h;
    }
    mb.connections=(struct MultiboxRect){x,y,w,height};
    mb.panel=(struct MultiboxRect){x,y+height+10,w,80};
    int toggle_w=w<300?68:100,sync_w=w<300?66:84;
    mb.toggle=(struct MultiboxRect){x+w-toggle_w-10,mb.panel.y+10,toggle_w,26};
    mb.button=(struct MultiboxRect){mb.toggle.x-sync_w-8,mb.toggle.y,sync_w,26};
    mb.arm_all=(struct MultiboxRect){x+10,mb.panel.y+45,w-20,26};
    const struct MultiboxState *rows[MB_PEERS+2];int total=mb_list_rows(rows),visible=mb_visible_rows();
    int pages=visible?(total+visible-1)/visible:1;
    if(mb.page>=pages)mb.page=pages-1;if(mb.page<0)mb.page=0;
    mb.reconnect_all=(struct MultiboxRect){x+w-184,y+4,112,18};
    mb.previous=(struct MultiboxRect){x+w-62,y+4,24,18};mb.next=(struct MultiboxRect){x+w-34,y+4,24,18};
    mb.nbuttons=0;
    for(int i=mb.page*visible;i<total&&mb.nbuttons<visible;++i){int j=mb.nbuttons++;
        mb.reconnect[j]=(struct MultiboxRect){x+w-94,y+26+j*38+16,84,18};mb.targets[j]=rows[i]->instance;}
    return 38;
}
static int mb_can_reconnect(uint32_t instance){
    if(instance==mb.instance)return !mb.pending_local&&(!mb.local_reconnect_at||mb_clock()-mb.local_reconnect_at>=2);
    struct MultiboxPeer *p=mb_peer(instance);
    return mb.role==MB_LEADER&&mb.fd>=0&&p&&p->has_addr&&(p->s.capabilities&MB_CAP_RECONNECT)&&
        !p->request_pending&&(!p->requested||mb_clock()-p->requested>=2);
}
/* All registered capable clients, including Main, regardless of the visible
 * health page or whether mirrored controls are On. Existing per-client guards
 * suppress repeats and skip older peers until they load command support. */
static int mb_can_reconnect_all(void){
    if(mb.role!=MB_LEADER)return 0;
    if(mb_can_reconnect(mb.instance))return 1;
    for(int i=0;i<mb.npeers;++i)if(mb_can_reconnect(mb.peers[i].s.instance))return 1;
    return 0;
}
static void mb_reconnect_all(void){
    if(mb.role!=MB_LEADER)return;int count=0;
    for(int i=0;i<mb.npeers;++i){uint32_t instance=mb.peers[i].s.instance;
        if(mb_can_reconnect(instance)){mb_reconnect_request(instance);++count;}}
    if(mb_can_reconnect(mb.instance)){mb_reconnect_request(mb.instance);++count;}
    if(count)LOG("Multibox: reconnect requested for %d client(s), including Main when available.",count);
}
static void mb_gui_button(struct MultiboxRect r,const char *label,uint32_t color){
    probe_gui_button((struct ProbeRect){r.x,r.y,r.w,r.h},label,color);
}
static int multibox_mouse(int x,int y,int button,int down,int window_w,int window_h){
    if(button<0||button>=8)return 0;unsigned bit=1u<<button;int captured=!!(mb.captured&bit);
    if(connectivity_ui_hidden||window_w<=0||window_h<=0||!multibox_rects(0)){
        if(!down&&captured){mb.captured&=~bit;mb.pressed=0;return 1;}return 0;}
    x=scaled_mouse(x,physical_w,window_w);y=scaled_mouse(y,physical_h,window_h);
    if(down){
        if(!mb_inside(mb.panel,x,y)&&!mb_inside(mb.connections,x,y))return 0;
        mb.captured|=bit;mb.pressed=0;mb.pressed_target=0;
        if(!button){
            if(mb_inside(mb.toggle,x,y))mb.pressed=1;
            else if(mb_inside(mb.button,x,y)&&mb.role==MB_LEADER&&mb_controls_enabled())mb.pressed=2;
            else if(mb.nbuttons&&mb_inside(mb.reconnect_all,x,y)&&mb_can_reconnect_all())mb.pressed=6;
            else if(mb.role==MB_LEADER&&mb_inside(mb.arm_all,x,y))mb.pressed=7;
            else if(mb_inside(mb.previous,x,y))mb.pressed=3;
            else if(mb_inside(mb.next,x,y))mb.pressed=4;
            else for(int i=0;i<mb.nbuttons;++i)if(mb_inside(mb.reconnect[i],x,y)&&mb_can_reconnect(mb.targets[i])){mb.pressed=5;mb.pressed_target=mb.targets[i];break;}
        }return 1;
    }
    if(!captured)return 0;mb.captured&=~bit;int action=mb.pressed;mb.pressed=0;if(button)return 1;
    if(action==1&&mb_inside(mb.toggle,x,y))mb_set_enabled(!mb_local_enabled());
    else if(action==2&&mb_inside(mb.button,x,y))mb_toggle_sync();
    else if(action==3&&mb_inside(mb.previous,x,y)&&mb.page>0)--mb.page;
    else if(action==4&&mb_inside(mb.next,x,y))++mb.page;
    else if(action==6&&mb.nbuttons&&mb_inside(mb.reconnect_all,x,y))mb_reconnect_all();
    else if(action==7&&mb_inside(mb.arm_all,x,y))set_option(MB_SECTION,"apply_to_all",getbool(MB_SECTION,"apply_to_all",0)?"no":"yes");
    else if(action==5)for(int i=0;i<mb.nbuttons;++i)if(mb.targets[i]==mb.pressed_target&&mb_inside(mb.reconnect[i],x,y)){mb_reconnect_request(mb.pressed_target);break;}
    return 1;
}
static void mb_row(const struct MultiboxState *s,int y,struct MultiboxRect reconnect){
    const struct MultiboxState *lead=mb.role==MB_LEADER?&mb.self:(mb.have_leader?&mb.leader_state:NULL);
    uint32_t dot=0xff6b7781;char status[48]="no character",text[80],name[48];
    if(s->leader){dot=0xff4fc3f7;copystr(status,sizeof(status),"main");}
    else if(s->syncing){dot=0xff4fc3f7;copystr(status,sizeof(status),"syncing");}
    else if(s->valid&&lead&&lead->valid){
        if(!mb_same_world(s,lead)){dot=0xffffb74d;copystr(status,sizeof(status),"other map");}
        else{double overlap=mb_overlap(s,lead);int synced=overlap>=mb_threshold();dot=synced?0xff66bb6a:0xffef5350;
            snprintf(status,sizeof(status),synced?"synced %.0f%%":"apart %.0f%%",overlap*100);}}
    if((s->capabilities&MB_CAP_CONTROL)&&s->control_off)copystr(status,sizeof(status),s->leader?"main / off":"controls off");
    int x=mb.connections.x+10,w=mb.connections.w-112;
    fill_rect(x,y+4,7,7,dot,BLENDMODE_BLEND);
    snprintf(name,sizeof(name),"%.18s%s",s->name[0]?s->name:"Client",s->instance==mb.instance?" (this)":"");
    probe_gui_text(name,x+13,y,13,0xffffffff,0,0);
    probe_gui_text(status,mb.connections.x+mb.connections.w-10,y,12,dot,1,0);
    int bar_y=y+20,bar_h=10;fill_rect(x,bar_y,w,bar_h,0xff2a333b,BLENDMODE_BLEND);
    if(s->valid&&s->maxhp>0){double f=clamp((double)s->hp/s->maxhp,0,1);
        fill_rect(x,bar_y,(int)lround(w*f),bar_h,f>.5?0xff66bb6a:f>.25?0xffffb74d:0xffef5350,BLENDMODE_BLEND);
        snprintf(text,sizeof(text),"%d/%d",s->hp,s->maxhp);
    }else copystr(text,sizeof(text),"No character");
    probe_gui_text(text,x+w/2,bar_y+bar_h/2,11,0xffffffff,.5,.5);
    struct MultiboxPeer *p=mb_peer(s->instance);const char *label="Reconnect";
    if(s->instance==mb.instance&&mb.local_reconnect_at&&mb_clock()-mb.local_reconnect_at<2)label="Requested";
    else if(p&&p->request_pending)label="Sending...";
    else if(p&&p->request_failed)label="Retry";
    else if(p&&p->requested&&mb_clock()-p->requested<2)label="Requested";
    else if(s->instance!=mb.instance&&mb.role==MB_LEADER&&!(s->capabilities&MB_CAP_RECONNECT))label="Update client";
    else if(s->instance!=mb.instance&&mb.role!=MB_LEADER)label="On main";
    mb_gui_button(reconnect,label,mb_can_reconnect(s->instance)?0xff315b86:0xff303a40);
}
static void multibox_connections_overlay(void){
    if(connectivity_ui_hidden)return;
    if(!multibox_rects(0))return;
    int old_w=*canvas_w,old_h=*canvas_h;*canvas_w=physical_w;*canvas_h=physical_h;
    struct MultiboxRect p=mb.connections;
    fill_rect(p.x,p.y,p.w,p.h,0xf0182430,BLENDMODE_BLEND);
    fill_rect(p.x+10,p.y,p.w-20,1,0xff374755,BLENDMODE_BLEND);
    if(!mb.nbuttons)probe_gui_text("Close Loading panel to show clients",p.x+10,p.y+2,11,0xffc8d7e4,0,0);
    else{
        const struct MultiboxState *rows[MB_PEERS+2];int total=mb_list_rows(rows),visible=mb_visible_rows();char heading[64];
        if(total>visible&&p.w>=300)snprintf(heading,sizeof(heading),"Clients %d-%d/%d",mb.page*visible+1,mb.page*visible+mb.nbuttons,total);
        else copystr(heading,sizeof(heading),"Clients");
        probe_gui_text(heading,p.x+10,p.y+5,13,0xffc8d7e4,0,0);
        mb_gui_button(mb.reconnect_all,"Reconnect all",mb_can_reconnect_all()?0xff315b86:0xff303a40);
        if(total>visible){mb_gui_button(mb.previous,"<",mb.page?0xff374755:0xff303a40);mb_gui_button(mb.next,">",mb.page*visible+visible<total?0xff374755:0xff303a40);}
        for(int i=0;i<mb.nbuttons;++i)mb_row(rows[mb.page*visible+i],p.y+26+i*38,mb.reconnect[i]);
    }
    *canvas_w=old_w;*canvas_h=old_h;
}
static void multibox_overlay(void){
    if(connectivity_ui_hidden)return;
    if(!multibox_rects(0))return;
    int old_w=*canvas_w,old_h=*canvas_h;*canvas_w=physical_w;*canvas_h=physical_h;
    struct MultiboxRect p=mb.panel;fill_rect(p.x,p.y,p.w,p.h,0xf0182430,BLENDMODE_BLEND);
    fill_rect(p.x,p.y,p.w,2,0xff4fc3f7,BLENDMODE_BLEND);
    probe_gui_text("Multibox",p.x+10,p.y+15,15,0xffffffff,0,0);
    int enabled=mb_local_enabled(),global=mb_controls_enabled();
    mb_gui_button(mb.toggle,p.w<300?(enabled?"ON":"OFF"):(enabled?"ON / turn off":"OFF / turn on"),enabled?0xff215c53:0xff49414e);
    mb_gui_button(mb.button,mb.role!=MB_LEADER?(global?"Following":"Paused"):mb_any_syncing()?"Stop sync":"Sync",global&&mb.role==MB_LEADER?0xff215c53:0xff303a40);
    mb_gui_button(mb.arm_all,getbool(MB_SECTION,"apply_to_all",0)?"[x] Arm all clients":"[ ] Arm all clients",mb.role==MB_LEADER?0xff215c53:0xff303a40);
    *canvas_w=old_w;*canvas_h=old_h;
}
