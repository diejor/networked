#include "netw/api/replication_core.hpp"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/call_args.hpp"
#include "netw/entity/control.hpp"
#include "netw/liveness_core.hpp"
#include "netw/session/frames.hpp"
#include "netw/spawn/record.hpp"
#include "netw/sync_kernel.hpp"
#include "netw/table/core.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;

namespace netw {

namespace {

const int64_t USER_CHANNEL_FIRST = 100;
const int64_t USER_CHANNEL_LAST = 254;

int64_t id_of(const wire::WireRegistry &p_registry, const char *p_name) {
    const wire::ChannelDecl *decl
        = p_registry.find_channel_by_name(StringName(p_name));
    return decl != nullptr ? int64_t(decl->id) : 0;
}

} // namespace

ReplicationCore::ReplicationCore() {
    resolve_channel_ids();
}

void ReplicationCore::resolve_channel_ids() {
    const wire::WireRegistry registry = wire::WireRegistry::create_default();
    ids.table = id_of(registry, "TABLE");
    ids.spawn = id_of(registry, "SPAWN");
    ids.despawn = id_of(registry, "DESPAWN");
    ids.hide = id_of(registry, "HIDE");
    ids.reparent = id_of(registry, "REPARENT");
    ids.call = id_of(registry, "CALL");
    ids.reply = id_of(registry, "REPLY");
    ids.control_request = id_of(registry, "CONTROL_REQUEST");
    ids.control_apply = id_of(registry, "CONTROL_APPLY");
    ids.property_sync = id_of(registry, "PROPERTY_SYNC");
    ids.signal = id_of(registry, "SIGNAL");
    ids.sync = id_of(registry, "SYNC");
    ids.sync_delta = id_of(registry, "SYNC_DELTA");
    ids.sync_row = id_of(registry, "SYNC_ROW");
    ids.sync_row_delta = id_of(registry, "SYNC_ROW_DELTA");
    ids.sync_row_window = id_of(registry, "SYNC_ROW_WINDOW");
    ids.predict_command = id_of(registry, "PREDICT_COMMAND");
    ids.predict_ack = id_of(registry, "PREDICT_ACK");
    ids.predict_relay = id_of(registry, "PREDICT_RELAY");
    ids.predict_relay_request = id_of(registry, "PREDICT_RELAY_REQUEST");
    ids.action = id_of(registry, "ACTION");
    ids.clock_handshake = id_of(registry, "CLOCK_HANDSHAKE");
    ids.clock_handshake_reply = id_of(registry, "CLOCK_HANDSHAKE_REPLY");
    ids.clock_ping = id_of(registry, "CLOCK_PING");
    ids.clock_pong = id_of(registry, "CLOCK_PONG");
    ids.interest_awareness = id_of(registry, "INTEREST_AWARENESS");
    ids.lagcomp_deny = id_of(registry, "LAGCOMP_DENY");
}

NetwMultiplayer *ReplicationCore::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

Object *ReplicationCore::api() const {
    return gd::instance_from_id(api_id);
}

Object *ReplicationCore::gate_seam(const StringName &p_seam) const {
    Object *seam = api();
    if (seam == nullptr || seam == core() || !seam->has_method(p_seam)) {
        return nullptr;
    }
    return seam;
}

Object *ReplicationCore::table_core() const {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return nullptr;
    }
    return plane->get_table_core().ptr();
}

