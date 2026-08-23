#include "netw/api/participant.hpp"

#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/colors.hpp"
#include "godot/callable.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

NetwMultiplayerCore *NetwParticipant::core() const {
    return Object::cast_to<NetwMultiplayerCore>(gd::instance_from_id(core_id));
}

void NetwParticipant::seat_at(NetwMultiplayerCore *p_core, int64_t p_peer) {
    core_id = gd::instance_id(p_core);
    peer_id = p_peer;
}

int64_t NetwParticipant::get_peer_id() const {
    return peer_id;
}

Ref<ResolvedJoin> NetwParticipant::get_join() const {
    NetwMultiplayerCore *held = core();
    if (held == nullptr) {
        return Ref<ResolvedJoin>();
    }
    return held->join_book().accepted_join(peer_id);
}

Ref<RefCounted> NetwParticipant::get_identity() const {
    NetwMultiplayerCore *held = core();
    return held == nullptr ? Ref<RefCounted>()
                           : held->participant_identity(peer_id);
}

StringName NetwParticipant::get_username() const {
    const Ref<ResolvedJoin> joined = get_join();
    return joined.is_valid() ? joined->get_username() : StringName();
}

Array NetwParticipant::get_arg_values() const {
    const Ref<ResolvedJoin> joined = get_join();
    return joined.is_valid() ? joined->get_arg_values() : Array();
}

bool NetwParticipant::get_is_debug() const {
    const Ref<ResolvedJoin> joined = get_join();
    return joined.is_valid() && joined->get_is_debug();
}

RID NetwParticipant::scene_entity_of(const Variant &p_scene) {
    const Ref<RefCounted> handle = p_scene;
    if (handle.is_null()) {
        return RID();
    }
    return handle->get(StringName("entity"));
}

Variant NetwParticipant::get_current_scene() const {
    NetwMultiplayerCore *held = core();
    if (held == nullptr) {
        return Variant();
    }
    const RID seat = held->participant_seat(peer_id);
    if (!seat.is_valid()) {
        return Variant();
    }
    return held->scene_handle_of(seat);
}

void NetwParticipant::set_current_scene(const Variant &p_scene) {
    NetwMultiplayerCore *held = core();
    if (held == nullptr) {
        return;
    }
    held->participant_seat_move(peer_id, scene_entity_of(p_scene));
}

void NetwParticipant::move_to(const Variant &p_dest) {
    NETW_ZONE_NC("NetwParticipant move_to", colors::SESSION);
    NetwMultiplayerCore *held = core();
    const RID destination = scene_entity_of(p_dest);
    if (held == nullptr || !destination.is_valid()) {
        return;
    }
    NETW_ERR_COND(
        !held->is_server(),
        sys::SESSION,
        "NetwParticipant.move_to is server-only, and peer %d asked",
        int(peer_id)
    );
    if (!held->is_server()) {
        return;
    }
    NETW_TRACE(
        sys::SESSION,
        "moving peer %d to scene %d",
        int(peer_id),
        int(destination.get_id())
    );
    if (held->participant_move_seat(peer_id, destination)) {
        held->interest_request_flush();
    }
}

void NetwParticipant::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_peer_id"), &NetwParticipant::get_peer_id);
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "peer_id"),
        "",
        "get_peer_id"
    );
    ClassDB::bind_method(D_METHOD("get_join"), &NetwParticipant::get_join);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "join",
            PROPERTY_HINT_RESOURCE_TYPE,
            "ResolvedJoin"
        ),
        "",
        "get_join"
    );
    ClassDB::bind_method(
        D_METHOD("get_identity"),
        &NetwParticipant::get_identity
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "identity",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwIdentity"
        ),
        "",
        "get_identity"
    );
    ClassDB::bind_method(
        D_METHOD("get_username"),
        &NetwParticipant::get_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "",
        "get_username"
    );
    ClassDB::bind_method(
        D_METHOD("get_arg_values"),
        &NetwParticipant::get_arg_values
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "arg_values"),
        "",
        "get_arg_values"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_debug"),
        &NetwParticipant::get_is_debug
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_debug"),
        "",
        "get_is_debug"
    );
    ClassDB::bind_method(
        D_METHOD("get_current_scene"),
        &NetwParticipant::get_current_scene
    );
    ClassDB::bind_method(
        D_METHOD("set_current_scene", "scene"),
        &NetwParticipant::set_current_scene
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "current_scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        ),
        "set_current_scene",
        "get_current_scene"
    );
    ClassDB::bind_method(D_METHOD("move_to", "dest"), &NetwParticipant::move_to);

    ADD_SIGNAL(MethodInfo(
        "scene_changed",
        PropertyInfo(
            Variant::OBJECT,
            "from",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        ),
        PropertyInfo(
            Variant::OBJECT,
            "to",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        )
    ));
}

} // namespace netw
