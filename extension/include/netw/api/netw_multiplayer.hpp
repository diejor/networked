#pragma once

#include <optional>

#include "godot/callable.hpp"
#include "godot/gdvirtual.hpp"
#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/ref_counted.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "godot/viewport.hpp"
#include "netw/action_gate_book.hpp"
#include "netw/api/action.hpp"
#include "netw/api/auth_flow.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/clock_handle.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/default_join.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/predict_runner.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/record.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/api/session_config.hpp"
#include "netw/api/session_handle.hpp"
#include "netw/api/session_stats.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/call_park.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/channel_book.hpp"
#include "netw/clock_engine.hpp"
#include "netw/connect/core.hpp"
#include "netw/datagram_seq_book.hpp"
#include "netw/display/book.hpp"
#include "netw/display/build.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/roles.hpp"
#include "netw/effect_ledger.hpp"
#include "netw/entity/departure.hpp"
#include "netw/gate_verdict_book.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/interest/engine.hpp"
#include "netw/interest/leave.hpp"
#include "netw/interest/perception.hpp"
#include "netw/interest/relay.hpp"
#include "netw/join_roster.hpp"
#include "netw/lagcomp_core.hpp"
#include "netw/liveness_core.hpp"
#include "netw/persist/drain.hpp"
#include "netw/persist/loop.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/relay_book.hpp"
#include "netw/predict/tap.hpp"
#include "netw/probe_guard.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/rate_window.hpp"
#include "netw/reparent_guard.hpp"
#include "netw/replication_send.hpp"
#include "netw/scene_core.hpp"
#include "netw/scene_decl.hpp"
#include "netw/session_core.hpp"
#include "netw/session_decl.hpp"
#include "netw/settle_queue.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/spawn/spawner_compat.hpp"
#include "netw/spawn/spawner_roster.hpp"
#include "netw/table/core.hpp"
#include "netw/txn_book.hpp"
#include "netw/wire/attribution.hpp"
#include "netw/wire/capture.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/stream.hpp"

namespace netw {

class NetwSceneHandle;
class ParticipantView;
class ReplicationCore;
class NetwJoinConfig;
class SyncPipeline;
class SyncCompat;
class ReplicationCore;
class NetwMultiplayer;

int64_t session_missing_local_join_warnings(const NetwMultiplayer *p_session);

class NetwMultiplayer : public MultiplayerApiBase {
    GDCLASS(NetwMultiplayer, MultiplayerApiBase)

    friend class NetwInterestHandle;
    friend class NetwNativeTests;
    friend int64_t session_missing_local_join_warnings(
        const NetwMultiplayer *p_session
    );

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

    enum TransportMode {
        TRANSPORT_MODE_HOST = connect::PEER_MODE_HOST,
        TRANSPORT_MODE_CLIENT = connect::PEER_MODE_CLIENT,
    };

    enum NameVerdict {
        NAME_ADMIT = JoinRoster::ADMIT,
        NAME_RENAME = JoinRoster::RENAME,
        NAME_REFUSE = JoinRoster::REFUSE,
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

    enum RecordKind {
        RECORD_KIND_STATE = NetwPropertySet::RECORD_STATE,
        RECORD_KIND_INPUT = NetwPropertySet::RECORD_INPUT,
        RECORD_KIND_BROADCAST = NetwPropertySet::RECORD_BROADCAST,
    };

    enum EntityState {
        ENTITY_STATE_UNKNOWN = NetwLivenessCore::STATE_UNKNOWN,
        ENTITY_STATE_LIVE = NetwLivenessCore::STATE_LIVE,
        ENTITY_STATE_LINGERING = NetwLivenessCore::STATE_LINGERING,
        ENTITY_STATE_DEAD = NetwLivenessCore::STATE_DEAD,
        ENTITY_STATE_ABSENT = NetwLivenessCore::STATE_ABSENT,
    };

    enum SyncMode {
        SYNC_MODE_SNAP = ClockEngine::SYNC_SNAP,
        SYNC_MODE_STRETCH = ClockEngine::SYNC_STRETCH,
    };

    enum MismatchAction {
        MISMATCH_ACTION_WARN = 0,
        MISMATCH_ACTION_DISCONNECT = 1,
        MISMATCH_ACTION_SIGNAL = 2,
    };

    enum ClockParam {
        CLOCK_PARAM_TICKRATE = 0,
        CLOCK_PARAM_SYNC_MODE = 1,
        CLOCK_PARAM_PING_INTERVAL = 2,
        CLOCK_PARAM_MAX_TICKS_PER_FRAME = 3,
        CLOCK_PARAM_STALL_THRESHOLD = 4,
        CLOCK_PARAM_PANIC_SNAP_THRESHOLD = 5,
        CLOCK_PARAM_STRETCH_NUDGE_FACTOR = 6,
        CLOCK_PARAM_DISPLAY_OFFSET = 7,
        CLOCK_PARAM_LEAD_TICKS = 8,
        CLOCK_PARAM_JITTER_MULTIPLIER = 9,
        CLOCK_PARAM_JITTER_WINDOW = 10,
        CLOCK_PARAM_JITTER_STABILITY_THRESHOLD = 11,
        CLOCK_PARAM_USE_PHYSICS_INTERPOLATION = 12,
        CLOCK_PARAM_ENABLE_DRIFT_LOGGING = 13,
        CLOCK_PARAM_MANUAL_TICK = 14,
        CLOCK_PARAM_TICK_FACTOR_OVERRIDE = 15,
    };

    enum ClockMonitor {
        CLOCK_MONITOR_RTT = 0,
        CLOCK_MONITOR_RTT_AVG = 1,
        CLOCK_MONITOR_RTT_JITTER = 2,
        CLOCK_MONITOR_ONE_WAY_LATENCY = 3,
        CLOCK_MONITOR_TICKTIME = 4,
        CLOCK_MONITOR_TICK_FACTOR = 5,
        CLOCK_MONITOR_TICK_PHASE = 6,
        CLOCK_MONITOR_TICK_ACCUMULATOR = 7,
        CLOCK_MONITOR_PHYSICS_FACTOR = 8,
        CLOCK_MONITOR_RECOMMENDED_DISPLAY_OFFSET = 9,
        CLOCK_MONITOR_PHYSICS_FRAMES = 10,
        CLOCK_MONITOR_POLLS = 11,
        CLOCK_MONITOR_WALL_SECONDS = 12,
        CLOCK_MONITOR_PHYSICS_HZ = 13,
        CLOCK_MONITOR_POLL_HZ = 14,
    };

    enum LayerParam {
        LAYER_PARAM_POLICY = 0,
        LAYER_PARAM_LEAVE_POLICY = 1,
        LAYER_PARAM_PERCEPTION_POLICY = 2,
    };

    enum LayerPolicy {
        LAYER_POLICY_HIDE_FROM_OUTSIDERS
        = interest::Engine::HIDE_FROM_OUTSIDERS,
        LAYER_POLICY_HIDE_FROM_INSIDERS = interest::Engine::HIDE_FROM_INSIDERS,
    };

    enum LeavePolicy {
        LEAVE_POLICY_HIDE = interest::Decl::LEAVE_HIDE,
        LEAVE_POLICY_RETAIN = interest::Decl::LEAVE_RETAIN,
        LEAVE_POLICY_CUSTOM = interest::Decl::LEAVE_CUSTOM,
    };

    enum EmbedPhase {
        EMBED_PHASE_DECLARING = 0,
        EMBED_PHASE_SETTLING = 1,
        EMBED_PHASE_LIVE = 2,
    };

    enum PerceptionPolicy {
        PERCEPTION_POLICY_HIDE = interest::Decl::PERCEPTION_HIDE,
        PERCEPTION_POLICY_SHOW = interest::Decl::PERCEPTION_SHOW,
        PERCEPTION_POLICY_CUSTOM = interest::Decl::PERCEPTION_CUSTOM,
    };

    enum TransportParam {
        TRANSPORT_PARAM_PEER_CLASS = 0,
        TRANSPORT_PARAM_DISPLAY_NAME = 1,
        TRANSPORT_PARAM_ADDRESS_LABEL = 2,
        TRANSPORT_PARAM_ADDRESS_PLACEHOLDER = 3,
        TRANSPORT_PARAM_ADDRESS_HELP = 4,
        TRANSPORT_PARAM_CAPABILITIES = 5,
        TRANSPORT_PARAM_HOST_SETTINGS = 6,
        TRANSPORT_PARAM_CLIENT_SETTINGS = 7,
    };

    enum TransportCapability {
        TRANSPORT_AVAILABLE = 1,
        TRANSPORT_CAN_HOST = 2,
        TRANSPORT_CAN_PROBE = 4,
        TRANSPORT_CAN_BROWSE = 8,
        TRANSPORT_ACCEPTS_EMPTY_ADDRESS = 16,
    };

    enum EndpointParam {
        ENDPOINT_PARAM_TRANSPORT = 0,
        ENDPOINT_PARAM_ADDRESS = 1,
        ENDPOINT_PARAM_DISPLAY_NAME = 2,
    };

    enum EndpointState {
        ENDPOINT_STATE_FLAGS = 0,
        ENDPOINT_STATE_STATUS = 1,
        ENDPOINT_STATE_INFO = 2,
    };

    enum EndpointFlags {
        ENDPOINT_FLAG_CALLER = 1,
        ENDPOINT_FLAG_AVAILABLE = 2,
        ENDPOINT_FLAG_OBSERVED = 4,
    };

    enum SceneParam {
        SCENE_PARAM_LABEL = 0,
        SCENE_PARAM_ISOLATION = 1,
        SCENE_PARAM_PROCESSING = 2,
    };

    enum SceneEvent {
        SCENE_EVENT_PARTICIPANT = 0,
        SCENE_EVENT_PLAYER = 1,
        SCENE_EVENT_ENTITY = 2,
    };

    enum SceneChange {
        SCENE_CHANGE_SESSION = NetwSceneCore::SCOPE_SESSION,
        SCENE_CHANGE_PARTICIPANT = NetwSceneCore::SCOPE_PARTICIPANT,
        SCENE_CHANGE_SCENE = NetwSceneCore::SCOPE_SCENE,
    };

    enum SceneIsolation {
        SCENE_ISOLATION_NONE = 0,
        SCENE_ISOLATION_OWN_WORLD = 1,
    };

    enum DisplayParam {
        DISPLAY_PARAM_ROLE = display::PARAM_ROLE,
        DISPLAY_PARAM_PREDICTED_MODE = display::PARAM_PREDICTED_MODE,
        DISPLAY_PARAM_PREDICTED_SMOOTH_TIME
        = display::PARAM_PREDICTED_SMOOTH_TIME,
        DISPLAY_PARAM_CHASE_GLIDE_TIME = display::PARAM_CHASE_GLIDE_TIME,
        DISPLAY_PARAM_TIMELINE_MODE = display::PARAM_TIMELINE_MODE,
        DISPLAY_PARAM_MAX_FORECAST_TICKS = display::PARAM_MAX_FORECAST_TICKS,
        DISPLAY_PARAM_SMART_DILATION = display::PARAM_SMART_DILATION,
        DISPLAY_PARAM_MAX_EXTRA_DILATION = display::PARAM_MAX_EXTRA_DILATION,
        DISPLAY_PARAM_LAG_ADAPT_RATE = display::PARAM_LAG_ADAPT_RATE,
        DISPLAY_PARAM_STARVATION_GROWTH = display::PARAM_STARVATION_GROWTH,
        DISPLAY_PARAM_FLOOR_SMOOTHING = display::PARAM_FLOOR_SMOOTHING,
        DISPLAY_PARAM_STARVATION_GRACE_FRAMES
        = display::PARAM_STARVATION_GRACE_FRAMES,
        DISPLAY_PARAM_TRACE_INTERVAL = display::PARAM_TRACE_INTERVAL,
        DISPLAY_PARAM_VISUAL_ROOT = display::PARAM_VISUAL_ROOT,
    };

    enum DisplayRole {
        DISPLAY_ROLE_AUTO = display::ROLE_AUTO,
        DISPLAY_ROLE_REMOTE = display::ROLE_REMOTE,
        DISPLAY_ROLE_PREDICTED = display::ROLE_PREDICTED,
        DISPLAY_ROLE_DISABLED = display::ROLE_DISABLED,
        DISPLAY_ROLE_AUTHORITY = display::ROLE_AUTHORITY,
    };

    enum DisplayPump {
        DISPLAY_PUMP_UNRESOLVED = display::PUMP_UNRESOLVED,
        DISPLAY_PUMP_DISABLED = display::PUMP_DISABLED,
        DISPLAY_PUMP_REMOTE = display::PUMP_REMOTE,
        DISPLAY_PUMP_BRACKETED = display::PUMP_BRACKETED,
        DISPLAY_PUMP_CHASE = display::PUMP_CHASE,
    };

    enum PredictedMode {
        PREDICTED_MODE_CHASE = display::PREDICTED_CHASE,
        PREDICTED_MODE_BRACKETED = display::PREDICTED_BRACKETED,
    };

    enum TimelineMode {
        TIMELINE_MODE_BUFFERED = display::TIMELINE_BUFFERED,
        TIMELINE_MODE_FORECAST = display::TIMELINE_FORECAST,
    };

    enum PredictParam {
        PREDICT_PARAM_ARCHETYPE = 0,
        PREDICT_PARAM_SCHEDULE = 1,
        PREDICT_PARAM_MISSING_POLICY = 2,
        PREDICT_PARAM_RECOVERY_POLICY = 3,
        PREDICT_PARAM_SNAP_RESTORE = 4,
        PREDICT_PARAM_CORRECTION_MODE = 5,
        PREDICT_PARAM_TELEPORT_THRESHOLD = 6,
        PREDICT_PARAM_DIVERGENCE_EPSILON = 7,
        PREDICT_PARAM_BREACH_RESPONSE = 8,
        PREDICT_PARAM_MAX_RESTORE_TICKS = 9,
        PREDICT_PARAM_COLLISION_COOLDOWN_TICKS = 10,
        PREDICT_PARAM_MAX_CONSUME_PER_TICK = 11,
        PREDICT_PARAM_MAX_CONSUME_LAG_TICKS = 12,
        PREDICT_PARAM_CONSUME_BUFFER_TICKS = 13,
        PREDICT_PARAM_REPLAY_BUFFER_DEPTH = 14,
    };

    enum IslandParam {
        ISLAND_PARAM_APPROXIMATE = 0,
        ISLAND_PARAM_EXACT_CLAIM = 1,
        ISLAND_PARAM_RECONCILE = 2,
        ISLAND_PARAM_PROMOTION = 3,
        ISLAND_PARAM_PROMOTION_COUNT = 4,
        ISLAND_PARAM_PROMOTION_METERS = 5,
        ISLAND_PARAM_PACING = 6,
        ISLAND_PARAM_INPUT_DELAY = 7,
    };

    enum MemberParam {
        MEMBER_PARAM_FIDELITY = 0,
        MEMBER_PARAM_PREDICTOR = 1,
    };

    enum WritePolicy {
        WRITE_POLICY_AUTHORITY = NetwMemberConfig::POLICY_AUTHORITY,
        WRITE_POLICY_CONTROLLER = NetwMemberConfig::POLICY_CONTROLLER,
        WRITE_POLICY_ANY_PEER = NetwMemberConfig::POLICY_ANY_PEER,
    };

    enum ColumnParam {
        COLUMN_PARAM_CLASS = 0,
        COLUMN_PARAM_EPSILON = 1,
        COLUMN_PARAM_TELEPORT_AT = 2,
        COLUMN_PARAM_CARRY_CHANNEL = 3,
        COLUMN_PARAM_TELEPORT_ONLY = 4,
        COLUMN_PARAM_RECONCILE_ONLY = 5,
        COLUMN_PARAM_LANE = 6,
        COLUMN_PARAM_CONVERGE_STIFFNESS = 7,
    };

    enum PropertySetParam {
        SET_PARAM_MASKED = 0,
        SET_PARAM_WINDOW = 1,
        SET_PARAM_AUDIENCE = 2,
        SET_PARAM_POLICY = 3,
        SET_PARAM_TRIGGER = 4,
        SET_PARAM_CADENCE = 5,
        SET_PARAM_STAMP = 6,
        SET_PARAM_PROFILE = 7,
        SET_PARAM_CHANNEL = 8,
        SET_PARAM_RELIABLE = 9,
    };

