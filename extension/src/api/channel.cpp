#include "netw/api/channel.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

ReplicationCore *NetwChannel::plane() const {
    NetwMultiplayer *api
        = Object::cast_to<NetwMultiplayer>(gd::object_of(session));
    return api != nullptr ? api->get_replication_plane() : nullptr;
}

Ref<NetwChannel> NetwChannel::over(int64_t p_id, NetwMultiplayer *p_session) {
    if (p_id < NetwMultiplayer::CHANNEL_USER_FIRST
        || p_id > NetwMultiplayer::CHANNEL_USER_LAST) {
        NETW_ERROR(
            sys::LIVENESS,
            "NetwChannel: id %d is outside the user range %d to %d",
            int(p_id),
            int(NetwMultiplayer::CHANNEL_USER_FIRST),
            int(NetwMultiplayer::CHANNEL_USER_LAST)
        );
        return Ref<NetwChannel>();
    }
    Ref<NetwChannel> channel;
    channel.instantiate();
    channel->id = p_id;
    channel->session = gd::instance_id(p_session);
    return channel;
}

Ref<NetwChannel> NetwChannel::of(Node *p_node, int64_t p_id) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::LIVENESS,
            "NetwChannel.of: no session governs the given node"
        );
        return Ref<NetwChannel>();
    }
    return over(p_id, api);
}

void NetwChannel::send(
    int64_t p_peer_id,
    const PackedByteArray &p_payload,
    bool p_reliable,
    bool p_batched
) {
    ReplicationCore *replication = plane();
    if (replication == nullptr) {
        return;
    }
    replication->send_to(
        p_peer_id,
        0,
        id,
        p_payload,
        p_reliable,
        0,
        String(),
        p_batched
    );
}

void NetwChannel::broadcast(
    const PackedByteArray &p_payload,
    bool p_reliable,
    bool p_batched
) {
    send(0, p_payload, p_reliable, p_batched);
}

void NetwChannel::forward_payload(
    const Variant &,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    const Callable &p_handler
) {
    p_handler.call(p_sender, p_payload);
}

void NetwChannel::register_handler(const Callable &p_handler) {
    ReplicationCore *replication = plane();
    if (replication == nullptr) {
        return;
    }
    replication->register_channel(
        id,
        callable_mp_static(&NetwChannel::forward_payload).bind(p_handler),
        false
    );
}

void NetwChannel::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwChannel",
        D_METHOD("of", "node", "id"),
        &NetwChannel::of
    );
    ClassDB::bind_method(
        D_METHOD("send", "peer_id", "payload", "reliable", "batched"),
        &NetwChannel::send,
        DEFVAL(true),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("broadcast", "payload", "reliable", "batched"),
        &NetwChannel::broadcast,
        DEFVAL(true),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("register", "handler"),
        &NetwChannel::register_handler
    );

    ClassDB::bind_method(D_METHOD("set_id", "id"), &NetwChannel::set_id);
    ClassDB::bind_method(D_METHOD("get_id"), &NetwChannel::get_id);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "id"), "set_id", "get_id");
}

} // namespace netw
