#include "netw/wire/registry.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::wire {

namespace {

uint64_t hash_combine(uint64_t h, uint64_t v) {
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

} // namespace

bool WireRegistry::register_channel(const ChannelDecl &decl) {
    if (decl.id == 0 && !decl.is_reserved) {
        NETW_WARN(
            sys::WIRE,
            "Channel '%s' claims the reserved id 0 and was not registered.",
            godot::String(decl.name).utf8().get_data()
        );
        return false;
    }
    NETW_WARN_COND(
        registered[decl.id] && channels[decl.id].name != decl.name,
        sys::WIRE,
        "Channel id %d moves from '%s' to '%s'.",
        int(decl.id),
        godot::String(channels[decl.id].name).utf8().get_data(),
        godot::String(decl.name).utf8().get_data()
    );
    channels[decl.id] = decl;
    registered[decl.id] = true;
    return true;
}

const ChannelDecl *WireRegistry::find_channel(uint8_t id) const {
    if (!registered[id]) {
        return nullptr;
    }
    return &channels[id];
}

const ChannelDecl *WireRegistry::find_channel_by_name(
    const godot::StringName &name
) const {
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (registered[i] && channels[i].name == name) {
            return &channels[i];
        }
    }
    return nullptr;
}

bool WireRegistry::aggregates(uint8_t id, bool requested) const {
    const ChannelDecl *decl = find_channel(id);
    if (decl == nullptr || decl->is_reserved) {
        return requested;
    }
    if (decl->delivery == Delivery::IMMEDIATE) {
        return false;
    }
    return decl->aggregated || requested;
}

uint32_t WireRegistry::active_count() const {
    uint32_t count = 0;
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (registered[i] && !channels[i].is_reserved) {
            count++;
        }
    }
    return count;
}

bool WireRegistry::id_is_builtin(uint8_t id) {
    static const WireRegistry table = create_default();
    return table.registered[id];
}

uint64_t WireRegistry::identity_hash() const {
    uint64_t hash = 14695981039346656037ULL;
    hash = hash_combine(hash, FORMAT_VERSION);
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (!registered[i] || channels[i].is_reserved) {
            continue;
        }
        const ChannelDecl &d = channels[i];
        hash = hash_combine(hash, d.id);
        hash = hash_combine(hash, static_cast<uint64_t>(d.kind));
        hash = hash_combine(hash, static_cast<uint64_t>(d.reliability));
        hash = hash_combine(hash, static_cast<uint64_t>(d.freshness));
        hash = hash_combine(hash, static_cast<uint64_t>(d.delivery));
        hash = hash_combine(hash, static_cast<uint64_t>(d.direction));
        hash = hash_combine(hash, static_cast<uint64_t>(d.payload));
        if (d.payload_revision != 0) {
            hash = hash_combine(hash, d.payload_revision);
        }
        hash = hash_combine(hash, d.name.hash());
    }
    return hash;
}

