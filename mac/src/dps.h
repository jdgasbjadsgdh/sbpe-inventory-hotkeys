/* Native ports of dps_v3.py and bv1totaldamage.py. Damage types and scalar
 * offsets are verified against HitNumberGraphic's Mac constructor/draw code.
 * Filter at the draw dispatch so a newly-created ally number never flashes.
 * Keep the graphics intact for the native lifetime/update and both counters. */
_Static_assert(offsetof(struct WorldView,dynamicGraphics)==0xe8,"Mac dynamic graphics");
_Static_assert(offsetof(struct HitNumberGraphic,nt)==0x34,"Mac hit number type");
_Static_assert(offsetof(struct HitNumberGraphic,startTime)==0x38,"Mac hit timestamp");
_Static_assert(offsetof(struct HitNumberGraphic,numTexture)==0x50,"Mac number texture pointer");
_Static_assert(offsetof(struct NumberTexture,value)==0,"Mac number texture value");
static void (*original_hit_number_draw)(struct HitNumberGraphic*,int,int);
static void *hit_number_class;
static int dps_kind(int type){return type==2||type==4||type==6?1:(type==3||type==5||type==7?2:0);}
static void hit_number_draw(struct HitNumberGraphic *number,int time,int layer){
    if(!paused&&number&&dps_kind(number->nt)==2&&getbool("plugin_bv1totaldamage","hide_ally_numbers",1))return;
    original_hit_number_draw(number,time,layer);
}
struct DamageIdentity {uintptr_t address;int start,value,type;};
#define DAMAGE_SLOTS (LIMIT*2)
static struct DamageIdentity damage_seen[2][DAMAGE_SLOTS];static int damage_table;
static int64_t self_dps,ally_dps,self_peak,self_total,ally_total;
static struct {struct WorldView *view;uint32_t world,game,zone;int floor;char name[64];} damage_room;
static uint64_t damage_hash(const struct DamageIdentity *id){return ((id->address>>4)*2654435761U)^((uint32_t)id->start*16777619U);}
static int damage_identity_equal(const struct DamageIdentity *a,const struct DamageIdentity *b){return a->address==b->address&&a->start==b->start&&a->value==b->value&&a->type==b->type;}
static int damage_contains(struct DamageIdentity *table,const struct DamageIdentity *id){
    size_t slot=damage_hash(id)&(DAMAGE_SLOTS-1);
    while(table[slot].address){if(damage_identity_equal(&table[slot],id))return 1;slot=(slot+1)&(DAMAGE_SLOTS-1);}return 0;
}
static int damage_insert(struct DamageIdentity *table,const struct DamageIdentity *id){
    size_t slot=damage_hash(id)&(DAMAGE_SLOTS-1);
    while(table[slot].address){if(damage_identity_equal(&table[slot],id))return 0;slot=(slot+1)&(DAMAGE_SLOTS-1);}table[slot]=*id;return 1;
}
static int64_t damage_add(int64_t total,int value){return value>0&&total<=INT64_MAX-value?total+value:total;}
static void dps_reset(void){self_dps=ally_dps=self_peak=self_total=ally_total=0;memset(damage_seen,0,sizeof(damage_seen));memset(&damage_room,0,sizeof(damage_room));damage_table=0;}
static void dps_update(struct ClientWorld *cw,struct WorldView *wv){
    if(!cw||!wv){if(damage_room.view)dps_reset();return;}
    char zone[64];read_string(cw->asWorld.props.zone,zone,sizeof(zone));if(!zone[0])read_string(cw->asWorld.props.music,zone,sizeof(zone));
    if(damage_room.view!=wv||damage_room.world!=cw->asWorld.id||damage_room.game!=cw->asWorld.props.gameid||damage_room.zone!=cw->asWorld.props.zoneid||damage_room.floor!=cw->asWorld.props.floor||strcmp(damage_room.name,zone)){
        dps_reset();damage_room.view=wv;damage_room.world=cw->asWorld.id;damage_room.game=cw->asWorld.props.gameid;damage_room.zone=cw->asWorld.props.zoneid;damage_room.floor=cw->asWorld.props.floor;copystr(damage_room.name,sizeof(damage_room.name),zone);
    }
    struct DamageIdentity *old=damage_seen[damage_table],*next=damage_seen[damage_table^1];memset(next,0,sizeof(damage_seen[0]));
    self_dps=ally_dps=0;struct STDVector v=wv->dynamicGraphics;int count=vector_count(v,sizeof(void*));
    for(int i=0;i<count;++i){
        struct HitNumberGraphic *pointer,hit;struct NumberTexture number;
        if(!read_memory((void**)v.start+i,&pointer,sizeof(pointer))||!pointer||!read_memory(pointer,&hit,sizeof(hit))||!hit_number_class||hit.asDynamicGraphic.asGraphic.classptr!=hit_number_class)continue;
        int kind=dps_kind(hit.nt);if(!kind||!read_memory(hit.numTexture,&number,sizeof(number))||number.value<=0)continue;
        struct DamageIdentity id={(uintptr_t)pointer,hit.startTime,number.value,hit.nt};if(!damage_insert(next,&id))continue;
        if(kind==1)self_dps=damage_add(self_dps,number.value);else ally_dps=damage_add(ally_dps,number.value);
        if(!damage_contains(old,&id)){if(kind==1)self_total=damage_add(self_total,number.value);else ally_total=damage_add(ally_total,number.value);}
    }
    damage_table^=1;if(self_dps>self_peak)self_peak=self_dps;
}
static void dps_label(const char *prefix,int64_t value,int dx,int dy,const char *fallback){
    const char *s="plugin_dps_v3";char key[80],label[64];snprintf(key,sizeof(key),"%s_show",prefix);if(!getbool(s,key,strcmp(prefix,"team_dps")!=0))return;
    snprintf(key,sizeof(key),"%s_x",prefix);int x=*canvas_w-(int)lround(clamp(getnum(s,key,dx),-100000,100000)/render_pixel_ratio);
    snprintf(key,sizeof(key),"%s_y",prefix);int y=*canvas_h-(int)lround(clamp(getnum(s,key,dy),-100000,100000)/render_pixel_ratio);
    snprintf(key,sizeof(key),"%s_size",prefix);int size=(int)clamp(getnum(s,key,20),1,160);snprintf(key,sizeof(key),"%s_color",prefix);uint32_t c=color(s,key,fallback);
    snprintf(label,sizeof(label),"%lld",(long long)value);text_style(label,x,y,size,c,"HemiHeadBold",1,1);
}
static void dps_overlay(struct ClientWorld *cw,struct WorldView *wv){
    if(!cw||!wv)return;
    int64_t total_dps=self_dps<=INT64_MAX-ally_dps?self_dps+ally_dps:INT64_MAX;
    if(getbool("plugin_dps_v3","enabled",1)){dps_label("self_dps",self_dps,50,4,"e6895a");dps_label("ally_dps",ally_dps,120,4,"e6df5a");dps_label("team_dps",total_dps,190,4,"e65a5a");dps_label("self_dps_max",self_peak,50,24,"e65a5a");}
    if(!getbool("plugin_bv1totaldamage","visible",1))return;
    int64_t sum=self_total<=INT64_MAX-ally_total?self_total+ally_total:INT64_MAX;char label[96];int x=*canvas_w-(int)lround(4/render_pixel_ratio);
    snprintf(label,sizeof(label),"%lld (%.1f%%)",(long long)self_total,sum?100.0*self_total/sum:0);text_style(label,x,*canvas_h-(int)lround(140/render_pixel_ratio),20,0xff6ff542,"HemiHeadBold",1,1);
    snprintf(label,sizeof(label),"%lld (%.1f%%)",(long long)ally_total,sum?100.0*ally_total/sum:0);text_style(label,x,*canvas_h-(int)lround(120/render_pixel_ratio),20,0xff42f5dd,"HemiHeadBold",1,1);
    snprintf(label,sizeof(label),"%lld",(long long)sum);text_style(label,x,*canvas_h-(int)lround(100/render_pixel_ratio),20,0xff4278f5,"HemiHeadBold",1,1);
}
#ifndef SBPE_TEST
static void install_dps(void){
    uintptr_t *slot=(void*)PTR(0x1001ee310);
    hit_number_class=(void*)PTR(0x1001ee300);
    if(*slot!=PTR(0x1000dacf0)){LOG("Hit-number drawing slot mismatch: first-frame ally-number filter unavailable.");return;}
    original_hit_number_draw=(void*)*slot;*slot=(uintptr_t)hit_number_draw;
    LOG("DPS/total-damage overlays installed; ally hit numbers filtered before the first draw, with damage data retained.");
}
#endif
