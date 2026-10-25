/* Parsed protobuf objects, real checked Mach reads and the actual Intel hook.
 * No live account, game process, packets or intentional native fatal calls. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../src/internals.h"
#include "../src/generated.h"
static unsigned semantic_rejections,parse_rejections;
static void spawn_log(const char *format,...){
    char message[512];va_list args;va_start(args,format);vsnprintf(message,sizeof(message),format,args);va_end(args);
    if(strstr(message,"CreateWorld missing spawn object"))++semantic_rejections;
    if(strstr(message,"CreateWorld parsing failed"))++parse_rejections;
}
#define LOG(...) spawn_log(__VA_ARGS__)
#define SBPE_NETWORK_GUARD_TEST 1
#include "../src/network_guard.h"
static struct Properties terrain,marker,foreign_props;
static struct WorldState_World_Obj a,b,c;
static void *server_objects[2],*foreign_objects[1],*client_worlds[1];
static struct WorldState_World server,foreign;
static struct WorldState state;
static struct ObjKeyMessage key;
static struct CreateWorld create;
static void reset(void){
    memset(&terrain,0,sizeof(terrain));memset(&marker,0,sizeof(marker));memset(&foreign_props,0,sizeof(foreign_props));
    terrain.terraintype=1;terrain.draw=1;terrain.boundsworld=1;terrain.wmp=terrain.hmp=256;
    a=(struct WorldState_World_Obj){.objid=7,.props=&terrain};b=(struct WorldState_World_Obj){.objid=9,.props=&marker};c=(struct WorldState_World_Obj){.objid=42,.props=&foreign_props};
    server_objects[0]=&a;server_objects[1]=&b;foreign_objects[0]=&c;
    server=(struct WorldState_World){.ownerid=999,.obj={server_objects,2,2,2}};
    foreign=(struct WorldState_World){.ownerid=78,.obj={foreign_objects,1,1,1}};client_worlds[0]=&foreign;
    state=(struct WorldState){.serverworld=&server,.clientworld={client_worlds,1,1,1}};
    key=(struct ObjKeyMessage){.worldid=1,.objid=9};
    create=(struct CreateWorld){.worldid=77,.worldstate=&state,.spawnobj=&key};
}
static struct NetworkSpawnInfo info;
static enum NetworkSpawnResult check(void){
    struct CreateWorld before=create;enum NetworkSpawnResult result=network_world_spawn_check(&create,&info);
    assert(!memcmp(&before,&create,sizeof(create)));return result;
}
static void test_semantics(void){
    reset();assert(check()==NETWORK_SPAWN_OK&&info.reference&&info.tile_map&&!info.fallback);
    key.objid=99;assert(check()==NETWORK_SPAWN_MISSING&&!info.reference&&!info.fallback);
    marker.spawn=1;assert(check()==NETWORK_SPAWN_OK&&info.fallback);
    create.spawnobj=NULL;assert(check()==NETWORK_SPAWN_OK&&info.fallback);
    marker.spawn=0;assert(check()==NETWORK_SPAWN_MISSING);
    foreign_props.spawn=1;assert(check()==NETWORK_SPAWN_MISSING); /* fallback is server-only */
    create.spawnobj=&key;key.worldid=78;key.objid=42;assert(check()==NETWORK_SPAWN_OK&&info.reference);
    key.worldid=77;foreign.ownerid=77;assert(check()==NETWORK_SPAWN_MISSING); /* native own-world lookup shadows foreign */
    reset();key.objid=99;terrain.boundsworld=0;assert(check()==NETWORK_SPAWN_OK&&!info.tile_map);
    terrain.boundsworld=1;terrain.draw=0;assert(check()==NETWORK_SPAWN_OK&&!info.tile_map);
    terrain.draw=1;terrain.terraintype=0;assert(check()==NETWORK_SPAWN_OK&&!info.tile_map);
    terrain.terraintype=1;terrain.wmp=-1;assert(check()==NETWORK_SPAWN_OK&&!info.tile_map);
    terrain.wmp=terrain.hmp=0;assert(check()==NETWORK_SPAWN_MISSING&&info.tile_map);
    reset();terrain.draw=0;foreign_props=terrain;foreign_props.draw=1;foreign_props.boundsworld=1;
    key.objid=99;assert(check()==NETWORK_SPAWN_MISSING&&info.tile_map);
    marker.spawn=1;assert(check()==NETWORK_SPAWN_OK);
    reset();b.props=NULL;assert(check()==NETWORK_SPAWN_OK&&info.reference); /* named default-properties object */
    create.worldstate=NULL;assert(check()==NETWORK_SPAWN_OK&&!info.tile_map);
    reset();state.serverworld=NULL;foreign_props=terrain;key.worldid=78;key.objid=42;
    assert(check()==NETWORK_SPAWN_OK&&info.reference&&info.tile_map);
    key.worldid=1;assert(check()==NETWORK_SPAWN_MISSING); /* no server reference/fallback */
}
static void test_bounds(void){
    reset();server.obj.current_size=-1;assert(check()==NETWORK_SPAWN_LIMIT);
    server.obj.current_size=NETWORK_SPAWN_MAX_OBJECTS+1;assert(check()==NETWORK_SPAWN_LIMIT);
    reset();state.clientworld.current_size=NETWORK_SPAWN_MAX_WORLDS+1;assert(check()==NETWORK_SPAWN_LIMIT);
    reset();server.obj.elements=(void**)(UINTPTR_MAX-3);assert(check()==NETWORK_SPAWN_UNREADABLE);
    reset();server_objects[1]=(void*)1;assert(check()==NETWORK_SPAWN_UNREADABLE);
    reset();a.props=(void*)1;assert(check()==NETWORK_SPAWN_UNREADABLE);
    reset();create.worldstate=(void*)1;assert(check()==NETWORK_SPAWN_UNREADABLE);
    reset();create.spawnobj=(void*)1;assert(check()==NETWORK_SPAWN_UNREADABLE);
    size_t page=(size_t)sysconf(_SC_PAGESIZE);void *unreadable=mmap(NULL,page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(unreadable!=MAP_FAILED);assert(network_world_spawn_check(unreadable,&info)==NETWORK_SPAWN_UNREADABLE);
    assert(!munmap(unreadable,page));assert(network_world_spawn_check(unreadable,&info)==NETWORK_SPAWN_UNREADABLE);
    assert(network_world_spawn_check(NULL,&info)==NETWORK_SPAWN_UNREADABLE);
}
static void test_actual_hook(void){
    /* Same RBP-0xb0 CreateWorld placement as the pinned handleCreateWorld.
     * This fixture really copies the parsed object into that native frame. */
    uint8_t prefix[128]={0x55,0x48,0x89,0xe5,0x41,0x57,0x48,0x81,0xec,0xc8,0,0,0,
        0x49,0x89,0xf7,0x89,0xbd,0x48,0xff,0xff,0xff};size_t at=22;
    _Static_assert(sizeof(struct CreateWorld)==48,"fixture parsed-world copy");
    _Static_assert(offsetof(struct Properties,draw)==0x68&&offsetof(struct Properties,boundsworld)==0x75&&offsetof(struct Properties,spawn)==0x98,"pinned property predicates");
    for(int offset=0;offset<48;offset+=8){
        uint8_t load[]={0x48,0x8b,0x42,(uint8_t)offset},store[]={0x48,0x89,0x85,0,0,0,0};int32_t displacement=-0xb0+offset;
        memcpy(store+3,&displacement,4);memcpy(prefix+at,load,sizeof(load));at+=sizeof(load);memcpy(prefix+at,store,sizeof(store));at+=sizeof(store);
    }
    const uint8_t parsed[]={0x8b,0x85,0x48,0xff,0xff,0xff};memcpy(prefix+at,parsed,sizeof(parsed));at+=sizeof(parsed);
    const uint8_t yes[]={0x41,0xc7,0x47,8,1,0,0,0,0x41,0xff,7,0x41,0xff,0x47,4,
        0x48,0x81,0xc4,0xc8,0,0,0,0x41,0x5f,0x5d,0xc3};
    uint8_t no[sizeof(yes)];memcpy(no,yes,sizeof(no));no[4]=2;
    const uint8_t cleanup[]={0x41,0xff,0x47,4,0x48,0x81,0xc4,0xc8,0,0,0,0x41,0x5f,0x5d,0xc3};
    size_t page=(size_t)sysconf(_SC_PAGESIZE);uint8_t *fixture=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(fixture!=MAP_FAILED);
    uint8_t *site=fixture+at,*valid_old=site+sizeof(network_world_original),*valid_new=valid_old+sizeof(yes),*fail=valid_new+sizeof(no);
    memcpy(fixture,prefix,at);memcpy(site,network_world_original,sizeof(network_world_original));
    memcpy(valid_old,yes,sizeof(yes));memcpy(valid_new,no,sizeof(no));memcpy(fail,cleanup,sizeof(cleanup));
    assert(!mprotect(fixture,page,PROT_READ|PROT_EXEC));
    uint8_t code[NETWORK_WORLD_CODE_SIZE];network_world_validator=network_world_validate;
    network_world_code(code,(uintptr_t)valid_old,(uintptr_t)valid_new,(uintptr_t)fail);
    assert(network_patch_apply(site,network_world_original,sizeof(network_world_original),code,sizeof(code)));
    struct TestClient{unsigned created,cleaned,branch;uint8_t pad[0x70-12];void *connection;uint8_t gap[8];void *world;} client={0};
    int connection;void *old_world[16]={0};old_world[15]=&client;client.connection=&connection;
    void (*load)(int,void*,const void*)=(void*)fixture;
    reset();
    for(int old=0;old<2;++old){
        client.created=client.cleaned=client.branch=0;client.world=old?old_world:NULL;network_reconnect_pending=NULL;
        load(1,&client,&create);assert(client.created==1&&client.cleaned==1&&client.branch==(old?1U:2U)&&!network_reconnect_pending);
    }
    key.objid=99;unsigned rejected_before=semantic_rejections;
    for(int old=0;old<2;++old){
        client.created=client.cleaned=client.branch=0;client.world=old?old_world:NULL;network_reconnect_pending=NULL;
        load(1,&client,&create);assert(!client.created&&client.cleaned==1&&!client.branch&&client.world==(old?old_world:NULL));
        assert(network_reconnect_pending==&connection);
    }
    assert(semantic_rejections==rejected_before+2);
    network_reconnect_pending=NULL;client.created=client.cleaned=0;unsigned parse_before=parse_rejections;
    load(0,&client,&create);assert(!client.created&&client.cleaned==1&&parse_rejections==parse_before+1&&network_reconnect_pending==&connection);
    marker.spawn=1;network_reconnect_pending=NULL;client.created=client.cleaned=0;
    load(1,&client,&create);assert(client.created==1&&client.cleaned==1&&!network_reconnect_pending);
    assert(!munmap(fixture,page));
}
int main(void){
    test_semantics();test_bounds();test_actual_hook();
    puts("PASS: direct server/foreign spawn lookup, own-world shadowing, server-only fallback, drawable/bounded terrain and valid no-tile worlds; malformed/unreadable state and count/address bounds; actual Intel hook copies CreateWorld at RBP-0xb0, preserves both valid native branches, rejects missing spawn before construction and preserves old world, runs native cleanup once and queues recovery outside dispatch.");
}
