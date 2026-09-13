#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"

#include "netw/api/participant.hpp"

#include "godot/class_db.hpp"
#include "godot/engine_debugger.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/packed_scene.hpp"
#include "godot/physics_server.hpp"
#include "godot/rendering_server.hpp"
#include "godot/resource.hpp"
#include "godot/spatial_node.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/identity.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/spec.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

const char *SIG_PEER_AUTHENTICATION_FAILED = "peer_authentication_failed";
const char *SIG_PEER_PACKET = "peer_packet";

constexpr int64_t CALL_TARGET_EVERYONE = 0;
constexpr int64_t CALL_TARGET_SERVER = 1;
constexpr int64_t CALL_TARGET_PEER = 2;
constexpr int64_t CALL_AWAITS_REPLY = 4;

PackedByteArray call_frame(
    int64_t p_target_type,
    int64_t p_txn,
    bool p_awaits_reply,
    int64_t p_target_peer,
    const Variant &p_method,
    const LocalVector<call_args::Slot> &p_slots,
    const Array &p_quantizers,
    const Array &p_types
) {
    wire::WriteStream stream;
    uint64_t flags = uint64_t(p_target_type)
        | uint64_t(p_awaits_reply ? CALL_AWAITS_REPLY : 0);
    if (!stream.bits(flags, 8)) {
        return PackedByteArray();
    }
    if (p_awaits_reply) {
        uint64_t txn = uint64_t(MAX(p_txn, int64_t(0)));
        if (!stream.varuint(txn, 5)) {
            return PackedByteArray();
        }
    }
    if (p_target_type == CALL_TARGET_PEER) {
        uint64_t target = uint64_t(p_target_peer) & 0xFFFFFFFFULL;
        if (!stream.bits(target, 32)) {
            return PackedByteArray();
        }
    }
    if (!script::model::write_call_body(
            stream,
            p_method,
            p_slots,
            p_quantizers,
            p_types
        )
        || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

} // namespace

Error NetwMultiplayer::relay_subscribe(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer,
    bool p_subscribed
) {
    if (!is_host()) {
        return ERR_UNAUTHORIZED;
    }
    if (p_entity.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    const RID entity = p_entity->get_rid_handle();
    if (!liveness_core->entity_is_valid(entity)) {
        return ERR_DOES_NOT_EXIST;
    }
    const int64_t slot = entity.get_id();
    if (!p_subscribed) {
        relay_book.set_subscribed(slot, p_peer, false);
        return OK;
    }
    if (!interest_admits(entity, p_peer)) {
        return ERR_UNAUTHORIZED;
    }
    relay_book.set_subscribed(slot, p_peer, true);
    return OK;
}

PackedInt64Array NetwMultiplayer::relay_peers(int64_t p_entity_slot) const {
    return relay_book.peers(p_entity_slot);
}

void NetwMultiplayer::relay_release(int64_t p_entity_slot) {
    relay_book.release(p_entity_slot);
}

int NetwMultiplayer::relay_request_of(const PackedByteArray &p_payload) const {
    return predict::RelayBook::request_of(p_payload);
}

void NetwMultiplayer::channel_dispatch(
    const Ref<NetwEntity> &p_wrapper,
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    const Callable *handler = channel_handlers.getptr(p_channel);
    if (handler == nullptr || !handler->is_valid()) {
        return;
    }
    handler->call(
        p_wrapper.is_valid() ? p_wrapper->get_rid_handle() : RID(),
        p_payload,
        p_sender
    );
}

void NetwMultiplayer::rpc_channel_register(
    int64_t p_channel,
    const Callable &p_handler,
    bool p_defer_when_unknown
) {
    if (p_channel < CHANNEL_USER_FIRST || p_channel > CHANNEL_USER_LAST
        || !p_handler.is_valid()) {
        return;
    }
    channel_handlers.insert(p_channel, p_handler);
    get_channel_book()->register_channel(
        p_channel,
        callable_mp(this, &NetwMultiplayer::channel_dispatch).bind(p_channel),
        p_defer_when_unknown
    );
}

NetwCarrierFrame NetwMultiplayer::receive_header(
    int64_t p_peer,
    const PackedByteArray &p_packet
) {
    const NetwCarrierFrame header = NetwCarrierFrame::read(p_packet);
    if (header.kind == NetwCarrierFrame::FOREIGN) {
        emit_signal(SIG_PEER_PACKET, p_peer, p_packet);
        return header;
    }
    if (header.kind == NetwCarrierFrame::MALFORMED) {
        if (plane.wants(EventPlane::DATAGRAM_MALFORMED, 0)) {
            EventPlane::Emission fact(
                EventPlane::DATAGRAM_MALFORMED,
                EventPlane::AFTER,
                clock_engine().get_tick()
            );
            fact.peer = p_peer;
            fact.verdict = ERR_INVALID_DATA;
            Dictionary detail;
            detail["size"] = p_packet.size();
            fact.detail = detail;
            plane.emit(fact);
        }
        return header;
    }
    count_received(p_packet.size() - header.payload_offset);
    attribution.note_datagram(true, int64_t(p_packet.size()));
    capture_note(wire::CaptureDirection::IN, p_peer, p_packet);
    if (plane.wants(EventPlane::DATAGRAM_RECEIVED, 0)) {
        EventPlane::Emission fact(
            EventPlane::DATAGRAM_RECEIVED,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        const bool reliable = header.kind == NetwCarrierFrame::RELIABLE;
        fact.peer = p_peer;
        Dictionary detail;
        detail["size"] = p_packet.size() - header.payload_offset;
        detail["seq"] = reliable ? int64_t(-1) : header.seq;
        detail["reliable"] = reliable;
        fact.detail = detail;
        plane.emit(fact);
    }
    return header;
}

Callable NetwMultiplayer::relay_for(const StringName &p_signal, int p_arity) {
    switch (p_arity) {
        case 0:
            return callable_mp(this, &NetwMultiplayer::relay_bare)
                .bind(p_signal);
        case 1:
            return callable_mp(this, &NetwMultiplayer::relay_one)
                .bind(p_signal);
        case 2:
            return callable_mp(this, &NetwMultiplayer::relay_two)
                .bind(p_signal);
    }
    NETW_ERR_V(
        Callable(),
        sys::SESSION,
        "no relay for a %d argument signal, %s is not published",
        p_arity,
        String(p_signal)
    );
}

void NetwMultiplayer::relay_from(
    Object *p_source,
    const StringName &p_signal,
    int p_arity
) {
    relay_named_from(p_source, p_signal, p_signal, p_arity);
}

void NetwMultiplayer::relay_named_from(
    Object *p_source,
    const StringName &p_source_signal,
    const StringName &p_own_signal,
    int p_arity
) {
    const Callable relay = relay_for(p_own_signal, p_arity);
    if (relay.is_null()) {
        return;
    }
    p_source->connect(p_source_signal, relay);
}

void NetwMultiplayer::stop_relay_named_from(
    Object *p_source,
    const StringName &p_source_signal,
    const StringName &p_own_signal,
    int p_arity
) {
    const Callable relay = relay_for(p_own_signal, p_arity);
    if (relay.is_null()) {
        return;
    }
    p_source->disconnect(p_source_signal, relay);
}

void NetwMultiplayer::stop_relay_from(
    Object *p_source,
    const StringName &p_signal,
    int p_arity
) {
    const Callable relay = relay_for(p_signal, p_arity);
    if (relay.is_null()) {
        return;
    }
    p_source->disconnect(p_signal, relay);
}

void NetwMultiplayer::relay_bare(const StringName &p_signal) {
    emit_signal(p_signal);
}

void NetwMultiplayer::relay_one(
    const Variant &p_first,
    const StringName &p_signal
) {
    emit_signal(p_signal, p_first);
}

void NetwMultiplayer::relay_two(
    const Variant &p_first,
    const Variant &p_second,
    const StringName &p_signal
) {
    emit_signal(p_signal, p_first, p_second);
}

void NetwMultiplayer::relay_auth_failed(int64_t p_peer) {
    Dictionary detail;
    detail["peer"] = p_peer;
    event_emit(
        EventPlane::PEER_AUTH_FAILED,
        0,
        detail,
        StringName(),
        p_peer,
        OK,
        Dictionary()
    );
    emit_signal(SIG_PEER_AUTHENTICATION_FAILED, p_peer);
    if (!probe_forget(p_peer)) {
        NETW_WARN(
            netw::sys::SESSION,
            "authentication failed for peer %d",
            int(p_peer)
        );
    }
}

bool NetwMultiplayer::rpc_routes_through_session(
    Object *p_object,
    const StringName &p_method,
    const Array &p_args,
    int64_t p_peer
) {
    Node *node = Object::cast_to<Node>(p_object);
    if (node == nullptr) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null() || liveness_route_of(entity.ptr()) <= 0) {
        return false;
    }
    rpc_call(Callable(node, p_method), p_args, p_peer);
    return true;
}

#if defined(NETW_MODULE)
Error NetwMultiplayer::rpcp(
    Object *p_object,
    int p_peer_id,
    const StringName &p_method,
    const Variant **p_args,
    int p_argcount
) {
    Array args;
    for (int at = 0; at < p_argcount; ++at) {
        args.push_back(*p_args[at]);
    }
    if (rpc_routes_through_session(p_object, p_method, args, p_peer_id)) {
        return OK;
    }
    return inner->rpcp(p_object, p_peer_id, p_method, p_args, p_argcount);
}
#else
Error NetwMultiplayer::_rpc(
    int32_t p_peer_id,
    Object *p_object,
    const StringName &p_method,
    const Array &p_args
) {
    if (rpc_routes_through_session(p_object, p_method, p_args, p_peer_id)) {
        return OK;
    }
    return inner->rpc(p_peer_id, p_object, p_method, p_args);
}
#endif

bool NetwMultiplayer::send_admits(
    int64_t p_peer_id,
    const Ref<NetwEntity> &p_entity
) {
    if (liveness_state_of(p_entity.ptr())
        != int64_t(NetwLivenessCore::STATE_LIVE)) {
        return false;
    }
    if (p_peer_id == 1) {
        return true;
    }
    if (!interest_entity_has_filter(p_entity)) {
        return true;
    }
    return interest_wire_admits(p_peer_id, p_entity);
}

PackedInt32Array NetwMultiplayer::send_recipients(
    const Ref<NetwEntity> &p_entity,
    const PackedInt32Array &p_peers
) {
    PackedInt32Array out;
    for (int at = 0; at < p_peers.size(); at++) {
        if (send_admits(int64_t(p_peers[at]), p_entity)) {
            out.push_back(p_peers[at]);
        }
    }
    return out;
}

NetwPredictTiming NetwMultiplayer::frame_timing() const {
    const ClockEngine &clock = clock_engine();
    const double ticktime = clock.ticktime();
    return NetwPredictTiming::of(
        clock.get_tick() - 1,
        ticktime,
        ticktime,
        physics_frame,
        declared_quantum(),
        clock.is_simulating()
    );
}

void NetwMultiplayer::count_sent(int64_t p_bytes) {
    sent_packets += 1;
    sent_bytes += p_bytes;
}

void NetwMultiplayer::attribution_set_armed(bool p_armed) {
    attribution.set_armed(p_armed);
}

bool NetwMultiplayer::attribution_is_armed() const {
    return attribution.is_armed();
}

Dictionary NetwMultiplayer::attribution_snapshot() const {
    Dictionary out = attribution.snapshot();
    const int64_t attributed_out = attribution.get_attributed_out();
    const int64_t framing_out = attribution.get_framing_out();
    const int64_t dropped_out = attribution.get_staged_dropped_out();
    const int64_t attributed_in = attribution.get_attributed_in();

    Dictionary residual;
    residual[StringName("datagram_bytes_out")] = sent_bytes;
    residual[StringName("attributed_out")] = attributed_out;
    residual[StringName("framing_out")] = framing_out;
    residual[StringName("staged_dropped_out")] = dropped_out;
    residual[StringName("residual_out")]
        = sent_bytes - attributed_out - framing_out;
    residual[StringName("datagram_bytes_in")] = received_bytes;
    residual[StringName("attributed_in")] = attributed_in;
    residual[StringName("residual_in")] = received_bytes - attributed_in;

    const int64_t wire_out = attribution.get_wire_out();
    const int64_t wire_in = attribution.get_wire_in();
    residual[StringName("wire_bytes_out")] = wire_out;
    residual[StringName("wire_bytes_in")] = wire_in;
    residual[StringName("datagrams_out")] = attribution.get_datagrams_out();
    residual[StringName("datagrams_in")] = attribution.get_datagrams_in();
    residual[StringName("carrier_overhead_out")]
        = wire_out - attributed_out - framing_out;
    residual[StringName("carrier_overhead_in")] = wire_in - attributed_in;
    out[StringName("residual")] = residual;
    return out;
}

void NetwMultiplayer::attribution_note_refusal(
    int64_t p_peer,
    int64_t p_channel,
    wire::Refusal p_refusal
) {
    attribution.note_refusal(p_peer, p_channel, p_refusal);
}

void NetwMultiplayer::attribution_note_subject(int64_t p_route) {
    attribution_subject_route = p_route;
}

void NetwMultiplayer::attribution_observe_in(
    int64_t p_peer,
    int64_t p_channel,
    int64_t p_route,
    int64_t p_comp,
    int64_t p_bytes
) {
    wire::Attribution incoming;
    incoming.peer = p_peer;
    incoming.channel = p_channel;
    incoming.route = p_route;
    incoming.comp = p_comp;
    incoming.bits = p_bytes * 8;
    attribution.observe_in(incoming);
}

void NetwMultiplayer::attribution_note_column(
    int64_t p_schema,
    int64_t p_column,
    int64_t p_bits
) {
    attribution.note_column(p_schema, p_column, p_bits);
}

Dictionary NetwMultiplayer::capture_header() {
    Dictionary header = wire::spec_document();
    header[StringName("peer")] = int64_t(get_unique_id());
    header[StringName("schemas")] = wire::spec_schemas(schema_core);
    return header;
}

void NetwMultiplayer::capture_note(
    wire::CaptureDirection p_dir,
    int64_t p_peer,
    const PackedByteArray &p_datagram
) {
    if (!capture_decided) {
        capture_decided = true;
        const String path = wire::CaptureWriter::claim_armed_path();
        if (!path.is_empty()) {
            capture.open(path, capture_header());
        }
    }
    if (!capture.is_open()) {
        return;
    }
    capture.note(p_dir, p_peer, clock_engine().get_tick(), p_datagram);
}

void NetwMultiplayer::capture_close() {
    capture.close();
}

const StringName &NetwMultiplayer::attribution_feed_message() {
    static const StringName message("netw:attribution");
    return message;
}

Array NetwMultiplayer::attribution_feed_payload() {
    Array payload;
    if (!attribution.is_armed()) {
        return payload;
    }
    payload.push_back(attribution.snapshot());
    return payload;
}

void NetwMultiplayer::attribution_feed_push() {
    if (!gd::debugger_active()) {
        return;
    }
    const Array payload = attribution_feed_payload();
    if (payload.is_empty()) {
        return;
    }
    gd::debugger_send(attribution_feed_message(), payload);
}

void NetwMultiplayer::count_received(int64_t p_bytes) {
    received_packets += 1;
    received_bytes += p_bytes;
}

void NetwMultiplayer::count_state_ack_out() {
    state_acks_out += 1;
}

void NetwMultiplayer::count_state_ack_in() {
    state_acks_in += 1;
}

void NetwMultiplayer::count_standalone_ack_out() {
    state_acks_out += 1;
    standalone_acks_out += 1;
}

int64_t NetwMultiplayer::receive_tick() const {
    return clock_engine().get_configured() ? clock_engine().get_tick()
                                           : frame_counter;
}

bool NetwMultiplayer::seq_is_fresher(int64_t a, int64_t b) {
    return DatagramSeqBook::is_fresher(uint16_t(a), uint16_t(b));
}

int64_t NetwMultiplayer::datagram_budget() const {
    constexpr int64_t HEADROOM = 150;
    constexpr int64_t FLOOR = 128;
    if (inner.is_null()) {
        return FLOOR;
    }
    const int64_t ceiling
        = int64_t(inner->get_max_sync_packet_size()) - HEADROOM;
    return ceiling > FLOOR ? ceiling : FLOOR;
}

bool NetwMultiplayer::channel_aggregates(
    int64_t p_channel,
    bool p_requested
) const {
    if (p_channel < 0 || p_channel > 255) {
        return false;
    }
    return channels.aggregates(uint8_t(p_channel), p_requested);
}

PackedByteArray NetwMultiplayer::frame_pack(
    int64_t p_route,
    int64_t p_comp,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    const String &p_path
) {
    return wire::frame_pack(
        p_route,
        uint8_t(p_comp),
        uint8_t(p_channel),
        p_payload,
        p_path
    );
}

void NetwMultiplayer::rpc_note_dropped_unroutable() {
    ++rpc_dropped_unroutable;
}

void NetwMultiplayer::rpc_note_dropped_not_live() {
    ++rpc_dropped_not_live;
}

void NetwMultiplayer::rpc_warn_dropped_once(Node *p_target) {
    if (p_target == nullptr) {
        return;
    }
    const uint64_t held = uint64_t(gd::instance_id(p_target));
    if (rpc_drop_warned.has(held)) {
        return;
    }
    rpc_drop_warned.insert(held);
    NETW_WARN(
        sys::TRANSPORT,
        "a call addressed to '%s' was dropped: it holds no live route, and "
        "a call is not replayed the way a state frame is. If the entity was "
        "despawned, stop calling it. If it has not spawned yet, wait for "
        "entity_live. Later calls to this target are counted in "
        "rpc_sends_dropped_not_live rather than reported.",
        String(p_target->get_name()).utf8().get_data()
    );
}

int64_t NetwMultiplayer::rpc_sends_dropped_unroutable() const {
    return rpc_dropped_unroutable;
}

int64_t NetwMultiplayer::rpc_sends_dropped_not_live() const {
    return rpc_dropped_not_live;
}

void NetwMultiplayer::rpc_resolve_parked(
    int64_t p_id,
    const Callable &p_callback
) {
    if (rpc_park.resolve(p_id) && p_callback.is_valid()) {
        p_callback.call();
    }
}

void NetwMultiplayer::rpc_defer_call(
    int64_t p_sender,
    int64_t p_route,
    const Callable &p_callback
) {
    const int64_t timeout = clock_engine().get_configured()
        ? int64_t(clock_engine().get_tickrate())
        : 30;
    const int64_t id
        = rpc_park.park(p_sender, p_route, receive_tick() + timeout);
    if (id < 0) {
        return;
    }
    liveness_when_live(
        p_route,
        callable_mp(this, &NetwMultiplayer::rpc_resolve_parked)
            .bind(id, p_callback),
        0,
        Callable()
    );
}

void NetwMultiplayer::rpc_sweep_deferred_calls() {
    rpc_park.sweep(receive_tick());
}

void NetwMultiplayer::rpc_sweep_transactions(int64_t p_current) {
    if (rpc_disposed) {
        return;
    }
    const PackedInt64Array expired = rpc_txns.expire(p_current);
    for (int at = 0; at < expired.size(); ++at) {
        const int64_t txn = expired[at];
        const RpcSettle settle = rpc_settle_of(txn);
        rpc_settles.erase(txn);
        if (settle.single.is_valid()) {
            settle.single->reject(ERR_TIMEOUT, String());
            continue;
        }
        if (settle.group.is_valid()) {
            settle.group->reject(ERR_TIMEOUT, String());
        }
    }
}

void NetwMultiplayer::rpc_handle_disconnect(int64_t p_peer) {
    if (rpc_disposed) {
        return;
    }
    const PackedInt64Array waiting = rpc_txns.waiting_on(p_peer);
    for (int at = 0; at < waiting.size(); ++at) {
        const int64_t txn = waiting[at];
        const RpcSettle settle = rpc_settle_of(txn);
        if (settle.single.is_valid()) {
            settle.single->reject(ERR_UNAVAILABLE, String("Peer disconnected"));
            rpc_settle_close(txn);
            continue;
        }
        if (settle.group.is_valid()) {
            settle.group->remove_peer(p_peer);
            if (settle.group->get_is_completed()) {
                rpc_settle_close(txn);
            }
        }
    }
}

void NetwMultiplayer::rpc_clear_session() {
    rpc_park.clear();
    rpc_calls_deferred = 0;
    if (rpc_disposed) {
        return;
    }
    const PackedInt64Array abandoned = rpc_txns.drain();
    const HashMap<int64_t, RpcSettle> settles(rpc_settles);
    rpc_settles.clear();
    for (int at = 0; at < abandoned.size(); ++at) {
        const RpcSettle *found = settles.getptr(abandoned[at]);
        if (found == nullptr) {
            continue;
        }
        if (found->single.is_valid()) {
            found->single->reject(ERR_UNAVAILABLE, String("Session ended"));
            continue;
        }
        if (found->group.is_valid()) {
            found->group->reject(ERR_UNAVAILABLE, String("Session ended"));
        }
    }
}

void NetwMultiplayer::rpc_dispose() {
    rpc_disposed = true;
    rpc_settles.clear();
}

Dictionary NetwMultiplayer::rpc_counters() const {
    Dictionary out;
    out[StringName("drops_backlog_limit")] = rpc_park.refused();
    out[StringName("sends_dropped_unroutable")]
        = rpc_sends_dropped_unroutable();
    out[StringName("sends_dropped_not_live")] = rpc_sends_dropped_not_live();
    return out;
}

Dictionary NetwMultiplayer::relay_stats_snapshot() {
    ReplicationCore *plane = get_replication_plane();
    const Dictionary repl = plane == nullptr ? Dictionary() : plane->counters();
    const Dictionary rpc = rpc_counters();
    static const char *RELAYED[] = {
        "drops_unknown_route",     "drops_not_live",
        "drops_no_node",           "drops_traversal",
        "drops_comp_unresolved",   "sync_drops_stale",
        "derived_sets_active",     "derived_frames_in",
        "drops_derived_no_set",    "drops_derived_bad_sender",
        "drops_derived_schema",    "row_frames_out",
        "row_frames_full",         "row_frames_stage_refused",
        "row_frames_ungathered",   "retained_frames_out",
        "window_frames_out",       "window_samples_out",
        "sync_sets_active",        "sync_frames_out",
        "sync_frames_in",          "delta_frames_out",
        "delta_frames_in",         "drops_sync_no_set",
        "drops_sync_bad_sender",   "drops_sync_poisoned",
        "drops_sync_unknown_flag", "spawn_book_armed",
        "spawn_book_spawned",      "spawn_book_recv",
    };
    Dictionary out;
    for (const char *key : RELAYED) {
        out[StringName(key)] = repl.get(StringName(key), int64_t(0));
    }
    out[StringName("drops_backlog_limit")]
        = rpc[StringName("drops_backlog_limit")];
    out[StringName("sends_dropped_unroutable")]
        = int64_t(repl.get(StringName("sends_dropped_unroutable"), 0))
        + int64_t(rpc[StringName("sends_dropped_unroutable")]);
    out[StringName("sends_dropped_not_live")]
        = int64_t(repl.get(StringName("sends_dropped_not_live"), 0))
        + int64_t(rpc[StringName("sends_dropped_not_live")]);
    out[StringName("sent_packets")] = sent_packets;
    out[StringName("sent_bytes")] = sent_bytes;
    out[StringName("received_packets")] = received_packets;
    out[StringName("received_bytes")] = received_bytes;
    out[StringName("state_acks_out")] = state_acks_out;
    out[StringName("state_acks_in")] = state_acks_in;
    out[StringName("standalone_acks_out")] = standalone_acks_out;
    out[StringName("attributed_bytes_out")] = attribution.get_attributed_out();
    out[StringName("attributed_bytes_in")] = attribution.get_attributed_in();
    out[StringName("attributed_frames_out")] = attribution.get_frames_out();
    out[StringName("attributed_frames_in")] = attribution.get_frames_in();
    out[StringName("attribution_framing_out")] = attribution.get_framing_out();
    out[StringName("attribution_dropped_out")]
        = attribution.get_staged_dropped_out();
    return out;
}

Dictionary NetwMultiplayer::lagcomp_metrics() const {
    Dictionary out = predict_metrics();
    out[StringName("pending_actions")] = pending_action_count();
    out[StringName("gate_fallbacks")] = action_gate_fallbacks();
    out[StringName("effects_armed")] = lagcomp_effect_count();
    return out;
}

Dictionary NetwMultiplayer::stats_snapshot() {
    NETW_ZONE_NC("session stats snapshot", colors::SESSION);
    Dictionary out = relay_stats_snapshot();
    out[StringName("pending_live")] = liveness_pending_live_count();

    const Dictionary interest = interest_monitor_snapshot();
    const Array interest_keys = interest.keys();
    for (int at = 0; at < interest_keys.size(); ++at) {
        const String counter = interest_keys[at];
        out[StringName("interest_" + counter)] = interest[counter];
    }
    if (table_core.is_valid()) {
        out.merge(table_core->counters());
    }

    const Dictionary predict = lagcomp_metrics();
    out[StringName("predict_entities")] = predict[StringName("entities")];
    out[StringName("predict_timelines")] = predict[StringName("timelines")];
    out[StringName("predict_corrections")] = predict[StringName("corrections")];
    out[StringName("predict_max_replay_depth")]
        = predict[StringName("max_replay_depth")];
    out[StringName("predict_consumed")] = predict[StringName("consumed")];
    out[StringName("predict_missing")] = predict[StringName("missing")];
    out[StringName("predict_pending_actions")]
        = predict.has(StringName("predict_pending_actions"))
        ? predict[StringName("predict_pending_actions")]
        : predict[StringName("pending_actions")];
    out[StringName("predict_effects_armed")]
        = predict[StringName("effects_armed")];
    out[StringName("predict_gate_fallbacks")]
        = predict[StringName("gate_fallbacks")];

    const Dictionary joint = predict[StringName("joint")];
    out[StringName("joint_passes")] = joint[StringName("joint_passes")];
    out[StringName("joint_members")] = joint[StringName("joint_members")];
    out[StringName("joint_cells_relayed")] = joint[StringName("cells_relayed")];
    out[StringName("joint_cells_substituted")]
        = joint[StringName("cells_substituted")];
    out[StringName("joint_heal_snaps")] = joint[StringName("heal_snaps")];
    out[StringName("joint_linger_held")] = joint[StringName("linger_held")];

    if (display_book.is_valid()) {
        const display::PumpStats &display = display_book->get_stats();
        out[StringName("display_runtimes")] = display.runtimes;
        out[StringName("display_starving")] = display.starving;
        out[StringName("display_sleeping")] = display.sleeping;
        out[StringName("display_projecting")] = display.projecting;
        out[StringName("display_snaps")] = display.snaps;
        out[StringName("display_max_display_lag")]
            = int64_t(display.max_display_lag);
        out[StringName("display_max_forecast_age")]
            = int64_t(display.max_forecast_age);
    }

    out[StringName("verdict_does_not_exist")]
        = stats_get_verdict_count(ERR_DOES_NOT_EXIST);
    out[StringName("verdict_skip")] = stats_get_verdict_count(ERR_SKIP);
    out[StringName("verdict_unavailable")]
        = stats_get_verdict_count(ERR_UNAVAILABLE);
    out[StringName("verdict_unauthorized")]
        = stats_get_verdict_count(ERR_UNAUTHORIZED);
    out[StringName("verdict_invalid_data")]
        = stats_get_verdict_count(ERR_INVALID_DATA);
    out[StringName("verdict_busy")] = stats_get_verdict_count(ERR_BUSY);

#define NETW_SESSION_STAT_FILL(m_name, m_key) \
    if (!out.has(StringName(m_key))) { \
        out[StringName(m_key)] = 0; \
    }
    NETW_SESSION_STAT_TABLE(NETW_SESSION_STAT_FILL)
#undef NETW_SESSION_STAT_FILL
    return out;
}

int64_t NetwMultiplayer::stats_get(Stat p_stat) {
    static const char *STAT_KEYS[] = {
#define NETW_SESSION_STAT_KEY(m_name, m_key) m_key,
        NETW_SESSION_STAT_TABLE(NETW_SESSION_STAT_KEY)
#undef NETW_SESSION_STAT_KEY
    };
    if (p_stat < 0 || p_stat >= int64_t(STAT_COUNT)) {
        return 0;
    }
    return int64_t(stats_snapshot().get(StringName(STAT_KEYS[p_stat]), 0));
}

void NetwMultiplayer::rpc_settle_open(
    int64_t p_txn,
    const RpcSettle &p_settle
) {
    rpc_settles[p_txn] = p_settle;
}

NetwMultiplayer::RpcSettle NetwMultiplayer::rpc_settle_of(int64_t p_txn) const {
    const RpcSettle *found = rpc_settles.getptr(p_txn);
    return found ? *found : RpcSettle();
}

void NetwMultiplayer::rpc_settle_close(int64_t p_txn) {
    rpc_settles.erase(p_txn);
    if (!rpc_disposed) {
        rpc_txns.close(p_txn);
    }
}

Variant NetwMultiplayer::rpc_method_token(
    const Ref<NetwEntity> &p_entity,
    Node *p_node,
    const StringName &p_method
) const {
    const Ref<Script> script = p_node->get_script();
    if (script.is_valid() && !p_entity->comp_table().get_poisoned()) {
        const int64_t known
            = netw::script::model::get_method_id(script, p_method);
        if (known > 0) {
            return known;
        }
    }
    return p_method;
}

LocalVector<call_args::Slot> NetwMultiplayer::rpc_encoded_args(
    const Array &p_args
) const {
    LocalVector<call_args::Slot> out;
    out.reserve(uint32_t(p_args.size()));
    for (int at = 0; at < p_args.size(); ++at) {
        out.push_back(rpc_encoded_arg(p_args[at]));
    }
    return out;
}

call_args::Slot NetwMultiplayer::rpc_encoded_arg(const Variant &p_arg) const {
    Ref<NetwEntity> entity;
    Node *node = nullptr;
    if (Object *held = p_arg.get_type() == Variant::OBJECT
            ? Object::cast_to<Object>(p_arg)
            : nullptr) {
        entity = Ref<NetwEntity>(Object::cast_to<NetwEntity>(held));
        if (entity.is_valid()) {
            node = entity->get_owner();
        } else if (Node *bare = Object::cast_to<Node>(held)) {
            node = bare;
            entity = NetwEntity::of(bare);
        }
    }
    if (entity.is_null()) {
        return call_args::of_value(p_arg);
    }
    const int64_t route = liveness_route_of(entity.ptr());
    if (route <= 0) {
        NETW_ERR_V(
            call_args::of_value(Variant()),
            sys::TRANSPORT,
            "an rpc argument entity has no route"
        );
    }
    return call_args::of_node(
        route,
        entity->comp_of(node),
        entity->comp_path_of(node)
    );
}

void NetwMultiplayer::rpc_call(
    const Callable &p_callable,
    const Array &p_args,
    int64_t p_peer
) {
    Node *node = Object::cast_to<Node>(p_callable.get_object());
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null()) {
        ++rpc_dropped_unroutable;
        return;
    }
    const int64_t route = liveness_route_of(entity.ptr());
    if (route <= 0) {
        ++rpc_dropped_not_live;
        rpc_warn_dropped_once(node);
        return;
    }

    const StringName method = p_callable.get_method();
    const Ref<Script> script = node->get_script();
    const Ref<NetwMemberConfig> options
        = netw::script::model::get_rpc_options(script, method);
    if (options.is_null()
        && (script.is_null()
            || !Dictionary(script->get_rpc_config()).has(method))) {
        NETW_WARN(
            sys::TRANSPORT,
            "method '%s' is not registered or annotated on '%s'",
            String(method),
            String(node->get_name())
        );
        return;
    }

    const Variant method_token = rpc_method_token(entity, node, method);
    const bool reliable = script.is_null()
        || netw::script::model::get_method_reliable(script, method);
    const LocalVector<call_args::Slot> encoded = rpc_encoded_args(p_args);

    const int64_t local_id = get_unique_id();
    if (p_peer == 0 || p_peer == local_id) {
        if (netw::script::model::get_method_call_local(script, method)) {
            const int64_t previous = dispatching_sender;
            dispatching_sender = local_id;
            p_callable.callv(p_args);
            dispatching_sender = previous;
        } else if (p_peer == local_id) {
            NETW_ERR(
                sys::TRANSPORT,
                "a call_remote method '%s' cannot be called locally",
                String(method)
            );
            return;
        }
    }

    const PackedByteArray payload = call_frame(
        p_peer == 0 ? CALL_TARGET_EVERYONE
                    : (p_peer == 1 ? CALL_TARGET_SERVER : CALL_TARGET_PEER),
        0,
        false,
        p_peer,
        method_token,
        encoded,
        options.is_valid() ? options->get_quantizers() : Array(),
        netw::script::model::get_method_arg_types(script, method)
    );

    PackedInt32Array recipients;
    if (p_peer == 0) {
        recipients = rpc_get_recipients(entity);
    } else if (p_peer != local_id) {
        recipients.push_back(int32_t(p_peer));
    }
    const int64_t comp = entity->comp_of(node);
    const String comp_path = entity->comp_path_of(node);
    for (int at = 0; at < recipients.size(); ++at) {
        send_to(
            recipients[at],
            route,
            wire::builtin_channel("CALL"),
            payload,
            reliable,
            comp,
            comp_path,
            false
        );
    }
}

bool NetwMultiplayer::rpc_request_prepare(
    const Callable &p_callable,
    const Array &p_args,
    double p_timeout_seconds,
    RpcRequest &r_out
) {
    Node *node = Object::cast_to<Node>(p_callable.get_object());
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null()) {
        ++rpc_dropped_unroutable;
        return false;
    }
    const int64_t route = liveness_route_of(entity.ptr());
    if (route <= 0) {
        ++rpc_dropped_not_live;
        rpc_warn_dropped_once(node);
        return false;
    }

    const StringName method = p_callable.get_method();
    const Ref<Script> script = node->get_script();
    const Ref<NetwMemberConfig> options
        = netw::script::model::get_rpc_options(script, method);
    if (options.is_null()
        && (script.is_null()
            || !Dictionary(script->get_rpc_config()).has(method))) {
        NETW_WARN(
            sys::TRANSPORT,
            "method '%s' is not registered or annotated on '%s'",
            String(method),
            String(node->get_name())
        );
        return false;
    }

    const bool timed = clock_engine().get_configured();
    const int64_t current = timed ? clock_engine().get_tick() : receive_tick();
    const int64_t tickrate = timed ? clock_engine().get_tickrate() : 30;

    r_out.entity = entity;
    r_out.node = node;
    r_out.route = route;
    r_out.comp = entity->comp_of(node);
    r_out.comp_path = entity->comp_path_of(node);
    r_out.method = method;
    r_out.method_token = rpc_method_token(entity, node, method);
    r_out.encoded_args = rpc_encoded_args(p_args);
    r_out.quantizers = options.is_valid() ? options->get_quantizers() : Array();
    r_out.arg_types = netw::script::model::get_method_arg_types(script, method);
    r_out.deadline = current + int64_t(p_timeout_seconds * double(tickrate));
    return true;
}

