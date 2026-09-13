#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/sync_compat.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/channel_book.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/spawn/spawner_compat.hpp"
#include "netw/wire/registry.hpp"

namespace netw {

class NetwMultiplayer;

class ReplicationCore {
    friend class NetwMultiplayer;

private:
    struct Channels {
        int64_t table = 0;
        int64_t spawn = 0;
        int64_t despawn = 0;
        int64_t hide = 0;
        int64_t reparent = 0;
        int64_t call = 0;
        int64_t reply = 0;
        int64_t control_request = 0;
        int64_t control_apply = 0;
        int64_t property_sync = 0;
        int64_t signal = 0;
        int64_t sync = 0;
        int64_t sync_delta = 0;
        int64_t sync_row = 0;
        int64_t sync_row_delta = 0;
        int64_t sync_row_window = 0;
        int64_t predict_command = 0;
        int64_t predict_ack = 0;
        int64_t predict_relay = 0;
        int64_t predict_relay_request = 0;
        int64_t action = 0;
        int64_t clock_handshake = 0;
        int64_t clock_handshake_reply = 0;
        int64_t clock_ping = 0;
        int64_t clock_pong = 0;
        int64_t interest_awareness = 0;
        int64_t lagcomp_deny = 0;
    };

    godot::ObjectID core_id;
    godot::ObjectID api_id;

    NetwChannelBook *channels = nullptr;
    NetwSyncModel sync_model;
    SyncPipeline sync_pipeline;
    spawn::Pipeline spawn_pipeline;
    spawn::SpawnerCompat spawner_compat;
    SyncCompat sync_compat;

    Channels ids;

    NetwMultiplayer *core() const;
    godot::Object *api() const;
    godot::Object *table_core() const;
    godot::Error finish_receive_pass(
        int64_t p_frames,
        godot::Error p_verdict,
        int64_t p_sender
    );
    void dispatch_frame(
        int64_t p_route,
        int64_t p_comp,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        const godot::String &p_path,
        int64_t p_sender,
        bool p_reliable,
        int64_t p_seq
    );
    void dispatch_reply(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void settle_reply(
        int64_t p_txn,
        int64_t p_sender,
        int64_t p_route,
        int64_t p_comp,
        const godot::String &p_path
    );
    bool defers_unknown_route(int64_t p_channel, bool p_reliable) const;
    bool channel_addresses_a_set(int64_t p_channel) const;
    godot::Error admit_entity_frame(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_comp,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );
    void broadcast_table_frames(
        const godot::TypedArray<godot::PackedByteArray> &p_frames,
        bool p_reliable
    );
    void handle_table_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void resolve_channel_ids();

public:
    ReplicationCore();

    void install(godot::Object *p_api);
    godot::Object *gate_seam(const godot::StringName &p_seam) const;
    void set_tap_seam(const godot::Callable &p_tap);

    NetwSyncModel *get_sync_model() {
        return &sync_model;
    }
    SyncPipeline *get_sync_pipeline() {
        return &sync_pipeline;
    }
    spawn::Pipeline *get_spawn_pipeline() {
        return &spawn_pipeline;
    }
    spawn::SpawnerCompat *get_spawner_compat() {
        return &spawner_compat;
    }
    SyncCompat *get_sync_compat() {
        return &sync_compat;
    }
    bool get_is_applying_remote_frame() const;

    void register_channel(
        int64_t p_channel,
        const godot::Callable &p_handler,
        bool p_defer_when_unknown
    );
    void register_protocol(int64_t p_channel, const godot::Callable &p_handler);
    godot::Error settle_channels();

    godot::Ref<NetwPropertySetBinding> derived_binding(
        godot::Node *p_node,
        int64_t p_record
    );
    godot::TypedArray<NetwPropertySetBinding> derived_group(int64_t p_route);
    void note_peer_ack(
        int64_t p_peer_id,
        int64_t p_acked_seq,
        uint32_t p_history
    );

    void send_to(
        int64_t p_peer_id,
        int64_t p_route,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        bool p_reliable,
        int64_t p_comp,
        const godot::String &p_path,
        bool p_batched
    );
    void broadcast_control(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer
    );
    bool is_live_for(int64_t p_peer_id, const godot::Ref<NetwEntity> &p_entity);
    bool policy_admits(
        int64_t p_policy,
        int64_t p_sender,
        godot::Node *p_node,
        const godot::Ref<NetwEntity> &p_entity
    );
    godot::PackedInt32Array live_peers(const godot::Ref<NetwEntity> &p_entity);
    void request_control(const godot::Ref<NetwEntity> &p_entity);
    void flush_all_buffers();
    void note_staged(int64_t p_peer_id, int64_t p_seq);

    godot::Error dispatch(
        int64_t p_route,
        int64_t p_comp,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        const godot::String &p_path,
        int64_t p_sender,
        bool p_reliable,
        int64_t p_seq
    );

    godot::Error receive_carrier(
        const godot::PackedByteArray &p_framed,
        int64_t p_sender,
        bool p_reliable,
        int64_t p_seq,
        int64_t p_base_tick
    );

    void send_property(
        godot::Node *p_node,
        const godot::StringName &p_property
    );
    void send_signal(
        godot::Node *p_node,
        const godot::StringName &p_signal,
        const godot::Array &p_args
    );

    godot::Node *resolve_comp_node(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_comp,
        const godot::String &p_path
    );

    void on_clock_tick(int64_t p_tick);
    void pump_tables(int64_t p_tick);
    void replay_tables(int64_t p_peer_id);
    void on_frame_end();
    void on_poll();

    void clear_session();
    void clear_route(int64_t p_route);
    void clear_peer(int64_t p_peer_id);
    void dispose();
    godot::Dictionary counters() const;

    godot::Ref<NetwEntity> replicate(
        godot::Node *p_node,
        const godot::Ref<NetwParticipant> &p_owner
    );
    godot::Node *spawn(
        const godot::Callable &p_fn,
        const godot::Array &p_args,
        const godot::Ref<NetwParticipant> &p_owner
    );
    void register_spawn_constructor(
        const godot::StringName &p_id,
        const godot::Callable &p_fn
    );
    godot::Node *spawn_registered(
        const godot::StringName &p_id,
        const godot::Array &p_args,
        const godot::Ref<NetwParticipant> &p_owner
    );
    godot::Ref<NetwEntity> adopt_in_place(godot::Node *p_root);
    godot::TypedArray<godot::Dictionary> spawn_state_of(godot::Node *p_root);
    bool owns_spawned_route(int64_t p_route);
};

} // namespace netw
