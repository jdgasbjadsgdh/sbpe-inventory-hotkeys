/* Preflight the parsed CreateWorld before native code replaces the old UI.
 * Match the pinned ClientWorld::spawnObj lookup/fallback and the drawable,
 * bounded terrain that makes WorldView call TileMap::generate. No object,
 * coordinate, readiness, input or server state is changed here.
 */
#ifndef SBPE_NETWORK_WORLD_SPAWN_H
#define SBPE_NETWORK_WORLD_SPAWN_H
#define NETWORK_SPAWN_MAX_OBJECTS 262144U
#define NETWORK_SPAWN_MAX_WORLDS 4096U
struct NetworkSpawnField {void **elements;int32_t size,allocated,total;};
struct NetworkSpawnCreate {void *type,*props,*state,*spawn;uint32_t world;int32_t cached;uint32_t bits;};
struct NetworkSpawnState {void *type,*server;struct NetworkSpawnField clients;};
struct NetworkSpawnWorld {void *type;struct NetworkSpawnField objects;uint32_t owner;};
struct NetworkSpawnObject {void *type,*props;uint32_t id;};
struct NetworkSpawnKey {void *type;uint32_t world,object;};
enum NetworkSpawnResult {NETWORK_SPAWN_OK,NETWORK_SPAWN_MISSING,NETWORK_SPAWN_UNREADABLE,NETWORK_SPAWN_LIMIT};
struct NetworkSpawnInfo {uint32_t own_world,key_world,key_object,objects,worlds;int tile_map,reference,fallback;};
static int network_spawn_read(const void *source,void *target,size_t size){
    if(!source)return 0;mach_vm_size_t copied=0;
    return mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)source,size,(mach_vm_address_t)target,&copied)==KERN_SUCCESS&&copied==size;
}
static enum NetworkSpawnResult network_spawn_field(struct NetworkSpawnField f,unsigned limit){
    if(f.size<0||(uint32_t)f.size>limit)return NETWORK_SPAWN_LIMIT;
    if(f.size&&(!f.elements||(uintptr_t)f.elements>UINTPTR_MAX-(size_t)f.size*sizeof(void*)))return NETWORK_SPAWN_UNREADABLE;
    return NETWORK_SPAWN_OK;
}
static enum NetworkSpawnResult network_spawn_world(const void *pointer,int server,struct NetworkSpawnInfo *info){
    struct NetworkSpawnWorld world;
    if(!network_spawn_read(pointer,&world,sizeof(world)))return NETWORK_SPAWN_UNREADABLE;
    enum NetworkSpawnResult result=network_spawn_field(world.objects,NETWORK_SPAWN_MAX_OBJECTS-info->objects);
    if(result!=NETWORK_SPAWN_OK)return result;
    ++info->worlds;
    for(int i=0;i<world.objects.size;++i){
        const void *object;struct NetworkSpawnObject obj;unsigned char props[0x99]={0};
        if(!network_spawn_read((const void*)((uintptr_t)world.objects.elements+(size_t)i*sizeof(void*)),&object,sizeof(object))||
           !network_spawn_read(object,&obj,sizeof(obj)))return NETWORK_SPAWN_UNREADABLE;
        ++info->objects;
        /* Absent optional properties use the native zero-terrain defaults.
         * A directly named object can exist even without spawn=true. */
        uint32_t id=server?1:world.owner;
        if(id==info->key_world&&obj.id==info->key_object&&(server||(id!=1&&id!=info->own_world)))info->reference=1;
        if(obj.props&&!network_spawn_read(obj.props,props,sizeof(props)))return NETWORK_SPAWN_UNREADABLE;
        if(server&&props[0x98])info->fallback=1;
        int32_t terrain,width,height;
        memcpy(&terrain,props+0x44,4);memcpy(&width,props+0x10,4);memcpy(&height,props+0x14,4);
        /* Object offsets +0x54/+0x78/+0x85 and +0x18..0x24 are verified
         * in WorldView::recalculateWorldBounds. A nonnegative bounded
         * drawable terrain rect makes both native bounds nonnegative. */
        if(terrain&&props[0x68]&&props[0x75]&&width>=0&&height>=0)info->tile_map=1;
    }
    return NETWORK_SPAWN_OK;
}
static enum NetworkSpawnResult network_world_spawn_check(const void *pointer,struct NetworkSpawnInfo *info){
    memset(info,0,sizeof(*info));struct NetworkSpawnCreate create;struct NetworkSpawnState state={0};
    if(!network_spawn_read(pointer,&create,sizeof(create)))return NETWORK_SPAWN_UNREADABLE;
    info->own_world=create.world;
    if(create.spawn){struct NetworkSpawnKey key;if(!network_spawn_read(create.spawn,&key,sizeof(key)))return NETWORK_SPAWN_UNREADABLE;info->key_world=key.world;info->key_object=key.object;}
    if(create.state&&!network_spawn_read(create.state,&state,sizeof(state)))return NETWORK_SPAWN_UNREADABLE;
    enum NetworkSpawnResult result=network_spawn_field(state.clients,NETWORK_SPAWN_MAX_WORLDS);
    if(result!=NETWORK_SPAWN_OK)return result;
    if(state.server){result=network_spawn_world(state.server,1,info);if(result!=NETWORK_SPAWN_OK)return result;}
    for(int i=0;i<state.clients.size;++i){
        const void *world;
        if(!network_spawn_read((const void*)((uintptr_t)state.clients.elements+(size_t)i*sizeof(void*)),&world,sizeof(world)))return NETWORK_SPAWN_UNREADABLE;
        result=network_spawn_world(world,0,info);if(result!=NETWORK_SPAWN_OK)return result;
    }
    return info->tile_map&&!info->reference&&!info->fallback?NETWORK_SPAWN_MISSING:NETWORK_SPAWN_OK;
}
#endif
