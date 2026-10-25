/* Streaming receiver for the pinned native connection layout. Keep native
 * encryption, dispatch and error recovery, but tolerate short/interrupted
 * reads and stop using an old byte budget after a callback changes sockets.
 * No authentication data or cipher material is logged or retained here. */
#ifndef SBPE_NETWORK_RECEIVE_H
#define SBPE_NETWORK_RECEIVE_H
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <errno.h>

static ssize_t (*network_receive_recv)(int,void*,size_t,int)=recv;
static int (*network_receive_available)(int,int*);
static void (*network_receive_dispatch)(void*,int,const void*,int);
static void (*network_receive_crypt)(void*,void*,int);
static void *(*network_receive_alloc)(size_t);
static void (*network_receive_free)(void*);
struct NetworkReceiveState {
    void *connection;int socket,active;uint8_t header[5];unsigned have;
    uint64_t generation,frames,bytes;
    uint32_t wire_hash,dispatch_hash,frame_length;int tracked,complete,wire_preserved;
    const void *cipher;uint32_t cipher_hash;int cipher_seen;
    uint64_t cipher_checks,cipher_changes;
    struct {uint8_t type;uint32_t length;} recent[8];unsigned recent_count;
};
static struct NetworkReceiveState network_receive_states[8];
static uint64_t network_receive_generation;
static uint64_t network_receive_short_reads,network_receive_interrupts,network_receive_wouldblock;
static uint32_t network_receive_hash(uint32_t hash,const void *buffer,size_t length){
    const uint8_t *p=buffer;for(size_t i=0;i<length;++i)hash=(hash^p[i])*16777619U;return hash;
}
static int network_receive_cipher_permutation(const void *cipher){
    if(!cipher)return 0;const uint8_t *p=cipher;uint64_t seen[4]={0};
    for(unsigned i=2;i<258;++i){unsigned v=p[i];uint64_t bit=1ULL<<(v&63);if(seen[v>>6]&bit)return 0;seen[v>>6]|=bit;}
    return 1;
}
/* Keep only an in-memory fingerprint, never a key/state copy or printed hash.
 * Native setRC4Keys replaces the context pointer. The receive context otherwise
 * changes only in the two native decrypt calls reproduced by this reader. */
