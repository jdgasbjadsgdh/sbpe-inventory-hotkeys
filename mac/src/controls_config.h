/* Shared action definitions for the native Controls page and runtime bindings.
 * The game's InputMapping enum is deliberately not extended: the server never
 * sees these local actions, and ordinary StarBreak bindings stay in Settings. */
struct ControlAction {const char *id,*label,*spec[4],*shortcut;};
#define INVENTORY_BIND_SLOTS 12
enum {CONTROL_EXTRA=8,CONTROL_RELOAD,CONTROL_PAUSE,CONTROL_DISCONNECT=16,CONTROL_INVENTORY_FIRST};
static const struct ControlAction control_actions[]={
    {"zoom","Zoom cycle",{"zoom level toggle 2000 2400"}},
    {"zoom_far","Far zoom",{"zoom level toggle 6000"}},
    {"hide_players","Hide players",{"noise hide_names toggle yes no","noise hide_healthbars toggle yes no","noise hide_ally_objects toggle yes no","highlight_player3 hide_players toggle yes no"}},
    {"history","Player history (hold)",{"playerlist visible hold yes no"}},
    {"map","Room map (hold)",{"map visible hold yes no"}},
    {"health","HP / armor",{"extra_info show_hp toggle yes no"}},
    {"chat","Hide chat",{"noise hide_chat toggle yes no"}},
    {"camera","Center camera",{"misc centered toggle 1 2"}},
    {"extra","Extra information",{NULL},"F6"},
    {"reload","Reload SBPE settings",{NULL},"F5"},
    {"pause","Pause / resume SBPE",{NULL},"F12"},
    {"dps","DPS overlay",{"dps_v3 enabled toggle yes no"}},
    {"totals","Damage totals",{"bv1totaldamage visible toggle yes no"}},
    {"mini_hud","Mini HUD",{"more_misc_b1_11_0 show_mini_hud toggle yes no"}},
    {"chests","Chest contents",{"sb_chest_display scd_display_box toggle yes no"}},
    {"squids","Squid prediction",{"squid_prediction enable toggle yes no"}},
    {"disconnect_hold","Emergency hold",{NULL},"F4"},
#define INVENTORY_CONTROL(n,key) {"inventory_slot_" #n,"Equip bag slot " #n,{NULL},key}
    /* INVENTORY HOTKEY DEFAULTS — edit the second argument, e.g. "Z".
       "None" means unbound. Rebuild with BUILD.command after editing C.
       Saved Controls > SBPE choices in config.ini override these defaults.
       Slot 1 is top-left; count across the three columns, then down. */
    INVENTORY_CONTROL(1,"None"),INVENTORY_CONTROL(2,"None"),INVENTORY_CONTROL(3,"None"),
    INVENTORY_CONTROL(4,"None"),INVENTORY_CONTROL(5,"None"),INVENTORY_CONTROL(6,"None"),
    INVENTORY_CONTROL(7,"None"),INVENTORY_CONTROL(8,"None"),INVENTORY_CONTROL(9,"None"),
    INVENTORY_CONTROL(10,"None"),INVENTORY_CONTROL(11,"None"),INVENTORY_CONTROL(12,"None"),
#undef INVENTORY_CONTROL
};
#define CONTROL_COUNT ((int)(sizeof(control_actions)/sizeof(*control_actions)))
static const char *control_columns[]={"primary","secondary","tertiary"};
static char control_keys[CONTROL_COUNT][3][64];
static int control_previous[CONTROL_COUNT][3];
static const uint8_t *input_keys(int *n){
    if(event_input_enabled){*n=(int)sizeof(event_keys);return event_keys;}
    return get_keys?get_keys(n):NULL;
}
static int controls_match(const struct Binding *b,const char *spec){
    char s[512];int used=snprintf(s,sizeof(s),"%s %s %s",b->plugin,b->option,b->kind);
    for(int i=0;i<b->nvalues&&used>0&&(size_t)used<sizeof(s);++i)used+=snprintf(s+used,sizeof(s)-(size_t)used," %s",b->values[i]);
    return !strcmp(s,spec);
}
static int controls_action_match(const struct Binding *b,int a){
    for(int j=0;j<4&&control_actions[a].spec[j];++j)if(controls_match(b,control_actions[a].spec[j]))return 1;return 0;
}
static void controls_key_option(char *out,size_t n,int action,int column){snprintf(out,n,"%s_%s",control_actions[action].id,control_columns[column]);}
static int controls_key_valid(const char *key){return !strcmp(key,"None")||(key_from_name&&key_from_name(key)>0);}
static int controls_disconnect_reserved(int sc){
    if(!key_from_name||sc<=0)return 0;
    return sc==key_from_name("F8")||sc==key_from_name("F9")||sc==key_from_name("F10")||
        (getbool("native","debug_shortcuts",0)&&sc==key_from_name("F7"));
}
static int controls_inventory_action(int a){return a>=CONTROL_INVENTORY_FIRST&&a<CONTROL_INVENTORY_FIRST+INVENTORY_BIND_SLOTS;}
static int controls_inventory_reserved(int sc){return controls_disconnect_reserved(sc)||(sc>=224&&sc<=231);}
static void controls_load(void){
    memset(control_keys,0,sizeof(control_keys));
    if(!find_option("plugin_dps_v3","enabled"))set_option("plugin_dps_v3","enabled","yes");
    for(int a=0;a<CONTROL_COUNT;++a){
        int slot=0,overridden=0;
        if(control_actions[a].shortcut)copystr(control_keys[a][slot++],64,control_actions[a].shortcut);
        else for(int i=0;i<nbindings&&slot<3;++i)if(controls_action_match(&bindings[i],a)){
            int duplicate=0;for(int j=0;j<slot;++j)if(!strcasecmp(control_keys[a][j],bindings[i].key))duplicate=1;
            if(!duplicate)copystr(control_keys[a][slot++],64,bindings[i].key);
        }
        for(int c=0;c<3;++c){char k[96];controls_key_option(k,sizeof(k),a,c);const char *v=getoptv("sbpe_controls",k,NULL);
            if(!control_keys[a][c][0])copystr(control_keys[a][c],64,"None");
            if(v&&controls_key_valid(v)){copystr(control_keys[a][c],64,v);overridden=1;}
        }
        if(overridden&&!control_actions[a].shortcut){
            for(int i=0;i<nbindings;)if(controls_action_match(&bindings[i],a)){memmove(bindings+i,bindings+i+1,(size_t)(--nbindings-i)*sizeof(*bindings));}else ++i;
            for(int c=0;c<3;++c)if(strcmp(control_keys[a][c],"None"))
                for(int j=0;j<4&&control_actions[a].spec[j];++j)parse_binding(control_actions[a].spec[j],control_keys[a][c]);
        }
    }
    int n=0;const uint8_t *keys=input_keys(&n);
    for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c){int sc=key_from_name&&strcmp(control_keys[a][c],"None")?key_from_name(control_keys[a][c]):0;control_previous[a][c]=keys&&sc>0&&sc<n&&keys[sc];}
}
static void controls_seed_bindings(void){
    int count=0;const uint8_t *keys=input_keys(&count);
    if(initialized&&keys)for(int i=0;i<nbindings;++i)if(strcmp(bindings[i].kind,"hold"))bindings[i].previous=bindings[i].scancode<count&&keys[bindings[i].scancode];
}
static void controls_apply_saved(char keys[CONTROL_COUNT][3][64]){
    /* Rebinding must preserve current zoom, visibility toggles and other live
       plugin values. A full F5 configuration reload remains a separate action. */
    for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c){char k[96];controls_key_option(k,sizeof(k),a,c);set_option("sbpe_controls",k,keys[a][c]);}
    nbindings=0;for(int i=0;i<noptions;++i)if(!strcmp(options[i].section,"plugin_keybinds")&&strchr(options[i].key,' '))parse_binding(options[i].key,options[i].value);
    controls_load();controls_seed_bindings();
}
static int controls_shortcut(const uint8_t *keys,int n,int action,int allowed){
    int pressed=0;for(int c=0;c<3;++c){int sc=strcmp(control_keys[action][c],"None")?key_from_name(control_keys[action][c]):0;int down=sc>0&&sc<n&&keys[sc];
        pressed|=allowed&&down&&!control_previous[action][c];control_previous[action][c]=down;}return pressed;
}
static int controls_managed_key(const char *key){
    for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c){char k[96];controls_key_option(k,sizeof(k),a,c);if(!strcmp(k,key))return 1;}return 0;
}
static int controls_write_values(FILE *f,char keys[CONTROL_COUNT][3][64]){
    for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c){char k[96];controls_key_option(k,sizeof(k),a,c);if(fprintf(f,"%s = %s\n",k,keys[a][c])<0)return 0;}return 1;
}
/* Replace only our managed entries; preserve every unrelated option/comment.
 * Use a same-directory temporary file so a failed write leaves config intact. */
