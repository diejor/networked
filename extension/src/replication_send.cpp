#include "netw/replication_send.hpp"

#include "netw/repl/snapshot_frame.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

namespace netw {

using namespace godot;

bool ReplicationSend::declare_channel(
    int64_t p_id,
    const StringName &p_name,
    bool p_fitted
) {
    if (wire::WireRegistry::id_is_builtin(uint8_t(p_id))) {
        NETW_WARN(
            sys::WIRE,
            "Channel id %d belongs to a built-in channel, so '%s' cannot use "
            "it. Pick another id.",
            int(p_id),
            String(p_name).utf8().get_data()
        );
        return false;
    }
    wire::ChannelDecl decl;
    decl.id = uint8_t(p_id);
    decl.name = p_name;
    decl.delivery
        = p_fitted ? wire::Delivery::FITTED : wire::Delivery::IMMEDIATE;
    return registry.register_channel(decl);
}

const repl::SessionResult &ReplicationSend::run(
    int64_t p_max_bits,
    int64_t p_base_tick,
    int64_t p_now_ms
) {
    return impl.run(registry, p_max_bits, p_base_tick, p_now_ms);
}

bool ReplicationSend::describe(const repl::RowSend &p_send, CarrierRow &r_row) {
    return impl.describe(p_send, r_row);
}

repl::RowExplain ReplicationSend::explain(
    int64_t p_route,
    int64_t p_comp,
    int64_t p_peer
) const {
    return impl.explain(p_route, uint8_t(p_comp), int(p_peer));
}

bool ReplicationSend::commit(
    int64_t p_peer,
    int64_t p_seq,
    const godot::LocalVector<CarrierRow> &p_rows,
    int64_t p_frames,
    int64_t p_bits
) {
    if (p_seq < 0) {
        return false;
    }
    return impl.commit(int(p_peer), uint16_t(p_seq), p_rows, p_frames, p_bits);
}

void ReplicationSend::cancel(
    int64_t p_peer,
    const godot::LocalVector<CarrierRow> &p_rows,
    int64_t p_tick
) {
    impl.cancel(int(p_peer), p_rows, p_tick);
}

void ReplicationSend::set_encode_stage(const Callable &p_stage) {
    impl.set_encode_stage(p_stage);
}

bool ReplicationSend::has_encode_stage() const {
    return impl.has_encode_stage();
}

repl::AckReport ReplicationSend::acknowledge(
    int64_t p_peer,
    int64_t p_acked_seq,
    uint32_t p_history,
    double p_rtt_ms,
    double p_jitter_ms,
    int64_t p_now_ms
) {
    return impl.acknowledge(
        int(p_peer),
        uint16_t(p_acked_seq),
        p_history,
        p_rtt_ms,
        p_jitter_ms,
        p_now_ms
    );
}

void ReplicationSend::hide(int64_t p_peer, const wire::StreamLane &p_lane) {
    impl.hide(int(p_peer), p_lane);
}

void ReplicationSend::forget_peer(int64_t p_peer) {
    impl.forget_peer(int(p_peer));
}

void ReplicationSend::close_route(int64_t p_route) {
    impl.close_route(p_route);
}

Dictionary ReplicationSend::apply_window(
    const SchemaRecord &p_schema,
    const PackedByteArray &p_frame,
    int64_t p_base_tick,
    int64_t p_expected_life
) {
    NETW_ZONE_NC("Replication window apply bridge", colors::WIRE);
    Dictionary out;
    out["ok"] = false;
    out["samples"] = Array();

    const wire::WirePlan plan = wire::WirePlan::compile(p_schema);
    if (!plan.valid()) {
        return out;
    }
    repl::RowFrameHeader header;
    LocalVector<repl::WindowSample> samples;
    if (!repl::read_window_frame(
            p_frame,
            p_base_tick,
            p_expected_life,
            plan,
            header,
            samples
        )) {
        return out;
    }
    Array rows;
    for (uint32_t at = 0; at < samples.size(); ++at) {
        Array values;
        if (!wire::decode_scalar_row(p_schema, samples[at].row, values)) {
            return out;
        }
        Dictionary entry;
        entry["tick"] = samples[at].tick;
        entry["values"] = values;
        rows.push_back(entry);
    }

    out["ok"] = true;
    out["samples"] = rows;
    out["life"] = header.life;
    out["tick"] = header.tick;
    out["ack"] = header.reconcile_ack;
    return out;
}

void ReplicationSend::entity_bind(int64_t p_route) {
    entities.bind_route(p_route);
}

void ReplicationSend::entity_tombstone(int64_t p_route) {
    entities.tombstone_route(p_route);
}

LocalVector<repl::EntityWrite> ReplicationSend::entity_release(
    int64_t p_route
) {
    LocalVector<repl::EntityWrite> completed;
    entities.release(p_route, completed);
    return completed;
}

int64_t ReplicationSend::entity_pending_count() const {
    return int64_t(entities.pending_count());
}

int64_t ReplicationSend::window_lane_count() const {
    return int64_t(impl.window_lane_count());
}

int64_t ReplicationSend::outstanding() const {
    return int64_t(impl.outstanding());
}

} // namespace netw
