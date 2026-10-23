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

void NetwPlayer::bind_account(
    const String &p_account,
    const String &p_account_name
) {
    account = p_account;
    account_name = p_account_name;
}

bool NetwPlayer::get_is_active() const {
    NetwMultiplayer *held = core();
    return held != nullptr && held->player_is_active(peer_id, incarnation);
}

String NetwPlayer::get_account() const {
    return account;
}

String NetwPlayer::get_account_name() const {
    return account_name;
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

TypedArray<NetwEntity> NetwPlayer::get_entities() const {
    NETW_ZONE_NC("NetwPlayer entities", colors::SESSION);
    NetwMultiplayer *held = core();
    return held == nullptr ? TypedArray<NetwEntity>()
                           : held->player_entities_held(peer_id, incarnation);
}

Ref<NetwScene> NetwPlayer::get_scene() const {
    NETW_ZONE_NC("NetwPlayer scene", colors::SESSION);
    NetwMultiplayer *held = core();
    if (held == nullptr || !held->player_is_active(peer_id, incarnation)) {
        return Ref<NetwScene>();
    }
    return held->scene_handle_of(held->scene_of_peer(peer_id));
}

void NetwPlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_peer_id"), &NetwPlayer::get_peer_id);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "peer_id"), "", "get_peer_id");
    ClassDB::bind_method(D_METHOD("get_is_active"), &NetwPlayer::get_is_active);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_active"), "", "get_is_active");
    ClassDB::bind_method(D_METHOD("get_username"), &NetwPlayer::get_username);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "",
        "get_username"
    );
    ClassDB::bind_method(D_METHOD("get_account"), &NetwPlayer::get_account);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "account"), "", "get_account");
    ClassDB::bind_method(
        D_METHOD("get_account_name"),
        &NetwPlayer::get_account_name
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "account_name"),
        "",
        "get_account_name"
    );
    ClassDB::bind_method(D_METHOD("get_entities"), &NetwPlayer::get_entities);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "entities",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        "",
        "get_entities"
    );
    ClassDB::bind_method(D_METHOD("get_scene"), &NetwPlayer::get_scene);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwScene",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_scene"
    );
    ADD_SIGNAL(MethodInfo(
        "scene_changed",
        PropertyInfo(
            Variant::OBJECT,
            "from",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwScene"
        ),
        PropertyInfo(
            Variant::OBJECT,
            "to",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwScene"
        )
    ));
    BIND_ENUM_CONSTANT(LEAVE_QUIT);
    BIND_ENUM_CONSTANT(LEAVE_STEPPED_AWAY);
    BIND_ENUM_CONSTANT(LEAVE_KICKED);
    BIND_ENUM_CONSTANT(LEAVE_DROPPED);
}

} // namespace netw
