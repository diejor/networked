#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/snapshot_stream.hpp"

namespace netw::wire {

uint64_t next_stream_token();
uint64_t next_open_request();

constexpr uint64_t STREAM_BINDING_SPAN = 256;
constexpr uint32_t DORMANT_LANE_CAP = 1024;

struct StreamLane {
    int64_t route = 0;
    uint8_t ordinal = 0;
    StreamFamily family = StreamFamily::VOLATILE;

    uint64_t address() const {
        return (uint64_t(route) << 16) | (uint64_t(ordinal) << 8)
            | uint64_t(family);
    }

    static int64_t route_of(uint64_t p_address) {
        return int64_t(p_address >> 16);
    }
};

enum class OpenVerdict : uint8_t {
    MINTED,
    REPEATED,
    INVALIDATED,
    SUPERSEDED,
    EXHAUSTED,
};

enum class ReadyVerdict : uint8_t {
    SEATED,
    UNKNOWN,
};

struct StreamTenure {
    uint64_t tenure = 0;
    bool bound = false;
    uint64_t anchor = 0;
    uint64_t anchor_author = 0;

    bool operator==(const StreamTenure &p_other) const {
        return tenure == p_other.tenure && bound == p_other.bound
            && anchor == p_other.anchor
            && anchor_author == p_other.anchor_author;
    }
};

typedef bool (*TenureDooms)(
    const StreamTenure &p_lane,
    const StreamTenure &p_bar
);

bool binding_follows(
    const StreamTenure &p_origin,
    const StreamTenure &p_bound
);

uint64_t binding_floor(
    const StreamTenure &p_origin,
    const StreamTenure &p_bound
);

struct BoundLane {
    int peer = 0;
    StreamLane lane;
    StreamTenure tenure;
};

struct ParkedOpen {
    int peer = 0;
    StreamLane lane;
    uint64_t request = 0;
    uint64_t epoch = 0;
    uint32_t schema = 0;
    uint64_t tenure = 0;
    uint64_t anchor = 0;
    uint64_t anchor_author = 0;
    int64_t parked_at_ms = 0;
};

struct ClosedLane {
    int peer = 0;
    uint64_t token = 0;
    StreamLane lane;
};

class StreamReaderBook {
    struct Lane {
        uint64_t request = 0;
        uint64_t token = 0;
        uint64_t epoch = 0;
        uint32_t schema = 0;
        StreamTenure origin;
        StreamTenure tenure;
        uint64_t floor = 0;
        bool held = false;
        bool invalidated = false;
    };

    struct Connection {
        uint64_t incarnation = 0;
        godot::HashMap<uint64_t, Lane> lanes;
        godot::HashMap<uint64_t, uint64_t> addresses;
        godot::HashMap<uint64_t, ParkedOpen> parks;
        godot::HashMap<uint64_t, int64_t> strays;
    };

    godot::HashMap<int, Connection> connections;
    godot::HashMap<int, uint64_t> incarnations;
    uint64_t unknown_tokens = 0;
    uint64_t expired_parks = 0;
    uint64_t refused_opens = 0;
    uint64_t held_rows = 0;
    uint64_t fenced_rows = 0;

    Connection &connection(int p_peer);
    Lane *lane_at(int p_peer, uint64_t p_token);
    uint32_t close_where(
        int64_t p_route,
        TenureDooms p_dooms,
        const StreamTenure &p_bar
    );

public:
    uint64_t incarnation_of(int p_peer);

    OpenVerdict open(
        int p_peer,
        const StreamLane &p_lane,
        uint64_t p_request,
        uint64_t p_epoch,
        uint32_t p_schema,
        uint64_t &r_token,
        const StreamTenure &p_tenure = StreamTenure()
    );

    void park(const ParkedOpen &p_open);

    void refuse_open() {
        refused_opens += 1;
    }

    godot::LocalVector<ParkedOpen> take_parks(int64_t p_route);

    godot::LocalVector<ParkedOpen> expire_parks(
        int p_peer,
        int64_t p_now_ms,
        int64_t p_interval_ms
    );

    godot::LocalVector<int> parked_peers() const;

    godot::LocalVector<BoundLane> lanes_bound_before(
        int64_t p_route,
        uint64_t p_tenure
    ) const;

    bool follow_tenure(
        int p_peer,
        const StreamLane &p_lane,
        uint64_t p_tenure,
        bool p_admitted
    );

    bool admits_row(int p_peer, uint64_t p_token, uint64_t p_revision);

    bool holds(int p_peer, const StreamLane &p_lane) const;

    bool answers_stray(
        int p_peer,
        uint64_t p_token,
        int64_t p_now_ms,
        int64_t p_interval_ms
    );

    uint32_t close_anchors_before(
        int64_t p_route,
        uint64_t p_anchor,
        uint64_t p_author
    );

    bool reaches(int p_peer, uint64_t p_token);

    bool names(int p_peer, uint64_t p_token, StreamLane &r_lane) const;

    uint64_t token_at(int p_peer, const StreamLane &p_lane) const;

    bool invalidate(
        int p_peer,
        const StreamLane &p_lane,
        uint64_t &r_request,
        uint64_t &r_token
    );

    bool close(int p_peer, uint64_t p_token);

    void forget_peer(int p_peer);

    void close_route(int64_t p_route);

    void clear();

    uint64_t unknown_token_count() const {
        return unknown_tokens;
    }

    uint64_t expired_park_count() const {
        return expired_parks;
    }

    uint64_t refused_open_count() const {
        return refused_opens;
    }

    uint64_t held_row_count() const {
        return held_rows;
    }

    uint64_t fenced_row_count() const {
        return fenced_rows;
    }

    uint32_t parked_count() const;

    uint32_t stream_count() const;
};

int64_t repair_interval_ms(int64_t p_smoothed_rtt_ms);

} // namespace netw::wire
