#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/snapshot_stream.hpp"

namespace netw::wire {

uint64_t next_stream_token();
uint64_t next_open_request();

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

class StreamReaderBook {
    struct Lane {
        uint64_t request = 0;
        uint64_t token = 0;
        uint64_t epoch = 0;
        uint32_t schema = 0;
        bool invalidated = false;
        SnapshotReceiver receiver;
    };

    struct Connection {
        uint64_t incarnation = 0;
        godot::HashMap<uint64_t, Lane> lanes;
        godot::HashMap<uint64_t, uint64_t> addresses;
    };

    godot::HashMap<int, Connection> connections;
    godot::HashMap<int, uint64_t> incarnations;
    uint64_t unknown_tokens = 0;

    Connection &connection(int p_peer);
    Lane *lane_at(int p_peer, uint64_t p_token);

public:
    uint64_t incarnation_of(int p_peer);

    OpenVerdict open(
        int p_peer,
        const StreamLane &p_lane,
        uint64_t p_request,
        uint64_t p_epoch,
        uint32_t p_schema,
        uint64_t &r_token
    );

    SnapshotReceiver *receiver(int p_peer, uint64_t p_token);

    bool names(int p_peer, uint64_t p_token, StreamLane &r_lane) const;

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

    uint32_t stream_count() const;
};

class StreamWriterBook {
    struct Lane {
        uint64_t request = 0;
        uint64_t token = 0;
        uint64_t epoch = 0;
        uint32_t schema = 0;
        int64_t repaired_at_ms = 0;
        bool token_was_reset = false;
        SnapshotSender sender;
    };

    struct Connection {
        godot::HashMap<uint64_t, Lane> lanes;
        godot::HashMap<uint64_t, uint64_t> requests;
        godot::HashMap<uint64_t, uint64_t> tokens;
    };

    godot::HashMap<int, Connection> connections;
    uint64_t unknown_receipts = 0;

    Lane *lane_at(int p_peer, const StreamLane &p_lane);
    Lane *lane_by_token(int p_peer, uint64_t p_token);

public:
    uint64_t open(
        int p_peer,
        const StreamLane &p_lane,
        uint64_t p_epoch,
        uint32_t p_schema
    );

    ReadyVerdict ready(int p_peer, uint64_t p_request, uint64_t p_token);

    bool reset(int p_peer, uint64_t p_request, uint64_t p_token);

    uint64_t token_of(int p_peer, const StreamLane &p_lane);

    uint64_t request_of(int p_peer, const StreamLane &p_lane);

    SnapshotSender *sender(int p_peer, const StreamLane &p_lane);

    SnapshotSender *sender_of_token(int p_peer, uint64_t p_token);

    bool names(int p_peer, uint64_t p_token, StreamLane &r_lane) const;

    ReceiptVerdict receipt(int p_peer, uint64_t p_token, uint64_t p_revision);

    uint64_t unknown_receipt_count() const {
        return unknown_receipts;
    }

    bool repair_due(
        int p_peer,
        const StreamLane &p_lane,
        int64_t p_now_ms,
        int64_t p_smoothed_rtt_ms
    );

    void note_attempt(
        int p_peer,
        const StreamLane &p_lane,
        int64_t p_now_ms
    );

    godot::LocalVector<StreamLane> unready(int p_peer) const;

    bool close(int p_peer, const StreamLane &p_lane, uint64_t &r_token);

    void forget_peer(int p_peer);

    void close_route(int64_t p_route);

    void clear();

    uint32_t stream_count() const;
};

int64_t repair_interval_ms(int64_t p_smoothed_rtt_ms);

} // namespace netw::wire
