#pragma once

#include "godot/callable.hpp"
#include "godot/gdvirtual.hpp"
#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "godot/multiplayer.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/channel_book.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/clock_engine.hpp"
#include "netw/datagram_seq_book.hpp"
#include "netw/effect_ledger.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/event_plane.hpp"
#include "godot/script.hpp"
#include "netw/gate_verdict_book.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/interest_engine.hpp"
#include "netw/interest_leave.hpp"
#include "netw/interest_perception.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/clock_handle.hpp"
#include "netw/interest_relay.hpp"
#include "netw/join_roster.hpp"
#include "netw/lagcomp_core.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/persistence_loop.hpp"
#include "netw/predict/engine.hpp"
#include "netw/api/liveness_core.hpp"
#include "netw/api/display_book.hpp"
#include "netw/display_build.hpp"
#include "netw/display_roles.hpp"
#include "netw/display_pump.hpp"
#include "netw/api/scene_mark.hpp"
#include "netw/scene_core.hpp"
#include "netw/api/quantize.hpp"
#include "netw/rate_window.hpp"
#include "netw/session_core.hpp"
#include "netw/settle_queue.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/table/table_core.hpp"
#include "netw/wire/registry.hpp"

namespace netw {

class NetwMultiplayerCore : public MultiplayerApiBase {
    GDCLASS(NetwMultiplayerCore, MultiplayerApiBase)

public:
    enum TableParam {
        TABLE_PARAM_RELIABLE = 0,
    };

    enum SessionState {
        SESSION_STATE_OFFLINE = SessionCore::STATE_OFFLINE,
        SESSION_STATE_CONNECTING = SessionCore::STATE_CONNECTING,
        SESSION_STATE_ONLINE = SessionCore::STATE_ONLINE,
        SESSION_STATE_DISCONNECTING = SessionCore::STATE_DISCONNECTING,
    };

    enum Role {
        ROLE_NONE = SessionCore::ROLE_NONE,
        ROLE_CLIENT = SessionCore::ROLE_CLIENT,
        ROLE_DEDICATED_SERVER = SessionCore::ROLE_DEDICATED_SERVER,
        ROLE_LISTEN_SERVER = SessionCore::ROLE_LISTEN_SERVER,
    };

    enum NameVerdict {
        NAME_ADMIT = JoinRoster::ADMIT,
        NAME_RENAME = JoinRoster::RENAME,
        NAME_REFUSE = JoinRoster::REFUSE,
    };

    enum SceneCapture {
        SCENE_CAPTURE_REFUSED = NetwSceneCore::CAPTURE_REFUSED,
        SCENE_CAPTURE_REQUEST = NetwSceneCore::CAPTURE_REQUEST,
        SCENE_CAPTURE_CHANGE_SESSION = NetwSceneCore::CAPTURE_CHANGE_SESSION,
        SCENE_CAPTURE_MOVE_ME = NetwSceneCore::CAPTURE_MOVE_ME,
        SCENE_CAPTURE_ACTIVATE = NetwSceneCore::CAPTURE_ACTIVATE,
    };

    enum SceneMove {
        SCENE_MOVE_REFUSED = NetwSceneCore::MOVE_REFUSED,
        SCENE_MOVE_ALREADY_THERE = NetwSceneCore::MOVE_ALREADY_THERE,
        SCENE_MOVE_CARRY = NetwSceneCore::MOVE_CARRY,
    };

    enum SceneDestination {
        SCENE_DESTINATION_NONE = NetwSceneCore::DESTINATION_NONE,
        SCENE_DESTINATION_NAME = NetwSceneCore::DESTINATION_NAME,
        SCENE_DESTINATION_NODE = NetwSceneCore::DESTINATION_NODE,
        SCENE_DESTINATION_PACKED = NetwSceneCore::DESTINATION_PACKED,
        SCENE_DESTINATION_PACKED_UNPATHED
            = NetwSceneCore::DESTINATION_PACKED_UNPATHED,
    };

private:
    godot::Ref<godot::MultiplayerPeer> peer;

    godot::Ref<godot::SceneMultiplayer> inner;

    godot::PackedInt32Array peer_ids;
    godot::Ref<NetwLivenessCore> liveness_core;
    JoinRoster join_roster;
    SessionCore session_core;
    godot::Ref<NetwClockHandle> clock_handle;
    godot::Ref<NetwSceneCore> scene_core;
    godot::Ref<NetwDisplayBook> display_book;
    RateWindow scene_request_window;
    InterestEngine interest_engine;
    InterestLeave interest_leave;
    InterestPerception interest_perception;
    InterestRelay interest_relay;
    godot::Ref<NetwLagCompCore> lagcomp_core;
    godot::Ref<NetwPredictionEngine> prediction_engine;
    persist::Plane persistence;

    int64_t sent_packets = 0;
    int64_t sent_bytes = 0;
    int64_t received_packets = 0;
    int64_t received_bytes = 0;
    int64_t state_acks_out = 0;
    int64_t state_acks_in = 0;
    int64_t standalone_acks_out = 0;
    int64_t frame_counter = 0;
    int64_t last_poll_usec = 0;
    DatagramSeqBook seq_book;
    godot::Ref<NetwCarrierBuffers> carrier;
    godot::Ref<NetwChannelBook> channel_book;
    wire::WireRegistry channels = wire::WireRegistry::create_default();
    struct GateChannels {
        uint8_t spawn = 0;
        uint8_t despawn = 0;
        uint8_t reparent = 0;
        uint8_t table = 0;
    } gate_channels;
    struct ControlChannels {
        uint8_t kicked = 0;
        uint8_t shutdown = 0;
        uint8_t kick_request = 0;
        uint8_t leave_request = 0;
        uint8_t control_request = 0;
        uint8_t pause = 0;
        uint8_t unpause = 0;
    } control_channels;
    struct ClockChannels {
        uint8_t handshake = 0;
        uint8_t handshake_reply = 0;
        uint8_t ping = 0;
        uint8_t pong = 0;
    } clock_channels;
    int32_t clock_mismatch_action = 0;
    uint8_t session_join_channel = 0;
    uint8_t session_accept_channel = 0;
    uint8_t session_roster_channel = 0;
    uint8_t scene_request_channel = 0;
    uint8_t scene_result_channel = 0;
    uint8_t scene_released_channel = 0;
    GateVerdictBook verdict_book;
    SettleQueue settle_queue;
    godot::Ref<NetwHandleLedger> schemas;
    godot::Ref<SchemaCore> schema_core;
    godot::Ref<NetwHandleLedger> tables;
    godot::Ref<TableCore> table_core;
    godot::Ref<NetwEffectLedger> effects;
    EventPlane plane;
    godot::HashSet<godot::StringName> misused_seams;
    godot::HashMap<godot::StringName, bool> seam_overrides;
    godot::ObjectID seam_script;
    godot::HashMap<godot::StringName, godot::RID> table_by_name;
    godot::HashMap<int64_t, godot::RID> table_schema;