void ReplicationCore::install(Object *p_api) {
    api_id = gd::instance_id(p_api);
    if (p_api == nullptr) {
        return;
    }
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(p_api);
    core_id = gd::instance_id(plane);
    if (plane == nullptr) {
        return;
    }
    channels = plane->get_channel_book();

    sync_pipeline.set_core(plane);
    sync_pipeline.set_api(p_api);
    sync_pipeline.set_sync_model(&sync_model);
    sync_pipeline.set_channels(
        ids.sync_row,
        ids.sync_row_window,
        ids.sync_row_delta,
        ids.property_sync,
        ids.signal
    );
    sync_pipeline.set_stage_seams(
        callable_mp(plane, &NetwMultiplayer::run_sync_encode_stage),
        callable_mp(plane, &NetwMultiplayer::run_sync_decode_stage)
    );

    spawn_pipeline.set_core(plane);
    spawn_pipeline.set_api(p_api);
    spawn_pipeline.set_channels(ids.spawn, ids.despawn, ids.hide, ids.reparent);
    spawn_pipeline.set_encode_seams(
        callable_mp(plane, &NetwMultiplayer::sync_pipeline_encode_prop_val)
    );
    spawn_pipeline.set_derived_seams(
        callable_mp(
            plane,
            &NetwMultiplayer::sync_pipeline_encode_derived_descriptors
        ),
        callable_mp(plane, &NetwMultiplayer::sync_pipeline_note_derived_schema)
    );
    spawn_pipeline.set_repl_seams(
        callable_mp(plane, &NetwMultiplayer::replication_flush_all_buffers),
        callable_mp(plane, &NetwMultiplayer::replication_resolve_comp_node)
    );

    spawner_compat.set_core(plane);
    spawner_compat.set_spawn_seams(
        callable_mp(plane, &NetwMultiplayer::spawn_books_node),
        callable_mp(plane, &NetwMultiplayer::spawn_arm_consumed)
    );

    sync_compat.set_core(plane);
    sync_compat.set_sync_model(&sync_model);
    sync_compat.set_spawn_book(spawn_pipeline.get_spawn_book());
    sync_compat.set_stage_seams(
        callable_mp(plane, &NetwMultiplayer::run_sync_encode_stage),
        callable_mp(plane, &NetwMultiplayer::run_sync_decode_stage)
    );
    sync_compat.set_spawn_seams(
        callable_mp(plane, &NetwMultiplayer::replication_adopt_in_place),
        callable_mp(plane, &NetwMultiplayer::spawn_schedule_visibility_sweep)
    );
    sync_compat.set_channels(ids.sync, ids.sync_delta);

    spawn_pipeline.set_adapters(&spawner_compat, &sync_compat);

    register_protocol(
        ids.table,
        callable_mp(plane, &NetwMultiplayer::replication_handle_table_frame)
    );
    register_protocol(
        ids.spawn,
        callable_mp(plane, &NetwMultiplayer::spawn_handle_spawn_frame)
    );
    register_protocol(
        ids.despawn,
        callable_mp(plane, &NetwMultiplayer::spawn_handle_despawn_frame)
    );
    register_protocol(
        ids.hide,
        callable_mp(plane, &NetwMultiplayer::spawn_handle_hide_frame)
    );
    register_protocol(
        ids.reparent,
        callable_mp(plane, &NetwMultiplayer::spawn_handle_reparent_frame)
    );
    register_protocol(
        ids.clock_handshake,
        callable_mp(plane, &NetwMultiplayer::clock_receive_handshake)
    );
    register_protocol(
        ids.clock_handshake_reply,
        callable_mp(plane, &NetwMultiplayer::clock_receive_handshake_reply)
    );
    register_protocol(
        ids.clock_ping,
        callable_mp(plane, &NetwMultiplayer::clock_receive_ping)
    );
    register_protocol(
        ids.clock_pong,
        callable_mp(plane, &NetwMultiplayer::clock_receive_pong)
    );
    register_protocol(
        ids.interest_awareness,
        callable_mp(plane, &NetwMultiplayer::interest_receive_awareness)
    );
    register_protocol(
        ids.lagcomp_deny,
        callable_mp(plane, &NetwMultiplayer::handle_deny)
    );
}

void ReplicationCore::set_tap_seam(const Callable &p_tap) {
    sync_pipeline.set_tap_seam(p_tap);
}

bool ReplicationCore::get_is_applying_remote_frame() const {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? plane->spawn_applying_remote_frame() : false;
}

void ReplicationCore::register_channel(
    int64_t p_channel,
    const Callable &p_handler,
    bool p_defer_when_unknown
) {
    if (channels != nullptr) {
        channels->register_channel(p_channel, p_handler, p_defer_when_unknown);
    }
}

void ReplicationCore::register_protocol(
    int64_t p_channel,
    const Callable &p_handler
) {
    if (channels != nullptr) {
        channels->register_protocol(p_channel, p_handler);
    }
}

Error ReplicationCore::settle_channels() {
    return channels != nullptr ? channels->settle_protocol() : ERR_UNCONFIGURED;
}

Ref<NetwPropertySetBinding> ReplicationCore::derived_binding(
    Node *p_node,
    int64_t p_record
) {
    return sync_pipeline.derived_binding(p_node, p_record);
}

TypedArray<NetwPropertySetBinding> ReplicationCore::derived_group(
    int64_t p_route
) {
    return sync_pipeline.derived_group(p_route);
}