    enum ColumnType {
        COLUMN_F32 = SchemaCore::F32,
        COLUMN_F64 = SchemaCore::F64,
        COLUMN_I8 = SchemaCore::I8,
        COLUMN_U8 = SchemaCore::U8,
        COLUMN_I16 = SchemaCore::I16,
        COLUMN_U16 = SchemaCore::U16,
        COLUMN_I32 = SchemaCore::I32,
        COLUMN_I64 = SchemaCore::I64,
        COLUMN_BOOL = SchemaCore::BOOL,
        COLUMN_VECTOR2 = SchemaCore::VECTOR2,
        COLUMN_VECTOR3 = SchemaCore::VECTOR3,
        COLUMN_VECTOR4 = SchemaCore::VECTOR4,
        COLUMN_COLOR = SchemaCore::COLOR,
        COLUMN_QUATERNION = SchemaCore::QUATERNION,
        COLUMN_ENTITY = SchemaCore::ENTITY,
        COLUMN_VARIANT = SchemaCore::VARIANT,
    };

    static constexpr int64_t CHANNEL_USER_FIRST = 100;
    static constexpr int64_t CHANNEL_USER_LAST = 254;
    static constexpr const char *LAW_EXTENSION_ENV = "NETW_LAW_EXTENSION";
    static constexpr const char *INSTALL_AS_DEFAULT_SETTING
        = "networked/install_as_default";

    enum Stat {
#define NETW_SESSION_STAT_ENUM(m_name, m_key) STAT_##m_name,
        NETW_SESSION_STAT_TABLE(NETW_SESSION_STAT_ENUM)
#undef NETW_SESSION_STAT_ENUM
            STAT_COUNT,
    };

private:
    struct PendingAction {
        godot::NodePath target_path;
        godot::StringName method;
        godot::Variant data;
        godot::StringName key;
        int64_t view_tick = 0;
        int64_t requester = 0;
        int64_t queued_at_tick = 0;
        int timing_mode = 0;
    };

    enum ActionReadiness {
        ACTION_NOT_READY = 0,
        ACTION_READY = 1,
        ACTION_READY_BY_DEADLINE = 2,
    };

    godot::LocalVector<PendingAction> pending_actions;
    godot::LocalVector<PendingAction> actions_still_waiting;
    int64_t gate_fallbacks = 0;
    godot::HashMap<godot::String, int64_t> action_slots;

    godot::Node *node_from_tree_path(const godot::NodePath &p_path) const;
    bool can_resolve_action(const PendingAction &p_request) const;
    int action_readiness(const PendingAction &p_request, int64_t p_tick);
    int input_readiness(const PendingAction &p_request, int64_t p_tick);
    void execute_ready_action(
        const PendingAction &p_request,
        int64_t p_tick,
        int p_readiness
    );
    void execute_action(const PendingAction &p_request, int64_t p_tick);

    void predict_history_record(
        int64_t p_tick,
        int p_schedule,
        bool p_include_unregistered
    );

    godot::Ref<godot::MultiplayerPeer> assigned_peer;
    godot::Ref<godot::MultiplayerPeer> effective_peer;

    bool peer_assignment_repeats(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) const;
    godot::Ref<godot::MultiplayerPeer> peer_shaped(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) const;
    void session_submit_host_join();
    void session_submit_host_join_on_startup();

    godot::Ref<godot::SceneMultiplayer> inner;

    godot::PackedInt32Array peer_ids;
    godot::Ref<NetwLivenessCore> liveness_core;
    persist::Drain *persistence_drain = nullptr;
    JoinRoster join_roster;
    ProbeGuard probe_guard;
    session_decl::Book declaration_slots;

    struct ConfigConsumption {
        bool consumed = false;
        uint64_t generation = 0;
    };
    ConfigConsumption config_consumption[3];
    bool config_settle_queued = false;

    godot::Ref<NetwAuthFlow> auth_flow_object;
    godot::Ref<NetwAuthFlow> auth_flow_override;
    godot::Ref<NetwAuthFlow> auth_flow_declared;
    uint64_t auth_flow_generation = 0;
    bool auth_flow_broken = false;
    std::optional<JoinRequest> auth_request;
    godot::Callable auth_app_callback;
    int64_t auth_app_tag = 0;
    SessionCore session_core;
    connect::ConnectCore connect_core;
    godot::Ref<NetwConnectHandle> connection_handle;
    godot::Ref<NetwSessionHandle> session_handle;
    godot::Ref<NetwClockHandle> clock_handle;
    godot::HashMap<uint64_t, godot::RID> directory_slots;
    mutable ClockEngine clock;
    int64_t rpc_calls_deferred = 0;
    godot::PackedByteArray sync_encode_stock;
    bool sync_encode_armed = false;
    godot::Callable sync_decoder;
    godot::Ref<NetwSceneCore> scene_core;
    godot::HashMap<godot::RID, godot::Ref<NetwInterestLayer>>
        scene_admission_layers;
    godot::Ref<display::Book> display_book;
    RateWindow scene_request_window;
    interest::Engine interest_engine;
    interest::Leave interest_leave_engine;
    interest::Perception interest_perception;
    interest::Relay interest_relay;
    NetwLagCompCore lagcomp_core;
    NetwPredictionEngine prediction_engine;
    predict::Tap predict_tap;
    bool predict_tap_disarmed = false;
    int64_t predict_tap_every = 0;
    int64_t predict_tap_frame = 0;
    godot::Callable join_handler_override;
    godot::Array join_handler_override_quantizers;
    int64_t dispatching_sender = 0;
    int64_t rpc_dropped_unroutable = 0;
    int64_t rpc_dropped_not_live = 0;
    godot::HashSet<uint64_t> rpc_drop_warned;

    struct RpcSettle {
        godot::Ref<NetwPromise> single;
        godot::Ref<NetwGroupPromise> group;

        RpcSettle() = default;
        RpcSettle(const godot::Ref<NetwPromise> &p_single) : single(p_single) {
        }
        RpcSettle(const godot::Ref<NetwGroupPromise> &p_group)
            : group(p_group) {
        }
    };

    struct RpcRequest {
        godot::Ref<NetwEntity> entity;
        godot::Node *node = nullptr;
        int64_t route = 0;
        int64_t comp = 0;
        godot::String comp_path;
        godot::StringName method;
        godot::Variant method_token;
        godot::LocalVector<call_args::Slot> encoded_args;
        godot::Array quantizers;
        godot::Array arg_types;
        int64_t deadline = 0;
    };

    bool rpc_request_prepare(
        const godot::Callable &p_callable,
        const godot::Array &p_args,
        double p_timeout_seconds,
        RpcRequest &r_out
    );
    void rpc_open_txn(
        int64_t p_txn,
        const godot::PackedInt64Array &p_addressed,
        const RpcSettle &p_settle,
        int64_t p_deadline
    );
    bool rpc_sender_allowed(
        const godot::Ref<NetwMemberConfig> &p_options,
        const godot::Ref<godot::Script> &p_script,
        const godot::StringName &p_method,
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_node,
        int64_t p_sender
    ) const;
    static bool rpc_defer_signal_fired(
        godot::Node *p_node,
        const godot::StringName &p_signal
    );
    void rpc_record_interpolated_args(
        godot::Node *p_comp_node,
        const godot::Array &p_args,
        const godot::Array &p_interpolators
    );
    void rpc_execute_call(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_comp_node,
        const godot::StringName &p_method,
        const godot::Array &p_args,
        int64_t p_txn,
        int64_t p_sender
    );
    void rpc_reply_resolved(
        const godot::Variant &p_value,
        int64_t p_peer,
        int64_t p_route,
        int64_t p_txn
    );
    void rpc_reply_rejected(
        int64_t p_code,
        const godot::String &p_detail,
        int64_t p_peer,
        int64_t p_route,
        int64_t p_txn
    );
    void rpc_reply_when_settled(
        const godot::Ref<NetwPromise> &p_promise,
        int64_t p_peer,
        int64_t p_route,
        int64_t p_txn
    );
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
    NetwCarrierBuffers carrier;
    wire::AttributionBook attribution;
    wire::CaptureWriter capture;
    bool capture_decided = false;
    int64_t attribution_subject_route = 0;
    NetwChannelBook channel_book;
    wire::WireRegistry channels = wire::WireRegistry::create_default();
    struct GateChannels {
        uint8_t spawn = 0;
        uint8_t despawn = 0;
        uint8_t hide = 0;
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
    MismatchAction clock_mismatch_action = MISMATCH_ACTION_WARN;
    uint8_t session_join_channel = 0;
    uint8_t session_accept_channel = 0;
    uint8_t session_roster_channel = 0;
    uint8_t scene_request_channel = 0;
    uint8_t scene_result_channel = 0;
    uint8_t scene_released_channel = 0;
    uint8_t scene_seat_channel = 0;
    godot::HashMap<int64_t, godot::HashMap<int64_t, bool>> scene_seat_parked;
    GateVerdictBook verdict_book;
    SettleQueue settle_queue;
    NetwHandleLedger schemas;
    NetwHandleLedger property_sets;
    godot::HashMap<int64_t, godot::Ref<NetwPropertySet>> property_set_records;
    godot::HashMap<uint64_t, godot::HashMap<int, godot::RID>>
        property_set_by_script;
    godot::HashMap<int64_t, godot::RID> property_set_schema;
    godot::HashMap<int64_t, godot::HashMap<int64_t, godot::RID>>
        entity_property_sets;
    SchemaCore schema_core;
    NetwHandleLedger tables;
    godot::Ref<table::Core> table_core;
    NetwEffectLedger effects;
    EventPlane plane;
    godot::HashSet<godot::StringName> misused_seams;
    godot::HashMap<godot::StringName, bool> seam_overrides;
    godot::ObjectID seam_script;
    godot::ObjectID scene_host_view_id;
    godot::ObjectID scene_participant_display_id;
    bool scene_participant_display_pending = false;
    godot::Ref<NetwParticipant> scene_local_participant;
    bool scene_constructor_registered = false;
    godot::HashMap<godot::StringName, godot::RID> table_by_name;
    godot::HashMap<int64_t, godot::RID> table_schema;

    struct ServiceRow {
        godot::ObjectID type;
        godot::ObjectID service;
        godot::StringName class_key;
    };
    godot::LocalVector<ServiceRow> services;
    mutable godot::HashMap<uint64_t, godot::StringName> native_class_names;

    godot::HashMap<int64_t, godot::Ref<NetwEntity>> live_wrappers;
    godot::HashMap<int64_t, godot::Ref<NetwEntity>> retired_wrappers;

    struct DepartedSeat {
        godot::RID scene;
        int64_t peer = 0;
    };
    struct EntityDeparture {
        godot::RID entity;
        godot::Ref<NetwEntity> wrapper;
        godot::ObjectID owner_id;
        int64_t generation = 0;
        int64_t route = 0;
        bool terminal = false;
        bool hidden = false;
        bool received = false;
        NetwEntityRecord::MoveReport report;
        godot::Ref<NetwPersistenceEngine> saved;
        PersistedWrite saved_write;
        godot::LocalVector<DepartedSeat> seats;
    };
    godot::HashMap<uint64_t, EntityDeparture> entity_departures;
    int64_t entity_generation = 1;

    godot::HashMap<int64_t, godot::Dictionary> peer_buckets;
    godot::HashMap<int64_t, godot::Ref<NetwIdentity>> peer_identities;
    NetwCallPark rpc_park;
    NetwTxnBook rpc_txns;
    godot::HashMap<int64_t, RpcSettle> rpc_settles;
    bool rpc_disposed = false;
    void rpc_resolve_parked(int64_t p_id, const godot::Callable &p_callback);

    godot::HashMap<int64_t, NetwEntityRecord *> wrapper_records;

    godot::HashMap<int64_t, godot::ObjectID> wrapper_owners;
    godot::HashMap<uint64_t, int64_t> handle_by_wrapper;
    ReplicationCore *replication_owner = nullptr;
    godot::ObjectID session_api_id;
    godot::Callable interest_flush;
    godot::Callable staged_gatherer;
    godot::Callable staged_applier;
    godot::Callable interest_compat_refresh;
    godot::Callable interest_awareness_send;
    interest::Delta interest_pending;
    bool interest_pending_live = false;

    void interest_relay_awareness();
    godot::Callable scene_participant_edge;
    struct SceneCarry {
        godot::Ref<NetwEntity> entity;
        godot::ObjectID body;
        godot::ObjectID source;
        godot::ObjectID target;
        godot::Ref<NetwReparentOpts> opts;
        godot::Ref<NetwPromise> promise;
        godot::RID guard;
        bool moved = false;
    };
    godot::HashMap<int64_t, SceneCarry> scene_carries;
    int64_t scene_carry_next = 0;
    godot::Callable scene_carry_move;
    struct SpawnCarry {
        godot::ObjectID node;
        godot::ObjectID parent;
        godot::Callable adopt;
        godot::RID guard;
        bool moved = false;
    };
    godot::HashMap<int64_t, SpawnCarry> spawn_carries;
    int64_t spawn_carry_next = 0;
    ReparentGuard reparent_guards;
    godot::Callable spawn_constructor;
    void scene_request_arm_deadline(int p_request_id, double p_deadline);
    int scene_request_armed = -1;
    double scene_request_armed_wait = 0.0;
    EmbedPhase embed_phase_value = EMBED_PHASE_DECLARING;
    bool embed_disposing = false;
    bool embed_autosettle_pending = true;
    godot::ObjectID embed_bare_level;
    void embed_set_phase(EmbedPhase p_phase);
    void embed_autosettle_tree_default();
    void scene_adopt_bare_level(godot::Node *p_level);
    godot::Callable session_root_reader;
    mutable godot::ObjectID session_root_last_live;
    godot::Callable session_join_resolver;
    struct SessionSettings {
        godot::StringName app_id;
        int64_t desired_role = 3;
        godot::Ref<NetwLinkConditions> link_conditions;
        godot::Ref<NetwServerInfo> server_info;
    };
    SessionSettings session_settings;
    godot::Ref<NetwSessionConfig> session_fallback;
    godot::ObjectID session_fallback_source;
    int64_t session_role_constraint = -1;
    godot::Ref<NetwClockConfig> clock_fallback;
    godot::ObjectID clock_fallback_source;
    double clock_auto_config_left = 0.0;
    int64_t clock_auto_config_best = 0;
    uint64_t clock_handshake_generation = 0;
    bool clock_pump_attached = false;
    std::optional<JoinRequest> prepared_join;
    godot::Ref<NetwPromise> preparing_join;
    int64_t held_hello_peer = 0;
    std::optional<JoinRequest> resubmit_join;
    bool join_awaiting_admission = false;
    int64_t missing_local_join_warnings = 0;
    godot::Ref<NetwDefaultJoin> default_join;
    display::Hooks display_hooks;
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
        const interest::Awareness &p_edge,
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

    NetwHandleLedger layer_ledger;
    godot::HashMap<godot::StringName, godot::RID> layer_by_name;
    godot::HashMap<int64_t, godot::Ref<NetwInterestLayer>> layer_views;
    struct LayerDriverRow {
        godot::RID layer;
        godot::Callable drive;
    };
    godot::HashMap<int64_t, LayerDriverRow> layer_drivers;
    godot::HashMap<int64_t, godot::Callable> layer_monitors;
    void layer_monitor_enter(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer,
        godot::RID p_layer
    );
    void layer_monitor_exit(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer,
        godot::RID p_layer
    );
    void layer_monitor_visible(
        const godot::Ref<NetwEntity> &p_entity,
        godot::RID p_layer
    );
    void layer_monitor_hidden(
        const godot::Ref<NetwEntity> &p_entity,
        godot::RID p_layer
    );
    void layer_monitor_report(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer,
        const godot::RID &p_layer,
        bool p_inside
    );
    void layer_monitor_detach(const godot::RID &p_layer);
    struct SteppedSpace {
        godot::RID space;
        godot::Ref<NetwPhysicsStepper> stepper;
        int dimension = 0;
        bool inactive = false;
    };
    godot::HashMap<int64_t, SteppedSpace> space_steppers;
    godot::HashMap<int64_t, NetwPredictSlotEngine *> predict_engines;
    NetwPredictRunner predict_runner;
    godot::HashMap<int64_t, bool> pending_scene_facets;
    int64_t physics_frame = 0;
    bool lagcomp_configured = false;
    godot::LocalVector<godot::RID> predict_awaiting_config;
    predict::RelayBook relay_book;
    godot::HashMap<int64_t, godot::Callable> channel_handlers;

