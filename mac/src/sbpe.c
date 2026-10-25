/* Mac SBPE beta (Intel process; Rosetta on Apple silicon). Derived ABI definitions: atomizer/sbpe (ISC).
 * Native implementation for one SHA-256/UUID-pinned StarBreak build.
 * Callbacks execute on the main/rendering thread. Verified network receive
 * guards are installed before networking begins.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#ifdef bool
#undef bool
#endif
#include "internals.h"
#include "generated.h"
#if !defined(SBPE_TEST) || defined(SBPE_CHECKED_READ_TEST)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#if __has_include(<mach/mach_vm.h>)
#include <mach/mach_vm.h>
#else
extern kern_return_t mach_vm_read_overwrite(mach_port_t,mach_vm_address_t,mach_vm_size_t,mach_vm_address_t,mach_vm_size_t*);
#endif
#endif

_Static_assert(sizeof(void*)==8,"Requires 64-bit Intel");
_Static_assert(offsetof(struct Stage,lastTimeMS)==0x78,"Stage ABI");
_Static_assert(offsetof(struct WorldClient,clientWorld)==0x78,"WorldClient ABI");
_Static_assert(offsetof(struct WorldClient,lastUpdate)==0x74,"WorldClient native update clock ABI");
_Static_assert(offsetof(struct WorldClient,serverConnection)==0x68,"WorldClient connection ABI");
_Static_assert(offsetof(struct WorldClient,scratchDelta)==0xb8,"WorldClient Delta ABI");
_Static_assert(offsetof(struct Delta,_has_bits)==0x40,"Delta presence ABI");
_Static_assert(offsetof(struct ClientWorld,serverSubWorld)==0x498,"Server sub-world ABI");
_Static_assert(offsetof(struct ClientWorld,clientSubWorlds)==0x4a0,"Sub-world vector ABI");
_Static_assert(offsetof(struct ClientWorld,mySubWorld)==0x4e0,"Own sub-world ABI");
_Static_assert(offsetof(struct ClientWorld,targetBuffer)==0x514,"Foreign-world throttle buffer ABI");
_Static_assert(offsetof(struct SubWorld,t)==0xc,"Sub-world simulation clock ABI");
_Static_assert(sizeof(struct PlayerInput)==0x20,"Native PlayerInput size ABI");
_Static_assert(offsetof(struct PlayerInput,total)==8&&offsetof(struct PlayerInput,_has_bits)==0x18,"Native PlayerInput fields ABI");
_Static_assert(offsetof(struct CreatePlayer,pos)==0x30&&offsetof(struct CreatePlayer,_has_bits)==0x5c,"Native CreatePlayer fields ABI");
_Static_assert(offsetof(struct WorldUpdate,worldid)==8,"WorldUpdate ID ABI");
_Static_assert(offsetof(struct ServerConnection,state)==0x28,"Connection state ABI");
_Static_assert(offsetof(struct GameClient,serverConnection)==0x70,"Game connection ABI");
_Static_assert(offsetof(struct GameClient,worldClient)==0x80,"Game world ABI");
_Static_assert(offsetof(struct ServerConnection,socket)==0x54,"Connection socket ABI");
_Static_assert(offsetof(struct ServerConnection,inputType)==0x70,"Connection input type ABI");
_Static_assert(offsetof(struct ServerConnection,inputLen)==0x74,"Connection input length ABI");
_Static_assert(offsetof(struct ServerConnection,socketBufferSize)==0xc0,"Connection received bytes ABI");
_Static_assert(offsetof(struct WorldView,offset)==0xbc,"WorldView ABI");
_Static_assert(offsetof(struct WorldObject,props)==0x10,"WorldObject ABI");
_Static_assert(offsetof(struct ClientWorld,player)==0x4f0,"ClientWorld ABI");
_Static_assert(sizeof(struct CreateWorld)==48&&offsetof(struct CreateWorld,worldstate)==16&&offsetof(struct CreateWorld,spawnobj)==24&&offsetof(struct CreateWorld,worldid)==32,"CreateWorld preflight ABI");
_Static_assert(offsetof(struct WorldState,serverworld)==8&&offsetof(struct WorldState,clientworld)==16&&offsetof(struct WorldState_World,obj)==8&&offsetof(struct WorldState_World,ownerid)==32,"WorldState preflight ABI");
_Static_assert(offsetof(struct WorldState_World_Obj,props)==8&&offsetof(struct WorldState_World_Obj,objid)==16,"Spawn object preflight ABI");
_Static_assert(offsetof(struct Properties,terraintype)==0x44&&offsetof(struct Properties,draw)==0x68&&offsetof(struct Properties,boundsworld)==0x75&&offsetof(struct Properties,spawn)==0x98,"Tile-map spawn predicate ABI");

#define LIMIT 8192
#define BIND_LIMIT 128
#define OPT_LIMIT 512
#define MAX_TEXT 2048
#define PTR(a) ((uintptr_t)(a)+slide)
static intptr_t slide;
static FILE *logfile;
static void (*sbpe_log_mirror)(uint64_t,const char*,va_list);
static void sbpe_log(const char *format,...){
    if(!logfile&&!sbpe_log_mirror)return;struct timespec now;clock_gettime(CLOCK_REALTIME,&now);va_list args;va_start(args,format);
    if(sbpe_log_mirror){va_list copy;va_copy(copy,args);sbpe_log_mirror((uint64_t)now.tv_sec*1000+(uint64_t)now.tv_nsec/1000000,format,copy);va_end(copy);}
    if(logfile){struct tm local;localtime_r(&now.tv_sec,&local);char stamp[48];strftime(stamp,sizeof(stamp),"%Y-%m-%d %H:%M:%S",&local);
        fprintf(logfile,"[%s.%03ld] ",stamp,now.tv_nsec/1000000);vfprintf(logfile,format,args);fputc('\n',logfile);fflush(logfile);}
    va_end(args);
}
#define LOG(...) sbpe_log(__VA_ARGS__)

struct Option { char section[64], key[160], value[512]; };
static struct Option options[OPT_LIMIT]; static int noptions;
#define OPTION_INDEX_SIZE (OPT_LIMIT*4)
static uint16_t option_index[OPTION_INDEX_SIZE];static int indexed_options=-1;
struct Binding { char plugin[40],option[80],kind[16],values[8][32],key[64]; int nvalues,scancode,previous,index; };
static struct Binding bindings[BIND_LIMIT]; static int nbindings;
static int zoom_width=-1,center_mode,map_visible,hp_visible,hitboxes,hide_shells,hide_ally,hide_chat;
static int extra_enabled=1,paused=0,initialized=0,playerlist_visible,hide_names,hide_healthbars;
static int highlight_hide;
static int self_hp_visible=1,custom_hud_visible=1;
static int text_outline=2;
static uint32_t text_outline_color=0xff000000,text_tint=0xff000000;
static int physical_w,physical_h,desired_w,desired_h;
static double render_pixel_ratio=1;
static int previous_reset,previous_boxes;
static char config_path[4096];

static struct Stage **stage_ptr;
static int *canvas_w,*canvas_h;
static void **window_ptr;
static void (*original_draw)(struct Stage*,int);
static void (*original_resize)(struct Stage*,int);
static void (*container_resize)(void*,int);
static void (*original_move)(int,int,void*);
static void (*original_button)(int,int,int,int,void*);
static void (*get_window_size)(void*,int*,int*);
static const uint8_t *(*get_keys)(int*);
static int (*key_from_name)(const char*);
static void *(*get_keyboard_focus)(void);
static int (*scancode_from_key)(int);
static const char *(*scancode_name)(int);
static int event_input_enabled;
static uint8_t event_keys[512];
static struct Controls *active_controls;
static int (*original_stage_keyboard)(struct Stage*,int,int);
static void (*fill_rect)(int,int,int,int,uint32_t,int);
static void (*fill_tri)(int,int,int,int,int,int,uint32_t,int);
static void (*draw_rect)(int,int,int,int,uint32_t,int);
static unsigned (*create_text)(unsigned,const char*,int,uint32_t,const char*,int,uint32_t);
static void (*query_texture)(unsigned,int*,int*);
static void (*draw_texture)(unsigned,int,int,int,int,int,int,int,int,int,int,int,int,uint32_t,int);
static uint8_t *game_hitboxes;
static uint8_t *game_boxes;
static int getbool(const char*,const char*,int);
static int readable_names_enabled(void);
static double monotime(void);
static void mouse_activity(void);
static void loading_probe_keys(const uint8_t*,int,int);
static int loading_probe_consumes(struct Stage*,int);
static int probe_camera_consumes(struct Stage*,int,int,int);
static int loading_probe_mouse(int,int,int,int,int,int);
static int reconnect_lab_mouse(int,int,int,int,int,int);
static void reconnect_lab_keys(const uint8_t*,int,int);
static int reconnect_lab_consumes(struct Stage*,int,int,int);
static void inventory_keys(const uint8_t*,int,int);
static int inventory_consumes(struct Stage*,int,int,int);
/* Presentation only: readiness holds, reconnects and mirrored input continue. */
static int connectivity_ui_hidden;
static void multibox_key(struct Stage*);
static int multibox_mouse(int,int,int,int,int,int);
static int bow_mouse(int,int,int,int,int,int);
static void (*original_animation_draw)(void*,int,int);
static void animation_draw(void *obj,int time,int layer){
    if(paused||!getbool("plugin_noise","hide_effects",1))original_animation_draw(obj,time,layer);
}
static void (*original_tex_parameteri)(unsigned,unsigned,int);
static void sharp_tex_parameteri(unsigned target,unsigned name,int value){
    /* Override only min/mag sampling; preserve wrap and all other parameters. */
    if(target==0x0de1&&(name==0x2800||name==0x2801)&&getbool("native","sharp_pixels",1))value=0x2600;
    static int reported; if(!reported&&(name==0x2800||name==0x2801)){LOG("Texture sampling wrapper reached, filter=0x%x",value);reported=1;}
    original_tex_parameteri(target,name,value);
}