void ReplicationCore::note_peer_ack(
    int64_t p_peer_id,
    int64_t p_acked_seq,
    uint32_t p_history
) {
    sync_pipeline.note_peer_ack(p_peer_id, p_acked_seq, p_history);
}

void ReplicationCore::send_to(
    int64_t p_peer_id,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    bool p_reliable,
    int64_t p_comp,
    const String &p_path,
    bool p_batched
) {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->send_to(
            p_peer_id,
            p_route,
            p_channel,
            p_payload,
            p_reliable,
            p_comp,
            p_path,
            p_batched
        );
    }
}

void ReplicationCore::broadcast_control(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || plane->session_get_inner().is_null()
        || plane->session_get_inner()->get_multiplayer_peer().is_null()) {
        return;
    }
    session::ControlApply applied;
    applied.controller = uint64_t(MAX(p_peer, int64_t(0)));
    const PackedByteArray payload = session::frame_write(applied);
    const PackedInt32Array recipients = plane->rpc_get_recipients(p_entity);
    for (int at = 0; at < recipients.size(); ++at) {
        send_to(
            recipients[at],
            p_entity->get_route(),
            ids.control_apply,
            payload,
            true,
            0,
            String(),
            false
        );
    }
}

bool ReplicationCore::is_live_for(
    int64_t p_peer_id,
    const Ref<NetwEntity> &p_entity
) {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? plane->send_admits(p_peer_id, p_entity) : true;
}

bool ReplicationCore::policy_admits(
    int64_t p_policy,
    int64_t p_sender,
    Node *p_node,
    const Ref<NetwEntity> &p_entity
) {
    Ref<NetwEntity> resolved = p_entity;
    if (resolved.is_null()) {
        resolved = NetwEntity::of(p_node);
    }
    return entity::Control::policy_admits(
        p_policy,
        p_sender,
        p_node != nullptr ? p_node->get_multiplayer_authority() : 0,
        resolved.is_valid() ? resolved->get_controller() : 0
    );
}

PackedInt32Array ReplicationCore::live_peers(const Ref<NetwEntity> &p_entity) {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? plane->rpc_get_recipients(p_entity)
                            : PackedInt32Array();
}

void ReplicationCore::request_control(const Ref<NetwEntity> &p_entity) {
    send_to(
        int64_t(MultiplayerPeer::TARGET_PEER_SERVER),
        p_entity->get_route(),
        ids.control_request,
        PackedByteArray(),
        true,
        0,
        String(),
        false
    );
}

void ReplicationCore::flush_all_buffers() {
    NetwMultiplayer *plane = core();
    Object *shell = api();
    if (plane == nullptr || shell == nullptr) {
        return;
    }
    const PackedInt64Array staged = plane->carrier_flush();
    Object *seam = gate_seam(StringName("_sync_note_sent"));
    for (int at = 0; at + 1 < staged.size(); at += 2) {
        if (seam != nullptr) {
            seam->call("_sync_note_sent", staged[at], staged[at + 1]);
            continue;
        }
        plane->note_sent(staged[at], staged[at + 1]);
    }
}

void ReplicationCore::note_staged(int64_t p_peer_id, int64_t p_seq) {
    Object *seam = gate_seam(StringName("_sync_note_sent"));
    if (seam != nullptr) {
        seam->call("_sync_note_sent", p_peer_id, p_seq);
        return;
    }
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->note_sent(p_peer_id, p_seq);
    }
}

Error ReplicationCore::receive_carrier(
    const PackedByteArray &p_framed,
    int64_t p_sender,
    bool p_reliable,
    int64_t p_seq,
    int64_t p_base_tick
) {
    if (p_framed.is_empty()) {
        return OK;
    }
    sync_pipeline.open_datagram(p_base_tick, p_reliable ? -1 : p_seq);
    wire::ReadStream reader(p_framed);
    Error result = OK;
    int64_t frames = 0;
    while (reader.bits_remaining() > 0) {
        wire::Frame frame;
        if (!wire::frame_unpack_next(reader, frame)) {
            return finish_receive_pass(frames, ERR_INVALID_DATA, p_sender);
        }
        frames += 1;
        const Error verdict = dispatch(
            frame.route,
            int64_t(frame.comp),
            int64_t(frame.channel),
            frame.payload,
            frame.path,
            p_sender,
            p_reliable,
            p_seq
        );
        if (result == OK && verdict != OK) {
            result = verdict;
        }
    }
    return finish_receive_pass(frames, result, p_sender);
}

