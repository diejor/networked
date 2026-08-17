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

// Declares the execution contract for one network channel.
struct ChannelDecl {
    uint8_t id = 0;
    godot::StringName name;
    ChannelKind kind = ChannelKind::SESSION;
    Reliability reliability = Reliability::RELIABLE;
    Freshness freshness = Freshness::NONE;
    Delivery delivery = Delivery::FITTED;
    Direction direction = Direction::EITHER;
    PayloadContract payload = PayloadContract::PLANNED;
    int32_t cap_bytes = 0;
    float priority = 1.0f;
    bool is_reserved = false;
    /* Whether a frame here waits for the carrier's flush without being asked.
     *
     * Outside `identity_hash` on purpose, and the only declared field that is:
     * a receiver reads the same frames whether they arrived alone or inside a
     * run, so how a sender paces them is local configuration rather than wire
     * shape, and two builds that pace differently still agree about the wire.
     */
    bool aggregated = false;

    bool valid() const {
        return id > 0 && !is_reserved;
    }
};

// Central source of truth for channel contracts and protocol identity hashing.
class WireRegistry {
public:
    static constexpr int MAX_CHANNELS = 256;

private:
    ChannelDecl channels[MAX_CHANNELS];
    bool registered[MAX_CHANNELS] = { false };

public:
    static WireRegistry create_default();

    bool register_channel(const ChannelDecl &decl);
    const ChannelDecl *find_channel(uint8_t id) const;
    const ChannelDecl *find_channel_by_name(
        const godot::StringName &name
    ) const;

    /* Whether a frame on `id` joins the per-peer aggregation.
     *
     * An IMMEDIATE channel refuses `requested` outright, because the round
     * trip a CLOCK_PING measures would otherwise be measuring the aggregation
     * delay. An id no declaration covers is answered by the ask alone, because
     * a custom channel is declared nowhere else.
     */
    bool aggregates(uint8_t id, bool requested) const;

    uint64_t identity_hash() const;
    uint32_t active_count() const;
};

} // namespace netw::wire
