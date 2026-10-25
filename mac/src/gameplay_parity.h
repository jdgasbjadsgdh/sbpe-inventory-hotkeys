/* Gameplay translations from the supplied Windows plugins. No packet,
 * capture, automation, or research tools. Mac scalar offsets are verified
 * by tests/verify_binary.py against the exact supported executable. */
_Static_assert(offsetof(struct WorldView,shakeMagnitude)==0x144,"Mac shake magnitude");
_Static_assert(offsetof(struct WorldView,flashColor)==0x160,"Mac flash color");
_Static_assert(offsetof(struct ChatWindow,chatLines)==0x70,"Mac chat lines");
_Static_assert(offsetof(struct WorldObject,lastDamageT)==0x118,"Mac damage flash timestamp");
_Static_assert(offsetof(struct World,props)==0x150,"Mac world properties");
_Static_assert(offsetof(struct ItemProperties,statboosts)==0x10,"Mac item modifier field");
_Static_assert(offsetof(struct MainMenu,version)==0x110,"Mac menu version label");
_Static_assert(offsetof(struct GameClient,sinceKeypress)==0xd4,"Mac idle timer");
static char selected_server[160];
struct MenuState {struct Label *version;bool visible;char text[128];};
static void menu_prepare(struct Stage *st,struct MenuState *saved){
    struct STDVector v=st->asUIElementContainer.children;if(!vector_count(v,sizeof(void*)))return;void *first=((void**)v.start)[0];
    if(!strcmp(class_name(first),"GameClient")){if(getbool("plugin_misc","disable_autokick",1))((struct GameClient*)first)->sinceKeypress=0;return;}
    if(strcmp(class_name(first),"MainMenu"))return;struct MainMenu *menu=first;
    if(menu->serverName)read_string(&menu->serverName->value,selected_server,sizeof(selected_server));
    if(menu->version&&getbool("plugin_version","visible",1)){saved->version=menu->version;saved->visible=menu->version->asUIElement.show;read_string(&menu->version->text,saved->text,sizeof(saved->text));menu->version->asUIElement.show=0;}
}
static void menu_overlay(const struct MenuState *saved){
    if(!saved->version)return;char label[192];snprintf(label,sizeof(label),"client v%s + SBPE Mac 0.19 gameplay",saved->text);
    uint32_t old=text_tint;text_tint=(uint32_t)((fabs(sin(monotime()*2))*.7+.3)*255)<<24;
    text_style(label,(int)lround(4/render_pixel_ratio),*canvas_h-(int)lround(4/render_pixel_ratio),16,0xffffffff,"HemiHeadBold",0,1);text_tint=old;
}
static void inventory_name_hide(struct WorldClient *wc){
    if(wc&&wc->overlay&&getbool("plugin_name_hide","enable",1)&&!strcmp(class_name(wc->overlay),"InventoryOverlay"))fill_rect(32,215,200,50,0xff000000,BLENDMODE_BLEND);
}
static const char *more_section="plugin_more_misc_b1_11_0";
static uint8_t *visual_flags; /* drawUniform, drawScatter, drawEdge, drawTiles, drawHUD */
static int *aura_flag;
static struct STDString *server_hostname;
static void (*play_sound)(const char*,int,double,double);
static int (*show_cursor)(int);
static unsigned (*window_flags)(void*);
static int (*window_fullscreen)(void*,unsigned);
static void (*window_position)(void*,int*,int*),(*set_window_position)(void*,int,int),(*set_window_size)(void*,int,int);
static void fixed_window_update(void){
    if(paused||!getbool("plugin_misc","fixed_window",0)||!window_ptr||!*window_ptr||!window_flags||!window_fullscreen||!window_position||!set_window_position||!set_window_size)return;
    void *window=*window_ptr;int x,y,w,h,tx=(int)getnum("plugin_misc","window_x",0),ty=(int)getnum("plugin_misc","window_y",0);
    int tw=(int)clamp(getnum("plugin_misc","window_width",1920),320,8192),th=(int)clamp(getnum("plugin_misc","window_height",1080),180,8192);
    if(window_flags(window)&1)window_fullscreen(window,0);
    get_window_size(window,&w,&h);if(w!=tw||h!=th)set_window_size(window,tw,th);
    window_position(window,&x,&y);if(x!=tx||y!=ty)set_window_position(window,tx,ty);
}
static double last_mouse_activity;
static int cursor_hidden;
static double shake_until,flash_until;
static void deposit_cue(struct ClientWorld *cw);