Error ReplicationCore::finish_receive_pass(
    int64_t p_frames,
    Error p_verdict,
    int64_t p_sender
) {
    NetwMultiplayer *plane = core();
    if (plane != nullptr && plane->event_wants(EventPlane::APPLY, 0)) {
        Dictionary detail;
        detail[StringName("frames")] = p_frames;
        plane->event_emit(
            EventPlane::APPLY,
            0,
            detail,
            StringName(),
            p_sender,
            p_verdict,
            Dictionary()
        );
    }
    return p_verdict;
}

Error ReplicationCore::dispatch(
    int64_t p_route,
    int64_t p_comp,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    const String &p_path,
    int64_t p_sender,
    bool p_reliable,
    int64_t p_seq
) {
    NetwMultiplayer *plane = core();
    Object *shell = api();
    if (plane == nullptr || shell == nullptr) {
        return ERR_UNAVAILABLE;
    }
    if (p_sender != plane->get_unique_id()) {
        plane->attribution_observe_in(
            p_sender,
            p_channel,
            p_route,
            p_comp,
            p_payload.size()
        );
    }
    if (p_route != 0) {
        const Error admission = admit_entity_frame(
            p_sender,
            p_route,
            p_comp,
            p_channel,
            p_payload
        );
        if (admission != OK) {
            plane->repl_note_gate_verdict(admission);
            plane->attribution_note_refusal(
                p_sender,
                p_channel,
                wire::Refusal::GATE
            );
            return admission;
        }
    }
    const int64_t previous = plane->rpc_get_relay_sender();
    plane->set_relay_sender(p_sender);
    dispatch_frame(
        p_route,
        p_comp,
        p_channel,
        p_payload,
        p_path,
        p_sender,
        p_reliable,
        p_seq
    );
    plane->set_relay_sender(previous);
    return OK;
}

void ReplicationCore::settle_reply(
    int64_t p_txn,
    int64_t p_sender,
    int64_t p_route,
    int64_t p_comp,
    const String &p_path
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = plane->wrapper_for_route(p_route);
    Node *node = entity.is_valid() ? resolve_comp_node(entity, p_comp, p_path)
                                   : nullptr;
    plane->rpc_handle_reply(p_txn, p_sender, node);
}

void ReplicationCore::dispatch_reply(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NetwMultiplayer *plane = core();
    Object *shell = api();
    if (plane == nullptr || shell == nullptr) {
        return;
    }
    wire::ReadStream reader(p_payload);
    uint64_t staged = 0;
    LocalVector<call_args::Slot> slots;
    if (!reader.varuint(staged, 5)
        || !call_args::read(reader, Array(), Array(), slots)
        || !reader.align_verify() || reader.bits_remaining() != 0) {
        return;
    }
    const int64_t txn = int64_t(staged);
    if (slots.is_empty() || !slots[0].addresses_node) {
        plane->rpc_handle_reply(
            txn,
            p_sender,
            slots.is_empty() ? Variant() : slots[0].value
        );
        return;
    }
    const call_args::NodeRef &node = slots[0].node;
    if (node.route > 0
        && plane->liveness_route_state(node.route)
            == NetwLivenessCore::STATE_UNKNOWN) {
        plane->liveness_when_live(
            node.route,
            callable_mp(core(), &NetwMultiplayer::replication_settle_reply)
                .bind(txn, p_sender, node.route, node.comp, node.path),
            0,
            Callable()
        );
        return;
    }
    settle_reply(txn, p_sender, node.route, node.comp, node.path);
}

