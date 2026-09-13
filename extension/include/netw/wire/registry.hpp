#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::wire {

enum class ChannelKind : uint8_t {
    KEYED = 0,
    SESSION = 1,
    ROUTED = 2,
};

enum class Reliability : uint8_t {
    UNRELIABLE = 0,
    UNRELIABLE_ACKED = 1,
    RELIABLE = 2,
};

enum class Freshness : uint8_t {
    NONE = 0,
    FRESHEST_WINS = 1,
};

enum class Delivery : uint8_t {
    IMMEDIATE = 0,
    FITTED = 1,
};

enum class Direction : uint8_t {
    EITHER = 0,
    SERVER_TO_CLIENT = 1,
    CLIENT_TO_SERVER = 2,
    OWNER_TO_SERVER = 3,
    SERVER_TO_OWNER = 4,
};

enum class PayloadContract : uint8_t {
    RAW = 0,
    PLANNED = 1,
    DELTA = 2,
};

inline constexpr uint16_t FORMAT_VERSION = 10;

struct ChannelDecl {
    uint8_t id = 0;
    godot::StringName name;
    ChannelKind kind = ChannelKind::SESSION;
    Reliability reliability = Reliability::RELIABLE;
    Freshness freshness = Freshness::NONE;
    Delivery delivery = Delivery::FITTED;
    Direction direction = Direction::EITHER;
    PayloadContract payload = PayloadContract::PLANNED;
    uint16_t payload_revision = 0;
    int32_t cap_bytes = 0;
    float priority = 1.0f;
    bool is_reserved = false;
    bool aggregated = false;

    bool valid() const {
        return id > 0 && !is_reserved;
    }
};

class WireRegistry {
public:
    static constexpr int MAX_CHANNELS = 256;

private:
    ChannelDecl channels[MAX_CHANNELS];
    bool registered[MAX_CHANNELS] = {false};

public:
    static WireRegistry create_default();

    bool register_channel(const ChannelDecl &decl);
    const ChannelDecl *find_channel(uint8_t id) const;
    const ChannelDecl *find_channel_by_name(
        const godot::StringName &name
    ) const;

    bool aggregates(uint8_t id, bool requested) const;

    static bool id_is_builtin(uint8_t id);

    uint64_t identity_hash() const;
    uint32_t active_count() const;
};

int64_t builtin_channel(const godot::StringName &name);

} // namespace netw::wire
