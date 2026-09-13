#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/packed_scene.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/sync_compat.hpp"
#include "netw/call_args.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/park.hpp"
#include "netw/spawn/record.hpp"
#include "netw/spawn/spawner_compat.hpp"

namespace netw {
class NetwMultiplayer;
}

namespace netw::spawn {

struct SpawnFrame {
    struct StateEntry {
        bool by_path = false;
        godot::String path;
        godot::PackedByteArray token;
        godot::PackedByteArray values;
    };

    struct NativeEntry {
        godot::String path;
        godot::PackedByteArray value;
    };

    int64_t route = 0;
    int64_t epoch = 0;
    godot::Dictionary header;
    int64_t recipe = 0;
    bool parent_is_spawn_target = false;
    godot::Dictionary parent_anchor;
    godot::String scene_path;
    godot::Dictionary spawner_anchor;
    int64_t scene_index = 0;
    godot::PackedByteArray custom;
    godot::String fn_id;
    godot::Dictionary host_anchor;
    godot::String method;
    godot::PackedByteArray args;
    godot::LocalVector<StateEntry> state;
    godot::LocalVector<NativeEntry> native;
    godot::PackedByteArray consumed;
    godot::PackedByteArray derived;
};

bool decode_spawn_frame(
    const NetwMultiplayer *p_plane,
    const godot::PackedByteArray &p_payload,
    SpawnFrame &r_frame
);

class Pipeline {
private:
    godot::ObjectID core_id;
    godot::ObjectID api_id;

    Book spawn_book;
    Park park;
    SpawnerCompat *spawner_compat = nullptr;
    SyncCompat *sync_compat = nullptr;

    godot::Callable encode_prop_val_seam;
    godot::Callable encode_derived_seam;
    godot::Callable note_derived_seam;
    godot::Callable flush_buffers_seam;
    godot::Callable resolve_comp_seam;

    int64_t channel_spawn = 0;
    int64_t channel_despawn = 0;
    int64_t channel_hide = 0;
    int64_t channel_reparent = 0;

    godot::HashMap<godot::StringName, godot::Callable> constructors;
    godot::HashMap<godot::StringName, godot::Array> constructor_schemas;

    int64_t drops_spawn_bad_sender = 0;
    int64_t drops_spawn_duplicate = 0;
    int64_t drops_spawn_unresolved = 0;
    int64_t drops_spawn_truncated = 0;
    int64_t drops_spawn_stale_life = 0;
    int64_t drops_despawn_unknown = 0;
    int64_t drops_hide_unknown = 0;
    int64_t spawn_deferrals = 0;
    int64_t moves_unadmitted = 0;
    int64_t spawn_parked_cancelled = 0;
    int64_t spawn_park_expired = 0;

#ifdef NETW_TESTS
    godot::TypedArray<godot::Dictionary> armed_spawn_state;
    bool spawn_state_is_armed = false;
    int spawn_state_asked = 0;
#endif

    friend class netw::NetwMultiplayer;

    NetwMultiplayer *core() const;
    godot::Object *api() const;
    godot::Object *stage_seam(const godot::StringName &p_seam) const;
    bool is_server_authority() const;
    bool has_peer() const;
    godot::PackedInt32Array connected_peers() const;

    godot::Error stage_verdict(
        int64_t p_stage,
        godot::Error p_verdict,
        int64_t p_route
    );
    void sink_verdict(godot::Error p_verdict, int64_t p_route);
    void connect_once(
        godot::Object *p_source,
        const godot::StringName &p_signal,
        const godot::Callable &p_callable,
        uint32_t p_flags
    );

    godot::Ref<NetwEntity> arm_authoritative_spawn(
        Record *p_record,
        godot::Node *p_node,
        const godot::Ref<NetwParticipant> &p_owner
    );
    godot::Error declare_stage(
        const godot::RID &p_handle,
        const godot::Dictionary &p_facts
    );

    void on_peer_connected(int64_t p_peer_id);
    godot::Error replay_spawn_book(int64_t p_peer_id);
    void run_visibility_sweep();
    void sweep_now();
    void schedule_carrier_flush();
    void run_carrier_flush();

    void on_armed_tree_entered(int64_t p_route);
    void schedule_armed_flush(int64_t p_route);
    void flush_armed_spawn(int64_t p_route);
    void settle_move(int64_t p_route);
    void settle_death(int64_t p_route);
    void settle_absence(int64_t p_route);
    bool holds_received_route(int64_t p_route) const;
    void despawn_tracked_route(int64_t p_route);
    void send_reparent(Record *p_record, godot::Node *p_node);

