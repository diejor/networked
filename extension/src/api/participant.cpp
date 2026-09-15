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

void NetwParticipant::bind_to(
    NetwMultiplayer *p_core,
    int64_t p_peer,
    int64_t p_incarnation,
    const StringName &p_username
) {
    core_id = gd::instance_id(p_core);
    peer_id = p_peer;
    if (p_incarnation != 0) {
        incarnation = p_incarnation;
    }
    if (!String(p_username).is_empty()) {
        username = p_username;
    }
}

void NetwParticipant::rebind_peer(int64_t p_peer) {
    peer_id = p_peer;
}

bool NetwParticipant::get_is_active() const {
    NetwMultiplayer *held = core();
    return held != nullptr && held->participant_is_active(peer_id, incarnation);
}

int64_t NetwParticipant::get_peer_id() const {
    return peer_id;
}

int64_t NetwParticipant::membership() const {
    return incarnation;
}

Ref<NetwIdentity> NetwParticipant::get_identity() const {
    NetwMultiplayer *held = core();
    return held == nullptr ? Ref<NetwIdentity>()
                           : held->participant_identity(peer_id);
}

StringName NetwParticipant::get_username() const {
    return username;
}

TypedArray<NetwEntity> NetwParticipant::get_players() const {
    NETW_ZONE_NC("NetwParticipant players", colors::SESSION);
    NetwMultiplayer *held = core();
    return held == nullptr ? TypedArray<NetwEntity>()
                           : held->participant_players(peer_id);
}

void NetwParticipant::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_peer_id"),
        &NetwParticipant::get_peer_id
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "peer_id"), "", "get_peer_id");
    ClassDB::bind_method(
        D_METHOD("get_is_active"),
        &NetwParticipant::get_is_active
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_active"),
        "",
        "get_is_active"
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
        D_METHOD("get_players"),
        &NetwParticipant::get_players
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "players",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        "",
        "get_players"
    );
}

} // namespace netw