    struct ServiceRow {
        godot::ObjectID type;
        godot::ObjectID service;
    };
    godot::LocalVector<ServiceRow> services;

    godot::HashMap<int64_t, godot::Ref<godot::RefCounted>> live_wrappers;
    godot::HashMap<int64_t, godot::Ref<godot::RefCounted>> retired_wrappers;

    godot::HashMap<int64_t, godot::Ref<NetwEntityRecord>> wrapper_records;

    godot::HashMap<int64_t, godot::ObjectID> wrapper_owners;
    godot::HashMap<uint64_t, int64_t> handle_by_wrapper;
    godot::Callable spawn_state;
    godot::ObjectID replication;
    godot::ObjectID spawn_pipeline_id;
    godot::ObjectID sync_pipeline_id;
    godot::Callable interest_flush;
    godot::Callable interest_compat_refresh;
    godot::Callable interest_awareness_send;
    godot::Callable interest_visibility_sweep;
    InterestDelta interest_pending;
    bool interest_pending_live = false;

    void interest_relay_awareness();
    godot::Callable scene_refresh;
    godot::Callable scene_path_reader;
    godot::Callable scene_mark_reader;
    godot::Callable scene_participant_edge;
    godot::Callable scene_carry_move;
    godot::Callable request_deadline_arm;
    godot::Callable session_root_reader;
    godot::Callable session_join_handler;
    godot::Callable session_join_resolver;
    godot::Callable session_entered_hook;
    godot::Callable session_edge_hook;
    godot::Callable desired_role_reader;
    godot::Callable identity_reader;
    display::DisplayHooks display_hooks;
    int64_t display_last_frame = -1;
    bool display_session_bound = false;

    struct PerceptionSnapshot {
        godot::ObjectID node;
        godot::StringName prop;
        godot::Variant value;
    };
    godot::HashMap<int64_t, godot::LocalVector<PerceptionSnapshot>>
        perception_snapshots;

    void interest_layer_transition(
        const godot::Ref<NetwInterestLayer> &p_layer,
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id,
        bool p_visible
    );
    void interest_report_edge(
        const InterestAwareness &p_edge,
        const godot::Ref<NetwEntity> &p_entity,
        bool p_entered,
        bool p_layer_edge
    );
    void interest_queue_layer_awareness(
        const godot::StringName &p_layer_id,
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_observer_peer,
        int p_kind
    );
    void interest_queue_observer_awareness(
        const godot::StringName &p_layer_id,
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_observer_peer,
        int p_kind
    );

    void perception_hide(int64_t p_slot, godot::Node *p_owner);
    void perception_restore(int64_t p_slot);
    void perception_dispatch_custom(
        int64_t p_slot,
        bool p_visible,
        int64_t p_peer_id
    );
    godot::Array perception_layers_for(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id
    );

    godot::Ref<NetwHandleLedger> layer_ledger;
    godot::HashMap<godot::StringName, godot::RID> layer_by_name;
    godot::HashMap<int64_t, godot::Ref<godot::RefCounted>> layer_views;

    godot::Ref<godot::RefCounted> local_player;
    int64_t local_player_id = 0;

    struct ParticipantRow {
        godot::Ref<godot::RefCounted> row;
        godot::RID seat;
        bool admitted = false;
    };
    godot::HashMap<int64_t, ParticipantRow> participants;

    ClockEngine &clock_engine() const { return clock_handle->engine; }

    int service_row(godot::Object *p_type) const;

    godot::Ref<NetwInterestDecl> interest_decl_of(const godot::RID &p_entity);
    static godot::Ref<NetwInterestDecl> interest_decl_on(
        const godot::Ref<NetwEntity> &p_entity
    );

    void relay_from(
        godot::Object *p_source,
        const godot::StringName &p_signal,
        int p_arity
    );
    void stop_relay_from(
        godot::Object *p_source,
        const godot::StringName &p_signal,
        int p_arity
    );
    godot::Callable relay_for(const godot::StringName &p_signal, int p_arity);

    void relay_named_from(
        godot::Object *p_source,
        const godot::StringName &p_source_signal,
        const godot::StringName &p_own_signal,
        int p_arity
    );
    void stop_relay_named_from(
        godot::Object *p_source,
        const godot::StringName &p_source_signal,
        const godot::StringName &p_own_signal,
        int p_arity
    );

    godot::Ref<godot::RefCounted> local_participant;
    void bind_local_participant(const godot::Ref<godot::RefCounted> &p_row);
    void participant_announce_seat(
        const godot::Ref<godot::RefCounted> &p_row,
        const godot::Ref<godot::RefCounted> &p_from,
        const godot::Ref<godot::RefCounted> &p_to
    );

    uint8_t declared_channel(const char *p_name) const;

    godot::Node *entity_component_node(
        const godot::RID &p_entity,
        int64_t p_comp
    ) const;

    void relay_bare(const godot::StringName &p_signal);
    void relay_one(
        const godot::Variant &p_first,
        const godot::StringName &p_signal
    );
    void relay_two(
        const godot::Variant &p_first,
        const godot::Variant &p_second,
        const godot::StringName &p_signal
    );

    void relay_auth_failed(int64_t p_peer);

