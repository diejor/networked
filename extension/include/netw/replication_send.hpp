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

    const repl::SessionResult &run(
        int64_t p_max_bits,
        int64_t p_base_tick,
        int64_t p_now_ms = 0
    );

    const wire::WireRegistry &channels() const {
        return registry;
    }

    bool describe(const repl::RowSend &p_send, CarrierRow &r_row);

    repl::RowExplain explain(
        int64_t p_route,
        int64_t p_comp,
        int64_t p_peer
    ) const;

    bool commit(
        int64_t p_peer,
        int64_t p_seq,
        const godot::LocalVector<CarrierRow> &p_rows,
        int64_t p_frames,
        int64_t p_bits
    );

    void cancel(
        int64_t p_peer,
        const godot::LocalVector<CarrierRow> &p_rows,
        int64_t p_tick
    );

    void set_encode_stage(const godot::Callable &p_stage);
    bool has_encode_stage() const;

    void want_encode_stage(bool p_wanted) {
        impl.want_encode_stage(p_wanted);
    }

    void publish_tables(bool p_publishes) {
        impl.publish_tables(p_publishes);
    }

    repl::AckReport acknowledge(
        int64_t p_peer,
        int64_t p_acked_seq,
        uint32_t p_history,
        double p_rtt_ms,
        double p_jitter_ms,
        int64_t p_now_ms
    );

    void set_rate(double p_bytes_per_second) {
        impl.set_rate(p_bytes_per_second);
    }

    void set_peer_rate(int64_t p_peer, double p_bytes_per_second) {
        impl.set_peer_rate(int(p_peer), p_bytes_per_second);
    }

    void set_tickrate(int64_t p_tickrate) {
        impl.set_tickrate(p_tickrate);
    }

    void charge(int64_t p_peer, int64_t p_bytes) {
        impl.charge(int(p_peer), p_bytes);
    }

    void note_backlog(int64_t p_peer, int64_t p_buffered, int64_t p_now_ms) {
        impl.note_backlog(int(p_peer), p_buffered, p_now_ms);
    }

    const repl::PeerAccount *account(int64_t p_peer) const {
        return impl.account(int(p_peer));
    }

    double configured_rate(int64_t p_peer, int64_t p_max_bits) const {
        return impl.configured_rate(int(p_peer), p_max_bits);
    }

    double fill_of(int64_t p_peer, int64_t p_max_bits) const {
        return impl.fill_of(int(p_peer), p_max_bits);
    }

    repl::LinkGovernor::Mode link_mode(int64_t p_peer) const {
        return impl.link_mode(int(p_peer));
    }

    float link_loss(int64_t p_peer) const {
        return impl.link_loss(int(p_peer));
    }

    int64_t link_budget_bits(int64_t p_peer, int64_t p_full_bits) const {
        return impl.link_budget_bits(int(p_peer), p_full_bits);
    }

    int64_t control_budget_bytes(
        int64_t p_peer,
        int64_t p_reserved_bytes
    ) const {
        return impl.control_budget_bytes(int(p_peer), p_reserved_bytes);
    }
    void hide(int64_t p_peer, const wire::StreamLane &p_lane);
    void forget_peer(int64_t p_peer);
    void close_route(int64_t p_route);

    void note_baseline_drop() {
        drops_baseline_unknown += 1;
    }

    int64_t baseline_drops() const {
        return drops_baseline_unknown;
    }

    wire::StreamReaderBook &reader_book() {
        return impl.reader_book();
    }

    wire::ControlScheduler &control_scheduler() {
        return impl.control_scheduler();
    }

    void watch_store(store::World *p_world) {
        impl.watch_store(p_world);
    }

    store::World *store() const {
        return impl.store();
    }

    uint64_t stream_open(
        int64_t p_peer,
        const wire::StreamLane &p_lane,
        const wire::StreamTenure &p_tenure = wire::StreamTenure()
    ) {
        return impl.stream_open(int(p_peer), p_lane, p_tenure);
    }

    uint64_t token_of(int64_t p_peer, const wire::StreamLane &p_lane) const {
        return impl.token_of(int(p_peer), p_lane);
    }

    wire::ReadyVerdict stream_ready(
        int64_t p_peer,
        uint64_t p_request,
        uint64_t p_token
    ) {
        return impl.stream_ready(int(p_peer), p_request, p_token);
    }

    wire::ReceiptVerdict stream_receipt(
        int64_t p_peer,
        uint64_t p_token,
        uint64_t p_revision
    ) {
        return impl.stream_receipt(int(p_peer), p_token, p_revision);
    }

    bool stream_reset(int64_t p_peer, uint64_t p_request, uint64_t p_token) {
        return impl.stream_reset(int(p_peer), p_request, p_token);
    }

    void close_tenures(int64_t p_route, uint64_t p_tenure) {
        impl.close_tenures(p_route, p_tenure);
    }

    void close_anchors(int64_t p_route, uint64_t p_anchor, uint64_t p_author) {
        impl.close_anchors(p_route, p_anchor, p_author);
    }

    godot::LocalVector<wire::ParkedOpen> expire_parks(int64_t p_now_ms) {
        return impl.expire_parks(p_now_ms);
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

    int64_t window_lane_count() const;
    int64_t outstanding() const;
};

} // namespace netw
