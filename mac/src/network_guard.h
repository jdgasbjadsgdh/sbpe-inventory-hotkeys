/* Guards for one pinned Intel client build. Reject implausible frame sizes
 * before allocation and never construct a world after protobuf parsing fails.
 * All recovery returns through native cleanup and connection-error paths.
 */
#ifndef SBPE_NETWORK_GUARD_H
#define SBPE_NETWORK_GUARD_H

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#define NETWORK_DEFAULT_MAX_MESSAGE (16U*1024U*1024U)
static uint32_t network_max_message=NETWORK_DEFAULT_MAX_MESSAGE;
static uint64_t network_header_sequence;
static int network_receive_installed;
static void network_receive_reset(const void*);
static void network_receive_rejection(const void*,const void*,int);
static void network_delta_reset(const void*);
static uint64_t network_delta_now(void);
static int network_delta_stalled(const void*,uint64_t);
#include "network_failure.h"
static void *network_reconnect_pending;
static const uint8_t network_length_original[20]={
    0x8b,0x55,0xd4,0x83,0xc2,0xfb,0x89,0x55,0xd4,
    0x49,0xc7,0x86,0xc0,0,0,0,0,0,0,0
};
static const uint8_t network_dispatch_original[18]={
    0x41,0xc6,0x46,0x70,0,0x8b,0x55,0xd4,0xb0,1,0x85,0xd2,
    0x0f,0x8f,0xcd,0,0,0
};
static const uint8_t network_world_original[16]={
    0x49,0x8b,0xb7,0x80,0,0,0,0x48,0x85,0xf6,0x74,0x27,0x48,0x8b,0x46,0x78
};

static void network_guard_jump(uint8_t *out,uintptr_t target){
    static const uint8_t jump[6]={0xff,0x25,0,0,0,0};
    memcpy(out,jump,6);memcpy(out+6,&target,8);
}
static void network_guard_rejected(uint32_t length){
    LOG("Network guard: rejected invalid server message length %u (limit %u bytes); reconnecting.",length,network_max_message);
}
static int network_header_valid(const void *connection,uint32_t length,uint32_t limit){
    ++network_header_sequence;
    uint8_t type;memcpy(&type,(const uint8_t*)connection+0x70,1);
    if(length>limit){network_guard_rejected(length);if(network_receive_installed)network_receive_rejection(connection,NULL,0);return 0;}
    const char *reason=NULL;
    /* This pinned client's MessageType enum ends at MT_FACTION_ROSTER (46).
       Both compressed consumers use compressUtil's 1 MiB input limit. */
    if(!type||type>0x2e)reason="invalid message type";
    else if((type==10||type==38)&&(length<5||length>1024*1024))reason="compressed message length";
    else if(type==37&&length<2)reason="movement message length";
    if(!reason){
#ifdef SBPE_LOADING_PROBE_H
        loading_probe_receive_header(connection,type,length);
#endif
        return 1;
    }
    LOG("Network guard: rejected header (type %u, %u advertised bytes, %s); reconnecting before allocation/payload wait.",type,length,reason);
    if(network_receive_installed)network_receive_rejection(connection,NULL,0);
    return 0;
}
static void network_world_request_recovery(void *game_client){
    /* GameClient::serverConnection is verified at +0x70 for this exact build.
     * Defer the reconnect until MessageHandlers and this callback have returned.
     * Reconnecting here would destroy the handler while it is still executing.
     */
    memcpy(&network_reconnect_pending,(const uint8_t*)game_client+0x70,sizeof(void*));
    if(network_receive_installed&&network_reconnect_pending){
        const uint8_t *p=network_reconnect_pending;void *buffer;uint32_t length;
        memcpy(&buffer,p+0xb0,8);memcpy(&length,p+0x74,4);
        network_receive_rejection(network_reconnect_pending,buffer,length<=network_max_message?(int)length:-1);
    }
}
static void network_world_rejected(void *game_client){
    LOG("Network guard: CreateWorld parsing failed; skipped world construction and requested reconnect after message dispatch.");
    network_world_request_recovery(game_client);
}
#include "network_world_spawn.h"
static unsigned char network_world_validate(void *game_client,const void *create_world,int parsed){
    if(!parsed){network_world_rejected(game_client);return 0;}
    struct NetworkSpawnInfo info;enum NetworkSpawnResult result=network_world_spawn_check(create_world,&info);
    if(result==NETWORK_SPAWN_OK)return 1;
    LOG("Network guard: CreateWorld %s; skipped construction before TileMap fatal check; objects=%u worlds=%u spawn_reference=%u/%u; native recovery requested after dispatch.",
        result==NETWORK_SPAWN_MISSING?"missing spawn object":result==NETWORK_SPAWN_LIMIT?"invalid object/world count":"unreadable world state",info.objects,info.worlds,info.key_world,info.key_object);
    network_world_request_recovery(game_client);return 0;
}
static unsigned char (*network_world_validator)(void*,const void*,int)=network_world_validate;

