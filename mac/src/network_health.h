/* Read-only correlation of r=0, received world time and render gaps. Socket
 * statistics contain counters only; never read payloads or cipher material. */
#ifndef SBPE_NETWORK_HEALTH_H
#define SBPE_NETWORK_HEALTH_H
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
_Static_assert(offsetof(struct ClientWorld,timeReserve)==0x510,"Mac time reserve ABI");
struct NetworkHealthSample {
    const void *connection,*world;int socket,reserve,max_t,local_t,spawned,hold;
    uint32_t game,id,window_flags;int window_known;
};
static struct {
    struct NetworkHealthSample sample;
    double began,last_frame,last_progress,candidate,last_report,max_frame_gap;
    int active,reported;unsigned reports;
} network_health;
static double (*network_health_clock)(void)=monotime;
static int network_health_tcp(int socket,struct tcp_connection_info *info){
    socklen_t size=sizeof(*info);memset(info,0,sizeof(*info));
    return !getsockopt(socket,IPPROTO_TCP,TCP_CONNECTION_INFO,info,&size)&&
        size>=offsetof(struct tcp_connection_info,tcpi_rxoutoforderbytes)+sizeof(info->tcpi_rxoutoforderbytes);
}
#ifdef SBPE_TEST
static int (*network_health_transport)(int,struct tcp_connection_info*);
#else
static int (*network_health_transport)(int,struct tcp_connection_info*)=network_health_tcp;
#endif
static const char *network_health_mode(int hold){return hold?"loading-held":"ordinary";}
static void network_health_emit(const char *event,const struct NetworkHealthSample *s,double now){
    int saved_errno=errno;
    LOG("Network health: %s; mode=%s player=%s game=%u world=%u r=%d maxT=%d own_t=%d world_progress_quiet_ms=%.0f largest_frame_gap_ms=%.0f socket=%d focus=%s minimized=%s.",
        event,network_health_mode(s->hold),s->spawned?"present":"absent",s->game,s->id,s->reserve,s->max_t,s->local_t,
        fmax(0,now-network_health.last_progress)*1000,network_health.max_frame_gap*1000,s->socket,s->window_known?(s->window_flags&0x200?"foreground":"background"):"unknown",s->window_known?(s->window_flags&0x40?"yes":"no"):"unknown");
    struct tcp_connection_info info;
    if(network_health_transport&&network_health_transport(s->socket,&info)){
        LOG("Network health: TCP state=%u rtt_current_ms=%u rtt_average_ms=%u rtt_variance=%u send_pending_bytes=%u rx_bytes=%llu tx_bytes=%llu rx_out_of_order_bytes=%llu tx_retransmitted_bytes=%llu. Counters are cumulative for this socket, not packet-loss percentages.",
            info.tcpi_state,info.tcpi_rttcur,info.tcpi_srtt,info.tcpi_rttvar,info.tcpi_snd_sbbytes,
            (unsigned long long)info.tcpi_rxbytes,(unsigned long long)info.tcpi_txbytes,
            (unsigned long long)info.tcpi_rxoutoforderbytes,(unsigned long long)info.tcpi_txretransmitbytes);
    }else LOG("Network health: TCP counters unavailable; no inference about packet loss.");
    errno=saved_errno;
}
static void network_health_update(const struct NetworkHealthSample *s,double now){
    if(!s){memset(&network_health,0,sizeof(network_health));return;}
    struct NetworkHealthSample *old=&network_health.sample;
    if(!network_health.active||old->connection!=s->connection||old->socket!=s->socket||old->world!=s->world||old->game!=s->game||old->id!=s->id||now<network_health.last_frame){
        memset(&network_health,0,sizeof(network_health));network_health.active=1;network_health.sample=*s;
        network_health.began=network_health.last_frame=network_health.last_progress=now;return;
    }
    double gap=now-network_health.last_frame;
    if(gap>network_health.max_frame_gap)network_health.max_frame_gap=gap;
    network_health.last_frame=now;
    if(old->max_t!=s->max_t)network_health.last_progress=now;
    network_health.sample=*s;
    /* Ignore entry transients and deliberate unspawned holds. At r<100,
       require 250ms persistence; missing world progress also reports at 750ms. */
    int low=s->spawned&&!s->hold&&now-network_health.began>=2&&
        (s->reserve<100||now-network_health.last_progress>=.75);
    if(low){
        if(!network_health.candidate)network_health.candidate=now;
        if(now-network_health.candidate>=.25&&(!network_health.reported||now-network_health.last_report>=5)&&network_health.reports<60){
            network_health_emit(network_health.reported?"update stall continuing":"update stall observed",s,now);
            network_health.reported=1;network_health.last_report=now;++network_health.reports;network_health.max_frame_gap=0;
        }
    }else{
        if(network_health.reported)network_health_emit(!s->spawned||s->hold?"stall observation ended (player absent or held)":"updates recovered",s,now);
        network_health.candidate=network_health.last_report=0;network_health.reported=0;network_health.reports=0;network_health.max_frame_gap=0;
    }
}
static void network_health_frame(struct WorldClient *wc){
    if(!getbool("native","network_health_diagnostics",1)||!wc||!wc->clientWorld||!wc->serverConnection||wc->serverConnection->state!=CONNECTED){network_health_update(NULL,0);return;}
    struct ClientWorld *cw=wc->clientWorld;
    struct NetworkHealthSample s={.connection=wc->serverConnection,.world=cw,.socket=wc->serverConnection->socket,
        .reserve=cw->timeReserve,.max_t=probe_world_t(cw),.local_t=probe_client_t(cw),.spawned=cw->player!=NULL,
        .hold=loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT,
        .game=cw->asWorld.props.gameid,.id=cw->asWorld.id};
    if(window_ptr&&*window_ptr&&window_flags){s.window_flags=window_flags(*window_ptr);s.window_known=1;}
    network_health_update(&s,network_health_clock());
}
static void network_health_lost(const void *connection){
    if(!getbool("native","network_health_diagnostics",1))return;
    struct NetworkHealthSample s={.connection=connection,.socket=((const struct ServerConnection*)connection)->socket,
        .hold=loading_probe.state==PROBE_HOLD||loading_probe.state==PROBE_WAIT};
    if(network_health.active&&network_health.sample.connection==connection&&network_health.sample.socket==s.socket){
        int held=s.hold;s=network_health.sample;s.hold=held;
    }else{network_health_update(NULL,0);network_health.last_progress=network_health_clock();}
    network_health_emit("connection loss snapshot",&s,network_health_clock());
    network_health_update(NULL,0);
}
#endif