static char *trim(char *s) {
    while(isspace((unsigned char)*s)) ++s;
    size_t n=strlen(s); while(n && isspace((unsigned char)s[n-1])) s[--n]=0;
    return s;
}
static void copystr(char *dst,size_t n,const char *src) { snprintf(dst,n,"%s",src); }
static size_t option_hash(const char *section,const char *key){
    uint32_t h=2166136261U;for(const unsigned char *p=(const void*)section;*p;++p)h=(h^*p)*16777619U;
    h=(h^0xffU)*16777619U;for(const unsigned char *p=(const void*)key;*p;++p)h=(h^*p)*16777619U;return h&(OPTION_INDEX_SIZE-1);
}
static void index_options(void){
    memset(option_index,0,sizeof(option_index));
    for(int i=0;i<noptions;++i){size_t slot=option_hash(options[i].section,options[i].key);
        while(option_index[slot]){struct Option *old=&options[option_index[slot]-1];if(!strcmp(old->section,options[i].section)&&!strcmp(old->key,options[i].key))break;slot=(slot+1)&(OPTION_INDEX_SIZE-1);}
        option_index[slot]=(uint16_t)(i+1); /* Last duplicate wins, as in the INI reader. */
    }indexed_options=noptions;
}
static struct Option *find_option(const char *section,const char *key){
    if(indexed_options!=noptions)index_options();size_t slot=option_hash(section,key);
    while(option_index[slot]){struct Option *o=&options[option_index[slot]-1];if(!strcmp(o->section,section)&&!strcmp(o->key,key))return o;slot=(slot+1)&(OPTION_INDEX_SIZE-1);}return NULL;
}
static const char *getoptv(const char *section,const char *key,const char *fallback) {
    struct Option *o=find_option(section,key);return o?o->value:fallback;
}
static int boolean(const char *s) { return !strcasecmp(s,"yes")||!strcasecmp(s,"true")||!strcmp(s,"1"); }
static int getbool(const char *s,const char *k,int d) {return boolean(getoptv(s,k,d?"yes":"no"));}
static double getnum(const char *s,const char *k,double d) {
    const char *v=getoptv(s,k,"");char *end;double r=strtod(v,&end);
    return end==v||!isfinite(r)||fabs(r)>1e9?d:r;
}
static uint32_t parse_color(const char *s) {
    size_t n=strlen(s);if(n==0||n>8)return 0;char *e;unsigned long c=strtoul(s,&e,16);if(*e)return 0;
    if(n==1&&c==0)return 0;
    if(n==3)c=((c>>8&15)*17<<16)|((c>>4&15)*17<<8)|((c&15)*17);
    if(n<=6)c|=0xff000000UL;return (uint32_t)c;
}
static uint32_t color(const char *s,const char *k,const char *d){return parse_color(getoptv(s,k,d));}
static double clamp(double x,double lo,double hi){return x<lo?lo:(x>hi?hi:x);}
static int vector_count(struct STDVector v,size_t size) {
    uintptr_t a=(uintptr_t)v.start,b=(uintptr_t)v.finish,e=(uintptr_t)v.endOfStorage;
    if(!size||!a||b<a||e<b||(b-a)%size)return 0;
    size_t n=(b-a)/size;return n>LIMIT?0:(int)n;
}
static int *option_target(const char *plugin,const char *option) {
    if(!strcmp(plugin,"zoom")&&!strcmp(option,"level"))return &zoom_width;
    if(!strcmp(plugin,"misc")&&!strcmp(option,"centered"))return &center_mode;
    if(!strcmp(plugin,"misc")&&!strcmp(option,"drawHitBoxes"))return &hitboxes;
    if(!strcmp(plugin,"map")&&!strcmp(option,"visible"))return &map_visible;
    if(!strcmp(plugin,"extra_info")&&!strcmp(option,"show_hp"))return &hp_visible;
    if(!strcmp(plugin,"advanced_hp")&&!strcmp(option,"show_adv_hp"))return &hp_visible;
    if(!strcmp(plugin,"native")&&!strcmp(option,"hp_hud"))return &self_hp_visible;
    if(!strcmp(plugin,"noise")&&!strcmp(option,"hide_names"))return &hide_names;
    if(!strcmp(plugin,"noise")&&(!strcmp(option,"hide_healthbars")||!strcmp(option,"hide_healtbars")))return &hide_healthbars;
    if(!strcmp(plugin,"playerlist")&&!strcmp(option,"visible"))return &playerlist_visible;
    if(!strcmp(plugin,"noise")&&!strcmp(option,"hide_shells"))return &hide_shells;
    if(!strcmp(plugin,"noise")&&!strcmp(option,"hide_ally_objects"))return &hide_ally;
    if(!strcmp(plugin,"noise")&&!strcmp(option,"hide_chat"))return &hide_chat;
    /* The supplied highlighting plugin's hide_players key becomes shell hiding. */
    if(!strcmp(plugin,"highlight_player3")&&!strcmp(option,"hide_players"))return &highlight_hide;
    return NULL;
}
static int bind_value(const char *v){
    if(!strcasecmp(v,"yes")||!strcasecmp(v,"no")||!strcasecmp(v,"true")||!strcasecmp(v,"false"))return boolean(v);
    long long n=strtoll(v,NULL,10);return n<INT32_MIN?INT32_MIN:(n>INT32_MAX?INT32_MAX:(int)n);
}
static int binding_equal(const char *a,const char *b){
    if(!strcasecmp(a,b))return 1;
    int ba=!strcasecmp(a,"yes")||!strcasecmp(a,"no")||!strcasecmp(a,"true")||!strcasecmp(a,"false");
    int bb=!strcasecmp(b,"yes")||!strcasecmp(b,"no")||!strcasecmp(b,"true")||!strcasecmp(b,"false");
    return ba&&bb&&boolean(a)==boolean(b);
}
static int set_option(const char *section,const char *key,const char *value){
    struct Option *o=find_option(section,key);
    if(!o){if(noptions>=OPT_LIMIT)return 0;o=&options[noptions++];copystr(o->section,sizeof(o->section),section);copystr(o->key,sizeof(o->key),key);}
    copystr(o->value,sizeof(o->value),value);return 1;
}
static void binding_set(struct Binding *b,const char *value){
    int *v=option_target(b->plugin,b->option);if(v)*v=bind_value(value);
    char section[64];snprintf(section,sizeof(section),"plugin_%s",b->plugin);set_option(section,b->option,value);
}
static void parse_binding(const char *spec,const char *key) {
    if(nbindings==BIND_LIMIT)return;
    char tmp[256];copystr(tmp,sizeof(tmp),spec);char *save=NULL;
    struct Binding b={0};char *p=strtok_r(tmp," \t",&save);if(!p)return;copystr(b.plugin,sizeof(b.plugin),p);
    p=strtok_r(NULL," \t",&save);if(!p)return;copystr(b.option,sizeof(b.option),p);
    p=strtok_r(NULL," \t",&save);if(!p)return;copystr(b.kind,sizeof(b.kind),p);
    while((p=strtok_r(NULL," \t",&save))&&b.nvalues<8)copystr(b.values[b.nvalues++],32,p);
    if(!b.nvalues||(!strcmp(b.kind,"hold")&&b.nvalues!=2))return;
    if(strcmp(b.kind,"hold")&&strcmp(b.kind,"toggle")&&strcmp(b.kind,"set"))return;
    char section[64];snprintf(section,sizeof(section),"plugin_%s",b.plugin);
    if(!option_target(b.plugin,b.option)&&!*getoptv(section,b.option,"")){LOG("Unsupported binding retained but inactive: %s",spec);return;}
    copystr(b.key,sizeof(b.key),key);
    if(strlen(b.key)==1&&isalpha((unsigned char)b.key[0]))b.key[0]=(char)toupper((unsigned char)b.key[0]);
    b.scancode=key_from_name?key_from_name(b.key):0;
    if(b.scancode<=0){LOG("Unknown key: %s",key);return;}
    b.index=-1;bindings[nbindings++]=b;
}
#include "controls_config.h"
static int load_config(void) {
    FILE *f=fopen(config_path,"r");if(!f){LOG("Could not open config.ini");return 0;}
    noptions=0;nbindings=0;indexed_options=-1;char section[64]="",line[2048];
    while(fgets(line,sizeof(line),f)) {
        char *s=trim(line);if(!*s||*s=='#'||*s==';')continue;
        if(*s=='['){char *e=strchr(s,']');if(e){*e=0;copystr(section,sizeof(section),s+1);}continue;}
        char *e=strchr(s,'=');if(!e)continue;*e=0;char *key=trim(s),*value=trim(e+1);
        /* Semicolon is a valid key name; do not treat inline semicolons as comments. */
        if(noptions<OPT_LIMIT){struct Option *o=&options[noptions++];copystr(o->section,64,section);copystr(o->key,160,key);copystr(o->value,512,value);}
    }
    fclose(f);
    zoom_width=(int)getnum("plugin_zoom","level",-1);center_mode=(int)getnum("plugin_misc","centered",0);
    map_visible=getbool("plugin_map","visible",0);hp_visible=getbool("plugin_advanced_hp","show_adv_hp",getbool("plugin_extra_info","show_hp",1));
    self_hp_visible=getbool("native","hp_hud",1);
    custom_hud_visible=1; /* Always use the Windows-style HUD. */
    hitboxes=getbool("plugin_misc","drawHitBoxes",0);hide_shells=getbool("plugin_noise","hide_shells",0);
    hide_ally=getbool("plugin_noise","hide_ally_objects",0);hide_chat=getbool("plugin_noise","hide_chat",0);
    hide_names=getbool("plugin_noise","hide_names",0);hide_healthbars=getbool("plugin_noise","hide_healthbars",0);
    highlight_hide=getbool("plugin_highlight_player3","hide_players",0);playerlist_visible=getbool("plugin_playerlist","visible",0);
    for(int i=0;i<noptions;++i)if(!strcmp(options[i].section,"plugin_keybinds")&&strchr(options[i].key,' '))parse_binding(options[i].key,options[i].value);
    controls_load();
    controls_seed_bindings();
    LOG("Configuration loaded, %d native gameplay bindings; Controls > SBPE bindings applied.",nbindings);
    return 1;
}
static void apply_binding(struct Binding *b,int down) {
    if(!strcmp(b->kind,"hold")){if(down!=b->previous)binding_set(b,b->values[down?0:1]);}
    else if(down&&!b->previous){
        if(!strcmp(b->kind,"toggle")){
            int *target=option_target(b->plugin,b->option),current=-1;char section[64];snprintf(section,sizeof(section),"plugin_%s",b->plugin);
            const char *value=getoptv(section,b->option,"");
            for(int i=0;i<b->nvalues;++i)if(target?bind_value(b->values[i])==*target:binding_equal(b->values[i],value))current=i;
            b->index=(current+1)%b->nvalues;binding_set(b,b->values[b->index]);
        }else binding_set(b,b->values[0]);
    }
    b->previous=down;
}
static int edge_key(const uint8_t *keys,int count,const char *name,int *previous,int allow) {
    int sc=key_from_name(name);int down=sc>0&&sc<count&&keys[sc];int result=down&&!*previous&&allow;*previous=down;return result;
}
static void keyboard(void) {
    int n=0;const uint8_t *keys=input_keys(&n);if(!keys)return;
    struct Stage *st=stage_ptr?*stage_ptr:NULL;
    /* A native key event proves the game received input. SDL's focus pointer
       can be stale on initial Cocoa activation even while movement works. */
    int allowed=(event_input_enabled||!get_keyboard_focus||get_keyboard_focus()==*window_ptr)&&(!st||!st->textInputTarget)&&!active_controls;
    loading_probe_keys(keys,n,allowed);
    reconnect_lab_keys(keys,n,allowed);
    if(controls_shortcut(keys,n,CONTROL_PAUSE,allowed)){paused=!paused;LOG("Mod pause=%d",paused);}
    if(controls_shortcut(keys,n,CONTROL_RELOAD,allowed))load_config();
    if(controls_shortcut(keys,n,CONTROL_EXTRA,allowed))extra_enabled=!extra_enabled;
    inventory_keys(keys,n,allowed&&!paused);
    /* Debug shortcuts are opt-in; ordinary zoom controls remain available. */
    if(getbool("native","debug_shortcuts",0)){
        if(edge_key(keys,n,"F7",&previous_boxes,allowed))hitboxes=!hitboxes;
        if(edge_key(keys,n,"F8",&previous_reset,allowed)&&!getbool("plugin_loading_probe","enabled",0))zoom_width=-1;
    }
    for(int i=0;i<nbindings;++i){struct Binding *b=&bindings[i];int down=b->scancode<n&&keys[b->scancode];
        /* Primary/secondary/tertiary keys are alternatives for the same hold.
           Releasing one must keep the action on while another is held. */
        if(!strcmp(b->kind,"hold"))for(int j=0;j<nbindings;++j){struct Binding *other=&bindings[j];
            if(!strcmp(other->kind,"hold")&&!strcmp(other->plugin,b->plugin)&&!strcmp(other->option,b->option)&&!strcmp(other->values[0],b->values[0])&&!strcmp(other->values[1],b->values[1]))down|=other->scancode<n&&keys[other->scancode];}
        if(!allowed||paused){
            if(!strcmp(b->kind,"hold")){if(b->previous)apply_binding(b,0);b->previous=0;}
            else b->previous=down;
            continue;
        }
        apply_binding(b,down);
    }
}
static int stage_keyboard(struct Stage *st,int down,int key){
    int sc=scancode_from_key?scancode_from_key(key):0;
    int was_down=sc>0&&sc<(int)sizeof(event_keys)?event_keys[sc]:0;
    if(sc>0&&sc<(int)sizeof(event_keys)){
        event_keys[sc]=down!=0;
        /* Process before forwarding, while chat/menu ownership still describes
           the recipient of this event. Repeats retain the same edge state. */
        keyboard();
    }
    if(inventory_consumes(st,down,key,was_down)||reconnect_lab_consumes(st,down,key,was_down)||probe_camera_consumes(st,down,key,was_down)||loading_probe_consumes(st,key))return 1;
    multibox_key(st);
    return original_stage_keyboard(st,down,key);
}
static const char *class_name(void *obj) {
    if(!obj)return "";void **vtable=*(void***)obj;if(!vtable)return "";
    void **ti=(void**)vtable[-1];if(!ti)return "";const char *s=(const char*)ti[1];if(!s)return "";
    while(isdigit((unsigned char)*s))++s;return s;
}
static struct GameClient *game_client(void) {
    struct Stage *st=stage_ptr?*stage_ptr:NULL;if(!st)return NULL;
    struct STDVector v=st->asUIElementContainer.children;int n=vector_count(v,sizeof(void*));if(!n)return NULL;
    void *first=((void**)v.start)[0];if(strcmp(class_name(first),"GameClient"))return NULL;
    return first;
}
static struct WorldClient *world_client(void) {
    struct GameClient *gc=game_client();return gc?gc->worldClient:NULL;
}
static void resize_stage(struct Stage *st,int event) {
    /* XDL event 3 is SDL_WINDOWEVENT_FOCUS_LOST in this pinned client. */
    if(event==3&&event_input_enabled){memset(event_keys,0,sizeof(event_keys));keyboard();}
    if(event!=1){original_resize(st,event);return;}
    int w=0,h=0;get_window_size(*window_ptr,&w,&h);if(w<1||h<1){original_resize(st,event);return;}
    physical_w=w;physical_h=h;double scale=(!paused&&zoom_width>0)?clamp((double)zoom_width/w,0.5,8.0):1;
    desired_w=(int)lround(w*scale);desired_h=(int)lround(h*scale);
    *canvas_w=desired_w;*canvas_h=desired_h;
    /* World-linked HUD panels, equipment and their input all share the
     * game's virtual canvas. Splitting these sizes moves anchored panels. */
    st->asUIElementContainer.asUIElement.w=desired_w;
    st->asUIElementContainer.asUIElement.h=desired_h;
    container_resize(st,1);
}
static void zoom_update(struct Stage *st) {
    static double target=1,start=1,started;double now=monotime();
    int w=0,h=0;get_window_size(*window_ptr,&w,&h);if(w<1||h<1)return;
    double scale=(!paused&&zoom_width>0)?clamp((double)zoom_width/w,0.5,8.0):1;
    if(!getbool("plugin_zoom","active",1))scale=1;
    if(scale!=target){start=physical_w>0?(double)*canvas_w/physical_w:1;target=scale;started=now;}
    double duration=paused?0:clamp(getnum("plugin_zoom","time",0),0,5);
    if(duration>0){double f=clamp((now-started)/duration,0,1);scale=start+(target-start)*f*(2-f);}
    int tw=(int)lround(w*scale),th=(int)lround(h*scale);
    if(w!=physical_w||h!=physical_h||*canvas_w!=tw||*canvas_h!=th||st->asUIElementContainer.asUIElement.w!=tw||st->asUIElementContainer.asUIElement.h!=th){
        physical_w=w;physical_h=h;desired_w=tw;desired_h=th;*canvas_w=tw;*canvas_h=th;
        st->asUIElementContainer.asUIElement.w=tw;st->asUIElementContainer.asUIElement.h=th;container_resize(st,1);
    }
}
static int scaled_mouse(int value,int canvas,int actual){return actual>0?(int)lround((double)value*canvas/actual):value;}
static void mouse_move(int x,int y,void *u){mouse_activity();int w=0,h=0;get_window_size(*window_ptr,&w,&h);original_move(scaled_mouse(x,*canvas_w,w),scaled_mouse(y,*canvas_h,h),u);}
static void mouse_button(int x,int y,int b,int down,void *u){mouse_activity();int w=0,h=0;get_window_size(*window_ptr,&w,&h);if(reconnect_lab_mouse(x,y,b,down,w,h)||bow_mouse(x,y,b,down,w,h)||multibox_mouse(x,y,b,down,w,h)||loading_probe_mouse(x,y,b,down,w,h))return;original_button(scaled_mouse(x,*canvas_w,w),scaled_mouse(y,*canvas_h,h),b,down,u);}

