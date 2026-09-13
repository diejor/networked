#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSceneRequestDeadlineLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwSceneCore;

Ref<NetwMultiplayer> armable_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->clock_engine().set_configured(true);
    core->session_get_inner()->set_multiplayer_peer(Ref<MultiplayerPeer>());
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] RD1 opening a request arms its "
    "deadline against the id that same open stamped, so the wait names the "
    "request it was opened for rather than whichever one is pending when it "
    "elapses"
) {
    const Ref<NetwMultiplayer> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> promise = core->scene_request_open(
        String("res://arena.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        2.5
    );

    CHECK(promise.is_valid());
    CHECK_FALSE(promise->get_is_settled());
    NETW_CHECK_EQ(
        core->scene_request_armed_id(),
        scenes->get_pending_request_id()
    );
    NETW_CHECK_CLOSE(core->scene_request_armed_deadline(), 2.5, 1e-9);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] RD2 a second open arms the second "
    "id, so the first request's own wait can only ever expire the request it "
    "named and never the one that superseded it"
) {
    const Ref<NetwMultiplayer> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> stale = core->scene_request_open(
        String("res://arena.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        2.5
    );
    const int stale_id = core->scene_request_armed_id();
    const Ref<NetwPromise> live = core->scene_request_open(
        String("res://annex.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        2.5
    );
    const int live_id = core->scene_request_armed_id();

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
    const Ref<NetwMultiplayer> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> none = core->scene_request_open(
        String("res://arena.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        0.0
    );

    CHECK(none.is_valid());
    CHECK_FALSE(none->get_is_settled());
    CHECK(scenes->is_current(scenes->get_pending_request_id()));
    NETW_CHECK_EQ(core->scene_request_armed_id(), -1);

    const Ref<NetwPromise> negative = core->scene_request_open(
        String("res://annex.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        -1.0
    );

    CHECK_FALSE(negative->get_is_settled());
    NETW_CHECK_EQ(core->scene_request_armed_id(), -1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] RD4 an open answers a live promise whatever "
    "the wait behind it did, because arming a deadline decides when an answer "
    "stops being expected and never whether the request opened"
) {
    const Ref<NetwMultiplayer> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> promise = core->scene_request_open(
        String("res://arena.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        2.5
    );

    CHECK(promise.is_valid());
    CHECK_FALSE(promise->get_is_settled());
    CHECK(bool(scenes->get_pending_request() == promise));
    CHECK(scenes->is_current(scenes->get_pending_request_id()));
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] RD5 an open with a deadline sends "
    "the same request a bare ask sends, so arming a wait changes when an "
    "answer stops being expected and never what was asked"
) {
    const Ref<NetwMultiplayer> core = armable_core();
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    const Ref<NetwPromise> bare = core->scene_request_send(
        String("res://annex.tscn"),
        NetwSceneCore::SCOPE_SESSION
    );
    const int bare_id = scenes->get_pending_request_id();

    NETW_CHECK_EQ(core->scene_request_armed_id(), -1);
    CHECK(bool(scenes->get_pending_request() == bare));

    const Ref<NetwPromise> armed = core->scene_request_open(
        String("res://annex.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        2.5
    );

    NETW_CHECK_EQ(bare->get_code(), ERR_SKIP);
    CHECK(bool(scenes->get_pending_request() == armed));
    CHECK(scenes->get_pending_request_id() != bare_id);
    NETW_CHECK_EQ(
        core->scene_request_armed_id(),
        scenes->get_pending_request_id()
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] RD6 a session that is its own server answers "
    "its own scene request through the same door a remote one arrives by, so "
    "a request with no admitted participant is refused rather than left open"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->clock_engine().set_configured(true);
    CHECK(core->session_get_inner()->get_multiplayer_peer().is_valid());
    NETW_CHECK_EQ(int64_t(core->get_unique_id()), int64_t(1));

    const Ref<NetwPromise> promise = core->scene_request_send(
        String("res://arena.tscn"),
        NetwSceneCore::SCOPE_SESSION
    );

    CHECK(promise.is_valid());
    CHECK(promise->get_is_settled());
    NETW_CHECK_EQ(promise->get_code(), int64_t(ERR_UNAUTHORIZED));
}

} // namespace TestNetwSceneRequestDeadlineLaws