    struct DisplayTrackRow {
        int64_t comp = 0;
        godot::Ref<NetwInterpolate> spec;
    };
    godot::HashMap<int64_t, godot::HashMap<godot::StringName, DisplayTrackRow>>
        display_declarations;
    godot::HashMap<int64_t, godot::RID> display_target_items;
    godot::HashMap<int64_t, godot::Callable> display_callbacks;

    godot::Ref<NetwEntity> local_player;
    int64_t local_player_id = 0;

    struct ParticipantRow {
        godot::Ref<NetwParticipant> row;
        godot::RID seat;
        bool admitted = false;
    };
    godot::HashMap<int64_t, ParticipantRow> participants;

    int service_row(godot::Object *p_type) const;
    int service_row_named(const godot::StringName &p_class) const;
    godot::StringName class_named_by(godot::Object *p_type) const;

    interest::Facet *interest_facet_of(const godot::RID &p_entity);
    static interest::Facet *interest_facet_on(
        const godot::Ref<NetwEntity> &p_entity
    );
    interest::Decl *interest_decl_of(const godot::RID &p_entity);
    static interest::Decl *interest_decl_on(
        const godot::Ref<NetwEntity> &p_entity
    );

    void interest_join(
        const godot::RID &p_entity,
        const godot::StringName &p_layer_id
    );
    void interest_leave(
        const godot::RID &p_entity,
        const godot::StringName &p_layer_id
    );
    godot::TypedArray<godot::StringName> interest_layer_ids(
        const godot::RID &p_entity
    );
    void interest_on_enter(
        const godot::RID &p_entity,
        const godot::StringName &p_layer_id,
        const godot::Callable &p_callback
    );
    void interest_on_leave(
        const godot::RID &p_entity,
        const godot::StringName &p_layer_id,
        const godot::Callable &p_callback
    );
    void interest_on_leave_policy(
        const godot::RID &p_entity,
        const godot::StringName &p_layer_id,
        LeavePolicy p_policy,
        const godot::Callable &p_custom
    );
    void interest_on_perception_policy(
        const godot::RID &p_entity,
        const godot::StringName &p_layer_id,
        PerceptionPolicy p_policy,
        const godot::Callable &p_custom
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

    godot::Ref<NetwParticipant> local_participant;
    void bind_local_participant(const godot::Ref<NetwParticipant> &p_row);
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
    void session_warn_missing_local_join();
    void session_transport_connected();
    void session_transport_failed();
    void session_transport_dropped();

    void scene_answer_settled_result(
        int64_t p_peer,
        int p_request_id,
        const godot::Ref<NetwPromise> &p_operation
    );

public:
    struct EntitySpace {
        godot::RID space;
        int dimension = 0;
    };
    EntitySpace entity_space_of(const godot::Ref<NetwEntity> &p_entity) const;
    godot::Dictionary entity_space(const godot::RID &p_entity) const;

    struct GatedBody {
        godot::RID entity;
        EntitySpace held;
    };
    godot::LocalVector<GatedBody> gated_bodies;
    void simulation_gate_set(const godot::RID &p_entity, bool p_wanted);
    void simulation_gate_apply();
    int64_t simulation_gate_count() const;

    int declared_quantum() const;
    void physics_frame_advance();
    NetwPredictTiming frame_timing() const;
    NetwPredictTiming tick_timing(int64_t p_tick, double p_delta) const;

    godot::Ref<NetwPredictFold> predict_drive(
        int64_t p_latest_input_tick,
        int64_t p_last_driven_input_tick,
        int64_t p_frame_tick
    );
    int predict_consume(int p_depth, int p_buffer);
    godot::Ref<NetwPredictFold> predict_drive_default(
        int64_t p_latest_input_tick,
        int64_t p_last_driven_input_tick,
        int64_t p_frame_tick
    );
    int predict_consume_default(int p_depth, int p_buffer);
    godot::Ref<NetwPredictJudgement> predict_evaluate(
        NetwPredictJournal::Domain p_domain,
        NetwPredict::ExactVerdict p_verdict,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        const godot::Dictionary &p_wiring,
        const godot::Dictionary &p_field_sink
    );
    godot::Ref<NetwPredictRecovery> predict_recover(
        const godot::Dictionary &p_payload,
        NetwPredict::RecoveryPolicy p_policy,
        NetwPredict::CorrectionMode p_correction,
        NetwPredict::RestoreMode p_snap_restore,
        const godot::Dictionary &p_projection,
        const godot::Dictionary &p_current,
        const godot::Dictionary &p_pose_errors,
        const godot::Dictionary &p_wiring,
        const godot::Dictionary &p_verdict,
        double p_tick_delta
    );
    godot::Ref<NetwPredictJudgement> predict_evaluate_default(
        NetwPredictJournal::Domain p_domain,
        NetwPredict::ExactVerdict p_verdict,
        const godot::Dictionary &p_predicted,
        const godot::Dictionary &p_payload,
        const godot::Dictionary &p_wiring,
        const godot::Dictionary &p_field_sink
    );
    godot::Ref<NetwPredictRecovery> predict_recover_default(
        const godot::Dictionary &p_payload,
        NetwPredict::RecoveryPolicy p_policy,
        NetwPredict::CorrectionMode p_correction,
        NetwPredict::RestoreMode p_snap_restore,
        const godot::Dictionary &p_projection,
        const godot::Dictionary &p_current,
        const godot::Dictionary &p_pose_errors,
        const godot::Dictionary &p_wiring,
        const godot::Dictionary &p_verdict,
        double p_tick_delta
    );

    GDVIRTUAL3R(
        godot::Ref<NetwPredictFold>,
        _predict_drive,
        int64_t,
        int64_t,
        int64_t
    )
    GDVIRTUAL2R(int, _predict_consume, int, int)
    GDVIRTUAL6R(
        godot::Ref<NetwPredictJudgement>,
        _predict_evaluate,
        NetwPredictJournal::Domain,
        NetwPredict::ExactVerdict,
        godot::Dictionary,
        godot::Dictionary,
        godot::Dictionary,
        godot::Dictionary
    )
    GDVIRTUAL10R(
        godot::Ref<NetwPredictRecovery>,
        _predict_recover,
        godot::Dictionary,
        NetwPredict::RecoveryPolicy,
        NetwPredict::CorrectionMode,
        NetwPredict::RestoreMode,
        godot::Dictionary,
        godot::Dictionary,
        godot::Dictionary,
        godot::Dictionary,
        godot::Dictionary,
        double
    )

    godot::PackedByteArray sync_encode(int64_t p_peer, int64_t p_tick);
    godot::Error sync_decode(
        const godot::RID &p_entity,
        int64_t p_comp,
        int64_t p_flags,
        int64_t p_tick,
        const godot::PackedByteArray &p_payload
    );
    godot::PackedByteArray run_sync_encode_stage(
        int64_t p_peer,
        const godot::PackedByteArray &p_stock,
        int64_t p_tick
    );
    godot::Error run_sync_decode_stage(
        const godot::RID &p_entity,
        int64_t p_comp,
        int64_t p_flags,
        int64_t p_tick,
        const godot::PackedByteArray &p_payload,
        const godot::Callable &p_decoder
    );

    GDVIRTUAL2R(godot::PackedByteArray, _sync_encode, int64_t, int64_t)
    GDVIRTUAL5R(
        godot::Error,
        _sync_decode,
        godot::RID,
        int64_t,
        int64_t,
        int64_t,
        godot::PackedByteArray
    )

    godot::Error finish_stage_verdict(
        int64_t p_stage,
        godot::Error p_verdict,
        int64_t p_route
    );

protected:
    static void _bind_methods();
    bool _get(const godot::StringName &p_name, godot::Variant &r_ret) const;

public:
    NetwMultiplayer();
    ~NetwMultiplayer() override;

    godot::Error NETW_API_VIRTUAL(poll)() override;
    void NETW_API_VIRTUAL(set_multiplayer_peer)(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) override;
    godot::Ref<godot::MultiplayerPeer>
        NETW_API_VIRTUAL(get_multiplayer_peer)() override;
    int32_t NETW_API_VIRTUAL(get_unique_id)() NETW_API_CONST override;
    godot::PackedInt32Array
        NETW_API_VIRTUAL(get_peer_ids)() NETW_API_CONST override;

    int32_t NETW_API_VIRTUAL(get_remote_sender_id)() NETW_API_CONST override;
    godot::Error NETW_API_VIRTUAL(object_configuration_add)(
        godot::Object *p_object,
        NETW_API_CONFIG_ARG p_configuration
    ) override;
    godot::Error NETW_API_VIRTUAL(object_configuration_remove)(
        godot::Object *p_object,
        NETW_API_CONFIG_ARG p_configuration
    ) override;

    bool rpc_routes_through_session(
        godot::Object *p_object,
        const godot::StringName &p_method,
        const godot::Array &p_args,
        int64_t p_peer
    );

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
    ClockEngine &clock_engine() const {
        return clock;
    }
    godot::Ref<NetwSceneCore> get_scene_core() const;
    SessionCore &session_plane();
    JoinRoster &join_book();
    void session_announce_entered();
    void session_announce_ended();
    void session_announce_edge(int p_old, int p_new);
    godot::Ref<display::Book> get_display_book() const;
    NetwLagCompCore *get_lagcomp_core();
    NetwPredictionEngine *get_prediction_engine();
    NetwChannelBook *get_channel_book();

    bool predict_tap_open();
    void predict_tap_drain(
        const godot::StringName &p_entity_id,
        int64_t p_slot,
        const godot::Dictionary &p_stats
    );
    void predict_tap_pump();
    void predict_flush_tap();
    godot::Dictionary predict_get_tap_cost() const;
    void predict_tap_close();

    interest::Engine &interest_plane();
    void reset_interest();

    void interest_sync_record(NetwEntity *p_wrapper);

    void interest_flush_sink();
    void set_interest_flush(const godot::Callable &p_flush);
    void set_interest_compat_refresh(const godot::Callable &p_refresh);
    void set_interest_awareness_send(const godot::Callable &p_send);
    godot::Error interest_recompute();
    void interest_commit();
    godot::Error interest_flush_tail();
    godot::Error interest_flush_immediate();
    godot::Array gather_set(const godot::RID &p_entity, int64_t p_comp);
    godot::Array sync_gather_set_default(
        const godot::RID &p_entity,
        int64_t p_comp
    );
    godot::Error apply_set(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::Array &p_values
    );
    godot::Error sync_apply_set_default(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::Array &p_values
    );
    godot::Array run_gather_set(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::Callable &p_gatherer
    );
    godot::Error run_apply_set(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::Array &p_values,
        const godot::Callable &p_applier
    );
    void scene_watch_entity(const godot::Ref<NetwEntity> &p_entity);
    void scene_on_entity_live(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void scene_report_live_entity_edge(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::RID &p_subject,
        bool p_present
    );
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
    bool send_admits(int64_t p_peer_id, const godot::Ref<NetwEntity> &p_entity);
    godot::PackedInt32Array send_recipients(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedInt32Array &p_peers
    );
    bool spawn_locally_desired(int64_t p_peer_id, godot::Node *p_node);
    bool spawn_visible_to(
        spawn::Book *p_book,
        int64_t p_route,
        int64_t p_peer_id,
        godot::Node *p_node
    );
    godot::TypedArray<godot::Dictionary> spawn_reconcile_rows(
        spawn::Book *p_book,
        const godot::PackedInt32Array &p_peers
    );
    void spawn_reanchor(
        spawn::Record *p_record,
        godot::Node *p_node,
        spawn::SpawnerRoster *p_roster
    );
    void spawn_refresh_anchor(
        spawn::Record *p_record,
        godot::Node *p_node,
        spawn::SpawnerRoster *p_roster
    );
    void spawn_refresh_anchors(
        spawn::Book *p_book,
        spawn::SpawnerRoster *p_roster
    );
    bool spawn_despawn_route(
        spawn::Book *p_book,
        int64_t p_route,
        const godot::PackedInt32Array &p_connected,
        int64_t p_channel,
        const godot::Callable &p_undeclare
    );
    godot::Ref<NetwEntity> spawn_arm_identity(
        spawn::Record *p_record,
        godot::Node *p_node,
        const godot::Ref<NetwParticipant> &p_owner,
        const godot::Callable &p_declare
    );
    godot::PackedInt32Array rpc_get_recipients(
        const godot::Ref<NetwEntity> &p_entity
    );
    godot::Node *repl_comp_node(
        int64_t p_route,
        int64_t p_comp,
        const godot::String &p_path
    );
    void repl_note_unknown_route();
    void repl_note_gate_verdict(int64_t p_verdict);
    godot::Dictionary repl_drop_stats() const;
    godot::LocalVector<repl::RowOffer> sync_pump_offers(
        NetwSyncModel *p_model,
        ReplicationSend *p_send,
        const godot::Array &p_bindings,
        int64_t p_tick,
        const godot::Callable &p_bind,
        const godot::Callable &p_tap
    );
    void sync_flush_offers(
        ReplicationSend *p_send,
        const godot::LocalVector<repl::RowOffer> &p_offers,
        int64_t p_row_channel,
        int64_t p_window_channel,
        int64_t p_delta_channel
    );
    void sync_note_columns(const repl::RowOffer &p_offer, uint64_t p_mask);
    godot::Dictionary sync_flush_stats() const;
    godot::Dictionary sync_explain(
        int64_t p_route,
        int64_t p_comp,
        int64_t p_peer
    ) const;
    godot::Dictionary peer_link_stats(int64_t p_peer) const;
    godot::PackedInt64Array spawn_replay_to(
        spawn::Book *p_book,
        int64_t p_peer_id,
        int64_t p_channel,
        const godot::Callable &p_encode
    );
    void spawn_execute_plan(
        spawn::Book *p_book,
        const godot::Array &p_plan,
        int64_t p_spawn_channel,
        int64_t p_hide_channel,
        const godot::Callable &p_encode
    );
    spawn::Record *spawn_issue_armed(spawn::Book *p_book, int64_t p_route);
    godot::PackedInt32Array spawn_fan_out(
        spawn::Book *p_book,
        spawn::Record *p_record,
        godot::Node *p_node,
        const godot::PackedByteArray &p_payload,
        const godot::PackedInt32Array &p_connected,
        int64_t p_channel
    );
    bool spawn_applying_remote_frame() const;
    void spawn_place_node(godot::Node *p_parent, godot::Node *p_node);
    bool spawn_reparent_node(
        godot::Node *p_node,
        godot::Node *p_parent,
        const godot::Callable &p_adopt
    );
    bool spawn_send_reparent(
        spawn::Book *p_book,
        spawn::Record *p_record,
        godot::Node *p_node,
        const godot::PackedInt32Array &p_connected,
        int64_t p_channel
    );
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

    void clock_set_mismatch_action(MismatchAction p_action);
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
    void set_display_spec_override(
        const godot::LocalVector<display::SpecRow> &p_rows
    );
    int display_spec_asks() const;
    void set_display_lane(const godot::Callable &p_lane);
    void set_display_sync_intervals(const godot::Callable &p_compute);
    void set_display_authors_streams(const godot::Callable &p_authors);
    void display_compute_sync_intervals(const godot::RID &p_entity);
    bool display_default_authors_streams(const godot::RID &p_entity);
    int display_default_role(
        const godot::RID &p_entity,
        bool p_authors_streams
    );
    double display_default_chase_clamp(const godot::RID &p_entity);
    void display_default_chase_hook(const godot::RID &p_entity, bool p_bind);
    void interest_send_awareness(int64_t p_peer, const godot::Array &p_wire);
    void interest_refresh_compat_intents();
    godot::Error display_write(
        const godot::RID &p_entity,
        const godot::StringName &p_track,
        const godot::Variant &p_value
    );
    godot::Error display_write_default(
        const godot::RID &p_entity,
        const godot::StringName &p_track,
        const godot::Variant &p_value
    );
    GDVIRTUAL3R(
        godot::Error,
        _display_write,
        godot::RID,
        godot::StringName,
        godot::Variant
    )
    GDVIRTUAL2R(godot::Array, _sync_gather_set, godot::RID, int64_t)
    GDVIRTUAL3R(
        godot::Error,
        _sync_apply_set,
        godot::RID,
        int64_t,
        godot::Array
    )
    godot::Error display_lane(
        const godot::RID &p_entity,
        const godot::StringName &p_track,
        const godot::Variant &p_value
    );
    godot::Node *scene_node_of(const godot::RID &p_scene) const;
    godot::Dictionary scene_nodes_by_label() const;
    godot::TypedArray<godot::Node> scene_live_nodes() const;
    godot::Node *scene_current_node() const;
    void scene_report_participant(
        const godot::RID &p_scene,
        int64_t p_peer,
        bool p_present
    );
    void scene_open_admission(godot::Node *p_container);
    void scene_close_admission(godot::Object *p_container);
    void scene_report_local_participant(
        const godot::RID &p_scene,
        bool p_present
    );
    void scene_on_participant_joined(
        const godot::Ref<NetwParticipant> &p_participant
    );
    void scene_on_admission_visible(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::RID &p_scene
    );
    void scene_on_admission_hidden(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::RID &p_scene
    );
    void display_on_recovered(
        int64_t p_entry,
        const godot::Dictionary &p_deltas,
        bool p_teleported,
        int64_t p_attribution,
        const godot::RID &p_entity
    );
    void set_display_role_reader(const godot::Callable &p_reader);
    void set_display_chase_hook(const godot::Callable &p_hook);

    void display_resolve_role(display::Runtime *p_runtime);

    void display_bind_session();
    display::Decl display_config_for(const godot::Ref<NetwEntity> &p_entity);
    int64_t display_route_of(const godot::Ref<NetwEntity> &p_entity);
    display::Runtime *display_runtime_for(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void display_on_entity_live(int64_t p_route, godot::Object *p_entity);
    void display_release_route(int64_t p_route);
    void display_release_hooks(display::Runtime *p_runtime);
    void display_clear_runtimes();
    void display_on_control_changed(
        int64_t p_previous,
        int64_t p_peer,
        int64_t p_route
    );
    void display_refresh_moved(int64_t p_route);
    void display_mark_role_dirty(const godot::RID &p_entity);
    void display_mark_dirty(const godot::RID &p_entity, display::Dirt p_dirt);
    void display_drain_dirty();
    void display_on_book_dirty(
        const godot::RID &p_entity,
        display::Dirt p_dirt
    );
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
    void display_rebuild_runtime(display::Runtime *p_runtime);
    display::Channel *display_ensure_state(
        display::Runtime *p_runtime,
        godot::Node *p_node,
        const godot::StringName &p_source_prop,
        const godot::StringName &p_target_prop,
        const godot::Ref<NetwInterpolate> &p_spec,
        bool p_authoring_tick
    );
    void display_pump_runtime(
        display::Runtime *p_runtime,
        const display::Timing &p_timing,
        display::PumpStats &p_stats
    );
    double display_chase_smooth_time(
        display::Runtime *p_runtime,
        const display::Timing &p_timing
    ) const;
    godot::Error display_pump_entity(
        const godot::RID &p_entity,
        const display::Timing &p_timing
    );
    void display_absorb_recovery(
        display::Runtime *p_runtime,
        const godot::Dictionary &p_deltas,
        bool p_teleported
    );

    godot::Ref<NetwPromise> scene_request_send(
        const godot::String &p_path,
        int p_scope
    );
    static constexpr double SCENE_REQUEST_DEADLINE = 10.0;
    godot::Ref<NetwPromise> scene_request(
        const godot::String &p_path,
        SceneChange p_scope
    );
    godot::Ref<NetwPromise> scene_request_open(
        const godot::String &p_path,
        int p_scope,
        double p_deadline
    );
    void scene_request_expire(int p_request_id);
    int scene_request_armed_id() const;
    double scene_request_armed_deadline() const;

    bool scene_receive_result_frame(
        const godot::PackedByteArray &p_payload,
        int p_sender
    );
    void scene_receive_request_frame(
        const godot::PackedByteArray &p_payload,
        int p_sender
    );
    void scene_receive_released_frame(
        const godot::PackedByteArray &p_payload,
        int p_sender
    );
    void scene_receive_seat_frame(
        const godot::PackedByteArray &p_payload,
        int p_sender
    );
    void scene_receive_request(
        int64_t p_peer,
        int p_request_id,
        const godot::String &p_scene_path,
        int p_scope
    );
    void scene_receive_path_request(
        int64_t p_peer,
        int p_request_id,
        const godot::Ref<NetwParticipant> &p_participant,
        const godot::String &p_scene_path,
        int p_scope
    );
    godot::Error scene_admits_request(
        const godot::Ref<NetwParticipant> &p_participant,
        const godot::Variant &p_destination,
        int p_scope
    );
    void scene_send_result(int64_t p_peer, int p_request_id, int p_code);
    void scene_answer_when_settled(
        const godot::Ref<NetwPromise> &p_operation,
        int64_t p_peer,
        int p_request_id
    );

    SceneDecl scene_decl_of(const godot::Ref<godot::Script> &p_script) const;

    void scene_set_request_handler(const godot::Callable &p_handler);
    godot::Callable scene_get_request_handler() const;

    godot::RID get_current_scene() const;
    godot::RID scene_named(const godot::StringName &p_stem) const;
    godot::Array scenes_named(const godot::StringName &p_stem) const;
    godot::Array live_scenes() const;

    void scene_observe(
        const godot::RID &p_scene,
        SceneEvent p_event,
        const godot::Callable &p_callback
    );
    void scene_unobserve(
        const godot::RID &p_scene,
        SceneEvent p_event,
        const godot::Callable &p_callback
    );

    godot::RID interest_layer_create(const godot::StringName &p_name);
    void interest_layer_free(const godot::RID &p_layer);
    godot::Error interest_layer_add_viewer(
        const godot::RID &p_layer,
        int64_t p_peer
    );
    void interest_layer_remove_viewer(
        const godot::RID &p_layer,
        int64_t p_peer
    );
    godot::Error interest_layer_add_entity(
        const godot::RID &p_layer,
        const godot::RID &p_entity
    );
    void layer_remove_entity(
        const godot::RID &p_layer,
        const godot::RID &p_entity
    );
    void interest_layer_set_param(
        const godot::RID &p_layer,
        LayerParam p_param,
        const godot::Variant &p_value
    );
    void interest_layer_set_driver_callback(
        const godot::RID &p_layer,
        const godot::Callable &p_callback
    );

    godot::Error run_layer_drivers();
    int64_t layer_driver_count() const;
    godot::Error interest_flush_now();
    godot::Error session_flush_tick(int64_t p_tick);
    void session_on_clock_tick(double p_delta, int64_t p_tick);
    void predict_stepper_install(
        const godot::RID &p_space,
        const godot::Ref<NetwPhysicsStepper> &p_stepper
    );
    void predict_reresolve_space(const godot::RID &p_space);
    void predict_reconcile_declaration(const godot::Ref<NetwEntity> &p_entity);
    godot::Ref<NetwPhysicsStepper> predict_get_stepper(
        const godot::RID &p_space
    ) const;
    void predict_stepper_hold(const godot::RID &p_space, int p_dimension);
    bool predict_space_is_stepped(const godot::RID &p_space) const;
    void predict_engine_install(
        const godot::RID &p_entity,
        NetwPredictSlotEngine *p_engine
    );
    NetwPredictSlotEngine *predict_engine_release(const godot::RID &p_entity);
    NetwPredictSlotEngine *predict_engine_for(const godot::RID &p_entity) const;
    bool predict_engine_seated(const godot::RID &p_entity) const;
    godot::TypedArray<godot::Object> predict_engine_entities() const;
    NetwPredictRunner *predict_runner_seated();
    void predict_history_record_tick(int64_t p_tick);
    void predict_history_record_frame(int64_t p_tick);
    bool lagcomp_is_configured() const;
    void set_lagcomp_configured(bool p_configured);
    void lagcomp_arm();
    godot::Error lagcomp_initialize(
        int64_t p_max_future_action_ticks,
        int64_t p_input_gate_deadline_ticks
    );
    void lagcomp_observe_mounted(godot::Node *p_node);
    void wire_lagcomp_service();
    godot::Callable lagcomp_node_watcher();
    void lagcomp_release();
    void predict_receive_command_carrier(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void predict_receive_relay_carrier(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void predict_receive_relay_request_carrier(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void predict_receive_ack_carrier(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void action_receive_carrier(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void predict_relay_command_frame(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::PackedByteArray &p_payload,
        int64_t p_author
    );
    void lagcomp_deny_action(
        int64_t p_requester,
        const godot::StringName &p_key
    );
    void submit_action(
        int64_t p_route,
        const godot::StringName &p_method,
        int64_t p_view_tick,
        const godot::Variant &p_data,
        const godot::StringName &p_key,
        int p_timing_mode,
        int64_t p_requester
    );
    void action_send_request(
        const godot::NodePath &p_target_path,
        const godot::StringName &p_method,
        int64_t p_view_tick,
        const godot::Variant &p_data,
        const godot::StringName &p_key,
        int p_timing_mode
    );
    void drain_pending_actions(int64_t p_tick);
    void tick_step(double p_delta, int64_t p_tick);
    void before_frame_step();
    void frame_step();
    int64_t pending_action_count() const;
    int64_t action_gate_fallbacks() const;
    godot::TypedArray<godot::Object> predict_stepped_entities() const;
    godot::Dictionary predict_metrics() const;

    bool interest_admits(const godot::RID &p_entity, int64_t p_peer);
    godot::PackedInt64Array interest_get_row(const godot::RID &p_entity);
    godot::String interest_explain(const godot::RID &p_entity, int64_t p_peer);
    godot::TypedArray<godot::RID> interest_get_membership(
        const godot::RID &p_entity
    );

    godot::RID layer_open(const godot::StringName &p_name);
    godot::RID interest_layer_find(const godot::StringName &p_name) const;
    godot::StringName layer_name_of(const godot::RID &p_layer) const;
    godot::Ref<NetwInterestLayer> interest_layer_view(
        const godot::RID &p_layer
    ) const;
    godot::Ref<NetwInterestLayer> layer_record(const godot::RID &p_layer) const;
    void layer_close(const godot::RID &p_layer);
    void interest_layer_set_monitor_callback(
        const godot::RID &p_layer,
        const godot::Callable &p_callback
    );
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
    void interest_release_body(const godot::Ref<NetwEntity> &p_entity);

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
    void interest_apply_delta(const interest::Delta &p_delta);
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

    bool interest_is_filtered(const godot::RID &p_entity);

    godot::Array interest_membership_ids(const godot::RID &p_entity);

    void persist_set_quit_guard(const godot::Callable &p_guard);
    void persist_set_drain(const godot::Callable &p_drain);

    void persistence_drain_start(double p_notify_delay);
    void persistence_drain_advance();
    void persistence_drain_forget();

    spawn::Pipeline *spawn_plane() const;
    void spawn_on_peer_connected(int64_t p_peer_id);
    godot::PackedByteArray spawn_encode_spawn_frame(
        int64_t p_route,
        godot::Node *p_node
    );
    void spawn_run_visibility_sweep();
    void spawn_run_carrier_flush();
    godot::Error spawn_declare_stage(
        const godot::RID &p_handle,
        const godot::Dictionary &p_facts
    );
    void spawn_on_armed_tree_entered(int64_t p_route);
    void spawn_flush_armed_spawn(int64_t p_route);
    godot::Node *spawn_build_adopt(
        godot::Object *p_parent,
        const godot::String &p_name
    );
    godot::Node *spawn_build_scene(const godot::Variant &p_packed);
    godot::Node *spawn_build_spawner(
        godot::Object *p_spawner,
        int64_t p_index,
        const godot::Variant &p_data
    );
    godot::Node *spawn_build_fn(
        const godot::Callable &p_fn,
        const godot::Array &p_args
    );
    godot::Node *spawn_build_host_fn(
        godot::Object *p_host,
        const godot::StringName &p_method,
        const godot::Array &p_args
    );
    void spawn_free_despawned(int64_t p_route);
    void spawn_handle_spawn_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void spawn_handle_despawn_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void spawn_handle_hide_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void spawn_handle_reparent_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void spawn_retry_parked(int64_t p_route);
    void spawn_expire_parked(int64_t p_route);
    void spawn_retry_scene_parked_spawns(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void spawn_on_action_reveal_tick(double p_delta, int64_t p_tick);
    void spawn_apply_action_gate(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void spawn_schedule_visibility_sweep();
    bool spawn_books_node(godot::Node *p_node) const;
    godot::Ref<NetwEntity> spawn_arm_consumed(
        godot::Node *p_node,
        godot::Object *p_spawner,
        int p_scene_index,
        const godot::Variant &p_data
    );

    SyncPipeline *sync_plane() const;
    void sync_pipeline_on_entity_live(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    godot::PackedByteArray sync_pipeline_stage_row_frame(
        int64_t p_peer,
        int64_t p_frame_tick,
        int64_t p_route,
        int64_t p_comp,
        int64_t p_mask,
        const godot::PackedByteArray &p_bytes
    );
    godot::Error sync_pipeline_decode_stage_body();
    godot::Array sync_pipeline_gather_stage();
    godot::Error sync_pipeline_apply_one_value(const godot::Array &p_values);
    void sync_pipeline_bind_declaration(
        const godot::Ref<NetwPropertySetBinding> &p_binding
    );
    void sync_pipeline_recapture_entity(const godot::Ref<NetwEntity> &p_entity);
    void sync_pipeline_report_missing_prediction_component(
        godot::Node *p_node,
        int64_t p_config_hash
    );
    godot::Variant sync_pipeline_encode_prop_val(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_node,
        const godot::StringName &p_property
    );
    godot::PackedByteArray sync_pipeline_encode_derived_descriptors(
        int64_t p_route
    );
    void sync_pipeline_note_derived_schema(
        int64_t p_route,
        const godot::Dictionary &p_descriptors
    );

    SyncCompat *sync_adapter() const;
    void sync_compat_entity_live(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity
    );
    void sync_compat_visibility_changed(int64_t p_peer, godot::Node *p_root);
    void sync_compat_config_changed();
    godot::Error sync_compat_decode_stage_body();
    godot::Array sync_compat_gather_stage();
    godot::Error sync_compat_apply_stage(const godot::Array &p_staged);

    spawn::SpawnerCompat *spawner_adapter() const;
    void spawner_session_entered();
    void spawner_session_ended();
    void spawner_node_added(godot::Node *p_node);
    void spawner_wrap_when_ready(godot::Node *p_spawner);
    void spawner_clear_session_state();
    godot::Node *spawner_wrapped_spawn(
        const godot::Variant &p_data,
        const godot::Callable &p_original
    );

    bool persistence_serves();
    void persistence_arm_quit_guard();
    godot::Error write_scene_facet(const godot::RID &p_entity, bool p_declared);
    void apply_pending_scene_facet(
        const godot::RID &p_entity,
        const godot::Ref<NetwEntity> &p_wrapper
    );
    static constexpr int CLOCKLESS_TICKRATE = 30;

    godot::Ref<NetwPromise> scene_move(
        const godot::RID &p_entity,
        const godot::RID &p_destination,
        const godot::Ref<NetwReparentOpts> &p_opts
    );
    void send_standalone_ack(int64_t p_peer, int64_t p_ack);
    void flush_standalone_acks();
    void liveness_when_live(
        int64_t p_route,
        const godot::Callable &p_callback,
        int64_t p_timeout_ticks,
        const godot::Callable &p_on_timeout
    );

    godot::PackedInt64Array liveness_claim_routes(int p_count);
    godot::Error liveness_release_routes(
        const godot::PackedInt64Array &p_routes
    );
    godot::Error entity_despawn(
        const godot::RID &p_entity,
        const godot::Ref<NetwDespawnOpts> &p_opts
    );
    godot::Error send_bytes(
        const godot::PackedByteArray &p_bytes,
        int64_t p_peer,
        godot::MultiplayerPeer::TransferMode p_mode,
        int p_channel
    );
    void clear_roster();
    int64_t native_prediction_slot(
        const godot::Ref<NetwEntity> &p_entity
    ) const;
    static NetwMultiplayer *tree_published_session(godot::Node *p_node);
    static godot::Ref<NetwMultiplayer> resolve_required(godot::Node *p_node);
    int64_t action_slot_of(const godot::Callable &p_authority);
    godot::Ref<NetwAction> lagcomp_action(const godot::Callable &p_authority);
    void register_prediction(const godot::Ref<NetwEntity> &p_entity);
    void unregister_prediction(const godot::Ref<NetwEntity> &p_entity);
    godot::Error predict_declare(const godot::RID &p_entity);
    void predict_undeclare(const godot::RID &p_entity);
    godot::Ref<NetwTimeline> register_timeline(
        const godot::Ref<NetwEntity> &p_entity
    );
    static bool is_extension_script(const godot::Ref<godot::Script> &p_script);

    godot::Ref<NetwPredictionHandle> prediction_handle(
        const godot::RID &p_entity
    ) const;
    void predict_set_param(
        const godot::RID &p_entity,
        PredictParam p_param,
        const godot::Variant &p_value
    );
    godot::Variant predict_get_param(
        const godot::RID &p_entity,
        PredictParam p_param
    );
    void predict_set_sensor_callback(
        const godot::RID &p_entity,
        const godot::StringName &p_name,
        const godot::Callable &p_callback
    );
    void predict_set_witness_callback(
        const godot::RID &p_entity,
        const godot::Callable &p_callback
    );
    void predict_set_corridor_callback(
        const godot::RID &p_entity,
        const godot::Callable &p_callback
    );
    void predict_set_simulate_callback(
        const godot::RID &p_entity,
        const godot::Callable &p_callback
    );
    godot::Error predict_bind_owner(
        const godot::RID &p_entity,
        godot::Object *p_owner
    );
    void predict_unbind_owner(const godot::RID &p_entity);
    godot::Error predict_island_add(
        const godot::RID &p_entity,
        const godot::RID &p_other
    );
    void predict_island_remove(
        const godot::RID &p_entity,
        const godot::RID &p_other
    );
    godot::Error predict_island_set_param(
        const godot::RID &p_entity,
        IslandParam p_param,
        const godot::Variant &p_value
    );
    godot::Error predict_island_set_member_param(
        const godot::RID &p_entity,
        const godot::RID &p_member,
        MemberParam p_param,
        const godot::Variant &p_value
    );
    godot::Variant predict_island_get_member_param(
        const godot::RID &p_entity,
        const godot::RID &p_member,
        MemberParam p_param
    );
    godot::Variant predict_sensor_sample(
        const godot::RID &p_entity,
        const godot::StringName &p_name,
        const godot::Variant &p_default
    );
    void predict_notify_contact(const godot::RID &p_entity);

    godot::Ref<ResolvedJoin> peer_get_accepted_join(int64_t p_peer) const;
    void peer_forget(int64_t p_peer);
    godot::Ref<NetwParticipant> peer_get_participant(int64_t p_peer);

    godot::Error display_declare(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::StringName &p_track,
        const godot::Ref<NetwInterpolate> &p_spec
    );
    void display_undeclare(const godot::RID &p_entity);
    godot::Error display_record_track(
        const godot::RID &p_entity,
        const godot::StringName &p_track,
        const godot::Variant &p_value,
        int64_t p_tick
    );
    void display_set_param(
        const godot::RID &p_entity,
        DisplayParam p_param,
        const godot::Variant &p_value
    );
    void display_set_target_node(
        const godot::RID &p_entity,
        godot::Node *p_node
    );
    void display_set_target_item(
        const godot::RID &p_entity,
        const godot::RID &p_item
    );
    void display_set_callback(
        const godot::RID &p_entity,
        const godot::Callable &p_callback
    );
    void display_clear_declarations();
    void write_display_param(
        const godot::Ref<NetwEntity> &p_wrapper,
        int p_param,
        const godot::Variant &p_value
    );

    godot::Variant display_get_param(
        const godot::RID &p_entity,
        DisplayParam p_param
    );
    void display_reset(const godot::RID &p_entity);
    void display_snap(
        const godot::RID &p_entity,
        const godot::StringName &p_track,
        const godot::Variant &p_value
    );
    godot::Variant display_get_value(
        const godot::RID &p_entity,
        const godot::StringName &p_track
    );
    int64_t display_get_tick(const godot::RID &p_entity);
    godot::Variant display_get_track_stat(
        const godot::RID &p_entity,
        const godot::StringName &p_track,
        const godot::StringName &p_stat
    );

    godot::Error scene_declare(const godot::RID &p_entity);
    godot::Error scene_undeclare(const godot::RID &p_entity);
    void scene_clear_pending_facets();
    godot::Error scene_set_param(
        const godot::RID &p_scene,
        SceneParam p_param,
        const godot::Variant &p_value
    );
    godot::Variant scene_get_param(
        const godot::RID &p_scene,
        SceneParam p_param
    );
    godot::RID scene_find(const godot::StringName &p_stem);
    godot::TypedArray<godot::RID> scene_find_all(
        const godot::StringName &p_stem
    );
    godot::TypedArray<godot::RID> scene_list();
    godot::Node *scene_get_node(const godot::RID &p_scene) const;
    godot::StringName scene_get_label(const godot::RID &p_scene) const;
    godot::TypedArray<godot::RID> scene_get_entities(const godot::RID &p_scene);
    godot::TypedArray<NetwEntity> scene_get_players(const godot::RID &p_scene);
    godot::TypedArray<NetwParticipant> scene_get_participants(
        const godot::RID &p_scene
    );
    godot::RID scene_get_local_player(const godot::RID &p_scene);
    godot::Error scene_add_player(
        const godot::RID &p_scene,
        const godot::RID &p_player
    );
    godot::Error scene_add_player_record(
        const godot::RID &p_scene,
        const godot::Ref<NetwEntity> &p_player
    );
    godot::RID scene_get_layer(const godot::RID &p_scene);
    godot::RID scene_get_current();
    godot::SubViewport *scene_participant_viewport();
    bool scene_destroy(const godot::RID &p_scene);
    godot::TypedArray<NetwEntity> scene_players_all();
    godot::RID scene_create(
        const godot::Variant &p_recipe,
        SceneIsolation p_isolation
    );

    godot::Error channel_send(
        int64_t p_peer,
        const godot::RID &p_entity,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        bool p_reliable
    );
    bool sync_policy_admits(
        int p_policy,
        int64_t p_sender,
        const godot::RID &p_entity,
        int64_t p_comp
    );
    static bool law_extension_named();
    static godot::Ref<godot::Script> law_extension_script();
    static godot::Ref<NetwMultiplayer> make(
        const godot::Ref<godot::SceneMultiplayer> &p_inner,
        const godot::Ref<godot::Script> &p_implementation
    );
    static void install_default_interface();
    static void restore_default_interface();
    static NetwMultiplayer *of(godot::Node *p_node);

    godot::Error entity_add_property_set(
        const godot::RID &p_entity,
        const godot::RID &p_set,
        int64_t p_comp
    );
    void entity_remove_property_set(const godot::RID &p_entity, int64_t p_comp);
    godot::Variant entity_get_property(
        const godot::RID &p_entity,
        int64_t p_comp,
        int p_column
    );

    godot::RID property_set_create(
        const godot::RID &p_schema,
        RecordKind p_record
    );
    int property_set_add_column(const godot::RID &p_set, int p_column);
    void property_set_set_column_param(
        const godot::RID &p_set,
        int p_field,
        int p_param,
        const godot::Variant &p_value
    );
    void property_set_set_param(
        const godot::RID &p_set,
        int p_param,
        const godot::Variant &p_value
    );
    godot::Error property_set_seal(const godot::RID &p_set);
    godot::RID adopt_property_set(
        const godot::Ref<godot::Script> &p_script,
        RecordKind p_record_kind,
        const godot::Ref<NetwPropertySet> &p_source,
        godot::Node *p_node
    );
    godot::RID script_schema(
        const godot::Ref<godot::Script> &p_script,
        godot::Node *p_node
    );
    void property_set_clear();
    int64_t property_set_get_wire_hash(const godot::RID &p_set) const;
    godot::RID property_set_get_schema(const godot::RID &p_set) const;
    godot::Ref<NetwPropertySet> property_set_record(
        const godot::RID &p_set
    ) const;
    godot::Ref<NetwPropertySet> mutable_property_set(const godot::RID &p_set);

    godot::Ref<NetwPromise> persist_hydrate(const godot::RID &p_entity);
    godot::Ref<NetwPromise> persist_flush(
        const godot::RID &p_entity,
        const godot::Array &p_keys
    );
    godot::Ref<NetwPersistenceEngine> persistence_engine_for(
        NetwEntity *p_entity
    );
    void persist_tick_default(double p_delta);
    void persist_pump(double p_delta);
    GDVIRTUAL1(_persist_tick, double)
    void persistence_flush_all();
    godot::TypedArray<NetwPersistenceEngine> persistence_live_engines();
    void persist_shutdown();

    SessionState session_get_state() const;
    Role session_get_role() const;
    bool is_online() const;
    bool is_host() const;
    bool has_server_role() const;
    bool is_local_client() const;

    void count_sent(int64_t p_bytes);
    void count_received(int64_t p_bytes);
    void count_state_ack_out();
    void count_state_ack_in();
    void count_standalone_ack_out();

    void attribution_set_armed(bool p_armed);
    bool attribution_is_armed() const;
    godot::Dictionary attribution_snapshot() const;
    const wire::AttributionBook &attribution_book() const {
        return attribution;
    }
    void attribution_note_subject(int64_t p_route);
    void attribution_observe_in(
        int64_t p_peer,
        int64_t p_channel,
        int64_t p_route,
        int64_t p_comp,
        int64_t p_bytes
    );
    void attribution_note_refusal(
        int64_t p_peer,
        int64_t p_channel,
        wire::Refusal p_refusal
    );
    void attribution_note_column(
        int64_t p_schema,
        int64_t p_column,
        int64_t p_bits
    );
    void capture_note(
        wire::CaptureDirection p_dir,
        int64_t p_peer,
        const godot::PackedByteArray &p_datagram
    );
    void capture_close();
    const wire::CaptureWriter &capture_writer() const {
        return capture;
    }
    godot::Dictionary capture_header();

    godot::Array attribution_feed_payload();
    void attribution_feed_push();
    static const godot::StringName &attribution_feed_message();

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
    int64_t receive_tick() const;
    godot::Error clock_configure(const godot::Ref<NetwClockConfig> &p_config);
    godot::Ref<NetwClockConfig> clock_get_config() const;

    godot::Variant clock_get_param(ClockParam p_param) const;
    godot::Error clock_set_param(
        ClockParam p_param,
        const godot::Variant &p_value
    );
    double clock_get_monitor(ClockMonitor p_monitor) const;

    int64_t clock_get_tick() const;
    int64_t clock_get_display_tick() const;
    int64_t clock_get_physics_steps_per_tick() const;
    int64_t clock_get_simulation_behind_count() const;
    bool clock_is_configured() const;
    bool clock_is_synchronized() const;
    bool clock_is_simulating() const;
    bool clock_is_stable() const;
    bool clock_is_gated() const;

    void clock_step(int64_t p_count);
    void clock_physics_step(double p_delta);
    bool clock_consume_ping_due(double p_delta);
    void clock_tick_loop(bool p_open);
    void clock_set_gate(bool p_armed);
    void clock_set_synchronized(bool p_value);
    godot::Dictionary clock_ingest_pong(
        double p_sample,
        int64_t p_server_tick_at_pong,
        double p_server_tick_phase,
        bool p_apply_lead
    );

    godot::Error clock_initialize(const godot::Ref<NetwClockConfig> &p_from);
    void clock_offer_fallback(
        const godot::Ref<NetwClockConfig> &p_draft,
        godot::Object *p_source
    );
    void clock_attach_pump();
    void clock_detach_pump();
    void clock_on_physics_frame();
    void clock_start_handshake();
    void clock_drop_synchronization();
    void clock_auto_configure_offset();
    void clock_step_auto_config(double p_delta);
    void clock_release();

    void clock_apply_config(const godot::Ref<NetwClockConfig> &p_config);
    void clock_sweep_effects(double p_delta, int64_t p_tick);

    void session_set_inner(const godot::Ref<godot::SceneMultiplayer> &p_inner);
    godot::Ref<godot::SceneMultiplayer> session_get_inner() const {
        return inner;
    }

    EmbedPhase embed_phase() const {
        return embed_phase_value;
    }
    void embed_offer_bare_level(godot::Node *p_level);
    godot::Error embed_settle();
    godot::Error embed_poll_transport();
    bool embed_is_disposing() const {
        return embed_disposing;
    }
    bool embed_claim_dispose();

    void session_set_root(const godot::Callable &p_reader);
    ActionGateBook action_gates;
    bool applying_remote_frame = false;
    struct SyncFlushCounters {
        int64_t rows = 0;
        int64_t whole_rows = 0;
        int64_t retained = 0;
        int64_t windows = 0;
        int64_t window_samples = 0;
        int64_t staged_out = 0;
        int64_t ungathered = 0;
        int64_t deferred = 0;
        int64_t skips_invalid_node = 0;
        int64_t skips_no_entity = 0;
        int64_t skips_no_route = 0;
    } sync_flush;
    struct ReplDropCounters {
        int64_t unknown_route = 0;
        int64_t not_live = 0;
        int64_t lingering_route = 0;
        int64_t dead_route = 0;
        int64_t no_node = 0;
        int64_t traversal = 0;
        int64_t comp_unresolved = 0;
    } repl_drops;

    godot::HashSet<int64_t> unreachable_peers;
    godot::HashSet<int64_t> announced_departures;

    godot::Node *session_root() const;
    int64_t linger_pumps(double p_seconds) const;
    void session_relay_peer_connected(int64_t p_peer);
    void session_relay_peer_disconnected(int64_t p_peer);
    void session_clear_disconnected_peer(int64_t p_peer);
    void embed_adopt_inner(const godot::Ref<godot::SceneMultiplayer> &p_inner);
    void embed_dispose();

    bool verb_head_write(wire::WriteStream &p_stream, int64_t p_route);
    static bool verb_head_read(
        wire::ReadStream &p_stream,
        int64_t &r_route,
        int64_t &r_epoch
    );

    bool anchor_encode(wire::WriteStream &p_stream, godot::Node *p_target);
    bool anchor_decode(
        wire::ReadStream &p_stream,
        godot::Dictionary &r_anchor
    ) const;
    godot::Node *anchor_resolve(const godot::Dictionary &p_anchor);

    bool action_gate_arm(
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_display_tick
    );
    godot::PackedInt64Array action_gate_sweep(int64_t p_display_tick);
    void action_gate_drop(int64_t p_route);
    void action_gate_clear();
    int64_t action_gate_count() const;

    godot::Ref<NetwIdentity> participant_identity(int64_t p_peer) const;
    Role session_get_authored_role() const;

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

    void set_relay_sender(int64_t p_sender);
    int64_t rpc_get_relay_sender() const;

    static double rpc_request_timeout_default() {
        return 5.0;
    }

    void rpc_call(
        const godot::Callable &p_callable,
        const godot::Array &p_args,
        int64_t p_peer
    );
    godot::Ref<NetwPromise> rpc_request_call(
        int64_t p_peer,
        const godot::Callable &p_callable,
        const godot::Array &p_args,
        double p_timeout_seconds = 5.0
    );
    godot::Ref<NetwGroupPromise> rpc_request_call_group(
        const godot::Callable &p_callable,
        const godot::Array &p_args,
        double p_timeout_seconds = 5.0
    );
    void rpc_send_reply(
        int64_t p_peer,
        int64_t p_route,
        int64_t p_txn,
        const godot::Variant &p_value
    );
    void rpc_handle_reply(
        int64_t p_txn,
        int64_t p_sender,
        const godot::Variant &p_value
    );
    void rpc_handle_call(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_comp_node,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    godot::Variant rpc_method_token(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_node,
        const godot::StringName &p_method
    ) const;
    godot::LocalVector<call_args::Slot> rpc_encoded_args(
        const godot::Array &p_args
    ) const;
    call_args::Slot rpc_encoded_arg(const godot::Variant &p_arg) const;
    godot::Error entity_call(
        const godot::RID &p_entity,
        int64_t p_comp,
        const godot::StringName &p_method,
        const godot::Array &p_args,
        int64_t p_peer
    );
    void rpc_note_dropped_unroutable();
    void rpc_note_dropped_not_live();
    void rpc_warn_dropped_once(godot::Node *p_target);
    int64_t rpc_sends_dropped_unroutable() const;
    int64_t rpc_sends_dropped_not_live() const;
    void rpc_defer_call(
        int64_t p_sender,
        int64_t p_route,
        const godot::Callable &p_callback
    );
    void rpc_sweep_deferred_calls();
    void rpc_sweep_transactions(int64_t p_current);
    void rpc_handle_disconnect(int64_t p_peer);
    void rpc_clear_session();
    void rpc_dispose();
    godot::Dictionary rpc_counters() const;
    godot::Dictionary relay_stats_snapshot();
    godot::Dictionary lagcomp_metrics() const;
    godot::Dictionary stats_snapshot();
    int64_t stats_get(Stat p_stat);
    void rpc_settle_open(int64_t p_txn, const RpcSettle &p_settle);
    RpcSettle rpc_settle_of(int64_t p_txn) const;
    void rpc_settle_close(int64_t p_txn);
    NetwCallPark *get_rpc_park() {
        return &rpc_park;
    }
    NetwTxnBook *get_rpc_txns() {
        return &rpc_txns;
    }

    int64_t send_datagram(
        int64_t p_peer,
        const godot::PackedByteArray &p_payload,
        bool p_reliable,
        bool p_carrier = true
    );

    NetwCarrierDatagram frame_datagram(
        int64_t p_peer,
        const godot::PackedByteArray &p_payload,
        bool p_reliable,
        bool p_carrier = true
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
    uint32_t inbound_delivery_history(int64_t p_peer) const;
    uint32_t peer_ack_history(int64_t p_peer) const;
    int64_t datagram_base_tick() const;
    bool note_inbound_seq(int64_t p_peer, int64_t p_seq);
    bool note_peer_ack(int64_t p_peer, int64_t p_ack, uint32_t p_history);
    void sync_note_sent_default(int64_t p_peer, int64_t p_sequence);
    void sync_note_ack_default(int64_t p_peer, int64_t p_sequence);
    void note_sent(int64_t p_peer, int64_t p_sequence);
    void note_ack(int64_t p_peer, int64_t p_sequence);
    GDVIRTUAL2(_sync_note_sent, int64_t, int64_t)
    GDVIRTUAL2(_sync_note_ack, int64_t, int64_t)
    godot::Object *session_seam(const godot::StringName &p_seam) const;
    void session_note_state_ack(
        int64_t p_peer,
        int64_t p_ack,
        uint32_t p_history
    );
    godot::Error session_receive_inner_packet(
        int64_t p_sender,
        const godot::PackedByteArray &p_packet
    );
    void session_on_inner_packet(
        int64_t p_sender,
        const godot::PackedByteArray &p_packet
    );
    int64_t peer_ack(int64_t p_peer) const;
    void note_echoed_seq(int64_t p_peer, int64_t p_seq);
    godot::PackedInt64Array peers_owed_echo() const;
    void forget_peer_seqs(int64_t p_peer);
    void clear_seq_books();

    static bool counts_verdict(godot::Error p_verdict);
    bool count_verdict(godot::Error p_verdict, int64_t p_route = 0);
    bool connect_once(
        godot::Signal p_signal,
        const godot::Callable &p_callback,
        int64_t p_flags = 0
    );
    bool disconnect_once(
        godot::Signal p_signal,
        const godot::Callable &p_callback
    );
    int64_t stats_get_verdict_count(godot::Error p_verdict) const;
    bool claim_verdict_warning(godot::Error p_verdict, int64_t p_route);
    bool warn_verdict(godot::Error p_verdict, int64_t p_route = 0);
    godot::Error sink_verdict(godot::Error p_verdict, int64_t p_route);
    void clear_verdicts();

    void clear_flat_family_state();
    void clear_session_state();
    void session_settle_clear_state();
    void session_set_tree_paused(bool p_paused);

    void session_defer(
        const godot::Callable &p_fn,
        const godot::StringName &p_key
    );
    void session_defer_after(
        const godot::Callable &p_fn,
        const godot::StringName &p_key,
        int p_pumps
    );
    void settle_advance();
    void session_cancel_deferred(const godot::StringName &p_key);
    godot::PackedStringArray settle_drain();
    void settle_clear();
    int settle_pending() const;
    bool settle_has_key(const godot::StringName &p_key) const;
    static int settle_max_passes();
    void session_flush_deferred();

    SchemaCore *get_schema_core();

    static godot::Error configuration_door_refuses_config(
        godot::Object *p_config
    );

    godot::RID schema_create(const godot::StringName &p_name);
    int schema_add_column(
        const godot::RID &p_schema,
        const godot::StringName &p_key,
        ColumnType p_type,
        int p_stride
    );
    void schema_set_column_quantizer(
        const godot::RID &p_schema,
        int p_column,
        const godot::Ref<NetwQuantize> &p_quantizer
    );
    godot::Error schema_seal(const godot::RID &p_schema);
    godot::RID schema_find(const godot::StringName &p_name) const;
    void adopt_schema_declarations();
    void adopt_table_declarations();
    godot::RID schema_find_or_adopt(const godot::StringName &p_name);
    godot::RID table_find_or_adopt(const godot::StringName &p_name);
    godot::StringName schema_get_name(const godot::RID &p_schema) const;
    static godot::Variant::Type schema_get_element_type(ColumnType p_type);
    int schema_get_hash(const godot::RID &p_schema) const;
    int schema_get_column_count(const godot::RID &p_schema) const;
    godot::StringName schema_get_column_key(
        const godot::RID &p_schema,
        int p_column
    ) const;
    ColumnType schema_get_column_type(
        const godot::RID &p_schema,
        int p_column
    ) const;
    int schema_get_column_stride(
        const godot::RID &p_schema,
        int p_column
    ) const;

    godot::Ref<table::Core> get_table_core() const;

    godot::RID table_create(const godot::RID &p_schema);
    godot::RID table_get_schema(const godot::RID &p_table) const;
    void table_set_param(
        const godot::RID &p_table,
        TableParam p_param,
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
    godot::Dictionary persist_table_commit(
        const godot::RID &p_table,
        const godot::RID &p_schema,
        const godot::Dictionary &p_data
    );
    godot::PackedInt64Array table_read_routes(const godot::RID &p_table) const;
    godot::Variant table_read_column(
        const godot::RID &p_table,
        int p_column
    ) const;
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

    void session_set_join_resolver(const godot::Callable &p_resolver);
    void session_admit(const godot::Ref<ResolvedJoin> &p_join);
    void session_run_join_handler(const godot::Ref<ResolvedJoin> &p_join);
    bool session_preflight_join(const godot::Ref<ResolvedJoin> &p_join);
    void session_fail_join(
        const godot::Ref<ResolvedJoin> &p_join,
        godot::Error p_error,
        const godot::String &p_reason
    );
    void session_turn_peer_away(int64_t p_peer, const godot::String &p_reason);
    void session_drop_turned_away_peer(int64_t p_peer);
    void session_report_join_failure(
        godot::Error p_error,
        const godot::String &p_reason
    );
    bool session_join_still_pending(
        const godot::Ref<ResolvedJoin> &p_join
    ) const;
    void session_seat_join_rejected(
        const godot::Variant &p_error,
        const godot::String &p_message,
        const godot::Ref<ResolvedJoin> &p_join
    );
    void session_seat_join_result(
        const godot::Variant &p_scene,
        const godot::Ref<ResolvedJoin> &p_join
    );
    void session_seat_join_into(
        int64_t p_peer,
        const godot::RID &p_scene,
        const godot::Ref<ResolvedJoin> &p_join
    );

    const char *session_identity_differs(const JoinRequest &p_request) const;

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

    int max_future_action_ticks = 8;
    int input_gate_deadline_ticks = 12;

    godot::NodePath get_root_path() const;
    void set_root_path(const godot::NodePath &p_path);
    bool is_object_decoding_allowed() const;
    void set_allow_object_decoding(bool p_value);
    double get_auth_timeout() const;
    void set_auth_timeout(double p_seconds);
    bool is_refusing_new_connections() const;
    void set_refuse_new_connections(bool p_value);
    bool is_server_relay_enabled() const;
    void set_server_relay_enabled(bool p_value);
    int get_max_sync_packet_size() const;
    void set_max_sync_packet_size(int p_value);
    int get_max_delta_packet_size() const;
    void set_max_delta_packet_size(int p_value);
    int get_max_future_action_ticks() const;
    void set_max_future_action_ticks(int p_value);
    int get_input_gate_deadline_ticks() const;
    void set_input_gate_deadline_ticks(int p_value);
    godot::TypedArray<godot::Object> get_connected_participants() const;

    godot::Error relay_subscribe(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer,
        bool p_subscribed
    );
    void predict_relay_subscribe(const godot::RID &p_entity, bool p_subscribed);
    godot::PackedInt64Array relay_peers(int64_t p_entity_slot) const;
    void relay_release(int64_t p_entity_slot);
    int relay_request_of(const godot::PackedByteArray &p_payload) const;
    void rpc_channel_register(
        int64_t p_channel,
        const godot::Callable &p_handler,
        bool p_defer_when_unknown
    );
    void channel_dispatch(
        const godot::Ref<NetwEntity> &p_wrapper,
        const godot::PackedByteArray &p_payload,
        int64_t p_sender,
        int64_t p_channel
    );

    void report_event(
        int64_t p_event,
        int64_t p_route,
        const godot::Dictionary &p_detail,
        int64_t p_peer,
        const godot::StringName &p_entity_id,
        const godot::Dictionary &p_model,
        int64_t p_verdict
    );
    godot::Error entity_bind_node(
        const godot::RID &p_entity,
        godot::Node *p_node
    );
    int64_t arm_rewind_timeline(const godot::Ref<NetwEntity> &p_entity);
    void lagcomp_rewind(
        const godot::TypedArray<godot::RID> &p_entities,
        int64_t p_tick,
        const godot::Callable &p_body
    );
    godot::Ref<DictionaryRecord> lagcomp_sample(
        const godot::RID &p_entity,
        int64_t p_tick
    ) const;
    godot::Ref<DictionaryRecord> lagcomp_sample_of(
        const godot::Ref<godot::RefCounted> &p_entity,
        int64_t p_tick
    ) const;

    godot::RID entity_create();
    int64_t entity_admit(const godot::RID &p_entity);
    godot::Error entity_bind_route(const godot::RID &p_entity, int64_t p_route);
    int64_t entity_get_route(const godot::RID &p_entity) const;
    godot::Node *entity_get_node(const godot::RID &p_entity) const;
    godot::RID entity_get_parent(const godot::RID &p_entity);
    int64_t entity_get_peer(const godot::RID &p_entity) const;
    void entity_grant_control(const godot::RID &p_entity, int64_t p_peer);
    EntityState entity_get_state(const godot::RID &p_entity) const;
    int entity_get_epoch(const godot::RID &p_entity) const;
    godot::RID entity_from_route(int p_route) const;
    godot::PackedInt32Array liveness_get_routes() const;

    godot::Error send_auth(
        int64_t p_peer,
        const godot::PackedByteArray &p_data
    );
    godot::Error complete_auth(int64_t p_peer);
    godot::PackedInt32Array get_authenticating_peers() const;

    void auth_set_flow(const godot::Ref<NetwAuthFlow> &p_flow);
    godot::Ref<NetwAuthFlow> auth_effective_flow();
    bool auth_is_configured();
    godot::Ref<NetwAuthFlow> auth_flow() const;
    void auth_set_app_tag(int64_t p_tag);
    int64_t auth_app_tag_of() const;
    void auth_set_join_request(const std::optional<JoinRequest> &p_request);
    const std::optional<JoinRequest> &auth_join_request() const;
    void set_auth_callback(const godot::Callable &p_callback);
    godot::Callable get_auth_callback() const;
    void auth_arm();
    void auth_disarm();
    godot::Ref<NetwPromise> auth_prepare(const godot::StringName &p_username);
    void auth_seat_host_identity();
    void auth_resolve_identity(int64_t p_peer, JoinRequest &r_join);
    void auth_receive(int64_t p_peer, const godot::PackedByteArray &p_data);
    void auth_send_hello(int64_t p_peer);
    void auth_release_hello();
    void discovery_probe_authenticating(int64_t p_peer_id);
    void discovery_probe_auth_received(
        int64_t p_peer_id,
        const godot::PackedByteArray &p_data
    );
    void discovery_directory_delivered(
        const godot::Ref<godot::MultiplayerPeer> &p_peer,
        int64_t p_directory
    );
    void discovery_directory_failed(
        int64_t p_error,
        const godot::String &p_message,
        int64_t p_directory
    );
    void discovery_directory_listed(
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos,
        int64_t p_directory
    );
    void discovery_probe_connection_failed();
    void discovery_probe_authentication_failed(int64_t p_peer_id);
    void auth_receive_hello(
        int64_t p_peer,
        const godot::PackedByteArray &p_data
    );
    void auth_clear();

    session_decl::Book &declaration_book();

    void declarations_changed();
    void config_settle();
    void config_consume_lagcomp_defaults();
    bool config_is_consumed(session_decl::Kind p_kind) const;
    godot::Error config_readiness(godot::String &r_reason);

    void session_answer_probe(int64_t p_peer);
    bool probe_forget(int64_t p_peer);
    void probe_clear();
    godot::PackedByteArray probe_reply_payload(bool &r_answered);

    void disconnect_peer(int64_t p_peer);
    void clear();

    godot::Variant peer_get_bucket(
        int64_t p_peer,
        const godot::Variant &p_type
    );
    bool peer_has_bucket(int64_t p_peer, const godot::Variant &p_type) const;

    void peer_set_identity(
        int64_t p_peer,
        const godot::Ref<NetwIdentity> &p_identity
    );
    godot::Ref<NetwIdentity> peer_get_identity(int64_t p_peer) const;

    godot::Ref<ResolvedJoin> session_accepted_join(int64_t p_peer) const;
    godot::Array session_accepted_joins() const;
    bool session_remember_join(const godot::Ref<ResolvedJoin> &p_join);
    void session_forget_peer(int64_t p_peer);
    void session_clear_roster();
    godot::PackedInt32Array reachable_peer_ids() const;
    bool session_link_is_connected() const;
    void peer_sweep_transport_state();
    void peer_mark_unreachable(int64_t p_peer);
    void peer_mark_reachable(int64_t p_peer);
    void session_refuse(int64_t p_peer, const godot::String &p_reason);
    godot::String session_refusal(int64_t p_peer) const;
    int session_name_verdict(
        const godot::StringName &p_name,
        const godot::PackedStringArray &p_taken,
        bool p_renames_on_collision,
        bool p_has_identity
    ) const;
    godot::StringName session_free_name(
        const godot::StringName &p_name,
        const godot::PackedStringArray &p_taken
    ) const;
    godot::PackedStringArray session_seated_names(
        const godot::TypedArray<NetwEntity> &p_seated
    ) const;
    bool session_admit_username(
        const godot::Ref<ResolvedJoin> &p_join,
        const godot::TypedArray<NetwEntity> &p_seated,
        const godot::Callable &p_disconnect
    );
    static godot::String session_role_name(Role p_role);

    void session_set_state(SessionState p_state);
    void session_set_role(Role p_role);
    void session_set_desired_role(Role p_role);
    void session_transition(SessionState p_state);
    void session_peer_assigned(bool p_live, bool p_connected, int p_unique_id);
    void session_resolve_online(int p_unique_id);
    void session_set_join_override(
        const godot::Callable &p_handler,
        const godot::Array &p_quantizers
    );
    godot::Callable session_join_override() const;
    godot::Array session_join_override_quantizers() const;
    static int64_t session_app_tag(const godot::StringName &p_app_id);

    connect::ConnectCore &connect_plane() {
        return connect_core;
    }
    const connect::ConnectCore &connect_plane() const {
        return connect_core;
    }
    godot::Ref<NetwConnectHandle> get_connection();
    godot::Ref<NetwSessionHandle> get_session();
    godot::Ref<NetwClockHandle> get_clock();

    godot::RID endpoint_add(
        const godot::RID &p_transport,
        const godot::String &p_address,
        const godot::String &p_display_name
    );
    void endpoint_remove(const godot::RID &p_endpoint);
    godot::RID endpoint_find(
        const godot::RID &p_transport,
        const godot::String &p_address
    );
    godot::Array endpoint_list();
    godot::Variant endpoint_get_param(
        const godot::RID &p_endpoint,
        EndpointParam p_param
    );
    godot::Error endpoint_set_param(
        const godot::RID &p_endpoint,
        EndpointParam p_param,
        const godot::Variant &p_value
    );
    godot::Variant endpoint_get_state(
        const godot::RID &p_endpoint,
        EndpointState p_state
    );
    void endpoint_probe(const godot::RID &p_endpoint);
    void endpoint_refresh();
    godot::RID transport_create_peer(
        const godot::RID &p_transport,
        int64_t p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings,
        const godot::Callable &p_completed,
        const godot::Callable &p_progress
    );
    void transport_cancel_peer_creation(const godot::RID &p_ticket);
    godot::RID transport_register(const godot::Ref<godot::Script> &p_type);
    godot::Error transport_unregister(const godot::RID &p_transport);
    godot::RID transport_find_script(
        const godot::Ref<godot::Script> &p_type
    ) const;
    godot::Array transport_list();
    godot::RID transport_find(const godot::StringName &p_peer_class);
    godot::Variant transport_get_param(
        const godot::RID &p_transport,
        TransportParam p_param
    );
    godot::StringName transport_class_of(const godot::RID &p_transport) const;
    const connect::TransportSlot *transport_held(
        const godot::RID &p_transport
    ) const;
    godot::Array session_get_join_schema();
    connect::ProbeHooks discovery_probe_hooks();
    void discovery_publish(
        const godot::StringName &p_peer_class,
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos
    );
    void endpoint_emit_added(const godot::RID &p_endpoint);
    void endpoint_emit_removed(const godot::RID &p_endpoint);
    void endpoint_emit_updated(const godot::RID &p_endpoint);
    godot::String peer_join_address();
    godot::Dictionary peer_diagnostics(int64_t p_peer_id);
    godot::Object *discovery_find_directory(
        const godot::StringName &p_peer_class
    ) const;
    godot::Array discovery_directory_peer_classes() const;
    void discovery_watch_directory(godot::Object *p_directory);
    void discovery_unwatch_directory(godot::Object *p_directory);

    godot::Ref<NetwSessionConfig> session_get_config() const;
    void session_set_server_info(const godot::Ref<NetwServerInfo> &p_info);
    godot::StringName session_get_app_id() const;
    godot::Error session_initialize(
        const godot::Ref<NetwSessionConfig> &p_from
    );
    void session_offer_fallback(
        const godot::Ref<NetwSessionConfig> &p_draft,
        godot::Object *p_source
    );
    void session_constrain_role(Role p_role);
    void session_apply_role_constraint();
    void session_report_discarded_fallback(const godot::String &p_scope);
    void session_push_desired_role();
    void session_apply_auth_config();

    struct JoinPlan {
        godot::Callable handler;
        godot::Array quantizers;
        bool available = false;
        bool declared = false;
    };
    JoinPlan session_resolve_join();
    static godot::Array join_declared_arg_types(
        const godot::Callable &p_handler
    );
    static godot::Array join_arg_types(const godot::Callable &p_handler);
    static int64_t join_schema_hash(
        const godot::Callable &p_handler,
        const godot::Array &p_quantizers
    );
    void session_encode_join_args(JoinRequest &r_request);
    bool session_decode_join_args(JoinRequest &r_request);
    godot::Ref<ResolvedJoin> session_resolve_inbound_join(
        JoinRequest &r_request,
        int64_t p_sender
    );
    void session_set_prepared_join(
        const godot::StringName &p_username,
        const godot::Array &p_args
    );
    const std::optional<JoinRequest> &session_prepared_join() const;
    void session_submit_join(
        const godot::StringName &p_username,
        const godot::Array &p_args
    );
    void session_clear_prepared_join();
    void session_dispose();
    void session_submit_prepared_join();
    void session_resubmit_join_on_server_peer(int64_t p_peer);
    godot::Ref<NetwPromise> session_prepare_join(
        const godot::StringName &p_username,
        const godot::Array &p_args
    );
    void session_settle_prepared_join(
        const godot::Variant &p_prepare_result,
        const godot::StringName &p_username,
        const godot::Array &p_args,
        const godot::Ref<NetwPromise> &p_prepared
    );
    void session_settle_refused_join(
        int64_t p_code,
        const godot::String &p_detail,
        const godot::StringName &p_username,
        const godot::Array &p_args,
        const godot::Ref<NetwPromise> &p_prepared
    );
    void session_submit_request(const JoinRequest &p_request);

    godot::Ref<NetwPromise> session_leave();
    void session_settle_leave(const godot::Ref<NetwPromise> &p_left);

    void session_pause(const godot::String &p_reason);
    void session_unpause();
    void session_notify_shutdown(const godot::String &p_reason);
    void session_request_leave(const godot::String &p_reason);

    void peer_kick(int64_t p_peer_id, const godot::String &p_reason);
    void peer_request_kick(int64_t p_peer_id, const godot::String &p_reason);

    NetwCarrierFrame receive_header(
        int64_t p_peer,
        const godot::PackedByteArray &p_packet
    );

    static constexpr int EFFECT_TIMEOUT_TICKS = 120;

    void lagcomp_effect_arm(
        const godot::StringName &p_key,
        const godot::Callable &p_revert,
        int p_timeout_ticks
    );
    bool lagcomp_effect_watch(
        const godot::StringName &p_key,
        const godot::Callable &p_confirmed,
        const godot::Callable &p_denied
    );
    godot::StringName lagcomp_effect_key(
        const godot::RID &p_entity,
        int64_t p_tick,
        int64_t p_slot
    ) const;
    godot::Error lagcomp_timeline_declare(const godot::RID &p_entity);
    void lagcomp_timeline_undeclare(const godot::RID &p_entity);
    godot::Ref<NetwTimeline> lagcomp_timeline_of(
        const godot::RID &p_entity
    ) const;
    void lagcomp_effect_adopt(const godot::StringName &p_key);
    void lagcomp_effect_discard(const godot::StringName &p_key);
    void observe_node_added(godot::Node *p_node);
    void settle_observe(godot::Node *p_node);
    void observe_node_entity(godot::Node *p_node);
    void observe_node_entity_ref(const godot::Variant &p_node_ref);
    void observe_entity_spawned(int64_t p_wrapper_id);
    void handle_deny(const godot::PackedByteArray &p_payload, int64_t p_sender);
    bool lagcomp_effect_pending(const godot::StringName &p_key) const;
    int64_t lagcomp_effect_count() const;
    void lagcomp_effect_sweep(int64_t p_tick);

    int64_t event_watch(
        const godot::PackedInt64Array &p_events,
        const godot::Dictionary &p_target,
        const godot::Dictionary &p_predicate,
        const godot::Callable &p_sink,
        const godot::Dictionary &p_opts
    );
    bool event_unwatch(int64_t p_id);
    godot::TypedArray<godot::Dictionary> event_watches() const;
    godot::TypedArray<godot::Dictionary> event_ring(int64_t p_route);
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

    godot::Error spawn_admit_frame(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    GDVIRTUAL4R(
        godot::Error,
        _spawn_admit_frame,
        int64_t,
        int64_t,
        int64_t,
        godot::PackedByteArray
    )

    godot::Error table_admit_frame_default(
        int64_t p_sender,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    godot::Error table_admit_frame(
        int64_t p_sender,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    GDVIRTUAL3R(
        godot::Error,
        _table_admit_frame,
        int64_t,
        int64_t,
        godot::PackedByteArray
    )

    godot::Error spawn_declare_default(
        const godot::RID &p_entity,
        const godot::Variant &p_recipe
    );
    godot::Error spawn_declare(
        const godot::RID &p_entity,
        const godot::Variant &p_recipe
    );
    void spawn_undeclare(const godot::RID &p_entity);
    void spawn_construct_arm(const godot::Callable &p_constructor);
    godot::Node *spawn_construct_default(const godot::RID &p_entity);
    godot::Node *spawn_construct(const godot::RID &p_entity);
    GDVIRTUAL2R(godot::Error, _spawn_declare, godot::RID, godot::Variant)
    GDVIRTUAL1(_spawn_undeclare, godot::RID)
    GDVIRTUAL1R(godot::Node *, _spawn_construct, godot::RID)

    void service_register(godot::Object *p_service, godot::Object *p_type);

    void service_unregister(godot::Object *p_service, godot::Object *p_type);

    godot::Object *get_service(godot::Object *p_type) const;
    godot::Object *get_service_named(const godot::StringName &p_class) const;
    godot::Variant service_held(godot::Object *p_type) const;
    godot::Variant service_held_named(const godot::StringName &p_class) const;

    godot::TypedArray<godot::Node> service_get_all(godot::Object *p_base) const;

    void service_clear();

    void wrapper_adopt(
        const godot::RID &p_entity,
        const godot::Ref<NetwEntity> &p_wrapper,
        godot::Object *p_owner
    );

    godot::RID entity_at_or_above(godot::Node *p_node) const;

    static godot::StringName wrapper_meta();

    static void set_wrapper_factory(const godot::Callable &p_factory);
    static bool has_wrapper_factory();
    static void clear_wrapper_factory();
    static godot::Callable wrapper_factory();

    static godot::Ref<NetwEntity> wrapper_at(godot::Object *p_node);
    static godot::Ref<NetwEntity> wrapper_ensure(godot::Node *p_root);
    static godot::Ref<NetwEntity> wrapper_resolve(godot::Node *p_node);
    static godot::Variant wrapper_held(
        godot::Node *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );

    static godot::Object *wrapper_bind(
        godot::Node *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );

    godot::RID entity_parent_of(const godot::RID &p_entity) const;

    godot::RID scene_of(const godot::RID &p_entity) const;

    godot::Ref<NetwSceneHandle> scene_handle_of(const godot::RID &p_entity);

    godot::RID scene_report_entity_edge(
        const godot::RID &p_subject,
        bool p_present,
        bool p_is_player
    );

    godot::Ref<NetwSceneHandle> entity_scene_facet(
        NetwEntityRecord *p_record,
        NetwEntity *p_wrapper
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
        NetwEntityRecord *p_record,
        godot::Node *p_owner,
        godot::Node *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    static void entity_move(
        NetwEntityRecord *p_record,
        godot::Node *p_owner,
        godot::Node *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    SyncPipeline *sync_pipeline() const;
    int64_t session_schema_identity() const;
    void set_session_api(godot::Object *p_api);
    godot::Object *session_api() const;
    godot::Ref<godot::MultiplayerAPI> get_session_api() const;

    bool session_is_active() const;

    static godot::TypedArray<godot::MultiplayerAPI> session_get_all();
    static godot::Ref<godot::MultiplayerAPI> session_of(godot::Node *p_node);
    static godot::Ref<NetwMultiplayer> core_of(godot::Node *p_node);
    static NetwMultiplayer *core_rooted_at(godot::Node *p_root);

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

    void entity_request_control(const godot::RID &p_entity);

    godot::RID entity_replicate(godot::Object *p_node, godot::Object *p_owner);

    godot::RID spawn_fn(
        const godot::Callable &p_function,
        const godot::Array &p_args,
        NetwParticipant *p_owner
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

    godot::RID entity_adopt(godot::Object *p_root);

    godot::TypedArray<godot::Dictionary> spawn_get_state(
        const godot::RID &p_entity
    );

    static void entity_enter_tree(
        godot::Object *p_wrapper,
        godot::Node *p_owner,
        NetwEntityRecord *p_record,
        NetwMultiplayer *p_session,
        bool p_is_authority
    );

    static void entity_wrapper_request_control(
        const godot::Ref<NetwEntity> &p_wrapper,
        godot::Node *p_owner,
        ReplicationCore *p_plane
    );

    static void entity_broadcast_control(
        const godot::Ref<NetwEntity> &p_wrapper,
        ReplicationCore *p_plane,
        int64_t p_peer
    );
    ReplicationCore *get_replication_plane() const;
    void replication_flush_all_buffers();
    godot::Node *replication_resolve_comp_node(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_comp,
        const godot::String &p_path
    );
    godot::Ref<NetwEntity> replication_adopt_in_place(godot::Node *p_root);
    void replication_handle_table_frame(
        const godot::PackedByteArray &p_payload,
        int64_t p_sender
    );
    void replication_settle_reply(
        int64_t p_txn,
        int64_t p_sender,
        int64_t p_route,
        int64_t p_comp,
        const godot::String &p_path
    );
    godot::Error replication_dispatch(
        int64_t p_route,
        int64_t p_comp,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload,
        const godot::String &p_path,
        int64_t p_sender,
        bool p_reliable,
        int64_t p_seq
    );
    void replication_clear_route(int64_t p_route);
    void replication_replay_tables(int64_t p_peer_id);

    godot::Error receive_carrier(
        const godot::PackedByteArray &p_framed,
        int64_t p_sender,
        bool p_reliable,
        int64_t p_seq,
        int64_t p_base_tick
    );

    godot::Ref<NetwPropertySetBinding> entity_derived_binding(
        godot::Node *p_owner,
        int64_t p_record,
        int64_t p_route
    );
    godot::Array entity_derived_group(int64_t p_route);

    bool entity_governs_property(
        godot::Node *p_owner,
        const godot::NodePath &p_path,
        godot::Node *p_exclude,
        int64_t p_route
    );
    godot::Node *entity_instantiate_from(
        godot::Node *p_template,
        const godot::Callable &p_configure
    );

    static godot::Node *entity_instantiate_copy(
        godot::Node *p_template,
        const godot::Callable &p_configure
    );

    godot::Node *entity_spawn_under(
        godot::Node *p_owner,
        godot::Node *p_parent,
        const godot::StringName &p_id
    );

    static godot::Node *entity_spawn_copy_under(
        godot::Node *p_owner,
        godot::Node *p_parent,
        const godot::StringName &p_id
    );

    godot::Node *entity_instantiate_player(
        godot::Node *p_owner,
        godot::Object *p_participant
    );

    godot::Node *entity_spawn_player(
        godot::Node *p_owner,
        NetwParticipant *p_participant,
        NetwSceneHandle *p_scene
    );

    void entity_linger(
        NetwEntityRecord *p_record,
        godot::Node *p_owner,
        int64_t p_pumps
    );

    void entity_free_owner(godot::Node *p_owner);

    godot::Dictionary entity_describe(int64_t p_route) const;

    void entity_note_stage(NetwEntityRecord *p_record, int64_t p_from);

    void entity_announce_reparented(
        const godot::Ref<NetwEntity> &p_wrapper,
        const NetwEntityRecord::MoveReport &p_report
    );
    void entity_refresh_moved_body(const godot::RID &p_entity, int64_t p_route);
    void entity_relocate_spatial_state(
        const EntityDeparture &p_row,
        int64_t p_tick
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
    static godot::StringName scene_container_meta();
    static bool scene_root_is_isolated(godot::Node *p_root);
    static godot::Node *scene_wrap_world(godot::Node *p_root);
    static godot::SubViewport *scene_world_of(godot::Node *p_root);
    static godot::Node *scene_outer_of(godot::Node *p_root);
    static godot::Node *scene_inner_of(godot::Node *p_node);
    static void scene_retire_world(godot::Node *p_root);
    static godot::StringName scene_packed_stem(
        const godot::Ref<godot::PackedScene> &p_packed
    );
    static void scene_set_host_view_factory(const godot::Callable &p_factory);
    static godot::Callable scene_host_view_factory();
    static ParticipantView *scene_placed_view(godot::Node *p_root);
    godot::Node *scene_container(const godot::StringName &p_stem) const;
    godot::Node *scene_existing_destination(
        const godot::Variant &p_destination
    );

    godot::Node *scene_spawn_node(
        const godot::Variant &p_data,
        int p_isolation
    );

    static godot::StringName scene_constructor_id();
    bool scene_owns_its_world(const godot::RID &p_scene) const;
    bool scene_hosts_isolated_world() const;
    void scene_ensure_host_view();
    godot::Node *scene_participant_player();
    godot::SubViewport *scene_first_active_viewport();
    godot::SubViewport *scene_resolve_participant_viewport();
    void scene_participant_display_invalidate();
    void scene_participant_display_settle();
    void scene_register_constructor();
    void scene_root_online(godot::Node *p_root);
    void scene_root_offline(godot::Object *p_root);
    void scene_release_host_view();
    godot::Node *scene_containing(godot::Node *p_node);
    godot::Node *scene_spawn(
        const godot::Variant &p_data,
        SceneIsolation p_isolation
    );
    godot::Node *scene_activate(const godot::Variant &p_destination);
    godot::Node *announce_scene_activated(godot::Node *p_active);
    godot::Node *scene_resolve_destination(const godot::Variant &p_destination);
    godot::TypedArray<NetwEntity> scene_players_in(godot::Node *p_container);
    godot::Ref<NetwSceneHandle> scene_handle_for(godot::Node *p_container);
    static double scene_request_deadline();
    godot::Ref<NetwPromise> scene_move_entity_to(
        const godot::Ref<NetwEntity> &p_mover,
        godot::Node *p_target
    );
    godot::Ref<NetwPromise> scene_replace_sources(
        const godot::Variant &p_destination,
        const godot::Array &p_sources
    );
    void scene_carry_transition();
    void scene_resume_transition(
        const godot::Ref<NetwEntity> &p_mover,
        const godot::Ref<NetwPromise> &p_moving
    );
    void scene_land_transition();
    godot::Array scene_sources_for_scope(
        int p_scope,
        godot::Node *p_requester,
        const godot::Ref<NetwParticipant> &p_participant
    );
    godot::Ref<NetwPromise> scene_apply_change(
        const godot::Ref<NetwParticipant> &p_participant,
        const godot::Variant &p_destination,
        int p_scope,
        godot::Node *p_requester
    );
    godot::Ref<NetwPromise> scene_front_door_change(
        godot::Node *p_requester,
        const godot::String &p_path,
        int p_scope
    );
    godot::Ref<NetwPromise> scene_change_to_file(
        godot::Node *p_requester,
        const godot::String &p_path,
        SceneChange p_scope
    );
    godot::Ref<NetwPromise> scene_change_to_packed(
        godot::Node *p_requester,
        const godot::Ref<godot::PackedScene> &p_packed,
        SceneChange p_scope
    );
    godot::Ref<NetwPromise> scene_reload_current(
        godot::Node *p_requester,
        SceneChange p_scope
    );
    void scene_pump_retired();
    bool scene_remember(godot::Node *p_root);
    void scene_arrive(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_source,
        godot::Node *p_target,
        const godot::Ref<NetwPromise> &p_promise
    );
    void scene_carry_begin(
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_body,
        godot::Node *p_source,
        godot::Node *p_target,
        const godot::Ref<NetwReparentOpts> &p_opts,
        const godot::Ref<NetwPromise> &p_promise
    );
    void scene_carry_open(int64_t p_id);
    void scene_set_carry_move(const godot::Callable &p_carry);
    bool scene_carry_reachable(int64_t p_id);
    void scene_carry_advance(int64_t p_id);
    void scene_carry_abandon(int64_t p_id);
    void scene_carry_sweep();
    void scene_carry_finish(int64_t p_id);

    static constexpr int GUARD_WINDOW_FRAMES = 2;
    godot::RID guard_hold(godot::Node *p_body);
    void guard_then(
        const godot::RID &p_guard,
        int p_frames,
        const godot::Callable &p_answered
    );
    void guard_spend_frame(godot::RID p_guard);
    void guard_close_window(const godot::RID &p_guard);
    void guard_let_go(const godot::RID &p_guard);

    void spawn_carry_begin(
        godot::Node *p_node,
        godot::Node *p_parent,
        const godot::Callable &p_adopt
    );
    void spawn_carry_open(int64_t p_id);
    bool spawn_carry_reachable(int64_t p_id);
    void spawn_carry_advance(int64_t p_id);
    void spawn_carry_abandon(int64_t p_id);
    void spawn_carry_sweep();
    void spawn_carry_finish(int64_t p_id);
    void scene_forget(godot::Object *p_container);
    void scene_settle_refresh();
    void scene_settle_sync_local();
    void scene_bind_local_participant(
        const godot::Ref<NetwParticipant> &p_participant
    );
    void scene_sync_local_participant();
    void scene_refresh_current();
    godot::RID scene_resolve_current() const;
    void scene_on_session_reclaimed();
    void scene_install();
    void scene_dispose();
    void scene_on_session_entered();
    void scene_announce_startup();
    void scene_on_native_change();

    static godot::NodePath relative_path(
        godot::Object *p_source,
        godot::Object *p_target
    );

    static godot::NodePath property_path(
        godot::Object *p_source,
        const godot::StringName &p_property,
        godot::Object *p_base
    );

    godot::Ref<NetwEntity> entity_get_view(const godot::RID &p_entity) const;

    godot::Node *wrapper_owner(const godot::RID &p_entity) const;
    godot::RID handle_of_wrapper(NetwEntity *p_wrapper) const;
    godot::Ref<NetwEntity> wrapper_for_route(int64_t p_route) const;

    godot::Ref<NetwEntity> wrapper_for_id(int64_t p_id) const;

    godot::TypedArray<godot::Object> wrapper_live() const;

    void wrapper_sweep_retired();
    void wrapper_clear();

    bool liveness_bind(
        const godot::RID &p_entity,
        int64_t p_route,
        const godot::Ref<NetwEntity> &p_wrapper,
        NetwEntityRecord *p_record,
        godot::Node *p_owner
    );

    godot::Error liveness_adopt_route(
        int64_t p_route,
        const godot::Ref<godot::RefCounted> &p_wrapper,
        NetwEntityRecord *p_record,
        godot::Node *p_owner
    );

    void liveness_publish_live(int64_t p_route);

    void liveness_settle_local_player(int64_t p_route);

    godot::Ref<NetwParticipant> participant_ensure(int64_t p_peer);
    void participant_adopt(
        int64_t p_peer,
        const godot::Ref<NetwParticipant> &p_participant
    );
    godot::Ref<NetwParticipant> participant_of(int64_t p_peer) const;
    godot::Ref<NetwParticipant> participant_joined_of(int64_t p_peer);
    godot::TypedArray<NetwParticipant> participant_joined_all();
    godot::Ref<NetwParticipant> participant_local();
    bool participant_has(int64_t p_peer) const;
    godot::TypedArray<NetwParticipant> participant_all() const;
    void participant_forget(int64_t p_peer);
    void participant_clear();

    godot::RID participant_seat(int64_t p_peer) const;
    bool participant_take_seat(int64_t p_peer, const godot::RID &p_scene);
    bool participant_leave_seat(int64_t p_peer, const godot::RID &p_scene);
    bool participant_seat_move(int64_t p_peer, const godot::RID &p_scene);
    bool participant_seat_clear(int64_t p_peer, const godot::RID &p_scene);
    bool participant_move_seat(int64_t p_peer, const godot::RID &p_scene);
    godot::PackedInt64Array participant_seated_in(
        const godot::RID &p_scene
    ) const;

    bool participant_admit(int64_t p_peer);
    godot::Ref<NetwParticipant> participant_admitted_of(int64_t p_peer) const;
    godot::TypedArray<godot::Object> participant_admitted_all() const;
    godot::Ref<NetwParticipant> participant_admitted_local();
    godot::Ref<NetwParticipant> scene_requester_participant(
        godot::Node *p_requester
    );
    void participant_publish_joined(int64_t p_peer);

    bool liveness_linger(const godot::RID &p_entity);

    bool liveness_retire(int64_t p_route);
    void liveness_release(int64_t p_route);
    void liveness_announce_dead(int64_t p_route);
    void liveness_drop_hooks(int64_t p_route);

    bool liveness_hide(int64_t p_route);

    void liveness_unindex_wrapper(const godot::RID &p_entity);
    void liveness_forget_wrapper(const godot::RID &p_entity);

    int64_t liveness_reserve_route();

    int64_t liveness_allocate_route(godot::Object *p_wrapper);

    bool liveness_bind_route(int64_t p_route, godot::Object *p_wrapper);

    void liveness_bind_routes_data(const godot::PackedInt64Array &p_routes);
    void liveness_tombstone_routes_data(
        const godot::PackedInt64Array &p_routes
    );

    int64_t liveness_route_of(godot::Object *p_wrapper) const;
    int64_t liveness_state_of(NetwEntity *p_wrapper) const;
    EntityState liveness_route_state(int64_t p_route) const;
    int64_t liveness_route_epoch(int64_t p_route) const;
    bool liveness_epoch_admits(int64_t p_route, int64_t p_epoch) const;
    bool liveness_adopt_epoch(int64_t p_route, int64_t p_epoch);
    int64_t liveness_route_wire_life(int64_t p_route) const;
    godot::Error entity_frame_verdict(int64_t p_route) const;
    godot::Error sync_admit_frame_default(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_comp,
        int64_t p_channel,
        int64_t p_flags,
        int64_t p_tick,
        const godot::PackedByteArray &p_payload
    ) const;

    godot::Error sync_admit_frame(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_comp,
        int64_t p_channel,
        int64_t p_flags,
        int64_t p_tick,
        const godot::PackedByteArray &p_payload
    ) const;

    GDVIRTUAL7RC(
        godot::Error,
        _sync_admit_frame,
        int64_t,
        int64_t,
        int64_t,
        int64_t,
        int64_t,
        int64_t,
        godot::PackedByteArray
    )

    godot::Error predict_admit_frame_default(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    godot::Error predict_admit_frame(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        const godot::PackedByteArray &p_payload
    );

    GDVIRTUAL4R(
        godot::Error,
        _predict_admit_frame,
        int64_t,
        int64_t,
        int64_t,
        godot::PackedByteArray
    )

    godot::RID liveness_adopt(NetwEntity *p_wrapper);
    godot::RID entity_of(godot::Object *p_node);
    godot::StringName scene_stem(const godot::RID &p_scene) const;

    int64_t scene_route_of(const godot::RID &p_scene) const;
    godot::StringName scene_layer_id(const godot::RID &p_scene) const;

    godot::Ref<NetwInterestLayer> scene_layer_view(
        const godot::RID &p_scene
    ) const;
    godot::PackedInt32Array scene_get_peers(const godot::RID &p_scene) const;

    void set_scene_participant_edge(const godot::Callable &p_edge);
    godot::Error scene_admit(const godot::RID &p_scene, int64_t p_peer);
    void scene_adopt_entity(const godot::RID &p_entity);
    bool scene_release(const godot::RID &p_scene, int64_t p_peer);
    bool scene_admits(const godot::RID &p_scene, int64_t p_peer) const;
    bool scene_leaves_route_unadmitted(int64_t p_route) const;
    bool scene_is_declared(const godot::RID &p_entity) const;
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
    godot::RID scene_seat_subject(int64_t p_route) const;
    void scene_publish_seat(
        const godot::RID &p_scene,
        int64_t p_peer,
        bool p_present
    );
    void scene_send_seat(
        int64_t p_target,
        int64_t p_route,
        int64_t p_peer,
        bool p_present
    );
    void scene_send_seat_roster(const godot::RID &p_scene, int64_t p_target);
    void scene_apply_seat(int64_t p_route, int64_t p_peer, bool p_present);
    void scene_drain_parked_seats(const godot::RID &p_scene);
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

    static godot::StringName scene_move_reason();
    godot::Ref<NetwPromise> scene_move_entity(
        const godot::RID &p_entity,
        const godot::RID &p_destination,
        const godot::Variant &p_opts
    );

    godot::TypedArray<NetwEntity> scene_players_of(int64_t p_peer);
    godot::Ref<NetwPromise> participant_travel(
        const godot::Ref<NetwParticipant> &p_participant,
        const godot::Ref<NetwSceneHandle> &p_destination
    );
    void scene_travel_land(
        int64_t p_peer,
        const godot::RID &p_destination,
        const godot::Array &p_pending,
        int p_at,
        const godot::Ref<NetwPromise> &p_settled
    );
    godot::HashSet<int64_t> scene_travel_reserved;
    godot::Ref<NetwGroupPromise> scene_move_participants(
        const godot::RID &p_scene,
        const godot::PackedInt32Array &p_peers
    );
    void scene_report_moved(
        const godot::Ref<NetwGroupPromise> &p_batch,
        const godot::PackedInt32Array &p_peers
    );

    godot::Node *liveness_node_of(int64_t p_route) const;
    godot::TypedArray<godot::Object> liveness_get_entities() const;

    void liveness_schedule_when_live(
        int64_t p_route,
        const godot::Callable &p_callback,
        int64_t p_deadline,
        bool p_on_clock,
        const godot::Callable &p_on_timeout
    );
    int64_t liveness_pending_live_count() const;

    void liveness_poll(int64_t p_clock_tick);

    void liveness_poll_now();

    void liveness_clear_session();
    void liveness_settle_clear_session();

    godot::Ref<NetwEntity> scene_player_local() const;

private:
    void set_local_player(const godot::Ref<NetwEntity> &p_player, int64_t p_id);

    static NetwEntityRecord *record_of_wrapper(godot::Object *p_wrapper);
    static godot::Node *owner_of_wrapper(godot::Object *p_wrapper);
    godot::Callable despawning_hook(godot::Object *p_wrapper);

    entity::Outcome entity_departure_outcome(
        const EntityDeparture &p_row
    ) const;
    void entity_capture_persistence(EntityDeparture &r_row);
    void entity_capture_seat(EntityDeparture &r_row, NetwEntity *p_entity);
    void entity_release_seats(
        const EntityDeparture &p_row,
        const godot::RID &p_keep
    );
    void entity_commit_move(const EntityDeparture &p_row);
    void entity_commit_death(const EntityDeparture &p_row);
    void entity_commit_hide(const EntityDeparture &p_row);
    void entity_release_body(
        const godot::RID &p_entity,
        const godot::Ref<NetwEntity> &p_wrapper,
        int64_t p_route
    );

public:
    void entity_capture_exit(godot::Object *p_wrapper);
    void entity_settle_departure(int64_t p_instance);
    void liveness_owner_despawning(
        const godot::StringName &p_reason,
        godot::Object *p_wrapper
    );

public:
    static bool is_coroutine(const godot::Variant &p_value);

    bool script_overrides_seam(
        const godot::Ref<godot::Script> &p_script,
        const godot::StringName &p_base_name,
        const godot::StringName &p_seam
    );
    bool overrides_seam(const godot::StringName &p_seam);
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
    void seam_refused(
        const godot::StringName &p_seam,
        int64_t p_route,
        const godot::String &p_reason
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwMultiplayer::EmbedPhase);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::TableParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::NameVerdict);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::TransportMode);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SessionState);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::Role);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SceneMove);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SceneDestination);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::RecordKind);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::EntityState);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SyncMode);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::MismatchAction);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::ClockParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::ClockMonitor);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::LayerParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::LayerPolicy);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::LeavePolicy);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::PerceptionPolicy);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::TransportParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::TransportCapability);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::EndpointParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::EndpointState);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::EndpointFlags);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SceneParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SceneEvent);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SceneChange);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::SceneIsolation);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::DisplayParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::DisplayRole);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::DisplayPump);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::PredictedMode);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::TimelineMode);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::PredictParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::IslandParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::MemberParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::WritePolicy);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::ColumnParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::PropertySetParam);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::ColumnType);
VARIANT_ENUM_CAST(netw::NetwMultiplayer::Stat);
