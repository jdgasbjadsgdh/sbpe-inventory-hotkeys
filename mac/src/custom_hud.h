/* Mac PlayerCharacter fields verified against HUDStatus::draw and modifyAmmo.
 * Do not use the unverified Windows PlayerCharacter aggregate for these reads. */
_Static_assert(offsetof(struct WorldClient,hud)==0x88,"WorldClient HUD pointer");
_Static_assert(offsetof(struct HUD,hudStatus)==0x58,"HUD status pointer");
struct HudSnapshot {
    int valid,hp,maxhp,ammo,maxammo,mult,ec,uc;
    struct WorldObject *player;
};
static int hud_snapshot(struct ClientWorld *cw,struct HudSnapshot *out){
    memset(out,0,sizeof(*out));if(!cw||!cw->player)return 0;
    const char *kind=class_name(cw->player);
    if(!in_list(kind,"PlayerCharacter Ancient Duskwing Fabricator Ironclad Toaster Wildfire"))return 0;
    int currency[2],ammo[4];struct Properties props;
    const unsigned char *pc=(const void*)cw->player;
    if(!read_memory(pc+0x218,currency,sizeof(currency))||!read_memory(pc+0x2cc,ammo,sizeof(ammo))||!read_memory(&((struct WorldObject*)pc)->props,&props,sizeof(props)))return 0;
    int64_t maximum=(int64_t)ammo[0]+ammo[1];
    if(maximum<0||maximum>INT32_MAX||ammo[2]<0||ammo[3]<1||ammo[3]>100||currency[0]<0||currency[1]<0||props.maxhitpoints<=0)return 0;
    *out=(struct HudSnapshot){1,props.hitpoints,props.maxhitpoints,ammo[2],(int)maximum,ammo[3],currency[0],currency[1],(struct WorldObject*)cw->player};
    static int reported;if(!reported){LOG("Custom HUD: Mac scalar reads active; character=%s",kind);reported=1;}
    return 1;
}
static struct UIElement *hide_builtin_hud(struct WorldClient *wc,bool *was_show){
    struct HUD *hud=NULL;struct HUDStatus *status=NULL;struct UIElement header;
    if(!wc||!read_memory(&wc->hud,&hud,sizeof(hud))||!hud||!read_memory(&hud->hudStatus,&status,sizeof(status))||!status||!read_memory(status,&header,sizeof(header)))return NULL;
    if(strcmp(class_name(status),"HUDStatus"))return NULL;
    *was_show=header.show;((struct UIElement*)status)->show=0;return (struct UIElement*)status;
}
static double hud_text(const char *value,double x,double y,int size,uint32_t c,double ax,double ay){
    int slot=text_slot;text_style(value,(int)lround(x),(int)lround(y),size,c,"HemiHeadBold",ax,ay);
    return text_slot>slot?texts[slot].h/render_pixel_ratio:0;
}
static void draw_custom_hud(const struct HudSnapshot *hud,struct WorldView *wv,int world_w,int world_h){
    if(!custom_hud_visible||!hud->valid)return;
    const char *s="plugin_hud";char label[80];int old_outline=text_outline;
    text_outline=(int)clamp(getnum(s,"outline",3),0,16);
    double x=getnum(s,"x",100)/render_pixel_ratio,y=getnum(s,"y",20)/render_pixel_ratio;
    double spacing=getnum(s,"spacing",0)/render_pixel_ratio;
    uint32_t hp_color=color(s,hud->hp==hud->maxhp?"color_hp_full":"color_hp",hud->hp==hud->maxhp?"0f0":"fff");
    snprintf(label,sizeof(label),"%d",hud->hp);
    double hp_height=hud_text(label,x,y,(int)getnum(s,"size_hp",42),hp_color,1,.5);
    snprintf(label,sizeof(label),"/%d",hud->maxhp);hud_text(label,x-4/render_pixel_ratio,y,(int)getnum(s,"size_hpmax",24),hp_color,0,.5);
    y+=hp_height+spacing;snprintf(label,sizeof(label),"%d",hud->ammo);
    double ammo_height=hud_text(label,x,y,(int)getnum(s,"size_ammo",20),color(s,"color_ammo","ff0"),1,.5);
    if(hud->mult>1)snprintf(label,sizeof(label),"/%d (x%d)",hud->maxammo,hud->mult);else snprintf(label,sizeof(label),"/%d",hud->maxammo);
    hud_text(label,x-4/render_pixel_ratio,y,(int)getnum(s,"size_ammomax",20),color(s,"color_ammo","ff0"),0,.5);
    y+=ammo_height+spacing;snprintf(label,sizeof(label),"%d EC  %d UC",hud->ec,hud->uc);
    hud_text(label,x,y,(int)getnum(s,"size_currency",16),color(s,"color_currency","99f"),.5,0);
    text_outline=old_outline;
    int bw=(int)getnum(s,"bar_width",90),bh=(int)clamp(getnum(s,"bar_height",4),0,128);
    if(bw<0)bw=hud->maxhp;bw=(int)fmin(bw,4096);
    if(!wv||world_w<=0||world_h<=0||bw<=0||bh<=0)return;
    struct Properties *p=&hud->player->props;
    double cx=(p->xmp/256.0+p->wmp/512.0-wv->offset.x)*physical_w/world_w*render_pixel_ratio;
    double cy=(p->ymp/256.0-wv->offset.y)*physical_h/world_h*render_pixel_ratio;
    int bx=(int)lround(cx)-bw/2,by=(int)lround(cy)-bh-(int)getnum(s,"bar_y",10);
    int saved_w=*canvas_w,saved_h=*canvas_h;
    *canvas_w=(int)lround(physical_w*render_pixel_ratio);*canvas_h=(int)lround(physical_h*render_pixel_ratio);
    fill_rect(bx-1,by-1,bw+2,bh+2,color(s,"bar_outline","808080"),BLENDMODE_BLEND);
    fill_rect(bx,by,bw,bh,color(s,"bar_background","000"),BLENDMODE_BLEND);
    int filled=(int)ceil(clamp((double)hud->hp/hud->maxhp,0,1)*bw);
    if(filled)fill_rect(bx,by,filled,bh,color(s,"bar_color","0afde1"),BLENDMODE_BLEND);
    int previous=-1;
    for(int st=1;st<=10000&&(int64_t)st*25<hud->maxhp;++st){
        int cx=bx+(int)lround(bw*(hud->maxhp-st*25.0)/hud->maxhp);
        if(cx<=bx+filled)break;
        if(cx!=previous)fill_rect(cx,by,1,bh,color(s,"bar_notches","fff"),BLENDMODE_BLEND);
        previous=cx;
    }
    *canvas_w=saved_w;*canvas_h=saved_h;
}
