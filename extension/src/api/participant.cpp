#include "netw/api/participant.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

NetwMultiplayer *NetwParticipant::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

void NetwParticipant::seat_at(NetwMultiplayer *p_core, int64_t p_peer) {
    core_id = gd::instance_id(p_core);
    peer_id = p_peer;
}

int64_t NetwParticipant::get_peer_id() const {
    return peer_id;
}

Ref<ResolvedJoin> NetwParticipant::get_join() const {
    NetwMultiplayer *held = core();
    if (held == nullptr) {
        return Ref<ResolvedJoin>();
    }
    return held->join_book().accepted_join(peer_id);
}

Ref<NetwIdentity> NetwParticipant::get_identity() const {
    NetwMultiplayer *held = core();
    return held == nullptr ? Ref<NetwIdentity>()
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

Ref<NetwSceneHandle> NetwParticipant::get_current_scene() const {
    NetwMultiplayer *held = core();
    if (held == nullptr) {
        return Ref<NetwSceneHandle>();
    }
    const RID seat = held->participant_seat(peer_id);
    if (!seat.is_valid()) {
        return Ref<NetwSceneHandle>();
    }
    return held->scene_handle_of(seat);
}

Ref<NetwPromise> NetwParticipant::move_to(
    const Ref<NetwSceneHandle> &p_destination
) {
    NETW_ZONE_NC("NetwParticipant move_to", colors::SESSION);
    NetwMultiplayer *held = core();
    if (held == nullptr) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("this participant belongs to no live session")
        );
    }
    return held->participant_travel(Ref<NetwParticipant>(this), p_destination);
}

void NetwParticipant::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_peer_id"),
        &NetwParticipant::get_peer_id
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "peer_id"), "", "get_peer_id");
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
        D_METHOD("get_current_scene"),
        &NetwParticipant::get_current_scene
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "current_scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        ),
        "",
        "get_current_scene"
    );
    ClassDB::bind_method(
        D_METHOD("move_to", "destination"),
        &NetwParticipant::move_to
    );

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
