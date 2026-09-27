#pragma once

#include "godot/variant.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/api/session_stats.hpp"
#include "netw/clock_engine.hpp"
#include "netw/connect/creation.hpp"
#include "netw/display/decl.hpp"
#include "netw/interest/engine.hpp"
#include "netw/liveness_core.hpp"
#include "netw/scene_core.hpp"
#include "netw/session_core.hpp"

namespace netw::enums {

struct NetwMultiplayer {
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
        CLOCK_MONITOR_TICK_ACCUMULATOR = 5,
        CLOCK_MONITOR_PHYSICS_FRAMES = 6,
        CLOCK_MONITOR_POLLS = 7,
        CLOCK_MONITOR_WALL_SECONDS = 8,
        CLOCK_MONITOR_PHYSICS_HZ = 9,
        CLOCK_MONITOR_POLL_HZ = 10,
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

    enum LeavePolicy : int64_t {
        LEAVE_POLICY_HIDE = interest::Decl::LEAVE_HIDE,
        LEAVE_POLICY_RETAIN = interest::Decl::LEAVE_RETAIN,
        LEAVE_POLICY_CUSTOM = interest::Decl::LEAVE_CUSTOM,
    };

    enum EmbedPhase {
        EMBED_PHASE_DECLARING = 0,
        EMBED_PHASE_SETTLING = 1,
        EMBED_PHASE_LIVE = 2,
    };

    enum PerceptionPolicy : int64_t {
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
        SCENE_EVENT_VIEWER = 0,
        SCENE_EVENT_BODY = 1,
        SCENE_EVENT_ENTITY = 2,
    };

    enum SceneChange {
        SCENE_CHANGE_SESSION = NetwSceneCore::SCOPE_SESSION,
        SCENE_CHANGE_PLAYER = NetwSceneCore::SCOPE_PLAYER,
        SCENE_CHANGE_SCENE = NetwSceneCore::SCOPE_SCENE,
    };

    enum SceneIsolation {
        SCENE_ISOLATION_NONE = 0,
        SCENE_ISOLATION_OWN_WORLD = 1,
    };

    enum DisplayParam {
        DISPLAY_PARAM_ROLE = display::PARAM_ROLE,
        DISPLAY_PARAM_LIVE_MODE = display::PARAM_LIVE_MODE,
        DISPLAY_PARAM_LIVE_SMOOTH_TIME = display::PARAM_LIVE_SMOOTH_TIME,
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

    enum LiveMode {
        LIVE_MODE_CHASE = display::LIVE_CHASE,
        LIVE_MODE_BRACKETED = display::LIVE_BRACKETED,
    };

    enum TimelineMode {
        TIMELINE_MODE_BUFFERED = display::TIMELINE_BUFFERED,
        TIMELINE_MODE_FORECAST = display::TIMELINE_FORECAST,
    };

    enum PredictParam {
        PREDICT_PARAM_ARCHETYPE = 0,
        PREDICT_PARAM_MISSING_POLICY = 2,
        PREDICT_PARAM_RECOVERY_POLICY = 3,
        PREDICT_PARAM_TELEPORT_THRESHOLD = 6,
        PREDICT_PARAM_DIVERGENCE_EPSILON = 7,
        PREDICT_PARAM_BREACH_RESPONSE = 8,
        PREDICT_PARAM_COLLISION_COOLDOWN_TICKS = 10,
        PREDICT_PARAM_MAX_CONSUME_PER_TICK = 11,
        PREDICT_PARAM_MAX_CONSUME_LAG_TICKS = 12,
        PREDICT_PARAM_CONSUME_BUFFER_TICKS = 13,
        PREDICT_PARAM_REPLAY_BUFFER_DEPTH = 14,
    };

    enum SimulationParam {
        SIMULATION_PARAM_BODIES = 0,
        SIMULATION_PARAM_SCHEDULE = 1,
        SIMULATION_PARAM_REPLICAS = 2,
        SIMULATION_PARAM_RESTORE = 3,
        SIMULATION_PARAM_MAX_RESTORE_TICKS = 4,
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
        SET_PARAM_HEARTBEAT = 10,
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
        COLUMN_STRING = SchemaCore::STRING,
    };

    enum DatabaseState {
        DATABASE_CLOSED = 0,
        DATABASE_OPENING = 1,
        DATABASE_OPEN = 2,
        DATABASE_CLOSING = 3,
        DATABASE_FAULTED = 4,
    };

    enum Stat {
#define NETW_SESSION_STAT_ENUM(m_name, m_key) STAT_##m_name,
        NETW_SESSION_STAT_TABLE(NETW_SESSION_STAT_ENUM)
#undef NETW_SESSION_STAT_ENUM
            STAT_COUNT,
    };
};

struct Netw {
    enum SceneChange {
        SCENE_CHANGE_SESSION = NetwSceneCore::SCOPE_SESSION,
        SCENE_CHANGE_PLAYER = NetwSceneCore::SCOPE_PLAYER,
        SCENE_CHANGE_SCENE = NetwSceneCore::SCOPE_SCENE,
    };

    enum SceneIsolation {
        SCENE_ISOLATION_NONE = NetwSceneCore::ISOLATION_NONE,
        SCENE_ISOLATION_OWN_WORLD = NetwSceneCore::ISOLATION_OWN_WORLD,
    };
};

struct LobbyDirectory {
    enum Capability {
        CAPABILITY_BROWSE = 1,
        CAPABILITY_FRIENDS_ONLY_SUPPORT = 2,
        CAPABILITY_INVITES = 4,
        CAPABILITY_FRIEND_NAMES = 8,
    };
};

} // namespace netw::enums

VARIANT_ENUM_CAST(netw::enums::Netw::SceneChange);
VARIANT_ENUM_CAST(netw::enums::Netw::SceneIsolation);
VARIANT_ENUM_CAST(netw::enums::LobbyDirectory::Capability);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::EmbedPhase);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::TableParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::TransportMode);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SessionState);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::Role);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SceneMove);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SceneDestination);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::EntityState);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SyncMode);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::MismatchAction);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::ClockParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::ClockMonitor);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::LayerParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::LayerPolicy);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::LeavePolicy);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::PerceptionPolicy);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::TransportParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::TransportCapability);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::EndpointParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::EndpointState);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::EndpointFlags);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SceneParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SceneEvent);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SceneChange);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SceneIsolation);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::DisplayParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::DisplayRole);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::DisplayPump);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::LiveMode);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::TimelineMode);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::PredictParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::SimulationParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::WritePolicy);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::ColumnParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::PropertySetParam);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::ColumnType);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::DatabaseState);
VARIANT_ENUM_CAST(netw::enums::NetwMultiplayer::Stat);
