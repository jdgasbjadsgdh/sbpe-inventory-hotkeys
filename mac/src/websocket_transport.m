/* Byte-stream adapter to StarBreak's binary WSS service. No application message
 * parsing, authentication changes, payload recording, or certificate overrides.
 * Each bridge owns separate descriptor references so shutdown cannot race a
 * descriptor being reused by the game's next connection. */
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

static void ws_event(NSString *host,const char *event,NSInteger code){
    const char *root=getenv("SBPE_ROOT");if(!root||strlen(root)>3800)return;
    char path[4096];snprintf(path,sizeof(path),"%s/websocket-transport.log",root);
    int fd=open(path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);if(fd<0)return;
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_nlink!=1){close(fd);return;}
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);char line[512];
    int n=snprintf(line,sizeof(line),"%lld.%03ld host=%s event=%s code=%ld\n",(long long)t.tv_sec,t.tv_nsec/1000000,host.UTF8String,event,(long)code);
    if(n>0&&n<(int)sizeof(line)){ssize_t written;do{written=write(fd,line,(size_t)n);}while(written<0&&errno==EINTR);}
    close(fd);
}
API_AVAILABLE(macos(10.15))
@interface SBPEWebSocketBridge : NSObject <NSURLSessionWebSocketDelegate> {
    int _control;
    BOOL _stopped;
    unsigned long long _bytesIn,_bytesOut;
    NSString *_host;
    NSURLSession *_session;
    NSURLSessionWebSocketTask *_task;
    dispatch_queue_t _incoming;
}
- (instancetype)initWithSocket:(int)fd URL:(NSURL*)url;
- (void)start;
- (void)finish:(NSInteger)code;
@end
@implementation SBPEWebSocketBridge
- (instancetype)initWithSocket:(int)fd URL:(NSURL*)url {
    if((self=[super init])){
        _control=fd;_host=[url.host copy];_incoming=dispatch_queue_create("sbpe.wss.incoming",DISPATCH_QUEUE_SERIAL);
        NSURLSessionConfiguration *config=[NSURLSessionConfiguration ephemeralSessionConfiguration];
        config.HTTPAdditionalHeaders=@{@"Origin":@"https://www.starbreak.com"};
        config.timeoutIntervalForRequest=30;config.timeoutIntervalForResource=24*60*60;
        NSOperationQueue *queue=[[NSOperationQueue alloc]init];queue.maxConcurrentOperationCount=1;
        _session=[NSURLSession sessionWithConfiguration:config delegate:self delegateQueue:queue];
        _task=[_session webSocketTaskWithURL:url protocols:@[@"binary"]];_task.maximumMessageSize=32*1024*1024;
    }return self;
}
- (int)duplicateSocket {
    @synchronized(self){return _stopped?-1:fcntl(_control,F_DUPFD_CLOEXEC,0);}
}
- (NSURLSessionWebSocketTask*)currentTask {
    @synchronized(self){return _stopped?nil:_task;}
}
- (void)finish:(NSInteger)code {
    NSURLSession *session;NSURLSessionWebSocketTask *task;unsigned long long incoming,outgoing;
    @synchronized(self){
        if(_stopped)return;_stopped=YES;
        /* shutdown wakes existing reader/writer duplicates; close only our
           own reference. Their owners close their references after use. */
        shutdown(_control,SHUT_RDWR);close(_control);_control=-1;
        session=_session;task=_task;_session=nil;_task=nil;incoming=_bytesIn;outgoing=_bytesOut;
    }
    char event[120];snprintf(event,sizeof(event),"closed bytes_in=%llu bytes_out=%llu",incoming,outgoing);
    ws_event(_host,event,code);[task cancelWithCloseCode:NSURLSessionWebSocketCloseCodeGoingAway reason:nil];[session invalidateAndCancel];
}
- (void)receiveNext {
    NSURLSessionWebSocketTask *task=[self currentTask];if(!task)return;
    [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *message,NSError *error){
        if(error){[self finish:error.code];return;}
        if(message.type!=NSURLSessionWebSocketMessageTypeData){[self finish:EPROTO];return;}
        NSData *data=message.data;@synchronized(self){self->_bytesIn+=data.length;}
        dispatch_async(self->_incoming,^{@autoreleasepool{
            int fd=[self duplicateSocket];if(fd<0)return;
            const uint8_t *bytes=data.bytes;NSUInteger left=data.length;int failed=0;
            while(left){ssize_t n=send(fd,bytes,left,0);if(n<0&&errno==EINTR)continue;
                if(n<=0){failed=errno?errno:EPIPE;break;}bytes+=n;left-=(NSUInteger)n;}
            close(fd);if(failed)[self finish:failed];else [self receiveNext];
        }});
    }];
}
- (void)pumpOutgoing {
    @autoreleasepool{
        int fd=[self duplicateSocket];if(fd<0)return;uint8_t bytes[65536];NSInteger failed=0;
        for(;;){
            ssize_t n=recv(fd,bytes,sizeof(bytes),0);if(n<0&&errno==EINTR)continue;
            if(n<=0){if(n<0)failed=errno;break;}
            NSURLSessionWebSocketTask *task=[self currentTask];if(!task)break;
            NSData *data=[NSData dataWithBytes:bytes length:(NSUInteger)n];
            NSURLSessionWebSocketMessage *message=[[NSURLSessionWebSocketMessage alloc]initWithData:data];
            dispatch_semaphore_t done=dispatch_semaphore_create(0);__block NSInteger result=0;
            [task sendMessage:message completionHandler:^(NSError *error){result=error.code;dispatch_semaphore_signal(done);}];
            if(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC))){failed=ETIMEDOUT;break;}
            if(result){failed=result;break;}
            @synchronized(self){_bytesOut+=(unsigned long long)n;}
        }
        close(fd);[self finish:failed];
    }
}
- (void)start {
    ws_event(_host,"connecting",0);[_task resume];[self receiveNext];
    [NSThread detachNewThreadSelector:@selector(pumpOutgoing) toTarget:self withObject:nil];
}
- (void)URLSession:(NSURLSession*)session webSocketTask:(NSURLSessionWebSocketTask*)task didOpenWithProtocol:(NSString*)protocol {
    (void)session;(void)task;
    if(protocol&&![protocol isEqualToString:@"binary"]){[self finish:EPROTO];return;}
    ws_event(_host,"TLS-WebSocket-open",0);
}
- (void)URLSession:(NSURLSession*)session webSocketTask:(NSURLSessionWebSocketTask*)task didCloseWithCode:(NSURLSessionWebSocketCloseCode)code reason:(NSData*)reason {
    (void)session;(void)task;(void)reason;[self finish:code];
}
- (void)URLSession:(NSURLSession*)session task:(NSURLSessionTask*)task didCompleteWithError:(NSError*)error {
    (void)session;(void)task;[self finish:error.code];
}
@end

