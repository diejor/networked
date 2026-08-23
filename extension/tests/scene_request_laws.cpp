#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSceneRequest {

using namespace godot;
using netw::NetwMultiplayerCore;

Ref<NetwMultiplayerCore> configured_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->get_clock_handle()->engine.set_configured(true);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR1 a scene request is open, current and "
    "unsettled from the moment it is asked for"
) {
    const Ref<NetwMultiplayerCore> core = configured_core();
    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();

    const Ref<netw::NetwPromise> promise
        = core->scene_request(false, StringName("Arena"), Array(), false);

    CHECK(promise.is_valid());
    CHECK_FALSE(promise->get_is_settled());
    CHECK(bool(scenes->get_pending_request() == promise));
    CHECK(scenes->is_current(scenes->get_pending_request_id()));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR2 a second request supersedes the first, so "
    "one peer never has two answers in flight"
) {
    const Ref<NetwMultiplayerCore> core = configured_core();
    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();

    const Ref<netw::NetwPromise> first
        = core->scene_request(false, StringName("Arena"), Array(), false);
    const int first_id = scenes->get_pending_request_id();

    const Ref<netw::NetwPromise> second
        = core->scene_request(true, String("res://annex.tscn"), Array(), false);

    CHECK(first->get_is_settled());
    NETW_CHECK_EQ(first->get_code(), ERR_SKIP);
    CHECK_FALSE(second->get_is_settled());
    CHECK(scenes->get_pending_request_id() != first_id);
    CHECK_FALSE(scenes->is_current(first_id));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR3 an expiry answers only the request that is "
    "still in flight"
) {
    const Ref<NetwMultiplayerCore> core = configured_core();
    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();

    const Ref<netw::NetwPromise> stale
        = core->scene_request(false, StringName("Arena"), Array(), false);
    const int stale_id = scenes->get_pending_request_id();
    const Ref<netw::NetwPromise> live
        = core->scene_request(false, StringName("Annex"), Array(), false);

    core->scene_request_expire(stale_id);

    NETW_CHECK_EQ(stale->get_code(), ERR_SKIP);
    CHECK_FALSE(live->get_is_settled());

    core->scene_request_expire(scenes->get_pending_request_id());

    CHECK(live->get_is_settled());
    NETW_CHECK_EQ(live->get_code(), ERR_TIMEOUT);
}

} // namespace TestNetwSceneRequest