WireRegistry WireRegistry::create_default() {
    NETW_ZONE_NC("WireRegistry default table", colors::WIRE);
    WireRegistry reg;

    auto res = [&](uint8_t id) {
        ChannelDecl d;
        d.id = id;
        d.is_reserved = true;
        reg.register_channel(d);
    };

    auto reg_c = [&](uint8_t id,
                     const char *name,
                     ChannelKind kind,
                     Reliability rel,
                     Freshness fresh,
                     Delivery deliv,
                     Direction dir,
                     PayloadContract payload) {
        ChannelDecl d;
        d.id = id;
        d.name = godot::StringName(name);
        d.kind = kind;
        d.reliability = rel;
        d.freshness = fresh;
        d.delivery = deliv;
        d.direction = dir;
        d.payload = payload;
        reg.register_channel(d);
    };

    res(0);
    res(1);
    res(7);

    reg_c(
        2,
        "ACTION",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::PLANNED
    );
    reg_c(
        3,
        "CALL",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::PLANNED
    );
    reg_c(
        4,
        "REPLY",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::PLANNED
    );
    reg_c(
        5,
        "SIGNAL",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::PLANNED
    );
    reg_c(
        6,
        "PROPERTY_SYNC",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::PLANNED
    );

    reg_c(
        8,
        "INTEREST_AWARENESS",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        9,
        "CLOCK_HANDSHAKE",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        10,
        "CLOCK_HANDSHAKE_REPLY",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        11,
        "CLOCK_PING",
        ChannelKind::SESSION,
        Reliability::UNRELIABLE,
        Freshness::NONE,
        Delivery::IMMEDIATE,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        12,
        "CLOCK_PONG",
        ChannelKind::SESSION,
        Reliability::UNRELIABLE,
        Freshness::NONE,
        Delivery::IMMEDIATE,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );

    reg_c(
        13,
        "LAGCOMP_DENY",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        14,
        "SPAWN",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        15,
        "DESPAWN",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        16,
        "REPARENT",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        17,
        "TABLE",
        ChannelKind::KEYED,
        Reliability::UNRELIABLE_ACKED,
        Freshness::FRESHEST_WINS,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::DELTA
    );

    reg_c(
        18,
        "HIDE",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );

    reg_c(
        19,
        "SYNC",
        ChannelKind::KEYED,
        Reliability::UNRELIABLE_ACKED,
        Freshness::FRESHEST_WINS,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::DELTA
    );
    reg_c(
        20,
        "SYNC_DELTA",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::DELTA
    );
    reg_c(
        21,
        "CONTROL_REQUEST",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        22,
        "CONTROL_APPLY",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );

    reg_c(
        23,
        "SESSION_JOIN",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        24,
        "SESSION_ACCEPT",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        25,
        "SESSION_ROSTER",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        26,
        "SESSION_PAUSE",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        27,
        "SESSION_UNPAUSE",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        28,
        "SESSION_KICKED",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        29,
        "SESSION_SCENE_REQUEST",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        30,
        "SESSION_SCENE_RESULT",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        31,
        "SESSION_SHUTDOWN",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        32,
        "SESSION_SCENE_RELEASED",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        33,
        "SESSION_KICK_REQUEST",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        34,
        "SESSION_LEAVE_REQUEST",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );

    reg_c(
        35,
        "PREDICT_COMMAND",
        ChannelKind::ROUTED,
        Reliability::UNRELIABLE,
        Freshness::FRESHEST_WINS,
        Delivery::FITTED,
        Direction::OWNER_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        36,
        "PREDICT_ACK",
        ChannelKind::ROUTED,
        Reliability::UNRELIABLE,
        Freshness::FRESHEST_WINS,
        Delivery::FITTED,
        Direction::SERVER_TO_OWNER,
        PayloadContract::PLANNED
    );
    reg_c(
        37,
        "PREDICT_RELAY",
        ChannelKind::ROUTED,
        Reliability::UNRELIABLE,
        Freshness::FRESHEST_WINS,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );
    reg_c(
        38,
        "PREDICT_RELAY_REQUEST",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::CLIENT_TO_SERVER,
        PayloadContract::PLANNED
    );
    reg_c(
        39,
        "SYNC_ROW",
        ChannelKind::KEYED,
        Reliability::UNRELIABLE_ACKED,
        Freshness::FRESHEST_WINS,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::DELTA
    );
    reg_c(
        40,
        "SYNC_ROW_DELTA",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::DELTA
    );

    reg_c(
        41,
        "SYNC_ROW_WINDOW",
        ChannelKind::KEYED,
        Reliability::UNRELIABLE,
        Freshness::FRESHEST_WINS,
        Delivery::FITTED,
        Direction::EITHER,
        PayloadContract::PLANNED
    );

    reg_c(
        42,
        "SESSION_SCENE_SEAT",
        ChannelKind::SESSION,
        Reliability::RELIABLE,
        Freshness::NONE,
        Delivery::FITTED,
        Direction::SERVER_TO_CLIENT,
        PayloadContract::PLANNED
    );

    auto batches = [&](const char *name) {
        const ChannelDecl *decl
            = reg.find_channel_by_name(godot::StringName(name));
        NETW_WARN_COND(
            decl == nullptr,
            sys::WIRE,
            "Channel '%s' batches on the tick flush but is not declared.",
            name
        );
        if (decl != nullptr) {
            reg.channels[decl->id].aggregated = true;
        }
    };
    batches("SIGNAL");
    batches("PROPERTY_SYNC");
    batches("TABLE");
    batches("SYNC");
    batches("SYNC_DELTA");
    batches("SYNC_ROW");
    batches("SYNC_ROW_DELTA");
    batches("SYNC_ROW_WINDOW");

    return reg;
}

int64_t builtin_channel(const godot::StringName &name) {
    static const WireRegistry table = WireRegistry::create_default();
    const ChannelDecl *decl = table.find_channel_by_name(name);
    if (decl == nullptr) {
        NETW_ERR_V(
            -1,
            sys::WIRE,
            "The built-in channel table declares no channel named '%s'.",
            godot::String(name).utf8().get_data()
        );
    }
    return int64_t(decl->id);
}

} // namespace netw::wire
