#include "netw/replication_send.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

namespace netw {

using namespace godot;

namespace {

/* The offer a send came from.
 *
 * The lane shape is part of the address here, not just of the lane maps: one
 * declared set offers its volatile and its retained half at the same route and
 * ordinal, and the two carry different schemas onto different channels.
 */
Dictionary offer_for(const Array &p_offers, const repl::RowSend &p_send) {
    for (int64_t at = 0; at < p_offers.size(); ++at) {
        const Dictionary entry = p_offers[at];
        if (int64_t(entry.get("route", -1)) == p_send.route
            && int64_t(entry.get("comp", -1)) == int64_t(p_send.comp)
            && bool(entry.get("reliable", false)) == p_send.reliable
            && bool(entry.get("windowed", false)) == p_send.windowed) {
            return entry;
        }
    }
    return Dictionary();
}


LocalVector<int> peers_of(const Variant &p_value) {
    LocalVector<int> out;
    const PackedInt32Array packed = p_value;
    for (int64_t at = 0; at < packed.size(); ++at) {
        out.push_back(packed[at]);
    }
    return out;
}

LocalVector<repl::RowOffer> offers_of(const Array &p_offers) {
    LocalVector<repl::RowOffer> offers;
    for (int64_t at = 0; at < p_offers.size(); ++at) {
        const Dictionary entry = p_offers[at];
        repl::RowOffer offer;
        offer.route = entry.get("route", 0);
        offer.comp = uint8_t(int64_t(entry.get("comp", 0)));
        offer.channel = uint8_t(int64_t(entry.get("channel", 0)));
        offer.schema = entry.get("schema", Variant());
        offer.values = entry.get("values", Array());
        offer.recipients = peers_of(entry.get("recipients", PackedInt32Array()));
        offer.priority = float(double(entry.get("priority", 1.0)));
        offer.masked = entry.get("masked", false);
        offer.reliable = entry.get("reliable", false);
        offer.windowed = entry.get("windowed", false);
        offer.window = uint32_t(int64_t(entry.get("window", 0)));
        offer.tick = entry.get("tick", -1);
        offers.push_back(offer);
    }
    return offers;
}

Array build_stage_args(
    const repl::RowSend &p_send,
    int64_t p_tick,
    const PackedByteArray &p_bytes
) {
    Array args;
    args.push_back(int64_t(p_send.peer));
    args.push_back(p_tick);
    args.push_back(p_send.route);
    args.push_back(int64_t(p_send.comp));
    args.push_back(int64_t(p_send.mask));
    args.push_back(p_bytes);
    return args;
}

} // namespace

void NetwReplicationSend::declare_channel(
    int64_t p_id,
    const StringName &p_name,
    bool p_fitted
) {
    wire::ChannelDecl decl;
    decl.id = uint8_t(p_id);
    decl.name = p_name;
    decl.delivery
        = p_fitted ? wire::Delivery::FITTED : wire::Delivery::IMMEDIATE;
    registry.register_channel(decl);
}

Dictionary NetwReplicationSend::run(
    const Array &p_offers,
    int64_t p_max_bits,
    int64_t p_seq,
    int64_t p_send_id
) {
    NETW_ZONE_NC("Replication send bridge", colors::WIRE);
    const LocalVector<repl::RowOffer> offers = offers_of(p_offers);

    const repl::SessionResult result = impl.run(
        registry,
        offers,
        p_max_bits,
        uint16_t(p_seq),
        p_send_id
    );
    return frames_of(p_offers, result, false);
}

Dictionary NetwReplicationSend::run_deferred(const Array &p_offers) {
    NETW_ZONE_NC("Replication send bridge, deferred", colors::WIRE);
    LocalVector<repl::RowOffer> offers = offers_of(p_offers);
    return frames_of(p_offers, impl.run_deferred(offers), true);
}

void NetwReplicationSend::confirm(int64_t p_index) {
    if (p_index < 0 || uint32_t(p_index) >= answered.size()) {
        return;
    }
    impl.defer(answered[uint32_t(p_index)]);
}

void NetwReplicationSend::commit(int64_t p_peer, int64_t p_seq) {
    impl.commit(int(p_peer), uint16_t(p_seq));
}

int64_t NetwReplicationSend::pending_count(int64_t p_peer) const {
    return int64_t(impl.pending_count(int(p_peer)));
}

Dictionary NetwReplicationSend::frames_of(
    const Array &p_offers,
    const repl::SessionResult &result,
    bool p_deferred
) {
    Array sends;
    answered.clear();
    int64_t dropped = 0;
    for (uint32_t at = 0; at < result.sends.size(); ++at) {
        const repl::RowSend &send = result.sends[at];
        // A FRAME, not a bare row. `apply` reads a frame, and a bridge whose
        // two halves disagreed about that would send bytes its own receiver
        // refuses.
        repl::RowFrameHeader header;
        const Dictionary offer = offer_for(p_offers, send);
        header.route = send.route;
        header.comp = send.comp;
        header.channel = uint8_t(int64_t(offer.get("channel", 0)));
        header.tick = offer.get("tick", -1);
        header.reconcile_ack = offer.get("ack", -1);
        header.mask = send.mask;
        const Ref<SchemaRecord> schema = offer.get("schema", Variant());
        const wire::WirePlan plan = wire::WirePlan::compile(schema);
        PackedByteArray bytes;
        if (send.windowed) {
            bytes = repl::write_window_frame(header, plan, send.samples);
        } else if (send.masked) {
            bytes = repl::write_row_frame(header, plan, send.row);
        } else {
            bytes = repl::write_plain_frame(header, plan, send.row);
        }
        if (bytes.is_empty()) {
            dropped += 1;
            continue;
        }
        if (stage.is_valid()) {
            const Array args = build_stage_args(send, header.tick, bytes);
            bytes = stage.callv(args);
            if (bytes.is_empty()) {
                dropped += 1;
                continue;
            }
        }
        Dictionary row;
        row["route"] = send.route;
        row["comp"] = int64_t(send.comp);
        row["peer"] = int64_t(send.peer);
        row["mask"] = int64_t(send.mask);
        row["reliable"] = send.reliable;
        row["windowed"] = send.windowed;
        row["samples"] = int64_t(send.samples.size());
        row["whole"] = send.mask == plan.full_mask();
        row["bytes"] = bytes;
        sends.push_back(row);
        if (p_deferred) {
            answered.push_back(send);
        }
    }

    Dictionary out;
    out["sends"] = sends;
    out["caught_up"] = int64_t(result.caught_up);
    out["ungathered"] = int64_t(result.ungathered);
    out["untrackable"] = result.untrackable;
    out["sent_bits"] = result.sent_bits;
    out["staged_out"] = dropped;
    return out;
}

void NetwReplicationSend::set_encode_stage(const Callable &p_stage) {
    stage = p_stage;
}

bool NetwReplicationSend::has_encode_stage() const {
    return stage.is_valid();
}

void NetwReplicationSend::acknowledge(int64_t p_peer, int64_t p_acked_seq) {
    impl.acknowledge(int(p_peer), uint16_t(p_acked_seq));
}

void NetwReplicationSend::retain(const PackedInt32Array &p_recipients) {
    impl.retain(peers_of(p_recipients));
}

void NetwReplicationSend::retain_row(
    int64_t p_route,
    int64_t p_comp,
    const PackedInt32Array &p_recipients
) {
    impl.retain_row(p_route, uint8_t(p_comp), peers_of(p_recipients));
}

void NetwReplicationSend::forget_peer(int64_t p_peer) {
    impl.forget_peer(int(p_peer));
}

void NetwReplicationSend::close_route(int64_t p_route) {
    impl.close_route(p_route);
}

Dictionary NetwReplicationSend::apply(
    const Ref<SchemaRecord> &p_schema,
    const PackedByteArray &p_held,
    const PackedByteArray &p_frame,
    bool p_masked
) {
    NETW_ZONE_NC("Replication apply bridge", colors::WIRE);
    Dictionary out;
    out["ok"] = false;
    out["values"] = Array();

    const wire::WirePlan plan = wire::WirePlan::compile(p_schema);
    if (!plan.valid()) {
        return out;
    }
    wire::CodeRow held = wire::CodeRow::for_plan(plan);
    if (!p_held.is_empty()) {
        held = wire::CodeRow::from_bytes(plan, p_held);
        if (!held.valid_for(plan)) {
            return out;
        }
    }

    repl::RowFrameHeader header;
    const bool read = p_masked
        ? repl::read_row_frame(p_frame, plan, header, held)
        : repl::read_plain_frame(p_frame, plan, header, held);
    if (!read) {
        return out;
    }
    Array values;
    if (!wire::decode_scalar_row(p_schema, held, values)) {
        return out;
    }

    out["ok"] = true;
    out["values"] = values;
    out["held"] = held.to_bytes();
    out["route"] = header.route;
    out["comp"] = int64_t(header.comp);
    out["mask"] = int64_t(header.mask);
    out["tick"] = header.tick;
    out["ack"] = header.reconcile_ack;
    out["whole"] = header.mask == plan.full_mask();
    return out;
}

Dictionary NetwReplicationSend::apply_window(
    const Ref<SchemaRecord> &p_schema,
    const PackedByteArray &p_frame
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
    if (!repl::read_window_frame(p_frame, plan, header, samples)) {
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
    out["route"] = header.route;
    out["comp"] = int64_t(header.comp);
    out["tick"] = header.tick;
    out["ack"] = header.reconcile_ack;
    return out;
}

Dictionary NetwReplicationSend::peek(const PackedByteArray &p_frame) const {
    Dictionary out;
    repl::RowFrameHeader header;
    if (!repl::peek_row_frame(p_frame, header)) {
        return out;
    }
    out["route"] = header.route;
    out["comp"] = int64_t(header.comp);
    out["channel"] = int64_t(header.channel);
    out["tick"] = header.tick;
    out["ack"] = header.reconcile_ack;
    return out;
}

int64_t NetwReplicationSend::lane_count() const {
    return int64_t(impl.lane_count());
}

int64_t NetwReplicationSend::retained_lane_count() const {
    return int64_t(impl.retained_lane_count());
}

int64_t NetwReplicationSend::window_lane_count() const {
    return int64_t(impl.window_lane_count());
}

int64_t NetwReplicationSend::outstanding() const {
    return int64_t(impl.outstanding());
}

void NetwReplicationSend::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("declare_channel", "id", "name", "fitted"),
        &NetwReplicationSend::declare_channel
    );
    ClassDB::bind_method(
        D_METHOD("run", "offers", "max_bits", "seq", "send_id"),
        &NetwReplicationSend::run
    );
    ClassDB::bind_method(
        D_METHOD("run_deferred", "offers"),
        &NetwReplicationSend::run_deferred
    );
    ClassDB::bind_method(
        D_METHOD("confirm", "index"),
        &NetwReplicationSend::confirm
    );
    ClassDB::bind_method(
        D_METHOD("commit", "peer", "seq"),
        &NetwReplicationSend::commit
    );
    ClassDB::bind_method(
        D_METHOD("pending_count", "peer"),
        &NetwReplicationSend::pending_count
    );
    ClassDB::bind_method(
        D_METHOD("set_encode_stage", "stage"),
        &NetwReplicationSend::set_encode_stage
    );
    ClassDB::bind_method(
        D_METHOD("has_encode_stage"),
        &NetwReplicationSend::has_encode_stage
    );
    ClassDB::bind_method(
        D_METHOD("acknowledge", "peer", "acked_seq"),
        &NetwReplicationSend::acknowledge
    );
    ClassDB::bind_method(
        D_METHOD("retain", "recipients"),
        &NetwReplicationSend::retain
    );
    ClassDB::bind_method(
        D_METHOD("retain_row", "route", "comp", "recipients"),
        &NetwReplicationSend::retain_row
    );
    ClassDB::bind_method(
        D_METHOD("apply", "schema", "held", "frame", "masked"),
        &NetwReplicationSend::apply
    );
    ClassDB::bind_method(
        D_METHOD("forget_peer", "peer"),
        &NetwReplicationSend::forget_peer
    );
    ClassDB::bind_method(
        D_METHOD("close_route", "route"),
        &NetwReplicationSend::close_route
    );
    ClassDB::bind_method(
        D_METHOD("peek", "frame"),
        &NetwReplicationSend::peek
    );
    ClassDB::bind_method(
        D_METHOD("lane_count"),
        &NetwReplicationSend::lane_count
    );
    ClassDB::bind_method(
        D_METHOD("retained_lane_count"),
        &NetwReplicationSend::retained_lane_count
    );
    ClassDB::bind_method(
        D_METHOD("window_lane_count"),
        &NetwReplicationSend::window_lane_count
    );
    ClassDB::bind_method(
        D_METHOD("apply_window", "schema", "frame"),
        &NetwReplicationSend::apply_window
    );
    ClassDB::bind_method(
        D_METHOD("outstanding"),
        &NetwReplicationSend::outstanding
    );
}

} // namespace netw
