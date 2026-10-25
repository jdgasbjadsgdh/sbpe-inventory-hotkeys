/* Recover a drained reserve with no received world-time progress. This is a
 * fallback for stalls outside the partial-frame/invalid-delta guards. It does
 * not repair corrupted streams or bypass server loading deadlines. Never
 * spawn, release readiness, or recover an intentionally held/armed client. */
#ifndef SBPE_NETWORK_STALL_H
#define SBPE_NETWORK_STALL_H
struct NetworkStallSample {
    struct WorldClient *wc;struct ClientWorld *cw;struct ServerConnection *connection;
    int socket,t,reserve;uint32_t game,world;
};
static struct {
    struct NetworkStallSample sample,queued;
    double began,last_frame,last_progress,low_since,last_recovery;
    int active,low,queued_valid,has_recovered;
} network_stall;
static double (*network_stall_clock)(void)=monotime;
static struct WorldClient *(*network_stall_world)(void)=world_client;
static int (*network_stall_request)(void*,int(*)(void*));
static int network_stall_read(struct NetworkStallSample *s){
    if(!getbool("native","network_stall_recovery",1)||!network_stall_request||
       loading_probe.state==PROBE_ARMED||loading_probe.state==PROBE_HOLD||
       loading_probe.state==PROBE_WAIT||loading_probe.state==PROBE_RECONNECT)return 0;
    struct WorldClient *wc=network_stall_world();
    if(!wc||!wc->clientWorld||!wc->serverConnection||wc->serverConnection->state!=CONNECTED||wc->serverConnection->socket<0)return 0;
    struct ClientWorld *cw=wc->clientWorld;
    if(!cw->player||((struct WorldObject*)cw->player)->props.hitpoints<=0||!cw->mySubWorld||!cw->serverSubWorld)return 0;
    *s=(struct NetworkStallSample){wc,cw,wc->serverConnection,wc->serverConnection->socket,
        probe_world_t(cw),cw->timeReserve,cw->asWorld.props.gameid,cw->asWorld.id};return 1;
}
static int network_stall_same(const struct NetworkStallSample *a,const struct NetworkStallSample *b){
    return a->wc==b->wc&&a->cw==b->cw&&a->connection==b->connection&&a->socket==b->socket&&a->game==b->game&&a->world==b->world;
}
static double network_stall_seconds(void){return clamp(getnum("native","network_stall_seconds",5),3,30);}
static void network_stall_clear(void){network_stall.active=network_stall.low=network_stall.queued_valid=0;}
static void network_stall_drop(const void *connection){
    if(network_stall.queued_valid&&network_stall.queued.connection==connection)network_stall.queued_valid=0;
}
static int network_stall_confirm(void *connection){
    struct NetworkStallSample s;double now=network_stall_clock();
    if(!network_stall.queued_valid||connection!=network_stall.queued.connection)return 0;
    network_stall.queued_valid=0;
    /* Native I/O has just run. Fresh progress, a spawn/map/socket transition,
       config disable or a newly armed hold cancels this pending recovery. */
    if(!network_stall_read(&s)||!network_stall_same(&s,&network_stall.queued)||
       s.t!=network_stall.queued.t||s.reserve>20||now<network_stall.last_progress||
       now-network_stall.last_progress<network_stall_seconds())return 0;
    network_stall.last_recovery=now;network_stall.has_recovered=1;network_stall.active=0;
    LOG("Network stall: confirmed after native I/O; r=%d received_maxT=%d quiet_ms=%.0f game=%u world=%u; one native reconnect requested. No readiness or player state changed.",
        s.reserve,s.t,(now-network_stall.last_progress)*1000,s.game,s.world);
    return 1;
}
static void network_stall_frame(void){
    struct NetworkStallSample s;double now=network_stall_clock();
    if(!network_stall_read(&s)){network_stall_clear();return;}
    if(network_stall.has_recovered&&now<network_stall.last_recovery)network_stall.has_recovered=0;
    if(!network_stall.active||!network_stall_same(&s,&network_stall.sample)||now<network_stall.last_frame||now-network_stall.last_frame>2){
        network_stall.sample=s;network_stall.active=1;network_stall.low=network_stall.queued_valid=0;
        network_stall.began=network_stall.last_frame=network_stall.last_progress=now;return;
    }
    network_stall.last_frame=now;
    if(s.t!=network_stall.sample.t){network_stall.last_progress=now;network_stall.low=0;}
    network_stall.sample=s;
    if(s.reserve>20){network_stall.low=0;return;}
    if(!network_stall.low){network_stall.low=1;network_stall.low_since=now;}
    if(network_stall.queued_valid||now-network_stall.began<3||now-network_stall.low_since<1||
       now-network_stall.last_progress<network_stall_seconds()||
       (network_stall.has_recovered&&now-network_stall.last_recovery<30))return;
    network_stall.queued=s;network_stall.queued_valid=1;
    if(!network_stall_request(s.connection,network_stall_confirm))network_stall.queued_valid=0;
    /* No socket/world work here. Recovery can run only after native dispatch. */
}
#endif