void NetwMultiplayer::rpc_open_txn(
    int64_t p_txn,
    const PackedInt64Array &p_addressed,
    const RpcSettle &p_settle,
    int64_t p_deadline
) {
    rpc_txns.open(p_txn, p_addressed, p_deadline);
    rpc_settle_open(p_txn, p_settle);
}

Ref<NetwPromise> NetwMultiplayer::rpc_request_call(
    int64_t p_peer,
    const Callable &p_callable,
    const Array &p_args,
    double p_timeout_seconds
) {
    RpcRequest request;
    if (!rpc_request_prepare(p_callable, p_args, p_timeout_seconds, request)) {
        return Ref<NetwPromise>();
    }

    const int64_t txn = rpc_txns.mint();
    Ref<NetwPromise> promise;
    promise.instantiate();
    PackedInt64Array addressed;
    addressed.push_back(p_peer);
    rpc_open_txn(txn, addressed, promise, request.deadline);

    const PackedByteArray payload = call_frame(
        p_peer == 1 ? CALL_TARGET_SERVER
                    : (p_peer > 1 ? CALL_TARGET_PEER : CALL_TARGET_EVERYONE),
        txn,
        true,
        p_peer,
        request.method_token,
        request.encoded_args,
        request.quantizers,
        request.arg_types
    );

    send_to(
        p_peer,
        request.route,
        wire::builtin_channel("CALL"),
        payload,
        true,
        request.comp,
        request.comp_path,
        false
    );
    return promise;
}