/* EAX is the decoded length; R14/RBP belong to the native doRead frame.
 * Valid input retains native RAX/callee-saved state and replays displaced
 * instructions; the caller-saved registers used here are dead at this site.
 */
static void network_guard_code(uint8_t out[89],uintptr_t resume,uintptr_t failure,uint32_t limit){
    /* Preserve native RAX (also used for allocation comparison), and keep
       the existing doRead frame 16-byte aligned while calling C. Other
       caller-saved registers are dead here; native EDX is reloaded below. */
    const uint8_t prefix[]={0x50,0x48,0x83,0xec,8,0x4c,0x89,0xf7,0x89,0xc6,0xba};
    memcpy(out,prefix,sizeof(prefix));memcpy(out+11,&limit,4);
    out[15]=0x48;out[16]=0xb8;uintptr_t checker=(uintptr_t)network_header_valid;memcpy(out+17,&checker,8);
    const uint8_t finish[]={0xff,0xd0,0x44,0x0f,0xb6,0xd8,0x48,0x83,0xc4,8,0x58,0x45,0x84,0xdb,0x74,34};
    memcpy(out+25,finish,sizeof(finish));
    memcpy(out+41,network_length_original,20);
    network_guard_jump(out+61,resume);network_guard_jump(out+75,failure);
}

/* Check ParseFromArray's AL result before the old code alters the world/UI.
 * Valid parse: preserve both the old-world and no-old-world branches.
 * Failed parse: request reconnect, then run the original CreateWorld and
 * decompressed-string destructors at the normal cleanup label.
 */
#define NETWORK_WORLD_CODE_SIZE 87
static void network_world_code(uint8_t out[NETWORK_WORLD_CODE_SIZE],uintptr_t nonnull,uintptr_t null_world,uintptr_t cleanup){
    /* Parse result AL -> EDX, GameClient R15 -> RDI, parsed CreateWorld
     * at native RBP-0xb0 -> RSI. The original call frame is already aligned.
     * Check before the native code removes or deletes any old-world/UI. */
    const uint8_t prefix[]={0x0f,0xb6,0xd0,0x4c,0x89,0xff,0x48,0x8d,0xb5,0x50,0xff,0xff,0xff,0x48,0xb8};
    memcpy(out,prefix,sizeof(prefix));uintptr_t checker=(uintptr_t)network_world_validator;memcpy(out+15,&checker,8);
    out[23]=0xff;out[24]=0xd0;out[25]=0x84;out[26]=0xc0;out[27]=0x74;out[28]=44;
    memcpy(out+29,network_world_original,10);
    out[39]=0x75;out[40]=14;network_guard_jump(out+41,null_world);
    memcpy(out+55,network_world_original+12,4);network_guard_jump(out+59,nonnull);
    network_guard_jump(out+73,cleanup);
}

/* Runs immediately after MessageHandlers::run, outside its C++ call frames.
 * Only a rejected message on THIS connection changes the native return path.
 * RCX is caller-saved and dead here; normal AL/EDX and branching are replayed.
 */
