#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/repl/lane_set.hpp"
#include "netw/repl/send_pass.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/api/schema_core.hpp"

namespace netw::repl {

struct RowOffer {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    godot::Ref<SchemaRecord> schema;
    godot::Array values;
    godot::LocalVector<int> recipients;
    float priority = 1.0f;
    bool masked = false;
    bool reliable = false;
    bool windowed = false;
    uint32_t window = 0;
    int64_t tick = -1;
};

struct RowSend {
    int64_t route = 0;
    uint8_t comp = 0;
    int peer = 0;
    uint64_t mask = 0;
    bool masked = false;
    bool reliable = false;
    bool windowed = false;
    wire::CodeRow row;
    godot::LocalVector<WindowSample> samples;
};

struct SessionResult {
    godot::LocalVector<RowSend> sends;
    uint32_t ungathered = 0;
    uint32_t caught_up = 0;
    bool untrackable = false;
    int64_t sent_bits = 0;
};

class SessionSend {
    struct Pending {
        int64_t route = 0;
        uint8_t comp = 0;
        wire::CodeRow row;
    };

    LaneSet lanes;
    SendPass pass;
    godot::HashMap<int, godot::LocalVector<Pending>> pending;

    godot::LocalVector<RowSend> collect(
        const godot::LocalVector<RowOffer> &p_offers,
        godot::LocalVector<wire::FitCandidate> &r_candidates,
        SessionResult &r_out
    );

public:
    SessionResult run(
        const wire::WireRegistry &p_registry,
        const godot::LocalVector<RowOffer> &p_offers,
        int64_t p_max_bits,
        uint16_t p_seq,
        int64_t p_send_id
    );

    SessionResult run_deferred(const godot::LocalVector<RowOffer> &p_offers);

    void defer(const RowSend &p_send);

    void commit(int p_peer, uint16_t p_seq);

    uint32_t pending_count(int p_peer) const;

    void forget_peer(int p_peer);

    void acknowledge(int p_peer, uint16_t p_acked_seq);

    void retain(const godot::LocalVector<int> &p_recipients);

    void retain_row(
        int64_t p_route,
        uint8_t p_comp,
        const godot::LocalVector<int> &p_recipients
    ) {
        lanes.retain_row(p_route, p_comp, p_recipients);
    }

    void close_route(int64_t p_route) {
        lanes.close_route(p_route);
    }

    uint32_t lane_count() const {
        return lanes.size();
    }

    uint32_t retained_lane_count() const {
        return lanes.retained_size();
    }

    uint32_t window_lane_count() const {
        return lanes.window_size();
    }

    int outstanding() const {
        return pass.outstanding();
    }
};

} // namespace netw::repl