Ref<NetwGroupPromise> NetwMultiplayer::rpc_request_call_group(
    const Callable &p_callable,
    const Array &p_args,
    double p_timeout_seconds
) {
    RpcRequest request;
    if (!rpc_request_prepare(p_callable, p_args, p_timeout_seconds, request)) {
        return Ref<NetwGroupPromise>();
    }

    const PackedInt32Array peers = rpc_get_recipients(request.entity);
    const int64_t txn = rpc_txns.mint();
    const Ref<NetwGroupPromise> promise = NetwGroupPromise::create(peers);
    PackedInt64Array owed;
    for (int at = 0; at < peers.size(); ++at) {
        owed.push_back(peers[at]);
    }
    rpc_open_txn(txn, owed, promise, request.deadline);
    if (peers.is_empty()) {
        session_defer(
            Callable(promise.ptr(), StringName("resolve_all")),
            StringName()
        );
    }

    const PackedByteArray payload = call_frame(
        CALL_TARGET_EVERYONE,
        txn,
        true,
        0,
        request.method_token,
        request.encoded_args,
        request.quantizers,
        request.arg_types
    );

    for (int at = 0; at < peers.size(); ++at) {
        send_to(
            peers[at],
            request.route,
            wire::builtin_channel("CALL"),
            payload,
            true,
            request.comp,
            request.comp_path,
            false
        );
    }
    return promise;
}

