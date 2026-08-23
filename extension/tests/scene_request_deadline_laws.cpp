#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneRequestDeadlineLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::NetwPromise;
using netw::NetwSceneCore;
using netw_test::CallLog;

Ref<NetwMultiplayerCore> armable_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->get_clock_handle()->engine.set_configured(true);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] RD1 opening a request arms its deadline "
    "against the id that same open stamped, so the wait names the request it "
    "was opened for rather than whichever one is pending when it elapses"
) {
    const Ref<NetwMultiplayerCore> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();
    const CallLog arm;
    core->set_request_deadline_arm(arm.callable("arm"));

    const Ref<NetwPromise> promise = core->scene_request_open(
        false,
        StringName("Arena"),
        Array(),
        false,
        2.5
    );

    CHECK(promise.is_valid());
    CHECK_FALSE(promise->get_is_settled());
    NETW_CHECK_EQ(arm.count("arm"), 1);
    const Array armed = arm.args("arm");
    REQUIRE(armed.size() == 2);
    NETW_CHECK_EQ(int(armed[0]), scenes->get_pending_request_id());
    NETW_CHECK_CLOSE(double(armed[1]), 2.5, 1e-9);
}

TEST_CASE(
    "[Networked][Scene][Hosted] RD2 a second open arms the second id, so the "
    "first request's own wait can only ever expire the request it named and "
    "never the one that superseded it"
) {
    const Ref<NetwMultiplayerCore> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();
    const CallLog arm;
    core->set_request_deadline_arm(arm.callable("arm"));

    const Ref<NetwPromise> stale = core->scene_request_open(
        false,
        StringName("Arena"),
        Array(),
        false,
        2.5
    );
    const int stale_id = int(arm.args("arm", 0)[0]);
    const Ref<NetwPromise> live = core->scene_request_open(
        false,
        StringName("Annex"),
        Array(),
        false,
        2.5
    );
    const int live_id = int(arm.args("arm", 1)[0]);

    NETW_CHECK_EQ(arm.count("arm"), 2);
    CHECK(stale_id != live_id);
    NETW_CHECK_EQ(live_id, scenes->get_pending_request_id());

    core->scene_request_expire(stale_id);

    NETW_CHECK_EQ(stale->get_code(), ERR_SKIP);
    CHECK_FALSE(live->get_is_settled());

    core->scene_request_expire(live_id);

    NETW_CHECK_EQ(live->get_code(), ERR_TIMEOUT);
}

TEST_CASE(
    "[Networked][Scene][Hosted] RD3 a deadline of zero or less arms nothing "
    "and still opens the request, because waiting forever for authority is a "
    "caller's choice rather than a request that failed to open"
) {
    const Ref<NetwMultiplayerCore> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();
    const CallLog arm;
    core->set_request_deadline_arm(arm.callable("arm"));

    const Ref<NetwPromise> none = core->scene_request_open(
        false,
        StringName("Arena"),
        Array(),
        false,
        0.0
    );

    CHECK(none.is_valid());
    CHECK_FALSE(none->get_is_settled());
    CHECK(scenes->is_current(scenes->get_pending_request_id()));
    NETW_CHECK_EQ(arm.count("arm"), 0);

    const Ref<NetwPromise> negative = core->scene_request_open(
        false,
        StringName("Annex"),
        Array(),
        false,
        -1.0
    );

    CHECK_FALSE(negative->get_is_settled());
    NETW_CHECK_EQ(arm.count("arm"), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] RD4 an open with no arm installed sends and "
    "answers a live promise, because a session driven with no scene tree "
    "behind it is the ordinary case rather than a defect"
) {
    const Ref<NetwMultiplayerCore> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> promise = core->scene_request_open(
        false,
        StringName("Arena"),
        Array(),
        false,
        2.5
    );

    CHECK(promise.is_valid());
    CHECK_FALSE(promise->get_is_settled());
    CHECK(bool(scenes->get_pending_request() == promise));
    CHECK(scenes->is_current(scenes->get_pending_request_id()));
}

TEST_CASE(
    "[Networked][Scene][Hosted] RD5 an open with a deadline sends the same "
    "request a bare ask sends, so arming a wait changes when an answer stops "
    "being expected and never what was asked"
) {
    const Ref<NetwMultiplayerCore> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();
    const CallLog arm;
    core->set_request_deadline_arm(arm.callable("arm"));

    const Ref<NetwPromise> bare
        = core->scene_request(true, String("res://annex.tscn"), Array(), false);
    const int bare_id = scenes->get_pending_request_id();

    NETW_CHECK_EQ(arm.count("arm"), 0);
    CHECK(bool(scenes->get_pending_request() == bare));

    const Ref<NetwPromise> armed = core->scene_request_open(
        true,
        String("res://annex.tscn"),
        Array(),
        false,
        2.5
    );

    NETW_CHECK_EQ(bare->get_code(), ERR_SKIP);
    CHECK(bool(scenes->get_pending_request() == armed));
    CHECK(scenes->get_pending_request_id() != bare_id);
    NETW_CHECK_EQ(arm.count("arm"), 1);
}

} // namespace TestNetwSceneRequestDeadlineLaws
