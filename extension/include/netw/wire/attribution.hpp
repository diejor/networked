#pragma once

#include <cstdint>

#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw::wire {

struct Attribution {
    int64_t peer = 0;
    int64_t seq = -1;
    int64_t channel = 0;
    int64_t route = 0;
    int64_t comp = 0;
    int64_t bits = 0;
    int64_t column = -1;
};

enum class Refusal : uint8_t {
    NONE = 0,
    GATE = 1,
    STALE = 2,
    UNROUTED = 3,
    UNBOUND = 4,
    MALFORMED = 5,
    BASELINE_UNKNOWN = 6,
    COUNT = 7,
};

const char *refusal_name(Refusal p_refusal);

class AttributionBook {
    godot::HashMap<uint64_t, int64_t> bytes_out;
    godot::HashMap<uint64_t, int64_t> bytes_in;
    godot::HashMap<int64_t, int64_t> route_out;
    godot::HashMap<int64_t, int64_t> route_in;
    godot::HashMap<uint64_t, int64_t> column_bits;
    godot::HashMap<uint64_t, int64_t> frames_by_peer_channel_in;
    godot::HashMap<uint64_t, int64_t> refusals;
    godot::HashMap<uint64_t, godot::Vector<Attribution>> staged;

    int64_t attributed_out = 0;
    int64_t attributed_in = 0;
    int64_t framing_out = 0;
    int64_t frames_out = 0;
    int64_t frames_in = 0;
    int64_t staged_dropped_out = 0;
    int64_t refused_in = 0;
    int64_t wire_out = 0;
    int64_t wire_in = 0;
    int64_t datagrams_out = 0;
    int64_t datagrams_in = 0;
    bool armed = false;

    static uint64_t lane_key(int64_t p_peer, bool p_reliable, bool p_carrier);
    static uint64_t peer_channel_key(int64_t p_peer, int64_t p_channel);
    static uint64_t refusal_key(
        int64_t p_peer,
        int64_t p_channel,
        Refusal p_refusal
    );

public:
    void stage(bool p_reliable, bool p_carrier, const Attribution &p_frame);
    int64_t commit(
        int64_t p_peer,
        bool p_reliable,
        bool p_carrier,
        int64_t p_seq
    );
    void discard(int64_t p_peer, bool p_reliable, bool p_carrier);
    void discard_peer(int64_t p_peer);

    void note_framing_out(int64_t p_bytes);
    void note_datagram(bool p_inbound, int64_t p_bytes);
    void observe_in(const Attribution &p_frame);
    void note_refusal(int64_t p_peer, int64_t p_channel, Refusal p_refusal);
    void note_column(int64_t p_schema, int64_t p_column, int64_t p_bits);

    void set_armed(bool p_armed) {
        armed = p_armed;
    }
    bool is_armed() const {
        return armed;
    }

    int64_t get_attributed_out() const {
        return attributed_out;
    }
    int64_t get_attributed_in() const {
        return attributed_in;
    }
    int64_t get_framing_out() const {
        return framing_out;
    }
    int64_t get_frames_out() const {
        return frames_out;
    }
    int64_t get_frames_in() const {
        return frames_in;
    }
    int64_t get_staged_dropped_out() const {
        return staged_dropped_out;
    }
    int64_t get_refused_in() const {
        return refused_in;
    }
    int64_t get_wire_out() const {
        return wire_out;
    }
    int64_t get_wire_in() const {
        return wire_in;
    }
    int64_t get_datagrams_out() const {
        return datagrams_out;
    }
    int64_t get_datagrams_in() const {
        return datagrams_in;
    }
    int64_t peer_channel_out(int64_t p_peer, int64_t p_channel) const;
    int64_t peer_channel_in(int64_t p_peer, int64_t p_channel) const;
    int64_t peer_channel_refused(
        int64_t p_peer,
        int64_t p_channel,
        Refusal p_refusal
    ) const;

    godot::Dictionary snapshot() const;
    void clear();
};

} // namespace netw::wire