void ReplicationCore::dispatch_frame(
    int64_t p_route,
    int64_t p_comp,
    int64_t p_channel,
    const PackedByteArray &p_payload,
    const String &p_path,
    int64_t p_sender,
    bool p_reliable,
    int64_t p_seq
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || channels == nullptr) {
        return;
    }

    if (p_route == 0) {
        const Callable protocol = channels->protocol_handler_of(p_channel);
        if (protocol.is_valid()) {
            protocol.call(p_payload, p_sender);
        } else if (
            p_channel >= USER_CHANNEL_FIRST && p_channel <= USER_CHANNEL_LAST
        ) {
            const Callable handler = channels->handler_of(p_channel);
            if (handler.is_valid()) {
                handler.call(Variant(), p_payload, p_sender);
            }
        }
        return;
    }

    if (p_seq >= 0
        && !sync_pipeline
                .accept_unreliable(p_sender, p_route, p_channel, p_seq)) {
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::STALE
        );
        return;
    }

    if (p_channel == ids.reply) {
        dispatch_reply(p_payload, p_sender);
        return;
    }

    if (plane->liveness_route_state(p_route)
        == NetwLivenessCore::STATE_UNKNOWN) {
        if (defers_unknown_route(p_channel, p_reliable)) {
            plane->rpc_defer_call(
                p_sender,
                p_route,
                callable_mp(core(), &NetwMultiplayer::replication_dispatch)
                    .bind(
                        p_route,
                        p_comp,
                        p_channel,
                        p_payload,
                        p_path,
                        p_sender,
                        p_reliable,
                        int64_t(-1)
                    )
            );
            return;
        }
        plane->repl_note_unknown_route();
        plane->attribution_note_refusal(
            p_sender,
            p_channel,
            wire::Refusal::UNROUTED
        );
        return;
    }

    if (channel_addresses_a_set(p_channel)) {
        const Ref<NetwEntity> set_owner = plane->wrapper_for_route(p_route);
        if (set_owner.is_null()
            || plane->repl_comp_node(p_route, 0, String()) == nullptr) {
            plane->attribution_note_refusal(
                p_sender,
                p_channel,
                wire::Refusal::UNBOUND
            );
            return;
        }
        if (p_channel == ids.sync_row) {
            sync_pipeline.handle_derived_row(
                set_owner,
                p_comp,
                p_payload,
                p_sender,
                p_channel
            );
        } else if (p_channel == ids.sync_row_delta) {
            sync_pipeline.handle_retained_row(
                set_owner,
                p_comp,
                p_payload,
                p_sender,
                p_channel
            );
        } else {
            sync_pipeline.handle_window_row(
                set_owner,
                p_comp,
                p_payload,
                p_sender,
                p_channel
            );
        }
        return;
    }

    Node *comp_node = plane->repl_comp_node(p_route, p_comp, p_path);
    if (comp_node == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = plane->wrapper_for_route(p_route);

    if (p_channel == ids.call) {
        plane->rpc_handle_call(entity, comp_node, p_payload, p_sender);
    } else if (p_channel == ids.control_request) {
        if (entity.is_valid() && p_payload.is_empty()) {
            entity->_handle_control_request(p_sender);
        }
    } else if (p_channel == ids.control_apply) {
        session::ControlApply applied;
        if (entity.is_valid() && session::frame_read(p_payload, applied)) {
            entity->_handle_control_apply(int64_t(applied.controller));
        }
    } else if (p_channel == ids.property_sync) {
        sync_pipeline.handle_property_sync(
            entity,
            comp_node,
            p_payload,
            p_sender,
            p_comp
        );
    } else if (p_channel == ids.signal) {
        sync_pipeline.handle_signal(entity, comp_node, p_payload, p_sender);
    } else if (p_channel == ids.sync) {
        sync_compat.handle_sync(entity, p_payload, p_sender);
    } else if (p_channel == ids.sync_delta) {
        sync_compat.handle_sync_delta(entity, p_payload, p_sender);
    } else if (
        p_channel == ids.predict_command || p_channel == ids.predict_ack
        || p_channel == ids.predict_relay
        || p_channel == ids.predict_relay_request || p_channel == ids.action
        || (p_channel >= USER_CHANNEL_FIRST && p_channel <= USER_CHANNEL_LAST)
    ) {
        const Callable handler = channels->handler_of(p_channel);
        if (handler.is_valid()) {
            handler.call(entity, p_payload, p_sender);
        }
    }
}

bool ReplicationCore::channel_addresses_a_set(int64_t p_channel) const {
    return p_channel == ids.sync_row || p_channel == ids.sync_row_delta
        || p_channel == ids.sync_row_window;
}

bool ReplicationCore::defers_unknown_route(
    int64_t p_channel,
    bool p_reliable
) const {
    if (p_channel == ids.call) {
        return p_reliable;
    }
    return channels != nullptr ? channels->defers(p_channel) : false;
}