static void network_receive_check_cipher(struct NetworkReceiveState *s,const void *connection,const char *where){
    void *cipher;memcpy(&cipher,(const uint8_t*)connection+0xa0,8);
    if(!cipher){s->cipher=NULL;s->cipher_seen=0;return;}
    uint32_t hash=network_receive_hash(2166136261U,cipher,258);++s->cipher_checks;
    if(s->cipher_seen&&s->cipher==cipher&&s->cipher_hash!=hash){
        ++s->cipher_changes;
        LOG("Receive evidence: incoming cipher changed outside receiver decryption (%s); permutation valid=%s; frame=%llu. No cipher bytes or hashes recorded.",
            where,network_receive_cipher_permutation(cipher)?"YES":"NO",(unsigned long long)s->frames);
    }
    s->cipher=cipher;s->cipher_hash=hash;s->cipher_seen=1;
}
static void network_receive_decrypt(struct NetworkReceiveState *s,const void *connection,void *data,int length,const char *where){
    network_receive_check_cipher(s,connection,where);
    void *cipher;memcpy(&cipher,(const uint8_t*)connection+0xa0,8);
    if(cipher&&length){network_receive_crypt(cipher,data,length);s->cipher_hash=network_receive_hash(2166136261U,cipher,258);}
}
static void network_receive_rejection(const void *connection,const void *data,int length){
    struct NetworkReceiveState *s=NULL;for(unsigned i=0;i<8;++i)if(network_receive_states[i].connection==connection){s=&network_receive_states[i];break;}
    if(!s)return;network_receive_check_cipher(s,connection,"at rejected message");
    void *cipher;memcpy(&cipher,(const uint8_t*)connection+0xa0,8);
    const char *wire="UNAVAILABLE",*dispatch="UNAVAILABLE";
    if(s->complete&&s->tracked){
        wire=s->wire_preserved?"YES":"NO";
        if(data&&length>=0&&(uint32_t)length==s->frame_length)dispatch=network_receive_hash(2166136261U,data,(size_t)length)==s->dispatch_hash?"YES":"NO";
    }
    LOG("Receive evidence: received buffer preserved before decrypt=%s; dispatch buffer preserved=%s; encryption=%s; frames=%llu bytes=%llu short_reads=%llu EINTR=%llu EAGAIN=%llu. No cipher material retained.",
        wire,dispatch,cipher?"on":"off",(unsigned long long)s->frames,(unsigned long long)s->bytes,(unsigned long long)network_receive_short_reads,(unsigned long long)network_receive_interrupts,(unsigned long long)network_receive_wouldblock);
    if(cipher)LOG("Receive evidence: incoming cipher permutation valid=%s; changes outside receiver decryption=%llu over %llu checks. No cipher bytes or hashes recorded.",
        network_receive_cipher_permutation(cipher)?"YES":"NO",(unsigned long long)s->cipher_changes,(unsigned long long)s->cipher_checks);
    unsigned n=s->recent_count<8?s->recent_count:8;
    char history[192]={0};size_t used=0;
    for(unsigned i=0;i<n;++i){unsigned at=(s->recent_count-n+i)%8;
        int wrote=snprintf(history+used,sizeof(history)-used,"%s%u:%u",i?",":"",s->recent[at].type,s->recent[at].length);
        if(wrote<0||(size_t)wrote>=sizeof(history)-used)break;used+=(size_t)wrote;
    }
    LOG("Receive evidence: recent complete frame type:length = [%s]; metadata only.",history);
}
static int network_receive_fionread(int socket,int *available){return ioctl(socket,FIONREAD,available);}
static void network_receive_reset(const void *connection){
    for(unsigned i=0;i<8;++i)if(network_receive_states[i].connection==connection){
        network_receive_states[i].have=0;network_receive_states[i].generation=++network_receive_generation;
        network_receive_states[i].cipher=NULL;network_receive_states[i].cipher_seen=0;
        /* Keep active while a callback is on the stack: a nested read must
           not consume the stream underneath its outer dispatch. */
        if(!network_receive_states[i].active)network_receive_states[i].connection=NULL;
    }
}
static struct NetworkReceiveState *network_receive_state(void *connection,int socket){
    struct NetworkReceiveState *empty=NULL;
    for(unsigned i=0;i<8;++i){struct NetworkReceiveState *s=&network_receive_states[i];
        if(s->connection==connection){
            if(!s->active&&s->socket!=socket){s->socket=socket;s->have=0;s->generation=++network_receive_generation;}
            return s;
        }
        if(!s->connection&&!empty)empty=s;
    }
    if(!empty)for(unsigned i=0;i<8;++i)if(!network_receive_states[i].active){empty=&network_receive_states[i];break;}
    if(!empty)return NULL;
    memset(empty,0,sizeof(*empty));empty->connection=connection;empty->socket=socket;empty->generation=++network_receive_generation;return empty;
}
static int network_receive_same(const uint8_t *p,int socket){
    int state,current;memcpy(&state,p+0x28,4);memcpy(&current,p+0x54,4);return state==3&&current==socket;
}
static ssize_t network_receive_part(int socket,void *data,size_t length){
    ssize_t n;do {n=network_receive_recv(socket,data,length,MSG_DONTWAIT);if(n<0&&errno==EINTR)++network_receive_interrupts;}while(n<0&&errno==EINTR);
    if(n>0&&(size_t)n<length)++network_receive_short_reads;
    if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))++network_receive_wouldblock;return n;
}
static unsigned char network_receive(void *connection){
    uint8_t *p=connection;int socket,state;memcpy(&socket,p+0x54,4);memcpy(&state,p+0x28,4);
    if(state!=3||socket<0){network_receive_reset(connection);return 1;}
    struct NetworkReceiveState *s=network_receive_state(connection,socket);
    if(!s)return 0;if(s->active)return 1;
    s->active=1;uint64_t generation=s->generation;unsigned char result=1;
    network_receive_check_cipher(s,connection,"between I/O calls");
    int available=0;
    if(network_receive_available(socket,&available)<0){if(errno!=EINTR)result=0;goto done;}
    if(available<0){result=0;goto done;}
    if(!available){
        uint8_t peek;ssize_t n=network_receive_recv(socket,&peek,1,MSG_PEEK|MSG_DONTWAIT);
        if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))result=0;
        goto done;
    }
    for(;;){
        if(!network_receive_same(p,socket)||s->generation!=generation)break;
        uint8_t type=p[0x70];uint32_t length;uint64_t received,allocation;void *buffer;
        memcpy(&length,p+0x74,4);memcpy(&received,p+0xc0,8);
        if(!type){
            if(available<=0)break;
            size_t wanted=5-s->have;if(wanted>(size_t)available)wanted=(size_t)available;
            ssize_t n=network_receive_part(socket,s->header+s->have,wanted);
            if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK)result=0;break;}
            if(!n){result=0;break;}
            s->have+=(unsigned)n;s->bytes+=(uint64_t)n;available-=(int)n;
            if(s->have<5)continue;
            network_receive_decrypt(s,connection,s->header,5,"before header decrypt");
            type=s->header[0];length=(uint32_t)s->header[1]<<24|(uint32_t)s->header[2]<<16|(uint32_t)s->header[3]<<8|s->header[4];
            s->have=0;p[0x70]=type;memcpy(p+0x74,&length,4);received=0;memcpy(p+0xc0,&received,8);
            s->tracked=type==10||type==12||type==37||type==38;s->complete=0;s->wire_hash=2166136261U;s->frame_length=length;
            if(!network_header_valid(connection,length,network_max_message)){result=0;break;}
            memcpy(&allocation,p+0xb8,8);memcpy(&buffer,p+0xb0,8);
            if(length>allocation){
                void *replacement=network_receive_alloc(length);
                if(!replacement){result=0;break;}
                if(buffer)network_receive_free(buffer);
                buffer=replacement;allocation=length;memcpy(p+0xb0,&buffer,8);memcpy(p+0xb8,&allocation,8);
            }
        }
        memcpy(&allocation,p+0xb8,8);memcpy(&buffer,p+0xb0,8);
        if(received>length||length>allocation||(length&&!buffer)){result=0;break;}
        if(received<length){
            if(available<=0)break;
            size_t wanted=(size_t)(length-received);if(wanted>(size_t)available)wanted=(size_t)available;
            ssize_t n=network_receive_part(socket,(uint8_t*)buffer+received,wanted);
            if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK)result=0;break;}
            if(!n){result=0;break;}
            if(s->tracked)s->wire_hash=network_receive_hash(s->wire_hash,(uint8_t*)buffer+received,(size_t)n);
            received+=(uint64_t)n;s->bytes+=(uint64_t)n;available-=(int)n;memcpy(p+0xc0,&received,8);
            if(received<length)continue;
        }
        if(s->tracked)s->wire_preserved=network_receive_hash(2166136261U,buffer,length)==s->wire_hash;
        network_receive_decrypt(s,connection,buffer,(int)length,"before payload decrypt");
        if(s->tracked)s->dispatch_hash=network_receive_hash(2166136261U,buffer,length);s->complete=1;
        unsigned recent=s->recent_count++%8;s->recent[recent].type=type;s->recent[recent].length=length;++s->frames;
        network_receive_dispatch(p+0x78,type,length?buffer:NULL,(int)length);
        /* The handler has returned. Preserve the existing deferred rejection
           path, and never read a new socket using the old FIONREAD snapshot. */
        if(network_reconnect_pending==connection){network_reconnect_pending=NULL;result=0;break;}
        if(!network_receive_same(p,socket)||s->generation!=generation){
            s->have=0;LOG("Network receiver: socket/state changed during dispatch; old read budget discarded.");break;
        }
        network_receive_check_cipher(s,connection,"during native message dispatch");
        p[0x70]=0;
    }
done:
    s->active=0;if(!result)network_receive_reset(connection);return result;
}
static const uint8_t network_receive_original[20]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xc8,0,0,0};
static int network_receive_apply(uint8_t *site){
    uint8_t code[14];network_guard_jump(code,(uintptr_t)network_receive);
    return network_patch_apply(site,network_receive_original,sizeof(network_receive_original),code,sizeof(code));
}
#endif
