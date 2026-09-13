#pragma once

#include "godot/callable.hpp"
#include "godot/variant.hpp"
#include "netw/repl/entity_book.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/session_send.hpp"

namespace netw {

using table::SchemaRecord;

class ReplicationSend {
    repl::SessionSend impl;
    repl::EntityBook entities;
    wire::WireRegistry registry = wire::WireRegistry::create_default();
    int64_t drops_baseline_unknown = 0;

public:
    bool declare_channel(
        int64_t p_id,
        const godot::StringName &p_name,
        bool p_fitted
    );

    repl::SessionResult run(
        const godot::LocalVector<repl::RowOffer> &p_offers,
        int64_t p_max_bits,
        int64_t p_base_tick
    );

    void confirm(const repl::RowSend &p_send);

    repl::RowExplain explain(
        int64_t p_route,
        int64_t p_comp,
        int64_t p_peer
    ) const;

    bool commit(int64_t p_peer, int64_t p_seq);
    int64_t pending_count(int64_t p_peer) const;

    void set_encode_stage(const godot::Callable &p_stage);
    bool has_encode_stage() const;

    repl::AckReport acknowledge(
        int64_t p_peer,
        int64_t p_acked_seq,
        uint32_t p_history,
        double p_rtt_ms,
        double p_jitter_ms,
        int64_t p_tick
    );

    repl::LinkGovernor::Mode link_mode(int64_t p_peer) const {
        return impl.link_mode(int(p_peer));
    }

    float link_loss(int64_t p_peer) const {
        return impl.link_loss(int(p_peer));
    }

    int64_t link_budget_bits(int64_t p_peer, int64_t p_full_bits) const {
        return impl.link_budget_bits(int(p_peer), p_full_bits);
    }
    void retain(const godot::PackedInt32Array &p_recipients);
    void retain_row(
        int64_t p_route,
        int64_t p_comp,
        const godot::PackedInt32Array &p_recipients
    );
    void forget_peer(int64_t p_peer);
    void close_route(int64_t p_route);

    godot::Dictionary apply(
        const SchemaRecord &p_schema,
        const godot::PackedByteArray &p_held,
        const godot::PackedByteArray &p_frame,
        int64_t p_base_tick,
        int64_t p_expected_life,
        repl::BaselineRing *p_ring = nullptr,
        int64_t p_seq = -1,
        repl::BaselineNaming p_naming = repl::BaselineNaming::BY_SEQ
    );

    int64_t baseline_drops() const {
        return drops_baseline_unknown;
    }

    int64_t entity_resolve(const repl::EntitySlot &p_slot, int64_t p_wanted) {
        return entities.resolve(p_slot, p_wanted);
    }

    void entity_bind(int64_t p_route);
    void entity_tombstone(int64_t p_route);
    godot::LocalVector<repl::EntityWrite> entity_release(int64_t p_route);
    int64_t entity_pending_count() const;

    godot::Dictionary apply_window(
        const SchemaRecord &p_schema,
        const godot::PackedByteArray &p_frame,
        int64_t p_base_tick,
        int64_t p_expected_life
    );

    int64_t lane_count() const;
    int64_t retained_lane_count() const;
    int64_t window_lane_count() const;
    int64_t outstanding() const;
};

} // namespace netw