/* Return -2 only when unavailable/invalid before replacing the socket. */
int sbpe_websocket_connect(int socket,const char *hostname){
    if(!hostname||!hostname[0]||strlen(hostname)>253)return -2;
    for(const unsigned char *p=(const unsigned char*)hostname;*p;++p)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='-'))return -2;
    @autoreleasepool{
        if(@available(macOS 10.15,*)){
            NSString *host=[NSString stringWithUTF8String:hostname];
            NSURL *url=[NSURL URLWithString:[NSString stringWithFormat:@"wss://%@/",host]];
            int pair[2];if(!url||socketpair(AF_UNIX,SOCK_STREAM,0,pair))return -1;
            fcntl(pair[1],F_SETFD,FD_CLOEXEC);
            int flags=fcntl(socket,F_GETFL),descriptor=fcntl(socket,F_GETFD);int one=1,buffer=1024*1024;
            setsockopt(pair[0],SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));setsockopt(pair[1],SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
            setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&buffer,sizeof(buffer));setsockopt(pair[1],SOL_SOCKET,SO_SNDBUF,&buffer,sizeof(buffer));
            if(flags<0||descriptor<0||fcntl(pair[0],F_SETFL,flags& O_NONBLOCK)<0||dup2(pair[0],socket)<0){int saved=errno;close(pair[0]);close(pair[1]);errno=saved;return -1;}
            close(pair[0]);fcntl(socket,F_SETFD,descriptor);
            SBPEWebSocketBridge *bridge=[[SBPEWebSocketBridge alloc]initWithSocket:pair[1] URL:url];
            if(!bridge){shutdown(socket,SHUT_RDWR);close(pair[1]);errno=ENOMEM;return -1;}
            [bridge start];return 0;
        }
    }return -2;
}
#ifdef SBPE_WEBSOCKET_TEST
/* Test adapter uses the same code with a local endpoint but remains absent
   from the shipped library. No certificate-validation override exists. */
int sbpe_websocket_test_url(int socket,const char *address){
    if(@available(macOS 10.15,*)){
    @autoreleasepool{
        NSURL *url=[NSURL URLWithString:[NSString stringWithUTF8String:address]];
        if(!url||!([url.scheme isEqualToString:@"ws"]||[url.scheme isEqualToString:@"wss"]))return -2;
        int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,pair))return -1;int one=1;setsockopt(pair[1],SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
        int flags=fcntl(socket,F_GETFL),desc=fcntl(socket,F_GETFD);
        if(dup2(pair[0],socket)<0){close(pair[0]);close(pair[1]);return -1;}close(pair[0]);fcntl(socket,F_SETFL,flags);fcntl(socket,F_SETFD,desc);
        SBPEWebSocketBridge *bridge=[[SBPEWebSocketBridge alloc]initWithSocket:pair[1] URL:url];[bridge start];return 0;
    }}return -2;
}
#endif
