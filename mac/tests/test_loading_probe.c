#define SBPE_TEST
#define LOADING_PROBE_HOOK_TEST
#include "../src/sbpe.c"
#define SBPE_NETWORK_GUARD_TEST
#include "../src/network_guard.h"
#include <assert.h>
static double fake_time=100;
static struct WorldClient *current;
static unsigned sends;static int last_type;
static struct ServerConnection connection;
static struct WorldClient wc;
static struct ClientWorld cw;
static struct ClientSubWorld own;
static uint8_t subworld[0x1a0];
static struct Player actual_player;
static unsigned proto_sends,proto_time_ms,wire_order,ready_order,input_order;
static int last_input_total;static unsigned last_input_bits;
static struct CreatePlayer incoming_player;
static unsigned observed_dispatches;static int decode_ok=1;
static struct STDString forest,lobby,cave,cathedral,ice,ice_boss,ice_hard,empty;
static double now(void){return fake_time;}
static void advance_ms(unsigned ms){fake_time+=(double)ms/1000;}
static void sent_proto(struct ServerConnection *c,int type,const void *message){
    assert(c==&connection);++proto_sends;++wire_order;
    if(type==MT_PLAYER_INPUT){const struct PlayerInput *p=message;last_input_total=p->total;last_input_bits=p->_has_bits[0];input_order=wire_order;}
    if(type==MT_TIME_PASSES){int dt=0;memcpy(&dt,(const uint8_t*)message+8,4);assert(dt>0);proto_time_ms+=(unsigned)dt;}
}
static struct WorldClient *world(void){return current;}
static void sent(struct ServerConnection *c,int type){assert(c==&connection);++sends;last_type=type;++wire_order;if(type==MT_WORLD_CREATED)ready_order=wire_order;}
static void string(struct STDString *s,const char *v){memset(s,0,sizeof(*s));s->data[0]=(uint8_t)(strlen(v)<<1);memcpy(s->data+1,v,strlen(v));}
static void received_time(int t){memcpy(subworld+0x178,&t,4);}
static unsigned char decode_spawn(int type,const char *data,int length,void *out){
    (void)type;(void)data;(void)length;struct Delta *d=out;memset(d,0,sizeof(*d));d->_has_bits[0]=8;d->createplayer=&incoming_player;return (unsigned char)decode_ok;
}
static void apply_observed_delta(void *client,int type,const char *data,int length){(void)type;(void)data;(void)length;assert(client==&wc);++observed_dispatches;}
static int lookup(const char *s){return !strcmp(s,"F9")?66:!strcmp(s,"F10")?67:0;}
static void context(void){
    memset(&wc,0,sizeof(wc));memset(&cw,0,sizeof(cw));memset(&own,0,sizeof(own));memset(&connection,0,sizeof(connection));memset(subworld,0,sizeof(subworld));memset(&actual_player,0,sizeof(actual_player));
    connection.state=CONNECTED;connection.socket=9;wc.serverConnection=&connection;wc.clientWorld=&cw;current=&wc;
    cw.asWorld.id=23;cw.asWorld.props.gameid=456;cw.asWorld.props.zone=&lobby;cw.asWorld.props.music=&lobby;cw.asWorld.props.safe=1;
    cw.serverSubWorld=(void*)subworld;cw.mySubWorld=&own;wc.lastUpdate=123456;received_time(1000);sends=0;proto_sends=proto_time_ms=wire_order=ready_order=input_order=0;last_input_total=0;last_input_bits=0;
}
static void target(void){cw.asWorld.props.zone=cw.asWorld.props.music=&forest;cw.asWorld.props.safe=0;}
static void arm(void){context();probe_arm();assert(loading_probe.state==PROBE_ARMED);target();}
static void hold(void){arm();loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_HOLD&&!sends);}
static void test_lifecycle(void){
    hold();assert(!loading_probe.player_before&&loading_probe.world_t0==1000);
    loading_probe_send(&connection,MT_SERVER_DELTA_ACK);assert(sends==1&&last_type==MT_SERVER_DELTA_ACK);
    loading_probe_send(&connection,MT_PVID_LOADED);assert(sends==2&&last_type==MT_PVID_LOADED);
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==2); /* no duplicate saved ack */
    fake_time+=1;received_time(1050);loading_probe_frame();assert(loading_probe.advances==1&&!loading_probe.player_before&&sends==2);
    fake_time+=3.999;loading_probe_frame();assert(loading_probe.state==PROBE_HOLD&&sends==2);
    fake_time+=.002;loading_probe_frame();assert(loading_probe.state==PROBE_RESULT&&sends==3&&last_type==MT_WORLD_CREATED);
    loading_probe_frame();assert(sends==3);
    cw.player=&actual_player;fake_time+=.05;loading_probe_frame();assert(loading_probe.spawn_logged&&!loading_probe.player_before&&sends==3);
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==4); /* one-shot does not rearm */
}
static void test_frozen_blight(void){
    set_option("plugin_loading_probe","hold_seconds","15");
    arm();cw.asWorld.props.zone=cw.asWorld.props.music=&ice;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_HOLD&&!sends);
    fake_time+=14;received_time(15000);loading_probe_frame();assert(loading_probe.state==PROBE_HOLD&&loading_probe.advances==1&&!sends);
    fake_time+=1.01;loading_probe_frame();assert(loading_probe.state==PROBE_RESULT&&sends==1&&last_type==MT_WORLD_CREATED);
    cw.player=&actual_player;fake_time+=.25;loading_probe_frame();assert(loading_probe.spawn_logged&&!loading_probe.player_before&&sends==1);
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==2); /* one shot */
    /* Boss rooms can have an empty zone and use the ice_boss music name. */
    arm();cw.asWorld.props.zone=&empty;cw.asWorld.props.music=&ice_boss;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_HOLD&&!sends);probe_release("manual release");assert(sends==1);
    arm();cw.asWorld.props.zone=&ice_boss;cw.asWorld.props.music=&empty;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_HOLD&&!sends);probe_release("manual release");assert(sends==1);
    /* Matching boss music must not bypass elite/safe-room exclusions. */
    arm();cw.asWorld.props.zone=&ice_hard;cw.asWorld.props.music=&ice_boss;cw.asWorld.props.gdv=1;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_IDLE&&sends==1);
    arm();cw.asWorld.props.zone=cw.asWorld.props.music=&ice_hard;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_IDLE&&sends==1);
    arm();cw.asWorld.props.zone=cw.asWorld.props.music=&ice;cw.asWorld.props.safe=1;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_IDLE&&sends==1);
    set_option("plugin_loading_probe","hold_seconds","5");
}
static void test_scope_and_cancellation(void){
    arm();cw.asWorld.props.zone=cw.asWorld.props.music=&cave;loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.state==PROBE_IDLE);
    arm();cw.asWorld.props.zone=cw.asWorld.props.music=&cathedral;loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.state==PROBE_IDLE);
    arm();cw.asWorld.props.gdv=1;loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.state==PROBE_IDLE);
    arm();cw.player=&actual_player;loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.state==PROBE_IDLE);
    context();probe_arm();loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.state==PROBE_ARMED); /* lobby ignored */
    target();fake_time+=91;loading_probe_frame();assert(loading_probe.state==PROBE_IDLE);
    hold();cw.player=&actual_player;loading_probe_frame();assert(sends==1&&loading_probe.player_before&&loading_probe.state==PROBE_RESULT);
    hold();current=NULL;loading_probe_frame();assert(!sends&&loading_probe.state==PROBE_RESULT);
    hold();connection.socket=10;loading_probe_frame();assert(!sends&&loading_probe.state==PROBE_RESULT);
    hold();cw.asWorld.id++;loading_probe_frame();assert(!sends&&loading_probe.state==PROBE_RESULT);
    hold();set_option("plugin_loading_probe","enabled","no");loading_probe_frame();assert(sends==1&&loading_probe.state==PROBE_RESULT);
    set_option("plugin_loading_probe","enabled","yes");
    arm();set_option("plugin_loading_probe","enabled","no");loading_probe_frame();assert(loading_probe.state==PROBE_IDLE);
    struct Stage st={0};assert(!loading_probe_consumes(&st,0x40000042));
    set_option("plugin_loading_probe","enabled","yes");
    hold();probe_release("manual release");assert(sends==1&&loading_probe.state==PROBE_RESULT);probe_release("again");assert(sends==1);
    context();target();loading_probe.state=PROBE_IDLE;probe_arm();assert(loading_probe.state==PROBE_IDLE); /* cannot arm in a mission */
    context();set_option("plugin_loading_probe","hold_seconds","999");probe_arm();assert(loading_probe.seconds==30);
    set_option("plugin_loading_probe","hold_seconds","-1");probe_arm();assert(loading_probe.seconds==1);
    set_option("plugin_loading_probe","hold_seconds","5");
}
static void test_keys(void){
    uint8_t keys[128]={0};probe_f9=probe_f10=0;context();loading_probe.state=PROBE_IDLE;
    keys[66]=1;loading_probe_keys(keys,128,0);assert(loading_probe.state==PROBE_IDLE);
    loading_probe_keys(keys,128,1);assert(loading_probe.state==PROBE_IDLE); /* held across chat is not a fresh press */
    keys[66]=0;loading_probe_keys(keys,128,1);keys[66]=1;loading_probe_keys(keys,128,1);assert(loading_probe.state==PROBE_ARMED);
    loading_probe_keys(keys,128,1);assert(loading_probe.state==PROBE_ARMED); /* repeat */
    keys[67]=1;loading_probe_keys(keys,128,1);assert(loading_probe.state==PROBE_IDLE);
    struct Stage st={0};assert(loading_probe_consumes(&st,0x40000042));st.textInputTarget=(void*)1;assert(!loading_probe_consumes(&st,0x40000042));
    hold();memset(keys,0,sizeof(keys));loading_probe_keys(keys,128,1);keys[67]=1;loading_probe_keys(keys,128,1);assert(sends==1&&loading_probe.state==PROBE_RESULT);
}
static void test_input_and_time(void){
    hold();struct PlayerInput p={._has_bits={2},.hit=4};cw.total=4;
    loading_probe_send_proto(&connection,MT_PLAYER_INPUT,&p);assert(proto_sends==1&&loading_probe.input_messages==1);
    cw.total=0;p.hit=0;p.unhit=4;p._has_bits[0]=4;
    loading_probe_send_proto(&connection,MT_PLAYER_INPUT,&p);assert(proto_sends==2&&loading_probe.input_messages==2);
    /* Stand in for the unchanged native simulation: time and loot physics
       must keep advancing, and every time message must reach the sender. */
    unsigned char time_message[16]={0};int dt=20;memcpy(time_message+8,&dt,4);
    for(int ms=20;ms<=4980;ms+=20){advance_ms(20);own.asNativeSubWorld.asSubWorldImpl.asSubWorld.t+=dt;received_time(1000+ms);loading_probe_send_proto(&connection,MT_TIME_PASSES,time_message);}
    assert(proto_sends==251&&proto_time_ms==4980&&loading_probe.time_ms==4980&&loading_probe.time_messages==249&&probe_client_t(&cw)==4980);
    unsigned before=proto_sends;probe_release("manual release");assert(sends==1&&proto_sends==before&&ready_order>input_order); /* ready emits no synthetic input, even zero */
    assert(wc.lastUpdate==123456&&probe_client_t(&cw)==4980); /* no clock reset */
    loading_probe_send_proto(&connection,MT_PLAYER_INPUT,&p);assert(proto_sends==before+1); /* normal after ready */
    hold();cw.total=0x25;p.total=0x25;p._has_bits[0]=1;loading_probe_send_proto(&connection,MT_PLAYER_INPUT,&p);probe_release("manual release");assert(last_input_total==0x25&&last_input_bits==1&&proto_sends==1&&input_order<ready_order);
    probe_release("again");assert(proto_sends==1); /* no duplicate/replay at release */
    hold();connection.socket++;probe_release("changed socket");assert(!sends&&!proto_sends); /* no stale resend */
    context();loading_probe.state=PROBE_IDLE;loading_probe_send_proto(&connection,MT_PLAYER_INPUT,&p);assert(proto_sends==1); /* unarmed passthrough */
    hold();loading_probe_send_proto(&connection,MT_SERVER_DELTA_ACK,&p);assert(proto_sends==1&&!loading_probe.input_messages); /* stateful ack preserved */
    probe_release("manual release");loading_probe_frame();assert(!loading_probe.post_closed);advance_ms(60001);loading_probe_frame();assert(loading_probe.post_closed);
    hold();probe_release("manual release");connection.socket++;loading_probe_frame();assert(loading_probe.post_closed);
}
static void test_spawn_diagnostics(void){
    hold();struct CreatePlayer p={0};struct RectMessage pos={.xmp=1024,.ymp=2048};struct Delta d={.createplayer=&p,._has_bits={8}};
    p.pos=&pos;p._has_bits[0]=0x20;loading_probe_received(&wc,&d);assert(loading_probe.create_logged&&!cw.player&&!sends); /* observes, never applies/mutates */
    hold();d._has_bits[0]=12;loading_probe_received(&wc,&d);assert(!loading_probe.create_logged); /* matches native first-field precedence */
    d._has_bits[0]=8;probe_release("manual release");loading_probe_received(&wc,&d);assert(loading_probe.create_logged&&!cw.player);
    /* Exercise the production receive-guard integration: only an accepted
       decoded CreatePlayer reaches the diagnostic, then native continuation. */
    network_decode_delta=decode_spawn;network_continue_delta=apply_observed_delta;observed_dispatches=0;decode_ok=1;
    hold();network_server_delta(&wc,MT_SERVER_DELTA,"",0);assert(loading_probe.create_logged&&observed_dispatches==1&&!cw.player);
    hold();decode_ok=0;network_reconnect_pending=NULL;network_server_delta(&wc,MT_SERVER_DELTA,"",0);
    assert(!loading_probe.create_logged&&observed_dispatches==1&&!network_reconnect_pending);network_reconnect_pending=NULL;decode_ok=1;
    hold();network_loss_observe(&connection,0x11);
    assert(loading_probe.state==PROBE_HOLD&&!sends&&!proto_sends); /* loss telemetry does not release/recover */
    probe_release("manual release");network_loss_observe(&connection,0x01);
    assert(loading_probe.state==PROBE_RESULT&&sends==1&&!proto_sends);
}
static void test_native_proto_entry(void){
#ifdef __x86_64__
    const uint8_t body[]={0x89,0x77,0x4c,0xff,0x47,0x50,0x8b,0x42,8,0x89,0x47,0x74,0x48,0x83,0xc4,0x78,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    size_t page=(size_t)sysconf(_SC_PAGESIZE);uint8_t *code=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(code!=MAP_FAILED);
    memcpy(code,probe_proto_original,17);memcpy(code+17,body,sizeof(body));assert(!mprotect(code,page,PROT_READ|PROT_EXEC));
    assert(loading_probe_proto_hook(code));uint8_t bad[17]={0};assert(!loading_probe_proto_hook(bad));void (*entry)(struct ServerConnection*,int,const void*)=(void*)code;
    hold();struct PlayerInput p={.total=0x25,._has_bits={1}};cw.total=0x25;
    entry(&connection,MT_PLAYER_INPUT,&p);assert(connection.waitTime==1&&connection.inputLen==0x25&&loading_probe.input_messages==1);
    unsigned char time_message[16]={0};int dt=20;memcpy(time_message+8,&dt,4);entry(&connection,MT_TIME_PASSES,time_message);
    assert(connection.waitTime==2&&connection.retriesLeft==MT_TIME_PASSES&&connection.inputLen==20&&loading_probe.time_ms==20);
    probe_release("manual release");assert(sends==1&&connection.waitTime==2&&connection.retriesLeft==MT_TIME_PASSES&&connection.inputLen==20); /* no manufactured input */
    entry(&connection,MT_PLAYER_INPUT,&p);assert(connection.waitTime==3);
    probe_original_proto=sent_proto;munmap(code,page);
#endif
}
static void test_native_entry(void){
#ifdef __x86_64__
    const uint8_t body[]={0x89,0x73,0x4c,0xff,0x43,0x50,0x48,0x83,0xc4,8,0x5b,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    size_t page=(size_t)sysconf(_SC_PAGESIZE);uint8_t *code=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(code!=MAP_FAILED);
    memcpy(code,probe_send_original,17);memcpy(code+17,body,sizeof(body));assert(!mprotect(code,page,PROT_READ|PROT_EXEC));
    assert(loading_probe_hook(code));uint8_t bad[17]={0};assert(!loading_probe_hook(bad));
    void (*entry)(struct ServerConnection*,int)=(void*)code;
    arm();entry(&connection,MT_WORLD_CREATED);assert(!connection.waitTime&&loading_probe.state==PROBE_HOLD);
    entry(&connection,MT_PVID_LOADED);assert(connection.waitTime==1&&connection.retriesLeft==MT_PVID_LOADED);
    fake_time+=5.01;loading_probe_frame();assert(connection.waitTime==2&&connection.retriesLeft==MT_WORLD_CREATED&&loading_probe.state==PROBE_RESULT);
    entry(&connection,MT_SERVER_DELTA_ACK);assert(connection.waitTime==3&&connection.retriesLeft==MT_SERVER_DELTA_ACK);
    probe_original_send=sent;munmap(code,page);
#endif
}
static unsigned io_calls,reconnect_calls;
static unsigned char fresh_io(void *p){assert(p==&connection);++io_calls;return 1;}
static unsigned char fresh_failure(void *p){
    assert(p==&connection&&io_calls&&loading_probe.reconnect_issued&&network_loss_synthetic==2);
    assert(!sends);++reconnect_calls;network_loss_observe(p,0xffff);connection.state=4;connection.socket=-1;return 0;
}
static void clean_context(void){
    memset(&network_loading_reconnect,0,sizeof(network_loading_reconnect));io_calls=reconnect_calls=0;
    network_continue_io=fresh_io;network_failure_io=fresh_failure;probe_reconnect_request=network_request_loading_reconnect;
    set_option("plugin_loading_probe","reconnect_on_release","yes");hold();
}
static void test_clean_reconnect(void){
    clean_context();own.asNativeSubWorld.asSubWorldImpl.asSubWorld.t=15000;
    fake_time+=5.01;loading_probe_frame();assert(loading_probe.state==PROBE_RECONNECT&&!sends&&!reconnect_calls);
    assert(network_loading_reconnect.connection==&connection&&probe_client_t(&cw)==15000&&wc.lastUpdate==123456);
    probe_release("repeat");assert(loading_probe.state==PROBE_RECONNECT&&!sends); /* no duplicate release */
    assert(network_do_io(&connection)==0&&io_calls==1&&reconnect_calls==1&&!network_loss_synthetic&&!network_loading_reconnect.connection);
    assert(probe_client_t(&cw)==15000&&!sends); /* old clock is never edited */
    /* Native constructor supplies a fresh world, potentially reusing the same
       addresses. Its readiness must pass once, with no second hold. */
    connection.state=CONNECTED;connection.socket=10;cw.asWorld.id=24;
    own.asNativeSubWorld.asSubWorldImpl.asSubWorld.t=0;received_time(0);
    loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==1&&loading_probe.state==PROBE_RESULT&&loading_probe.reloaded&&loading_probe.mission_retained&&!loading_probe.create_logged);
    cw.player=&actual_player;fake_time+=.2;loading_probe_frame();assert(loading_probe.spawn_logged&&probe_client_t(&cw)==0&&reconnect_calls==1);
    /* A return to lobby is recorded as a changed mission, never success. */
    clean_context();probe_release("manual");assert(network_do_io(&connection)==0);
    connection.state=CONNECTED;connection.socket=10;cw.asWorld.id++;cw.asWorld.props.zone=&lobby;cw.asWorld.props.safe=1;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.reloaded&&!loading_probe.mission_retained);
    /* Receive dispatch may change the world after queueing but before the
       request runs. Stale validation must prevent an unintended reconnect. */
    clean_context();probe_release("manual");cw.asWorld.id++;
    assert(network_do_io(&connection)==1&&!reconnect_calls&&!network_loading_reconnect.connection);
    loading_probe_frame();assert(loading_probe.state==PROBE_RESULT&&loading_probe.post_closed&&!sends);
    clean_context();probe_release("manual");cw.player=&actual_player;
    assert(network_do_io(&connection)==1&&!reconnect_calls&&sends==1&&loading_probe.player_before&&loading_probe.state==PROBE_RESULT);
    clean_context();probe_release("manual");connection.socket++;
    assert(network_do_io(&connection)==1&&!reconnect_calls&&!network_loading_reconnect.connection);
    /* A genuine loss racing the queued request uses the first native reload;
       the next world is not held and no repeated reconnect is requested. */
    clean_context();probe_release("manual");loading_probe_connection_lost(&connection,"native read false");
    assert(loading_probe.reconnect_issued);connection.socket++;cw.asWorld.id++;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.reloaded);
    assert(network_do_io(&connection)==1&&!reconnect_calls&&!network_loading_reconnect.connection);
    /* Without a verified recovery hook, clean-reconnect mode cannot arm. */
    context();probe_reconnect_request=NULL;probe_arm();assert(loading_probe.state==PROBE_IDLE);
    set_option("plugin_loading_probe","reconnect_on_release","no");
    memset(&network_loading_reconnect,0,sizeof(network_loading_reconnect));
}
static void test_passive_observation(void){
    context();target();loading_probe.state=PROBE_IDLE;memset(&loading_observation,0,sizeof(loading_observation));
    set_option("plugin_loading_probe","observe_spawns","yes");
    struct ClientWorld before=cw;struct ClientSubWorld own_before=own;struct ServerConnection connection_before=connection;
    loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==1&&loading_observation.active&&!loading_observation.present&&loading_probe.state==PROBE_IDLE);
    assert(!memcmp(&cw,&before,sizeof(cw))&&!memcmp(&own,&own_before,sizeof(own))&&!memcmp(&connection,&connection_before,sizeof(connection)));
    assert(loading_observation.previous_epoch);
    cw.player=&actual_player;fake_time+=.2;loading_probe_frame();
    assert(loading_observation.present&&sends==1&&loading_probe.state==PROBE_IDLE);
    double sampled=loading_observation.last_position;fake_time+=.01;loading_probe_frame();assert(loading_observation.last_position==sampled);
    fake_time+=.11;loading_probe_frame();assert(loading_observation.last_position>sampled);
    cw.player=NULL;fake_time+=.01;loading_probe_frame();assert(!loading_observation.present);
    connection.state=4;loading_probe_frame();assert(!loading_observation.active);
    connection.state=CONNECTED;cw.player=&actual_player;loading_probe_frame();assert(loading_observation.present);
    sampled=loading_observation.last_position;fake_time+=91;loading_probe_frame();assert(loading_observation.last_position==sampled);
    cw.asWorld.id++;loading_probe_frame();assert(loading_observation.world==cw.asWorld.id&&loading_observation.last_position>sampled);
    set_option("plugin_loading_probe","observe_spawns","no");loading_probe_frame();assert(!loading_observation.active&&sends==1);
    set_option("plugin_loading_probe","observe_spawns","yes");
}
int main(void){
    probe_world=world;probe_clock=now;probe_original_send=sent;loading_probe_installed=1;key_from_name=lookup;
    probe_original_proto=sent_proto;
    string(&forest,"forest");string(&lobby,"lobby");string(&cave,"cave");string(&cathedral,"sc_boss");
    string(&ice,"ice");string(&ice_boss,"ice_boss");string(&ice_hard,"iceHard");string(&empty,"");
    set_option("plugin_loading_probe","enabled","yes");set_option("plugin_loading_probe","hold_seconds","5");set_option("plugin_loading_probe","reconnect_on_release","no");
    test_lifecycle();test_frozen_blight();test_scope_and_cancellation();test_keys();test_input_and_time();test_spawn_diagnostics();test_native_proto_entry();test_native_entry();test_clean_reconnect();test_passive_observation();
    puts("PASS: queued clean reconnect outside callbacks, old ready discarded, fresh ready never held, old clocks untouched, mission/lobby classification, stale socket/world cancellation and natural-loss race; no reconnect loop.");
    puts("PASS: Fire Forest/Frozen Blight one-shot scope, normal physics/time path preserved, native inputs forwarded without omissions, no synthetic input at release, stateful acknowledgments, spawn diagnostics and post-release monitoring, original lifecycle/key/stale-world guards.");
#ifdef __x86_64__
    puts("PASS: actual Intel empty/protobuf send entry trampolines, third-argument payload forwarding, input/time-message passthrough without reordering or release injection.");
#endif
    return 0;
}
