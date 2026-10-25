/* A bounded, screen-space sharpening pass for the legacy OpenGL renderer.
 * It reads the completed game image, never the packed sprite atlases. Custom
 * text/HUD/map overlays are drawn afterward, preserving their pixel geometry.
 * Entry points are resolved from the game's current SDL OpenGL context. */
#if !defined(SBPE_TEST) || defined(SBPE_CLARITY_TEST)
static void (*render_flush)(int);
static void *(*clarity_proc)(const char*),*(*clarity_context)(void);
struct ClarityGL {
    const unsigned char *(*GetString)(unsigned);
    void (*GetIntegerv)(unsigned,int*);
    void (*PushAttrib)(unsigned);void (*PopAttrib)(void);
    void (*ActiveTexture)(unsigned);void (*BindTexture)(unsigned,unsigned);
    void (*GenTextures)(int,unsigned*);void (*DeleteTextures)(int,const unsigned*);
    void (*TexParameteri)(unsigned,unsigned,int);
    void (*CopyTexImage2D)(unsigned,int,unsigned,int,int,int,int,int);
    void (*CopyTexSubImage2D)(unsigned,int,int,int,int,int,int,int);
    void (*GetTexLevelParameteriv)(unsigned,int,unsigned,int*);
    unsigned (*CreateShader)(unsigned);void (*ShaderSource)(unsigned,int,const char*const*,const int*);
    void (*CompileShader)(unsigned);void (*GetShaderiv)(unsigned,unsigned,int*);
    void (*GetShaderInfoLog)(unsigned,int,int*,char*);void (*DeleteShader)(unsigned);
    unsigned (*CreateProgram)(void);void (*AttachShader)(unsigned,unsigned);
    void (*LinkProgram)(unsigned);void (*GetProgramiv)(unsigned,unsigned,int*);
    void (*GetProgramInfoLog)(unsigned,int,int*,char*);void (*DeleteProgram)(unsigned);
    void (*UseProgram)(unsigned);int (*GetUniformLocation)(unsigned,const char*);
    void (*Uniform1i)(int,int);void (*Uniform1f)(int,float);void (*Uniform2f)(int,float,float);
    void (*Disable)(unsigned);void (*ColorMask)(unsigned char,unsigned char,unsigned char,unsigned char);
    void (*ReadBuffer)(unsigned);void (*PolygonMode)(unsigned,unsigned);
    void (*Begin)(unsigned);void (*End)(void);void (*TexCoord2f)(float,float);void (*Vertex2f)(float,float);
};
static struct ClarityGL clarity_gl;
static struct {void *context;unsigned program,texture;int w,h,failed,ready,reported,sampler,step,amount,srgb;} clarity;
static int clarity_load(void){
    if(!clarity_proc)return 0;
#define CLARITY_LOAD(name) do { void *address=clarity_proc("gl" #name);memcpy(&clarity_gl.name,&address,sizeof(address));if(!clarity_gl.name)return 0; } while(0)
    CLARITY_LOAD(GetString);CLARITY_LOAD(GetIntegerv);CLARITY_LOAD(PushAttrib);CLARITY_LOAD(PopAttrib);
    CLARITY_LOAD(ActiveTexture);CLARITY_LOAD(BindTexture);CLARITY_LOAD(GenTextures);CLARITY_LOAD(DeleteTextures);
    CLARITY_LOAD(TexParameteri);CLARITY_LOAD(CopyTexImage2D);CLARITY_LOAD(CopyTexSubImage2D);CLARITY_LOAD(GetTexLevelParameteriv);
    CLARITY_LOAD(CreateShader);CLARITY_LOAD(ShaderSource);CLARITY_LOAD(CompileShader);CLARITY_LOAD(GetShaderiv);CLARITY_LOAD(GetShaderInfoLog);CLARITY_LOAD(DeleteShader);
    CLARITY_LOAD(CreateProgram);CLARITY_LOAD(AttachShader);CLARITY_LOAD(LinkProgram);CLARITY_LOAD(GetProgramiv);CLARITY_LOAD(GetProgramInfoLog);CLARITY_LOAD(DeleteProgram);
    CLARITY_LOAD(UseProgram);CLARITY_LOAD(GetUniformLocation);CLARITY_LOAD(Uniform1i);CLARITY_LOAD(Uniform1f);CLARITY_LOAD(Uniform2f);
    CLARITY_LOAD(Disable);CLARITY_LOAD(ColorMask);CLARITY_LOAD(Begin);CLARITY_LOAD(End);CLARITY_LOAD(TexCoord2f);CLARITY_LOAD(Vertex2f);
    CLARITY_LOAD(ReadBuffer);CLARITY_LOAD(PolygonMode);
#undef CLARITY_LOAD
    return 1;
}
static unsigned clarity_shader(unsigned kind,const char *source){
    struct ClarityGL *g=&clarity_gl;unsigned shader=g->CreateShader(kind);if(!shader)return 0;
    g->ShaderSource(shader,1,&source,NULL);g->CompileShader(shader);int ok=0;g->GetShaderiv(shader,0x8b81,&ok);
    if(!ok){char message[512]={0};g->GetShaderInfoLog(shader,sizeof(message)-1,NULL,message);LOG("Zoom sharpening shader unavailable: %s",message);g->DeleteShader(shader);return 0;}
    return shader;
}
static int clarity_prepare(void *context){
    /* A new context owns different object names. Never delete an old context's
     * names through a replacement context (they may name unrelated objects). */
    if(context!=clarity.context){memset(&clarity,0,sizeof(clarity));clarity.context=context;}
    if(clarity.failed)return 0;if(clarity.ready)return 1;
    if(!clarity_load()){clarity.failed=1;LOG("Zoom sharpening unavailable: missing legacy OpenGL entry point; normal rendering retained.");return 0;}
    const char *version=(const char*)clarity_gl.GetString(0x1f02);int major=0,minor=0;
    if(!version||sscanf(version,"%d.%d",&major,&minor)!=2||major<2){clarity.failed=1;return 0;}
    if(major>3||(major==3&&minor>=2)){
        int profile=0;clarity_gl.GetIntegerv(0x9126,&profile);
        if(!(profile&2)){clarity.failed=1;LOG("Zoom sharpening requires a compatibility OpenGL context; normal rendering retained.");return 0;}
    }
    const char *extensions=(const char*)clarity_gl.GetString(0x1f03);
    clarity.srgb=major>=3||(extensions&&(strstr(extensions,"GL_EXT_framebuffer_sRGB")||strstr(extensions,"GL_ARB_framebuffer_sRGB")));
    const char *vs="#version 120\nvarying vec2 uv;void main(){uv=gl_MultiTexCoord0.xy;gl_Position=gl_Vertex;}\n";
    const char *fs="#version 120\nuniform sampler2D scene;uniform vec2 texel;uniform float amount;varying vec2 uv;\n"
        "void main(){vec4 c=texture2D(scene,uv);vec3 avg=(texture2D(scene,uv+vec2(texel.x,0.0)).rgb+"
        "texture2D(scene,uv-vec2(texel.x,0.0)).rgb+texture2D(scene,uv+vec2(0.0,texel.y)).rgb+"
        "texture2D(scene,uv-vec2(0.0,texel.y)).rgb)*0.25;"
        "vec3 detail=clamp(c.rgb-avg,vec3(-0.15),vec3(0.15));"
        "gl_FragColor=vec4(clamp(c.rgb+amount*detail,0.0,1.0),c.a);}\n";
    unsigned vertex=clarity_shader(0x8b31,vs),fragment=clarity_shader(0x8b30,fs);
    struct ClarityGL *g=&clarity_gl;
    if(!vertex||!fragment){if(vertex)g->DeleteShader(vertex);if(fragment)g->DeleteShader(fragment);clarity.failed=1;return 0;}
    unsigned program=g->CreateProgram();int ok=0;
    if(program){g->AttachShader(program,vertex);g->AttachShader(program,fragment);g->LinkProgram(program);g->GetProgramiv(program,0x8b82,&ok);}
    g->DeleteShader(vertex);g->DeleteShader(fragment);
    if(!ok){char message[512]={0};if(program){g->GetProgramInfoLog(program,sizeof(message)-1,NULL,message);g->DeleteProgram(program);}LOG("Zoom sharpening program unavailable: %s",message);clarity.failed=1;return 0;}
    clarity.program=program;clarity.sampler=g->GetUniformLocation(program,"scene");clarity.step=g->GetUniformLocation(program,"texel");clarity.amount=g->GetUniformLocation(program,"amount");
    if(clarity.sampler<0||clarity.step<0||clarity.amount<0){g->DeleteProgram(program);clarity.program=0;clarity.failed=1;return 0;}
    clarity.ready=1;return 1;
}
static void clarity_frame(void){
    double amount=clamp(getnum("native","zoom_sharpen",.45),0,1);
    if(paused||amount<=0||!canvas_w||physical_w<=0||*canvas_w<=physical_w||!render_flush||!clarity_context)return;
    void *context=clarity_context();if(!context||!clarity_prepare(context))return;
    struct ClarityGL *g=&clarity_gl;int viewport[4]={0},maximum=0,depth=0,maxdepth=0;
    g->GetIntegerv(0xba2,viewport);g->GetIntegerv(0xd33,&maximum);
    g->GetIntegerv(0xbb0,&depth);g->GetIntegerv(0xd35,&maxdepth);if(depth>=maxdepth)return;
    int w=viewport[2],h=viewport[3];
    if(viewport[0]||viewport[1]||w<1||h<1||w>maximum||h>maximum||(int64_t)w*h>33554432)return;
    /* Submit the game's batch before copying its image. This is the verified
     * internal flush, not XDL_Present or a buffer swap. */
    render_flush(0);
    int program=0,read=0,active=0;g->GetIntegerv(0x8b8d,&program);g->GetIntegerv(0xc01,&read);g->GetIntegerv(0x84e0,&active);if(!read)return;
    g->PushAttrib(0x000fffff);g->ActiveTexture(0x84c0);
    if(!clarity.texture)g->GenTextures(1,&clarity.texture);
    if(!clarity.texture){clarity.failed=1;g->PopAttrib();return;}
    g->BindTexture(0xde1,clarity.texture);
    g->ReadBuffer((unsigned)read);
    if(w!=clarity.w||h!=clarity.h){
        g->TexParameteri(0xde1,0x2801,0x2600);g->TexParameteri(0xde1,0x2800,0x2600);
        g->TexParameteri(0xde1,0x2802,0x812f);g->TexParameteri(0xde1,0x2803,0x812f);
        g->CopyTexImage2D(0xde1,0,0x8058,0,0,w,h,0);
        int actual_w=0,actual_h=0;g->GetTexLevelParameteriv(0xde1,0,0x1000,&actual_w);g->GetTexLevelParameteriv(0xde1,0,0x1001,&actual_h);
        if(actual_w!=w||actual_h!=h){clarity.failed=1;LOG("Zoom sharpening allocation unavailable; normal rendering retained.");g->PopAttrib();return;}
        clarity.w=w;clarity.h=h;
    }else g->CopyTexSubImage2D(0xde1,0,0,0,0,0,w,h);
    const unsigned disabled[]={0xbe2,0xb71,0xb90,0xc11,0xb44,0xbc0,0xbf2,0xb60,0xb50,0xbd0};
    for(size_t i=0;i<sizeof(disabled)/sizeof(*disabled);++i)g->Disable(disabled[i]);
    for(unsigned i=0;i<6;++i)g->Disable(0x3000+i);
    if(clarity.srgb)g->Disable(0x8db9);
    g->PolygonMode(0x0408,0x1b02);
    g->ColorMask(1,1,1,1);g->UseProgram(clarity.program);
    g->Uniform1i(clarity.sampler,0);g->Uniform2f(clarity.step,1.0f/w,1.0f/h);g->Uniform1f(clarity.amount,(float)amount);
    g->Begin(0x0007);
    g->TexCoord2f(0,0);g->Vertex2f(-1,-1);g->TexCoord2f(1,0);g->Vertex2f(1,-1);
    g->TexCoord2f(1,1);g->Vertex2f(1,1);g->TexCoord2f(0,1);g->Vertex2f(-1,1);
    g->End();g->UseProgram((unsigned)program);g->PopAttrib();g->ActiveTexture((unsigned)active);
    if(!clarity.reported){LOG("Zoom sharpening active: strength %.2f, %dx%d drawable pixels; custom overlays remain unfiltered.",amount,w,h);clarity.reported=1;}
}
static void install_clarity(void){
    clarity_proc=dlsym(RTLD_DEFAULT,"SDL_GL_GetProcAddress");clarity_context=dlsym(RTLD_DEFAULT,"SDL_GL_GetCurrentContext");
    render_flush=(void*)PTR(0x1000fbda0);
}
#else
static void clarity_frame(void){}
#endif