Error ReplicationCore::admit_entity_frame(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_comp,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return ERR_UNAVAILABLE;
    }
    Object *seam = gate_seam(StringName("_sync_admit_frame"));
    int64_t gate = -1;
    Error verdict = OK;
    if (p_channel == ids.sync) {
        gate = EventPlane::GATE_SYNC;
        wire::ReadStream reader(p_payload);
        sync_kernel::VolatileHead head;
        const int64_t flags = sync_kernel::VolatileHead::wire.run(reader, head)
            ? int64_t(head.flags)
            : int64_t(-1);
        verdict = seam != nullptr ? Error(
                                        int(seam->call(
                                            "_sync_admit_frame",
                                            p_sender,
                                            p_route,
                                            p_comp,
                                            p_channel,
                                            flags,
                                            -1,
                                            p_payload
                                        ))
                                    )
                                  : plane->sync_admit_frame(
                                        p_sender,
                                        p_route,
                                        p_comp,
                                        p_channel,
                                        flags,
                                        -1,
                                        p_payload
                                    );
    } else if (
        p_channel == ids.sync_row || p_channel == ids.sync_row_delta
        || p_channel == ids.sync_row_window || p_channel == ids.sync_delta
    ) {
        gate = EventPlane::GATE_SYNC;
        verdict = seam != nullptr ? Error(
                                        int(seam->call(
                                            "_sync_admit_frame",
                                            p_sender,
                                            p_route,
                                            p_comp,
                                            p_channel,
                                            0,
                                            -1,
                                            p_payload
                                        ))
                                    )
                                  : plane->sync_admit_frame(
                                        p_sender,
                                        p_route,
                                        p_comp,
                                        p_channel,
                                        0,
                                        -1,
                                        p_payload
                                    );
    } else if (
        p_channel == ids.predict_command || p_channel == ids.predict_ack
        || p_channel == ids.predict_relay
        || p_channel == ids.predict_relay_request
    ) {
        gate = EventPlane::GATE_PREDICT;
        Object *lane = gate_seam(StringName("_predict_admit_frame"));
        verdict = lane != nullptr ? Error(
                                        int(lane->call(
                                            "_predict_admit_frame",
                                            p_sender,
                                            p_route,
                                            p_channel,
                                            p_payload
                                        ))
                                    )
                                  : plane->predict_admit_frame(
                                        p_sender,
                                        p_route,
                                        p_channel,
                                        p_payload
                                    );
    }
    if (gate < 0) {
        return OK;
    }
    return plane->finish_stage_verdict(gate, verdict, p_route);
}

void ReplicationCore::send_property(
    Node *p_node,
    const StringName &p_property
) {
    NetwMultiplayer *plane = core();
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (plane == nullptr || entity.is_null()) {
        sync_pipeline.send_property(p_node, p_property);
        return;
    }
    const Error verdict = plane->sync_send_property(
        entity->get_rid_handle(),
        entity->comp_of(p_node),
        p_property
    );
    plane->sink_verdict(verdict, entity->get_route());
}

void ReplicationCore::send_signal(
    Node *p_node,
    const StringName &p_signal,
    const Array &p_args
) {
    NetwMultiplayer *plane = core();
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (plane == nullptr || entity.is_null()) {
        sync_pipeline.send_signal(p_node, p_signal, p_args);
        return;
    }
    const Error verdict = plane->sync_send_signal(
        entity->get_rid_handle(),
        entity->comp_of(p_node),
        p_signal,
        p_args
    );
    plane->sink_verdict(verdict, entity->get_route());
}

Node *ReplicationCore::resolve_comp_node(
    const Ref<NetwEntity> &p_entity,
    int64_t p_comp,
    const String &p_path
) {
    if (p_entity.is_null()) {
        return nullptr;
    }
    return p_entity->comp_table()
        .resolve_node(p_entity->get_owner(), p_comp, p_path);
}

void ReplicationCore::on_clock_tick(int64_t p_tick) {
    sync_pipeline.pump(p_tick);
    sync_compat.pump();
    spawn_pipeline.retry_adopt_parked();
    pump_tables(p_tick);
    flush_all_buffers();
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->flush_standalone_acks();
    }
}

