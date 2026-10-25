#define SBPE_ANY_MAP_MAIN inherited_loading_main
#include "test_any_map_loading.c"
#undef SBPE_ANY_MAP_MAIN
int main(int argc,char **argv){
    inherited_loading_main(argc,argv);
    manual_context();loading_probe_installed=1;probe_preset=PROBE_RUSH;probe_arm();
    loading_probe_send(&connection,MT_PVID_LOADED);assert(loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_PVID_LOADED);
    struct Properties terrain={.terraintype=1,.draw=1,.boundsworld=1,.wmp=256,.hmp=256};
    struct WorldState_World_Obj obj={.objid=7,.props=&terrain};void *objects[]={&obj};
    struct WorldState_World server={.obj={objects,1,1,1}};
    struct WorldState state={.serverworld=&server};struct CreateWorld bad={.worldstate=&state,.worldid=24};
    network_reconnect_pending=NULL;struct ClientWorld *old=fixture_gc.worldClient->clientWorld;
    unsigned before=sends;assert(!network_world_validate(&fixture_gc,&bad,1));
    assert(network_reconnect_pending==&connection&&fixture_gc.worldClient->clientWorld==old);
    assert(loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_PVID_LOADED&&sends==before);
    /* This is the existing native loss callback reached after dispatch has
       unwound, not a reconnect inside the parsed-message handler. */
    network_loss_observe(&connection,0);connection.state=ERRORED;connection.socket=-1;
    assert(loading_probe.state==PROBE_WAIT&&loading_probe.manual&&!loading_probe.reconnect_issued&&sends==before);
    connection.state=CONNECTED;connection.socket=10;cw.asWorld.id++;loading_probe_send(&connection,MT_PVID_LOADED);
    assert(loading_probe.state==PROBE_HOLD&&loading_probe.ready_type==MT_PVID_LOADED&&sends==before);
    probe_record_close();puts("PASS: missing-spawn preflight defers recovery, preserves old world and active Rush readiness hold, then retains manual hold on a fresh connection.");return 0;
}
