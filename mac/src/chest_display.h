/* Windows sb_chest_display gameplay overlay. Read-only Chest/ItemProperties
 * snapshots; no foreign STL traversal and no writes into game item data. */
#include "loot_data.h"
#define CHEST_ITEMS 256
#define CHEST_BOOSTS 8
struct ChestItem {int type,nboost,amount,perf;char name[192],variant[48],boosts[CHEST_BOOSTS][128],mod_names[CHEST_BOOSTS][64];};
static void *chest_class;
struct ChestState {struct Properties props;struct RepeatedPtrField items;int pos;};
_Static_assert(offsetof(struct WorldObject,props)+sizeof(struct Properties)<=0x1b8,"Chest property snapshot bounds");
static int chest_state(struct WorldObject *obj,struct ChestState *state){
    /* All reads cross a checked-copy boundary. Never follow RTTI through a
     * possibly retired object: the supported game UUID pins this vtable. */
    unsigned char raw[0x1ec];void *kind;
    if(!chest_class||!read_memory(obj,&kind,sizeof(kind))||kind!=chest_class||!read_memory(obj,raw,sizeof(raw)))return 0;
    memcpy(&kind,raw,sizeof(kind));if(kind!=chest_class)return 0;
    memcpy(&state->props,raw+offsetof(struct WorldObject,props),sizeof(state->props));
    memcpy(&state->items,raw+0x1b8,sizeof(state->items));
    memcpy(&state->pos,raw+0x1e8,sizeof(state->pos));
    return state->pos==-1;
}
static int chest_rule_matches(const struct ChestRule *rules,const struct ChestItem *item){
    for(int r=0;rules[r].name;++r){const struct ChestRule *rule=&rules[r];if(strcmp(rule->name,item->name))continue;
        if(rule->wildcard)return r+1;if(rule->count!=item->nboost)continue;
        unsigned used=0;int count=0;for(int i=0;i<rule->count;++i)for(int j=0;j<item->nboost;++j)if(!(used&(1U<<j))&&!strcmp(rule->mods[i],item->mod_names[j])){used|=1U<<j;++count;break;}
        if(count==rule->count)return r+1;
    }return 0;
}
static const struct LootName *loot_name(int id){
    for(size_t i=0;i<sizeof(loot_names)/sizeof(*loot_names);++i)if(loot_names[i].id==id)return &loot_names[i];return NULL;
}
static int repeated_count(struct RepeatedPtrField f,int limit){return f.elements&&f.current_size>=0&&f.current_size<=limit&&f.allocated_size>=f.current_size&&f.total_size>=f.allocated_size?f.current_size:0;}
static const struct BoostName *boost_name(const struct LootName *item,const struct StatBoost *boost){
    if(!item)return NULL;
    for(size_t i=0;i<sizeof(boost_names)/sizeof(*boost_names);++i){const struct BoostName *b=&boost_names[i];
        if(b->id!=(int)boost->stat||(*b->kind&&!in_list(b->kind,item->kind))||(*b->slot&&!in_list(b->slot,item->slot)))continue;
        if(boost->stat==43&&strcmp(b->name,boost->val==100?"Armor Piercing":"Barbed"))continue;
        if(boost->stat==36&&!strcmp(item->kind,"Ironclad")&&!strcmp(item->slot,"Main")&&strcmp(b->name,boost->val==1?"Height":"Plinks"))continue;
        return b;
    }return NULL;
}
static void boost_label(const struct LootName *item,const struct StatBoost *boost,char out[128]){
    const struct BoostName *b=boost_name(item,boost);if(!b){snprintf(out,128,"Stat %u: %+d",boost->stat,boost->val);return;}
    double value=b->divide?boost->val/100.0:boost->val;
    switch(b->display){
        case 1:snprintf(out,128,"%s: +%d",b->name,boost->val);break;
        case 2:snprintf(out,128,"%s: +%g%%",b->name,value);break;
        case 3:snprintf(out,128,"%s: +%gx",b->name,value);break;
        case 4:snprintf(out,128,"%s: %d",b->name,boost->val);break;
        case 5:snprintf(out,128,"%s: +%dHP",b->name,boost->val);break;
        case 6:snprintf(out,128,"%s: -%g%%",b->name,value);break;
        default:copystr(out,128,b->name);
    }
}
static int chest_items_snapshot(const struct ChestState *state,struct ChestItem items[CHEST_ITEMS]){
    /* Mac ClientObject is 8 bytes larger than the Windows-derived aggregate:
     * Chest::Chest/throwLoot prove items at 0x1b8 and pos at 0x1e8. */
    struct RepeatedPtrField field=state->items;
    int count=repeated_count(field,CHEST_ITEMS),n=0;int currency[2]={0,0};
    for(int i=0;i<count;++i){
        struct ItemProperties *ptr,item;if(!read_memory(field.elements+i,&ptr,sizeof(ptr))||!read_memory(ptr,&item,sizeof(item)))continue;
        struct ChestItem row={0};row.type=item.type;row.amount=1;const struct LootName *name=loot_name(item.type);
        if(name)copystr(row.name,sizeof(row.name),name->name);else snprintf(row.name,sizeof(row.name),"Item %d",item.type);
        int boosts=repeated_count(item.statboosts,CHEST_BOOSTS);row.nboost=boosts;
        for(int j=0;j<boosts;++j){struct StatBoost *bp,boost;if(read_memory(item.statboosts.elements+j,&bp,sizeof(bp))&&read_memory(bp,&boost,sizeof(boost))){boost_label(name,&boost,row.boosts[j]);const struct BoostName *bn=boost_name(name,&boost);if(bn)copystr(row.mod_names[j],sizeof(row.mod_names[j]),bn->name);}}
        int match=chest_rule_matches(chest_perf_rules,&row);if(match){row.perf=1;copystr(row.variant,sizeof(row.variant),chest_perf_rules[match-1].variant);}
        int value=0;char kind[3]="",extra;int coin=!row.nboost&&sscanf(row.name,"%d %2s %c",&value,kind,&extra)==2&&value>0&&(!strcmp(kind,"EC")||!strcmp(kind,"UC"));
        if(coin&&!getbool("plugin_sb_chest_display",!strcmp(kind,"EC")?"scd_show_ec":"scd_show_uc",1))continue;
        /* Currency is independent of equipment rarity. Three modifiers are
         * Experimental; four or more use the existing Prototype tier. */
        if(!coin&&row.nboost<(int)clamp(getnum("plugin_sb_chest_display","scd_min_modifiers",0),0,CHEST_BOOSTS))continue;
        int compress=coin&&getbool("plugin_sb_chest_display","scd_ec_uc_compress",0);
        struct ChestItem filter=row;if(compress)copystr(filter.name,sizeof(filter.name),kind);
        if(getbool("plugin_sb_chest_display","scd_remove_filter",0)){if(chest_rule_matches(chest_remove_rules,&filter))continue;}
        else if(getbool("plugin_sb_chest_display","scd_display_filter",0)&&!chest_rule_matches(chest_display_rules,&filter))continue;
        if(compress){currency[!strcmp(kind,"UC")]+=value;continue;}
        if(getbool("plugin_sb_chest_display","scd_item_compress",0)){
            int found=-1;for(int j=0;j<n;++j)if(items[j].type==row.type&&items[j].nboost==row.nboost&&!memcmp(items[j].boosts,row.boosts,sizeof(row.boosts))){found=j;break;}
            if(found>=0){++items[found].amount;continue;}
        }
        items[n++]=row;
    }
    for(int c=0;c<2&&n<CHEST_ITEMS;++c)if(currency[c]){struct ChestItem row={0};row.type=-1-c;row.amount=1;snprintf(row.name,sizeof(row.name),"%d %s",currency[c],c?"UC":"EC");items[n++]=row;}
    return n;
}
static int chest_snapshot(struct WorldObject *obj,struct ChestItem items[CHEST_ITEMS]){
    struct ChestState state;
    return chest_state(obj,&state)?chest_items_snapshot(&state,items):0;
}
static const char *chest_rarity(const struct ChestItem *item){
    static const char *names[]={"common","modified","custom","experimental","prototype"};return item->perf?"perf_item":names[item->nboost>4?4:item->nboost];
}
static uint32_t chest_opacity(uint32_t c){
    double opacity=clamp(getnum("plugin_sb_chest_display","scd_display_opacity",1),0,1);
    return (c&0xffffff)|((uint32_t)lround((c>>24)*opacity)<<24);
}
static uint32_t chest_color(const struct ChestItem *item,int modifier,int background){
    static const char *defaults[]={"ffffff","79f071","3d70f0","d04ef0","f0b03d"};char key[100];const char *r=chest_rarity(item),*s="plugin_sb_chest_display";
    if(background)snprintf(key,sizeof(key),"scd_%s_fill_background_colour",r);else snprintf(key,sizeof(key),"scd_text_%s_%s_colour",r,modifier?"modifier":"label");
    uint32_t c=color(s,key,item->perf?"d22b2b":defaults[item->nboost>4?4:item->nboost]);char opacity[120];snprintf(opacity,sizeof(opacity),"%s_opacity",key);
    return chest_opacity((c&0xffffff)|((uint32_t)lround(clamp(getnum(s,opacity,background?25:1),0,background?255:1)*(background?1:255))<<24));
}
static void chest_item_title(const struct ChestItem *item,char out[256]){
    static const char *mods[]={"","Modified ","Custom ","Experimental ","Prototype "};char amount[24]="";
    if(item->amount>1)snprintf(amount,sizeof(amount),"%d ",item->amount);
    if(item->perf)snprintf(out,256,"%s%s %s",amount,item->variant,item->name);else snprintf(out,256,"%s%s%s",amount,mods[item->nboost>4?4:item->nboost],item->name);
}
static uint32_t chest_panel_color(const char *key,const char *fallback){
    const char *s="plugin_sb_chest_display";uint32_t c=color(s,key,fallback);char opt[120];snprintf(opt,sizeof(opt),"%s_opacity",key);
    return chest_opacity((c&0xffffff)|((uint32_t)clamp(getnum(s,opt,255),0,255)<<24));
}
static void chest_overlay(struct ClientWorld *cw,struct WorldView *wv){
    const char *s="plugin_sb_chest_display";
    if(!cw||!wv||!cw->player||!getbool(s,"scd_display_box",1)||in_zone(cw,"home")||in_zone(cw,"lobby")||*canvas_w<=0||*canvas_h<=0||physical_w<=0||physical_h<=0)return;
    struct Properties player;if(!read_memory(&((struct WorldObject*)cw->player)->props,&player,sizeof(player)))return;
    int ww=*canvas_w,wh=*canvas_h,old_outline=text_outline;text_outline=0;
    double ratio=render_pixel_ratio,sw=physical_w*ratio,sh=physical_h*ratio;
    static struct ChestItem items[CHEST_ITEMS];static struct TextCache *labels[CHEST_ITEMS][CHEST_BOOSTS+1];
    for(int k=0;k<nobjects;++k){struct WorldObject *obj=objects[k];struct ChestState state;if(!(object_source(obj)&2)||!chest_state(obj,&state))continue;
        const struct Properties *chest=&state.props;
        double x=(chest->xmp/256.0+chest->wmp/512.0-wv->offset.x)*sw/ww,y=(chest->ymp/256.0-wv->offset.y)*sh/wh;
        if(x<0||x>sw||y<0||y>sh)continue;
        const struct Properties *p=&player;
        int over=p->xmp<(int64_t)chest->xmp+chest->wmp&&(int64_t)p->xmp+p->wmp>chest->xmp&&p->ymp<(int64_t)chest->ymp+chest->hmp&&(int64_t)p->ymp+p->hmp>chest->ymp;
        if(getbool(s,"scd_disable_on_walk_over",0)&&over)continue;
        if(getbool(s,"scd_enable_on_walk_over",0)&&!getbool(s,"scd_disable_on_walk_over",0)&&!over)continue;
        int n=chest_items_snapshot(&state,items);if(!n)continue;
        int limit=(int)clamp(getnum(s,"scd_max_items_per_box",15),1,256),columns=(n+limit-1)/limit;if(columns>3)columns=3;
        int per=getbool(s,"scd_equal_chest_display",1)?(n+columns-1)/columns:limit;
        int starts[3]={0},ends[3]={0};double widths[3]={0},heights[3]={0};int column=0,used=0;
        double scale=clamp(getnum(s,"scd_display_scale",1),.4,2);
        int size=(int)clamp(lround(getnum(s,"scd_text_display_size",15)*scale),8,64);
        *canvas_w=physical_w;*canvas_h=physical_h;
        for(int i=0;i<n;++i){if(used>=per&&column+1<columns){ends[column]=i;starts[++column]=i;used=0;}
            char title[256];chest_item_title(&items[i],title);labels[i][0]=cache_text(title,size,chest_color(&items[i],0,0),"HemiHeadBold");
            double h=0,w=0;for(int j=0;j<=items[i].nboost;++j){if(j)labels[i][j]=cache_text(items[i].boosts[j-1],size,chest_color(&items[i],1,0),"HemiHeadBold");struct TextCache *t=labels[i][j];if(t){w=fmax(w,t->w+(j?20*scale:0));h+=fmax(22*scale,t->h+5*scale);}}
            widths[column]=fmax(widths[column],w+64*scale);heights[column]+=h+4*scale;++used;
        }
        ends[column]=n;columns=column+1;
        double gap=clamp(getnum(s,"scd_display_x_spacing",25),0,500)*scale,total=gap*(columns-1),maxh=0;for(int c=0;c<columns;++c){total+=widths[c];maxh=fmax(maxh,heights[c]);}
        double bx=clamp(x-total/2,4,fmax(4,sw-total-4)),by=clamp(y-maxh-getnum(s,"scd_display_y_spacing",15)*scale,4,fmax(4,sh-maxh-4));
        /* Background geometry uses the same drawable pixels as its text. */
        *canvas_w=(int)lround(sw);*canvas_h=(int)lround(sh);
        for(int c=0;c<columns;++c){int ix=(int)lround(bx),iy=(int)lround(by),width=(int)ceil(widths[c]),height=(int)ceil(heights[c]);
            if(getbool(s,"scd_display_container_background",1))fill_rect(ix,iy,width,height,chest_panel_color("scd_display_background_colour","2b2b2b"),BLENDMODE_BLEND);
            uint32_t border=chest_panel_color("scd_display_border_colour","a0a0a0");
            fill_rect(ix,iy,width,1,border,BLENDMODE_BLEND);fill_rect(ix,iy+height-1,width,1,border,BLENDMODE_BLEND);fill_rect(ix,iy,1,height,border,BLENDMODE_BLEND);fill_rect(ix+width-1,iy,1,height,border,BLENDMODE_BLEND);
            double yy=by+5*scale;
            for(int i=starts[c];i<ends[c];++i){double ih=0;for(int j=0;j<=items[i].nboost;++j)if(labels[i][j])ih+=fmax(22*scale,labels[i][j]->h+5*scale);
                if(items[i].nboost&&getbool(s,"scd_display_panel_group_background",0))fill_rect(ix+1,(int)lround(yy-3*scale),width-2,(int)ceil(ih),chest_color(&items[i],0,1),BLENDMODE_BLEND);
                *canvas_w=physical_w;*canvas_h=physical_h;
                for(int j=0;j<=items[i].nboost;++j){struct TextCache *t=labels[i][j];if(t){draw_cached_text(t,(int)lround((bx+(10+(j?20:0))*scale)/ratio),(int)lround(yy/ratio),0,0);yy+=fmax(22*scale,t->h+5*scale);}}
                *canvas_w=(int)lround(sw);*canvas_h=(int)lround(sh);yy+=4*scale;
                if(items[i].nboost&&i+1<ends[c])fill_rect(ix+(int)lround(11*scale),(int)lround(yy-2*scale),width-(int)lround(22*scale),1,chest_opacity(0xff808080),BLENDMODE_BLEND);
            }
            bx+=widths[c]+gap;
        }
        *canvas_w=ww;*canvas_h=wh;
    }
    text_outline=old_outline;*canvas_w=ww;*canvas_h=wh;
}