void ReplicationCore::pump_tables(int64_t p_tick) {
    NetwMultiplayer *plane = core();
    Object *shell = api();
#if defined(NETW_MODULE)
    Object *table_object = table_core();
    netw::table::Core *table = nullptr;
    if (table_object != nullptr) {
        table = static_cast<netw::table::Core *>(table_object);
    }
#else
    netw::table::Core *table = Object::cast_to<netw::table::Core>(table_core());
#endif
    if (plane == nullptr || shell == nullptr || table == nullptr) {
        return;
    }
    plane->table_publish_intake();

    if (!plane->is_server() || plane->session_get_inner().is_null()
        || plane->session_get_inner()->get_multiplayer_peer().is_null()) {
        return;
    }
    const int64_t budget = plane->datagram_budget();
    const PackedInt64Array retired = table->take_lifecycle_removals();
    if (!retired.is_empty()) {
        broadcast_table_frames(
            netw::table::Core::encode_lifecycle(retired, p_tick, budget),
            true
        );
    }
    const TypedArray<RID> dirty = table->dirty_tables();
    for (int at = 0; at < dirty.size(); ++at) {
        const RID id = dirty[at];
        const PackedInt64Array gone = table->take_pending_removals(id);
        if (!gone.is_empty()) {
            broadcast_table_frames(
                table->encode_removal(id, gone, p_tick, budget),
                true
            );
        }
        broadcast_table_frames(
            table->encode_frames(id, budget, false),
            table->is_reliable(id)
        );
        table->clear_dirty(id);
        plane->table_publish(id);
    }
}

void ReplicationCore::broadcast_table_frames(
    const TypedArray<PackedByteArray> &p_frames,
    bool p_reliable
) {
    NetwMultiplayer *plane = core();
    if (p_frames.is_empty() || plane == nullptr) {
        return;
    }
    const PackedInt32Array peers = gd::api_peer_ids(plane->session_get_inner());
    for (int peer = 0; peer < peers.size(); ++peer) {
        for (int at = 0; at < p_frames.size(); ++at) {
            send_to(
                peers[peer],
                0,
                ids.table,
                p_frames[at],
                p_reliable,
                0,
                String(),
                true
            );
        }
    }
}

void ReplicationCore::handle_table_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NetwMultiplayer *plane = core();
#if defined(NETW_MODULE)
    Object *table_object = table_core();
    netw::table::Core *table = nullptr;
    if (table_object != nullptr) {
        table = static_cast<netw::table::Core *>(table_object);
    }
#else
    netw::table::Core *table = Object::cast_to<netw::table::Core>(table_core());
#endif
    if (plane == nullptr || table == nullptr) {
        return;
    }
    Object *seam = gate_seam(StringName("_table_admit_frame"));
    const Variant verdict = seam != nullptr
        ? seam->call("_table_admit_frame", p_sender, ids.table, p_payload)
        : Variant(plane->table_admit_frame(p_sender, ids.table, p_payload));
    if (plane->finish_stage_verdict(
            EventPlane::GATE_TABLE,
            Error(int64_t(verdict)),
            0
        )
        != OK) {
        return;
    }
    const Dictionary result = table->apply_frame(p_payload);
    const PackedInt64Array bound = result[StringName("bound")];
    if (!bound.is_empty()) {
        plane->liveness_bind_routes_data(bound);
    }
    const PackedInt64Array retired = result[StringName("retired")];
    if (!retired.is_empty()) {
        plane->liveness_tombstone_routes_data(retired);
    }
}

void ReplicationCore::replay_tables(int64_t p_peer_id) {
    NetwMultiplayer *plane = core();
#if defined(NETW_MODULE)
    Object *table_object = table_core();
    netw::table::Core *table = nullptr;
    if (table_object != nullptr) {
        table = static_cast<netw::table::Core *>(table_object);
    }
#else
    netw::table::Core *table = Object::cast_to<netw::table::Core>(table_core());
#endif
    if (plane == nullptr || table == nullptr || !plane->is_server()
        || plane->session_get_inner().is_null()
        || plane->session_get_inner()->get_multiplayer_peer().is_null()) {
        return;
    }
    const int64_t budget = plane->datagram_budget();
    const TypedArray<RID> published = table->published_tables();
    for (int at = 0; at < published.size(); ++at) {
        const TypedArray<PackedByteArray> frames
            = table->encode_frames(published[at], budget, true);
        for (int frame = 0; frame < frames.size(); ++frame) {
            send_to(
                p_peer_id,
                0,
                ids.table,
                frames[frame],
                true,
                0,
                String(),
                true
            );
        }
    }
}

void ReplicationCore::on_frame_end() {
    flush_all_buffers();
}