    void session_broadcast_control(
        uint8_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    void session_after_entered();
    void session_after_edge(int64_t p_old, int64_t p_new);

    void scene_answer_settled_result(
        int64_t p_peer,
        int p_request_id,
        const godot::Ref<NetwPromise> &p_operation
    );

protected:
    static void _bind_methods();

public:
    NetwMultiplayerCore();
    ~NetwMultiplayerCore() override;

    godot::Error NETW_API_VIRTUAL(poll)() override;
    void NETW_API_VIRTUAL(set_multiplayer_peer)(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) override;
    godot::Ref<godot::MultiplayerPeer> NETW_API_VIRTUAL(get_multiplayer_peer)(
    ) override;
    int32_t NETW_API_VIRTUAL(get_unique_id)() NETW_API_CONST override;
    godot::PackedInt32Array NETW_API_VIRTUAL(get_peer_ids)(
    ) NETW_API_CONST override;

    int32_t NETW_API_VIRTUAL(get_remote_sender_id)() NETW_API_CONST override;
    godot::Error NETW_API_VIRTUAL(object_configuration_add)(
        godot::Object *p_object,
        NETW_API_CONFIG_ARG p_configuration
    ) override;
    godot::Error NETW_API_VIRTUAL(object_configuration_remove)(
        godot::Object *p_object,
        NETW_API_CONFIG_ARG p_configuration
    ) override;

#if defined(NETW_MODULE)
    godot::Error rpcp(
        godot::Object *p_object,
        int p_peer_id,
        const godot::StringName &p_method,
        const godot::Variant **p_args,
        int p_argcount
    ) override;
#else
    godot::Error _rpc(
        int32_t p_peer_id,
        godot::Object *p_object,
        const godot::StringName &p_method,
        const godot::Array &p_args
    ) override;
#endif

    void set_peer_ids(const godot::PackedInt32Array &p_peer_ids);

    godot::Ref<NetwLivenessCore> get_liveness_core() const;
    godot::Ref<NetwClockHandle> get_clock_handle() const;
    godot::Ref<NetwSceneCore> get_scene_core() const;
    SessionCore &session_plane();
    JoinRoster &join_book();
    void session_announce_entered();
    void session_announce_ended();
    void session_announce_edge(int p_old, int p_new);
    godot::Ref<NetwDisplayBook> get_display_book() const;
    godot::Ref<NetwLagCompCore> get_lagcomp_core() const;
    godot::Ref<NetwPredictionEngine> get_prediction_engine() const;
    godot::Ref<NetwChannelBook> get_channel_book() const;

    InterestEngine &interest_plane();
    void reset_interest();

    void interest_sync_record(godot::Object *p_wrapper);

    void set_scene_refresh(const godot::Callable &p_refresh);

    void set_interest_flush(const godot::Callable &p_flush);
    void set_interest_compat_refresh(const godot::Callable &p_refresh);
    void set_interest_awareness_send(const godot::Callable &p_send);
    void set_interest_visibility_sweep(const godot::Callable &p_sweep);
    godot::Error interest_recompute();
    void interest_commit();
    godot::Error interest_flush_tail();
    void interest_sync_live_peers();
    godot::Array interest_resolved_layer_ids(
        const godot::Ref<NetwEntity> &p_entity
    );
    godot::TypedArray<godot::Object> interest_shared_entities(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::StringName &p_layer_id
    );
    godot::Dictionary interest_monitor_snapshot();
    bool interest_wire_admits(
        int64_t p_peer_id,
        const godot::Ref<NetwEntity> &p_entity
    );
    bool interest_entity_has_filter(const godot::Ref<NetwEntity> &p_entity);
    bool interest_has_committed_intent(const godot::Ref<NetwEntity> &p_entity);
    godot::PackedInt64Array interest_committed_row(
        const godot::Ref<NetwEntity> &p_entity
    );
    bool interest_bit_admits(
        const godot::Ref<NetwEntity> &p_entity,
        int p_peer_bit
    );
    godot::String interest_explain_bit(
        const godot::Ref<NetwEntity> &p_entity,
        int p_peer_bit
    );
    void interest_forget_layer_row(const godot::StringName &p_layer_id);
    int interest_peer_bit(int64_t p_peer_id) const;
    bool interest_has_layer(const godot::StringName &p_layer_id) const;
    void interest_sync_scene_membership(const godot::Ref<NetwEntity> &p_entity);
    void interest_peer_connected(int64_t p_peer_id);
    void interest_peer_disconnected(int64_t p_peer_id);
    void interest_session_ended();
    void interest_clear_session();
    godot::PackedInt64Array interest_known_peers() const;
    void interest_set_entity_intent(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedInt64Array &p_admitted
    );
    static godot::StringName interest_flush_key();
    void interest_request_flush();
    bool interest_flush_pending() const;

    void set_clock_mismatch_action(int p_action);
    void clock_request_handshake();
    void clock_send_ping();
    void clock_receive_handshake(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void clock_receive_handshake_reply(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void clock_receive_ping(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void clock_receive_pong(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );

    void interest_awareness_queue_layer(
        int64_t p_peer_id,
        int64_t p_route,
        const godot::StringName &p_layer_id,
        int p_kind
    );
    void interest_awareness_queue_observer(
        int64_t p_peer_id,
        int64_t p_route,
        const godot::StringName &p_layer_id,
        int64_t p_observer_peer,
        int p_kind
    );
    godot::Array interest_awareness_drain();
    void interest_awareness_forget(int64_t p_peer_id);
    void interest_awareness_clear();

    void set_display_role_resolver(const godot::Callable &p_resolver);
    void set_display_chase_clamp(const godot::Callable &p_clamp);
    void set_display_spec_reader(const godot::Callable &p_reader);
    void set_display_lane(const godot::Callable &p_lane);
    void set_display_sync_intervals(const godot::Callable &p_compute);
    void set_display_authors_streams(const godot::Callable &p_authors);
    void set_display_role_facts(const godot::Callable &p_facts);
    void set_display_chase_hook(const godot::Callable &p_hook);

    void display_resolve_role(
        const godot::Ref<NetwDisplayRuntime> &p_runtime
    );

    void display_bind_session();
    godot::Ref<NetwDisplayDecl> display_config_for(
        const godot::Ref<NetwEntity> &p_entity
    );
    int64_t display_route_of(const godot::Ref<NetwEntity> &p_entity);
    godot::Ref<NetwDisplayRuntime> display_runtime_for(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void display_on_entity_live(int64_t p_route, godot::Object *p_entity);
    void display_on_entity_dead(int64_t p_route);
    void display_release_hooks(
        const godot::Ref<NetwDisplayRuntime> &p_runtime
    );
    void display_clear_runtimes();
    void display_on_control_changed(
        int64_t p_previous,
        int64_t p_peer,
        int64_t p_route
    );
    void display_on_reparented(const godot::Variant &p_opts, int64_t p_route);
    void display_mark_role_dirty(const godot::RID &p_entity);
    void display_drain_dirty();
    void display_on_book_dirty(const godot::RID &p_entity, int p_dirt);
    void display_record(
        godot::Node *p_node,
        const godot::StringName &p_target_property,
        const godot::Variant &p_value,
        int64_t p_tick,
        const godot::Ref<NetwInterpolate> &p_spec,
        bool p_authoring_tick
    );
    void display_on_clock_tick(double p_delta, int64_t p_tick);
    godot::Error display_pump(double p_delta);

    bool display_wants_runtime(godot::Node *p_owner) const;
    void display_rebuild_runtime(
        const godot::Ref<NetwDisplayRuntime> &p_runtime
    );
    godot::Ref<NetwDisplayChannel> display_ensure_state(
        const godot::Ref<NetwDisplayRuntime> &p_runtime,
        godot::Node *p_node,
        const godot::StringName &p_source_prop,
        const godot::StringName &p_target_prop,
        const godot::Ref<NetwInterpolate> &p_spec,
        bool p_authoring_tick
    );
    bool display_authors_streams(
        const godot::Ref<NetwDisplayRuntime> &p_runtime
    ) const;

    void display_pump_runtime(
        const godot::Ref<NetwDisplayRuntime> &p_runtime,
        const godot::Ref<NetwDisplayTiming> &p_timing,
        const godot::Ref<NetwPumpStats> &p_stats
    );
    double display_chase_smooth_time(
        const godot::Ref<NetwDisplayRuntime> &p_runtime,
        const godot::Ref<NetwDisplayTiming> &p_timing
    ) const;
    godot::Error display_pump_entity(
        const godot::RID &p_entity,
        const godot::Ref<NetwDisplayTiming> &p_timing
    );
    void display_absorb_recovery(
        const godot::Ref<NetwDisplayRuntime> &p_runtime,
        const godot::Dictionary &p_deltas,
        bool p_teleported
    );

    godot::Error scene_despawn(const godot::RID &p_scene, int p_drain_pumps);

    godot::Ref<NetwPromise> scene_request(
        bool p_is_path,
        const godot::Variant &p_destination,
        const godot::Array &p_args,
        bool p_from_capture
    );
    godot::Ref<NetwPromise> scene_request_open(
        bool p_is_path,
        const godot::Variant &p_destination,
        const godot::Array &p_args,
        bool p_from_capture,
        double p_deadline
    );
    void scene_request_expire(int p_request_id);
    void set_request_deadline_arm(const godot::Callable &p_arm);

    bool scene_receive_result_frame(
        const godot::PackedByteArray &p_payload,
        int p_sender
    );
    void scene_send_result(int64_t p_peer, int p_request_id, int p_code);
    void scene_answer_when_settled(
        const godot::Ref<NetwPromise> &p_operation,
        int64_t p_peer,
        int p_request_id
    );

    void set_scene_mark_reader(const godot::Callable &p_reader);
    godot::Ref<NetwSceneMark> scene_mark_of(
        const godot::Ref<godot::Script> &p_script
    ) const;

    void set_scene_path_reader(const godot::Callable &p_reader);
    bool scene_request_targets(
        const godot::Variant &p_destination,
        const godot::StringName &p_label
    );

    void scene_set_request_handler(const godot::Callable &p_handler);
    bool set_request_reach(int p_reach);
    int get_request_reach() const;

    godot::RID get_current_scene() const;
    godot::RID scene_named(const godot::StringName &p_stem) const;
    godot::Array scenes_named(const godot::StringName &p_stem) const;
    godot::Array live_scenes() const;

    void scene_observe(
        const godot::RID &p_scene,
        int p_event,
        const godot::Callable &p_callback
    );
    void scene_unobserve(
        const godot::RID &p_scene,
        int p_event,
        const godot::Callable &p_callback
    );

    godot::RID layer_open(const godot::StringName &p_name);
    godot::RID layer_named(const godot::StringName &p_name) const;
    godot::StringName layer_name_of(const godot::RID &p_layer) const;
    godot::Ref<godot::RefCounted> layer_view(const godot::RID &p_layer) const;
    void layer_close(const godot::RID &p_layer);
    void layer_forget_all();

    godot::Ref<NetwInterestLayer> interest_layer(
        const godot::StringName &p_name
    );
    godot::Ref<NetwInterestLayer> interest_layer_named(
        const godot::StringName &p_name
    ) const;
    godot::Array interest_layers() const;


    void interest_track_lifecycle(const godot::Ref<NetwEntity> &p_entity);
    void interest_untrack_lifecycle(const godot::Ref<NetwEntity> &p_entity);
    void interest_retire_entity(const godot::Ref<NetwEntity> &p_entity);
    godot::Dictionary interest_leave_resolve(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id
    );
    void interest_leave_commit(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id,
        const godot::Dictionary &p_decision,
        bool p_forced
    );
    void interest_leave_finish_sweep();
    void interest_on_entity_exiting(const godot::Ref<NetwEntity> &p_entity);

    bool interest_can_send_to(int64_t p_peer_id);
    void interest_receive_awareness(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void interest_apply_awareness(
        int p_edge_type,
        int64_t p_route,
        const godot::StringName &p_layer_id,
        int64_t p_observer_peer,
        int p_kind
    );
    void interest_apply_delta(const InterestDelta &p_delta);
    int64_t interest_local_participant();
    void interest_refresh_perception(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::Array &p_layer_ids
    );
    void interest_refresh_all_perception();
    void interest_reapply_perception(const godot::Ref<NetwEntity> &p_entity);
    void interest_clear_perception(
        const godot::Ref<NetwEntity> &p_entity,
        bool p_restore
    );
    void interest_clear_all_perception();

    bool interest_participant_sees(
        int64_t p_peer_id,
        const godot::Ref<NetwEntity> &p_entity
    );

    bool interest_has_filter(const godot::RID &p_entity);

    godot::Array interest_membership_ids(const godot::RID &p_entity);

    void set_persistence_quit_guard(const godot::Callable &p_guard);
    void set_persistence_drain(const godot::Callable &p_drain);

    bool persistence_serves();
    godot::Ref<NetwPersistenceEngine> persistence_engine_for(
        godot::Object *p_entity
    );
    void persistence_tick(double p_delta);
    void persistence_flush_all();
    void persistence_owner_exiting(const godot::RID &p_entity);
    godot::TypedArray<NetwPersistenceEngine> persistence_live_engines();
    void persistence_shutdown();

    SessionState get_state() const;
    Role get_role() const;
    bool is_online() const;
    bool is_host() const;
    bool is_local_client() const;

    void count_sent(int64_t p_bytes);
    void count_received(int64_t p_bytes);
    void count_state_ack_out();
    void count_state_ack_in();
    void count_standalone_ack_out();

    int64_t get_sent_packets() const;
    int64_t get_sent_bytes() const;
    int64_t get_received_packets() const;
    int64_t get_received_bytes() const;
    int64_t get_state_acks_out() const;
    int64_t get_state_acks_in() const;
    int64_t get_standalone_acks_out() const;

    double poll_delta(int64_t p_now_usec);

    void advance_frame();
    int64_t get_frame_counter() const;

    void set_inner(const godot::Ref<godot::SceneMultiplayer> &p_inner);
    godot::Ref<godot::SceneMultiplayer> get_inner() const { return inner; }

    void set_session_root(const godot::Callable &p_reader);
    godot::Node *session_root() const;

    void set_desired_role_reader(const godot::Callable &p_reader);
    void set_identity_reader(const godot::Callable &p_reader);
    godot::Ref<godot::RefCounted> participant_identity(int64_t p_peer) const;
    Role authored_desired_role() const;

    bool presents_as_listen_host() const;

    int64_t datagram_budget() const;

    bool channel_aggregates(int64_t p_channel, bool p_requested) const;

    static godot::PackedByteArray frame_pack(
        int64_t p_route,
        int64_t p_comp,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        const godot::String &p_path
    );

    godot::Error send_to(
        int64_t p_peer,
        int64_t p_route,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        bool p_reliable,
        int64_t p_comp,
        const godot::String &p_path,
        bool p_batched
    );

    int64_t send_datagram(
        int64_t p_peer,
        const godot::PackedByteArray &p_payload,
        bool p_reliable
    );

    godot::Ref<NetwCarrierDatagram> frame_datagram(
        int64_t p_peer,
        const godot::PackedByteArray &p_payload,
        bool p_reliable
    );

    int64_t carrier_append(
        int64_t p_peer,
        const godot::PackedByteArray &p_frame,
        bool p_reliable
    );

    godot::PackedInt64Array carrier_flush();

    void carrier_clear();
    int64_t carrier_pending(int64_t p_peer, bool p_reliable) const;

    static bool seq_is_fresher(int64_t a, int64_t b);
    int64_t next_send_seq(int64_t p_peer);
    bool has_inbound_seq(int64_t p_peer) const;
    int64_t inbound_seq(int64_t p_peer) const;
    bool note_inbound_seq(int64_t p_peer, int64_t p_seq);
    bool note_peer_ack(int64_t p_peer, int64_t p_ack);
    int64_t peer_ack(int64_t p_peer) const;
    void note_echoed_seq(int64_t p_peer, int64_t p_seq);
    godot::PackedInt64Array peers_owed_echo() const;
    void forget_peer_seqs(int64_t p_peer);
    void clear_seq_books();

    static bool counts_verdict(int64_t p_verdict);
    bool count_verdict(int64_t p_verdict, int64_t p_route = 0);
    bool connect_once(
        godot::Signal p_signal,
        const godot::Callable &p_callback,
        int64_t p_flags = 0
    );
    int64_t verdict_total(int64_t p_verdict) const;
    bool claim_verdict_warning(int64_t p_verdict, int64_t p_route);
    bool warn_verdict(int64_t p_verdict, int64_t p_route = 0);
    int64_t sink_verdict(int64_t p_verdict, int64_t p_route);
    int64_t stage_verdict(
        int64_t p_stage,
        int64_t p_verdict,
        int64_t p_route
    );
    void clear_verdicts();

    void settle_schedule(
        const godot::Callable &p_fn,
        const godot::StringName &p_key
    );
    void settle_schedule_after(
        const godot::Callable &p_fn,
        const godot::StringName &p_key,
        int p_pumps
    );
    void settle_advance();
    void settle_cancel(const godot::StringName &p_key);
    godot::PackedStringArray settle_drain();
    void settle_clear();
    int settle_pending() const;
    bool settle_has_key(const godot::StringName &p_key) const;
    static int settle_max_passes();

    godot::Ref<SchemaCore> get_schema_core() const;

    godot::RID schema_create(const godot::StringName &p_name);
    int schema_add_column(
        const godot::RID &p_schema,
        const godot::StringName &p_key,
        int p_type,
        int p_stride
    );
    void schema_set_column_quantizer(
        const godot::RID &p_schema,
        int p_column,
        const godot::Ref<NetwQuantize> &p_quantizer
    );
    godot::Error schema_seal(const godot::RID &p_schema);
    godot::RID schema_find(const godot::StringName &p_name) const;
    int schema_get_hash(const godot::RID &p_schema) const;
    int schema_get_column_count(const godot::RID &p_schema) const;
    godot::StringName schema_get_column_key(
        const godot::RID &p_schema,
        int p_column
    ) const;
    int schema_get_column_type(const godot::RID &p_schema, int p_column) const;
    int schema_get_column_stride(const godot::RID &p_schema, int p_column)
        const;

    godot::Ref<TableCore> get_table_core() const;

    godot::RID table_create(const godot::RID &p_schema);
    godot::RID table_get_schema(const godot::RID &p_table) const;
    void table_set_param(
        const godot::RID &p_table,
        int p_param,
        const godot::Variant &p_value
    );
    godot::RID table_find(const godot::StringName &p_name) const;
    int table_get_wire_hash(const godot::RID &p_table) const;
    godot::Error table_write_routes(
        const godot::RID &p_table,
        const godot::PackedInt64Array &p_routes
    );
    godot::Error table_write_column(
        const godot::RID &p_table,
        int p_column,
        const godot::Variant &p_data
    );
    godot::Error table_commit(const godot::RID &p_table);
    godot::PackedInt64Array table_read_routes(const godot::RID &p_table) const;
    godot::Variant table_read_column(const godot::RID &p_table, int p_column)
        const;
    godot::PackedInt64Array table_read_births(const godot::RID &p_table) const;
    godot::PackedInt64Array table_read_deaths(const godot::RID &p_table) const;
    int table_get_row(const godot::RID &p_table, int64_t p_route) const;
    godot::PackedInt32Array table_get_rows(
        const godot::RID &p_table,
        const godot::PackedInt64Array &p_routes
    ) const;
    int64_t table_get_tick(const godot::RID &p_table) const;

    void table_publish_intake();

    void table_publish(const godot::RID &p_table);

    bool session_publish_control(
        int64_t p_channel,
        int64_t p_sender,
        const godot::PackedByteArray &p_payload
    );

    void set_session_join_handler(const godot::Callable &p_handler);
    void set_session_join_resolver(const godot::Callable &p_resolver);
    void set_session_entered_hook(const godot::Callable &p_hook);
    void set_session_edge_hook(const godot::Callable &p_hook);
    void session_admit(const godot::Ref<ResolvedJoin> &p_join);

    void session_receive_join(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void session_receive_accept(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void session_receive_roster(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void session_receive_control(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender,
        int64_t p_channel
    );

    godot::Ref<ResolvedJoin> session_accepted_join(int64_t p_peer) const;
    godot::Array session_accepted_joins() const;
    bool session_remember_join(const godot::Ref<ResolvedJoin> &p_join);
    void session_forget_peer(int64_t p_peer);
    void session_clear_roster();
    void session_refuse(int64_t p_peer, const godot::String &p_reason);
    godot::String session_refusal(int64_t p_peer) const;
    int session_name_verdict(
        const godot::StringName &p_name,
        const godot::PackedStringArray &p_taken,
        bool p_is_debug,
        bool p_has_identity
    ) const;
    godot::StringName session_free_name(
        const godot::StringName &p_name,
        const godot::PackedStringArray &p_taken
    ) const;

    void session_set_state(int p_state);
    void session_set_role(int p_role);
    void session_set_desired_role(int p_role);
    void session_transition(int p_state);
    void session_peer_assigned(
        bool p_live,
        bool p_connected,
        int p_unique_id
    );
    void session_resolve_online(int p_unique_id);
    void session_set_advertised_max_players(int p_cap);
    int session_advertised_max_players() const;
    static int64_t session_app_tag(const godot::StringName &p_app_id);

    void session_pause(const godot::String &p_reason);
    void session_unpause();
    void session_notify_shutdown(const godot::String &p_reason);
    void session_request_leave(const godot::String &p_reason);

    void session_kick(int64_t p_peer_id, const godot::String &p_reason);
    void session_request_kick(
        int64_t p_peer_id,
        const godot::String &p_reason
    );

    godot::Ref<NetwCarrierFrame> receive_header(
        int64_t p_peer,
        const godot::PackedByteArray &p_packet
    );

    static constexpr int EFFECT_TIMEOUT_TICKS = 120;

    godot::Ref<NetwEffectLedger> get_effect_ledger() const;

    void effect_arm(
        const godot::StringName &p_key,
        const godot::Callable &p_revert,
        int p_timeout_ticks
    );
    bool effect_watch(
        const godot::StringName &p_key,
        const godot::Callable &p_confirmed,
        const godot::Callable &p_denied
    );
    void effect_adopt(const godot::StringName &p_key);
    void effect_discard(const godot::StringName &p_key);
    bool effect_pending(const godot::StringName &p_key) const;
    int64_t effect_count() const;
    void effect_sweep(int64_t p_tick);

    int64_t event_watch(
        const godot::PackedInt64Array &p_events,
        const godot::Dictionary &p_target,
        const godot::Dictionary &p_predicate,
        const godot::Callable &p_sink,
        const godot::Dictionary &p_opts
    );
    bool event_unwatch(int64_t p_id);
    godot::Array event_watches() const;
    godot::Array event_ring(int64_t p_route);
    void event_ring_clear(int64_t p_route);
    void event_arm(bool p_enabled);
    bool event_wants(int64_t p_event, int64_t p_route) const;

    void event_emit(
        int64_t p_event,
        int64_t p_route,
        const godot::Dictionary &p_detail,
        const godot::StringName &p_entity_id,
        int64_t p_peer,
        int64_t p_verdict,
        const godot::Dictionary &p_model
    );

    godot::Error spawn_admit_frame_default(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    godot::Error table_admit_frame_default(
        int64_t p_sender,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    void service_register(godot::Object *p_type, godot::Object *p_service);

    void service_unregister(godot::Object *p_type, godot::Object *p_service);

    godot::Object *service_of(godot::Object *p_type) const;
    godot::Variant service_held(godot::Object *p_type) const;

    godot::TypedArray<godot::Object> service_all(godot::Object *p_base) const;

    void service_clear();

    void wrapper_adopt(
        const godot::RID &p_entity,
        const godot::Ref<godot::RefCounted> &p_wrapper,
        godot::Object *p_owner
    );

    godot::RID entity_at_or_above(godot::Object *p_node) const;

    static godot::StringName wrapper_meta();

    static void set_wrapper_factory(const godot::Callable &p_factory);
    static bool has_wrapper_factory();
    static void clear_wrapper_factory();
    static godot::Callable wrapper_factory();

    static godot::Ref<godot::RefCounted> wrapper_at(godot::Object *p_node);
    static godot::Ref<godot::RefCounted> wrapper_ensure(godot::Object *p_root);
    static godot::Ref<godot::RefCounted> wrapper_resolve(godot::Object *p_node);
    static godot::Variant wrapper_held(
        godot::Object *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );

    static godot::Object *wrapper_bind(
        godot::Object *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );

    godot::RID entity_parent_of(const godot::RID &p_entity) const;

    godot::RID entity_scene_of(const godot::RID &p_entity) const;

    godot::Ref<godot::RefCounted> scene_handle_of(const godot::RID &p_entity);

    godot::RID scene_report_entity_edge(
        const godot::RID &p_subject,
        bool p_present,
        bool p_is_player
    );

    godot::Ref<godot::RefCounted> entity_scene_facet(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_wrapper
    );

    void scene_publish_live(
        int64_t p_route,
        const godot::RID &p_container,
        const godot::String &p_name
    );

    bool entity_reparent_crosses(
        const godot::RID &p_entity,
        const godot::RID &p_destination
    );

    void entity_reparent(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner,
        godot::Object *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    static void entity_move(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner,
        godot::Object *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    void set_spawn_state_gather(const godot::Callable &p_gather);

    void set_replication_plane(godot::Object *p_plane);

    void set_sync_pipeline(godot::Object *p_pipeline);
    godot::Object *sync_pipeline() const;

    godot::Error sync_send_property(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::StringName &p_property
    );

    godot::Error sync_send_signal(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::StringName &p_signal,
        const godot::Array &p_args
    );

    void entity_control_request(const godot::RID &p_entity);

    void set_spawn_pipeline(godot::Object *p_pipeline);
    godot::Object *spawn_pipeline() const;

    godot::RID spawn_replicate(godot::Object *p_node, godot::Object *p_owner);

    godot::RID spawn_function(
        const godot::Callable &p_function,
        const godot::Array &p_args,
        godot::Object *p_owner
    );

    void spawn_register_constructor(
        const godot::StringName &p_id,
        const godot::Callable &p_function,
        const godot::Array &p_arg_types = godot::Array(),
        const godot::Array &p_quantizers = godot::Array()
    );

    godot::RID spawn_registered(
        const godot::StringName &p_id,
        const godot::Array &p_args,
        godot::Object *p_owner
    );

    godot::RID spawn_adopt(godot::Object *p_root);

    godot::TypedArray<godot::Dictionary> spawn_state_of(
        const godot::RID &p_entity
    );

    static void entity_enter_tree(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_session,
        bool p_is_authority
    );

    static void entity_request_control(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        godot::Object *p_plane
    );

    static void entity_broadcast_control(
        godot::Object *p_wrapper,
        godot::Object *p_plane,
        int64_t p_peer
    );
    godot::Object *replication_plane() const;

    godot::Ref<godot::RefCounted> entity_derived_binding(
        godot::Object *p_owner,
        int64_t p_record,
        int64_t p_route
    );
    godot::Array entity_derived_group(int64_t p_route);

    bool entity_governs_property(
        godot::Object *p_owner,
        const godot::NodePath &p_path,
        godot::Object *p_exclude,
        int64_t p_route
    );
    godot::Callable spawn_state_gather() const { return spawn_state; }

    godot::Node *entity_instantiate_from(
        godot::Object *p_template,
        const godot::Callable &p_configure
    );

    static godot::Node *entity_instantiate_copy(
        godot::Object *p_template,
        const godot::Callable &p_configure
    );

    godot::Node *entity_spawn_under(
        godot::Object *p_owner,
        godot::Object *p_parent,
        const godot::StringName &p_id
    );

    static godot::Node *entity_spawn_copy_under(
        godot::Object *p_owner,
        godot::Object *p_parent,
        const godot::StringName &p_id
    );

    godot::Node *entity_instantiate_player(
        godot::Object *p_owner,
        godot::Object *p_participant
    );

    godot::Node *entity_spawn_player(
        godot::Object *p_owner,
        godot::Object *p_participant,
        godot::Object *p_scene
    );

    void entity_linger(
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner,
        int64_t p_pumps
    );

    void entity_free_owner(godot::Object *p_owner);

    void entity_settle_reparented(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    godot::Dictionary entity_describe(int64_t p_route) const;

    void entity_note_stage(
        const godot::Ref<NetwEntityRecord> &p_record,
        int64_t p_from
    );

    void entity_announce_reparented(
        godot::Object *p_wrapper,
        godot::Object *p_owner,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    bool scene_request_flooded(int peer, int64_t now_msec);
    godot::Array scene_request_frame_row(
        const godot::PackedByteArray &p_payload,
        int p_sender,
        int64_t p_now_msec
    );
    godot::RID scene_released_seat(
        const godot::PackedByteArray &p_payload,
        int p_sender
    );
    int scene_capture_verdict(const godot::String &p_path);
    static int scene_destination_kind(const godot::Variant &p_destination);
    static godot::Ref<godot::PackedScene> scene_packed_at(
        const godot::String &p_reference
    );
    static godot::Ref<godot::Script> scene_packed_root_script(
        const godot::Ref<godot::PackedScene> &p_packed
    );
    static godot::Ref<godot::Script> scene_root_script_at(
        const godot::String &p_reference
    );
    static godot::String scene_resolve_requested_path(
        const godot::String &p_reference
    );
    static int scene_move_verdict(
        bool p_mover_live,
        bool p_target_live,
        bool p_same_scene
    );
    static bool scene_native_change_strands(bool p_online, bool p_marked);
    static godot::StringName scene_container_meta();
    static godot::Node *scene_build_container(bool p_hosting, bool p_own_world);
    static godot::StringName scene_packed_stem(
        const godot::Ref<godot::PackedScene> &p_packed
    );
    godot::Node *scene_container(const godot::StringName &p_stem) const;
    godot::Node *scene_existing_destination(
        const godot::Variant &p_destination
    );
    static void scene_install_level(
        godot::Object *p_container,
        godot::Object *p_level
    );

    godot::Node *scene_spawn_node(
        const godot::Variant &p_data,
        int p_isolation
    );

    static godot::StringName scene_constructor_id();
    static godot::Node *scene_level_of(godot::Object *p_container);
    bool scene_owns_its_world(const godot::RID &p_scene) const;
    bool scene_hosts_isolated_world() const;
    godot::Node *scene_containing(godot::Object *p_node);
    godot::Node *scene_spawn(const godot::Variant &p_data, int p_isolation);
    godot::Node *scene_spawn_declared(const godot::StringName &p_stem);
    void scene_spawn_initial();
    godot::Node *scene_activate(const godot::Variant &p_destination);
    godot::Node *scene_activate_named(const godot::StringName &p_stem);
    bool scene_freeze(const godot::StringName &p_stem);
    bool scene_destroy(const godot::StringName &p_stem);
    bool scene_retire_named(
        const godot::StringName &p_stem,
        int p_drain_pumps
    );
    void scene_pump_retired();
    void scene_forget(godot::Object *p_container);
    void scene_settle_refresh();

    static godot::NodePath relative_path(
        godot::Object *p_source,
        godot::Object *p_target
    );

    static godot::NodePath property_path(
        godot::Object *p_source,
        const godot::StringName &p_property,
        godot::Object *p_base
    );

    godot::Ref<godot::RefCounted> wrapper_of(const godot::RID &p_entity) const;

    godot::Object *wrapper_owner(const godot::RID &p_entity) const;
    godot::RID handle_of_wrapper(godot::Object *p_wrapper) const;
    godot::Ref<godot::RefCounted> wrapper_for_route(int64_t p_route) const;

    godot::Ref<godot::RefCounted> wrapper_for_id(int64_t p_id) const;

    godot::TypedArray<godot::Object> wrapper_live() const;

    void wrapper_sweep_retired();
    void wrapper_clear();

    bool liveness_bind(
        const godot::RID &p_entity,
        int64_t p_route,
        const godot::Ref<godot::RefCounted> &p_wrapper,
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner
    );

    godot::Error liveness_adopt_route(
        int64_t p_route,
        const godot::Ref<godot::RefCounted> &p_wrapper,
        const godot::Ref<NetwEntityRecord> &p_record,
        godot::Object *p_owner
    );

    void liveness_publish_live(int64_t p_route);

    void liveness_settle_local_player(int64_t p_route);

    godot::Ref<godot::RefCounted> participant_ensure(int64_t p_peer);
    void participant_adopt(
        int64_t p_peer,
        const godot::Ref<godot::RefCounted> &p_participant
    );
    godot::Ref<godot::RefCounted> participant_of(int64_t p_peer) const;
    bool participant_has(int64_t p_peer) const;
    godot::TypedArray<godot::Object> participant_all() const;
    void participant_forget(int64_t p_peer);
    void participant_clear();

    godot::RID participant_seat(int64_t p_peer) const;
    bool participant_take_seat(int64_t p_peer, const godot::RID &p_scene);
    bool participant_leave_seat(int64_t p_peer, const godot::RID &p_scene);
    bool participant_seat_move(int64_t p_peer, const godot::RID &p_scene);
    bool participant_seat_clear(int64_t p_peer, const godot::RID &p_scene);
    bool participant_move_seat(int64_t p_peer, const godot::RID &p_scene);
    godot::PackedInt64Array participant_seated_in(const godot::RID &p_scene
    ) const;

    bool participant_admit(int64_t p_peer);
    godot::Ref<godot::RefCounted> participant_admitted_of(int64_t p_peer
    ) const;
    godot::TypedArray<godot::Object> participant_admitted_all() const;
    godot::Ref<godot::RefCounted> participant_admitted_local();

    void participant_publish_joined(int64_t p_peer);

    bool liveness_linger(const godot::RID &p_entity);

    bool liveness_retire(int64_t p_route);

    int64_t liveness_reserve_route();

    int64_t liveness_allocate_route(godot::Object *p_wrapper);

    bool liveness_bind_route(int64_t p_route, godot::Object *p_wrapper);

    void liveness_bind_routes_data(const godot::PackedInt64Array &p_routes);
    void liveness_tombstone_routes_data(const godot::PackedInt64Array &p_routes
    );

    int64_t liveness_route_of(godot::Object *p_wrapper) const;
    int64_t liveness_state_of(godot::Object *p_wrapper) const;
    int64_t liveness_route_state(int64_t p_route) const;

    godot::RID liveness_adopt(godot::Object *p_wrapper);
    godot::RID entity_of(godot::Object *p_node);
    godot::StringName scene_stem(const godot::RID &p_scene) const;

    godot::StringName scene_layer_id(const godot::RID &p_scene) const;

    godot::Ref<godot::RefCounted> scene_layer_view(const godot::RID &p_scene
    ) const;
    godot::PackedInt32Array scene_peers(const godot::RID &p_scene) const;

    void set_scene_participant_edge(const godot::Callable &p_edge);
    godot::Error scene_admit(const godot::RID &p_scene, int64_t p_peer);
    bool scene_release(const godot::RID &p_scene, int64_t p_peer);
    bool scene_admits(const godot::RID &p_scene, int64_t p_peer) const;
    bool scene_declared(const godot::RID &p_entity) const;
    godot::Node *scene_entity_node(const godot::RID &p_entity) const;
    godot::TypedArray<godot::RID> scene_entities_under(
        const godot::RID &p_scene
    ) const;
    void collect_scene_entities(
        godot::Node *p_node,
        godot::TypedArray<godot::RID> &r_out
    ) const;
    bool scene_admit_peer(const godot::RID &p_scene, int64_t p_peer);
    bool scene_release_peer(const godot::RID &p_scene, int64_t p_peer);
    bool scene_notify_released(const godot::RID &p_scene, int64_t p_peer);
    bool scene_release_departed(
        const godot::RID &p_scene,
        const godot::RID &p_subject,
        bool p_mover_live,
        int64_t p_peer
    );

    godot::RID scene_seat_sync(int64_t p_peer);
    static godot::StringName scene_seat_clear_key(
        int64_t p_peer,
        const godot::RID &p_scene
    );
    void scene_seat_clear_deferred(int64_t p_peer, const godot::RID &p_scene);
    static godot::StringName scene_seat_release_key(
        int64_t p_peer,
        const godot::RID &p_scene
    );

    void set_scene_carry_move(const godot::Callable &p_carry);
    static godot::StringName scene_move_reason();
    godot::Ref<NetwPromise> scene_move_entity(
        const godot::RID &p_entity,
        const godot::RID &p_destination,
        const godot::Variant &p_opts
    );

    godot::Ref<NetwGroupPromise> scene_move_participants(
        const godot::RID &p_scene,
        const godot::PackedInt32Array &p_peers
    );
    void scene_report_moved(
        const godot::Ref<NetwGroupPromise> &p_batch,
        const godot::PackedInt32Array &p_peers
    );

    godot::Object *liveness_node_of(int64_t p_route) const;
    godot::TypedArray<godot::Object> liveness_live_entities() const;

    void liveness_when_live(
        int64_t p_route,
        const godot::Callable &p_callback,
        int64_t p_deadline,
        bool p_on_clock,
        const godot::Callable &p_on_timeout
    );
    int64_t liveness_pending_live_count() const;

    void liveness_poll(int64_t p_clock_tick);

    void liveness_clear_session();

    godot::Ref<godot::RefCounted> get_local_player() const;

private:
    void set_local_player(
        const godot::Ref<godot::RefCounted> &p_player,
        int64_t p_id
    );

    godot::Ref<NetwEntityRecord> record_of_wrapper(godot::Object *p_wrapper
    ) const;
    static godot::Object *owner_of_wrapper(godot::Object *p_wrapper);
    godot::Callable owner_exit_hook(godot::Object *p_wrapper);
    godot::Callable despawning_hook(godot::Object *p_wrapper);

public:
    void liveness_owner_exiting(godot::Object *p_wrapper);
    void liveness_resolve_tracked_exit(
        int64_t p_route,
        godot::Object *p_wrapper
    );
    void liveness_owner_despawning(
        const godot::StringName &p_reason,
        godot::Object *p_wrapper
    );
    void liveness_transition_dead(int64_t p_route);

public:

    static bool is_coroutine(const godot::Variant &p_value);

    bool overrides_seam(
        const godot::Ref<godot::Script> &p_script,
        const godot::StringName &p_base_name,
        const godot::StringName &p_seam
    );
    void forget_seam_overrides();

    void seam_entered(
        const godot::StringName &p_seam,
        int64_t p_event,
        int64_t p_route,
        const godot::Dictionary &p_detail
    );
    godot::Variant seam_settled(
        const godot::StringName &p_seam,
        int64_t p_event,
        int64_t p_route,
        const godot::Variant &p_result,
        const godot::Variant &p_fallback
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::TableParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::NameVerdict);
VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::SessionState);
VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::Role);
VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::SceneCapture);
VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::SceneMove);
VARIANT_ENUM_CAST(netw::NetwMultiplayerCore::SceneDestination);
