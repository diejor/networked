#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/repl/property_set.hpp"
#include "netw/replication_send.hpp"
#include "netw/store/world.hpp"
#include "netw/sync_progress.hpp"

namespace netw {

class NetwMultiplayer;

class SyncPipeline {
    friend class NetwMultiplayer;

private:
    godot::ObjectID core_id;
    bool hears_entity_live = false;
    godot::ObjectID api_id;
    NetwSyncModel *sync_model = nullptr;
    ReplicationSend row_sender;
    bool row_sender_armed = false;
    SyncProgress progress;

    using IndexedBinding = NetwEntityRecord::HeldBinding;

    godot::LocalVector<uint64_t> bindings;
    godot::LocalVector<uint64_t> binding_entities;
    godot::LocalVector<IndexedBinding> loose_bindings;
    godot::LocalVector<uint64_t> binding_nodes;
    uint64_t binding_sequence = 0;
    godot::LocalVector<uint64_t> prune_candidates;
    bool tree_watched = false;

    const godot::LocalVector<uint64_t> *node_bindings(
        const godot::Node *p_node
    ) const;
    uint32_t binding_position(const repl::PropertyBinding *p_binding) const;
    repl::PropertyBinding *live(uint64_t p_binding) const;
    repl::BindingBook *binding_book() const;
    godot::LocalVector<IndexedBinding> *held_by(uint64_t p_entity) const;
    void seat_record(repl::PropertyBinding *p_binding);
    void unseat_record(
        uint64_t p_entity,
        const repl::PropertyBinding *p_binding
    );
    godot::LocalVector<godot::RID> state_binding_dropped;

    void append_binding(repl::PropertyBinding *p_binding);
    void replace_binding(uint32_t p_at, repl::PropertyBinding *p_binding);
    void remove_binding(uint32_t p_at);
    void retire(uint64_t p_binding);
    void clear_bindings();
    void store_admit(const repl::PropertyBindingPtr &p_binding);
    void store_place(const repl::PropertyBindingPtr &p_binding);
    godot::LocalVector<repl::PropertyBindingPtr> bindings_of_entity(
        const godot::Ref<NetwEntity> &p_entity
    ) const;
    bool binding_belongs_to(
        const repl::PropertyBindingPtr &p_binding,
        const godot::Ref<NetwEntity> &p_entity
    ) const;
    godot::LocalVector<repl::PropertyBindingPtr>
    entity_bindings_checked(const godot::Ref<NetwEntity> &p_entity) const;
    godot::HashMap<int64_t, int64_t> contract_hashes;

    godot::Callable encode_stage_seam;
    godot::Callable decode_stage_seam;
    godot::Callable tap_seam;

    int64_t channel_row = 0;
    int64_t channel_row_window = 0;
    int64_t channel_row_delta = 0;
    int64_t channel_row_control = 0;
    int64_t channel_property = 0;
    int64_t channel_signal = 0;

    int64_t sends_dropped_unroutable = 0;
    int64_t sends_dropped_not_live = 0;
    int64_t rows_refused_stale = 0;

    godot::HashMap<int64_t, bool> schemas_sealed_before_open;

    godot::PackedByteArray stage_bytes;
    godot::Array stage_values;
    godot::PackedByteArray stage_payload;
    store::RowAddress stage_at;
    int64_t stage_sender = 0;
    bool stage_windowed = false;
    store::Landing stage_landing = store::Landing::MALFORMED;
    const godot::LocalVector<CarrierRow> *carrier_rows = nullptr;
    int64_t carrier_peer = 0;
    int64_t carrier_frames = 0;
    int64_t carrier_bits = 0;
    int64_t datagram_tick = -1;
    int64_t datagram_seq = -1;
    godot::Node *stage_node = nullptr;
    godot::StringName stage_property;

    NetwMultiplayer *core() const;
    godot::Object *api() const;
    ReplicationSend *row_send();

#if defined(NETW_TESTS)
public:
    ReplicationSend *row_send_under_test() {
        return row_send();
    }

private:
#endif

    godot::PackedByteArray stage_row_frame(
        int64_t p_peer,
        int64_t p_frame_tick,
        int64_t p_route,
        int64_t p_comp,
        int64_t p_mask,
        const godot::PackedByteArray &p_bytes
    );
    godot::PackedByteArray run_encode_stage(
        int64_t p_peer,
        int64_t p_tick,
        const godot::PackedByteArray &p_bytes
    );
    godot::Error run_decode_stage(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::PackedByteArray &p_payload
    );
    godot::Error decode_stage_body();
    godot::Array gather_stage();