void ReplicationCore::on_poll() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    if (plane->clock_engine().get_configured()) {
        return;
    }
    sync_compat.pump();
    spawn_pipeline.retry_adopt_parked();
    flush_all_buffers();
    plane->flush_standalone_acks();
}

void ReplicationCore::clear_session() {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->carrier_clear();
    }
    sync_model.clear();
    sync_pipeline.clear_session();
    spawn_pipeline.clear_session();
    sync_compat.clear_session();
}

void ReplicationCore::clear_route(int64_t p_route) {
    sync_model.clear_route(p_route);
    sync_pipeline.clear_route(p_route);
    spawn_pipeline.clear_route(p_route);
    sync_compat.clear_route(p_route);
}

void ReplicationCore::clear_peer(int64_t p_peer_id) {
    sync_pipeline.clear_peer(p_peer_id);
    sync_compat.clear_peer(p_peer_id);
}

void ReplicationCore::dispose() {
    sync_pipeline.dispose();
    sync_compat.dispose();
}

Dictionary ReplicationCore::counters() const {
    NetwMultiplayer *plane = core();
    const Dictionary drops
        = plane != nullptr ? plane->repl_drop_stats() : Dictionary();
    Dictionary out;
    out[StringName("drops_unknown_route")]
        = int64_t(drops.get(StringName("drops_unknown_route"), 0));
    out[StringName("drops_not_live")]
        = int64_t(drops.get(StringName("drops_not_live"), 0));
    out[StringName("drops_lingering_route")]
        = int64_t(drops.get(StringName("drops_lingering_route"), 0));
    out[StringName("drops_dead_route")]
        = int64_t(drops.get(StringName("drops_dead_route"), 0));
    out[StringName("drops_no_node")]
        = int64_t(drops.get(StringName("drops_no_node"), 0));
    out[StringName("drops_traversal")]
        = int64_t(drops.get(StringName("drops_traversal"), 0));
    out[StringName("drops_comp_unresolved")]
        = int64_t(drops.get(StringName("drops_comp_unresolved"), 0));
    out.merge(sync_pipeline.counters());
    out.merge(spawn_pipeline.counters());
    out.merge(spawner_compat.counters());
    out.merge(sync_compat.counters());
    return out;
}

Ref<NetwEntity> ReplicationCore::replicate(
    Node *p_node,
    const Ref<NetwParticipant> &p_owner
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return Ref<NetwEntity>();
    }
    const RID handle = plane->entity_replicate(p_node, p_owner.ptr());
    return plane->entity_get_view(handle);
}

Node *ReplicationCore::spawn(
    const Callable &p_fn,
    const Array &p_args,
    const Ref<NetwParticipant> &p_owner
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return nullptr;
    }
    const RID handle = plane->spawn_fn(p_fn, p_args, p_owner.ptr());
    return NetwMultiplayer::scene_outer_of(plane->entity_get_node(handle));
}

void ReplicationCore::register_spawn_constructor(
    const StringName &p_id,
    const Callable &p_fn
) {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->spawn_register_constructor(p_id, p_fn);
    }
}

Node *ReplicationCore::spawn_registered(
    const StringName &p_id,
    const Array &p_args,
    const Ref<NetwParticipant> &p_owner
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return nullptr;
    }
    const RID handle = plane->spawn_registered(p_id, p_args, p_owner.ptr());
    return NetwMultiplayer::scene_outer_of(plane->entity_get_node(handle));
}

Ref<NetwEntity> ReplicationCore::adopt_in_place(Node *p_root) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return Ref<NetwEntity>();
    }
    const RID handle = plane->entity_adopt(p_root);
    return plane->entity_get_view(handle);
}

TypedArray<Dictionary> ReplicationCore::spawn_state_of(Node *p_root) {
    NetwMultiplayer *plane = core();
    const Ref<NetwEntity> wrapper = NetwEntity::of(p_root);
    if (plane != nullptr && wrapper.is_valid()
        && plane->get_liveness_core()->entity_is_valid(
            wrapper->get_rid_handle()
        )) {
        return plane->spawn_get_state(wrapper->get_rid_handle());
    }
    return spawn_pipeline.collect_spawn_state(p_root);
}

bool ReplicationCore::owns_spawned_route(int64_t p_route) {
    return spawn_pipeline.owns_spawned_route(p_route);
}

} // namespace netw
