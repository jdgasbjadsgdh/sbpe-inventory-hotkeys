#define SBPE_TEST
#include "../src/sbpe.c"
#include <assert.h>

static uint8_t polled[512];static int forwards,resets,added_count;
static const uint8_t *polled_keys(int *n){*n=512;return polled;}
static void *no_focus(void){return NULL;}
static void *new_object(size_t n){return calloc(1,n);}
static int lookup(const char *s){
    if(!strcmp(s,"None"))return 0;
    if(strlen(s)==1){if(isalpha((unsigned char)*s))return toupper((unsigned char)*s)-'A'+4;if(*s=='.')return 55;if(*s=='\'')return 52;}
    if(!strcmp(s,"Right Shift"))return 229;
    if(!strcmp(s,"F4"))return 61;if(!strcmp(s,"F5"))return 62;if(!strcmp(s,"F6"))return 63;if(!strcmp(s,"F12"))return 69;
    if(!strcmp(s,"F8"))return 65;
    return 0;
}
static int from_key(int key){if(key&0x40000000)return key&0x3fffffff;char name[2]={(char)key,0};return lookup(name);}
static const char *from_sc(int sc){static char name[2];if(sc>=4&&sc<=29){name[0]='A'+sc-4;name[1]=0;return name;}if(sc==61)return "F4";if(sc==62)return "F5";if(sc==63)return "F6";if(sc==65)return "F8";if(sc==69)return "F12";if(sc==229)return "Right Shift";if(sc==55)return ".";if(sc==52)return "'";return "";}
static int forward(struct Stage *st,int down,int key){(void)st;(void)down;(void)key;++forwards;return 1;}
static void window_event(struct Stage *st,int event){(void)st;assert(event==3);}
static void push(struct STDVector *v,void **value){size_t n=(size_t)vector_count(*v,sizeof(void*));void **data=realloc(v->start,(n+1)*sizeof(void*));assert(data);data[n]=*value;v->start=data;v->finish=v->endOfStorage=data+n+1;}
static void add_child(void *parent,void *child){struct UIElementContainer *p=parent;struct UIElement *c=child;c->parent=p;c->stage=p->asUIElement.stage;push(&p->children,&child);}
static void page_ctor(struct KeyRemapPage *p,const int *keys){assert(keys[0]==-1);memset(p,0,sizeof(*p));p->asUIElementContainer.asUIElement.show=1;}
static void set_text(struct Label *l,const char *s){snprintf((char*)l->text.data,24,"%s",s);}
static void label_ctor(struct Label *l,const char *s,const char *font,int size,uint32_t color,int outline,uint32_t oc,int width){
    (void)font;(void)outline;(void)oc;(void)width;memset(l,0,sizeof(*l));l->asUIElement.show=1;l->asUIElement.w=(int)strlen(s)*7;l->asUIElement.h=18;l->fontSize=size;l->color=color;set_text(l,s);
}
static void tab_ctor(struct ClickableLabel *l,const char *s,const char *font,int size,uint32_t color,uint32_t over){memset(l,0,sizeof(*l));label_ctor(&l->asLabel,s,font,size,color,1,0,0);l->color=color;l->overColor=over;}
static void set_color(struct Label *l,uint32_t c){l->color=c;}
static void native_added(struct Controls *c){(void)c;++added_count;}
static void native_removed(struct Controls *c){(void)c;}
static void native_reset(struct Controls *c){(void)c;++resets;}
static int native_keyboard(struct Controls *c,int down,int key){(void)c;(void)down;(void)key;return 0;}
static void switch_tab(struct Tabs *tabs,int page){tabs->selected=page;struct Controls *c=active_controls;c->pageId=page;struct SBPEControlPage *p=controls_page(c);if(page==4)control_page_added(p);else control_page_removed(p);}
static char *contents(void){FILE *f=fopen(config_path,"r");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *text=calloc((size_t)n+1,1);assert(fread(text,1,(size_t)n,f)==(size_t)n);fclose(f);return text;}
static void destroy_children(struct UIElementContainer *p){int n=vector_count(p->children,sizeof(void*));for(int i=0;i<n;++i)free(((void**)p->children.start)[i]);free(p->children.start);}
int main(int argc,char **argv){
    assert(argc==2);char dir[]="/tmp/sbpe-controls-XXXXXX";assert(mkdtemp(dir));snprintf(config_path,sizeof(config_path),"%s/config.ini",dir);
    FILE *source=fopen(argv[1],"r"),*dest=fopen(config_path,"w");assert(source&&dest);int byte;while((byte=fgetc(source))!=EOF)fputc(byte,dest);fclose(source);fputs("\n# Keep this unrelated comment.\n[sbpe_controls]\nfuture_option = keep\n",dest);fclose(dest);
    key_from_name=lookup;get_keys=polled_keys;get_keyboard_focus=no_focus;scancode_from_key=from_key;scancode_name=from_sc;original_stage_keyboard=forward;
    struct Stage stage={0},*st=&stage;stage_ptr=&st;void *window=(void*)123;window_ptr=&window;
    assert(load_config());assert(!strcmp(control_keys[2][0],"R")&&!strcmp(control_keys[3][0],"C"));
    /* Actual movement events already reach Stage while SDL focus is null. */
    event_input_enabled=1;initialized=1;stage_keyboard(&stage,1,'r');assert(highlight_hide&&hide_names&&hide_healthbars&&hide_ally);stage_keyboard(&stage,1,'r');assert(highlight_hide);stage_keyboard(&stage,0,'r');assert(forwards==3);
    stage.textInputTarget=(void*)1;stage_keyboard(&stage,1,'r');stage.textInputTarget=NULL;keyboard();assert(highlight_hide);stage_keyboard(&stage,0,'r');stage_keyboard(&stage,1,'r');assert(!highlight_hide);stage_keyboard(&stage,0,'r');
    stage_keyboard(&stage,1,'c');assert(playerlist_visible);original_resize=window_event;resize_stage(&stage,3);assert(!playerlist_visible);
    control_new=new_object;
    control_page_ctor=page_ctor;control_label_ctor=label_ctor;control_tab_ctor=tab_ctor;control_label_text=set_text;control_label_color=set_color;control_add_child=add_child;
    control_pages_push=push;control_labels_push=push;control_switch_tab=switch_tab;control_original_added=native_added;control_original_removed=native_removed;control_original_keyboard=native_keyboard;control_original_reset=native_reset;
    struct Controls c={0};struct Tabs tabs={0};c.tabs=&tabs;tabs.asUIElementContainer.asUIElement.w=580;
    for(int i=0;i<4;++i){void *page=calloc(1,sizeof(struct KeyRemapPage)),*label=calloc(1,sizeof(struct ClickableLabel));push(&c.pages,&page);push(&tabs.labels,&label);add_child(&tabs,label);}
    controls_added(&c);assert(vector_count(c.pages,8)==5&&vector_count(tabs.labels,8)==5);controls_added(&c);assert(vector_count(c.pages,8)==5&&added_count==2);
    struct SBPEControlPage *p=controls_page(&c);assert(p&&vector_count(p->base.keys,8)==0&&vector_count(p->base.asUIElementContainer.children,8)==CONTROL_COUNT*4+1);
    assert(!strcmp(control_keys[CONTROL_DISCONNECT][0],"F4"));
    for(int a=CONTROL_INVENTORY_FIRST;a<CONTROL_COUNT;++a)for(int k=0;k<3;++k)assert(!strcmp(control_keys[a][k],"None"));
    struct ClickableLabel *tab=((void**)tabs.labels.start)[4];assert(!control_tab_mouse(tab,0,0,1,1)&&!control_tab_mouse(tab,0,0,1,0)&&c.pageId==0);control_tab_mouse(tab,0,0,0,1);control_tab_mouse(tab,0,0,0,0);assert(c.pageId==4);
    stage_keyboard(&stage,1,'r');assert(!highlight_hide);stage_keyboard(&stage,0,'r');
    /* Native coordinates, arrows and all three columns are interactive. */
    p->base.asUIElementContainer.asUIElement.x=60;p->base.asUIElementContainer.asUIElement.y=156;
    assert(!control_page_mouse(p,60+450+10,156+2*24+10,1,1)&&!p->editing);
    assert(control_page_mouse(p,60+450+10,156+2*24+10,0,1)&&p->selected==2&&p->column==2&&p->editing);
    controls_keyboard(&c,0,'h');assert(!strcmp(control_keys[2][2],"None"));controls_keyboard(&c,1,'h');assert(!strcmp(control_keys[2][2],"H"));assert(!p->editing);
    char *text=contents();assert(strstr(text,"future_option = keep")&&strstr(text,"# Keep this unrelated comment.")&&strstr(text,"hide_players_tertiary = H"));free(text);
    /* Duplicate key moves between SBPE actions, including grouped actions. */
    control_select(p,0,1);controls_keyboard(&c,1,13);controls_keyboard(&c,1,'r');assert(!strcmp(control_keys[0][1],"R")&&!strcmp(control_keys[2][0],"None")&&!strcmp(control_keys[2][2],"H"));
    controls_removed(&c);assert(!active_controls);stage_keyboard(&stage,1,'h');assert(highlight_hide&&hide_names&&hide_healthbars&&hide_ally);stage_keyboard(&stage,0,'h');
    stage_keyboard(&stage,1,'r');assert(zoom_width==2000);stage_keyboard(&stage,0,'r');
    active_controls=&c;control_select(p,2,2);control_page_keyboard(p,1,13);control_page_keyboard(p,1,27);assert(!p->editing&&!strcmp(control_keys[2][2],"H"));
    control_page_keyboard(p,1,127);assert(!strcmp(control_keys[2][2],"None"));
    control_page_keyboard(p,1,0x4000004e);assert(p->offset==12&&p->selected==14&&!p->names[0]->asUIElement.show&&p->names[14]->asUIElement.show);control_page_keyboard(p,1,0x4000004b);assert(p->offset==0);
    /* Failed write cannot alter existing bindings or truncate the config. */
    char original_path[4096];copystr(original_path,sizeof(original_path),config_path);snprintf(config_path,sizeof(config_path),"%s/missing/config.ini",dir);control_select(p,0,0);assert(!control_commit(p,lookup("J"))&&!strcmp(control_keys[0][0],"V"));copystr(config_path,sizeof(config_path),original_path);
    controls_reset(&c);assert(resets==1&&!strcmp(control_keys[2][0],"R")&&!strcmp(control_keys[2][2],"None")&&!strcmp(control_keys[0][1],"None"));
    /* Reload and a new Controls instance retain exactly what was saved. */
    control_select(p,3,1);int old_zoom=zoom_width;assert(control_commit(p,lookup("K"))&&zoom_width==old_zoom);assert(load_config()&&!strcmp(control_keys[3][1],"K"));
    controls_removed(&c);stage_keyboard(&stage,1,'c');stage_keyboard(&stage,1,'k');assert(playerlist_visible);stage_keyboard(&stage,0,'c');assert(playerlist_visible);stage_keyboard(&stage,0,'k');assert(!playerlist_visible);
    active_controls=&c;control_select(p,CONTROL_PAUSE,0);assert(control_commit(p,lookup("P")));control_select(p,11,0);assert(control_commit(p,lookup("D")));
    controls_removed(&c);stage_keyboard(&stage,1,'p');assert(paused);stage_keyboard(&stage,0,'p');stage_keyboard(&stage,1,'p');assert(!paused);stage_keyboard(&stage,0,'p');
    stage_keyboard(&stage,1,0x40000000|69);assert(!paused);stage_keyboard(&stage,0,0x40000000|69);
    assert(getbool("plugin_dps_v3","enabled",1));stage_keyboard(&stage,1,'d');assert(!getbool("plugin_dps_v3","enabled",1));stage_keyboard(&stage,0,'d');
    text=contents();const char *entry=strstr(text,"history_secondary = K");assert(entry&&!strstr(entry+1,"history_secondary = K"));assert(strstr(text,"disconnect_hold_primary = F4"));free(text);
    control_select(p,CONTROL_DISCONNECT,0);assert(!control_commit(p,lookup("F8"))&&!strcmp(control_keys[CONTROL_DISCONNECT][0],"F4"));
    assert(control_commit(p,lookup("J"))&&!strcmp(control_keys[CONTROL_DISCONNECT][0],"J"));assert(load_config()&&!strcmp(control_keys[CONTROL_DISCONNECT][0],"J"));
    assert(control_commit(p,lookup("F4"))&&!strcmp(control_keys[CONTROL_DISCONNECT][0],"F4"));
    /* All inventory actions are editable through the actual SBPE page, even
       on its last page. They persist without adding synthetic plugin toggles. */
    control_select(p,CONTROL_COUNT-1,0);assert(p->offset==24&&p->names[CONTROL_COUNT-1]->asUIElement.show);
    assert(!control_commit(p,lookup("F8")));assert(!control_commit(p,lookup("Right Shift")));
    assert(control_commit(p,lookup("Z")));assert(load_config()&&!strcmp(control_keys[CONTROL_COUNT-1][0],"Z"));
    text=contents();assert(strstr(text,"inventory_slot_12_primary = Z"));free(text);
    controls_reset(&c);for(int a=CONTROL_INVENTORY_FIRST;a<CONTROL_COUNT;++a)assert(!strcmp(control_keys[a][0],"None"));
    controls_removed(&c);for(int i=0;i<5;++i){if(i==4)destroy_children(&p->base.asUIElementContainer);free(((void**)c.pages.start)[i]);}free(c.pages.start);destroy_children(&tabs.asUIElementContainer);free(tabs.labels.start);unlink(config_path);rmdir(dir);
    puts("PASS: native Controls tab lifecycle/layout, three key slots, capture/release/cancel/clear, pagination, grouped hide actions, duplicate reassignment, atomic persistence/preservation/failure, defaults/reload, no menu hotkeys, and startup key events with stale SDL focus.");return 0;
}