static struct WorldObject *objects[LIMIT];static int nobjects;
static uintptr_t seen_objects[LIMIT*2];
static uint8_t seen_foreign[LIMIT*2];static int collecting_foreign;
static uint8_t seen_source[LIMIT*2];static int collecting_source;
static void add_object(struct WorldObject *obj){
    if(!obj||nobjects>=LIMIT)return;
    size_t i=(((uintptr_t)obj>>4)*2654435761U)%(LIMIT*2);
    while(seen_objects[i]&&seen_objects[i]!=(uintptr_t)obj)i=(i+1)%(LIMIT*2);
    if(collecting_foreign)seen_foreign[i]=1;
    seen_source[i]|=collecting_source;
    if(seen_objects[i])return;
    seen_objects[i]=(uintptr_t)obj;objects[nobjects++]=obj;
}
static void add_subworld(struct SubWorldImpl *sub,int foreign) {
    if(!sub)return;struct STDVector v=sub->objs.vec;int n=vector_count(v,sizeof(struct SortedVecElement));
    struct SortedVecElement *p=v.start;collecting_foreign=foreign;for(int i=0;i<n;++i)add_object(p[i].obj);collecting_foreign=0;
}
static void collect_objects(struct ClientWorld *cw) {
    nobjects=0;collecting_foreign=0;memset(seen_objects,0,sizeof(seen_objects));memset(seen_foreign,0,sizeof(seen_foreign));memset(seen_source,0,sizeof(seen_source));if(!cw)return;
    collecting_source=1;
    if(cw->serverSubWorld)add_subworld(&cw->serverSubWorld->asSubWorldImpl,0);
    collecting_source=2;
    if(cw->mySubWorld)add_subworld(&cw->mySubWorld->asNativeSubWorld.asSubWorldImpl,0);
    collecting_source=0;
    /* Foreign client worlds use the same checked vector-of-pairs layout. */
    struct STDVector v=cw->clientSubWorlds.vec;int n=vector_count(v,sizeof(struct SortedVecElement));
    struct SortedVecElement *p=v.start;for(int i=0;i<n;++i)if(p[i].obj)add_subworld(&((struct ForeignSubWorld*)p[i].obj)->asSubWorldImpl,1);
    v=cw->allies;n=vector_count(v,sizeof(void*));struct WorldObject **a=v.start;collecting_source=4;for(int i=0;i<n;++i)add_object(a[i]);collecting_source=0;
    add_object((struct WorldObject*)cw->player);
}
static unsigned object_source(struct WorldObject *o){
    size_t i=(((uintptr_t)o>>4)*2654435761U)%(LIMIT*2);
    while(seen_objects[i]&&seen_objects[i]!=(uintptr_t)o)i=(i+1)%(LIMIT*2);
    return seen_objects[i]?seen_source[i]:0;
}
static void short_string(const struct STDString*,char[24]);
static void read_string(const struct STDString*,char*,size_t);
static int read_memory(const void*,void*,size_t);
static int in_list(const char*,const char*);
struct PlayerIdentity {char name[256],faction[256],account[256];int level;};
static int player_identity(struct WorldObject *obj,struct PlayerIdentity *out){
    memset(out,0,sizeof(*out));out->level=-1;if(!obj)return 0;
    struct Properties p;struct PlayerData pd;if(!read_memory(&obj->props,&p,sizeof(p)))return 0;
    read_string(p.displayname,out->name,sizeof(out->name));
    if(read_memory(p.playerdata,&pd,sizeof(pd))){
        read_string(pd.accountid,out->account,sizeof(out->account));read_string(pd.factionname,out->faction,sizeof(out->faction));out->level=pd.level;
        if(!out->name[0]&&out->account[0])snprintf(out->name,sizeof(out->name),"#%.6s",out->account);
    }
    return out->name[0]||out->faction[0];
}
static int highlight_list_match(const char *name,const char *list,int faction){
    if(!*name)return 0;int comma=strchr(list,',')!=NULL;
    const char *p=list;
    while(*p){
        while(*p&&(comma?(*p==','||isspace((unsigned char)*p)):isspace((unsigned char)*p)))++p;
        const char *start=p;while(*p&&(comma?*p!=',':!isspace((unsigned char)*p)))++p;
        size_t n=(size_t)(p-start);while(n&&isspace((unsigned char)start[n-1]))--n;
        if(n&&n<256){char entry[256];memcpy(entry,start,n);entry[n]=0;if(faction)for(size_t i=0;i<n;++i)if(entry[i]=='_')entry[i]=' ';if(!strcmp(name,entry))return 1;}
    }
    return 0;
}
static unsigned player_matches(const struct PlayerIdentity *id){
    return (highlight_list_match(id->name,getoptv("native_highlight","players",""),0)?1:0) |
           (highlight_list_match(id->faction,getoptv("native_highlight","factions",""),1)?2:0);
}
static unsigned highlight_parts(const struct PlayerIdentity *id,unsigned *primary){
    unsigned matches=player_matches(id),flags=0;
    if((matches&1)&&getbool("plugin_highlight_player3","highlight_players",1))flags|=1;
    if((matches&2)&&getbool("plugin_highlight_player3","highlight_faction",1))flags|=2;
    *primary=flags;if(flags&&getbool("plugin_highlight_player3","highlight_both",1))flags=3;
    return flags;
}
static int notable(struct WorldObject *obj){
    struct PlayerIdentity id;if(!player_identity(obj,&id))return 0;unsigned matches=player_matches(&id);
    return ((matches&1)&&getbool("plugin_highlight_player3","whitelist_players",1))||((matches&2)&&getbool("plugin_highlight_player3","whitelist_factions",1));
}
static int shell_hidden(struct WorldObject *o){
    return hide_shells||(highlight_hide&&!notable(o));
}
static int from_ally_world(struct ClientWorld *cw,struct WorldObject *obj){
    (void)cw;size_t i=(((uintptr_t)obj>>4)*2654435761U)%(LIMIT*2);
    while(seen_objects[i]&&seen_objects[i]!=(uintptr_t)obj)i=(i+1)%(LIMIT*2);
    return seen_objects[i]&&seen_foreign[i];
}