void NetwMultiplayer::rpc_send_reply(
    int64_t p_peer,
    int64_t p_route,
    int64_t p_txn,
    const Variant &p_value
) {
    wire::WriteStream stream;
    uint64_t txn = uint64_t(MAX(p_txn, int64_t(0)));
    LocalVector<call_args::Slot> slots;
    slots.push_back(rpc_encoded_arg(p_value));
    if (!stream.varuint(txn, 5)
        || !call_args::write(stream, slots, Array(), Array())
        || !stream.align_verify()) {
        return;
    }
    send_to(
        p_peer,
        p_route,
        wire::builtin_channel("REPLY"),
        stream.to_bytes(),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::rpc_handle_reply(
    int64_t p_txn,
    int64_t p_sender,
    const Variant &p_value
) {
    if (!rpc_txns.admits(p_txn, p_sender)) {
        return;
    }
    const RpcSettle settle = rpc_settle_of(p_txn);
    if (settle.single.is_valid()) {
        settle.single->resolve(p_value);
        rpc_settle_close(p_txn);
        return;
    }
    if (settle.group.is_valid()) {
        settle.group->resolve_peer(p_sender, p_value);
        if (settle.group->get_is_completed()) {
            rpc_settle_close(p_txn);
        }
    }
}

void NetwMultiplayer::rpc_reply_resolved(
    const Variant &p_value,
    int64_t p_peer,
    int64_t p_route,
    int64_t p_txn
) {
    rpc_send_reply(p_peer, p_route, p_txn, p_value);
}

void NetwMultiplayer::rpc_reply_rejected(
    int64_t p_code,
    const String &p_detail,
    int64_t p_peer,
    int64_t p_route,
    int64_t p_txn
) {
    rpc_send_reply(p_peer, p_route, p_txn, Variant());
}

void NetwMultiplayer::rpc_reply_when_settled(
    const Ref<NetwPromise> &p_promise,
    int64_t p_peer,
    int64_t p_route,
    int64_t p_txn
) {
    p_promise->then(callable_mp(this, &NetwMultiplayer::rpc_reply_resolved)
                        .bind(p_peer, p_route, p_txn));
    p_promise->catch_error(
        callable_mp(this, &NetwMultiplayer::rpc_reply_rejected)
            .bind(p_peer, p_route, p_txn)
    );
}

bool NetwMultiplayer::rpc_defer_signal_fired(
    Node *p_node,
    const StringName &p_signal
) {
    if (p_signal == StringName("ready")) {
#if defined(NETW_MODULE)
        return p_node->is_ready();
#else
        return p_node->is_node_ready();
#endif
    }
    return bool(p_node->get_meta(
        StringName(String("_netw_fired_") + String(p_signal)),
        false
    ));
}

bool NetwMultiplayer::rpc_sender_allowed(
    const Ref<NetwMemberConfig> &p_options,
    const Ref<Script> &p_script,
    const StringName &p_method,
    const Ref<NetwEntity> &p_entity,
    Node *p_node,
    int64_t p_sender
) const {
    if (p_sender == 1) {
        return true;
    }
    if (p_options.is_valid() && p_options->get_is_controller_only()) {
        return p_entity.is_valid() && p_sender == p_entity->get_controller();
    }
    const int64_t mode
        = netw::script::model::get_method_rpc_mode(p_script, p_method);
    if (mode == 2) {
        return p_sender == p_node->get_multiplayer_authority();
    }
    return mode == 1;
}

void NetwMultiplayer::rpc_record_interpolated_args(
    Node *p_comp_node,
    const Array &p_args,
    const Array &p_interpolators
) {
    if (p_interpolators.is_empty()) {
        return;
    }
    const int64_t tick = receive_tick();
    for (int at = 0; at < p_interpolators.size(); ++at) {
        const Ref<NetwInterpolate> spec = p_interpolators[at];
        if (spec.is_null() || String(spec->get_target()).is_empty()) {
            continue;
        }
        if (at >= p_args.size()) {
            continue;
        }
        display_record(
            p_comp_node,
            spec->get_target(),
            p_args[at],
            tick,
            spec,
            false
        );
    }
}

void NetwMultiplayer::rpc_execute_call(
    const Ref<NetwEntity> &p_entity,
    Node *p_comp_node,
    const StringName &p_method,
    const Array &p_args,
    int64_t p_txn,
    int64_t p_sender
) {
    const Variant result = p_comp_node->callv(p_method, p_args);
    if (p_txn <= 0) {
        return;
    }
    const int64_t route = p_entity->get_route();
    const Ref<NetwPromise> promise = result;
    if (promise.is_valid()) {
        rpc_reply_when_settled(promise, p_sender, route, p_txn);
        return;
    }
    rpc_send_reply(p_sender, route, p_txn, result);
}

void NetwMultiplayer::rpc_handle_call(
    const Ref<NetwEntity> &p_entity,
    Node *p_comp_node,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    wire::ReadStream reader(p_payload);
    uint64_t flag = 0;
    if (!reader.bits(flag, 8)) {
        return;
    }
    const int64_t target_type = int64_t(flag) & 3;
    const bool awaits_reply = (flag & CALL_AWAITS_REPLY) != 0;

    int64_t txn = 0;
    if (awaits_reply) {
        uint64_t staged = 0;
        if (!reader.varuint(staged, 5)) {
            return;
        }
        txn = int64_t(staged);
    }

    int64_t target_peer = 0;
    if (target_type == CALL_TARGET_SERVER) {
        target_peer = 1;
    } else if (target_type == CALL_TARGET_PEER) {
        uint64_t staged = 0;
        if (!reader.bits(staged, 32)) {
            return;
        }
        target_peer = int64_t(staged);
    }

    const Ref<Script> script = p_comp_node->get_script();
    if (script.is_null()) {
        return;
    }

    Variant token;
    if (!netw::script::model::read_token(reader, token)) {
        return;
    }
    const StringName method = token.get_type() == Variant::INT
        ? netw::script::model::get_method_name_by_id(script, token)
        : StringName(token);
    if (String(method).is_empty()) {
        return;
    }

    const Ref<NetwMemberConfig> options
        = netw::script::model::get_rpc_options(script, method);
    if (options.is_null()
        && !Dictionary(script->get_rpc_config()).has(method)) {
        NETW_WARN(
            sys::TRANSPORT,
            "method '%s' not registered or annotated on '%s'",
            String(method),
            String(p_comp_node->get_name())
        );
        return;
    }

    LocalVector<call_args::Slot> encoded_args;
    const bool decoded = call_args::read(
        reader,
        options.is_valid() ? options->get_quantizers() : Array(),
        netw::script::model::get_method_arg_types(script, method),
        encoded_args
    );
    if (!decoded || !reader.align_verify() || reader.bits_remaining() != 0) {
        NETW_WARN(
            sys::TRANSPORT,
            "a call frame for '%s' did not decode whole and calls nothing",
            String(method)
        );
        return;
    }

    if (!rpc_sender_allowed(
            options,
            script,
            method,
            p_entity,
            p_comp_node,
            p_sender
        )) {
        NETW_WARN(
            sys::TRANSPORT,
            "unauthorized rpc sender %d for '%s' on '%s'",
            p_sender,
            String(method),
            String(p_comp_node->get_name())
        );
        return;
    }

    if (!netw::script::model::validate_argument_count(
            script,
            method,
            encoded_args.size()
        )) {
        NETW_WARN(
            sys::TRANSPORT,
            "arity mismatch for '%s' on '%s'",
            String(method),
            String(p_comp_node->get_name())
        );
        return;
    }

    Array args;
    for (uint32_t at = 0; at < encoded_args.size(); ++at) {
        const call_args::Slot &slot = encoded_args[at];
        if (!slot.addresses_node) {
            args.push_back(slot.value);
            continue;
        }
        const Ref<NetwEntity> arg_entity = wrapper_for_route(slot.node.route);
        if (arg_entity.is_null()
            && liveness_route_state(slot.node.route)
                == NetwLivenessCore::STATE_UNKNOWN) {
            liveness_when_live(
                slot.node.route,
                callable_mp(this, &NetwMultiplayer::rpc_handle_call)
                    .bind(p_entity, p_comp_node, p_payload, p_sender),
                0,
                Callable()
            );
            return;
        }
        if (is_server() && arg_entity.is_valid()
            && !send_admits(p_sender, arg_entity)) {
            if (warn_verdict(ERR_UNAUTHORIZED, slot.node.route)) {
                NETW_WARN(
                    sys::TRANSPORT,
                    "sender %d interest view does not admit route %d",
                    p_sender,
                    slot.node.route
                );
            }
            return;
        }
        Node *arg_node = nullptr;
        if (arg_entity.is_valid()) {
            arg_node = repl_comp_node(
                slot.node.route,
                slot.node.comp,
                slot.node.path
            );
        }
        args.push_back(arg_node);
    }

    rpc_record_interpolated_args(
        p_comp_node,
        args,
        options.is_valid() ? options->get_interpolators() : Array()
    );

    const bool is_server_peer = is_server();
    const int64_t route = p_entity->get_route();

    if (txn > 0 && target_peer != 1 && is_server_peer && p_sender != 1) {
        const Ref<NetwPromise> relayed = rpc_request_call(
            target_peer,
            Callable(p_comp_node, method),
            args
        );
        if (relayed.is_valid()) {
            rpc_reply_when_settled(relayed, p_sender, route, txn);
        }
        return;
    }

    const int64_t comp = p_entity->comp_of(p_comp_node);
    const String comp_path = p_entity->comp_path_of(p_comp_node);
    const bool reliable
        = netw::script::model::get_method_reliable(script, method);

    if (txn == 0 && target_peer != 1 && is_server_peer && p_sender != 1) {
        if (target_peer == 0) {
            PackedInt32Array recipients = rpc_get_recipients(p_entity);
            const int at = recipients.find(int32_t(p_sender));
            if (at >= 0) {
                recipients.remove_at(at);
            }
            for (int i = 0; i < recipients.size(); ++i) {
                send_to(
                    recipients[i],
                    route,
                    wire::builtin_channel("CALL"),
                    p_payload,
                    reliable,
                    comp,
                    comp_path,
                    false
                );
            }
        } else if (send_admits(target_peer, p_entity)) {
            send_to(
                target_peer,
                route,
                wire::builtin_channel("CALL"),
                p_payload,
                reliable,
                comp,
                comp_path,
                false
            );
        }
        if (target_peer != 0) {
            return;
        }
    }

    if (options.is_valid()
        && !String(options->get_defer_signal_name()).is_empty()) {
        const StringName gate = options->get_defer_signal_name();
        if (!rpc_defer_signal_fired(p_comp_node, gate)
            && p_comp_node->has_signal(gate)) {
            rpc_calls_deferred += 1;
            NETW_PLOT(
                profile::names::RPC_CALLS_DEFERRED,
                double(rpc_calls_deferred)
            );
            NETW_TRACE(
                sys::SESSION,
                "'%s' is parked on '%s' rather than run now",
                String(method),
                String(gate)
            );
            p_comp_node->connect(
                gate,
                callable_mp(this, &NetwMultiplayer::rpc_execute_call)
                    .bind(p_entity, p_comp_node, method, args, txn, p_sender),
                Object::CONNECT_ONE_SHOT
            );
            return;
        }
    }

    rpc_execute_call(p_entity, p_comp_node, method, args, txn, p_sender);
}

Error NetwMultiplayer::send_to(
    int64_t p_peer,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    bool p_reliable,
    int64_t p_comp,
    const String &p_path,
    bool p_batched
) {
    NETW_ZONE_NC("session send frame", colors::TRANSPORT);
    NETW_ERR_COND_V(
        p_route < 0,
        ERR_INVALID_PARAMETER,
        sys::TRANSPORT,
        "channel %d addressed peer %d with no route",
        int(p_channel),
        int(p_peer)
    );

    const int64_t subject_route
        = p_route != 0 ? p_route : attribution_subject_route;
    attribution_subject_route = 0;

    const bool connected
        = inner.is_valid() && inner->get_multiplayer_peer().is_valid();
    if (connected && p_peer == get_unique_id()) {
        ReplicationCore *plane = get_replication_plane();
        NETW_ERR_COND_V(
            plane == nullptr,
            ERR_UNCONFIGURED,
            sys::TRANSPORT,
            "channel %d looped back before a dispatcher was installed",
            int(p_channel)
        );
        NETW_TRACE(
            sys::TRANSPORT,
            "loopback route=%d channel=%d",
            int(p_route),
            int(p_channel)
        );
        plane->dispatch(
            p_route,
            p_comp,
            p_channel,
            p_payload,
            p_path,
            p_peer,
            p_reliable,
            -1
        );
        return OK;
    }

    const PackedByteArray framed = wire::frame_pack(
        p_route,
        uint8_t(p_comp),
        uint8_t(p_channel),
        p_payload,
        p_path
    );

    wire::Attribution outgoing;
    outgoing.peer = p_peer;
    outgoing.channel = p_channel;
    outgoing.route = subject_route;
    outgoing.comp = p_comp;
    outgoing.bits = int64_t(framed.size()) * 8;

    const bool aggregating = channel_aggregates(p_channel, p_batched);
    if (!aggregating) {
        if (p_reliable) {
            const PackedByteArray pending = carrier.take(p_peer, true);
            if (!pending.is_empty()) {
                send_datagram(p_peer, pending, true, true);
            }
        }
        attribution.stage(p_reliable, false, outgoing);
        send_datagram(p_peer, framed, p_reliable, false);
        return OK;
    }

    const int64_t seq = carrier_append(p_peer, framed, p_reliable);
    attribution.stage(p_reliable, true, outgoing);
    if (seq < 0) {
        return OK;
    }
    ReplicationCore *plane = get_replication_plane();
    NETW_ERR_COND_V(
        plane == nullptr,
        ERR_UNCONFIGURED,
        sys::TRANSPORT,
        "peer %d was staged at seq %d with no dispatcher to tell",
        int(p_peer),
        int(seq)
    );
    plane->note_staged(p_peer, seq);
    return OK;
}

int64_t NetwMultiplayer::send_datagram(
    int64_t p_peer,
    const PackedByteArray &p_payload,
    bool p_reliable,
    bool p_carrier
) {
    if (p_payload.is_empty() || inner.is_null()) {
        attribution.discard(p_peer, p_reliable, p_carrier);
        return -1;
    }

    if (p_peer != 0 && reachable_peer_ids().find(int32_t(p_peer)) < 0) {
        attribution.discard(p_peer, p_reliable, p_carrier);
        return -1;
    }

    const NetwCarrierDatagram datagram
        = frame_datagram(p_peer, p_payload, p_reliable, p_carrier);
    const Error delivered = inner->send_bytes(
        datagram.bytes,
        int(p_peer),
        p_reliable ? MultiplayerPeer::TRANSFER_MODE_RELIABLE
                   : MultiplayerPeer::TRANSFER_MODE_UNRELIABLE,
        0
    );
    if (delivered != OK && p_peer != 0) {
        if (session_link_is_connected()) {
            peer_mark_unreachable(p_peer);
        }
        attribution.discard(p_peer, p_reliable, p_carrier);
        return -1;
    }
    attribution.note_datagram(false, int64_t(datagram.bytes.size()));
    capture_note(wire::CaptureDirection::OUT, p_peer, datagram.bytes);
    if (plane.wants(EventPlane::DATAGRAM_SENT, 0)) {
        EventPlane::Emission fact(
            EventPlane::DATAGRAM_SENT,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.peer = p_peer;
        Dictionary detail;
        detail["size"] = datagram.bytes.size();
        detail["seq"] = datagram.seq;
        detail["reliable"] = p_reliable;
        fact.detail = detail;
        plane.emit(fact);
    }
    return datagram.seq;
}

int64_t NetwMultiplayer::carrier_append(
    int64_t p_peer,
    const PackedByteArray &p_frame,
    bool p_reliable
) {
    const PackedByteArray owed = carrier.append(
        p_peer,
        p_frame,
        p_reliable,
        p_reliable ? 0 : datagram_budget()
    );
    if (owed.is_empty()) {
        return -1;
    }
    return send_datagram(p_peer, owed, p_reliable);
}

PackedInt64Array NetwMultiplayer::carrier_flush() {
    PackedInt64Array staged;
    for (const int32_t peer : carrier.peers(false)) {
        const int64_t seq
            = send_datagram(peer, carrier.take(peer, false), false);
        if (seq >= 0) {
            staged.push_back(peer);
            staged.push_back(seq);
        }
    }
    for (const int32_t peer : carrier.peers(true)) {
        send_datagram(peer, carrier.take(peer, true), true);
    }
    return staged;
}

void NetwMultiplayer::carrier_clear() {
    for (const int32_t peer : carrier.peers(false)) {
        attribution.discard(peer, false, true);
    }
    for (const int32_t peer : carrier.peers(true)) {
        attribution.discard(peer, true, true);
    }
    carrier.clear();
}

int64_t NetwMultiplayer::carrier_pending(
    int64_t p_peer,
    bool p_reliable
) const {
    return carrier.pending(p_peer, p_reliable);
}

NetwCarrierDatagram NetwMultiplayer::frame_datagram(
    int64_t p_peer,
    const PackedByteArray &p_payload,
    bool p_reliable,
    bool p_carrier
) {
    NetwCarrierDatagram out;
    count_sent(p_payload.size());

    int64_t seq = -1;
    int64_t ack = -1;
    uint32_t history = 0;
    if (!p_reliable) {
        seq = next_send_seq(p_peer);
        if (has_inbound_seq(p_peer)) {
            ack = inbound_seq(p_peer);
            history = inbound_delivery_history(p_peer);
            count_state_ack_out();
            note_echoed_seq(p_peer, ack);
        }
    }
    attribution.commit(p_peer, p_reliable, p_carrier, seq);
    out.bytes = NetwCarrierFrame::build(
        p_payload,
        p_reliable,
        seq,
        ack,
        history,
        datagram_base_tick()
    );
    out.seq = seq;
    return out;
}

int64_t NetwMultiplayer::datagram_base_tick() const {
    return int64_t(clock_engine().get_tick());
}

int64_t NetwMultiplayer::next_send_seq(int64_t p_peer) {
    return seq_book.next_send_seq(p_peer);
}

bool NetwMultiplayer::has_inbound_seq(int64_t p_peer) const {
    return seq_book.has_inbound(p_peer);
}

int64_t NetwMultiplayer::inbound_seq(int64_t p_peer) const {
    return seq_book.inbound_seq(p_peer);
}

uint32_t NetwMultiplayer::inbound_delivery_history(int64_t p_peer) const {
    return seq_book.inbound_delivery_history(p_peer);
}

uint32_t NetwMultiplayer::peer_ack_history(int64_t p_peer) const {
    return seq_book.peer_ack_history(p_peer);
}

bool NetwMultiplayer::note_inbound_seq(int64_t p_peer, int64_t p_seq) {
    return seq_book.note_inbound(p_peer, uint16_t(p_seq));
}

bool NetwMultiplayer::note_peer_ack(
    int64_t p_peer,
    int64_t p_ack,
    uint32_t p_history
) {
    const bool advanced
        = seq_book.note_peer_ack(p_peer, uint16_t(p_ack), p_history);
    if (advanced && plane.wants(EventPlane::ACK_ADVANCED, 0)) {
        EventPlane::Emission fact(
            EventPlane::ACK_ADVANCED,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.peer = p_peer;
        Dictionary detail;
        detail["ack"] = p_ack;
        fact.detail = detail;
        plane.emit(fact);
    }
    return advanced;
}

void NetwMultiplayer::sync_note_ack_default(
    int64_t p_peer,
    int64_t p_sequence
) {
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        NETW_TRACE(
            sys::SESSION,
            "a receive-side ack was recorded with no replication plane"
        );
        return;
    }
    SyncPipeline *pipeline = plane->get_sync_pipeline();
    if (pipeline == nullptr) {
        NETW_TRACE(sys::SESSION, "a receive-side ack found no sync pipeline");
        return;
    }
    pipeline->note_peer_ack(p_peer, p_sequence, peer_ack_history(p_peer));
}

Object *NetwMultiplayer::session_seam(const StringName &p_seam) const {
    Object *published = session_api();
    if (published == nullptr || published == this
        || !published->has_method(p_seam)) {
        return nullptr;
    }
    return published;
}

void NetwMultiplayer::session_note_state_ack(
    int64_t p_peer,
    int64_t p_ack,
    uint32_t p_history
) {
    if (!note_peer_ack(p_peer, p_ack, p_history)) {
        return;
    }
    Object *seam = session_seam(StringName("_sync_note_ack"));
    if (seam != nullptr) {
        seam->call("_sync_note_ack", p_peer, p_ack);
        return;
    }
    note_ack(p_peer, p_ack);
}

Error NetwMultiplayer::session_receive_inner_packet(
    int64_t p_sender,
    const PackedByteArray &p_packet
) {
    const NetwCarrierFrame header = receive_header(p_sender, p_packet);
    if (header.kind == NetwCarrierFrame::FOREIGN) {
        return OK;
    }
    if (header.kind == NetwCarrierFrame::MALFORMED) {
        return ERR_INVALID_DATA;
    }
    const PackedByteArray framed = p_packet.slice(int(header.payload_offset));
    if (header.kind == NetwCarrierFrame::RELIABLE) {
        return receive_carrier(framed, p_sender, true, -1, header.tick);
    }
    if (header.kind == NetwCarrierFrame::UNRELIABLE_ACKED) {
        count_state_ack_in();
        session_note_state_ack(p_sender, header.ack, header.history);
    }
    note_inbound_seq(p_sender, header.seq);
    return receive_carrier(framed, p_sender, false, header.seq, header.tick);
}

void NetwMultiplayer::session_on_inner_packet(
    int64_t p_sender,
    const PackedByteArray &p_packet
) {
    sink_verdict(session_receive_inner_packet(p_sender, p_packet), 0);
}

void NetwMultiplayer::note_ack(int64_t p_peer, int64_t p_sequence) {
    if (GDVIRTUAL_CALL(_sync_note_ack, p_peer, p_sequence)) {
        return;
    }
    sync_note_ack_default(p_peer, p_sequence);
}

void NetwMultiplayer::note_sent(int64_t p_peer, int64_t p_sequence) {
    if (GDVIRTUAL_CALL(_sync_note_sent, p_peer, p_sequence)) {
        return;
    }
    sync_note_sent_default(p_peer, p_sequence);
}

void NetwMultiplayer::sync_note_sent_default(
    int64_t p_peer,
    int64_t p_sequence
) {
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        NETW_TRACE(
            sys::SESSION,
            "a send-side ack was recorded with no replication plane"
        );
        return;
    }
    SyncPipeline *pipeline = plane->get_sync_pipeline();
    if (pipeline == nullptr) {
        NETW_TRACE(sys::SESSION, "a send-side ack found no sync pipeline");
        return;
    }
    pipeline->commit_pending_masked(p_peer, p_sequence);
}

void NetwMultiplayer::note_echoed_seq(int64_t p_peer, int64_t p_seq) {
    seq_book.note_echoed(p_peer, uint16_t(p_seq));
}

PackedInt64Array NetwMultiplayer::peers_owed_echo() const {
    return seq_book.peers_owed_echo();
}

void NetwMultiplayer::forget_peer_seqs(int64_t p_peer) {
    seq_book.forget_peer(p_peer);
}

bool NetwMultiplayer::count_verdict(Error p_verdict, int64_t p_route) {
    const bool counted = verdict_book.count(p_verdict);
    if (plane.wants(EventPlane::VERDICT, p_route)) {
        EventPlane::Emission fact(
            EventPlane::VERDICT,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.route = p_route;
        fact.verdict = p_verdict;
        Dictionary detail;
        detail["stage"] = "gate";
        detail["counted"] = counted;
        fact.detail = detail;
        plane.emit(fact);
    }
    return counted;
}

void NetwMultiplayer::send_standalone_ack(int64_t p_peer, int64_t p_ack) {
    const int64_t seq = next_send_seq(p_peer);
    const PackedByteArray framed = NetwCarrierFrame::build(
        PackedByteArray(),
        false,
        seq,
        p_ack,
        inbound_delivery_history(p_peer),
        datagram_base_tick()
    );
    note_echoed_seq(p_peer, p_ack);
    count_standalone_ack_out();
    count_sent(framed.size());
    attribution.note_framing_out(framed.size());
    attribution.note_datagram(false, int64_t(framed.size()));
    capture_note(wire::CaptureDirection::OUT, p_peer, framed);
    const Error delivered = inner->send_bytes(
        framed,
        int32_t(p_peer),
        MultiplayerPeer::TRANSFER_MODE_UNRELIABLE,
        0
    );
    if (delivered != OK && session_link_is_connected()) {
        peer_mark_unreachable(p_peer);
    }
}

void NetwMultiplayer::flush_standalone_acks() {
    if (inner.is_null() || inner->get_multiplayer_peer().is_null()) {
        return;
    }
    const PackedInt32Array live = reachable_peer_ids();
    const PackedInt64Array owed = peers_owed_echo();
    for (int at = 0; at < owed.size(); at++) {
        const int64_t peer = owed[at];
        if (peer != 0 && !live.has(int32_t(peer))) {
            continue;
        }
        send_standalone_ack(peer, inbound_seq(peer));
    }
}

Error NetwMultiplayer::send_bytes(
    const PackedByteArray &p_bytes,
    int64_t p_peer,
    MultiplayerPeer::TransferMode p_mode,
    int p_channel
) {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->send_bytes(p_bytes, int32_t(p_peer), p_mode, p_channel);
}

Error NetwMultiplayer::channel_send(
    int64_t p_peer,
    const RID &p_entity,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    bool p_reliable
) {
    if (p_channel < CHANNEL_USER_FIRST || p_channel > CHANNEL_USER_LAST) {
        return ERR_INVALID_DATA;
    }
    const int64_t route = entity_get_route(p_entity);
    if (route <= 0) {
        return ERR_DOES_NOT_EXIST;
    }
    return send_to(
        p_peer,
        route,
        p_channel,
        p_payload,
        p_reliable,
        0,
        String(),
        false
    );
}

} // namespace netw
