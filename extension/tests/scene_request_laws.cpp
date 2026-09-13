#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSceneRequest {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> configured_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->clock_engine().set_configured(true);
    core->session_get_inner()->set_multiplayer_peer(Ref<MultiplayerPeer>());
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR1 a scene request is open, current and "
    "unsettled from the moment it is asked for"
) {
    const Ref<NetwMultiplayer> core = configured_core();
    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();

    const Ref<netw::NetwPromise> promise = core->scene_request_send(
        String("res://arena.tscn"),
        netw::NetwSceneCore::SCOPE_SESSION
    );

    CHECK(promise.is_valid());
    CHECK_FALSE(promise->get_is_settled());
    CHECK(bool(scenes->get_pending_request() == promise));
    CHECK(scenes->is_current(scenes->get_pending_request_id()));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR2 a second request supersedes the first, so "
    "one peer never has two answers in flight"
) {
    const Ref<NetwMultiplayer> core = configured_core();
    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();

    const Ref<netw::NetwPromise> first = core->scene_request_send(
        String("res://arena.tscn"),
        netw::NetwSceneCore::SCOPE_SESSION
    );
    const int first_id = scenes->get_pending_request_id();

    const Ref<netw::NetwPromise> second = core->scene_request_send(
        String("res://annex.tscn"),
        netw::NetwSceneCore::SCOPE_SESSION
    );

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
    const Ref<NetwMultiplayer> core = configured_core();
    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();

    const Ref<netw::NetwPromise> stale = core->scene_request_send(
        String("res://arena.tscn"),
        netw::NetwSceneCore::SCOPE_SESSION
    );
    const int stale_id = scenes->get_pending_request_id();
    const Ref<netw::NetwPromise> live = core->scene_request_send(
        String("res://annex.tscn"),
        netw::NetwSceneCore::SCOPE_SESSION
    );

    core->scene_request_expire(stale_id);

    NETW_CHECK_EQ(stale->get_code(), ERR_SKIP);
    CHECK_FALSE(live->get_is_settled());

    core->scene_request_expire(scenes->get_pending_request_id());

    CHECK(live->get_is_settled());
    NETW_CHECK_EQ(live->get_code(), ERR_TIMEOUT);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR7 a request reaching no handler is denied "
    "rather than admitted by default, a handler answering anything but a "
    "numeric code is denied as invalid rather than as unauthorized so a "
    "broken handler reads apart from a refusing one, and only a handler that "
    "answers OK admits"
) {
    const Ref<NetwMultiplayer> core = configured_core();
    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();
    const Variant destination = Variant(StringName("Arena"));

    NETW_CHECK_EQ(
        int(scenes->decide_request(
            Variant(),
            destination,
            netw::NetwSceneCore::SCOPE_SESSION
        )),
        int(ERR_UNAUTHORIZED)
    );

    scenes->set_request_handler(callable_mp_static(
        +[](const Variant &, const Variant &, const Variant &) -> Variant {
            return String("sure");
        }
    ));
    NETW_CHECK_EQ(
        int(scenes->decide_request(
            Variant(),
            destination,
            netw::NetwSceneCore::SCOPE_SESSION
        )),
        int(ERR_INVALID_DATA)
    );

    scenes->set_request_handler(callable_mp_static(
        +[](const Variant &, const Variant &, const Variant &) -> Variant {
            return int64_t(OK);
        }
    ));
    NETW_CHECK_EQ(
        int(scenes->decide_request(
            Variant(),
            destination,
            netw::NetwSceneCore::SCOPE_SESSION
        )),
        int(OK)
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR8 a session that installs no handler is "
    "told so once, because deny-by-default is silent otherwise and a game "
    "that simply forgot Netw.configure_scene_requests reads as a game whose "
    "policy refused"
) {
    const Ref<NetwMultiplayer> core = configured_core();

    NETW_CHECK_EQ(
        int(core->scene_admits_request(
            Ref<netw::NetwParticipant>(),
            Variant(String("res://arena.tscn")),
            netw::NetwSceneCore::SCOPE_SESSION
        )),
        int(ERR_UNAUTHORIZED)
    );

    CHECK_FALSE(core->claim_verdict_warning(ERR_UNAUTHORIZED, 0));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR9 a handler answering no verdict is told "
    "so, and a handler REFUSING is not, because a refusal is the answer the "
    "game was asked for and only a broken handler is a fault"
) {
    const Ref<NetwMultiplayer> refusing = configured_core();
    refusing->get_scene_core()->set_request_handler(callable_mp_static(
        +[](const Variant &, const Variant &, const Variant &) -> Variant {
            return int64_t(ERR_UNAUTHORIZED);
        }
    ));

    NETW_CHECK_EQ(
        int(refusing->scene_admits_request(
            Ref<netw::NetwParticipant>(),
            Variant(String("res://arena.tscn")),
            netw::NetwSceneCore::SCOPE_SESSION
        )),
        int(ERR_UNAUTHORIZED)
    );
    CHECK(refusing->claim_verdict_warning(ERR_UNAUTHORIZED, 0));

    const Ref<NetwMultiplayer> broken = configured_core();
    broken->get_scene_core()->set_request_handler(callable_mp_static(
        +[](const Variant &, const Variant &, const Variant &) -> Variant {
            return String("sure");
        }
    ));

    NETW_CHECK_EQ(
        int(broken->scene_admits_request(
            Ref<netw::NetwParticipant>(),
            Variant(String("res://arena.tscn")),
            netw::NetwSceneCore::SCOPE_SESSION
        )),
        int(ERR_INVALID_DATA)
    );
    CHECK_FALSE(broken->claim_verdict_warning(ERR_INVALID_DATA, 0));
}

} // namespace TestNetwSceneRequest