    godot::Callable constructor_of(const godot::StringName &p_id) const;
    godot::Variant fn_registry_schema(
        const godot::StringName &p_id,
        const godot::Callable &p_fn
    ) const;
    static godot::Variant fn_script_schema(
        const godot::Ref<godot::Script> &p_script,
        const godot::StringName &p_method
    );
    godot::PackedByteArray encode_fn_args(
        const godot::Array &p_schema,
        const godot::Array &p_args
    );
    static godot::LocalVector<call_args::Slot> read_fn_args(
        const godot::PackedByteArray &p_bytes,
        const godot::Array &p_schema
    );
    godot::Variant resolve_spawn_args(
        const godot::LocalVector<call_args::Slot> &p_slots,
        const godot::PackedByteArray &p_payload,
        int64_t p_route
    );

    godot::Node *run_construct_stage(const godot::Callable &p_constructor);
    godot::Node *build_adopt(
        godot::Object *p_parent,
        const godot::String &p_name
    );
    godot::Node *build_scene(const godot::Variant &p_packed);
    godot::Node *build_spawner(
        godot::Object *p_spawner,
        int64_t p_index,
        const godot::Variant &p_data
    );
    godot::Node *build_fn(
        const godot::Callable &p_fn,
        const godot::Array &p_args
    );
    godot::Node *build_host_fn(
        godot::Object *p_host,
        const godot::StringName &p_method,
        const godot::Array &p_args
    );
    bool frame_read(bool p_healthy, int64_t p_route);
    bool admits_frame(
        int64_t p_sender,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );
    void try_apply_spawn(const godot::PackedByteArray &p_payload);
    void free_despawned(int64_t p_route);
    void free_route_node(int64_t p_route, godot::Node *p_node);

    int64_t park_timeout_ticks() const;
    void park_spawn(
        const godot::PackedByteArray &p_payload,
        int64_t p_dep_route,
        int64_t p_route
    );
    void retry_parked(int64_t p_route);
    void expire_parked(int64_t p_route);
    void drop_scene_park_retry();

    int64_t gate_display_tick() const;
    void apply_action_gate(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void on_action_reveal_tick(double p_delta, int64_t p_tick);
    void disconnect_action_reveal_if_idle();
    void ensure_action_gate_connection();
    void clear_action_gates();

public:
    double park_timeout_seconds = 5.0;
    void set_park_timeout_seconds(double p_seconds) {
        park_timeout_seconds = p_seconds;
    }
    double get_park_timeout_seconds() const {
        return park_timeout_seconds;
    }

    void set_core(godot::Object *p_core);
    void set_api(godot::Object *p_api);
    void set_adapters(SpawnerCompat *p_spawner, SyncCompat *p_sync);
    void set_channels(
        int64_t p_spawn,
        int64_t p_despawn,
        int64_t p_hide,
        int64_t p_reparent
    );
    void set_encode_seams(const godot::Callable &p_encode_prop_val);
    void set_derived_seams(
        const godot::Callable &p_encode_derived,
        const godot::Callable &p_note_derived
    );
    void set_repl_seams(
        const godot::Callable &p_flush_buffers,
        const godot::Callable &p_resolve_comp
    );

    Book *get_spawn_book() {
        return &spawn_book;
    }
    bool books_node(godot::Node *p_node) const {
        return spawn_book.books_node(p_node);
    }
    Park &get_park() {
        return park;
    }

    void park_spawn_for_scene(
        const godot::PackedByteArray &p_payload,
        int64_t p_route
    );
    void retry_scene_parked_spawns(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );

    void park_spawn_for_adopt(
        const godot::PackedByteArray &p_payload,
        int64_t p_route
    );
    void retry_adopt_parked();

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
        const godot::Callable &p_fn,
        const godot::Array &p_arg_types,
        const godot::Array &p_quantizers
    );
    godot::Node *spawn_registered(
        const godot::StringName &p_id,
        const godot::Array &p_args,
        const godot::Ref<NetwParticipant> &p_owner
    );
    godot::Ref<NetwEntity> adopt_in_place(godot::Node *p_root);
    godot::Ref<NetwEntity> arm_consumed_spawn(
        godot::Node *p_node,
        godot::Object *p_spawner,
        int p_scene_index,
        const godot::Variant &p_data
    );

    void schedule_visibility_sweep();
    bool owns_spawned_route(int64_t p_route) const;
    godot::TypedArray<godot::Dictionary> collect_spawn_state(
        godot::Node *p_root
    );

#ifdef NETW_TESTS
    void arm_spawn_state(const godot::Array &p_rows);
    void disarm_spawn_state();
    int spawn_state_asks() const {
        return spawn_state_asked;
    }
#endif

    static bool put_scene_recipe(
        wire::WriteStream &p_stream,
        const godot::String &p_path
    );
    static bool get_scene_recipe(
        wire::ReadStream &p_stream,
        godot::String &r_path
    );

    godot::PackedByteArray encode_spawn_frame(
        int64_t p_route,
        godot::Node *p_node
    );

    void handle_spawn_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void handle_despawn_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void handle_hide_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void handle_reparent_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );

    void clear_session();
    void clear_route(int64_t p_route);

    godot::Dictionary counters() const;
};

} // namespace netw::spawn
