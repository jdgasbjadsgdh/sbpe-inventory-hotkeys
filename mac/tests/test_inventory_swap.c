/* Real input/control paths and checked memory reads, with native equip mocked
 * at its call boundary. No running game, sockets or experimental weapon hooks. */
#define SBPE_TEST 1
#define SBPE_CHECKED_READ_TEST 1
#include "../src/sbpe.c"
#include <assert.h>
#include <sys/mman.h>
static struct Stage stage,*current_stage=&stage;
static struct WorldClient wc;static struct ClientWorld cw;static struct ServerConnection conn;
static struct PlayerCharacter pc;static struct Item items[INVENTORY_BIND_SLOTS],equipped[6];
static struct ItemDescription descriptions[INVENTORY_BIND_SLOTS];
static struct Item *bag[INVENTORY_BIND_SLOTS];static uintptr_t pc_table[64];
static const int destinations[]={SLOT_MAIN,SLOT_SEC,SLOT_SPECIAL,SLOT_BODY,SLOT_MOB,SLOT_IMP};
static unsigned requests,forwarded;static int capacity=INVENTORY_BIND_SLOTS,slot_queries,last_from,last_to;
static struct WorldClient *world(void){return &wc;}
static struct Item *get_item(struct PlayerCharacter *p,int slot){
    assert(p==&pc);++slot_queries;assert(slot>=SLOT_INV&&slot<SLOT_INV+INVENTORY_BIND_SLOTS);return bag[slot-SLOT_INV];
}
static int num_slots(struct PlayerCharacter *p,int slot){assert(p==&pc);return slot==SLOT_INV?capacity:1;}
static void modify(struct WorldClient *client,const struct ModifyInv *request){
    assert(client==&wc&&request->classptr==inventory_message_class&&request->command==MODINV_MOVE&&request->_has_bits[0]==7);
    assert(request->slotpos>=SLOT_INV&&request->slotpos<SLOT_INV+INVENTORY_BIND_SLOTS);
    ++requests;last_from=request->slotpos;last_to=request->targetslotpos;
    /* Emulate the normal native swap so a held key/repeat would visibly swap
       back if the edge detector incorrectly sent another request. */
    int to=-1;for(int i=0;i<6;++i)if(destinations[i]==last_to)to=i;assert(to>=0);
    struct Item *item=bag[last_from-SLOT_INV];bag[last_from-SLOT_INV]=&equipped[to];
    equipped[to].user=&pc;equipped[to].desc=item->desc;equipped[to].slotPos=last_from;
}
static int lookup(const char *s){
    if(!strcmp(s,"None"))return 0;
    if(strlen(s)==1&&isalpha((unsigned char)*s))return toupper((unsigned char)*s)-'A'+4;
    if(!strcmp(s,"F8"))return 65;if(!strcmp(s,"F9"))return 66;if(!strcmp(s,"F10"))return 67;
    return 0;
}
static int scancode(int key){return key;}
static int native_key(struct Stage *s,int down,int key){(void)s;(void)down;(void)key;++forwarded;return 1;}
static void reset(void){
    memset(&wc,0,sizeof(wc));memset(&cw,0,sizeof(cw));memset(&conn,0,sizeof(conn));memset(&pc,0,sizeof(pc));
    memset(&loading_probe,0,sizeof(loading_probe));memset(&probe_camera,0,sizeof(probe_camera));memset(&stage,0,sizeof(stage));
    memset(inventory_owned,0,sizeof(inventory_owned));memset(event_keys,0,sizeof(event_keys));memset(control_previous,0,sizeof(control_previous));
    memset(pc_table,0,sizeof(pc_table));pc_table[0x1e8/8]=(uintptr_t)get_item;pc_table[0x1e0/8]=(uintptr_t)num_slots;
    ((struct WorldObject*)&pc)->classptr=pc_table;((struct WorldObject*)&pc)->props.hitpoints=100;((struct WorldObject*)&pc)->props.maxhitpoints=100;
    wc.clientWorld=&cw;wc.serverConnection=&conn;cw.player=(void*)&pc;cw.mySubWorld=(void*)1;conn.state=CONNECTED;conn.socket=20;
    inventory_world=world;inventory_get_item=get_item;inventory_num_slots=num_slots;inventory_modify=modify;inventory_message_class=(void*)0x12340000;
    for(int a=0;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c)copystr(control_keys[a][c],64,"None");
    for(int i=0;i<INVENTORY_BIND_SLOTS;++i){
        items[i]=(struct Item){.user=&pc,.desc=&descriptions[i],.slotPos=SLOT_INV+i};
        descriptions[i]=(struct ItemDescription){.slot=destinations[i%6]};bag[i]=&items[i];
        char key[2]={(char)('A'+i),0};copystr(control_keys[CONTROL_INVENTORY_FIRST+i][0],64,key);
    }
    key_from_name=lookup;scancode_from_key=scancode;original_stage_keyboard=native_key;
    stage_ptr=&current_stage;active_controls=NULL;paused=0;event_input_enabled=1;nbindings=0;
    requests=forwarded=slot_queries=0;capacity=INVENTORY_BIND_SLOTS;
    set_option("plugin_loading_probe","enabled","no");set_option("plugin_reconnect_lab","enabled","no");
    set_option("plugin_inventory_swap","enabled","yes");
}
static void press(int i){stage_keyboard(&stage,1,4+i);}
static void release(int i){stage_keyboard(&stage,0,4+i);}
static void no_request(void){assert(!inventory_equip(0)&&!requests);}
static void test_swaps(void){
    reset();for(int i=0;i<INVENTORY_BIND_SLOTS;++i){
        unsigned before=requests;press(i);assert(requests==before+1&&last_from==SLOT_INV+i&&last_to==destinations[i%6]);
        press(i);keyboard();assert(requests==before+1);release(i);
    }
    assert(!forwarded); /* Configured keys are paired, consumed native events. */
    reset();bag[0]=NULL;press(0);release(0);assert(!requests);
    reset();capacity=1;press(1);release(1);assert(!requests&&!slot_queries);
    reset();assert(!inventory_equip(-1)&&!inventory_equip(INVENTORY_BIND_SLOTS)&&!requests);
    reset();descriptions[0].slot=SLOT_INV;no_request();descriptions[0].slot=SLOT_HOME;no_request();
    reset();items[0].user=NULL;no_request();reset();items[0].slotPos=SLOT_MAIN;no_request();
}
static void test_input(void){
    reset();stage.textInputTarget=(void*)1;press(0);assert(!requests&&forwarded==1);stage.textInputTarget=NULL;
    keyboard();press(0);assert(!requests);release(0);press(0);assert(requests==1);release(0);
    reset();paused=1;press(0);paused=0;keyboard();assert(!requests);release(0);press(0);assert(requests==1);release(0);
    reset();active_controls=(void*)1;press(0);active_controls=NULL;keyboard();assert(!requests);release(0);press(0);assert(requests==1);release(0);
    reset();event_keys[227]=1;press(0);assert(!requests&&forwarded==1);event_keys[227]=0;keyboard();assert(!requests);release(0);press(0);assert(requests==1);release(0);
    reset();inventory_keys(event_keys,512,0);event_keys[4]=1;inventory_keys(event_keys,512,0);inventory_keys(event_keys,512,1);assert(!requests);
    event_keys[4]=0;inventory_keys(event_keys,512,1);event_keys[4]=1;inventory_keys(event_keys,512,1);assert(requests==1);
    reset();copystr(control_keys[CONTROL_INVENTORY_FIRST][1],64,"Z");press(0);press(25);assert(requests==1);
    release(0);press(25);assert(requests==1);release(25);press(25);assert(requests==2);release(25);
    reset();press(0);set_option("plugin_inventory_swap","enabled","no");release(0);assert(!forwarded&&!inventory_owned[4]);
    reset();copystr(control_keys[CONTROL_INVENTORY_FIRST][0],64,"F8");stage_keyboard(&stage,1,65);assert(!requests);stage_keyboard(&stage,0,65);
}
static void test_context(void){
    int states[]={PROBE_HOLD,PROBE_WAIT,PROBE_RECONNECT};
    for(unsigned i=0;i<sizeof(states)/sizeof(*states);++i){reset();loading_probe.state=states[i];no_request();}
    reset();loading_probe.asset_finish=1;no_request();reset();probe_camera.active=1;no_request();
    reset();wc.overlay=(void*)1;no_request();reset();conn.state=ERRORED;no_request();reset();conn.socket=-1;no_request();
    reset();cw.player=NULL;no_request();reset();cw.mySubWorld=NULL;no_request();reset();((struct WorldObject*)&pc)->props.hitpoints=0;no_request();
    reset();((struct WorldObject*)&pc)->classptr=NULL;no_request();reset();pc_table[0x1e8/8]=1;no_request();
    reset();loading_probe.state=PROBE_ARMED;assert(inventory_equip(0)&&requests==1); /* Current spawned room is still ordinary play. */
    size_t page=(size_t)sysconf(_SC_PAGESIZE);void *bad=mmap(NULL,page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(bad!=MAP_FAILED);
    reset();bag[0]=bad;no_request();reset();items[0].desc=bad;no_request();reset();wc.clientWorld=bad;no_request();
    reset();cw.player=bad;no_request();reset();((struct WorldObject*)&pc)->classptr=bad;no_request();assert(!munmap(bad,page));
}
int main(void){test_swaps();test_input();test_context();puts("PASS: 12 bag keys, native typed swap requests, empty/locked/incompatible slots, paired/repeat/alternative input, chat/menu/pause/modifier/focus guards, held/reconnecting/dead player guards, and real unreadable-pointer rejection.");return 0;}