struct Hidden {struct WorldObject *obj;int x,hidden;};static struct Hidden hidden[LIMIT];static int nhidden;
static int object_hidden(struct ClientWorld *cw,struct WorldObject *obj){
    if(!cw||obj==(struct WorldObject*)cw->player)return 0;
    struct STDVector v=cw->allies;int n=vector_count(v,sizeof(void*));struct WorldObject **allies=v.start;
    for(int j=0;j<n;++j)if(obj==allies[j])return shell_hidden(obj);
    if(!hide_ally||obj->props.terraintype!=0||!from_ally_world(cw,obj))return 0;
    char vid[24];short_string(obj->props.vid,vid);return strncmp(vid,"timer",5)!=0;
}
static void hide_objects(struct ClientWorld *cw){
    nhidden=0;if(!cw||(!hide_shells&&!hide_ally&&!highlight_hide))return;
    for(int i=0;i<nobjects;++i){struct WorldObject *obj=objects[i];
        if(object_hidden(cw,obj)){
            int x=obj->props.xmp;if(x<INT32_MIN+(1<<29))continue;
            int nx=x-(1<<29);hidden[nhidden++]=(struct Hidden){obj,x,nx};obj->props.xmp=nx;
        }
    }
}
static void restore_objects(void){for(int i=0;i<nhidden;++i)if(hidden[i].obj->props.xmp==hidden[i].hidden)hidden[i].obj->props.xmp=hidden[i].x;nhidden=0;}
static void center_camera(struct WorldView *wv,int mode) {
    if(!wv)return;const int bn=1000000;
    if(mode)wv->offsetsInitialized=0;
    if(mode==2&&wv->playerBounds.w<wv->worldBounds.w+bn){wv->playerBounds.x-=bn;wv->playerBounds.y-=bn;wv->playerBounds.w+=bn*2;wv->playerBounds.h+=bn*2;}
    if(mode!=2&&wv->playerBounds.w>wv->worldBounds.w+bn){wv->playerBounds.x+=bn;wv->playerBounds.y+=bn;wv->playerBounds.w-=bn*2;wv->playerBounds.h-=bn*2;wv->offsetsInitialized=0;}
}
static void outline(int x,int y,int w,int h,int thickness,uint32_t c){if(w<1||h<1||!c)return;thickness=thickness<1?1:thickness;fill_rect(x,y,w,thickness,c,0);fill_rect(x,y+h-thickness,w,thickness,c,0);fill_rect(x,y,thickness,h,c,0);fill_rect(x+w-thickness,y,thickness,h,c,0);}
static void shape_blend(double x,double y,double w,double h,uint32_t c,unsigned type,int blend){
    const double maximum=INT32_MAX/4.0;
    if(!c||!isfinite(x+y+w+h)||w<0||h<0||fabs(x)>maximum||fabs(y)>maximum||w>maximum||h>maximum)return;int a=(int)floor(x),b=(int)floor(y),ww=(int)ceil(w),hh=(int)ceil(h);if(ww<1)ww=1;if(hh<1)hh=1;
    if(type==SHAPE_LR_TRI)fill_tri(a+ww,b,a+ww,b+hh,a,b+hh,c,blend);
    else if(type==SHAPE_LL_TRI)fill_tri(a,b,a+ww,b+hh,a,b+hh,c,blend);
    else if(type==SHAPE_UL_TRI)fill_tri(a,b,a+ww,b,a,b+hh,c,blend);
    else if(type==SHAPE_UR_TRI)fill_tri(a,b,a+ww,b,a+ww,b+hh,c,blend);
    else fill_rect(a,b,ww,hh,c,blend);
}
static void shape(double x,double y,double w,double h,uint32_t c,unsigned type){shape_blend(x,y,w,h,c,type,BLENDMODE_NONE);}
static void map_shape(double x,double y,double w,double h,uint32_t c,unsigned type){
    unsigned alpha=(unsigned)lround((c>>24)*clamp(getnum("plugin_map","opacity",.4),0,1));
    if(!alpha)return;
    shape_blend(x,y,w,h,(c&0xffffff)|(alpha<<24),type,BLENDMODE_BLEND);
}
static uint32_t object_color(struct WorldObject *o,struct ClientWorld *cw){
    struct Properties *p=&o->props;const char *s="plugin_map";
    if(o==(struct WorldObject*)cw->player)return color(s,"player_self","0f0");
    if(p->ownertype==OWNERTYPE_ENEMY)return color(s,p->damage>0?"enemy":"enemy_harmless",p->damage>0?"f00":"f80");
    if(p->terraintype==TERRAIN_PLATFORM)return color(s,"platform","fff");
    if(p->terraintype==TERRAIN_LEDGE)return color(s,"ledge","aaa");
    if(p->ownertype==OWNERTYPE_PLAYER){
        struct STDVector av=cw->allies;int an=vector_count(av,sizeof(void*));struct WorldObject **a=av.start;
        for(int i=0;i<an;++i)if(a[i]==o)return color(s,"player","ff0");return 0;
    }
    if(p->interact){if(p->interactdescription&&p->interactdescription->type==INTERACTTYPE_ITEM)return color(s,"item","0");if(p->interactdescription&&p->interactdescription->type==INTERACTTYPE_OPEN_CHEST)return color(s,"chest","08f");return color(s,"interact","0ff");}
    if(p->trigger)return color(s,"trigger","f0f");return 0;
}
static void draw_map(struct ClientWorld *cw,struct WorldView *wv){
    if(!map_visible||!cw||!wv)return;
    if(getbool("plugin_map","hide_outside_zones",0)&&cw->asWorld.props.safe)return;
    struct Rectangle b=wv->worldBounds;if(b.w<=0||b.h<=0)return;
    double scale=fmin(clamp(getnum("plugin_map","w",.3),.05,1)*(*canvas_w)/b.w,clamp(getnum("plugin_map","h",.3),.05,1)*(*canvas_h)/b.h);
    double small=getnum("plugin_map","small_room",0);if(fmax((double)b.w/(*canvas_w),(double)b.h/(*canvas_h))<small)return;
    double w=b.w*scale,h=b.h*scale;
    double x=clamp(getnum("plugin_map","px",.5)*(*canvas_w)-w/2,0,*canvas_w-w);
    double y=clamp(getnum("plugin_map","py",.5)*(*canvas_h)-h/2,0,*canvas_h-h);
    map_shape(x,y,w,h,color("plugin_map","bg","0"),0);
    for(int i=0;i<nobjects;++i){struct WorldObject *o=objects[i];struct Properties *p=&o->props;uint32_t c=object_color(o,cw);if(!c)continue;
        map_shape(x+(p->xmp/256.0-b.x)*scale,y+(p->ymp/256.0-b.y)*scale,p->wmp/256.0*scale,p->hmp/256.0*scale,c,p->shapetype);
    }
    struct STDVector v=cw->activeHitboxes.hitboxes;int n=vector_count(v,sizeof(struct ActiveHitbox));struct ActiveHitbox *hb=v.start;
    for(int i=0;i<n;++i)if(hb[i].damage>0){struct Rectangle r=hb[i].rectmp;map_shape(x+(r.x/256.0-b.x)*scale,y+(r.y/256.0-b.y)*scale,r.w/256.0*scale,r.h/256.0*scale,color("plugin_map","enemy","f00"),0);}
}
/* Native libc++ string access. Reads are checked with Mach before use. */
static int read_memory(const void *src,void *dst,size_t n){
    if(!src)return 0;
#if defined(SBPE_TEST) && !defined(SBPE_CHECKED_READ_TEST)
    memcpy(dst,src,n);return 1;
#else
    mach_vm_size_t copied=0;
    return mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)src,n,(mach_vm_address_t)dst,&copied)==KERN_SUCCESS && copied==n;
