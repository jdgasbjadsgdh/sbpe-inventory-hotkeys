/* Opt-in manual experiment, separate from ordinary Reconnect. Reuse the
 * verified deferred recovery path; never change HP, position or clocks. */
#ifndef SBPE_RECONNECT_LAB_H
#define SBPE_RECONNECT_LAB_H
#define DC_LAB_SECTION "plugin_reconnect_lab"
enum {DC_IDLE,DC_QUEUED,DC_WAIT,DC_HELD,DC_DONE,DC_CANCELLED};
struct DCLabSample {
    struct WorldClient *wc;struct ClientWorld *cw;struct ServerConnection *connection;
    struct Player *player;int socket,hp,maxhp;uint32_t game,world;
};
static struct {
    int state,visible,initialized,pressed,captured,was_open,hold_logged;
    unsigned trial;double requested,issued,last_trial,spawned_at,last_hp_sample;
    struct DCLabSample before,after;char result[160];
    uint8_t owned[512];
    double shortcut_refresh;char shortcut[64];
} dc_lab;
static int dc_lab_enabled(void){return getbool(DC_LAB_SECTION,"enabled",0);}
static int dc_lab_read(struct DCLabSample *s){
    struct WorldClient *wc=probe_world();struct Properties p;
    if(!wc||!wc->clientWorld||!wc->serverConnection||wc->serverConnection->state!=CONNECTED||wc->serverConnection->socket<0)return 0;
    struct ClientWorld *cw=wc->clientWorld;
    if(!cw->player||!read_memory(&((struct WorldObject*)cw->player)->props,&p,sizeof(p))||p.hitpoints<=0||p.maxhitpoints<=0)return 0;
    *s=(struct DCLabSample){wc,cw,wc->serverConnection,cw->player,wc->serverConnection->socket,p.hitpoints,p.maxhitpoints,cw->asWorld.props.gameid,cw->asWorld.id};return 1;
}
static int dc_lab_same(const struct DCLabSample *a,const struct DCLabSample *b){
    return a->wc==b->wc&&a->cw==b->cw&&a->connection==b->connection&&a->socket==b->socket&&a->player==b->player&&a->game==b->game&&a->world==b->world;
}
static void dc_lab_disarm_own(void){
    if(loading_probe.state==PROBE_ARMED&&loading_probe.preset==PROBE_RUSH)loading_probe.state=PROBE_IDLE;
}
static int dc_lab_confirm(void *connection);
static void dc_lab_cancel_queue(const char *why){
    if(dc_lab.state!=DC_QUEUED)return;
    if(probe_reconnect_cancel)probe_reconnect_cancel(dc_lab.before.connection,dc_lab_confirm);
    dc_lab.state=DC_CANCELLED;dc_lab_disarm_own();
    snprintf(dc_lab.result,sizeof(dc_lab.result),"%s",why);
    LOG("Loading test: disconnect lab trial=%u cancelled before close; reason=%s.",dc_lab.trial,why);
}
static int dc_lab_confirm(void *connection){
    struct DCLabSample current;
    if(dc_lab.state!=DC_QUEUED)return 0;
    if(!dc_lab_enabled()||paused||connection!=dc_lab.before.connection||!dc_lab_read(&current)||!dc_lab_same(&current,&dc_lab.before)||
       loading_probe.state!=PROBE_ARMED||loading_probe.preset!=PROBE_RUSH){
        dc_lab_cancel_queue("Player, room or connection changed; no close issued.");return 0;
    }
    dc_lab.state=DC_WAIT;dc_lab.issued=probe_clock();
    snprintf(dc_lab.result,sizeof(dc_lab.result),"Disconnect issued; waiting for held recovery.");
    LOG("Loading test: disconnect lab trial=%u manual close confirmed after native I/O; request_to_close_ms=%.3f hp_before=%d hp_at_close=%d game=%u world=%u. Rush arm retained; server disappearance and protection remain unverified.",
        dc_lab.trial,(dc_lab.issued-dc_lab.requested)*1000,dc_lab.before.hp,current.hp,current.game,current.world);
    return 1;
}
static int dc_lab_start(void){
    double now=probe_clock();struct DCLabSample current;
    if(!dc_lab_enabled()||paused||!loading_probe_installed||!getbool("plugin_loading_probe","enabled",0)||!probe_reconnect_request||!probe_reconnect_cancel||!dc_lab_read(&current)||
       loading_probe.asset_finish||loading_probe.state==PROBE_ARMED||loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT||loading_probe.state==PROBE_RECONNECT||
       dc_lab.state==DC_QUEUED||dc_lab.state==DC_WAIT||dc_lab.state==DC_HELD||
       (dc_lab.trial&&now-dc_lab.last_trial<10)){
        snprintf(dc_lab.result,sizeof(dc_lab.result),"Need a living character, no active hold. Wait 10s between uses.");return 0;
    }
    int old_local=probe_group_local;probe_group_local=1;probe_preset=PROBE_RUSH;
    set_option("plugin_loading_probe","mode","manual");probe_arm();probe_group_local=old_local;
    if(loading_probe.state!=PROBE_ARMED){snprintf(dc_lab.result,sizeof(dc_lab.result),"Could not arm or open test log; no disconnect.");return 0;}
    dc_lab.before=current;dc_lab.state=DC_QUEUED;dc_lab.requested=dc_lab.last_trial=now;dc_lab.issued=dc_lab.spawned_at=0;dc_lab.hold_logged=0;++dc_lab.trial;
    LOG("Loading test: disconnect lab trial=%u requested manually; hp_before=%d maxhp=%d game=%u world=%u. One client only, no health trigger; no death rollback.",dc_lab.trial,current.hp,current.maxhp,current.game,current.world);
    if(!probe_reconnect_request(current.connection,dc_lab_confirm)){
        dc_lab_cancel_queue("Recovery queue busy; no disconnect issued.");return 0;
    }
    snprintf(dc_lab.result,sizeof(dc_lab.result),"Queued one disconnect; Rush stays armed.");return 1;
}
static int dc_lab_key_available(int sc){
    if(!key_from_name||sc<=0)return 0;
    if(controls_disconnect_reserved(sc))return 0;
    for(int a=0;a<CONTROL_COUNT;++a)if(a!=CONTROL_DISCONNECT)for(int c=0;c<3;++c)
        if(sc==key_from_name(control_keys[a][c]))return 0;
    for(int i=0;i<nbindings;++i)if(bindings[i].scancode==sc)return 0;
    return 1;
}
static int dc_lab_key_matches(int sc){
    if(!key_from_name||sc<=0)return 0;
    for(int c=0;c<3;++c)if(sc==key_from_name(control_keys[CONTROL_DISCONNECT][c]))return 1;
    return 0;
}
static const char *dc_lab_shortcut(void){
    double now=probe_clock();if(dc_lab.shortcut[0]&&now<dc_lab.shortcut_refresh)return dc_lab.shortcut;
    dc_lab.shortcut_refresh=now+.25;copystr(dc_lab.shortcut,sizeof(dc_lab.shortcut),"GUI only");
    for(int c=0;c<3;++c){const char *key=control_keys[CONTROL_DISCONNECT][c];
        if(key[0]&&strcmp(key,"None")&&dc_lab_key_available(key_from_name?key_from_name(key):0)){copystr(dc_lab.shortcut,sizeof(dc_lab.shortcut),key);break;}
    }return dc_lab.shortcut;
}
static void reconnect_lab_keys(const uint8_t *keys,int count,int allowed){
    int previous[3];memcpy(previous,control_previous[CONTROL_DISCONNECT],sizeof(previous));
    int pressed=controls_shortcut(keys,count,CONTROL_DISCONNECT,allowed&&!paused&&dc_lab_enabled());
    if(!pressed)return;
    for(int c=0;c<3;++c){int sc=key_from_name(control_keys[CONTROL_DISCONNECT][c]);
        if(sc>0&&sc<count&&keys[sc]&&!previous[c]&&dc_lab_key_available(sc)){dc_lab_start();return;}
    }
    snprintf(dc_lab.result,sizeof(dc_lab.result),"Shortcut already in use. Change Emergency hold in Controls > SBPE.");
}
static int reconnect_lab_consumes(struct Stage *st,int down,int key,int was_down){
    int sc=scancode_from_key?scancode_from_key(key):0;
    if(sc<=0||sc>=(int)sizeof(dc_lab.owned))return 0;
    if(!down&&dc_lab.owned[sc]){dc_lab.owned[sc]=0;return 1;}
    if(down&&dc_lab.owned[sc])return 1;
    if(down&&!was_down&&dc_lab_enabled()&&!paused&&st&&!st->textInputTarget&&!active_controls&&dc_lab_key_matches(sc)&&dc_lab_key_available(sc)){
        dc_lab.owned[sc]=1;return 1;
    }return 0;
}
static void reconnect_lab_frame(void){
    if(dc_lab.state==DC_DONE&&dc_lab.spawned_at){
        double now=probe_clock();struct DCLabSample current;
        if(now-dc_lab.spawned_at>5){dc_lab.spawned_at=0;return;}
        if(now-dc_lab.last_hp_sample>=.25&&dc_lab_read(&current)&&dc_lab_same(&current,&dc_lab.after)){
            dc_lab.last_hp_sample=now;
            if(current.hp!=dc_lab.after.hp){
                LOG("Loading test: disconnect lab trial=%u post-spawn HP changed after %.3f ms; hp=%d previous=%d. Observation only.",dc_lab.trial,(now-dc_lab.spawned_at)*1000,current.hp,dc_lab.after.hp);
                dc_lab.after=current;snprintf(dc_lab.result,sizeof(dc_lab.result),"After spawn: HP %d -> %d.",dc_lab.before.hp,current.hp);
            }
        }return;
    }
    if(dc_lab.state==DC_QUEUED){
        if(loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT){
            if(probe_reconnect_cancel)probe_reconnect_cancel(dc_lab.before.connection,dc_lab_confirm);
            dc_lab.state=DC_WAIT;
            LOG("Loading test: disconnect lab trial=%u native loss/loading overtook the queued request; no additional close issued.",dc_lab.trial);
        }else if(!dc_lab_enabled()||paused||probe_clock()-dc_lab.requested>2){
            if(probe_reconnect_cancel)probe_reconnect_cancel(dc_lab.before.connection,dc_lab_confirm);
            dc_lab_cancel_queue("Request cancelled or expired; no further close.");
        }
    }
    if(dc_lab.state!=DC_WAIT&&dc_lab.state!=DC_HELD)return;
    double elapsed=probe_clock()-dc_lab.requested;
    if(!dc_lab.hold_logged&&loading_probe.state==PROBE_HOLD){
        dc_lab.hold_logged=1;dc_lab.state=DC_HELD;
        snprintf(dc_lab.result,sizeof(dc_lab.result),"Held after %.2fs; observer must check disappearance.",elapsed);
        LOG("Loading test: disconnect lab trial=%u first held recovery after %.3f ms; ready_type=%d local_player=%s. This records local readiness only, not remote disappearance.",dc_lab.trial,elapsed*1000,loading_probe.ready_type,probe_world()&&probe_world()->clientWorld&&probe_world()->clientWorld->player?"present":"absent");
    }
    struct DCLabSample after;
    if(dc_lab_read(&after)){
        dc_lab.state=DC_DONE;dc_lab.after=after;dc_lab.spawned_at=dc_lab.last_hp_sample=probe_clock();
        snprintf(dc_lab.result,sizeof(dc_lab.result),"Spawned: HP %d -> %d. Check observer and room.",dc_lab.before.hp,after.hp);
        LOG("Loading test: disconnect lab trial=%u spawned after %.3f ms; hp_before=%d hp_after=%d same_game=%s game=%u world=%u. HP can change for other reasons; no survival claim inferred.",dc_lab.trial,elapsed*1000,dc_lab.before.hp,after.hp,after.game==dc_lab.before.game?"YES":"NO",after.game,after.world);
    }else if(elapsed>90){
        dc_lab.state=DC_DONE;snprintf(dc_lab.result,sizeof(dc_lab.result),"90s observation ended. Any loading hold stays active.");
        LOG("Loading test: disconnect lab trial=%u observation ended at 90s without a live spawned player; no readiness released or extra disconnect requested.",dc_lab.trial);
    }
}
static int dc_lab_rects(struct ProbeRect *panel,struct ProbeRect buttons[3],struct ProbeRect *tab){
    if(!dc_lab_enabled()||paused||active_controls||physical_w<340||physical_h<300)return 0;
    if(!dc_lab.initialized){dc_lab.initialized=1;dc_lab.visible=getbool(DC_LAB_SECTION,"gui_visible",1)&&physical_w>=760;}
    *panel=(struct ProbeRect){12,physical_h>=600?220:20,324,252};*tab=(struct ProbeRect){12,panel->y,180,28};
    buttons[0]=(struct ProbeRect){24,panel->y+104,300,28};buttons[1]=(struct ProbeRect){24,panel->y+156,145,28};buttons[2]=(struct ProbeRect){179,panel->y+156,145,28};return 1;
}
static int reconnect_lab_mouse(int x,int y,int button,int down,int ww,int hh){
    if(button<0||button>7)return 0;unsigned bit=1u<<button;int captured=dc_lab.captured&bit;
    struct ProbeRect p,b[3],tab;
    if(connectivity_ui_hidden||ww<=0||hh<=0||!dc_lab_rects(&p,b,&tab)){
        if(!down&&captured){dc_lab.captured&=~bit;return 1;}return 0;
    }
    x=scaled_mouse(x,physical_w,ww);y=scaled_mouse(y,physical_h,hh);
    if(down){
        if(!probe_inside(dc_lab.visible?p:tab,x,y))return 0;
        dc_lab.captured|=bit;dc_lab.was_open=dc_lab.visible;dc_lab.pressed=-1;
        if(!button){if(!dc_lab.visible)dc_lab.pressed=3;else for(int i=0;i<3;++i)if(probe_inside(b[i],x,y))dc_lab.pressed=i;}return 1;
    }
    if(!captured)return 0;dc_lab.captured&=~bit;int action=dc_lab.pressed;dc_lab.pressed=-1;
    if(button||dc_lab.was_open!=dc_lab.visible)return 1;
    if(action==3&&probe_inside(tab,x,y))dc_lab.visible=1;
    else if(action>=0&&action<3&&probe_inside(b[action],x,y)){
        if(action==0)dc_lab_start();
        else if(action==1){
            if(dc_lab.state==DC_QUEUED){if(probe_reconnect_cancel)probe_reconnect_cancel(dc_lab.before.connection,dc_lab_confirm);dc_lab_cancel_queue("Cancelled by user before close.");}
            else probe_manual_reconnect();
        }else dc_lab.visible=0;
    }return 1;
}
static void reconnect_lab_overlay(void){
    if(connectivity_ui_hidden)return;
    struct ProbeRect p,b[3],tab;if(!canvas_w||!canvas_h||!fill_rect||!dc_lab_rects(&p,b,&tab))return;
    int w=*canvas_w,h=*canvas_h;*canvas_w=physical_w;*canvas_h=physical_h;
    if(!dc_lab.visible)probe_gui_button(tab,"Emergency hold",0xff29485a);
    else{
        fill_rect(p.x,p.y,p.w,p.h,0xff182430,BLENDMODE_BLEND);
        probe_gui_text("Emergency hold",p.x+12,p.y+12,16,0xffffffff,0,0);
        const char *states[]={"Ready","Queued","Reconnecting","Holding","Ready","Not started"};
        char line[128];snprintf(line,sizeof(line),"%s  |  Shortcut: %.20s",states[dc_lab.state>=0&&dc_lab.state<=DC_CANCELLED?dc_lab.state:0],dc_lab_shortcut());
        probe_gui_text(line,p.x+12,p.y+38,12,0xff88e8bd,0,0);
        const char *message=dc_lab.result[0]?dc_lab.result:"Disconnect now; keep your next spawn held.";
        for(int row=0;row<2&&*message;++row){int count=(int)strlen(message);if(count>46){count=46;while(count>0&&message[count]!=' ')--count;if(!count)count=46;}
            snprintf(line,sizeof(line),"%.*s",count,message);probe_gui_text(line,p.x+12,p.y+62+row*16,12,0xffc8d7e4,0,0);message+=count;while(*message==' ')++message;
        }
        probe_gui_button(b[0],"Disconnect + hold",0xff315b86);
        probe_gui_text("This client only. No automatic health trigger.",p.x+12,p.y+136,12,0xffc8d7e4,0,0);
        probe_gui_button(b[1],"Resume / reconnect",0xff215c53);probe_gui_button(b[2],"Close panel",0xff30434e);
        probe_gui_text("F10 also resumes. Close keeps the hold active.",p.x+12,p.y+188,12,0xffc8d7e4,0,0);
        probe_gui_text("Remap shortcut: Controls > SBPE.",p.x+12,p.y+208,12,0xffc8d7e4,0,0);
        probe_gui_text("Experimental: damage protection unverified.",p.x+12,p.y+230,12,0xffffd185,0,0);
    }*canvas_w=w;*canvas_h=h;
}
#endif
