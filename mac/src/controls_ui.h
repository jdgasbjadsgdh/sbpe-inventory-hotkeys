/* Native Controls extension for the UUID-pinned Mac client. Construct actual
 * libc++ UI objects: never copy owning strings, vectors, or Handler objects. */
_Static_assert(sizeof(struct KeyRemapPage)==0x68,"Controls page ABI");
_Static_assert(sizeof(struct Label)==0xa0,"Label ABI");
_Static_assert(sizeof(struct ClickableLabel)==0xd0,"Tab label ABI");
_Static_assert(offsetof(struct Controls,tabs)==0xc0,"Controls tabs ABI");
_Static_assert(offsetof(struct Controls,pages)==0xc8,"Controls pages ABI");
_Static_assert(offsetof(struct Tabs,labels)==0x78,"Tab labels ABI");
_Static_assert(offsetof(struct Tabs,selected)==0x90,"Tab selection ABI");
#define CONTROL_ROWS 12
struct SBPEControlPage {
    struct KeyRemapPage base;
    struct Label *names[CONTROL_COUNT],*keys[CONTROL_COUNT][3],*footer;
    int selected,column,editing,offset;
    char status[128];
};
static uintptr_t control_page_vtable[17],control_tab_vtable[17];
static void *(*control_new)(size_t);
static void (*control_page_ctor)(struct KeyRemapPage*,const int*);
static void (*control_label_ctor)(struct Label*,const char*,const char*,int,uint32_t,int,uint32_t,int);
static void (*control_tab_ctor)(struct ClickableLabel*,const char*,const char*,int,uint32_t,uint32_t);
static void (*control_label_text)(struct Label*,const char*);
static void (*control_label_color)(struct Label*,uint32_t);
static void (*control_add_child)(void*,void*);
static void (*control_container_draw)(void*,int);
static void (*control_fill)(void*,int,int,int,int,uint32_t);
static void (*control_pages_push)(struct STDVector*,void**);
static void (*control_labels_push)(struct STDVector*,void**);
static void (*control_switch_tab)(struct Tabs*,int);
static void (*control_original_added)(struct Controls*);
static void (*control_original_removed)(struct Controls*);
static int (*control_original_keyboard)(struct Controls*,int,int);
static void (*control_original_reset)(struct Controls*);
static struct SBPEControlPage *controls_page(struct Controls *c){
    int n=vector_count(c->pages,sizeof(void*));if(!n)return NULL;struct SBPEControlPage *p=((void**)c->pages.start)[n-1];
    return p->base.asUIElementContainer.asUIElement.classptr==&control_page_vtable[2]?p:NULL;
}
static int control_global_coord(struct UIElement *e,int y){
    int result=0;for(int depth=0;e&&depth<32;++depth,e=(void*)e->parent)result+=y?e->y:e->x;return result;
}
static void control_refresh(struct SBPEControlPage *p){
    for(int a=0;a<CONTROL_COUNT;++a){int row=a-p->offset,show=row>=0&&row<CONTROL_ROWS;
        p->names[a]->asUIElement.show=(bool)show;p->names[a]->asUIElement.y=row*24;
        for(int c=0;c<3;++c){struct Label *l=p->keys[a][c];l->asUIElement.show=(bool)show;l->asUIElement.y=row*24;
            control_label_text(l,p->editing&&p->selected==a&&p->column==c?"Press a key...":control_keys[a][c]);
            control_label_color(l,p->editing&&p->selected==a&&p->column==c?0xffffffff:0xff07d8bb);
        }
    }
    char footer[200];snprintf(footer,sizeof(footer),"< Prev   %d / %d   Next >  |  %.58s",p->offset/CONTROL_ROWS+1,(CONTROL_COUNT+CONTROL_ROWS-1)/CONTROL_ROWS,p->status[0]?p->status:"Enter: bind   Backspace: clear   Esc: cancel");
    control_label_text(p->footer,footer);
}
static struct Label *control_label(struct SBPEControlPage *p,const char *text,int x,int y,uint32_t color){
    struct Label *l=control_new(sizeof(*l));control_label_ctor(l,text,"TenbyFive",15,color,1,0xff000000,0);
    l->asUIElement.x=x;l->asUIElement.y=y;control_add_child(p,l);return l;
}
static void control_page_added(struct SBPEControlPage *p){p->editing=0;p->status[0]=0;control_refresh(p);}
static void control_page_removed(struct SBPEControlPage *p){p->editing=0;}
static void control_page_draw(struct SBPEControlPage *p,int time){
    if(p->selected>=p->offset&&p->selected<p->offset+CONTROL_ROWS)
        control_fill(p,150+p->column*150,(p->selected-p->offset)*24+2,130,20,0x1f07d8bb);
    control_container_draw(p,time);
}
static void control_select(struct SBPEControlPage *p,int row,int column){
    p->selected=(int)clamp(row,0,CONTROL_COUNT-1);p->column=(column+3)%3;
    p->offset=(p->selected/CONTROL_ROWS)*CONTROL_ROWS;p->editing=0;p->status[0]=0;control_refresh(p);
}
static int control_commit(struct SBPEControlPage *p,int sc){
    const char *name=sc?scancode_name(sc):"None";if(!name||!*name||strlen(name)>=64||!controls_key_valid(name)){
        copystr(p->status,sizeof(p->status),"That key is unavailable. Choose another key.");control_refresh(p);return 0;}
    if(p->selected==CONTROL_DISCONNECT&&controls_disconnect_reserved(sc)){
        copystr(p->status,sizeof(p->status),"That key is reserved for loading controls. Choose another.");control_refresh(p);return 0;
    }
    if(controls_inventory_action(p->selected)&&controls_inventory_reserved(sc)){
        copystr(p->status,sizeof(p->status),"Choose a key other than loading or modifier keys.");control_refresh(p);return 0;
    }
    char keys[CONTROL_COUNT][3][64];memcpy(keys,control_keys,sizeof(keys));
    /* One key triggers one SBPE action. Moving it also moves all grouped hide
       actions; normal game bindings can intentionally share that key. */
    int reassigned=0;
    for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c)
        if(sc&&key_from_name(keys[a][c])==sc&&(a!=p->selected||c!=p->column)){copystr(keys[a][c],64,"None");reassigned=1;}
    copystr(keys[p->selected][p->column],64,name);
    if(!controls_save(keys)){copystr(p->status,sizeof(p->status),"Save failed; previous keys kept.");control_refresh(p);return 0;}
    controls_apply_saved(keys);p->editing=0;
    copystr(p->status,sizeof(p->status),reassigned?"Saved. Key moved from its previous SBPE action.":"Saved.");control_refresh(p);return 1;
}
static int control_page_keyboard(struct SBPEControlPage *p,int down,int key){
    if(!down)return p->editing; /* Never bind on release. */
    if(p->editing){
        if(key==27){p->editing=0;p->status[0]=0;control_refresh(p);return 1;}
        if(key==8||key==127){control_commit(p,0);return 1;}
        int sc=scancode_from_key?scancode_from_key(key):0;
        if(sc>0)control_commit(p,sc);return 1;
    }
    switch(key){
        case 13:p->editing=1;p->status[0]=0;control_refresh(p);return 1;
        case 8:case 127:control_commit(p,0);return 1;
        case 0x40000052:control_select(p,p->selected-1,p->column);return 1;
        case 0x40000051:control_select(p,p->selected+1,p->column);return 1;
        case 0x40000050:control_select(p,p->selected,p->column+2);return 1;
        case 0x4000004f:control_select(p,p->selected,p->column+1);return 1;
        case 0x4000004b:control_select(p,p->selected-CONTROL_ROWS,p->column);return 1;
        case 0x4000004e:control_select(p,p->selected+CONTROL_ROWS,p->column);return 1;
    }return 0;
}
static int control_page_mouse(struct SBPEControlPage *p,int x,int y,int button,int down){
    /* XDL translates SDL's left button (1) into game button 0. */
    if(button!=0||!down)return 0;struct UIElement *e=(void*)p;
    x-=control_global_coord(e,0);y-=control_global_coord(e,1);
    if(y>=CONTROL_ROWS*24+6&&y<CONTROL_ROWS*24+28){
        if(x>=0&&x<65)control_select(p,p->selected-CONTROL_ROWS,p->column);
        else if(x>=105&&x<165)control_select(p,p->selected+CONTROL_ROWS,p->column);return 1;
    }
    if(y<0||y>=CONTROL_ROWS*24||x<150||x>=580||(x-150)%150>=130)return 0;
    int row=p->offset+y/24;if(row>=CONTROL_COUNT)return 0;
    control_select(p,row,(x-150)/150);p->editing=1;control_refresh(p);return 1;
}
static int control_tab_mouse(struct ClickableLabel *l,int x,int y,int button,int down){
    (void)x;(void)y;if(button!=0)return 0;if(down){l->mouseDown=1;return 1;}
    if(!l->mouseDown)return 0;l->mouseDown=0;
    struct Tabs *tabs=(void*)l->asLabel.asUIElement.parent;if(!tabs)return 0;
    int n=vector_count(tabs->labels,sizeof(void*));for(int i=0;i<n;++i)if(((void**)tabs->labels.start)[i]==l){control_switch_tab(tabs,i);return 1;}return 0;
}
static void controls_added(struct Controls *c){
    control_original_added(c);active_controls=c;
    struct SBPEControlPage *p=controls_page(c);if(p)return;
    int pages=vector_count(c->pages,sizeof(void*)),labels=vector_count(c->tabs->labels,sizeof(void*));
    if(pages!=4||labels!=4){LOG("Controls layout mismatch: SBPE tab skipped.");return;}
    p=control_new(sizeof(*p));memset(p,0,sizeof(*p));const int empty[]={-1};control_page_ctor(&p->base,empty);
    struct UIElement *e=(void*)p;e->classptr=&control_page_vtable[2];e->x=60;e->y=156;e->w=580;e->h=320;
    for(int a=0;a<CONTROL_COUNT;++a){p->names[a]=control_label(p,control_actions[a].label,0,0,0xffffffff);
        for(int col=0;col<3;++col)p->keys[a][col]=control_label(p,control_keys[a][col],150+col*150,0,0xff07d8bb);}
    p->footer=control_label(p,"",0,CONTROL_ROWS*24+6,0xff07d8bb);control_refresh(p);
    struct ClickableLabel *label=control_new(sizeof(*label));control_tab_ctor(label,"SBPE","HemiHeadBold",14,0xffffffff,0xffff9b00);
    label->asLabel.asUIElement.classptr=&control_tab_vtable[2];void *page_value=p,*label_value=label;
    control_pages_push(&c->pages,&page_value);control_labels_push(&c->tabs->labels,&label_value);control_add_child(c->tabs,label);
    int n=labels+1,width=c->tabs->asUIElementContainer.asUIElement.w/n;
    for(int i=0;i<n;++i){struct UIElement *l=((void**)c->tabs->labels.start)[i];l->x=i*width+width/2-l->w/2;l->y=10;}
    LOG("Controls > SBPE tab added: %d actions, three key slots, saved to config.ini.",CONTROL_COUNT);
}
static void controls_removed(struct Controls *c){if(active_controls==c)active_controls=NULL;control_original_removed(c);}
static int controls_keyboard(struct Controls *c,int down,int key){
    struct SBPEControlPage *p=controls_page(c);
    if(p&&c->pageId==vector_count(c->pages,sizeof(void*))-1&&control_page_keyboard(p,down,key))return 1;
    return control_original_keyboard(c,down,key);
}
static void controls_reset(struct Controls *c){
    control_original_reset(c);struct SBPEControlPage *p=controls_page(c);if(!p)return;
    char keys[CONTROL_COUNT][3][64];const char *defaults[]={"V","T","R","C","Right Shift","'",".","L","F6","F5","F12"};
    for(int a=0;a<CONTROL_COUNT;++a)for(int col=0;col<3;++col)copystr(keys[a][col],64,!col?(a<11?defaults[a]:control_actions[a].shortcut?control_actions[a].shortcut:"None"):"None");
    if(controls_save(keys)){controls_apply_saved(keys);p->editing=0;copystr(p->status,sizeof(p->status),"Default SBPE bindings restored and saved.");}
    else copystr(p->status,sizeof(p->status),"StarBreak reset; SBPE bindings could not be saved.");control_refresh(p);
}
#ifndef SBPE_TEST
static void install_controls_ui(void){
    uintptr_t *table=(void*)PTR(0x1001eba80);
    if(table[6]!=PTR(0x1000addd0)||table[7]!=PTR(0x10006c840)||table[8]!=PTR(0x1000adeb0)||table[17]!=PTR(0x1000adf80)){
        LOG("Controls vtable mismatch: SBPE tab disabled.");return;}
    memcpy(control_page_vtable,(void*)PTR(0x1001ec290),sizeof(control_page_vtable));
    memcpy(control_tab_vtable,(void*)PTR(0x1001e9a60),sizeof(control_tab_vtable));
    control_page_vtable[5]=(uintptr_t)control_page_draw;control_page_vtable[6]=(uintptr_t)control_page_added;
    control_page_vtable[7]=(uintptr_t)control_page_removed;control_page_vtable[8]=(uintptr_t)control_page_keyboard;control_page_vtable[12]=(uintptr_t)control_page_mouse;
    control_tab_vtable[12]=(uintptr_t)control_tab_mouse;
    control_new=(void*)PTR(0x10018f7d0);control_page_ctor=(void*)PTR(0x1000b38d0);
    control_label_ctor=(void*)PTR(0x1000726b0);control_tab_ctor=(void*)PTR(0x10006dd40);
    control_label_text=(void*)PTR(0x100072a70);control_label_color=(void*)PTR(0x1000729b0);
    control_add_child=(void*)PTR(0x100077e60);control_container_draw=(void*)PTR(0x100078210);control_fill=(void*)PTR(0x100077bb0);
    control_pages_push=(void*)PTR(0x10018ef96);control_labels_push=(void*)PTR(0x10018efba);control_switch_tab=(void*)PTR(0x1000b60c0);
    control_original_added=(void*)table[6];control_original_removed=(void*)table[7];control_original_keyboard=(void*)table[8];control_original_reset=(void*)table[17];
    table[6]=(uintptr_t)controls_added;table[7]=(uintptr_t)controls_removed;table[8]=(uintptr_t)controls_keyboard;table[17]=(uintptr_t)controls_reset;
    LOG("Native Controls extension installed; SBPE settings tab available on next open.");
}
#endif