struct HSV {double h,s,v;};
static struct HSV rgb_hsv(uint32_t c){
    double r=((c>>16)&255)/255.0,g=((c>>8)&255)/255.0,b=(c&255)/255.0;
    double hi=fmax(r,fmax(g,b)),lo=fmin(r,fmin(g,b)),d=hi-lo,h=0;
    if(d){if(hi==r)h=fmod((g-b)/d,6);else if(hi==g)h=(b-r)/d+2;else h=(r-g)/d+4;h/=6;if(h<0)h+=1;}
    return (struct HSV){h,hi?d/hi:0,hi};
}
static uint32_t hsv_rgb(struct HSV c){
    double h=(c.h-floor(c.h))*6,f=h-floor(h),p=c.v*(1-c.s),q=c.v*(1-f*c.s),t=c.v*(1-(1-f)*c.s),r,g,b;
    switch((int)h%6){case 0:r=c.v;g=t;b=p;break;case 1:r=q;g=c.v;b=p;break;case 2:r=p;g=c.v;b=t;break;case 3:r=p;g=q;b=c.v;break;case 4:r=t;g=p;b=c.v;break;default:r=c.v;g=p;b=q;}
    return ((uint32_t)(clamp(r,0,1)*255)<<16)|((uint32_t)(clamp(g,0,1)*255)<<8)|(uint32_t)(clamp(b,0,1)*255);
}
static uint32_t hsv_mix(uint32_t from,uint32_t to,double progress){
    struct HSV a=rgb_hsv(from),b=rgb_hsv(to);double f=clamp(progress,0,1);
    return hsv_rgb((struct HSV){a.h+(b.h-a.h)*f,a.s+(b.s-a.s)*f,a.v+(b.v-a.v)*f});
}
static void mouse_activity(void){last_mouse_activity=monotime();if(cursor_hidden&&show_cursor){show_cursor(1);cursor_hidden=0;}}
static void cursor_update(void){
    if(!show_cursor)return;double delay=getnum("plugin_misc","hide_cursor_after",2);
    int focus=!get_keyboard_focus||get_keyboard_focus()==*window_ptr;
    if(paused||!focus||delay<=0){if(cursor_hidden){show_cursor(1);cursor_hidden=0;}last_mouse_activity=monotime();return;}
    if(!last_mouse_activity)last_mouse_activity=monotime();
    if(!cursor_hidden&&monotime()-last_mouse_activity>=delay){show_cursor(0);cursor_hidden=1;}
}
static void gameplay_prepare(struct ClientWorld *cw,struct WorldView *wv){
    if(!cw||!wv)return;
    deposit_cue(cw);
    if(getbool("plugin_noise","replace_shake",1)){
        double now=monotime();
        if(wv->shakeMagnitude>0&&wv->shakePos<wv->shakeDuration)shake_until=fmax(shake_until,now+(wv->shakeDuration-wv->shakePos)/1000.0);
        int64_t duration=(int64_t)wv->flashStart+wv->flashHold+wv->flashEnd;
        if(wv->flashColor&&wv->flashPos<duration)flash_until=fmax(flash_until,now+clamp((duration-wv->flashPos)/1000.0,0,60));
        wv->shakeMagnitude=0;wv->flashColor=0;
    }
    int reduce=(int)lround((1-clamp(getnum("plugin_noise","damage_flash_intensity",1),0,1))*132);
    int swt=cw->serverSubWorld?cw->serverSubWorld->asSubWorldImpl.asSubWorld.t:0;
    if(reduce&&swt>132)for(int i=0;i<nobjects;++i)if(object_source(objects[i])&1){
        int64_t dt=(int64_t)swt-objects[i]->lastDamageT;
        if(dt>=0&&dt<reduce)objects[i]->lastDamageT=swt-reduce;
    }
    if(getbool(more_section,"hide_extras",1))for(int i=0;i<nobjects&&nhidden<LIMIT;++i){
        struct WorldObject *o=objects[i];char v[24];short_string(o->props.vid,v);
        if(strcmp(v,"city.hbshot"))continue;
        int found=0;for(int j=0;j<nhidden;++j)if(hidden[j].obj==o){found=1;break;}
        if(!found&&o->props.xmp>=INT32_MIN+(1<<29)){int x=o->props.xmp,nx=x-(1<<29);hidden[nhidden++]=(struct Hidden){o,x,nx};o->props.xmp=nx;}
    }
}
struct VisualState {uint8_t flags[5];uint32_t background;int aura;};
static void visual_prepare(struct Stage *st,struct VisualState *saved){
    static const char *keys[]={"drawUniform","drawScatter","drawEdge","drawTiles","drawHUD"};
    saved->background=st->backgroundColor;
    if(visual_flags)for(int i=0;i<5;++i){saved->flags[i]=visual_flags[i];visual_flags[i]=getbool("plugin_misc",keys[i],1);}
    if(!getbool(more_section,"draw_bg",1)){
        uint32_t c=color(more_section,"base_color","000");struct HSV h=rgb_hsv(c);double speed=clamp(getnum(more_section,"change_speed",0),0,20);
        if(speed>0){if(h.s==0)h.s=.65;if(h.v==0)h.v=.7;h.h+=monotime()*speed/100;}
        st->backgroundColor=speed>0?hsv_rgb(h):(c&0xffffff);if(visual_flags)visual_flags[0]=visual_flags[1]=0;
    }else{struct HSV h=rgb_hsv(st->backgroundColor);double max=clamp(getnum("plugin_misc","max_bg_value",1),0,1);if(h.v>max){h.v=max;st->backgroundColor=hsv_rgb(h);}}
    if(aura_flag){saved->aura=*aura_flag;*aura_flag=getbool(more_section,"show_aura",1)?0:1;}
}
static void visual_restore(struct Stage *st,const struct VisualState *saved){
    if(visual_flags)memcpy(visual_flags,saved->flags,5);if(aura_flag)*aura_flag=saved->aura;st->backgroundColor=saved->background;
}
static void gameplay_frame_box(double x,double y,double w,double h,const char *section){
    if(w<=0||h<=0)return;
    uint32_t c=color(section,"box_color","fff");c=(c&0xffffff)|((uint32_t)(clamp(getnum(section,"box_transparency",.6),0,1)*255)<<24);
    int lw=(int)fmax(2,lround(clamp(getnum(section,"box_outline_thickness",2),0,128)*(*canvas_w)/(physical_w?physical_w:1)));
    if(getbool(section,"box_outline_show",0)){
        uint32_t oc=color(section,"box_outline_color","fff");oc=(oc&0xffffff)|((uint32_t)(clamp(getnum(section,"box_outline_transparency",1),0,1)*255)<<24);
        if(lw*2>w||lw*2>h){shape_blend(x,y,w,h,oc,0,BLENDMODE_BLEND);return;}
        fill_rect((int)x,(int)y,(int)w-lw,lw,oc,BLENDMODE_BLEND);fill_rect((int)x+lw,(int)(y+h)-lw,(int)w-lw,lw,oc,BLENDMODE_BLEND);
        fill_rect((int)x,(int)y+lw,lw,(int)h-lw,oc,BLENDMODE_BLEND);fill_rect((int)(x+w)-lw,(int)y,lw,(int)h-lw,oc,BLENDMODE_BLEND);
        x+=lw;y+=lw;w-=2*lw;h-=2*lw;
    }
    if(getbool(section,"box_show",1))shape_blend(x,y,w,h,c,0,BLENDMODE_BLEND);
}
static void engineer_overlay(struct ClientWorld *cw,struct WorldView *wv){
    if(!cw||!wv)return;const char *s="plugin_gy_highlight";
    for(int i=0;i<nobjects;++i){struct Properties *p=&objects[i]->props;char vid[48];read_string(p->vid,vid,sizeof(vid));
        if(!((!strcmp(vid,"hulk.scavengerengineer")&&getbool(s,"highlight_engineers",1))||(!strcmp(vid,"hulk.pathlightarmed")&&getbool(s,"highlight_pathlights",0))))continue;
        double x=floor(p->xmp/256.0)-wv->offset.x,y=floor(p->ymp/256.0)-wv->offset.y,w=floor(p->wmp/256.0),h=floor(p->hmp/256.0);
        if(x+w<=0||y+h<=0||x>=*canvas_w||y>=*canvas_h)continue;gameplay_frame_box(x,y,w,h,s);
    }
}
static void room_label(struct ClientWorld *cw){
    if(!cw||!getbool("plugin_extra_info","show_room_id",1))return;char zone[120],label[160];
    read_string(cw->asWorld.props.zone,zone,sizeof(zone));if(!zone[0])read_string(cw->asWorld.props.music,zone,sizeof(zone));
    if(cw->asWorld.props.floor>0)snprintf(label,sizeof(label),"%s %lld",zone,(long long)cw->asWorld.props.floor+1);else copystr(label,sizeof(label),zone);
    text_style(label,*canvas_w-(int)lround(4/render_pixel_ratio),*canvas_h-(int)lround(4/render_pixel_ratio),16,0xffffffff,"HemiHeadBold",1,1);
}
static void mini_pair(const char *value,const char *maximum,double x,double y,const char *key,uint32_t c){
    char opt[48];int old=text_outline;
    snprintf(opt,sizeof(opt),"outline_%s",key);text_outline=(int)clamp(getnum(more_section,opt,2),0,16);
    snprintf(opt,sizeof(opt),"size_%s",key);int size=(int)clamp(getnum(more_section,opt,!strcmp(key,"hp")?18:14),1,160);
    text_style(value,(int)lround(x),(int)lround(y),size,c,"HemiHeadBold",1,0);
    if(maximum){snprintf(opt,sizeof(opt),"outline_max%s",key);text_outline=(int)clamp(getnum(more_section,opt,2),0,16);snprintf(opt,sizeof(opt),"size_max%s",key);size=(int)clamp(getnum(more_section,opt,14),1,160);text_style(maximum,(int)lround(x-2/render_pixel_ratio),(int)lround(y),size,c,"HemiHeadBold",0,0);}
    text_outline=old;
}
static void spin_bar(const struct HudSnapshot *hud,struct WorldView *wv,int ww,int wh){
    if(!getbool(more_section,"show_spin_bar",1)||strcmp(class_name(hud->player),"Duskwing"))return;
    struct AnimationProperties anim;
    if(!hud->player->props.animprops||!read_memory(hud->player->props.animprops,&anim,sizeof(anim)))return;
    if(anim.animid!=14&&anim.animid!=19&&anim.animid!=24&&anim.animid!=29)return;
    double duration=getnum(more_section,"spin_duration",800);if(duration<=0)return;
    double f=clamp(fabs(anim.pos-duration)/duration,0,1);int side=(int)getnum(more_section,"spin_bar_side",3);
    int width=(int)clamp(getnum(more_section,"spin_bar_width",90),1,4096),height=(int)clamp(getnum(more_section,"spin_bar_height",4),1,4096),by=(int)getnum(more_section,"spin_bar_y",10);
    struct Properties *p=&hud->player->props;double sx=physical_w*render_pixel_ratio/(double)ww,sy=physical_h*render_pixel_ratio/(double)wh;
    double left=(p->xmp/256.0-wv->offset.x)*sx,top=(p->ymp/256.0-wv->offset.y)*sy,rw=p->wmp/256.0*sx,rh=p->hmp/256.0*sy;
    int bx,yy,bw=width,bh=height;if(side==1||side==3){bw=height;bh=width;bx=(int)lround(side==1?left+rw+by:left-bw-by);yy=(int)lround(top+rh/2-bh/2.0);}else{bx=(int)lround(left+rw/2-bw/2.0);yy=(int)lround(side==2?top+rh+by:top-bh-by);}
    int sw=*canvas_w,sh=*canvas_h;*canvas_w=(int)lround(physical_w*render_pixel_ratio);*canvas_h=(int)lround(physical_h*render_pixel_ratio);
    fill_rect(bx-1,yy-1,bw+2,bh+2,color(more_section,"spin_bar_outline","808080"),BLENDMODE_BLEND);
    fill_rect(bx,yy,bw,bh,color(more_section,f<=getnum(more_section,"warn_percent",30)/100?"spin_bar_background_warn":"spin_bar_background",f<=getnum(more_section,"warn_percent",30)/100?"ff8080":"000"),BLENDMODE_BLEND);
    int fw=(side==1||side==3)?bw:(int)lround(bw*f),fh=(side==1||side==3)?(int)lround(bh*f):bh;
    if(fw&&fh)fill_rect(bx,(side==1||side==3)?yy+bh-fh:yy,fw,fh,color(more_section,"spin_bar_color","ffff80"),BLENDMODE_BLEND);
    *canvas_w=sw;*canvas_h=sh;
}
static void mini_hud(struct ClientWorld *cw,const struct HudSnapshot *hud,struct WorldView *wv,int ww,int wh){
    if(!hud->valid||!wv||ww<=0||wh<=0||!getbool(more_section,"show_mini_hud",1))return;
    struct Properties *p=&hud->player->props;double x=(p->xmp/256.0+p->wmp/512.0-wv->offset.x)*physical_w/ww,y=(p->ymp/256.0-wv->offset.y)*physical_h/wh;
    char a[32],b[64];
    if(getbool(more_section,"show_mini_hp",1)){snprintf(a,sizeof(a),"%d",hud->hp);snprintf(b,sizeof(b),"/%d",hud->maxhp);mini_pair(a,b,x-getnum(more_section,"x_hp",30)/render_pixel_ratio,y-getnum(more_section,"y_hp",35)/render_pixel_ratio,"hp",color(more_section,hud->hp==hud->maxhp?"color_hp_full":"color_hp",hud->hp==hud->maxhp?"0f0":"fff"));}
    if(getbool(more_section,"show_mini_ammo",1)){snprintf(a,sizeof(a),"%d",hud->ammo);if(hud->mult>1)snprintf(b,sizeof(b),"/%d (x%d)",hud->maxammo,hud->mult);else snprintf(b,sizeof(b),"/%d",hud->maxammo);mini_pair(a,b,x-getnum(more_section,"x_ammo",-35)/render_pixel_ratio,y-getnum(more_section,"y_ammo",35)/render_pixel_ratio,"ammo",color(more_section,hud->ammo==hud->maxammo?"color_ammo_full":"color_ammo","ffff80"));}
    if(getbool(more_section,"show_spam_timer",1)&&(in_zone(cw,"lobby")||in_zone(cw,"home"))){int timer;if(read_memory((char*)hud->player+0x400,&timer,sizeof(timer))&&timer>=0&&timer<=12000){snprintf(a,sizeof(a),"%.1f",timer/1000.0);mini_pair(a,NULL,x-getnum(more_section,"x_spam",-48)/render_pixel_ratio,y-getnum(more_section,"y_spam",50)/render_pixel_ratio,"spam",color(more_section,"color_spam","ffff80"));}}
    spin_bar(hud,wv,ww,wh);
}
static void misc_text(const char *v,const char *key,int defx,int defy,int defsize){
    char opt[48];int old=text_outline;snprintf(opt,sizeof(opt),"outline_%s",key);text_outline=(int)clamp(getnum(more_section,opt,3),0,16);
    snprintf(opt,sizeof(opt),"x_%s",key);int x=(int)lround(getnum(more_section,opt,defx)/render_pixel_ratio);snprintf(opt,sizeof(opt),"y_%s",key);int y=(int)lround(getnum(more_section,opt,defy)/render_pixel_ratio);
    snprintf(opt,sizeof(opt),"size_%s",key);int size=(int)clamp(getnum(more_section,opt,defsize),1,160);snprintf(opt,sizeof(opt),"color_%s",key);uint32_t c=color(more_section,opt,"f0f0f0");snprintf(opt,sizeof(opt),"font_%s",key);
    text_style(v,x,y,size,c,getoptv(more_section,opt,"HemiHeadBold"),.5,0);text_outline=old;
}
static int ec_started,ec_start,ec_entry,ec_home;static char ec_zone[64];
static void ec_counter(struct ClientWorld *cw,const struct HudSnapshot *hud){
    if(!cw||!hud->valid)return;int home=in_zone(cw,"lobby")||in_zone(cw,"home");char zone[64];read_string(cw->asWorld.props.zone,zone,sizeof(zone));if(!zone[0])read_string(cw->asWorld.props.music,zone,sizeof(zone));
    if(!ec_started){ec_started=1;ec_start=ec_entry=hud->ec;ec_home=1;copystr(ec_zone,sizeof(ec_zone),zone);}
    if(home)ec_home=1;else if(ec_home){ec_home=0;ec_entry=hud->ec;copystr(ec_zone,sizeof(ec_zone),zone);}
    if(!getbool(more_section,"show_ec_counter",1))return;
    char label[160];snprintf(label,sizeof(label),"%d EC last %s",hud->ec-ec_entry,ec_zone);misc_text(label,"ec",120,165,12);
    snprintf(label,sizeof(label),"%d EC Total",hud->ec-ec_start);
    int old=text_outline;text_outline=(int)clamp(getnum(more_section,"outline_ec",3),0,16);
    struct TextCache *t=cache_text(label,(int)clamp(getnum(more_section,"size_ec",12),1,160),color(more_section,"color_ec","f0f0f0"),getoptv(more_section,"font_ec","HemiHeadBold"));
    if(t)draw_cached_text(t,(int)lround(getnum(more_section,"x_ec",120)/render_pixel_ratio),(int)lround((getnum(more_section,"y_ec",165)+t->h)/render_pixel_ratio),.5,0);text_outline=old;
}
static double sc_opened_at;static time_t sc_wall_time;static const struct ChatLogEntry *last_sc_entry;static int last_sc_time;
static void deposit_cue(struct ClientWorld *cw){
    static int was_complete;int complete=0;
    if(in_zone(cw,"lobby"))for(int i=0;i<nobjects;++i){char vid[24];short_string(objects[i]->props.vid,vid);if(!strcmp(vid,"lobby.keydropdone"))++complete;}
    if(complete==8&&!was_complete&&(!sc_opened_at||monotime()-sc_opened_at>1290)&&play_sound&&getbool(more_section,"play_sc_audio_cue",1))play_sound("ui.buttonclick",0,0,0);
    was_complete=complete==8;
}
static void sc_timer_update(struct Stage *st){
    struct STDVector children=st->asUIElementContainer.children;if(!vector_count(children,sizeof(void*)))return;struct GameClient *gc=((struct GameClient**)children.start)[0];
    if(strcmp(class_name(gc),"GameClient")||!gc->chatLog)return;
    struct STDVector v=gc->chatLog->entries;int n=vector_count(v,sizeof(void*));if(!n)return;
    struct ChatLogEntry entry,*ptr=((struct ChatLogEntry**)v.start)[n-1];if(!read_memory(ptr,&entry,sizeof(entry)))return;
    if(ptr==last_sc_entry&&entry.receiveTime==last_sc_time)return;last_sc_entry=ptr;last_sc_time=entry.receiveTime;
    char message[160];read_string(entry.chat.message,message,sizeof(message));
    if(entry.chat.chattype==4&&!strcmp(message,"A portal to the Star Cathedral has opened!")){
        sc_opened_at=monotime();sc_wall_time=time(NULL);
        if(play_sound&&getbool(more_section,"play_sc_audio_cue",1))play_sound("ui.buttonclick",0,0,0);
    }
}
static void misc_overlays(struct ClientWorld *cw,const struct HudSnapshot *hud){
    room_label(cw);ec_counter(cw,hud);
    if(cw&&getbool(more_section,"show_server",1)){char host[160],label[180];if(selected_server[0])copystr(host,sizeof(host),selected_server);else read_string(server_hostname,host,sizeof(host));if(host[0]){snprintf(label,sizeof(label),"Server: %s",host);misc_text(label,"server",120,200,12);}}
    if(getbool("plugin_noise","replace_shake",1)){double now=monotime();const char *v=shake_until>now?(flash_until>now?"[SHAKE, FLASH]":"[SHAKE]"):(flash_until>now?"[FLASH]":"");text_style(v,*canvas_w/2,(int)lround(20/render_pixel_ratio),30,0xffffff00,"HemiHeadBold",.5,0);}
    if(sc_opened_at&&getbool(more_section,"show_sc_time",1)&&monotime()-sc_opened_at<getnum(more_section,"sc_close_timer_duration",20)){
        time_t close=sc_wall_time+1200;struct tm local;localtime_r(&close,&local);char label[80];snprintf(label,sizeof(label),"closing: XX:%02d:%02d",local.tm_min,local.tm_sec);
        text_style(label,(int)lround(400/render_pixel_ratio),(int)lround(200/render_pixel_ratio),24,0xffffffff,"HemiHeadBold",0,0);
    }
}
static void zone_autoconfig(struct ClientWorld *cw){
    static uint32_t game,zoneid;static int floor,valid;static char lastzone[64];
    if(!cw){valid=0;return;}char zone[64];read_string(cw->asWorld.props.zone,zone,sizeof(zone));if(!zone[0])read_string(cw->asWorld.props.music,zone,sizeof(zone));
    if(valid&&game==cw->asWorld.props.gameid&&zoneid==cw->asWorld.props.zoneid&&floor==cw->asWorld.props.floor&&!strcmp(lastzone,zone))return;
    valid=1;game=cw->asWorld.props.gameid;zoneid=cw->asWorld.props.zoneid;floor=cw->asWorld.props.floor;copystr(lastzone,sizeof(lastzone),zone);
    int count=noptions;for(int i=0;i<count;++i)if(!strcmp(options[i].section,more_section)){
        char plugin[40],option[80],want[64],extra;int fields=sscanf(options[i].key,"%39s %79s %63s %c",plugin,option,want,&extra);if(fields!=3||strcmp(want,zone))continue;
        struct Binding b={0};copystr(b.plugin,sizeof(b.plugin),plugin);copystr(b.option,sizeof(b.option),option);char section[64];snprintf(section,sizeof(section),"plugin_%s",plugin);
        if(option_target(plugin,option)||*getoptv(section,option,"")){char value[512];copystr(value,sizeof(value),options[i].value);binding_set(&b,value);}
    }
}
