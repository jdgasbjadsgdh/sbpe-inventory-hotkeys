/* Exercise the manual any-map extension alongside every previous loading test.
 * Uses local fixture worlds, never a live client, login or remote socket. */
#define main normal_loading_fixture_main
#include "test_loading_probe.c"
#undef main
static unsigned manual_losses,forwarded_clicks,gui_rectangles,gui_texts;
static struct GameClient fixture_gc;
static struct GameClient *fixture_game(void){return &fixture_gc;}
static int manual_lookup(const char *s){return !strcmp(s,"F8")?65:lookup(s);}
static unsigned char manual_failure(void *p){
    assert(p==&connection&&network_loss_synthetic==2&&loading_probe.reconnect_issued);
    ++manual_losses;network_loss_observe(p,0);connection.state=4;connection.socket=-1;return 0;
}
static unsigned char stalled_hold_failure(void *p){
    assert(p==&connection&&network_loss_synthetic==1&&!loading_probe.reconnect_issued);
    ++manual_losses;network_loss_observe(p,0);assert(loading_probe.state==PROBE_WAIT);
    connection.state=4;connection.socket=-1;return 0;
}
static void manual_context(void){
    context();set_option("plugin_loading_probe","mode","manual");
    memset(&fixture_gc,0,sizeof(fixture_gc));fixture_gc.serverConnection=&connection;fixture_gc.worldClient=&wc;probe_game=fixture_game;probe_preset=PROBE_ANY;
    memset(&loading_probe,0,sizeof(loading_probe));memset(&network_loading_reconnect,0,sizeof(network_loading_reconnect));
    network_continue_io=fresh_io;network_failure_io=manual_failure;probe_reconnect_request=network_request_loading_reconnect;
    manual_losses=io_calls=0;key_from_name=manual_lookup;
}
static void manual_hold(struct STDString *zone){
    manual_context();cw.player=&actual_player;probe_arm();assert(loading_probe.state==PROBE_ARMED&&loading_probe.manual);
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_ARMED&&sends==1); /* current room stays ordinary */
    cw.asWorld.id++;cw.player=NULL;cw.asWorld.props.zone=cw.asWorld.props.music=zone;cw.asWorld.props.gdv=zone==&ice_hard?1:0;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_HOLD&&sends==1&&!loading_probe.deadline);
}
static void test_manual_maps(void){
    struct STDString *maps[]={&cathedral,&cave,&forest,&ice,&ice_hard,&lobby};
    for(unsigned i=0;i<sizeof(maps)/sizeof(*maps);++i){
        manual_hold(maps[i]); /* Includes safe rooms and elite variants. */
        fake_time+=600;received_time(601000);loading_probe_frame();
        assert(loading_probe.state==PROBE_HOLD&&sends==1&&loading_probe.advances);
        loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1);
        loading_probe_send(&connection,MT_SERVER_DELTA_ACK);assert(sends==2);
        unsigned char time[16]={0};int dt=20;memcpy(time+8,&dt,4);
        loading_probe_send_proto(&connection,MT_TIME_PASSES,time);assert(proto_sends==1&&loading_probe.time_messages==1);
        probe_manual_reconnect();assert(loading_probe.state==PROBE_RECONNECT&&!manual_losses&&sends==2);
        probe_manual_reconnect();assert(network_loading_reconnect.connection==&connection);
        assert(network_do_io(&connection)==0&&manual_losses==1&&sends==2);
        connection.state=CONNECTED;connection.socket=10;cw.asWorld.id++;received_time(0);
        loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==3&&loading_probe.state==PROBE_RESULT&&loading_probe.reloaded);
        loading_probe_frame();assert(manual_losses==1); /* no auto rearm/reconnect */
    }
    manual_context();probe_arm();fake_time+=1000;loading_probe_frame();assert(loading_probe.state==PROBE_ARMED);
    probe_cancel();assert(loading_probe.state==PROBE_IDLE&&!network_loading_reconnect.connection);
    /* An unexpected player ends observation without old-ready replay or a forced loss. */
    manual_hold(&cathedral);cw.player=&actual_player;loading_probe_frame();assert(loading_probe.state==PROBE_RESULT&&sends==1&&!manual_losses);
    /* Natural losses, including a trickling-frame deadline, never authorize
       player readiness. Recovered worlds remain held until a manual finish. */
    manual_hold(&cathedral);loading_probe_connection_lost(&connection,"socket hangup reported by poll");
    assert(loading_probe.state==PROBE_WAIT&&!loading_probe.reconnect_issued&&!network_loading_reconnect.connection);
    double started=loading_probe.started;
    connection.state=4;fake_time+=300;loading_probe_frame();assert(loading_probe.state==PROBE_WAIT&&sends==1);
    for(int i=0;i<4;++i){
        connection.state=CONNECTED;connection.socket++;cw.asWorld.id++;received_time(0);
        loading_probe_send(&connection,MT_WORLD_CREATED);
        assert(loading_probe.state==PROBE_HOLD&&sends==1&&!manual_losses&&loading_probe.started==started&&loading_probe.recoveries==i+1);
        loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1);
        loading_probe_connection_lost(&connection,"incomplete-message deadline");assert(loading_probe.state==PROBE_WAIT);
    }
    /* Explicit finish during native recovery authorizes exactly its next load,
       without requesting another close or needing a live WorldClient. */
    connection.state=2;probe_manual_reconnect();
    assert(loading_probe.state==PROBE_RECONNECT&&loading_probe.reconnect_issued&&!network_loading_reconnect.connection);
    connection.state=CONNECTED;connection.socket++;cw.asWorld.id++;loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==2&&loading_probe.state==PROBE_RESULT&&!manual_losses);
    /* A new world arriving before the render/loss callback is also re-held. */
    manual_hold(&cave);connection.socket++;cw.asWorld.id++;loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==1&&loading_probe.state==PROBE_HOLD&&loading_probe.recoveries==1);
    cw.asWorld.id++;loading_probe_frame();assert(loading_probe.state==PROBE_WAIT);
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.state==PROBE_HOLD);
    probe_manual_reconnect();assert(network_do_io(&connection)==0&&manual_losses==1);
    connection.state=CONNECTED;connection.socket++;cw.asWorld.id++;loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==2);
    /* Cancel is an explicit finish, also when there is no live connection. */
    manual_hold(&cave);loading_probe_connection_lost(&connection,"hangup");connection.state=2;probe_cancel();
    assert(loading_probe.state==PROBE_RECONNECT&&loading_probe.reconnect_issued);
    /* Actual doIO watchdog composition: trickling bytes exhaust the assembly
       deadline after native I/O returns; the loss hook preserves manual intent. */
    manual_hold(&cave);network_failure_io=stalled_hold_failure;
    uint8_t type=37;uint32_t length=9591167;uint64_t received=100;
    memcpy((uint8_t*)&connection+0x70,&type,1);memcpy((uint8_t*)&connection+0x74,&length,4);memcpy((uint8_t*)&connection+0xc0,&received,8);
    memset(&network_partial,0,sizeof(network_partial));assert(network_do_io(&connection)==1);
    network_partial.started_ms=network_progress_now()-11000;++received;memcpy((uint8_t*)&connection+0xc0,&received,8);
    assert(network_do_io(&connection)==0&&manual_losses==1&&loading_probe.state==PROBE_WAIT&&!network_loss_synthetic);
    connection.state=CONNECTED;connection.socket=10;cw.asWorld.id++;loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==1&&loading_probe.state==PROBE_HOLD);
    /* F10 also reconnects ordinary play with an existing local character. */
    manual_context();target();cw.player=&actual_player;probe_manual_reconnect();
    assert(loading_probe.force_reconnect&&loading_probe.state==PROBE_RECONNECT&&!sends);
    assert(network_do_io(&connection)==0&&manual_losses==1&&!sends);
    manual_context();target();cw.player=&actual_player;probe_manual_reconnect();cw.asWorld.id++;
    assert(network_do_io(&connection)==1&&!manual_losses&&!network_loading_reconnect.connection); /* stale target canceled */
}
static void test_rush_boss(void){
    manual_context();probe_preset=PROBE_BOSS;probe_arm();target();cw.asWorld.id++;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==1&&loading_probe.state==PROBE_ARMED);
    cw.asWorld.id++;cw.asWorld.props.music=&cathedral;loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==1&&loading_probe.state==PROBE_HOLD&&loading_probe.preset==PROBE_BOSS);probe_cancel();
    manual_context();probe_preset=PROBE_RUSH;probe_arm();current=NULL;fixture_gc.worldClient=NULL;
    loading_probe_send(&connection,MT_PVID_LOADED);
    assert(loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_PVID_LOADED&&!sends);
    double started=loading_probe.started;fake_time+=600;loading_probe_frame();
    loading_probe_send(&connection,MT_PVID_LOADED);assert(!sends&&loading_probe.started==started);
    loading_probe_send(&connection,MT_SERVER_DELTA_ACK);assert(sends==1);
    for(int i=0;i<3;++i){
        loading_probe_connection_lost(&connection,"hangup");assert(loading_probe.state==PROBE_WAIT);
        connection.state=CONNECTED;connection.socket++;loading_probe_send(&connection,MT_PVID_LOADED);
        assert(loading_probe.state==PROBE_HOLD&&!manual_losses&&sends==1&&loading_probe.started==started);
    }
    probe_manual_reconnect();assert(loading_probe.asset_finish&&loading_probe.state==PROBE_RESULT&&sends==2&&last_type==MT_PVID_LOADED&&!network_loading_reconnect.connection);
    current=&wc;fixture_gc.worldClient=&wc;target();cw.asWorld.id++;loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==3&&loading_probe.state==PROBE_RESULT&&!loading_probe.asset_finish&&!manual_losses);
    /* Absent asset-ready phase is an explicit, logged world-ready fallback. */
    manual_context();probe_preset=PROBE_RUSH;probe_arm();target();loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(!sends&&loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_WORLD_CREATED);
    /* Observed 24 October: Rush armed during ordinary spawned play, then a
       natural transport loss. Recovery may omit PVID_LOADED and the pending
       arm holds WORLD_CREATED, leaving no player just like a boss hold. */
    manual_context();cw.player=&actual_player;probe_preset=PROBE_RUSH;probe_arm();
    assert(loading_probe.state==PROBE_ARMED&&cw.player);
    loading_probe_connection_lost(&connection,"fixture ordinary receive failure");
    connection.state=4;assert(loading_probe.state==PROBE_ARMED&&!manual_losses&&!network_loading_reconnect.connection);
    connection.state=CONNECTED;connection.socket++;cw.asWorld.id++;target();cw.player=NULL;
    loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(!sends&&loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_WORLD_CREATED&&!cw.player);
    assert(!manual_losses&&!network_loading_reconnect.connection);
    /* A server may create a world despite our early acknowledgment hold. */
    manual_context();probe_preset=PROBE_RUSH;probe_arm();current=NULL;fixture_gc.worldClient=NULL;
    loading_probe_send(&connection,MT_PVID_LOADED);current=&wc;fixture_gc.worldClient=&wc;target();
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(!sends&&loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_WORLD_CREATED);
    probe_manual_reconnect();assert(network_do_io(&connection)==0&&manual_losses==1);
    puts("PASS: separate rush/boss presets; boss intermediate-room passthrough; early rush holds/recovery without WorldClient; manual asset-ready continuation once without reconnect; server-world observation and explicit world-ready fallbacks.");
}
static void staged_early(void){
    manual_context();set_option("plugin_loading_probe","stage_early_seconds","360");
    probe_preset=PROBE_STAGED;probe_arm();
    assert(loading_probe.state==PROBE_ARMED&&loading_probe.stage==PROBE_STAGE_EARLY&&loading_probe.early_seconds==360);
    probe_manual_reconnect();assert(loading_probe.state==PROBE_ARMED&&!network_loading_reconnect.connection); /* no premature preview/reconnect */
    current=NULL;fixture_gc.worldClient=NULL;
    loading_probe_send(&connection,MT_PVID_LOADED);
    assert(loading_probe.state==PROBE_HOLD&&!sends&&loading_probe.ready_type==MT_PVID_LOADED);
}
static void staged_world(struct STDString *zone){
    current=&wc;fixture_gc.worldClient=&wc;target();cw.asWorld.id++;
    cw.asWorld.props.zone=cw.asWorld.props.music=zone;
    loading_probe_send(&connection,MT_WORLD_CREATED);
}
static void test_staged_hold(void){
    /* Timer uses monotonic elapsed time with no WorldClient. Duplicated asset
       callbacks cannot reset it; server acks and time messages still pass. */
    staged_early();double start=loading_probe.started;
    loading_probe_send(&connection,MT_SERVER_DELTA_ACK);assert(sends==1);
    unsigned char time[16]={0};int dt=20;memcpy(time+8,&dt,4);
    loading_probe_send_proto(&connection,MT_TIME_PASSES,time);assert(proto_sends==1&&proto_time_ms==20);
    fake_time=start+359.999;loading_probe_frame();loading_probe_send(&connection,MT_PVID_LOADED);
    assert(sends==1&&loading_probe.stage==PROBE_STAGE_EARLY&&loading_probe.started==start);
    fake_time=start+360;loading_probe_frame();
    assert(sends==2&&last_type==MT_PVID_LOADED&&loading_probe.stage==PROBE_STAGE_PENDING&&loading_probe.asset_finish&&!network_loading_reconnect.connection);
    loading_probe_frame();probe_manual_reconnect();assert(sends==2); /* no second asset send */
    /* Reject an old connection even if the WorldClient briefly points at it. */
    struct ServerConnection old=connection;current=&wc;fixture_gc.worldClient=&wc;wc.serverConnection=&old;
    loading_probe_send(&old,MT_WORLD_CREATED);
    assert(sends==2&&loading_probe.asset_finish&&loading_probe.stage==PROBE_STAGE_PENDING);
    wc.serverConnection=&connection;staged_world(&cathedral);
    assert(sends==2&&loading_probe.state==PROBE_HOLD&&loading_probe.stage==PROBE_STAGE_PREVIEW&&loading_probe.ready_type==MT_WORLD_CREATED&&!loading_probe.asset_finish);
    assert(strstr(loading_probe.result,"boss room")&&!loading_probe.staged_fallback);
    fake_time+=600;loading_probe_frame();loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==2);
    /* A natural preview loss retains the preview, never restarting the early
       timer; explicit finish uses the existing verified fresh reconnect. */
    loading_probe_connection_lost(&connection,"preview hangup");connection.socket++;cw.asWorld.id++;
    loading_probe_send(&connection,MT_WORLD_CREATED);assert(sends==2&&loading_probe.state==PROBE_HOLD&&loading_probe.stage==PROBE_STAGE_PREVIEW);
    probe_manual_reconnect();assert(loading_probe.state==PROBE_RECONNECT&&sends==2);
    assert(network_do_io(&connection)==0&&manual_losses==1);
    connection.state=CONNECTED;connection.socket++;cw.asWorld.id++;
    loading_probe_send(&connection,MT_PVID_LOADED);loading_probe_send(&connection,MT_WORLD_CREATED);
    assert(sends==4&&loading_probe.state==PROBE_RESULT&&loading_probe.reloaded);
    /* Manual preview before the timer, including a lobby selected by server. */
    staged_early();fake_time+=5;probe_manual_reconnect();assert(sends==1&&loading_probe.stage==PROBE_STAGE_PENDING);
    staged_world(&lobby);assert(sends==1&&loading_probe.state==PROBE_HOLD&&strstr(loading_probe.result,"ESCHATON; mission lost"));
    /* Losses preserve total elapsed, but reset the current-connection clock.
       These observations must not be called uninterrupted six-minute holds. */
    staged_early();start=loading_probe.started;fake_time+=120;loading_probe_connection_lost(&connection,"early hangup");
    connection.state=4;connection.socket=-1;fake_time+=10;loading_probe_frame();assert(loading_probe.state==PROBE_WAIT&&!sends);
    connection.state=CONNECTED;connection.socket=11;loading_probe_send(&connection,MT_PVID_LOADED);
    assert(loading_probe.started==start&&loading_probe.early_connection_started==fake_time&&loading_probe.recoveries==1&&!sends);
    fake_time=start+360;loading_probe_frame();assert(sends==1&&loading_probe.stage==PROBE_STAGE_PENDING);
    /* A fresh socket can present readiness before the loss/render callback. */
    staged_early();start=loading_probe.started;fake_time+=25;connection.socket++;
    loading_probe_send(&connection,MT_PVID_LOADED);
    assert(!sends&&loading_probe.started==start&&loading_probe.early_connection_started==fake_time&&loading_probe.recoveries==1);
    /* Timer expiration and manual advance while offline only authorize fresh
       assets. The fresh world still cannot release player readiness. */
    for(int automatic=0;automatic<=1;++automatic){
        staged_early();start=loading_probe.started;loading_probe_connection_lost(&connection,"offline");connection.state=4;connection.socket=-1;
        fake_time=start+(automatic?360:15);if(automatic)loading_probe_frame();else probe_manual_reconnect();
        assert(!sends&&loading_probe.asset_finish&&loading_probe.stage==PROBE_STAGE_PENDING&&!network_loading_reconnect.connection);
        connection.state=CONNECTED;connection.socket=12;loading_probe_send(&connection,MT_PVID_LOADED);assert(sends==1);
        staged_world(&cathedral);assert(sends==1&&loading_probe.state==PROBE_HOLD&&loading_probe.stage==PROBE_STAGE_PREVIEW);
    }
    /* Both kinds of early-phase fallback become explicit preview holds. */
    staged_early();staged_world(&cathedral);assert(!sends&&loading_probe.state==PROBE_HOLD&&loading_probe.stage==PROBE_STAGE_PREVIEW&&loading_probe.staged_fallback==2&&strstr(probe_state_label(),"FALLBACK"));
    manual_context();probe_preset=PROBE_STAGED;probe_arm();staged_world(&cathedral);
    assert(!sends&&loading_probe.state==PROBE_HOLD&&loading_probe.stage==PROBE_STAGE_PREVIEW&&loading_probe.staged_fallback==1&&strstr(probe_state_label(),"FALLBACK"));
    /* Cancel before advance and while waiting for CreateWorld finishes the
       ordinary loading flow; neither path can capture another world-ready. */
    staged_early();probe_cancel();assert(sends==1&&loading_probe.stage==PROBE_STAGE_NONE);
    staged_world(&cathedral);assert(sends==2&&loading_probe.state==PROBE_RESULT);
    staged_early();probe_manual_reconnect();probe_cancel();probe_manual_reconnect();assert(sends==1&&loading_probe.stage==PROBE_STAGE_NONE);
    staged_world(&cathedral);assert(sends==2&&loading_probe.state==PROBE_RESULT);
    staged_early();connection.state=4;connection.socket=-1;probe_cancel();
    assert(!sends&&loading_probe.state==PROBE_RECONNECT&&loading_probe.reconnect_issued);
    connection.state=CONNECTED;connection.socket=14;staged_world(&cathedral);assert(sends==1&&loading_probe.state==PROBE_RESULT);
    staged_early();set_option("plugin_loading_probe","enabled","no");loading_probe_frame();set_option("plugin_loading_probe","enabled","yes");
    staged_world(&cathedral);assert(sends==2&&loading_probe.stage==PROBE_STAGE_NONE&&loading_probe.state==PROBE_RESULT);
    /* Configuration is bounded, and an existing player cannot be presented as
       a successful unspawned preview. */
    manual_context();probe_preset=PROBE_STAGED;set_option("plugin_loading_probe","stage_early_seconds","9000");probe_arm();assert(loading_probe.early_seconds==1800);
    set_option("plugin_loading_probe","stage_early_seconds","0");probe_arm();assert(loading_probe.early_seconds==1);
    staged_early();probe_manual_reconnect();cw.player=&actual_player;staged_world(&cathedral);
    assert(sends==2&&loading_probe.state==PROBE_RESULT&&loading_probe.stage==PROBE_STAGE_NONE&&strstr(loading_probe.result,"already present"));
    puts("PASS: staged six-minute early hold with no world, exact-once asset continuation, boss/lobby preview readiness held, fallback reporting, total/current-connection clocks, natural recovery and offline advance, cancellation/disable, stale socket rejection, bounded settings, clean final reconnect without rearm.");
}
static void fixture_header(unsigned char type,unsigned length){
    memcpy((uint8_t*)&connection+0x70,&type,1);
    assert(network_header_valid(&connection,length,network_max_message));
}
static void test_phase_trace(void){
    /* Exercise the actual receive guard integration. No payload is needed or
       read. Later deltas cannot flood the trace or change its first-stage facts. */
    manual_context();probe_preset=PROBE_STAGED;probe_arm();
    fixture_header(MT_CHANGE_SERVER,20);fixture_header(MT_LOAD_PVID,10);fixture_header(MT_CREATE_WORLD,32);
    assert(loading_probe.asset_headers==1&&loading_probe.world_headers==1&&loading_probe.phase_trace_logs==3&&!sends);
    for(int i=0;i<64;++i)fixture_header(MT_CHANGE_SERVER,20);
    assert(loading_probe.phase_trace_logs==16);
    fixture_header(MT_SERVER_DELTA_MTC,12);assert(loading_probe.phase_trace_logs==16);
    /* A failed current-client lookup must remain visible in the trace; it
       cannot be mislabeled as absence of an asset-ready callback. */
    fixture_gc.serverConnection=NULL;
    loading_probe_send(&connection,MT_PVID_LOADED);
    assert(sends==1&&loading_probe.state==PROBE_ARMED&&loading_probe.asset_empty_calls==1&&loading_probe.asset_unbound_calls==1);
    fixture_gc.serverConnection=&connection;
    loading_probe_send_proto(&connection,MT_PVID_LOADED,NULL);
    assert(proto_sends==1&&loading_probe.asset_proto_calls==1&&loading_probe.asset_unbound_calls==1);
    staged_world(&cathedral);
    assert(sends==1&&loading_probe.staged_fallback==1&&loading_probe.state==PROBE_HOLD);
    unsigned headers=loading_probe.world_headers;fixture_header(MT_CREATE_WORLD,32);assert(loading_probe.world_headers==headers);
    loading_probe_connection_lost(&connection,"fixture fallback loss");assert(strstr(probe_state_label(),"FALLBACK"));
    /* A genuinely intercepted early acknowledgment has no fallback warning. */
    staged_early();assert(loading_probe.asset_empty_calls==1&&!loading_probe.asset_unbound_calls);
    probe_manual_reconnect();staged_world(&cathedral);
    assert(!loading_probe.staged_fallback&&!strstr(probe_state_label(),"FALLBACK"));
    manual_context();fixture_header(MT_LOAD_PVID,10);assert(!loading_probe.asset_headers&&!loading_probe.phase_trace_logs);
    puts("PASS: bounded loading-phase metadata through native header guard; empty/protobuf readiness attempts and missing-client binding distinguished; passive trace leaves sends unchanged; visible fallback persists across recovery and is absent for real early holds.");
}
static void test_free_camera(void){
    manual_hold(&cathedral);
    struct WorldView view={0};wc.worldView=&view;view.world=&cw;
    view.asUIElement.w=800;view.asUIElement.h=600;
    view.worldBounds=(struct Rectangle){-200,0,4000,3000};view.playerBounds=(struct Rectangle){0,0,2000,2000};
    view.offset=(struct Point){100,200};view.targetPos=(struct Point){500,500};
    struct WorldView original=view;struct ClientWorld unchanged=cw;struct Stage stage={0};
    probe_camera_toggle();assert(probe_camera.active);uint8_t keys[512]={0};keys[7]=keys[22]=1;
    probe_camera_keys(keys,512,1);fake_time+=.1;probe_camera_begin(&view);
    assert(probe_camera.applied&&view.offset.x>100&&view.offset.y>200);
    assert(view.targetPos.x==view.offset.x+400&&view.targetPos.y==view.offset.y+300);
    assert(!memcmp(&unchanged,&cw,sizeof(cw))&&sends==1&&!manual_losses);
    probe_camera_end();assert(!memcmp(&view,&original,sizeof(view)));
    /* No motion while chat owns the keyboard, even if movement keys remain held. */
    double x=probe_camera.x,y=probe_camera.y;probe_camera_keys(keys,512,0);fake_time+=5;probe_camera_begin(&view);
    assert(probe_camera.x==x&&probe_camera.y==y);probe_camera_end();
    probe_camera.x=1e9;probe_camera.y=-1e9;probe_camera_begin(&view);
    assert(view.offset.x==3000&&view.offset.y==0);probe_camera_end();
    /* Captured releases are consumed even after disabling; a key already held
       before camera activation keeps its ordinary release. */
    assert(probe_camera_consumes(&stage,1,100,0));probe_camera_toggle();
    assert(probe_camera_consumes(&stage,0,100,1)&&!probe_camera.captured);
    probe_camera_toggle();assert(!probe_camera_consumes(&stage,1,97,1)&&!probe_camera_consumes(&stage,0,97,1));
    stage.textInputTarget=(void*)1;assert(!probe_camera_consumes(&stage,1,119,0));stage.textInputTarget=NULL;
    cw.player=&actual_player;loading_probe_frame();assert(!probe_camera.active&&!probe_camera_available(&view));
    assert(!memcmp(&view,&original,sizeof(view)));cw.player=NULL;
    manual_hold(&cathedral);wc.worldView=&view;probe_camera_toggle();assert(probe_camera.active);
    probe_camera.x=700;probe_camera.y=500;
    cw.asWorld.id++;probe_camera_begin(&view);assert(probe_camera.active&&!probe_camera.applied);
    assert(!memcmp(&view,&original,sizeof(view)));
    /* Boss-hold recovery retains selection and coordinates, but never the
       old view pointer. A different room starts at its native camera offset. */
    loading_probe_connection_lost(&connection,"fixture freecam loss");connection.state=4;wc.worldView=NULL;
    loading_probe_frame();assert(probe_camera.active&&probe_camera.view==NULL);
    current=NULL;probe_camera_begin(NULL);assert(probe_camera.active&&!probe_camera.applied);fake_time+=25;
    struct WorldView recovered=original;recovered.offset=(struct Point){20,30};struct WorldView recovered_original=recovered;
    current=&wc;wc.worldView=&recovered;connection.state=CONNECTED;connection.socket++;
    loading_probe_send(&connection,MT_WORLD_CREATED);probe_camera_keys(keys,512,0);loading_probe_frame();
    assert(probe_camera.active&&probe_camera.view==&recovered);
    probe_camera_begin(&recovered);assert(recovered.offset.x==700&&recovered.offset.y==500);
    probe_camera_end();assert(!memcmp(&recovered,&recovered_original,sizeof(recovered))&&!memcmp(&view,&original,sizeof(view)));
    cw.asWorld.id++;cw.asWorld.props.seed++;loading_probe_send(&connection,MT_WORLD_CREATED);
    probe_camera_begin(&recovered);assert(recovered.offset.x==20&&recovered.offset.y==30);probe_camera_end();
    loading_probe_connection_lost(&connection,"fixture toggle while offline");connection.state=4;wc.worldView=NULL;loading_probe_frame();
    assert(probe_camera.active);probe_camera_toggle();assert(!probe_camera.active);
    connection.state=CONNECTED;connection.socket++;cw.asWorld.id++;wc.worldView=&recovered;loading_probe_send(&connection,MT_WORLD_CREATED);
    probe_camera_begin(&recovered);assert(!probe_camera.active&&!probe_camera.applied);
    probe_camera_toggle();assert(probe_camera.active);probe_manual_reconnect();loading_probe_frame();assert(!probe_camera.active);
    manual_context();probe_preset=PROBE_RUSH;probe_arm();current=NULL;fixture_gc.worldClient=NULL;
    loading_probe_send(&connection,MT_PVID_LOADED);probe_camera_toggle();assert(!probe_camera.active);
    current=&wc;fixture_gc.worldClient=&wc;
    puts("PASS: free camera during map holds; safe suspension across loss and same-room position recovery, fresh-room recentering, offline toggle-off; WASD/arrows, chat ownership, paired releases and bounds; draw-only fields restored, no player/clock/network mutation; cleared on finish/spawn, unavailable before map creation.");
}
static void click_passthrough(int x,int y,int b,int down,void *u){(void)x;(void)y;(void)b;(void)down;(void)u;++forwarded_clicks;}
static void gui_window(void *unused,int *w,int *h){(void)unused;*w=400;*h=300;}
static void test_manual_gui_input(void){
    manual_context();physical_w=800;physical_h=600;int canvas_x=2000,canvas_y=1500;canvas_w=&canvas_x;canvas_h=&canvas_y;
    struct ProbeRect panel,buttons[PROBE_BUTTONS],tab;assert(probe_gui_rects(&panel,buttons,&tab));
    void *window=(void*)1;window_ptr=&window;get_window_size=gui_window;original_button=click_passthrough;
    probe_gui_visible=1;probe_gui_captured=0;forwarded_clicks=0;
    for(int action=0;action<=3;action+=3){
        int x=(buttons[action].x+buttons[action].w/2)/2,y=(buttons[action].y+buttons[action].h/2)/2;
        mouse_button(x,y,0,1,NULL);mouse_button(x,y,0,0,NULL);
        assert(!forwarded_clicks);assert(loading_probe.state==(action==0?PROBE_ARMED:PROBE_IDLE));
    }
    /* Staged GUI arms, waits for a real entry, then advances without spawning. */
    int sx=(buttons[6].x+buttons[6].w/2)/2,sy=(buttons[6].y+buttons[6].h/2)/2;
    mouse_button(sx,sy,0,1,NULL);mouse_button(sx,sy,0,0,NULL);assert(loading_probe.preset==PROBE_STAGED&&loading_probe.state==PROBE_ARMED);
    sx=(buttons[7].x+buttons[7].w/2)/2;sy=(buttons[7].y+buttons[7].h/2)/2;
    mouse_button(sx,sy,0,1,NULL);mouse_button(sx,sy,0,0,NULL);assert(loading_probe.state==PROBE_ARMED&&!network_loading_reconnect.connection);
    loading_probe_send(&connection,MT_PVID_LOADED);
    mouse_button(sx,sy,0,1,NULL);mouse_button(sx,sy,0,0,NULL);assert(loading_probe.stage==PROBE_STAGE_PENDING&&sends==1);
    sx=(buttons[0].x+buttons[0].w/2)/2;sy=(buttons[0].y+buttons[0].h/2)/2;
    mouse_button(sx,sy,0,1,NULL);mouse_button(sx,sy,0,0,NULL);assert(loading_probe.preset==PROBE_STAGED&&loading_probe.stage==PROBE_STAGE_PENDING);
    uint8_t pending_keys[128]={0};probe_f9=0;pending_keys[66]=1;loading_probe_keys(pending_keys,128,1);assert(loading_probe.stage==PROBE_STAGE_PENDING);
    probe_cancel();staged_world(&cathedral);assert(sends==2);manual_context();probe_preset=PROBE_RUSH;
    int x=(buttons[5].x+buttons[5].w/2)/2,y=(buttons[5].y+buttons[5].h/2)/2;
    mouse_button(x,y,0,1,NULL);mouse_button(x,y,0,0,NULL);assert(!probe_gui_visible);
    x=(tab.x+tab.w/2)/2;y=(tab.y+tab.h/2)/2;mouse_button(x,y,0,1,NULL);mouse_button(x,y,0,0,NULL);assert(probe_gui_visible);
    /* Mouse-up after an outside gameplay press must always reach the game. */
    mouse_button(5,5,0,1,NULL);mouse_button(x,y,0,0,NULL);assert(forwarded_clicks==2);
    /* A panel press released outside is consumed but performs no action. */
    x=(buttons[0].x+buttons[0].w/2)/2;y=(buttons[0].y+buttons[0].h/2)/2;
    mouse_button(x,y,0,1,NULL);mouse_button(5,5,0,0,NULL);assert(forwarded_clicks==2&&loading_probe.state==PROBE_IDLE);
    struct Controls controls={0};active_controls=&controls;mouse_button(x,y,0,1,NULL);mouse_button(x,y,0,0,NULL);assert(forwarded_clicks==4);active_controls=NULL;
    uint8_t keys[128]={0};probe_f8=probe_f9=probe_f10=0;keys[65]=1;
    loading_probe_keys(keys,128,0);loading_probe_keys(keys,128,1);assert(probe_gui_visible); /* ownership/held-key guard */
    keys[65]=0;loading_probe_keys(keys,128,1);keys[65]=1;loading_probe_keys(keys,128,1);assert(!probe_gui_visible);
    loading_probe_keys(keys,128,1);assert(!probe_gui_visible);keys[65]=0;loading_probe_keys(keys,128,1);
    keys[66]=1;loading_probe_keys(keys,128,1);assert(loading_probe.state==PROBE_ARMED);keys[66]=0;loading_probe_keys(keys,128,1);
    cw.asWorld.id++;cw.player=NULL;loading_probe_send(&connection,MT_WORLD_CREATED);assert(loading_probe.state==PROBE_HOLD);
    keys[66]=1;loading_probe_keys(keys,128,1);assert(loading_probe.state==PROBE_HOLD&&!manual_losses); /* F9 never ends manual hold */
    keys[67]=1;loading_probe_keys(keys,128,1);assert(loading_probe.state==PROBE_RECONNECT&&!manual_losses);
    assert(network_do_io(&connection)==0&&manual_losses==1);
    struct Stage stage={0};assert(loading_probe_consumes(&stage,0x40000041)&&loading_probe_consumes(&stage,0x40000043));
    stage.textInputTarget=(void*)1;assert(!loading_probe_consumes(&stage,0x40000041));
    window_ptr=NULL;get_window_size=NULL;canvas_w=canvas_h=NULL;physical_w=physical_h=0;
}
static void test_persistent_log(const char *artifact_dir){
    char directory[]="/tmp/sbpe-any-map-log-XXXXXX";assert(mkdtemp(directory));assert(!setenv("SBPE_ROOT",directory,1));
    manual_context();probe_arm();assert(probe_record&&sbpe_log_mirror&&probe_record_path[0]);
    char first[4096];snprintf(first,sizeof(first),"%s",probe_record_path);
    FILE *regular_log=tmpfile();assert(regular_log);FILE *old_log=logfile;logfile=regular_log;
    LOG("Loading test: mirror args %u %s; quote \" and newline\n persisted immediately.",42U,"same");
    rewind(regular_log);char regular[1024]={0};assert(fread(regular,1,sizeof(regular)-1,regular_log)>0&&strstr(regular,"mirror args 42 same"));logfile=old_log;fclose(regular_log);
    FILE *f=fopen(first,"r");assert(f);char bytes[8192]={0};assert(fread(bytes,1,sizeof(bytes)-1,f)>0);fclose(f);
    assert(strstr(bytes,"test_begin")&&strstr(bytes,"ANY next map")&&strstr(bytes,"mirror args 42 same")&&strstr(bytes,"\\\"")&&strstr(bytes,"\\u000a")); /* readable before close */
    LOG("Retina: ignored diagnostic");probe_cancel();probe_arm();assert(strcmp(first,probe_record_path));
    struct stat st;assert(!stat(first,&st)&&(st.st_mode&0777)==0600);assert(!strstr(bytes,"Retina:"));
    char second[4096];snprintf(second,sizeof(second),"%s",probe_record_path);probe_record_close();
    staged_early();char staged[4096];snprintf(staged,sizeof(staged),"%s",probe_record_path);
    fake_time+=360;loading_probe_frame();staged_world(&cathedral);
    f=fopen(staged,"r");assert(f);char staged_bytes[16384]={0};assert(fread(staged_bytes,1,sizeof(staged_bytes)-1,f)>0);fclose(f);
    assert(strstr(staged_bytes,"total early hold 360.000s, current connection 360.000s, recoveries=0")&&strstr(staged_bytes,"boss=YES lobby=NO")&&strstr(staged_bytes,"Player readiness remains blocked"));
    probe_record_close();
    manual_context();probe_preset=PROBE_STAGED;probe_arm();char fallback[4096];snprintf(fallback,sizeof(fallback),"%s",probe_record_path);
    fixture_header(MT_CREATE_WORLD,32);staged_world(&cathedral);
    f=fopen(fallback,"r");assert(f);char fallback_bytes[16384]={0};assert(fread(fallback_bytes,1,sizeof(fallback_bytes)-1,f)>0);fclose(f);
    assert(strstr(fallback_bytes,"phase trace received header type=10 length=32")&&strstr(fallback_bytes,"six-minute timer did not run")&&strstr(fallback_bytes,"Valid headers asset/world=0/1; asset-ready attempts empty/protobuf=0/0, unbound=0"));
    assert(!strstr(fallback_bytes,"total early hold 360.000s"));probe_record_close();
    if(artifact_dir){char copy[4096];snprintf(copy,sizeof(copy),"%s/autosave-fixture.jsonl",artifact_dir);FILE *in=fopen(first,"r"),*out=fopen(copy,"w");assert(in&&out);int ch;while((ch=fgetc(in))!=EOF)fputc(ch,out);fclose(in);fclose(out);}
    if(artifact_dir){char copy[4096];snprintf(copy,sizeof(copy),"%s/staged-autosave-fixture.jsonl",artifact_dir);FILE *in=fopen(staged,"r"),*out=fopen(copy,"w");assert(in&&out);int ch;while((ch=fgetc(in))!=EOF)fputc(ch,out);fclose(in);fclose(out);}
    if(artifact_dir){char copy[4096];snprintf(copy,sizeof(copy),"%s/fallback-autosave-fixture.jsonl",artifact_dir);FILE *in=fopen(fallback,"r"),*out=fopen(copy,"w");assert(in&&out);int ch;while((ch=fgetc(in))!=EOF)fputc(ch,out);fclose(in);fclose(out);}
    unlink(first);unlink(second);unlink(staged);unlink(fallback);char sub[4096];snprintf(sub,sizeof(sub),"%s/loading-tests",directory);rmdir(sub);rmdir(directory);unsetenv("SBPE_ROOT");
    assert(!probe_record&&!sbpe_log_mirror);
    setenv("SBPE_ROOT","/this/path/does/not/exist",1);manual_context();probe_arm();assert(loading_probe.state==PROBE_IDLE&&probe_record_failed);unsetenv("SBPE_ROOT");
}
static FILE *preview;static unsigned gui_next_texture;static struct {char value[256];int size,w,h;uint32_t color;} glyphs[MAX_TEXT+1];
static unsigned gui_make_text(unsigned old,const char *font,int size,uint32_t color,const char *text,int outline,uint32_t oc){
    (void)font;(void)oc;unsigned id=old?old:++gui_next_texture;assert(id<=MAX_TEXT);snprintf(glyphs[id].value,256,"%s",text);glyphs[id].size=size;
    glyphs[id].w=(int)strlen(text)*size/2+outline*2;glyphs[id].h=size+outline*2;glyphs[id].color=color;return id;
}
static void gui_query(unsigned id,int *w,int *h){*w=glyphs[id].w;*h=glyphs[id].h;}
static void svg_string(const char *s){for(;*s;++s){if(*s=='&')fputs("&amp;",preview);else if(*s=='<')fputs("&lt;",preview);else if(*s=='>')fputs("&gt;",preview);else fputc(*s,preview);}}
static void gui_texture(unsigned id,int a,int b,int c,int d,int x,int y,int w,int h,int i,int j,int k,int l,uint32_t tint,int blend){
    (void)a;(void)b;(void)c;(void)d;(void)i;(void)j;(void)k;(void)l;(void)w;(void)tint;assert(blend==BLENDMODE_BLEND);++gui_texts;
    if(preview){fprintf(preview,"<text x='%d' y='%d' font-family='GameFont,monospace' font-size='%d' fill='#%06x'>",x,y+h-3,glyphs[id].size,glyphs[id].color&0xffffff);svg_string(glyphs[id].value);fputs("</text>\n",preview);}
}
static void gui_rectangle(int x,int y,int w,int h,uint32_t color,int blend){
    assert(blend==BLENDMODE_BLEND&&x>=0&&y>=0&&x+w<=physical_w&&y+h<=physical_h);++gui_rectangles;
    if(preview)fprintf(preview,"<rect x='%d' y='%d' width='%d' height='%d' fill='#%06x' fill-opacity='%.3f'/>\n",x,y,w,h,color&0xffffff,(color>>24)/255.0);
}
static void gui_snapshot(const char *artifact_dir,const char *name){
    if(artifact_dir){char path[4096];snprintf(path,sizeof(path),"%s/%s.svg",artifact_dir,name);preview=fopen(path,"w");assert(preview);fputs("<svg xmlns='http://www.w3.org/2000/svg' width='800' height='600'><rect width='800' height='600' fill='#09233a'/>\n",preview);}
    loading_probe_overlay();
    if(preview){fputs("</svg>\n",preview);fclose(preview);preview=NULL;}
}
static void test_gui_draw(const char *artifact_dir){
    manual_context();probe_gui_visible=1;probe_record_failed=0;int w=2400,h=1800;canvas_w=&w;canvas_h=&h;physical_w=800;physical_h=600;render_pixel_ratio=1;
    create_text=gui_make_text;query_texture=gui_query;draw_texture=gui_texture;fill_rect=gui_rectangle;
    gui_snapshot(artifact_dir,"mac-loading-panel");assert(gui_rectangles==3+PROBE_BUTTONS&&gui_texts>=10&&w==2400&&h==1800);
    staged_early();fake_time+=90;text_slot=0;gui_snapshot(artifact_dir,"staged-early-panel");
    probe_manual_reconnect();text_slot=0;gui_snapshot(artifact_dir,"staged-pending-panel");
    staged_world(&cathedral);text_slot=0;gui_snapshot(artifact_dir,"staged-preview-panel");
    manual_context();probe_preset=PROBE_STAGED;probe_arm();staged_world(&cathedral);text_slot=0;gui_snapshot(artifact_dir,"staged-fallback-panel");
    int fallback_warning=0;for(unsigned id=1;id<=gui_next_texture;++id)if(strstr(glyphs[id].value,"Fallback: early wait unavailable"))fallback_warning=1;assert(fallback_warning);
    gui_rectangles=gui_texts=0;probe_gui_visible=0;text_slot=0;loading_probe_overlay();assert(gui_rectangles==1&&gui_texts==1&&w==2400&&h==1800);
    for(int ratio=1;ratio<=2;++ratio)for(int width=400;width<=1600;width+=200){render_pixel_ratio=ratio;physical_w=width;physical_h=480;probe_gui_visible=1;text_slot=0;loading_probe_overlay();assert(w==2400&&h==1800);}
    render_pixel_ratio=1;
    physical_w=physical_h=0;canvas_w=canvas_h=NULL;
}
#ifndef SBPE_ANY_MAP_MAIN
#define SBPE_ANY_MAP_MAIN main
#endif
int SBPE_ANY_MAP_MAIN(int argc,char **argv){
    normal_loading_fixture_main();const char *artifacts=argc>1?argv[1]:NULL;if(artifacts)assert(!mkdir(artifacts,0700)||errno==EEXIST);
    test_manual_maps();test_rush_boss();test_staged_hold();test_phase_trace();test_free_camera();test_manual_gui_input();test_persistent_log(artifacts);test_gui_draw(artifacts);
    set_option("plugin_loading_probe","mode","normal");probe_record_close();
    puts("PASS: manual any-map holds with no arm/hold deadline, existing-room readiness passthrough, unchanged time/input/acks, deferred exact-once F10 during hold/ordinary play; repeated natural losses retain hold across fresh worlds, early world replacement, explicit finish/cancel during recovery, stale-target cancellation; F8 and clickable close/open, mouse scaling/capture and Controls ownership; draw callbacks restore canvas; immediate persistent JSONL.");return 0;
}
