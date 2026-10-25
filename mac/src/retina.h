/* Retina support keeps game/mouse coordinates in points, but draws into the
 * full OpenGL backing surface. Only verified writable import slots are used. */
#if !defined(SBPE_TEST) || defined(SBPE_RETINA_TEST)
static void *(*retina_create_window)(const char*,int,int,int,int,unsigned);
static void (*retina_drawable_size)(void*,int*,int*);
static void (*retina_viewport)(int,int,int,int);
static void *(*objc_class)(const char*),*(*objc_sel)(const char*);
static void *(*objc_send)(void*,void*,...);
static void *retina_context;
static void *create_retina_window(const char *t,int x,int y,int w,int h,unsigned flags){
    if(getbool("native","retina",1))flags|=0x2000;
    LOG("Window creation: high-DPI flag=%d",(flags&0x2000)!=0);
    return retina_create_window(t,x,y,w,h,flags);
}
static void drawable_viewport(int x,int y,int w,int h){
    if(getbool("native","retina",1)&&window_ptr&&*window_ptr&&retina_drawable_size){
        int lw=0,lh=0,dw=0,dh=0;get_window_size(*window_ptr,&lw,&lh);retina_drawable_size(*window_ptr,&dw,&dh);
        if(x==0&&y==0&&w==lw&&h==lh&&dw>0&&dh>0){w=dw;h=dh;}
    }
    retina_viewport(x,y,w,h);
}
static void retina_update(void){
    if(!getbool("native","retina",1)||!window_ptr||!*window_ptr||!retina_viewport||!retina_drawable_size)return;
    if(objc_class&&objc_sel&&objc_send){
        static int reported_bundle;
        if(!reported_bundle){
            void *bundle=objc_send(objc_class("NSBundle"),objc_sel("mainBundle"));
            void *path=objc_send(bundle,objc_sel("bundlePath"));
            const char *utf8=(const char*)objc_send(path,objc_sel("UTF8String"));
            void *key=objc_send(objc_class("NSString"),objc_sel("stringWithUTF8String:"),"NSHighResolutionCapable");
            void *value=objc_send(bundle,objc_sel("objectForInfoDictionaryKey:"),key);
            int enabled=((signed char(*)(void*,void*))objc_send)(value,objc_sel("boolValue"));
            LOG("Retina app bundle: %s; NSHighResolutionCapable=%d",utf8?utf8:"unknown",enabled!=0);reported_bundle=1;
        }
        void *ctx=objc_send(objc_class("NSOpenGLContext"),objc_sel("currentContext"));
        if(ctx&&ctx!=retina_context){
            void *view=objc_send(ctx,objc_sel("view"));
            void *selector=objc_sel("setWantsBestResolutionOpenGLSurface:");
            if(view&&((signed char(*)(void*,void*,void*))objc_send)(view,objc_sel("respondsToSelector:"),selector)){
                ((void(*)(void*,void*,signed char))objc_send)(view,selector,1);
                objc_send(ctx,objc_sel("update"));
            }
            retina_context=ctx;
        }
    }
    int w=0,h=0,dw=0,dh=0;get_window_size(*window_ptr,&w,&h);retina_drawable_size(*window_ptr,&dw,&dh);
    if(dw>0&&dh>0){retina_viewport(0,0,dw,dh);render_pixel_ratio=w>0?clamp((double)dw/w,1,4):1;}
    static int lastw,lasth,lastdw,lastdh;
    if(w!=lastw||h!=lasth||dw!=lastdw||dh!=lastdh){
        LOG("Retina: window %dx%d points, drawable %dx%d pixels",w,h,dw,dh);
        lastw=w;lasth=h;lastdw=dw;lastdh=dh;
    }
}
static void install_retina(void){
    retina_create_window=dlsym(RTLD_DEFAULT,"SDL_CreateWindow");
    retina_drawable_size=dlsym(RTLD_DEFAULT,"SDL_GL_GetDrawableSize");
    retina_viewport=dlsym(RTLD_DEFAULT,"glViewport");
    objc_class=dlsym(RTLD_DEFAULT,"objc_getClass");objc_sel=dlsym(RTLD_DEFAULT,"sel_registerName");objc_send=dlsym(RTLD_DEFAULT,"objc_msgSend");
    uintptr_t *create=(void*)PTR(0x1001e2c88),*viewport=(void*)PTR(0x1001e2f80);
    if(retina_create_window&&(*create==PTR(0x100190118)||*create==(uintptr_t)retina_create_window)){
        *create=(uintptr_t)create_retina_window;LOG("Retina window creation hook installed.");
    }else LOG("Retina window import mismatch; creation hook disabled.");
    if(retina_viewport&&(*viewport==PTR(0x1001904ce)||*viewport==(uintptr_t)retina_viewport))*viewport=(uintptr_t)drawable_viewport;
}
#else
static void retina_update(void){}
#endif
