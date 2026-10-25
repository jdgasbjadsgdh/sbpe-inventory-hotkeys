/* Bag hotkeys use the game's ordinary inventory request. No retained item
 * pointers, direct equip mutation, cooldown changes or test-plugin hooks. */
#ifndef SBPE_INVENTORY_SWAP_H
#define SBPE_INVENTORY_SWAP_H
_Static_assert(offsetof(struct Item,user)==8&&offsetof(struct Item,desc)==0x10&&offsetof(struct Item,slotPos)==0x58,"Inventory item ABI");
_Static_assert(offsetof(struct ItemDescription,slot)==0x2c,"Inventory destination ABI");
_Static_assert(sizeof(struct ModifyInv)==32&&offsetof(struct ModifyInv,_has_bits)==0x18,"Inventory request ABI");
_Static_assert(CONTROL_COUNT==CONTROL_INVENTORY_FIRST+INVENTORY_BIND_SLOTS,"Inventory control range");
static struct WorldClient *(*inventory_world)(void)=world_client;
static struct Item *(*inventory_get_item)(struct PlayerCharacter*,int);
static int (*inventory_num_slots)(struct PlayerCharacter*,int);
static void (*inventory_modify)(struct WorldClient*,const struct ModifyInv*);
static void *inventory_message_class;
static uint8_t inventory_owned[512];
static int inventory_enabled(void){return inventory_get_item&&inventory_num_slots&&inventory_modify&&inventory_message_class&&getbool("plugin_inventory_swap","enabled",1);}
static int inventory_modifiers(const uint8_t *keys,int n){
    const int modifiers[]={224,226,227,228,230,231};
    for(unsigned i=0;i<sizeof(modifiers)/sizeof(*modifiers);++i)if(modifiers[i]<n&&keys[modifiers[i]])return 1;
    return 0;
}
static int inventory_key_matches(int sc){
    if(!key_from_name||sc<=0||controls_inventory_reserved(sc))return 0;
    for(int a=CONTROL_INVENTORY_FIRST;a<CONTROL_COUNT;++a)for(int c=0;c<3;++c)
        if(sc==key_from_name(control_keys[a][c]))return 1;
    return 0;
}
static int inventory_equip(int slot){
    if(slot<0||slot>=INVENTORY_BIND_SLOTS||!inventory_enabled()||paused||active_controls||
       loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT||loading_probe.state==PROBE_RECONNECT||
       loading_probe.asset_finish||probe_camera.active)return 0;
    struct Stage *st=stage_ptr?*stage_ptr:NULL;if(!st||st->textInputTarget)return 0;
    struct WorldClient *wc=inventory_world(),client;struct ClientWorld world;struct ServerConnection connection;
    if(!read_memory(wc,&client,sizeof(client))||client.overlay||!read_memory(client.clientWorld,&world,sizeof(world))||
       !read_memory(client.serverConnection,&connection,sizeof(connection))||connection.state!=CONNECTED||connection.socket<0||
       !world.player||!world.mySubWorld)return 0;
    struct PlayerCharacter *pc=(void*)world.player;struct Properties props;void **table;uintptr_t get_item,num_slots;
    if(!read_memory(pc,&table,sizeof(table))||!table||!read_memory(table+0x1e8/8,&get_item,8)||!read_memory(table+0x1e0/8,&num_slots,8)||
       get_item!=(uintptr_t)inventory_get_item||num_slots!=(uintptr_t)inventory_num_slots||
       !read_memory(&((struct WorldObject*)pc)->props,&props,sizeof(props))||props.hitpoints<=0||props.maxhitpoints<=0)return 0;
    /* getNumPos is also what InventoryWindow uses; locked slots stay inert.
       The bag is three columns, ordered left-to-right and then top-to-bottom. */
    int count=inventory_num_slots(pc,SLOT_INV);if(count<=slot||count>256)return 0;
    int from=SLOT_INV+slot;struct Item *item=inventory_get_item(pc,from);uint8_t header[0x5c];
    if(!read_memory(item,header,sizeof(header)))return 0;
    struct PlayerCharacter *owner;struct ItemDescription *description;int stored;uint32_t target;
    memcpy(&owner,header+8,8);memcpy(&description,header+0x10,8);memcpy(&stored,header+0x58,4);
    if(owner!=pc||stored!=from||!description||
       (uintptr_t)description>UINTPTR_MAX-0x30||!read_memory((void*)((uintptr_t)description+0x2c),&target,4))return 0;
    if(target!=SLOT_MAIN&&target!=SLOT_SEC&&target!=SLOT_SPECIAL&&target!=SLOT_BODY&&target!=SLOT_MOB&&target!=SLOT_IMP)return 0;
    if(inventory_num_slots(pc,(int)target)<=0)return 0;
    struct ModifyInv request={0};request.classptr=inventory_message_class;request.command=MODINV_MOVE;
    request.slotpos=from;request.targetslotpos=(int32_t)target;request._has_bits[0]=7;
    /* Executes synchronously on the same main-thread keyboard path as the
       native inventory UI. handleModifyInv retains slot/class checks, the
       native input-buffer budget, bonus recalculation and network send. */
    inventory_modify(wc,&request);return 1;
}
static void inventory_keys(const uint8_t *keys,int n,int allowed){
    int selected=-1;allowed=allowed&&inventory_enabled()&&!inventory_modifiers(keys,n);
    for(int i=0;i<INVENTORY_BIND_SLOTS;++i){
        int a=CONTROL_INVENTORY_FIRST+i;
        int previous=0,down=0;
        for(int c=0;c<3;++c){int sc=key_from_name&&strcmp(control_keys[a][c],"None")?key_from_name(control_keys[a][c]):0;
            /* The three bindings are alternatives: holding either must not
               turn an OS repeat or a second alternative into another swap. */
            previous|=control_previous[a][c];
            down|=sc>0&&sc<n&&!controls_inventory_reserved(sc)&&keys[sc];
        }
        controls_shortcut(keys,n,a,0);
        if(allowed&&down&&!previous&&selected<0)selected=i;
    }
    if(selected>=0)inventory_equip(selected);
}
static int inventory_consumes(struct Stage *st,int down,int key,int was_down){
    int sc=scancode_from_key?scancode_from_key(key):0;
    if(sc<=0||sc>=(int)sizeof(inventory_owned))return 0;
    if(inventory_owned[sc]){if(!down)inventory_owned[sc]=0;return 1;}
    int n=0;const uint8_t *keys=input_keys(&n);
    if(down&&!was_down&&inventory_enabled()&&!paused&&st&&!st->textInputTarget&&!active_controls&&
       keys&&!inventory_modifiers(keys,n)&&inventory_key_matches(sc)){inventory_owned[sc]=1;return 1;}
    return 0;
}
#ifndef SBPE_TEST
static void install_inventory_swap(void){
    const uint8_t item[]={0x55,0x48,0x89,0xe5,0x4c,0x8b,0x8f,0x60,0x03,0,0};
    const uint8_t count[]={0x55,0x48,0x89,0xe5,0x89,0xf0,0x25,0,0xf0,0,0};
    const uint8_t modify[]={0x55,0x48,0x89,0xe5,0x41,0x56,0x53,0x49,0x89,0xf6,0x48,0x89,0xfb};
    if(memcmp((void*)PTR(0x100026d90),item,sizeof(item))||memcmp((void*)PTR(0x100026ca0),count,sizeof(count))||
       memcmp((void*)PTR(0x1000f56c0),modify,sizeof(modify))){LOG("Inventory hotkeys: native signatures differ; equip actions disabled.");return;}
    inventory_get_item=(void*)PTR(0x100026d90);inventory_num_slots=(void*)PTR(0x100026ca0);
    inventory_modify=(void*)PTR(0x1000f56c0);inventory_message_class=(void*)PTR(0x1001f2210);
    LOG("Inventory hotkeys: %d bag-slot actions available in Controls > SBPE; normal native equip rules retained.",INVENTORY_BIND_SLOTS);
}
#endif
#endif