static void network_dispatch_code(uint8_t out[78],uintptr_t loop,uintptr_t done,uintptr_t failure){
    memcpy(out,network_dispatch_original,5); /* inputType_ = 0 */
    out[5]=0x48;out[6]=0xb9;
    uintptr_t pending=(uintptr_t)&network_reconnect_pending;memcpy(out+7,&pending,8);
    out[15]=0x4c;out[16]=0x39;out[17]=0x31; /* cmp R14,(RCX) */
    out[18]=0x75;out[19]=21;
    const uint8_t clear[7]={0x48,0xc7,1,0,0,0,0};memcpy(out+20,clear,7);
    network_guard_jump(out+27,failure);
    memcpy(out+41,network_dispatch_original+5,7);
    out[48]=0x7f;out[49]=14;
    network_guard_jump(out+50,done);
    network_guard_jump(out+64,loop);
}

/* Install only during the dylib constructor, before the game's networking.
 * Exact original bytes and the existing executable SHA/UUID checks pin ABI.
 */
static int network_patch_apply(uint8_t *site,const uint8_t *original,size_t length,const uint8_t *code,size_t code_size){
    if(length<14||length>32||memcmp(site,original,length))return 0;
    long page_size=sysconf(_SC_PAGESIZE);
    if(page_size<=0||code_size>(size_t)page_size)return 0;
    uint8_t *stub=mmap(NULL,(size_t)page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(stub==MAP_FAILED)return 0;
    memcpy(stub,code,code_size);
    if(mprotect(stub,(size_t)page_size,PROT_READ|PROT_EXEC)){
        munmap(stub,(size_t)page_size);return 0;
    }
    uintptr_t mask=(uintptr_t)page_size-1;
    mach_vm_address_t first=(uintptr_t)site&~mask;
    mach_vm_address_t end=((uintptr_t)site+length+mask)&~mask;
    kern_return_t kr=mach_vm_protect(mach_task_self(),first,end-first,FALSE,
                                    VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE);
    if(kr!=KERN_SUCCESS){munmap(stub,(size_t)page_size);return 0;}
    uint8_t patch[32];memset(patch,0x90,length);network_guard_jump(patch,(uintptr_t)stub);
    memcpy(site,patch,length);
    __builtin___clear_cache((char*)site,(char*)site+length);
    kr=mach_vm_protect(mach_task_self(),first,end-first,FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
    if(kr!=KERN_SUCCESS)LOG("Network guard: restoring read/execute protection failed (%d).",kr);
    return 1;
}
static int network_guard_apply(uint8_t *site,uintptr_t resume,uintptr_t failure,uint32_t limit){
    uint8_t code[89];network_guard_code(code,resume,failure,limit);
    return network_patch_apply(site,network_length_original,20,code,sizeof(code));
}

#include "network_disconnect.h"
#include "network_progress.h"

/* MT_SERVER_DELTA_MTC (0x25) has two varints (world ID, dt), followed
 * by complete (object ID, zigzag X, zigzag Y) triples. The original decoder
 * fatally CHECKs the two header reads and ignores failures in object reads.
 * Preflight without constructing/mutating Delta or acknowledging bad input.
 * ReadVarint32 accepts up to ten bytes, including noncanonical encodings;
 * retain that behavior rather than imposing a five-byte canonical encoding.
 */
static const uint8_t network_delta_original[15]={
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x20
};
static void (*network_continue_delta)(void*,int,const char*,int);
/* Native decode returns ParseFromArray's AL for plain/GENC protobuf updates;
 * MTC returns no status, so only its validated structure is used for that type. */
static unsigned char (*network_decode_delta)(int,const char*,int,void*);
static const uint8_t network_delta_setup[11]={0x49,0x89,0xfc,0x49,0x8d,0x9c,0x24,0xb8,0,0,0};
enum NetworkDeltaError {NETWORK_DELTA_OK,NETWORK_DELTA_LENGTH,NETWORK_DELTA_WORLD,NETWORK_DELTA_TIME,NETWORK_DELTA_OBJECT};
static int network_delta_varint(const uint8_t *data,size_t size,size_t *at){
    for(unsigned i=0;i<10;++i){if(*at>=size)return 0;if(!(data[(*at)++]&0x80))return 1;}return 0;
}
static enum NetworkDeltaError network_delta_check(const void *data,int length,size_t *bad_offset){
    *bad_offset=0;
    if(length<0||(uint32_t)length>network_max_message||(!data&&length))return NETWORK_DELTA_LENGTH;
    size_t at=0,size=(size_t)length;
    if(!network_delta_varint(data,size,&at))return NETWORK_DELTA_WORLD;
    *bad_offset=at;if(!network_delta_varint(data,size,&at))return NETWORK_DELTA_TIME;
    while(at<size){
        for(unsigned field=0;field<3;++field){*bad_offset=at;if(!network_delta_varint(data,size,&at))return NETWORK_DELTA_OBJECT;}
    }
    *bad_offset=at;return NETWORK_DELTA_OK;
}
static int network_delta_world_known(const void *world,uint32_t id){
    if(!world)return 0;
    if(id==1){void *server;memcpy(&server,(const uint8_t*)world+0x498,8);return server!=NULL;}
    /* Inspect the native map's vector of (uint32_t, ForeignSubWorld*) pairs,
       avoiding dependence on libc++'s private unordered_map node layout. */
    uintptr_t bounds[3];memcpy(bounds,(const uint8_t*)world+0x4a0,sizeof(bounds));
    if(!bounds[0]||bounds[1]<bounds[0]||bounds[2]<bounds[1]||(bounds[1]-bounds[0])%16)return 0;
    size_t count=(bounds[1]-bounds[0])/16;if(count>100000)return 0;
    for(size_t i=0;i<count;++i){uint32_t key;void *sub;memcpy(&key,(const void*)(bounds[0]+i*16),4);memcpy(&sub,(const void*)(bounds[0]+i*16+8),8);if(key==id)return sub!=NULL;}return 0;
}
/* A complete frame can be unusable without losing TCP framing. In particular,
   stock ClientWorld aborts on an update for an absent sub-world. Drop that
   isolated update before application/ack; retain the connection for the next
   valid update. Bound a run with no usable world progress so it cannot leave
   the player in an empty world indefinitely. */
static struct {
    const void *client,*connection,*world;
    uint64_t started_ms;
    unsigned rejected;
} network_delta_errors;
static uint64_t network_delta_recovery_ms=5000;
static uint64_t (*network_delta_clock)(void)=network_progress_now;
static uint64_t network_delta_now(void){return network_delta_clock();}
static void network_delta_reset(const void *connection){
    if(network_delta_errors.connection==connection)memset(&network_delta_errors,0,sizeof(network_delta_errors));
}
static int network_delta_stalled(const void *connection,uint64_t now){
    if(!network_delta_errors.rejected||network_delta_errors.connection!=connection)return 0;
    if(now<network_delta_errors.started_ms){network_delta_reset(connection);return 0;}
    if(now-network_delta_errors.started_ms<network_delta_recovery_ms)return 0;
    network_delta_reset(connection);return 1;
}
static void network_delta_reject(void *world_client,int type,const char *data,int length,const char *reason,size_t offset,int decoded){
    void *connection,*world;memcpy(&connection,(const uint8_t*)world_client+0x68,8);memcpy(&world,(const uint8_t*)world_client+0x78,8);
    uint64_t now=network_delta_now();
    if(network_delta_errors.client!=world_client||network_delta_errors.connection!=connection||network_delta_errors.world!=world||now<network_delta_errors.started_ms){
        memset(&network_delta_errors,0,sizeof(network_delta_errors));network_delta_errors.client=world_client;
        network_delta_errors.connection=connection;network_delta_errors.world=world;network_delta_errors.started_ms=now;
    }
    ++network_delta_errors.rejected;
    LOG("Network guard: rejected world update (type %d, %s, %d bytes, offset %zu); discarded frame without world mutation or ack; connection retained, resync if no usable world update for %llu ms.",type,reason,length,offset,(unsigned long long)network_delta_recovery_ms);
    if(network_receive_installed)network_receive_rejection(connection,data,length);
    uint32_t fields=0;if(decoded)memcpy(&fields,(const uint8_t*)world_client+0xb8+0x40,4);
    network_failure_record(type,data,length,reason,fields,decoded);
    /* If the user disabled the I/O deadline hook, keep the old deferred
       dispatch path as a fallback once subsequent rejected frames hit it. */
    if(!network_failure_io&&network_delta_stalled(connection,now))network_reconnect_pending=connection;
}
static void network_server_delta(void *world_client,int type,const char *data,int length){
    if(type!=0xc&&type!=0x25&&type!=0x26){network_delta_reject(world_client,type,data,length,"unsupported encoding",0,0);return;}
    if(length<0||(uint32_t)length>network_max_message||(!data&&length)){network_delta_reject(world_client,type,data,length,"invalid length/buffer",0,0);return;}
    if(type==0x25){size_t offset;enum NetworkDeltaError error=network_delta_check(data,length,&offset);
        if(error!=NETWORK_DELTA_OK){
            const char *reason=error==NETWORK_DELTA_LENGTH?"invalid length":error==NETWORK_DELTA_WORLD?"world ID":error==NETWORK_DELTA_TIME?"time value":"object record";
            network_delta_reject(world_client,type,data,length,reason,offset,0);
            return;
        }
    }
    if(type==0x26){
        /* compressUtil uses a four-byte big-endian expanded-size prefix and
           a 1 MiB bound. Reject negative/oversized lengths before string resize. */
        if(length<4||length>1024*1024){network_delta_reject(world_client,type,data,length,"compressed length",0,0);return;}
        const uint8_t *p=(const void*)data;uint32_t expanded=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
        if(expanded>1024*1024){network_delta_reject(world_client,type,data,length,"expanded length",0,0);return;}
    }
    uint8_t *delta=(uint8_t*)world_client+0xb8;unsigned char parsed=network_decode_delta(type,data,length,delta);
    if(type!=0x25&&!parsed){network_delta_reject(world_client,type,data,length,"protobuf parse failed",0,1);return;}
    uint32_t fields;memcpy(&fields,delta+0x40,4);
    if(!(fields&0x7f)){network_delta_reject(world_client,type,data,length,"empty/unknown Delta",0,1);return;}
    /* Match World's first-present-field dispatch order. Bit 0x40 alone is a
       legal state-request message. Check world updates before native enqueue. */
    if(!(fields&3)&&(fields&4)){
        void *update,*world;memcpy(&update,delta+0x18,8);memcpy(&world,(uint8_t*)world_client+0x78,8);
        uint32_t id=0;if(update)memcpy(&id,(const uint8_t*)update+8,4);
        if(!update||!network_delta_world_known(world,id)){LOG("Network guard: target sub-world %u is missing.",id);network_delta_reject(world_client,type,data,length,"WorldUpdate to missing world",0,1);return;}
    }
    if(fields&7){void *connection;memcpy(&connection,(uint8_t*)world_client+0x68,8);network_delta_reset(connection);}
    /* Decode exactly once. Continue after the original decode call, preserving
       the game's world application and acknowledgement/destructor code. */
#ifdef SBPE_LOADING_PROBE_H
    loading_probe_received(world_client,delta);
#endif
    network_continue_delta(world_client,type,data,length);
}
static int network_delta_guard_apply(uint8_t *site,uintptr_t after_decode){
    if(memcmp(site,network_delta_original,sizeof(network_delta_original)))return 0;
    size_t page=(size_t)sysconf(_SC_PAGESIZE);if(!page||page==(size_t)-1)return 0;
    uint8_t *trampoline=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(trampoline==MAP_FAILED)return 0;
    /* These displaced instructions have no RIP-relative operands. Replay the
       native prologue, then resume at its first complete body instruction. */
    memcpy(trampoline,network_delta_original,sizeof(network_delta_original));
    memcpy(trampoline+sizeof(network_delta_original),network_delta_setup,sizeof(network_delta_setup));
    network_guard_jump(trampoline+sizeof(network_delta_original)+sizeof(network_delta_setup),after_decode);
    if(mprotect(trampoline,page,PROT_READ|PROT_EXEC)){munmap(trampoline,page);return 0;}
    void (*previous)(void*,int,const char*,int)=network_continue_delta;network_continue_delta=(void*)trampoline;
    uint8_t code[14];network_guard_jump(code,(uintptr_t)network_server_delta);
    if(network_patch_apply(site,network_delta_original,sizeof(network_delta_original),code,sizeof(code)))return 1;
    network_continue_delta=previous;munmap(trampoline,page);return 0;
}
#include "network_receive.h"
#ifndef SBPE_NETWORK_GUARD_TEST
#include "network_websocket.h"
static void install_network_guard(void){
    install_websocket_transport();
    if(!getbool("native","network_length_guard",1))return;
    double mb=getnum("native","network_max_message_mb",16);
    if(mb<1||mb>2047)mb=16;
    network_max_message=(uint32_t)(mb*1024*1024);
    const char *failure_root=getenv("SBPE_ROOT");
    if(failure_root&&strlen(failure_root)<sizeof(network_failure_root)&&getbool("native","network_failure_records",1)){
        snprintf(network_failure_root,sizeof(network_failure_root),"%s",failure_root);
        network_failure_lz4=(void*)PTR(0x1001874a0);
    }
    const uint8_t decoded[9]={0x8b,0x45,0x9c,0x0f,0xc8,0x41,0x89,0x46,0x74};
    const uint8_t failure[4]={0x31,0xc0,0xeb,0x06};
    if(memcmp((void*)PTR(0x1000867bf),decoded,9)||memcmp((void*)PTR(0x10008695a),failure,4)||
       !network_guard_apply((void*)PTR(0x1000867c8),PTR(0x1000867dc),PTR(0x10008695a),network_max_message)){
        LOG("Network length guard could not be installed; original receiver retained.");
    }else LOG("Network length guard installed: maximum %u bytes; normal reconnect on invalid input.",network_max_message);
    /* Install before progress recovery; the latter can enter this same block
       using a fresh native frame and marks that source explicitly. */
    int loss_diagnostic=network_loss_apply((void*)PTR(0x1000863d4),PTR(0x10018eca8),PTR(0x1000863ea));
    if(loss_diagnostic)
        LOG("Passive disconnect diagnostics installed; native socket/error recovery unchanged.");
    else LOG("Disconnect diagnostic bytes mismatched; native failure path retained.");
    double timeout=getnum("native","network_partial_timeout_seconds",5);
    if(timeout>0){
        network_partial_timeout_ms=(uint64_t)(fmin(fmax(timeout,1),60)*1000);
        double lifetime=getnum("native","network_partial_max_seconds",10);
        network_partial_lifetime_ms=lifetime>0?(uint64_t)(fmin(fmax(lifetime,1),60)*1000):0;
        const uint8_t io_setup[3]={0x49,0x89,0xfc};
        if(!memcmp((void*)PTR(0x10008624f),io_setup,sizeof(io_setup))&&
           (loss_diagnostic||!memcmp((void*)PTR(0x1000863d4),network_loss_original,sizeof(network_loss_original)))&&
           network_progress_apply((void*)PTR(0x100086240),PTR(0x10008624f),PTR(0x1000863d4)))
            LOG("Incomplete-message recovery installed: native recovery after %llu ms without progress or %llu ms total for one incomplete frame; manual holds survive recovery.",(unsigned long long)network_partial_timeout_ms,(unsigned long long)network_partial_lifetime_ms);
        else LOG("Incomplete-message recovery could not be installed; native timeout retained.");
    }
#ifdef SBPE_LOADING_PROBE_H
    probe_reconnect_request=network_failure_io?network_request_loading_reconnect:NULL;
    probe_reconnect_cancel=network_cancel_loading_reconnect;
#endif
#ifdef SBPE_NETWORK_STALL_H
    network_stall_request=network_failure_io?network_request_stall_reconnect:NULL;
    if(network_stall_request&&getbool("native","network_stall_recovery",1))
        LOG("Spawned-world stall recovery enabled: r<=20 with no world-time progress for %.1fs; 30s cooldown; holds, arming and unspawned clients excluded.",network_stall_seconds());
#endif

    const uint8_t parse_call[5]={0xe8,0xbe,0x34,0x11,0};
    const uint8_t cleanup[7]={0x48,0x8d,0xbd,0x50,0xff,0xff,0xff};
    uint8_t dispatch_code[78],world_code[NETWORK_WORLD_CODE_SIZE];
    network_dispatch_code(dispatch_code,PTR(0x100086770),PTR(0x100086964),PTR(0x10008695a));
    network_world_code(world_code,PTR(0x10007c5f8),PTR(0x10007c61b),PTR(0x10007c70f));
    /* The dispatch hook must precede the parse hook, to make deferred recovery
     * effective from the first rejected world message. */
    int dispatch_ready=network_patch_apply((void*)PTR(0x100086691),network_dispatch_original,18,dispatch_code,sizeof(dispatch_code));
    if(!dispatch_ready||memcmp((void*)PTR(0x10007c5e3),parse_call,5)||memcmp((void*)PTR(0x10007c70f),cleanup,7)||
       memcmp((void*)PTR(0x10007c5e8),network_world_original,16)||
       !network_patch_apply((void*)PTR(0x10007c5e8),network_world_original,16,world_code,sizeof(world_code))){
        LOG("CreateWorld parse guard could not be installed; original parser behavior retained.");
    }else LOG("CreateWorld parse/spawn guard installed: failed parses and missing tile-map spawns skip construction, clean up normally and recover after message dispatch.");

    /* Never intercept a delta unless its deferred recovery path is installed. */
    if(getbool("native","network_delta_guard",1)){
        double recovery=getnum("native","network_delta_recovery_seconds",5);
        network_delta_recovery_ms=(uint64_t)(fmin(fmax(recovery,1),30)*1000);
        const uint8_t decode_call[5]={0xe8,0xa7,0xf3,0xf0,0xff};
        const uint8_t connection_load[5]={0x49,0x8b,0x7c,0x24,0x68};
        network_decode_delta=(void*)PTR(0x100003e70);
        if(!dispatch_ready||memcmp((void*)PTR(0x1000f4ac4),decode_call,5)||memcmp((void*)PTR(0x1000f4b24),connection_load,5)||
           memcmp((void*)PTR(0x1000f4aaf),network_delta_setup,sizeof(network_delta_setup))||
           !network_delta_guard_apply((void*)PTR(0x1000f4aa0),PTR(0x1000f4ac9))){
            LOG("World-update guard could not be installed; original decoder retained.");
        }else LOG("World-update guard installed: unusable updates discarded before application; native resync only after %llu ms without a usable world update.",(unsigned long long)network_delta_recovery_ms);
    }
    if(getbool("native","network_stream_receiver",1)||getbool("native","network_websocket_transport",0)){
        network_receive_available=network_receive_fionread;
        network_receive_dispatch=(void*)PTR(0x1000044d0);network_receive_crypt=(void*)PTR(0x10018e0b0);
        network_receive_alloc=dlsym(RTLD_DEFAULT,"_Znam");network_receive_free=dlsym(RTLD_DEFAULT,"_ZdaPv");
        if(dispatch_ready&&network_receive_alloc&&network_receive_free&&network_receive_apply((void*)PTR(0x1000865e0))){
            network_receive_installed=1;
            LOG("Streaming receiver installed: short reads/EINTR/EAGAIN handled; partial headers retained; reentrant reads deferred; socket transitions end old read budget. Native dispatch/encryption/recovery retained.");
        }else LOG("Streaming receiver not installed; original guarded receiver retained.");
    }
}
#endif
#endif
