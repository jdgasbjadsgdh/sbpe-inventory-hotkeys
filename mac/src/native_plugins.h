/* Native translations of the supplied portal and squid overlays.
 * Reference logic: map_timers2.py and squid_prediction.py by Alexrns.
 * These use scalar protobuf properties; Windows string writes are not used.
 */
static double monotime(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int in_zone(struct ClientWorld *cw,const char *want){
    if(!cw)return 0;char zone[24];short_string(cw->asWorld.props.zone,zone);if(!zone[0])short_string(cw->asWorld.props.music,zone);return !strcmp(zone,want);
}
struct PortalTimer {uint32_t id;int lifetime;double observed;unsigned frame;};
static struct PortalTimer portals[256];static unsigned portal_frame;
static void portal_timers(struct ClientWorld *cw,struct WorldView *wv){
    ++portal_frame;if(!cw||!wv||!in_zone(cw,"lobby")){memset(portals,0,sizeof(portals));return;}
    double now=monotime();const char *section="plugin_map_timers2";
    for(int i=0;i<nobjects;++i){struct WorldObject *obj=objects[i];struct Properties *p=&obj->props;struct InteractDescription *d=p->interactdescription;
        if(!d||d->type!=3||d->mk<0||d->numused<=0)continue;
        int lifetime=p->lifetime/1000;if(lifetime<0)continue;
        int slot=-1,free_slot=-1;
        for(int j=0;j<256;++j){if(portals[j].id==obj->objId&&portals[j].observed){slot=j;break;}if(!portals[j].observed&&free_slot<0)free_slot=j;}
        if(slot<0){slot=free_slot;if(slot<0)continue;portals[slot]=(struct PortalTimer){obj->objId,lifetime,now,portal_frame};}
        struct PortalTimer *pt=&portals[slot];if(pt->lifetime!=lifetime){pt->lifetime=lifetime;pt->observed=now;}pt->frame=portal_frame;
        int count=lifetime-(int)(now-pt->observed);
        if((count>=0&&!getbool(section,"show_count_down",1))||(count<0&&!getbool(section,"show_count_up",0)))continue;
        double x=p->xmp/256.0-wv->offset.x,y=p->ymp/256.0-wv->offset.y,w=p->wmp/256.0,h=p->hmp/256.0;
        if(x+w<0||y+h<0||x>*canvas_w||y>*canvas_h)continue;
        x+=w/2*(getnum(section,"anchor_x_count_down",0)+1)+getnum(section,"offset_x_count_down",0);
        y+=h/2*(getnum(section,"anchor_y_count_down",-1)+1)+getnum(section,"offset_y_count_down",0);
        char label[40];snprintf(label,sizeof(label),"%d",abs(count));world_text(label,x,y,(int)getnum(section,"size_count_down",16),color(section,"color_count_down","fff"),getoptv(section,"font_count_down","TenbyFive"),.5,.5);
    }
    for(int j=0;j<256;++j)if(portals[j].observed&&portals[j].frame!=portal_frame)memset(&portals[j],0,sizeof(portals[j]));
}
static void hide_green_squids(struct ClientWorld *cw){
    if(!cw||!in_zone(cw,"sc_boss")||!getbool("plugin_squid_prediction","enable",1)||!getbool("plugin_squid_prediction","hide_green_squids",1))return;
    for(int i=0;i<nobjects&&nhidden<LIMIT;++i){struct WorldObject *o=objects[i];char vid[24];short_string(o->props.vid,vid);
        if(strcmp(vid,"scboss.summonsb"))continue;int found=0;for(int j=0;j<nhidden;++j)if(hidden[j].obj==o)found=1;
        if(!found&&o->props.xmp>=INT32_MIN+(1<<29)){int x=o->props.xmp,nx=x-(1<<29);hidden[nhidden++]=(struct Hidden){o,x,nx};o->props.xmp=nx;}
    }
}
static uint32_t squid_ids[1024];static int squid_total,squid_alive;
static int squid_zone;
static void squid_overlay(struct ClientWorld *cw,struct WorldView *wv){
    const char *s="plugin_squid_prediction";
    if(!cw||!wv||!in_zone(cw,"sc_boss")){squid_total=0;squid_zone=0;return;}
    if(!getbool(s,"enable",1))return;
    squid_zone=1;squid_alive=0;
    for(int i=0;i<nobjects;++i){struct WorldObject *o=objects[i];struct Properties *p=&o->props;char vid[24];short_string(p->vid,vid);
        if(!strcmp(vid,"scboss.summons")){++squid_alive;int found=0;for(int j=0;j<squid_total;++j)if(squid_ids[j]==o->objId)found=1;if(!found&&squid_total<1024)squid_ids[squid_total++]=o->objId;}
        if(strcmp(vid,"scboss.summonsb")||!p->move||!p->move->to)continue;
        struct Linear *m=p->move->to;if(m->dt<0)continue;
        double x=m->xmp/256.0-wv->offset.x,y=m->ymp/256.0-wv->offset.y,w=p->wmp/256.0,h=p->hmp/256.0;
        if(x+w<0||y+h<0||x>*canvas_w||y>*canvas_h)continue;
        uint32_t c=color(s,"box_default_color","ff7f00");
        if(m->dt>500&&getbool(s,"color_fade",1)){
            c=hsv_mix(color(s,"box_fade_start_color","ffff00"),color(s,"box_fade_end_color","ff0000"),(double)m->pos/m->dt);
        }
        c=(c&0xffffff)|((uint32_t)lround(clamp(getnum(s,"box_transparency",.6),0,1)*255)<<24);
        /* Match the Windows predictor: alpha-blended interior, with a
         * separately blended border outside it. BLENDMODE_NONE ignores alpha. */
        double bx=x,by=y,bw=w,bh=h;
        if(getbool(s,"box_outline_show",1)){
            uint32_t oc=color(s,"box_outline_color","f00");oc=(oc&0xffffff)|((uint32_t)lround(clamp(getnum(s,"box_outline_transparency",1),0,1)*255)<<24);
            int lw=(int)fmax(2,lround(clamp(getnum(s,"box_outline_thickness",2),0,128)*(*canvas_w)/(physical_w?physical_w:1)));
            if(lw*2>w||lw*2>h){shape_blend(x,y,w,h,oc,0,BLENDMODE_BLEND);bw=bh=0;}
            else{
                fill_rect((int)x,(int)y,(int)w-lw,lw,oc,BLENDMODE_BLEND);
                fill_rect((int)x+lw,(int)y+(int)h-lw,(int)w-lw,lw,oc,BLENDMODE_BLEND);
                fill_rect((int)x,(int)y+lw,lw,(int)h-lw,oc,BLENDMODE_BLEND);
                fill_rect((int)x+(int)w-lw,(int)y,lw,(int)h-lw,oc,BLENDMODE_BLEND);
                bx+=lw;by+=lw;bw-=lw*2;bh-=lw*2;
            }
        }
        if(getbool(s,"box_show",1)&&bw>0&&bh>0)shape_blend(bx,by,bw,bh,c,0,BLENDMODE_BLEND);
        if(getbool(s,"number_show",1)){int old=text_outline;uint32_t oldcolor=text_outline_color;text_outline=(int)clamp(getnum(s,"number_outline_size",2),0,16);text_outline_color=color(s,"number_outline_color","00");char label[40];snprintf(label,sizeof(label),"%d",(m->dt-m->pos)/10);world_text(label,x+w/2,y+h/2,(int)getnum(s,"number_size",16),color(s,"number_color","ffc300"),"HemiHeadBold",.5,.5);text_outline=old;text_outline_color=oldcolor;}
    }
    double unit=physical_w>0?(double)*canvas_w/physical_w:1;
    int x=(int)(clamp(getnum(s,"squid_count_x",400),-100000,100000)*unit),y=(int)(clamp(getnum(s,"squid_count_y",200),-100000,100000)*unit);char text[40];
    if(getbool(s,"squid_count_alive_show",1)){snprintf(text,sizeof(text),"%d",squid_alive);world_text(text,x,y,(int)getnum(s,"squid_count_alive_size",16),0xffffffff,"HemiHeadBold",0,0);}
    if(getbool(s,"squid_count_total_show",1)){snprintf(text,sizeof(text),"%d",squid_total);world_text(text,x,y+(int)((getnum(s,"squid_count_alive_size",16)+8)*unit),(int)getnum(s,"squid_count_total_size",12),0xffffffff,"HemiHeadBold",0,0);}
}
struct SavedLabel {struct WorldObject *obj;uint32_t bits;int hp;struct PlayerData *original_pd;struct PlayerData pd;struct STDString empty_faction;int swapped;};
static struct SavedLabel saved_labels[LIMIT];static int nsaved_labels;
static void prepare_labels(struct ClientWorld *cw){
    nsaved_labels=0;if(!cw)return;
    struct STDVector v=cw->allies;int n=vector_count(v,sizeof(void*));struct WorldObject **allies=v.start;
    for(int i=0;i<n&&nsaved_labels<LIMIT;++i){struct WorldObject *o=allies[i];if(!o)continue;
        struct PlayerIdentity id;unsigned primary=0,parts=player_identity(o,&id)?highlight_parts(&id,&primary):0;
        int readable=readable_names_enabled();
        int faction_hide=hide_names||(parts&2)||getbool("plugin_noise","hide_factions",0);
        if(hide_names||hide_healthbars||parts||faction_hide||readable){
            struct SavedLabel *saved=&saved_labels[nsaved_labels++];memset(saved,0,sizeof(*saved));saved->obj=o;saved->bits=o->props._has_bits[0];saved->hp=o->props.hitpoints;saved->original_pd=o->props.playerdata;
            if(hide_names||(parts&1)||readable)o->props._has_bits[0]&=~0x300000U;
            if(hide_healthbars)o->props.hitpoints=o->props.maxhitpoints;
            /* Swap a checked, draw-only PlayerData copy; never alter a libc++ string. */
            if(faction_hide&&read_memory(saved->original_pd,&saved->pd,sizeof(saved->pd))){saved->pd.factionname=&saved->empty_faction;saved->swapped=1;o->props.playerdata=&saved->pd;}
        }
    }
}
static void restore_labels(void){
    for(int i=0;i<nsaved_labels;++i){struct SavedLabel *saved=&saved_labels[i];saved->obj->props._has_bits[0]=saved->bits;saved->obj->props.hitpoints=saved->hp;if(saved->swapped&&saved->obj->props.playerdata==&saved->pd)saved->obj->props.playerdata=saved->original_pd;}
    nsaved_labels=0;
}
static struct TextCache *highlight_texture(const char *label,const char *prefix){
    if(!prefix)return NULL;const char *s="plugin_highlight_player3";char key[80];int old=text_outline;
    snprintf(key,sizeof(key),"%soutline_size",prefix);text_outline=(int)clamp(getnum(s,key,2),0,16);
    snprintf(key,sizeof(key),"%ssize",prefix);int size=(int)getnum(s,key,strstr(prefix,"faction")?14:16);
    snprintf(key,sizeof(key),"%scolor",prefix);uint32_t c=color(s,key,strstr(prefix,"faction")?(strstr(prefix,"alternate")?"1c74cb":"0f0"):"2cff80");
    snprintf(key,sizeof(key),"%sfont",prefix);struct TextCache *result=cache_text(label,size,c,getoptv(s,key,"TenbyFive"));text_outline=old;return result;
}
static void highlight_players(struct ClientWorld *cw,struct WorldView *wv){
    if(!cw||!wv||physical_w<=0||physical_h<=0)return;const char *s="plugin_highlight_player3";
    struct STDVector v=cw->allies;int n=vector_count(v,sizeof(void*));struct WorldObject **allies=v.start;
    int world_w=*canvas_w,world_h=*canvas_h;if(world_w<=0||world_h<=0)return;
    double chat_height=0;struct Stage *st=stage_ptr?*stage_ptr:NULL;
    if(!hide_chat&&st&&vector_count(st->asUIElementContainer.children,sizeof(void*))){
        struct GameClient *gc=((struct GameClient**)st->asUIElementContainer.children.start)[0];
        if(!strcmp(class_name(gc),"GameClient")&&gc->chatWindow){
            struct STDVector lines=gc->chatWindow->chatLines;int count=vector_count(lines,sizeof(void*));
            if(count)chat_height=4;
            for(int j=0;j<count;++j){struct UIElement *line=((struct UIElement**)lines.start)[j];if(line)chat_height+=clamp(line->h,0,world_h)+4;}
        }
    }
    for(int i=0;i<n;++i){struct WorldObject *o=allies[i];struct PlayerIdentity id;unsigned primary;
        if(!o||object_hidden(cw,o)||!player_identity(o,&id))continue;unsigned parts=highlight_parts(&id,&primary);if(!parts)continue;
        const char *name=id.name;if(name[0]=='#')name=getoptv("native_spark_labels",name+1,name);
        char label[320];struct TextCache *nt=NULL,*ft=NULL;
        *canvas_w=physical_w;*canvas_h=physical_h;
        if((parts&1)&&*name){snprintf(label,sizeof(label),"[%d] %s",id.level,name);nt=highlight_texture(label,primary&1?"player_":"alternate_player_");}
        if((parts&2)&&id.faction[0]){snprintf(label,sizeof(label),"<%s>",id.faction);ft=highlight_texture(label,primary&2?"faction_":"alternate_faction_");}
        double screen_w=physical_w*render_pixel_ratio,screen_h=physical_h*render_pixel_ratio;
        double x=(o->props.xmp/256.0+o->props.wmp/512.0-wv->offset.x+getnum(s,"x_offset",0))*screen_w/world_w;
        double y=(o->props.ymp/256.0+o->props.hmp/256.0-wv->offset.y+getnum(s,"y_offset",-30))*screen_h/world_h;
        double width=nt?nt->w:0,nh=nt?nt->h:0,fh=ft?ft->h:0;
        if(!getbool(s,"out_of_bounds_faction_name",1))width=fmax(width,ft?ft->w:0);
        if(width>screen_w-6)x=screen_w/2;else x=clamp(x,3+width/2,screen_w-3-width/2);
        y=clamp(y,4,fmax(4,screen_h-3-nh-fh));
        uint32_t old_tint=text_tint;int behavior=(int)getnum(s,"chat_behavior",3);
        double chatw=340*screen_w/world_w,chath=chat_height*screen_h/world_h;
        if(behavior>0&&behavior<4&&chath>0&&x-width/2<chatw&&y+nh+fh>screen_h-chath){
            if(behavior==2||(behavior==3&&(y+nh+fh>=screen_h-3||x-width/2<=3))){
                if(y>-(chath/chatw)*x+screen_h)x=chatw+width/2;
                else y=screen_h-3-nh-fh-chath;
            }else text_tint=(uint32_t)(clamp(getnum(s,"chat_alpha",.5),0,1)*255)<<24;
        }
        draw_cached_text(nt,(int)lround(x/render_pixel_ratio),(int)lround(y/render_pixel_ratio),.5,0);
        draw_cached_text(ft,(int)lround(x/render_pixel_ratio),(int)lround((y+nh)/render_pixel_ratio),.5,0);
        text_tint=old_tint;
        *canvas_w=world_w;*canvas_h=world_h;
    }
}
#define HISTORY_LIMIT 2048
struct PlayerRow {char account[256],name[256],faction[256],shell[12],previous_shell[12];int spark,died;uint64_t seen;};
static struct PlayerRow player_rows[HISTORY_LIMIT];static int nplayer_rows,history_was_safe;static uint64_t history_frame;
static void history_update(struct ClientWorld *cw){
    if(!cw)return;
    if(cw->asWorld.props.safe||history_was_safe)nplayer_rows=0;
    history_was_safe=cw->asWorld.props.safe;++history_frame;
    struct STDVector v=cw->allies;int n=vector_count(v,sizeof(void*));struct WorldObject **a=v.start;
    for(int i=0;i<n;++i){
        struct WorldObject *obj=NULL;struct Properties p;struct PlayerData pd;char account[256];
        if(!read_memory(a+i,&obj,sizeof(obj))||!obj||!read_memory(&obj->props,&p,sizeof(p))||!read_memory(p.playerdata,&pd,sizeof(pd)))continue;
        read_string(pd.accountid,account,sizeof(account));if(!account[0])continue;
        int index=-1;for(int j=0;j<nplayer_rows;++j)if(!strcmp(player_rows[j].account,account)){index=j;break;}
        if(index<0){
            if(nplayer_rows<HISTORY_LIMIT)index=nplayer_rows++;
            else{index=0;for(int j=1;j<HISTORY_LIMIT;++j)if(player_rows[j].seen<player_rows[index].seen)index=j;}
            memset(&player_rows[index],0,sizeof(player_rows[index]));copystr(player_rows[index].account,sizeof(player_rows[index].account),account);
        }
        struct PlayerRow *r=&player_rows[index];char vid[24];
        read_string(p.displayname,r->name,sizeof(r->name));if(!r->name[0])snprintf(r->name,sizeof(r->name),"#%.6s",account);
        read_string(pd.factionname,r->faction,sizeof(r->faction));r->seen=history_frame;
        short_string(p.vid,vid);const char *abbr="?";int minimum=0;r->spark=0;
        if(!strcmp(vid,"soldier")){abbr="W";minimum=80;}else if(!strcmp(vid,"assassin")){abbr="D";minimum=50;}
        else if(!strcmp(vid,"heavy")){abbr="I";minimum=100;}else if(!strcmp(vid,"fabricator")){abbr="F";minimum=60;}
        else if(!strcmp(vid,"toaster"))abbr="T";else if(!strcmp(vid,"spark")){abbr="*";r->spark=1;}
        if(minimum&&p.maxhitpoints>0){int mk=(int)fmin(1+floor((p.maxhitpoints-minimum)/10.0),6);if(mk==6&&p.maxhitpoints%5)mk=5;snprintf(r->shell,sizeof(r->shell),"%s%d",abbr,mk);copystr(r->previous_shell,sizeof(r->previous_shell),r->shell);}
        else copystr(r->shell,sizeof(r->shell),abbr);
        if(r->spark&&r->previous_shell[0])r->died=1;
    }
}
static int row_compare(const void *aa,const void *bb){
    const struct PlayerRow *a=*(const struct PlayerRow*const*)aa,*b=*(const struct PlayerRow*const*)bb;
    if(a->seen!=b->seen)return a->seen>b->seen?-1:1;
    int c=strcasecmp(a->faction,b->faction);if(c)return c;c=strcasecmp(a->name,b->name);return c?c:(a>b)-(a<b);
}
static int row_group(const struct PlayerRow *r){return r->seen==history_frame?r->spark:2;}
static void simple_playerlist(struct ClientWorld *cw){
    (void)cw;if(!playerlist_visible)return;
    static struct PlayerRow *sorted[HISTORY_LIMIT];for(int i=0;i<nplayer_rows;++i)sorted[i]=&player_rows[i];
    qsort(sorted,nplayer_rows,sizeof(sorted[0]),row_compare);
    int size=(int)clamp(getnum("plugin_playerlist","size",14),8,48),y=(int)lround(10/render_pixel_ratio),x=*canvas_w/2;
    for(int group=0;group<3;++group){int count=0;for(int i=0;i<nplayer_rows;++i)if(row_group(sorted[i])==group)++count;if(!count)continue;
        char heading[40];snprintf(heading,sizeof(heading),"%d %s",count,group==0?"shells":group==1?"sparks":"seen before");
        text_style(heading,x-(int)lround(5/render_pixel_ratio),y,size,0xffffffff,"TenbyFive",1,0);int step=(int)fmax(1,lround(size/render_pixel_ratio));y+=step;
        for(int i=0;i<nplayer_rows;++i){struct PlayerRow *r=sorted[i];if(row_group(r)!=group)continue;if(y>*canvas_h)return;
            char shell[16];if(r->died&&r->spark)snprintf(shell,sizeof(shell),"(%s)",r->previous_shell);else copystr(shell,sizeof(shell),r->shell);
            text_style(shell,x-(int)lround(5/render_pixel_ratio),y,size,0xffffffff,"TenbyFive",1,0);
            text_style(r->name,x,y,size,0xffffffff,"TenbyFive",0,0);
            text_style(r->faction,x+(int)lround(100/render_pixel_ratio),y,size,0xffffffff,"TenbyFive",0,0);y+=step;
        }
    }
}
static void draw_uses(struct ClientWorld *cw,struct WorldView *wv){
    if(!cw||!wv||!getbool("plugin_extra_info","show_uses",1))return;
    int world_w=*canvas_w,world_h=*canvas_h;
    for(int i=0;i<nobjects;++i){struct Properties *p=&objects[i]->props;struct InteractDescription d;
        if(!p->interact||!read_memory(p->interactdescription,&d,sizeof(d))||d.totaluses<=0||d.numused<0)continue;
        if(d.type==INTERACTTYPE_GATE&&(d.mk<0||d.numused==0))continue;
        double x=(p->xmp+p->wmp/2.0)/256-wv->offset.x,y=(p->ymp+p->hmp/2.0)/256-wv->offset.y;
        if(x<0||x>world_w||y<0||y>world_h)continue;
        char label[32];snprintf(label,sizeof(label),"%d",d.numused);
        if(physical_w>0&&physical_h>0){x*=physical_w/(double)world_w;y*=physical_h/(double)world_h;*canvas_w=physical_w;*canvas_h=physical_h;}
        text_style(label,(int)lround(x),(int)lround(y),*getoptv("plugin_advanced_hp","show_adv_hp","")?16:20,0xffffff80,"HemiHeadBold",.5,.5);*canvas_w=world_w;*canvas_h=world_h;
    }
}
static void fps_overlay(struct ClientWorld *cw){
    static double previous,last_update,intervals[8192],fps;static unsigned head,count;static int old_res;
    double now=monotime();if(previous){intervals[head]=fmax(0,now-previous);head=(head+1)%8192;if(count<8192)++count;}previous=now;
    if(now-last_update>=clamp(getnum("plugin_fps","update",.5),.01,60)){
        double total=0,window=clamp(getnum("plugin_fps","average",1),.01,60);unsigned frames=0;
        while(frames<count&&total<window){total+=intervals[(head+8191-frames)%8192];++frames;}
        if(total>0)fps=frames/total;last_update=now;
    }
    char label[80];snprintf(label,sizeof(label),"%.1f",fps);int slot=text_slot;
    text_style(label,*canvas_w-(int)lround(4/render_pixel_ratio),0,(int)getnum("plugin_fps","size",16),color("plugin_fps","color","ff0"),"HemiHeadBold",1,0);
    int h=text_slot>slot?(int)lround(texts[slot].h/render_pixel_ratio):16;
    if(cw){snprintf(label,sizeof(label),"r:%d",cw->timeReserve);uint32_t c=cw->timeReserve<900&&old_res>=cw->timeReserve?color("plugin_fps","res_warning","f00"):color("plugin_fps","res_color","fff");
        text_style(label,*canvas_w-(int)lround(4/render_pixel_ratio),h,(int)getnum("plugin_fps","res_size",12),c,"HemiHeadBold",1,0);old_res=cw->timeReserve;
    }else old_res=0;
}
