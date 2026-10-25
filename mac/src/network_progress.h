/* Detect a stalled, partially received frame. Run only after doIO has returned,
 * so its dispatch callbacks have finished; use the native connection-error
 * path instead of closing or destroying a connection in a message handler. */
#ifndef SBPE_NETWORK_PROGRESS_H
#define SBPE_NETWORK_PROGRESS_H
static uint64_t network_partial_timeout_ms=5000;
static uint64_t network_partial_lifetime_ms=10000;
struct NetworkPartialFrame {
    const void *connection;
    int socket;
    uint32_t length;
    uint8_t type;
    uint64_t received,progress_ms,generation,started_ms;
};
static struct NetworkPartialFrame network_partial;
static int network_partial_stalled(const void *connection,uint64_t now){
    int state,socket;uint8_t type;uint32_t length;uint64_t received;
    const uint8_t *p=connection;
    memcpy(&state,p+0x28,4);memcpy(&socket,p+0x54,4);memcpy(&type,p+0x70,1);
    memcpy(&length,p+0x74,4);memcpy(&received,p+0xc0,8);
    if(state!=3||socket<0||!type||received>=length){memset(&network_partial,0,sizeof(network_partial));return 0;}
    if(network_partial.connection!=connection||network_partial.socket!=socket||network_partial.type!=type||
       network_partial.length!=length||network_partial.generation!=network_header_sequence||now<network_partial.progress_ms){
        network_partial=(struct NetworkPartialFrame){connection,socket,length,type,received,now,network_header_sequence,now};return 0;
    }
    if(network_partial.received!=received){network_partial.received=received;network_partial.progress_ms=now;}
    return (network_partial_timeout_ms&&now-network_partial.progress_ms>=network_partial_timeout_ms)||
        (network_partial_lifetime_ms&&now-network_partial.started_ms>=network_partial_lifetime_ms);
}
static unsigned char (*network_continue_io)(void*),(*network_failure_io)(void*);
/* A loading test may request one normal reconnect. Queue it so no socket or
   world is destroyed from a render/input/message callback. Revalidate after
   native dispatch returns; an intervening transition cancels the request. */
static struct {void *connection;int socket;int (*validate)(void*);int reason;} network_loading_reconnect;
static void network_cancel_loading_reconnect(void *connection,int (*validate)(void*)){
    if(network_loading_reconnect.connection==connection&&network_loading_reconnect.validate==validate)
        memset(&network_loading_reconnect,0,sizeof(network_loading_reconnect));
}
static int network_request_loading_reconnect(void *connection,int (*validate)(void*)){
    if(!connection||!validate||!network_continue_io||!network_failure_io||network_loading_reconnect.connection)return 0;
    int state,socket;memcpy(&state,(uint8_t*)connection+0x28,4);memcpy(&socket,(uint8_t*)connection+0x54,4);
    if(state!=3||socket<0)return 0;
    network_loading_reconnect.connection=connection;network_loading_reconnect.socket=socket;network_loading_reconnect.validate=validate;network_loading_reconnect.reason=2;return 1;
}
#ifdef SBPE_NETWORK_STALL_H
static int network_request_stall_reconnect(void *connection,int (*validate)(void*)){
    if(!network_request_loading_reconnect(connection,validate))return 0;
    network_loading_reconnect.reason=4;return 1;
}
#endif
static uint64_t network_progress_now(void){struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);return (uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;}
static unsigned char network_do_io(void *connection){
    if(network_receive_installed){int state;memcpy(&state,(uint8_t*)connection+0x28,4);if(state!=3)network_receive_reset(connection);}
    unsigned char ok=network_continue_io(connection);
    if(!ok){
        network_delta_reset(connection);
#ifdef SBPE_NETWORK_STALL_H
        network_stall_drop(connection);
#endif
        if(network_loading_reconnect.connection==connection)memset(&network_loading_reconnect,0,sizeof(network_loading_reconnect));
        memset(&network_partial,0,sizeof(network_partial));return ok;
    }
    if(network_loading_reconnect.connection==connection){
        int state,socket,expected_socket=network_loading_reconnect.socket,reason=network_loading_reconnect.reason;
        int (*validate)(void*)=network_loading_reconnect.validate;
        memset(&network_loading_reconnect,0,sizeof(network_loading_reconnect));
        memcpy(&state,(uint8_t*)connection+0x28,4);memcpy(&socket,(uint8_t*)connection+0x54,4);
        if(state==3&&socket==expected_socket&&validate(connection)){
            memset(&network_partial,0,sizeof(network_partial));
            int previous=network_loss_synthetic;network_loss_synthetic=reason?reason:2;
            unsigned char result=network_failure_io(connection);network_loss_synthetic=previous;return result;
        }
    }
    int current_state;memcpy(&current_state,(uint8_t*)connection+0x28,4);
    if(current_state!=3)network_delta_reset(connection);
    if(current_state==3&&network_delta_stalled(connection,network_delta_now())){
        LOG("Network guard: no usable world update after rejection for the recovery window; one native resync requested after I/O returned.");
        int previous=network_loss_synthetic;network_loss_synthetic=3;
        unsigned char result=network_failure_io(connection);network_loss_synthetic=previous;return result;
    }
    if(network_partial_stalled(connection,network_progress_now())){
        LOG("Network guard: incomplete message stalled (type %u, %llu/%u bytes, age %llu ms, idle %llu ms; limits %llu/%llu ms); native recovery requested. An active manual hold remains held.",network_partial.type,(unsigned long long)network_partial.received,network_partial.length,(unsigned long long)(network_progress_now()-network_partial.started_ms),(unsigned long long)(network_progress_now()-network_partial.progress_ms),(unsigned long long)network_partial_lifetime_ms,(unsigned long long)network_partial_timeout_ms);
        memset(&network_partial,0,sizeof(network_partial));
        int previous=network_loss_synthetic;network_loss_synthetic=1;
        unsigned char result=network_failure_io(connection);
        network_loss_synthetic=previous;return result;
    }
    return ok;
}
static const uint8_t network_io_original[15]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x60};
static int network_progress_apply(uint8_t *site,uintptr_t after_prologue,uintptr_t failure){
    if(memcmp(site,network_io_original,sizeof(network_io_original)))return 0;
    long page=sysconf(_SC_PAGESIZE);if(page<=0)return 0;
    uint8_t *tr=mmap(NULL,(size_t)page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);if(tr==MAP_FAILED)return 0;
    memcpy(tr,network_io_original,15);network_guard_jump(tr+15,after_prologue);
    /* Native failure block needs R12=this and the original frame layout. */
    memcpy(tr+32,network_io_original,15);const uint8_t setup[]={0x49,0x89,0xfc};memcpy(tr+47,setup,3);network_guard_jump(tr+50,failure);
    if(mprotect(tr,(size_t)page,PROT_READ|PROT_EXEC)){munmap(tr,(size_t)page);return 0;}
    network_continue_io=(void*)tr;network_failure_io=(void*)(tr+32);
    uint8_t code[14];network_guard_jump(code,(uintptr_t)network_do_io);
    if(network_patch_apply(site,network_io_original,15,code,sizeof(code)))return 1;
    network_continue_io=network_failure_io=NULL;munmap(tr,(size_t)page);return 0;
}
#endif
