#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/lane_set.hpp"
#include "netw/repl/link_governor.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/send_pass.hpp"
#include "netw/repl/window_ring.hpp"

namespace netw::repl {

using netw::table::SchemaRecord;

struct RowOffer {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    const SchemaRecord *schema = nullptr;
    godot::Array values;
    godot::LocalVector<int> recipients;
    float priority = 1.0f;
    bool masked = false;
    bool reliable = false;
    bool windowed = false;
    uint32_t window = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    int64_t life = 0;

    const SchemaRecord &declared() const {
        static const SchemaRecord unplannable;
        return schema != nullptr ? *schema : unplannable;
    }
};

struct RowSend {
    int64_t route = 0;
    uint8_t comp = 0;
    int peer = 0;
    uint32_t offer = 0;
    uint64_t mask = 0;
    int64_t bits = 0;
    uint32_t sample_count = 0;
    bool masked = false;
    bool reliable = false;
    bool windowed = false;
    wire::CodeRow row;
    godot::PackedByteArray bytes;
};

struct SessionResult {
    godot::LocalVector<RowSend> sends;
    uint32_t ungathered = 0;
    uint32_t caught_up = 0;
    uint32_t deferred = 0;
    uint32_t staged_out = 0;
    int64_t sent_bits = 0;
};

struct AckReport {
    uint32_t delivered = 0;
    uint32_t lost = 0;
    int64_t delivered_bits = 0;
};

enum class RowVerdict : uint8_t {
    UNOFFERED,
    SENT,
    DEFERRED,
    CAUGHT_UP,
    UNGATHERED,
    REFUSED
};

struct RowExplain {
    RowVerdict verdict = RowVerdict::UNOFFERED;
    int64_t tick = -1;
    uint64_t sticky = 0;
    uint32_t in_flight = 0;
    bool has_baseline = false;
};

class SessionSend {
    struct Pending {
        int64_t route = 0;
        uint8_t comp = 0;
        int64_t bits = 0;
        bool masked = false;
        wire::CodeRow row;
    };

    LaneSet lanes;
    SendPass pass;
    LinkGovernor link;
    godot::HashMap<int, godot::LocalVector<Pending>> pending;
    godot::HashMap<int, godot::HashMap<uint64_t, float>> owed;
    godot::HashMap<int, godot::HashMap<uint64_t, RowExplain>> verdicts;
    godot::Callable stage;

    static uint64_t address_of(const RowSend &p_send);

    static uint64_t address_of(int64_t p_route, uint8_t p_comp);

    void note_verdict(
        int64_t p_route,
        uint8_t p_comp,
        int p_peer,
        RowVerdict p_verdict,
        int64_t p_tick
    );

    void note_offer_verdict(
        const RowOffer &p_offer,
        RowVerdict p_verdict,
        int64_t p_tick
    );

    float owed_by(const RowSend &p_send, float p_priority) const;

    godot::PackedByteArray price(
        const RowSend &p_send,
        int64_t p_base_tick
    ) const;

    godot::LocalVector<RowSend> collect(
        const godot::LocalVector<RowOffer> &p_offers,
        int64_t p_base_tick,
        godot::LocalVector<wire::FitCandidate> &r_candidates,
        SessionResult &r_out
    );

public:
    static constexpr int64_t VOLATILE_FLOOR_BITS = 128 * 8;

    void set_encode_stage(const godot::Callable &p_stage) {
        stage = p_stage;
    }

    bool has_encode_stage() const {
        return stage.is_valid();
    }

    SessionResult run(
        const wire::WireRegistry &p_registry,
        const godot::LocalVector<RowOffer> &p_offers,
        int64_t p_max_bits,
        int64_t p_base_tick
    );

    void defer(const RowSend &p_send);

    bool commit(int p_peer, uint16_t p_seq);

    uint32_t pending_count(int p_peer) const;

    void forget_peer(int p_peer);

    RowExplain explain(int64_t p_route, uint8_t p_comp, int p_peer) const;

    AckReport acknowledge(
        int p_peer,
        uint16_t p_acked_seq,
        uint32_t p_history,
        double p_rtt_ms = 0.0,
        double p_jitter_ms = 0.0,
        int64_t p_tick = 0
    );

    LinkGovernor::Mode link_mode(int p_peer) const {
        return link.mode(p_peer);
    }

    float link_loss(int p_peer) const {
        return link.loss(p_peer);
    }

    int64_t link_budget_bits(int p_peer, int64_t p_full_bits) const {
        return link.budget_bits(p_peer, p_full_bits);
    }

    void retain(const godot::LocalVector<int> &p_recipients);

    void retain_row(
        int64_t p_route,
        uint8_t p_comp,
        const godot::LocalVector<int> &p_recipients
    ) {
        lanes.retain_row(p_route, p_comp, p_recipients);
    }

    void close_route(int64_t p_route);

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