static int controls_save(char keys[CONTROL_COUNT][3][64]){
    for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c)if(!controls_key_valid(keys[a][c]))return 0;
    int needed=0;for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c){char k[96];controls_key_option(k,sizeof(k),a,c);needed+=!find_option("sbpe_controls",k);}
    if(noptions+needed>OPT_LIMIT)return 0;
    struct stat st;if(stat(config_path,&st)||st.st_size>2*1024*1024)return 0;
    FILE *in=fopen(config_path,"r");if(!in)return 0;
    char path[sizeof(config_path)+32];snprintf(path,sizeof(path),"%s.controls.XXXXXX",config_path);int fd=mkstemp(path);
    if(fd<0){fclose(in);return 0;}FILE *out=fdopen(fd,"w");if(!out){close(fd);unlink(path);fclose(in);return 0;}
    int ok=fchmod(fd,st.st_mode&0777)==0,inside=0,written=0;char *line=NULL;size_t capacity=0;ssize_t length;int last_newline=1;
    while(ok&&(length=getline(&line,&capacity,in))>=0){
        char *copy=strdup(line);if(!copy){ok=0;break;}char *s=trim(copy);int skip=0;
        if(*s=='['){char *e=strchr(s,']');if(e)*e=0;inside=e&&!strcmp(s+1,"sbpe_controls");
            if(inside&&!written){ok=fwrite(line,1,(size_t)length,out)==(size_t)length;if(ok&&line[length-1]!='\n')ok=fputc('\n',out)!=EOF;
                if(ok)ok=controls_write_values(out,keys);written=1;skip=1;last_newline=1;}
        }else if(inside&&*s!='#'&&*s!=';'){char *e=strchr(s,'=');if(e){*e=0;skip=controls_managed_key(trim(s));}}
        if(!skip){ok=fwrite(line,1,(size_t)length,out)==(size_t)length;last_newline=length&&line[length-1]=='\n';}free(copy);
    }
    if(ferror(in))ok=0;free(line);fclose(in);
    if(ok&&!written){if(!last_newline)ok=fputc('\n',out)!=EOF;if(ok)ok=fputs("\n[sbpe_controls]\n",out)!=EOF;if(ok)ok=controls_write_values(out,keys);}
    if(fflush(out)||fsync(fd))ok=0;if(fclose(out))ok=0;
    if(ok&&rename(path,config_path)==0)return 1;unlink(path);return 0;
}