#endif
}
static void read_string(const struct STDString *str,char *out,size_t capacity){
    if(!capacity)return;out[0]=0;unsigned char p[24];if(!read_memory(str,p,24))return;
    size_t n;const void *source;
    if(!(p[0]&1)&&(p[0]>>1)<=22){n=p[0]>>1;source=p+1;}
    else if(p[0]&1){uint64_t len;uintptr_t ptr;memcpy(&len,p+8,8);memcpy(&ptr,p+16,8);if(len>4096||!len)return;n=(size_t)len;source=(void*)ptr;}
    else return;
    if(n>=capacity)n=capacity-1;
    if(source==p+1){memcpy(out,source,n);out[n]=0;}
    else if(read_memory(source,out,n))out[n]=0;else out[0]=0;
}
static void short_string(const struct STDString *str,char out[24]){read_string(str,out,24);}

static int in_list(const char *word,const char *list){size_t n=strlen(word);if(!n)return 0;const char *p=list;while(*p){while(*p==' ')++p;const char *end=strchr(p,' ');size_t len=end?(size_t)(end-p):strlen(p);if(len==n&&!memcmp(p,word,n))return 1;if(!end)break;p=end+1;}return 0;}
static const char *target_kind(const char *v){
    if(in_list(v,"cave.sd city.sd dumb.fw forest.sspot forest.sdopen hulk.sd2 jungle.sd2 lab.sd2"))return "secret";
    if(in_list(v,"dumb.secretspot dumb.secretspoton hulk.sspot hulk.sdopen jungle.sspot jungle.sdopen lab.sspot lab.sspotdone"))return "shop";
    if(in_list(v,"cave.nextDoor cave.bossgate city.nextDoor city.bossgate dumb.nextDoor dumb.bossgate forest.nextdoor forest.unextdoor forest.bossgate hulk.door3 hulk.prevdoor hulk.hportalbase ice.nextDoor ice.bossgate jungle.door jungle.bossgate lab.nextdoor lab.bossgate sc.door"))return "door";
    if(in_list(v,"forest.miner dumb.steletop hulk.sg lab.sg"))return "arcade";
    if(in_list(v,"loot-health1 loot-xmas"))return "hp";return NULL;
}
#include "stat_drops.h"
static double monotime(void);
static double arrow_num(const char *kind,const char *option,double fallback){
    const char *s="plugin_extra_info";char key[64];
    snprintf(key,sizeof(key),"arrows_%s",option);double common=getnum(s,key,fallback);
    snprintf(key,sizeof(key),"arrow_%s_%s",kind,option);return getnum(s,key,common);
}
/* Arrow dimensions are Windows device pixels, independent of zoom and Retina.
 * World positions/fade distances stay in world units; only drawing uses a
 * temporary drawable-pixel canvas. */
