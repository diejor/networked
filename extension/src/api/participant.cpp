#include "netw/api/participant.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

NetwMultiplayer *NetwPlayer::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

void NetwPlayer::bind_to(
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

void NetwPlayer::rebind_peer(int64_t p_peer) {
    peer_id = p_peer;
}

bool NetwPlayer::get_is_active() const {
    NetwMultiplayer *held = core();
    return held != nullptr && held->player_is_active(peer_id, incarnation);
}

int64_t NetwPlayer::get_peer_id() const {
    return peer_id;
}

int64_t NetwPlayer::player_id() const {
    return incarnation;
}

StringName NetwPlayer::get_username() const {
    return username;
}

TypedArray<NetwEntity> NetwPlayer::get_bodies() const {
    NETW_ZONE_NC("NetwPlayer bodies", colors::SESSION);
    NetwMultiplayer *held = core();
    return held == nullptr
        ? TypedArray<NetwEntity>()
        : held->player_bodies_held(peer_id, incarnation);
}

void NetwPlayer::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_peer_id"),
        &NetwPlayer::get_peer_id
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "peer_id"), "", "get_peer_id");
    ClassDB::bind_method(
        D_METHOD("get_is_active"),
        &NetwPlayer::get_is_active
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_active"),
        "",
        "get_is_active"
    );
    ClassDB::bind_method(
        D_METHOD("get_username"),
        &NetwPlayer::get_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "",
        "get_username"
    );
    ClassDB::bind_method(
        D_METHOD("get_bodies"),
        &NetwPlayer::get_bodies
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "bodies",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        "",
        "get_bodies"
    );
}

} // namespace netw