    void capture_declaration_key(const repl::PropertyBindingPtr &p_b);
    void bind_declaration(const repl::PropertyBindingPtr &p_b);
    void drop_declaration(const repl::PropertyBindingPtr &p_b);
    void prune_derived();
    void prune_node(uint64_t p_key);
    void watch_tree();
    void recapture_entity(const godot::Ref<NetwEntity> &p_entity);
    void on_entity_live(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void on_route_retired(int64_t p_route);
    void settle_entity_column(int64_t p_route, bool p_live);

    void queue_prediction_contract_check(
        godot::Node *p_node,
        const repl::PropertySet *p_state,
        const repl::PropertySet *p_input
    );
    repl::PropertySet *set_of(uint64_t p_set) const;
    void report_missing_prediction_component(
        godot::Node *p_node,
        int64_t p_config_hash
    );
    void register_state_timeline(godot::Node *p_node);
    void reconcile_state_timeline(godot::Node *p_node);
    void reconcile_dropped_state_timelines();
    bool holds_state_binding(const godot::Ref<NetwEntity> &p_entity) const;

    godot::Dictionary resolve_send(godot::Node *p_node);
    void send_entity_event(
        const godot::Dictionary &p_frame,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        bool p_reliable
    );
    void apply_row(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_ordinal,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender,
        int64_t p_channel,
        int p_lane
    );
    void feed_derived_interpolation(
        const repl::PropertyBindingPtr &p_binding,
        const store::RowAddress &p_at
    );
    void flush_rows();
    void flush_row_control();
    void reset_stream_of(int p_peer, uint64_t p_token);
    void reset_stray(int p_peer, uint64_t p_token);
    void record_interpolated_signal_args(
        godot::Node *p_comp_node,
        const godot::Array &p_args,
        const godot::Ref<NetwMemberConfig> &p_opt
    );
    godot::Error apply_one_value(const godot::Array &p_values);
    int64_t receive_tick() const;

public:
    int64_t prune_visits = 0;

    void note_node_removed(godot::Node *p_node);
    void set_core(godot::Object *p_core);
    void set_api(godot::Object *p_api);
    void set_sync_model(NetwSyncModel *p_model);
    void set_channels(
        int64_t p_row,
        int64_t p_row_window,
        int64_t p_row_delta,
        int64_t p_row_control,
        int64_t p_property,
        int64_t p_signal
    );
    void set_stage_seams(
        const godot::Callable &p_encode,
        const godot::Callable &p_decode
    );
    void set_tap_seam(const godot::Callable &p_tap);

    void register_derived(godot::Node *p_node);
    godot::Error register_property_set(
        godot::Node *p_node,
        uint64_t p_set
    );
    repl::PropertyBindingPtr derived_binding(
        godot::Node *p_node,
        int64_t p_record
    );
    void note_schema_seal(const repl::PropertySet *p_set);
    int64_t sealed_schema_identity() const;
    bool owes_rows() const {
        return !bindings.is_empty();
    }
    void unregister_derived(godot::Node *p_node);
    godot::LocalVector<repl::PropertyBindingPtr> derived_group(
        int64_t p_route
    );
#if defined(NETW_TESTS)
    void reconcile_state_timeline_for_test(godot::Node *p_node) {
        reconcile_state_timeline(p_node);
    }
    void reconcile_dropped_state_timelines_for_test() {
        reconcile_dropped_state_timelines();
    }
#endif

    void pump(int64_t p_tick);
    void open_carrier_batch(
        int64_t p_peer_id,
        const godot::LocalVector<CarrierRow> *p_rows,
        int64_t p_frames,
        int64_t p_bits
    );
    void commit_pending_masked(int64_t p_peer_id, int64_t p_seq);
    void close_carrier_batch();
    void note_peer_ack(
        int64_t p_peer_id,
        int64_t p_acked_seq,
        uint32_t p_history
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

    bool accept_unreliable(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        int64_t p_seq
    );
    void open_datagram(int64_t p_base_tick, int64_t p_seq = -1);

    static godot::Dictionary gather_payload(
        godot::Node *p_node,
        const repl::PropertySet *p_set
    );
    static void apply_payload(
        godot::Node *p_node,
        const repl::PropertySet *p_set,
        const godot::Dictionary &p_payload
    );

    void handle_derived_row(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_ordinal,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender,
        int64_t p_channel
    );
    void handle_window_row(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_ordinal,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender,
        int64_t p_channel
    );
    void handle_retained_row(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_ordinal,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender,
        int64_t p_channel
    );

    godot::Variant encode_prop_val(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_node,
        const godot::StringName &p_property
    );
    godot::Variant encode_signal_val(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_node,
        const godot::StringName &p_signal
    );

    void handle_property_sync(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_comp_node,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender,
        int64_t p_comp
    );
    void handle_signal(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_comp_node,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );

    godot::PackedByteArray encode_derived_descriptors(int64_t p_route);
    void note_derived_schema(
        int64_t p_route,
        const godot::Dictionary &p_descriptors
    );

    static godot::String missing_prediction_component_message(
        const godot::String &p_entity_name
    );

    void clear_session();
    void clear_route(int64_t p_route);
    void clear_peer(int64_t p_peer_id);
    void dispose();

    godot::Dictionary counters() const;
};

} // namespace netw
