/* Observe the pinned native lost-connection block before its callback can
 * start a reconnect and clear the socket/world. Never alter recovery policy. */
#ifndef SBPE_NETWORK_DISCONNECT_H
#define SBPE_NETWORK_DISCONNECT_H
static int network_loss_synthetic;
static const char *network_loss_reason(int synthetic,unsigned events){
    if(synthetic==2)return "loading experiment requested fresh reconnect";
    if(synthetic==3)return "SBPE world-update resynchronization deadline";
    if(synthetic==4)return "SBPE stalled-world recovery";
    if(synthetic)return "SBPE incomplete-frame deadline";
    if(events&0x10)return "socket hangup reported by poll";
    if(events&0x08)return "socket error reported by poll";
    if(events&0x01)return "native read returned false (EOF or receive guard)";
    return "native socket failure; no recognized poll flag";
}
static void network_loss_observe(const void *connection,unsigned events){
#ifdef SBPE_NETWORK_HEALTH_H
    network_health_lost(connection);
#endif
    network_delta_reset(connection);
    if(network_receive_installed)network_receive_reset(connection);
    const uint8_t *p=connection;int state,socket;uint32_t length;uint64_t received;uint8_t type;
    memcpy(&state,p+0x28,4);memcpy(&socket,p+0x54,4);memcpy(&type,p+0x70,1);
    memcpy(&length,p+0x74,4);memcpy(&received,p+0xc0,8);
    if(network_loss_synthetic)events=0; /* Synthetic frame has no pollfd. */
    LOG("Connection lost: %s; poll=0x%x socket=%d state=%d incoming_type=%u received=%llu advertised=%u. Transport evidence does not identify a server rejection reason.",
        network_loss_reason(network_loss_synthetic,events),events,socket,state,type,(unsigned long long)received,length);
#ifdef SBPE_LOADING_PROBE_H
    loading_probe_connection_lost(connection,network_loss_reason(network_loss_synthetic,events));
#endif
}
/* Exact 22 bytes: argument setup, native Handler::run call, state=ERROR.
 * The logger uses the existing aligned doIO frame. Caller-saved registers
 * are dead here; R12=this and all native callee-saved state remain intact. */
static const uint8_t network_loss_original[22]={
    0xbe,2,0,0,0,0x4c,0x89,0xe7,0xe8,0xc7,0x88,0x10,0,
    0x41,0xc7,0x44,0x24,0x28,4,0,0,0
};
static void network_loss_code(uint8_t out[62],uintptr_t observer,uintptr_t handler,uintptr_t resume){
    const uint8_t args[]={0x4c,0x89,0xe7,0x0f,0xb7,0x75,0xbe,0x48,0xb8};
    memcpy(out,args,sizeof(args));memcpy(out+9,&observer,8);
    out[17]=0xff;out[18]=0xd0;
    memcpy(out+19,network_loss_original,8);
    out[27]=0x48;out[28]=0xb8;memcpy(out+29,&handler,8);
    out[37]=0xff;out[38]=0xd0;
    memcpy(out+39,network_loss_original+13,9);
    network_guard_jump(out+48,resume);
}
static int network_loss_apply(uint8_t *site,uintptr_t handler,uintptr_t resume){
    uint8_t code[62];network_loss_code(code,(uintptr_t)network_loss_observe,handler,resume);
    return network_patch_apply(site,network_loss_original,sizeof(network_loss_original),code,sizeof(code));
}
#endif