static void arrow_frame(double x,double y,double w,double h,uint32_t c,double width){
    if(!c||width<=0||w<=0||h<=0)return;
    int ix=(int)lround(x),iy=(int)lround(y),iw=(int)lround(w),ih=(int)lround(h);
    int lw=(int)fmax(1,lround(clamp(width,0,4096)));
    if(iw<=0||ih<=0)return;
    if(lw*2>iw||lw*2>ih){fill_rect(ix,iy,iw,ih,c,BLENDMODE_ADD);return;}
    fill_rect(ix,iy,iw-lw,lw,c,BLENDMODE_ADD);
    fill_rect(ix+lw,iy+ih-lw,iw-lw,lw,c,BLENDMODE_ADD);
    fill_rect(ix,iy+lw,lw,ih-lw,c,BLENDMODE_ADD);
    fill_rect(ix+iw-lw,iy,lw,ih-lw,c,BLENDMODE_ADD);
}
/* Windows extra_info draws frames around invisible, nonzero trigger zones.
 * This includes the wall activation areas reported by TL/ETL. Positions stay
 * in world units; frame thickness uses the same drawable pixels as arrows. */
_Static_assert(offsetof(struct Properties,trigger)==0x54,"Mac trigger property");
_Static_assert(offsetof(struct Properties,vid)==0x88,"Mac visual ID property");
static void draw_triggers(struct ClientWorld *cw,struct WorldView *wv){
    const char *s="plugin_extra_info";
    if(paused||!extra_enabled||!cw||!wv||!getbool(s,"show_triggers",1)||*canvas_w<1||*canvas_h<1)return;
    uint32_t c=color(s,"trigger_color","80ff00ff");double frame=clamp(getnum(s,"trigger_frame",3),0,128);
    if(!c||frame<=0)return;
    int world_w=*canvas_w,world_h=*canvas_h;
    int screen_w=physical_w>0?(int)lround(physical_w*render_pixel_ratio):world_w;
    int screen_h=physical_h>0?(int)lround(physical_h*render_pixel_ratio):world_h;
    if(screen_w<1||screen_h<1)return;
    double sx=(double)screen_w/world_w,sy=(double)screen_h/world_h;
    for(int i=0;i<nobjects;++i){
        struct Properties *p=&objects[i]->props;char vid[24];
        if(!p->trigger||p->wmp<=0||p->hmp<=0)continue;
        short_string(p->vid,vid);if(vid[0])continue;
        double x=floor(p->xmp/256.0)-wv->offset.x,y=floor(p->ymp/256.0)-wv->offset.y;
        double w=floor(p->wmp/256.0),h=floor(p->hmp/256.0);
        if(w<=0||h<=0||x+w<0||y+h<0||x>world_w||y>world_h)continue;
        *canvas_w=screen_w;*canvas_h=screen_h;
        arrow_frame(x*sx,y*sy,w*sx,h*sy,c,frame);
        *canvas_w=world_w;*canvas_h=world_h;
    }
}
static void arrow_triangle(double x,double y,double len,double half,double out,double angle,uint32_t c){
    double co=cos(angle),si=sin(angle);
    if(out>0){
        const double quarter_pi=0.78539816339744830962;
        double ox=out/tan(quarter_pi-atan2(len,half)/2),oy=out/tan(quarter_pi-atan2(half,len)/2);
        fill_tri((int)lround(x+ox*co),(int)lround(y+ox*si),
                 (int)lround(x+(-len-out)*co-(half+oy)*si),(int)lround(y+(-len-out)*si+(half+oy)*co),
                 (int)lround(x+(-len-out)*co+(half+oy)*si),(int)lround(y+(-len-out)*si-(half+oy)*co),c&0xff000000,BLENDMODE_BLEND);
    }
    fill_tri((int)lround(x),(int)lround(y),
             (int)lround(x-len*co-half*si),(int)lround(y-len*si+half*co),
             (int)lround(x-len*co+half*si),(int)lround(y-len*si-half*co),c,BLENDMODE_BLEND);
}
static void draw_arrows(struct ClientWorld *cw,struct WorldView *wv){
    if(!extra_enabled||!cw||!wv||*canvas_w<=0||*canvas_h<=0)return;
    struct WorldObject *player=(struct WorldObject*)cw->player;
    int world_w=*canvas_w,world_h=*canvas_h;
    double screen_w=physical_w>0?physical_w*render_pixel_ratio:world_w;
    double screen_h=physical_h>0?physical_h*render_pixel_ratio:world_h;
    double sx=screen_w/world_w,sy=screen_h/world_h,now=monotime();
    for(int i=0;i<nobjects;++i){
        struct Properties *p=&objects[i]->props;char vid[24];short_string(p->vid,vid);const char *kind=target_kind(vid);
        if(!kind&&needed_stat_drop(cw,vid))kind="boost";
        if(!kind&&objects[i]!=player&&(object_source(objects[i])&4)&&!object_hidden(cw,objects[i]))kind="player";
        if(!kind)continue;
        if(!in_list(kind,getoptv("plugin_extra_info","arrows","door shop secret arcade hp")))continue;
        if(!strcmp(kind,"hp")&&(!player||player->props.hitpoints>=player->props.maxhitpoints))continue;
        char key[64];snprintf(key,sizeof(key),"arrow_%s_color",kind);
        uint32_t c=color("plugin_extra_info",key,getoptv("plugin_extra_info","arrows_color","fff"));
        double x=floor(p->xmp/256.0)-wv->offset.x,y=floor(p->ymp/256.0)-wv->offset.y;
        double w=floor(p->wmp/256.0),h=floor(p->hmp/256.0);
        int inbounds=x+w>=0&&x<=world_w&&y+h>=0&&y<=world_h;
        double frame=arrow_num(kind,"frame",5);if(inbounds&&frame<=0)continue;
        double x0=world_w/2.0,y0=world_h/2.0;
        if(inbounds&&player){x0=floor((player->props.xmp+player->props.wmp/2.0)/256.0)-wv->offset.x;y0=floor((player->props.ymp+player->props.hmp/2.0)/256.0)-wv->offset.y;}
        double x1=x+floor(p->wmp/512.0),y1=y+floor(p->hmp/512.0),dx=x1-x0,dy=y1-y0,t=1;
        if(x1<0&&dx!=0)t=fmin(t,-x0/dx);
        if(y1<0&&dy!=0)t=fmin(t,-y0/dy);
        if(x1>world_w&&dx!=0)t=fmin(t,(world_w-x0)/dx);
        if(y1>world_h&&dy!=0)t=fmin(t,(world_h-y0)/dy);
        double ax=x0+t*dx,ay=y0+t*dy,fade=arrow_num(kind,"fade",300),f=1;
        if(fade>0)f=fmin(1,hypot(ax-x0,ay-y0)/fade);
        double blink=arrow_num(kind,"blink",0);
        if(blink>0)f*=fabs(fmod(now,blink)-blink/2)*2/blink;
        c=(c&0xffffff)|((uint32_t)floor((c>>24)*clamp(f,0,1))<<24);
        *canvas_w=(int)lround(screen_w);*canvas_h=(int)lround(screen_h);
        if(inbounds)arrow_frame(x*sx,y*sy,w*sx,h*sy,c,frame);
        else{
            double len=clamp(arrow_num(kind,"length",20),0,4096),half=clamp(arrow_num(kind,"width",10),0,4096)/2;
            double out=clamp(arrow_num(kind,"outline",2),0,128);
            if(len>0&&half>0)arrow_triangle(ax*sx,ay*sy,len,half,out,atan2(dy,dx),c);
        }
        *canvas_w=world_w;*canvas_h=world_h;
    }
}
struct TextCache {unsigned texture;char value[256],font[40];int size,w,h,outline;uint32_t color,outline_color;};static struct TextCache texts[MAX_TEXT];static int text_slot;
/* SDL_ttf may reject empty/invalid UTF-8. XDL's upload path does not
 * tolerate a failed surface. Sanitize names before calling that path. */
