#include "support/netw_test.h"

#include "godot/utility.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSceneResultFrameLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::NetwPromise;
using netw::NetwSceneCore;

Ref<NetwMultiplayerCore> asking_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->get_clock_handle()->engine.set_configured(true);
    return core;
}

PackedByteArray result_frame(int p_request_id, int p_code) {
    Array row;
    row.push_back(p_request_id);
    row.push_back(p_code);
    return netw::gd::var_to_bytes(row);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SF1 the server's answer settles the request it "
    "names and is counted against nobody, because an ordinary answer read as "
    "an attack would bury the real ones under the traffic of a working session"
) {
    const Ref<NetwMultiplayerCore> core = asking_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> asked
        = core->scene_request(false, StringName("Arena"), Array(), false);
    const int request_id = scenes->get_pending_request_id();

    CHECK(core->scene_receive_result_frame(
        result_frame(request_id, ERR_UNAUTHORIZED),
        1
    ));

    NETW_CHECK_EQ(asked->get_code(), ERR_UNAUTHORIZED);
    NETW_CHECK_EQ(int(core->verdict_total(ERR_UNAUTHORIZED)), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SF2 an answer from a peer that is not the "
    "server settles nothing and is counted, so a peer forging outcomes for "
    "other peers' requests is a number the session reports rather than a log "
    "line nobody reads"
) {
    const Ref<NetwMultiplayerCore> core = asking_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> asked
        = core->scene_request(false, StringName("Arena"), Array(), false);
    const int request_id = scenes->get_pending_request_id();

    CHECK_FALSE(core->scene_receive_result_frame(
        result_frame(request_id, OK),
        7
    ));

    CHECK_FALSE(asked->get_is_settled());
    CHECK(scenes->is_current(request_id));
    NETW_CHECK_EQ(int(core->verdict_total(ERR_UNAUTHORIZED)), 1);

    CHECK_FALSE(core->scene_receive_result_frame(
        result_frame(request_id, OK),
        7
    ));

    NETW_CHECK_EQ(int(core->verdict_total(ERR_UNAUTHORIZED)), 2);
    CHECK_FALSE(asked->get_is_settled());
}

TEST_CASE(
    "[Networked][Scene][Hosted] SF3 an answer naming a request that is no "
    "longer in flight settles nothing, so a slow answer to a superseded ask "
    "cannot land on the request that replaced it"
) {
    const Ref<NetwMultiplayerCore> core = asking_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    core->scene_request(false, StringName("Arena"), Array(), false);
    const int stale_id = scenes->get_pending_request_id();
    const Ref<NetwPromise> live
        = core->scene_request(false, StringName("Annex"), Array(), false);
    const int live_id = scenes->get_pending_request_id();

    CHECK(stale_id != live_id);
    CHECK_FALSE(core->scene_receive_result_frame(
        result_frame(stale_id, ERR_UNAUTHORIZED),
        1
    ));

    CHECK_FALSE(live->get_is_settled());
    NETW_CHECK_EQ(int(core->verdict_total(ERR_UNAUTHORIZED)), 0);

    CHECK(core->scene_receive_result_frame(result_frame(live_id, OK), 1));

    NETW_CHECK_EQ(live->get_code(), OK);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SF4 a payload that is not a two-field row is "
    "refused rather than read positionally, because a row read short would "
    "settle whatever request id its first field happened to decode as"
) {
    const Ref<NetwMultiplayerCore> core = asking_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> asked
        = core->scene_request(false, StringName("Arena"), Array(), false);
    const int request_id = scenes->get_pending_request_id();

    Array short_row;
    short_row.push_back(request_id);

    CHECK_FALSE(core->scene_receive_result_frame(
        netw::gd::var_to_bytes(short_row),
        1
    ));
    CHECK_FALSE(core->scene_receive_result_frame(
        netw::gd::var_to_bytes(Variant(request_id)),
        1
    ));
    Array long_row;
    long_row.push_back(request_id);
    long_row.push_back(int(OK));
    long_row.push_back(int(OK));

    CHECK_FALSE(core->scene_receive_result_frame(
        netw::gd::var_to_bytes(long_row),
        1
    ));

    CHECK_FALSE(asked->get_is_settled());
    NETW_CHECK_EQ(int(core->verdict_total(ERR_UNAUTHORIZED)), 0);
}

} // namespace TestNetwSceneResultFrameLaws
