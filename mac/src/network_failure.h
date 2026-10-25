/* Local, bounded replay evidence for rejected world updates only. Never
 * records authentication messages, cipher state, keys, or outgoing traffic.
 * The normal receive/decode path performs no capture work. */
#ifndef SBPE_NETWORK_FAILURE_H
#define SBPE_NETWORK_FAILURE_H
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#define NETWORK_FAILURE_LIMIT (1024U*1024U)
static char network_failure_root[4096];
static unsigned network_failure_slot;
static int (*network_failure_lz4)(const char*,char*,int,int);
struct NetworkFailureHeader {
    char magic[8];
    uint32_t type,wire_length,expanded_expected;
    int32_t expanded_actual; /* -2: not attempted; negative: LZ4 failure */
    uint32_t raw_saved,decoded_saved;
    uint64_t epoch_ms;
    uint32_t delta_fields,decode_attempted;
    char reason[80];
};
_Static_assert(sizeof(struct NetworkFailureHeader)==128,"Failure record format");
static int network_failure_write(int fd,const void *data,size_t size){
    const uint8_t *p=data;
    while(size){ssize_t n=write(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return 0;p+=n;size-=(size_t)n;}return 1;
}
static void network_failure_record(int type,const char *data,int length,const char *reason,uint32_t fields,int decoded){
    /* Refuse unrelated encodings and untrusted lengths. Capture at most 1 MiB
       of a valid receive buffer; the header retains its original length. */
    if(!network_failure_root[0]||(type!=0xc&&type!=0x25&&type!=0x26)||length<0||
       (uint32_t)length>network_max_message||(!data&&length))return;
    struct NetworkFailureHeader h={.magic="SBPEWU1",.type=(uint32_t)type,.wire_length=(uint32_t)length,
        .expanded_actual=-2,.raw_saved=(uint32_t)length<NETWORK_FAILURE_LIMIT?(uint32_t)length:NETWORK_FAILURE_LIMIT,
        .delta_fields=fields,.decode_attempted=(uint32_t)decoded};
    struct timespec now;clock_gettime(CLOCK_REALTIME,&now);h.epoch_ms=(uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
    snprintf(h.reason,sizeof(h.reason),"%s",reason);
    char *expanded=NULL;
    if(type==0x26&&length>=4){
        const uint8_t *p=(const void*)data;h.expanded_expected=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
        if(h.expanded_expected<=NETWORK_FAILURE_LIMIT&&length>4&&length<=(int)NETWORK_FAILURE_LIMIT&&network_failure_lz4){
            expanded=malloc(h.expanded_expected?h.expanded_expected:1);
            if(expanded){
                h.expanded_actual=network_failure_lz4(data+4,expanded,length-4,(int)h.expanded_expected);
                if(h.expanded_actual==(int)h.expanded_expected)h.decoded_saved=h.expanded_expected;
            }
        }
    }
    char path[4352];unsigned slot=network_failure_slot++%4;
    snprintf(path,sizeof(path),"%s/world-update-failure-%u.bin",network_failure_root,slot);
    int fd=open(path,O_WRONLY|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
    if(fd>=0){
        struct stat st;
        int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==getuid()&&st.st_nlink==1&&
            !fchmod(fd,0600)&&!ftruncate(fd,0)&&network_failure_write(fd,&h,sizeof(h))&&
            network_failure_write(fd,data,h.raw_saved)&&network_failure_write(fd,expanded,h.decoded_saved);
        close(fd);
        if(ok)LOG("Network guard: local failure record %u saved (%u input, %u decoded bytes; LZ4 result %d).",slot,h.raw_saved,h.decoded_saved,h.expanded_actual);
    }
    free(expanded);
}
#endif