static struct TextCache *cache_text_raster(const char *value,int pixel_size,uint32_t c,const char *font){
    char safe[256];int k=0;
    for(int i=0;value&&value[i]&&k<255;){
        unsigned char ch=value[i];int n=ch<128?1:(ch>=0xc2&&ch<=0xdf?2:(ch>=0xe0&&ch<=0xef?3:(ch>=0xf0&&ch<=0xf4?4:0)));
        int valid=n>0;
        for(int j=1;j<n;++j)if(!value[i+j]||((unsigned char)value[i+j]&0xc0)!=0x80){valid=0;break;}
        if(valid&&n>=3){unsigned char b=value[i+1];if((ch==0xe0&&b<0xa0)||(ch==0xed&&b>=0xa0)||(ch==0xf0&&b<0x90)||(ch==0xf4&&b>=0x90))valid=0;}
        if(!valid){safe[k++]='?';++i;continue;}
        if(k+n>255)break;
        if(ch>=32){memcpy(safe+k,value+i,n);k+=n;}i+=n;
    }
    safe[k]=0;if(!k||text_slot>=MAX_TEXT)return NULL;value=safe;struct TextCache *t=&texts[text_slot++];
    /* Default to the Windows plugin colors; optional user brightness keeps alpha. */
    double brightness=clamp(getnum("native","font_brightness",1),0,1);
    uint32_t shaded=c&0xff000000;
    for(int shift=0;shift<=16;shift+=8)shaded|=(uint32_t)lround(((c>>shift)&255)*brightness)<<shift;
    c=shaded;
    int sz=(int)clamp(pixel_size,1,320);
    if(!t->texture||strcmp(t->value,value)||t->size!=sz||t->color!=c||strcmp(t->font,font)||t->outline!=text_outline||t->outline_color!=text_outline_color){t->color=c;copystr(t->font,sizeof(t->font),font);t->outline=text_outline;t->outline_color=text_outline_color;t->texture=create_text(t->texture,font,sz,c,value,text_outline,text_outline_color);t->size=sz;copystr(t->value,sizeof(t->value),value);if(t->texture)query_texture(t->texture,&t->w,&t->h);else t->w=t->h=0;}
    return t;
}
static struct TextCache *cache_text(const char *value,int font_size,uint32_t c,const char *font){
    double scale=physical_w>0?(double)*canvas_w/physical_w:1;
    return cache_text_raster(value,(int)clamp(lround(font_size*scale),8,320),c,font);
}
static void draw_cached_text(struct TextCache *t,int x,int y,double ax,double ay){
    if(!t)return;
    /* Match original util.PlainText: zero RGB tint, full alpha.
       All-white RGB overrides the glyphs and produces solid rectangles. */
    int dw=(int)lround(t->w/render_pixel_ratio),dh=(int)lround(t->h/render_pixel_ratio);
    if(t->texture)draw_texture(t->texture,0,0,t->w,t->h,x-(int)lround(dw*ax),y-(int)lround(dh*ay),dw,dh,0,0,0,0,text_tint,BLENDMODE_BLEND);
}
static void text_style(const char *value,int x,int y,int font_size,uint32_t c,const char *font,double ax,double ay){draw_cached_text(cache_text(value,font_size,c,font),x,y,ax,ay);}
static void text_anchor(const char *v,int x,int y,int size,uint32_t c,int middle){text_style(v,x,y,size,c,"TenbyFive",.5,middle?.5:1);}
static void text_at(const char *value,int x,int y,int size,uint32_t c){text_anchor(value,x,y,size,c,0);}
static void world_text(const char *v,double x,double y,int size,uint32_t c,const char *font,double ax,double ay){
    int w=*canvas_w,h=*canvas_h;
    if(physical_w>0&&physical_h>0){x*=physical_w/(double)w;y*=physical_h/(double)h;*canvas_w=physical_w;*canvas_h=physical_h;}
    text_style(v,(int)lround(x),(int)lround(y),size,c,font,ax,ay);*canvas_w=w;*canvas_h=h;
}
static int hp_kind(struct WorldObject *o,struct ClientWorld *cw){
    if(o==(struct WorldObject*)cw->player)return 1;
    struct STDVector v=cw->allies;int n=vector_count(v,sizeof(void*));struct WorldObject **a=v.start;
    for(int i=0;i<n;++i)if(a[i]==o)return 2;
    if(o->props.ownertype==OWNERTYPE_ENEMY)return 3;
    return o->props.hitpoints>0||o->props.hitpoints< -1||o->props.maxhitpoints>0||o->props.armor>0?4:0;
}
static void draw_hp(struct ClientWorld *cw,struct WorldView *wv){
    if(!hp_visible||!cw||!wv)return;
    const char *s="plugin_advanced_hp";int advanced=*getoptv(s,"show_adv_hp","")!=0;
    int world_w=*canvas_w,world_h=*canvas_h;
    for(int i=0;i<nobjects;++i){struct WorldObject *obj=objects[i];struct Properties *p=&obj->props;
        int kind=hp_kind(obj,cw);if((!kind&&!advanced)||object_hidden(cw,obj))continue;
        if(advanced){
            unsigned source=object_source(obj);
            int selected=((source&1)&&getbool(s,"enemy",1))||((source&2)&&getbool(s,"object",0))||((source&4)&&getbool(s,"ally",0));
            if(!selected)continue;
        }
        int show_hp=p->hitpoints!= -1&&(p->hitpoints!=0||p->maxhitpoints>0);
        if(advanced){char num[24];snprintf(num,sizeof(num),"%d",p->hitpoints);show_hp=!in_list(num,getoptv(s,"excluded_numbers","-1"))&&getbool(s,p->hitpoints<0?"show_negative":"show_positive",1);}
        if(!show_hp&&p->armor<=0)continue;
        double x=(p->xmp+p->wmp/2.0)/256-wv->offset.x,y=(p->ymp+p->hmp/2.0)/256-wv->offset.y;
        if(x<0||x>*canvas_w||y<0||y>*canvas_h)continue;
        /* Convert the shell center to window coordinates for crisp fixed-size text. */
        if(physical_w>0&&physical_h>0){x*=physical_w/(double)world_w;y*=physical_h/(double)world_h;*canvas_w=physical_w;*canvas_h=physical_h;}
        char label[80];
        if(show_hp){
            if(p->hitpoints>=0&&getbool("native","hp_show_max",0)&&p->maxhitpoints>=p->hitpoints)snprintf(label,sizeof(label),"%d/%d",p->hitpoints,p->maxhitpoints);
            else if(p->hitpoints<0)snprintf(label,sizeof(label),"%lld",-(long long)p->hitpoints);
            else snprintf(label,sizeof(label),"%d",p->hitpoints);
            /* Windows uses magnitude/color for negative HP, and anchors HP
             * above the center with positive armor immediately underneath. */
            text_style(label,(int)lround(x),(int)lround(y),advanced?(int)clamp(getnum(s,p->hitpoints<0?"negative_size":"positive_size",16),1,160):(p->hitpoints<0?25:20),color(s,p->hitpoints<0?"negative_color":"positive_color",p->hitpoints<0?"8080ff":"ffff80"),"HemiHeadBold",.5,1);
        }
        if(p->armor>0&&getbool(s,"show_armor",1)){snprintf(label,sizeof(label),"%d",p->armor);text_style(label,(int)lround(x),(int)lround(y),advanced?(int)clamp(getnum(s,"positive_size",16),1,160):20,color(s,"positive_color","ffff80"),"HemiHeadBold",.5,0);}
        *canvas_w=world_w;*canvas_h=world_h;
    }
}
static uint32_t hsv_mix(uint32_t,uint32_t,double);
#include "native_plugins.h"
#include "custom_hud.h"
#include "gameplay_parity.h"
#include "chest_display.h"
#include "dps.h"
#include "retina.h"
#include "clarity.h"
#include "screen_ui.h"
#include "controls_ui.h"
#include "efc_capture.h"
#include "loading_probe.h"
#include "reconnect_lab.h"
#include "network_health.h"
#include "network_stall.h"
#include "multibox.h"
#include "stonebow_lab.h"
#include "inventory_swap.h"
static void install_mouse_wrappers(void){
    uintptr_t *move_slot=(void*)PTR(0x1002177e8),*button_slot=(void*)PTR(0x1002177f0);
    if(*move_slot==PTR(0x100075b30))*move_slot=(uintptr_t)mouse_move;
    if(*button_slot==PTR(0x100075b50))*button_slot=(uintptr_t)mouse_button;
}
#include "minimal_background.h"
static void stage_draw(struct Stage *st,int dt){
#ifndef SBPE_TEST
    install_mouse_wrappers();
#endif
    retina_update();keyboard();cursor_update();fixed_window_update();zoom_update(st);struct WorldClient *wc=world_client();struct ClientWorld *cw=wc?wc->clientWorld:NULL;struct WorldView *wv=wc?wc->worldView:NULL;
    loading_probe_frame();reconnect_lab_frame();network_health_frame(wc);network_stall_frame();bow_frame();
    efc_frame(cw,wv,dt);
    if(!paused){zone_autoconfig(cw);sc_timer_update(st);}
    screen_names_drawn=0;
    /* User does not want debug collision/bounding-box lines. */
    if(game_hitboxes)*game_hitboxes=0;
    if(game_boxes)*game_boxes=0;
    center_camera(wv,paused?0:center_mode);collect_objects(paused?NULL:cw);
    probe_camera_begin(wv);
    struct HudSnapshot hud={0};
    if(!paused)hud_snapshot(cw,&hud);
    multibox_frame(st,cw,&hud);
    if(!paused){history_update(cw);hide_objects(cw);hide_green_squids(cw);gameplay_prepare(cw,wv);prepare_labels(cw);}
    struct UIElement *chat=NULL;int chat_show=0;
    if(!paused&&hide_chat){
        struct STDVector children=st->asUIElementContainer.children;
        if(vector_count(children,sizeof(void*))){struct GameClient *gc=((struct GameClient**)children.start)[0];
            if(!strcmp(class_name(gc),"GameClient")&&gc->chatWindow){chat=(struct UIElement*)gc->chatWindow;chat_show=chat->show;chat->show=0;}
        }
    }
    struct UIElement *old_status=NULL;bool status_show=0;
    if(!paused&&custom_hud_visible&&cw&&cw->player)old_status=hide_builtin_hud(wc,&status_show);
    text_slot=0;
    struct MenuState menu={0};if(!paused)menu_prepare(st,&menu);
    struct VisualState visuals={0};if(!paused)visual_prepare(st,&visuals);
    struct MinimalBackgroundState minimal={0};
    if(!paused)minimal_background_prepare(st,wv,&minimal);
    efc_begin_draw();
    original_draw(st,dt);
    efc_end_draw();
    if(!paused)minimal_background_restore(&minimal);
    if(menu.version)menu.version->asUIElement.show=menu.visible;
    if(!paused)visual_restore(st,&visuals);
    if(!screen_ui_hook&&!paused&&cw&&wv)clarity_frame();
    if(old_status)old_status->show=status_show;
    if(chat)chat->show=(bool)chat_show;
    restore_objects();restore_labels();
    /* Native draw can retire local objects (notably an opened SC chest).
     * Post-draw overlays must use the current lists, not cached addresses
     * collected before that native lifetime boundary. */
    collect_objects(paused?NULL:cw);
    if(!paused)dps_update(cw,wv);
    if(!paused){inventory_name_hide(wc);draw_triggers(cw,wv);draw_arrows(cw,wv);draw_hp(cw,wv);draw_uses(cw,wv);portal_timers(cw,wv);squid_overlay(cw,wv);engineer_overlay(cw,wv);chest_overlay(cw,wv);if(!screen_names_drawn){draw_readable_names(cw,wv);highlight_players(cw,wv);}
        /* HUD remains readable in window coordinates regardless of world zoom. */
        int saved_w=*canvas_w,saved_h=*canvas_h;
        if(physical_w>0&&physical_h>0){*canvas_w=physical_w;*canvas_h=physical_h;}
        simple_playerlist(cw);draw_custom_hud(&hud,wv,saved_w,saved_h);mini_hud(cw,&hud,wv,saved_w,saved_h);misc_overlays(cw,&hud);menu_overlay(&menu);dps_overlay(cw,wv);fps_overlay(cw);
        *canvas_w=saved_w;*canvas_h=saved_h;draw_map(cw,wv);
    }
    efc_overlay();
    loading_probe_overlay();
    bow_overlay();
    reconnect_lab_overlay();
    if(!paused){multibox_connections_overlay();multibox_overlay();}
    probe_camera_end();
    /* Notice is also confirmation the drawing-table hook is running. */
    if(!initialized){LOG("First frame reached: Windows gameplay parity build active; V zoom, R hide players, hold C history.");initialized=1;}
}
#ifndef SBPE_TEST
#include "network_guard.h"
static int uuid_matches(void){
    const struct mach_header_64 *h=(const struct mach_header_64*)_dyld_get_image_header(0);
    static const uint8_t expected[16]={0x34,0xf7,0x1c,0x89,0x23,0x66,0x38,0xe1,0x92,0xa2,0x9c,0x40,0x0e,0x28,0x04,0x5a};
    if(!h||h->magic!=MH_MAGIC_64||h->cputype!=CPU_TYPE_X86_64)return 0;
    const uint8_t *p=(const uint8_t*)(h+1);
    for(uint32_t i=0;i<h->ncmds;++i){const struct load_command *lc=(const struct load_command*)p;
        if(lc->cmdsize<sizeof(*lc))return 0;
        if(lc->cmd==LC_UUID)return !memcmp(((const struct uuid_command*)p)->uuid,expected,16);
        p+=lc->cmdsize;
    }return 0;
}
__attribute__((constructor)) static void start(void){
    const char *root=getenv("SBPE_ROOT");if(!root)return;
    char path[4096];snprintf(path,sizeof(path),"%s/sbpe-mac.log",root);logfile=fopen(path,"w");
    LOG("Mac SBPE Windows gameplay parity (based on beta 0.19; x86_64 process)");
    if(!uuid_matches()){LOG("Unsupported game UUID/architecture: mod left disabled.");return;}
    slide=_dyld_get_image_vmaddr_slide(0);
    chest_class=(void*)PTR(0x1001e5380); /* Chest vtable address point: symbol +16. */
    stage_ptr=(void*)PTR(0x1001ff418);canvas_w=(void*)PTR(0x1001ff7c4);canvas_h=(void*)PTR(0x1002177d0);window_ptr=(void*)PTR(0x1001ff710);
    get_window_size=dlsym(RTLD_DEFAULT,"SDL_GetWindowSize");get_keys=dlsym(RTLD_DEFAULT,"SDL_GetKeyboardState");key_from_name=dlsym(RTLD_DEFAULT,"SDL_GetScancodeFromName");get_keyboard_focus=dlsym(RTLD_DEFAULT,"SDL_GetKeyboardFocus");
    scancode_from_key=dlsym(RTLD_DEFAULT,"SDL_GetScancodeFromKey");scancode_name=dlsym(RTLD_DEFAULT,"SDL_GetScancodeName");
    if(!get_window_size||!get_keys||!key_from_name){LOG("SDL entry points unavailable: mod disabled.");return;}
    fill_rect=(void*)PTR(0x1000fc5d0);fill_tri=(void*)PTR(0x1000fca50);draw_rect=(void*)PTR(0x1000fc390);
    create_text=(void*)PTR(0x1000fe920);query_texture=(void*)PTR(0x1000ff7c0);draw_texture=(void*)PTR(0x1000fe170);game_hitboxes=(void*)PTR(0x1002180c3);
    game_boxes=(void*)PTR(0x1002180c2);
    visual_flags=(void*)PTR(0x1001f7bfa);aura_flag=(void*)PTR(0x1002180cc);server_hostname=(void*)PTR(0x1002180a8);
    show_cursor=dlsym(RTLD_DEFAULT,"SDL_ShowCursor");play_sound=(void*)PTR(0x100100a20);
    window_flags=dlsym(RTLD_DEFAULT,"SDL_GetWindowFlags");window_fullscreen=dlsym(RTLD_DEFAULT,"SDL_SetWindowFullscreen");window_position=dlsym(RTLD_DEFAULT,"SDL_GetWindowPosition");set_window_position=dlsym(RTLD_DEFAULT,"SDL_SetWindowPosition");set_window_size=dlsym(RTLD_DEFAULT,"SDL_SetWindowSize");
    snprintf(config_path,sizeof(config_path),"%s/config.ini",root);if(!load_config())return;
    install_network_guard();
    install_loading_probe();
    install_inventory_swap();
    install_stonebow_lab();
    install_efc_capture();
    install_retina();
    install_clarity();
    uintptr_t *anim_slot=(void*)PTR(0x1001ee1c0);
    if(*anim_slot==PTR(0x1000d9fd0)){original_animation_draw=(void*)*anim_slot;*anim_slot=(uintptr_t)animation_draw;LOG("Animation effects suppression installed.");}
    else LOG("Animation drawing slot mismatch: effect suppression disabled.");
    original_tex_parameteri=dlsym(RTLD_DEFAULT,"glTexParameteri");
    uintptr_t *sampling_slot=(void*)PTR(0x1001e2f58);
    if(original_tex_parameteri&&(*sampling_slot==PTR(0x10019049c)||*sampling_slot==(uintptr_t)original_tex_parameteri)){
        *sampling_slot=(uintptr_t)sharp_tex_parameteri;
        LOG("Sharp sampling hook installed: GL_NEAREST min/mag; debug boxes disabled.");
    }else LOG("Sampling import did not match; leaving game filtering unchanged.");
    /* Gameplay draw hooks use the supported client's writable __DATA slots.
       The network guard separately validates and installs its receive patches. */
    uintptr_t *draw_slot=(void*)PTR(0x1001ea168),*resize_slot=(void*)PTR(0x1001ea1b0);
    uintptr_t *move_slot=(void*)PTR(0x1002177e8),*button_slot=(void*)PTR(0x1002177f0);
    if(*draw_slot!=PTR(0x100078210)||*resize_slot!=PTR(0x1000760f0)) {LOG("Drawing-table validation failed: mod disabled.");return;}
    original_draw=(void*)*draw_slot;original_resize=(void*)*resize_slot;container_resize=(void*)PTR(0x100078500);
    original_move=(void*)PTR(0x100075b30);original_button=(void*)PTR(0x100075b50);
    /* Callbacks are assigned later by game startup; install our wrappers via
       exported XDL setter interception is unnecessary: use Stage mouse slots.
       Game callback slots are checked/replaced on the first rendered frame. */
    (void)move_slot;(void)button_slot;
    *resize_slot=(uintptr_t)resize_stage;*draw_slot=(uintptr_t)stage_draw;
    uintptr_t *key_slot=(void*)PTR(0x1001ea180);
    if(*key_slot==PTR(0x100076060)&&scancode_from_key&&scancode_name){original_stage_keyboard=(void*)*key_slot;*key_slot=(uintptr_t)stage_keyboard;event_input_enabled=1;LOG("Native keyboard event hook installed; startup focus polling bypassed.");}
    install_controls_ui();
    install_screen_ui();
    install_dps();
    LOG("UUID and draw slots verified. Native Stage hooks installed.");
}
#endif
