/* Keep native UI and world layout in the same virtual coordinates. Ordinary
 * nameplates are rasterized at their zoomed drawable size and placed on pixel
 * boundaries, without changing their world anchor or fixing their font size. */
static int screen_ui_hook,screen_names_drawn;
static void (*screen_original_world_draw)(struct WorldView*,int);
/* Mac libc++ strings align to eight bytes. The original generated Windows
 * aggregate has no padding after texture and must not be used for this read. */
struct ScreenTextTexture {
    _Alignas(8) struct STDString fontName;
    int size;uint32_t color,colorMod;int outlineSize;uint32_t outlineColor;
    int lineSpacing;unsigned texture;
    _Alignas(8) struct STDString text;
    int w,h;
};
static struct ScreenTextTexture *(*screen_name_texture)(struct WorldView*,struct WorldObject*);
static struct ScreenTextTexture *(*screen_faction_texture)(struct WorldView*,struct WorldObject*,struct Player*);
_Static_assert(offsetof(struct ScreenTextTexture,size)==0x18,"Mac TextTexture size");
_Static_assert(offsetof(struct ScreenTextTexture,color)==0x1c,"Mac TextTexture color");
_Static_assert(offsetof(struct ScreenTextTexture,outlineSize)==0x24,"Mac TextTexture outline");
_Static_assert(offsetof(struct ScreenTextTexture,texture)==0x30,"Mac TextTexture texture");
_Static_assert(offsetof(struct ScreenTextTexture,text)==0x38,"Mac TextTexture text");
_Static_assert(offsetof(struct ScreenTextTexture,w)==0x50,"Mac TextTexture width");
_Static_assert(sizeof(struct ScreenTextTexture)==0x58,"Mac TextTexture size/alignment");
_Static_assert(offsetof(struct WorldView,world)==0x30,"Mac WorldView client world");
static int readable_names_enabled(void){return screen_ui_hook&&!paused&&getbool("native","readable_names",1);}
static int screen_ui_extent(int logical){return (int)fmax(1,lround(logical*render_pixel_ratio));}
static struct TextCache *screen_name_label(struct ScreenTextTexture *source,const char *size_key,double scale,int *world_h){
    struct ScreenTextTexture native;char text[256],font[64];
    if(!read_memory(source,&native,sizeof(native)))return NULL;
    read_string(&native.text,text,sizeof(text));read_string(&native.fontName,font,sizeof(font));
    if(!text[0]||!font[0]||native.size<1||native.size>320)return NULL;
    int size=(int)clamp(getnum("native",size_key,native.size),1,320);
    int pixels=(int)clamp(lround(size*scale),1,320),old_outline=text_outline;
    /* Keep a one-pixel border when zoomed down, rather than a heavy two-pixel
     * border around a five-pixel glyph. Do not impose an eight-pixel minimum. */
    text_outline=native.outlineSize>0?(int)clamp(lround(native.outlineSize*scale),1,16):0;
    struct TextCache *result=cache_text_raster(text,pixels,native.color,font);text_outline=old_outline;
    *world_h=(int)clamp(native.h,0,4096);return result;
}
static void screen_draw_label(struct TextCache *t,int x,int y){
    if(t&&t->texture)draw_texture(t->texture,0,0,t->w,t->h,x-(int)lround(t->w*.5),y,t->w,t->h,0,0,0,0,0xff000000,BLENDMODE_BLEND);
}
static void draw_readable_names(struct ClientWorld *cw,struct WorldView *wv){
    if(!readable_names_enabled()||!cw||!wv||!screen_name_texture||!screen_faction_texture||physical_w<1||physical_h<1)return;
    int world_w=*canvas_w,world_h=*canvas_h;if(world_w<1||world_h<1)return;
    int screen_w=screen_ui_extent(physical_w),screen_h=screen_ui_extent(physical_h);
    double sx=(double)screen_w/world_w,sy=(double)screen_h/world_h;
    struct STDVector v=cw->allies;int n=vector_count(v,sizeof(void*));struct WorldObject **allies=v.start;
    for(int i=0;i<n;++i){
        struct WorldObject *obj=allies[i];struct PlayerIdentity id;unsigned primary;
        if(!obj||obj==(struct WorldObject*)cw->player||object_hidden(cw,obj)||hide_names||!(obj->props._has_bits[0]&0x300000)||!player_identity(obj,&id))continue;
        unsigned parts=highlight_parts(&id,&primary);struct TextCache *name=NULL,*faction=NULL;int nh=0,fh=0;
        if(!(parts&1))name=screen_name_label(screen_name_texture(wv,obj),"player_name_size",sy,&nh);
        if(!(parts&2)&&id.faction[0]&&!getbool("plugin_noise","hide_factions",0))faction=screen_name_label(screen_faction_texture(wv,obj,cw->player),"faction_name_size",sy,&fh);
        (void)fh;
        double x=(obj->props.xmp/256.0+obj->props.wmp/512.0-wv->offset.x)*sx;
        double y=(obj->props.ymp/256.0+obj->props.hmp/256.0-wv->offset.y+2)*sy;
        double faction_y=y+(name?fmax(0,nh-2)*sy:0);
        double width=fmax(name?name->w:0,faction?faction->w:0),height=faction?faction_y-y+faction->h:(name?name->h:0);
        x=width>screen_w-6?screen_w/2.0:clamp(x,3+width/2,screen_w-3-width/2);
        double clamped_y=clamp(y,3,fmax(3,screen_h-3-height));faction_y+=clamped_y-y;
        /* Positions and glyph extents use drawable pixels together. Dividing
         * through window points here caused extra Retina rounding/drift. */
        *canvas_w=screen_w;*canvas_h=screen_h;
        screen_draw_label(name,(int)lround(x),(int)lround(clamped_y));
        screen_draw_label(faction,(int)lround(x),(int)lround(faction_y));
        *canvas_w=world_w;*canvas_h=world_h;
    }
}
static void screen_world_draw(struct WorldView *wv,int dt){
    screen_original_world_draw(wv,dt);
    if(paused)return;
    clarity_frame();
    /* The original draw computes the final camera offset first. Restore only
     * the temporary name suppression, then use that camera for crisp labels. */
    restore_labels();draw_readable_names(wv->world,wv);highlight_players(wv->world,wv);screen_names_drawn=1;
    static int reported;if(!reported){LOG("Native UI/world layout shared: %dx%d; ordinary labels scale with zoom.",*canvas_w,*canvas_h);reported=1;}
}
#ifndef SBPE_TEST
static void install_screen_ui(void){
    uintptr_t *slot=(void*)PTR(0x1001ee4e8);
    if(*slot!=PTR(0x1000e23a0)){LOG("WorldView draw slot mismatch: zoom-scaled nameplate pass left disabled.");return;}
    screen_original_world_draw=(void*)*slot;
    screen_name_texture=(void*)PTR(0x1000e4e60);screen_faction_texture=(void*)PTR(0x1000e5430);
    *slot=(uintptr_t)screen_world_draw;screen_ui_hook=1;
    LOG("WorldView post-draw sharpening and zoom-scaled nameplates installed; native UI layout retained.");
}
#endif
