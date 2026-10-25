/* Optional WSS byte-stream transport at the pinned native connect call only.
 * Authentication, native message encoding/encryption, and recovery stay in the
 * game. Off delegates exactly to the original connect syscall. */
#ifndef SBPE_NETWORK_WEBSOCKET_H
#define SBPE_NETWORK_WEBSOCKET_H
#include <sys/socket.h>
#include <netinet/in.h>
#ifndef SBPE_TEST
extern int sbpe_websocket_connect(int,const char*);
static int (*network_websocket_open)(int,const char*)=sbpe_websocket_connect;
#else
static int (*network_websocket_open)(int,const char*);
#endif
static int (*network_websocket_native)(int,const struct sockaddr*,socklen_t)=connect;
_Static_assert(offsetof(struct ServerConnection,hostname)==0x30&&offsetof(struct ServerConnection,socket)==0x54,"WSS connection ABI");
static int network_websocket_connect_call(int fd,const struct sockaddr *address,socklen_t length,struct ServerConnection *connection){
    if(getbool("native","network_websocket_transport",0)&&network_websocket_open&&connection&&address&&
       length>=sizeof(struct sockaddr_in)&&address->sa_family==AF_INET&&ntohs(((const struct sockaddr_in*)address)->sin_port)==2048){
        char host[256];read_string(&connection->hostname,host,sizeof(host));
        if(host[0]){
            int result=network_websocket_open(fd,host);
            if(result!=-2){int saved=errno;LOG("WebSocket transport: native game stream queued for TLS host %s; result=%d. Login and message encoding retained.",host,result);errno=saved;return result;}
            LOG("WebSocket transport: unavailable for this host/system; native TCP retained.");
        }
    }
    return network_websocket_native(fd,address,length);
}
static const uint8_t network_websocket_original[18]={0x41,0x8b,0x7e,0x54,0x48,0x8d,0x75,0xc0,0xba,0x10,0,0,0,0xe8,0xeb,0x8e,0x10,0};
static void network_websocket_code(uint8_t out[42],uintptr_t resume){
    memcpy(out,network_websocket_original,13); /* original fd/address/length */
    const uint8_t argument[]={0x4c,0x89,0xf1,0x48,0xb8}; /* R14 connection -> RCX */
    memcpy(out+13,argument,5);uintptr_t call=(uintptr_t)network_websocket_connect_call;memcpy(out+18,&call,8);
    out[26]=0xff;out[27]=0xd0;network_guard_jump(out+28,resume);
}
static int network_websocket_apply(uint8_t *site,uintptr_t resume){
    uint8_t code[42];network_websocket_code(code,resume);
    return network_patch_apply(site,network_websocket_original,sizeof(network_websocket_original),code,sizeof(code));
}
#ifndef SBPE_TEST
static void install_websocket_transport(void){
    if(!network_websocket_apply((void*)PTR(0x100086d4d),PTR(0x100086d5f)))
        LOG("WebSocket transport: native connect bytes mismatch; transport hook not installed.");
    else LOG("WebSocket transport hook installed; encrypted browser route %s for new game connections.",getbool("native","network_websocket_transport",0)?"ENABLED":"disabled");
}
#endif
#endif
