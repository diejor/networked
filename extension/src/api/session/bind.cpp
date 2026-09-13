#include "netw/api/interest_handle.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"

#include "netw/api/participant.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/packed_scene.hpp"
#include "godot/physics_server.hpp"
#include "godot/rendering_server.hpp"
#include "godot/resource.hpp"
#include "godot/scene_tree.hpp"
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
#include "netw/carrier_frame.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/identity.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

const char *SIG_CLOCK_AFTER_TICK = "clock_after_tick";
const char *SIG_CLOCK_AFTER_TICK_LOOP = "clock_after_tick_loop";
const char *SIG_CLOCK_BEFORE_TICK = "clock_before_tick";
const char *SIG_CLOCK_BEFORE_TICK_LOOP = "clock_before_tick_loop";
const char *SIG_CLOCK_ON_TICK = "clock_on_tick";
const char *SIG_CLOCK_PONG_RECEIVED = "clock_pong_received";
const char *SIG_CLOCK_STABILITY_CHANGED = "clock_stability_changed";
const char *SIG_CLOCK_SYNCHRONIZED = "clock_synchronized";
const char *SIG_CLOCK_CONFIGURED = "clock_configured";
const char *SIG_CLOCK_TICKRATE_MISMATCH = "clock_tickrate_mismatch";
const char *SIG_ENTITY_DEAD = "entity_dead";
const char *SIG_ENTITY_HIDDEN = "entity_hidden";
const char *SIG_ENTITY_LINGERING = "entity_lingering";
const char *SIG_ENTITY_LIVE = "entity_live";
const char *SIG_PARTICIPANT_JOINED = "participant_joined";
const char *SIG_PARTICIPANT_LOCAL_JOINED = "participant_local_joined";
const char *SIG_PARTICIPANT_VIEWPORT_CHANGED = "participant_viewport_changed";
const char *SIG_PEER_AUTHENTICATING = "peer_authenticating";
const char *SIG_PEER_AUTHENTICATION_FAILED = "peer_authentication_failed";
const char *SIG_PEER_KICKED = "peer_kicked";
const char *SIG_PEER_KICK_REQUESTED = "peer_kick_requested";
const char *SIG_PEER_PACKET = "peer_packet";
const char *SIG_SCENE_ACTIVATED = "scene_activated";
const char *SIG_SCENE_DESPAWNED = "scene_despawned";
const char *SIG_SCENE_ENTITY_MOVED = "scene_entity_moved";
const char *SIG_SCENE_LIVE = "scene_live";
const char *SIG_SCENE_LOCAL_CHANGED = "scene_local_changed";
const char *SIG_SCENE_LOCAL_PLAYER_CHANGED = "scene_local_player_changed";
const char *SIG_SCENE_SPAWNED = "scene_spawned";
const char *SIG_SCENE_STARTUP_SPAWNED = "scene_startup_spawned";
const char *SIG_SERVICE_REGISTERED = "service_registered";
const char *SIG_SERVICE_UNREGISTERED = "service_unregistered";
const char *SIG_SESSION_DISCONNECT_REQUESTED = "session_disconnect_requested";
const char *SIG_SESSION_ENDED = "session_ended";
const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_JOIN_FAILED = "session_join_failed";
const char *SIG_SESSION_POLL_STARTED = "session_poll_started";
const char *SIG_SESSION_RECLAIMED = "session_reclaimed";
const char *SIG_SESSION_SERVER_DISCONNECTING = "session_server_disconnecting";
const char *SIG_SESSION_STATE_CHANGED = "session_state_changed";
const char *SIG_SESSION_TREE_PAUSED = "session_tree_paused";
const char *SIG_SESSION_TREE_UNPAUSED = "session_tree_unpaused";
const char *SIG_TABLE_RECEIVED = "table_received";

} // namespace

void NetwMultiplayer::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("predict_flush_tap"),
        &NetwMultiplayer::predict_flush_tap
    );
    ClassDB::bind_method(
        D_METHOD("predict_get_tap_cost"),
        &NetwMultiplayer::predict_get_tap_cost
    );
    ClassDB::bind_method(
        D_METHOD("session_flush_deferred"),
        &NetwMultiplayer::session_flush_deferred
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer", "name"),
        &NetwMultiplayer::interest_layer
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_named", "name"),
        &NetwMultiplayer::interest_layer_named
    );
    ClassDB::bind_method(
        D_METHOD("interest_layers"),
        &NetwMultiplayer::interest_layers
    );
    ClassDB::bind_method(
        D_METHOD("interest_participant_sees", "peer_id", "entity"),
        &NetwMultiplayer::interest_participant_sees
    );
    ClassDB::bind_method(
        D_METHOD("sync_gather_set_default", "entity", "comp"),
        &NetwMultiplayer::sync_gather_set_default
    );
    ClassDB::bind_method(
        D_METHOD("sync_apply_set_default", "entity", "comp", "values"),
        &NetwMultiplayer::sync_apply_set_default
    );
    GDVIRTUAL_BIND(_sync_gather_set, "entity", "comp");
    GDVIRTUAL_BIND(_sync_apply_set, "entity", "comp", "values");
    ClassDB::bind_method(
        D_METHOD("interest_resolved_layer_ids", "entity"),
        &NetwMultiplayer::interest_resolved_layer_ids
    );
    ClassDB::bind_method(
        D_METHOD("interest_shared_entities", "entity", "layer_id"),
        &NetwMultiplayer::interest_shared_entities,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("interest_monitor_snapshot"),
        &NetwMultiplayer::interest_monitor_snapshot
    );
    ClassDB::bind_method(
        D_METHOD("interest_wire_admits", "peer_id", "entity"),
        &NetwMultiplayer::interest_wire_admits
    );
    ClassDB::bind_method(
        D_METHOD("interest_entity_has_filter", "entity"),
        &NetwMultiplayer::interest_entity_has_filter
    );
    ClassDB::bind_method(
        D_METHOD("rpc_get_recipients", "entity"),
        &NetwMultiplayer::rpc_get_recipients
    );
    ClassDB::bind_method(
        D_METHOD("clock_set_mismatch_action", "action"),
        &NetwMultiplayer::clock_set_mismatch_action
    );
    ClassDB::bind_method(
        D_METHOD("clock_auto_configure_offset"),
        &NetwMultiplayer::clock_auto_configure_offset
    );
    ClassDB::bind_method(
        D_METHOD("clock_request_handshake"),
        &NetwMultiplayer::clock_request_handshake
    );
    ClassDB::bind_method(
        D_METHOD("clock_send_ping"),
        &NetwMultiplayer::clock_send_ping
    );
    ClassDB::bind_method(
        D_METHOD("scene_request", "path", "scope"),
        &NetwMultiplayer::scene_request,
        DEFVAL(SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_method(
        D_METHOD("rpc_get_relay_sender"),
        &NetwMultiplayer::rpc_get_relay_sender
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "relay_sender"),
        "",
        "rpc_get_relay_sender"
    );
    ClassDB::bind_method(
        D_METHOD("rpc_call", "callable", "args", "peer"),
        &NetwMultiplayer::rpc_call
    );
    ClassDB::bind_method(
        D_METHOD(
            "rpc_request_call",
            "peer",
            "callable",
            "args",
            "timeout_seconds"
        ),
        &NetwMultiplayer::rpc_request_call,
        DEFVAL(NetwMultiplayer::rpc_request_timeout_default())
    );
    ClassDB::bind_method(
        D_METHOD(
            "rpc_request_call_group",
            "callable",
            "args",
            "timeout_seconds"
        ),
        &NetwMultiplayer::rpc_request_call_group,
        DEFVAL(NetwMultiplayer::rpc_request_timeout_default())
    );
    ClassDB::bind_method(
        D_METHOD("rpc_send_reply", "peer", "route", "txn", "value"),
        &NetwMultiplayer::rpc_send_reply
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_metrics"),
        &NetwMultiplayer::lagcomp_metrics
    );
    ClassDB::bind_method(
        D_METHOD("stats_snapshot"),
        &NetwMultiplayer::stats_snapshot
    );
    ClassDB::bind_method(
        D_METHOD("stats_get", "stat"),
        &NetwMultiplayer::stats_get
    );
    ClassDB::bind_method(
        D_METHOD("attribution_set_armed", "armed"),
        &NetwMultiplayer::attribution_set_armed
    );
    ClassDB::bind_method(
        D_METHOD("attribution_is_armed"),
        &NetwMultiplayer::attribution_is_armed
    );
    ClassDB::bind_method(
        D_METHOD("attribution_snapshot"),
        &NetwMultiplayer::attribution_snapshot
    );
    ClassDB::bind_method(
        D_METHOD("scene_participant_viewport"),
        &NetwMultiplayer::scene_participant_viewport
    );
    ClassDB::bind_method(
        D_METHOD("scene_observe", "scene", "event", "callback"),
        &NetwMultiplayer::scene_observe
    );
    ClassDB::bind_method(
        D_METHOD("scene_unobserve", "scene", "event", "callback"),
        &NetwMultiplayer::scene_unobserve
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_find", "name"),
        &NetwMultiplayer::interest_layer_find
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_view", "layer"),
        &NetwMultiplayer::interest_layer_view
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_set_monitor_callback", "layer", "callback"),
        &NetwMultiplayer::interest_layer_set_monitor_callback
    );
    ClassDB::bind_method(
        D_METHOD("interest_is_filtered", "entity"),
        &NetwMultiplayer::interest_is_filtered
    );
    ClassDB::bind_method(
        D_METHOD("persist_set_quit_guard", "guard"),
        &NetwMultiplayer::persist_set_quit_guard
    );
    ClassDB::bind_method(
        D_METHOD("persist_set_drain", "drain"),
        &NetwMultiplayer::persist_set_drain
    );
    ClassDB::bind_method(
        D_METHOD("persist_tick_default", "delta"),
        &NetwMultiplayer::persist_tick_default
    );
    ClassDB::bind_method(
        D_METHOD("persist_shutdown"),
        &NetwMultiplayer::persist_shutdown
    );
    ClassDB::bind_method(
        D_METHOD("session_get_state"),
        &NetwMultiplayer::session_get_state
    );
    ClassDB::bind_method(
        D_METHOD("session_get_role"),
        &NetwMultiplayer::session_get_role
    );
    ClassDB::bind_method(D_METHOD("is_online"), &NetwMultiplayer::is_online);
    ClassDB::bind_method(D_METHOD("is_host"), &NetwMultiplayer::is_host);
    ClassDB::bind_method(
        D_METHOD("is_local_client"),
        &NetwMultiplayer::is_local_client
    );
    ClassDB::bind_method(
        D_METHOD("session_set_inner", "inner"),
        &NetwMultiplayer::session_set_inner
    );
    ClassDB::bind_method(
        D_METHOD("session_get_inner"),
        &NetwMultiplayer::session_get_inner
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "inner",
            PROPERTY_HINT_RESOURCE_TYPE,
            "SceneMultiplayer",
            PROPERTY_USAGE_NONE
        ),
        "session_set_inner",
        "session_get_inner"
    );
    ClassDB::bind_method(
        D_METHOD("session_set_root", "reader"),
        &NetwMultiplayer::session_set_root
    );
    ClassDB::bind_method(
        D_METHOD("session_root"),
        &NetwMultiplayer::session_root
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwMultiplayer::clear);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "root",
            PROPERTY_HINT_NODE_TYPE,
            "Node",
            PROPERTY_USAGE_NONE,
            "Node"
        ),
        "",
        "session_root"
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_add", "transport", "address", "display_name"),
        &NetwMultiplayer::endpoint_add,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_remove", "endpoint"),
        &NetwMultiplayer::endpoint_remove
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_find", "transport", "address"),
        &NetwMultiplayer::endpoint_find
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_list"),
        &NetwMultiplayer::endpoint_list
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_get_param", "endpoint", "param"),
        &NetwMultiplayer::endpoint_get_param
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_set_param", "endpoint", "param", "value"),
        &NetwMultiplayer::endpoint_set_param
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_get_state", "endpoint", "state"),
        &NetwMultiplayer::endpoint_get_state
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_probe", "endpoint"),
        &NetwMultiplayer::endpoint_probe
    );
    ClassDB::bind_method(
        D_METHOD("endpoint_refresh"),
        &NetwMultiplayer::endpoint_refresh
    );
    ClassDB::bind_method(
        D_METHOD(
            "transport_create_peer",
            "transport",
            "mode",
            "address",
            "settings",
            "completed",
            "progress"
        ),
        &NetwMultiplayer::transport_create_peer,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD("transport_cancel_peer_creation", "ticket"),
        &NetwMultiplayer::transport_cancel_peer_creation
    );
    ClassDB::bind_method(
        D_METHOD("transport_register", "type"),
        &NetwMultiplayer::transport_register
    );
    ClassDB::bind_method(
        D_METHOD("transport_unregister", "transport"),
        &NetwMultiplayer::transport_unregister
    );
    ClassDB::bind_method(
        D_METHOD("transport_list"),
        &NetwMultiplayer::transport_list
    );
    ClassDB::bind_method(
        D_METHOD("transport_find", "peer_class"),
        &NetwMultiplayer::transport_find
    );
    ClassDB::bind_method(
        D_METHOD("transport_get_param", "transport", "param"),
        &NetwMultiplayer::transport_get_param
    );
    ClassDB::bind_method(
        D_METHOD("peer_join_address"),
        &NetwMultiplayer::peer_join_address
    );
    ClassDB::bind_method(
        D_METHOD("session_get_join_schema"),
        &NetwMultiplayer::session_get_join_schema
    );
    ClassDB::bind_method(
        D_METHOD("session_get_config"),
        &NetwMultiplayer::session_get_config
    );
    ClassDB::bind_method(
        D_METHOD("session_set_server_info", "info"),
        &NetwMultiplayer::session_set_server_info
    );
    ClassDB::bind_method(
        D_METHOD("peer_set_identity", "peer", "identity"),
        &NetwMultiplayer::peer_set_identity
    );
    ClassDB::bind_method(
        D_METHOD("peer_get_identity", "peer"),
        &NetwMultiplayer::peer_get_identity
    );
    ClassDB::bind_method(
        D_METHOD("session_get_authored_role"),
        &NetwMultiplayer::session_get_authored_role
    );
    ClassDB::bind_method(
        D_METHOD(
            "send_to",
            "peer",
            "route",
            "channel",
            "payload",
            "reliable",
            "comp",
            "path",
            "batched"
        ),
        &NetwMultiplayer::send_to,
        DEFVAL(0),
        DEFVAL(String()),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("sync_note_sent_default", "peer", "sequence"),
        &NetwMultiplayer::sync_note_sent_default
    );
    ClassDB::bind_method(
        D_METHOD("sync_note_ack_default", "peer", "sequence"),
        &NetwMultiplayer::sync_note_ack_default
    );
    ClassDB::bind_method(
        D_METHOD("sync_explain", "route", "comp", "peer"),
        &NetwMultiplayer::sync_explain
    );
    ClassDB::bind_method(
        D_METHOD("peer_link_stats", "peer"),
        &NetwMultiplayer::peer_link_stats
    );
    ClassDB::bind_method(
        D_METHOD("session_flush_tick", "tick"),
        &NetwMultiplayer::session_flush_tick
    );
    ClassDB::bind_method(
        D_METHOD("peer_ack", "peer"),
        &NetwMultiplayer::peer_ack
    );
    ClassDB::bind_method(
        D_METHOD("stats_get_verdict_count", "verdict"),
        &NetwMultiplayer::stats_get_verdict_count
    );
    ClassDB::bind_method(
        D_METHOD("session_defer", "fn", "key"),
        &NetwMultiplayer::session_defer,
        DEFVAL(godot::StringName())
    );
    ClassDB::bind_method(
        D_METHOD("session_cancel_deferred", "key"),
        &NetwMultiplayer::session_cancel_deferred
    );
    ClassDB::bind_method(
        D_METHOD("schema_create", "name"),
        &NetwMultiplayer::schema_create
    );
    ClassDB::bind_method(
        D_METHOD("schema_add_column", "schema", "key", "type", "stride"),
        &NetwMultiplayer::schema_add_column,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD(
            "schema_set_column_quantizer",
            "schema",
            "column",
            "quantizer"
        ),
        &NetwMultiplayer::schema_set_column_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("schema_seal", "schema"),
        &NetwMultiplayer::schema_seal
    );
    ClassDB::bind_method(
        D_METHOD("schema_find", "name"),
        &NetwMultiplayer::schema_find
    );
    ClassDB::bind_method(
        D_METHOD("schema_find_or_adopt", "name"),
        &NetwMultiplayer::schema_find_or_adopt
    );
    ClassDB::bind_method(
        D_METHOD("table_find_or_adopt", "name"),
        &NetwMultiplayer::table_find_or_adopt
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_name", "schema"),
        &NetwMultiplayer::schema_get_name
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("schema_get_element_type", "type"),
        &NetwMultiplayer::schema_get_element_type
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_hash", "schema"),
        &NetwMultiplayer::schema_get_hash
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_count", "schema"),
        &NetwMultiplayer::schema_get_column_count
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_key", "schema", "column"),
        &NetwMultiplayer::schema_get_column_key
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_type", "schema", "column"),
        &NetwMultiplayer::schema_get_column_type
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_stride", "schema", "column"),
        &NetwMultiplayer::schema_get_column_stride
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_effect_arm", "key", "revert", "timeout_ticks"),
        &NetwMultiplayer::lagcomp_effect_arm,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_effect_watch", "key", "confirmed", "denied"),
        &NetwMultiplayer::lagcomp_effect_watch
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_effect_adopt", "key"),
        &NetwMultiplayer::lagcomp_effect_adopt
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_effect_discard", "key"),
        &NetwMultiplayer::lagcomp_effect_discard
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_effect_pending", "key"),
        &NetwMultiplayer::lagcomp_effect_pending
    );
    ClassDB::bind_method(
        D_METHOD(
            "event_watch",
            "events",
            "target",
            "predicate",
            "sink",
            "opts"
        ),
        &NetwMultiplayer::event_watch,
        DEFVAL(Dictionary()),
        DEFVAL(Dictionary()),
        DEFVAL(Callable()),
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("event_unwatch", "id"),
        &NetwMultiplayer::event_unwatch
    );
    ClassDB::bind_method(
        D_METHOD("event_watches"),
        &NetwMultiplayer::event_watches
    );
    ClassDB::bind_method(
        D_METHOD("event_ring", "route"),
        &NetwMultiplayer::event_ring
    );
    ClassDB::bind_method(
        D_METHOD("event_arm", "enabled"),
        &NetwMultiplayer::event_arm
    );
    for (int index = 0; index < EventPlane::taxonomy_size(); index++) {
        const int64_t value = EventPlane::value_at(index);
        ClassDB::bind_integer_constant(
            get_class_static(),
            StringName("Event"),
            StringName(String("EVENT_") + String(EventPlane::name_of(value))),
            value
        );
    }
    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName("Phase"),
        StringName("EVENT_PHASE_BEFORE"),
        EventPlane::BEFORE
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName("Phase"),
        StringName("EVENT_PHASE_AFTER"),
        EventPlane::AFTER
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName(),
        StringName("CARRIER_MAGIC_RELIABLE"),
        NetwCarrierFrame::MAGIC_RELIABLE
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName(),
        StringName("CARRIER_MAGIC_UNRELIABLE"),
        NetwCarrierFrame::MAGIC_UNRELIABLE
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName(),
        StringName("CARRIER_MAGIC_UNRELIABLE_ACKED"),
        NetwCarrierFrame::MAGIC_UNRELIABLE_ACKED
    );
    ClassDB::bind_method(
        D_METHOD("table_create", "schema"),
        &NetwMultiplayer::table_create
    );
    ClassDB::bind_method(
        D_METHOD("table_get_schema", "table"),
        &NetwMultiplayer::table_get_schema
    );
    ClassDB::bind_method(
        D_METHOD("table_set_param", "table", "param", "value"),
        &NetwMultiplayer::table_set_param
    );
    ClassDB::bind_method(
        D_METHOD("table_find", "name"),
        &NetwMultiplayer::table_find
    );
    ClassDB::bind_method(
        D_METHOD("table_get_wire_hash", "table"),
        &NetwMultiplayer::table_get_wire_hash
    );
    ClassDB::bind_method(
        D_METHOD("table_write_routes", "table", "routes"),
        &NetwMultiplayer::table_write_routes
    );
    ClassDB::bind_method(
        D_METHOD("table_write_column", "table", "column", "data"),
        &NetwMultiplayer::table_write_column
    );
    ClassDB::bind_method(
        D_METHOD("table_commit", "table"),
        &NetwMultiplayer::table_commit
    );
    ClassDB::bind_method(
        D_METHOD("persist_table_commit", "table", "schema", "data"),
        &NetwMultiplayer::persist_table_commit
    );
    ClassDB::bind_method(
        D_METHOD("table_read_routes", "table"),
        &NetwMultiplayer::table_read_routes
    );
    ClassDB::bind_method(
        D_METHOD("table_read_column", "table", "column"),
        &NetwMultiplayer::table_read_column
    );
    ClassDB::bind_method(
        D_METHOD("table_read_births", "table"),
        &NetwMultiplayer::table_read_births
    );
    ClassDB::bind_method(
        D_METHOD("table_read_deaths", "table"),
        &NetwMultiplayer::table_read_deaths
    );
    ClassDB::bind_method(
        D_METHOD("table_get_row", "table", "route"),
        &NetwMultiplayer::table_get_row
    );
    ClassDB::bind_method(
        D_METHOD("table_get_rows", "table", "routes"),
        &NetwMultiplayer::table_get_rows
    );
    ClassDB::bind_method(
        D_METHOD("table_get_tick", "table"),
        &NetwMultiplayer::table_get_tick
    );
    BIND_ENUM_CONSTANT(TABLE_PARAM_RELIABLE);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "state"), "", "session_get_state");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "role"), "", "session_get_role");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_online"), "", "is_online");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_host"), "", "is_host");
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_local_client"),
        "",
        "is_local_client"
    );
    ADD_SIGNAL(
        MethodInfo("endpoint_added", PropertyInfo(Variant::RID, "endpoint"))
    );
    ADD_SIGNAL(
        MethodInfo("endpoint_removed", PropertyInfo(Variant::RID, "endpoint"))
    );
    ADD_SIGNAL(
        MethodInfo("endpoint_updated", PropertyInfo(Variant::RID, "endpoint"))
    );
    ADD_SIGNAL(MethodInfo(
        SIG_CLOCK_BEFORE_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_CLOCK_ON_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_CLOCK_AFTER_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        "predict_owner_divergence",
        PropertyInfo(Variant::INT, "peer"),
        PropertyInfo(Variant::INT, "entry"),
        PropertyInfo(Variant::INT, "attribution")
    ));
    ADD_SIGNAL(MethodInfo(
        "lagcomp_action_gate_fallback",
        PropertyInfo(Variant::STRING_NAME, "key"),
        PropertyInfo(Variant::INT, "view_tick")
    ));
    ADD_SIGNAL(
        MethodInfo(SIG_SCENE_SPAWNED, PropertyInfo(Variant::OBJECT, "scene"))
    );
    ADD_SIGNAL(
        MethodInfo(SIG_SCENE_ACTIVATED, PropertyInfo(Variant::OBJECT, "scene"))
    );
    ADD_SIGNAL(
        MethodInfo(SIG_SCENE_DESPAWNED, PropertyInfo(Variant::OBJECT, "scene"))
    );
    ADD_SIGNAL(MethodInfo(
        SIG_SCENE_ENTITY_MOVED,
        PropertyInfo(Variant::OBJECT, "entity"),
        PropertyInfo(Variant::OBJECT, "from"),
        PropertyInfo(Variant::OBJECT, "to")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SCENE_STARTUP_SPAWNED));
    ADD_SIGNAL(MethodInfo(
        SIG_PARTICIPANT_VIEWPORT_CHANGED,
        PropertyInfo(Variant::OBJECT, "viewport")
    ));

    ADD_SIGNAL(MethodInfo(SIG_CLOCK_BEFORE_TICK_LOOP));
    ADD_SIGNAL(MethodInfo(SIG_CLOCK_AFTER_TICK_LOOP));
    ADD_SIGNAL(MethodInfo(SIG_CLOCK_SYNCHRONIZED));
    ADD_SIGNAL(MethodInfo(SIG_CLOCK_CONFIGURED));
    ADD_SIGNAL(MethodInfo(
        SIG_CLOCK_TICKRATE_MISMATCH,
        PropertyInfo(Variant::INT, "peer_id"),
        PropertyInfo(Variant::INT, "their_tickrate")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_CLOCK_PONG_RECEIVED,
        PropertyInfo(Variant::DICTIONARY, "data")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_CLOCK_STABILITY_CHANGED,
        PropertyInfo(Variant::BOOL, "is_stable")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SESSION_STATE_CHANGED,
        PropertyInfo(Variant::INT, "old_state"),
        PropertyInfo(Variant::INT, "new_state")
    ));
    ADD_SIGNAL(
        MethodInfo("embed_phase_changed", PropertyInfo(Variant::INT, "phase"))
    );
    ADD_SIGNAL(MethodInfo(
        "session_join_submitted",
        PropertyInfo(Variant::STRING_NAME, "username"),
        PropertyInfo(Variant::ARRAY, "args")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SESSION_ENTERED));
    ADD_SIGNAL(MethodInfo(
        SIG_SESSION_JOIN_FAILED,
        PropertyInfo(Variant::INT, "error"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SESSION_ENDED));
    ADD_SIGNAL(MethodInfo(SIG_SESSION_RECLAIMED));
    ADD_SIGNAL(MethodInfo(
        SIG_PEER_AUTHENTICATING,
        PropertyInfo(Variant::INT, "peer_id")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PEER_AUTHENTICATION_FAILED,
        PropertyInfo(Variant::INT, "peer_id")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SESSION_POLL_STARTED,
        PropertyInfo(Variant::FLOAT, "delta")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_TABLE_RECEIVED,
        PropertyInfo(Variant::RID, "table"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PEER_PACKET,
        PropertyInfo(Variant::INT, "id"),
        PropertyInfo(Variant::PACKED_BYTE_ARRAY, "packet")
    ));
    ADD_SIGNAL(
        MethodInfo(SIG_PEER_KICKED, PropertyInfo(Variant::STRING, "reason"))
    );
    ADD_SIGNAL(MethodInfo(
        SIG_SESSION_SERVER_DISCONNECTING,
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PEER_KICK_REQUESTED,
        PropertyInfo(Variant::INT, "requester_id"),
        PropertyInfo(Variant::INT, "target_id"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SESSION_DISCONNECT_REQUESTED,
        PropertyInfo(Variant::INT, "peer_id"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SESSION_TREE_PAUSED,
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SESSION_TREE_UNPAUSED));
    ADD_SIGNAL(MethodInfo(
        SIG_SERVICE_REGISTERED,
        PropertyInfo(
            Variant::OBJECT,
            "service",
            PROPERTY_HINT_NODE_TYPE,
            "Node"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SERVICE_UNREGISTERED,
        PropertyInfo(
            Variant::OBJECT,
            "service",
            PROPERTY_HINT_NODE_TYPE,
            "Node"
        )
    ));
    ClassDB::bind_method(
        D_METHOD("service_register", "service", "type"),
        &NetwMultiplayer::service_register,
        DEFVAL(static_cast<Object *>(nullptr))
    );
    ClassDB::bind_method(
        D_METHOD("service_unregister", "service", "type"),
        &NetwMultiplayer::service_unregister,
        DEFVAL(static_cast<Object *>(nullptr))
    );
    ClassDB::bind_method(
        D_METHOD("service_get", "type"),
        &NetwMultiplayer::service_held
    );
    ClassDB::bind_method(
        D_METHOD("service_get_named", "class_name"),
        &NetwMultiplayer::service_held_named
    );
    ClassDB::bind_method(
        D_METHOD("service_get_all", "base"),
        &NetwMultiplayer::service_get_all
    );
    ClassDB::bind_method(
        D_METHOD("service_clear"),
        &NetwMultiplayer::service_clear
    );
    ADD_SIGNAL(MethodInfo(
        SIG_ENTITY_LIVE,
        PropertyInfo(Variant::INT, "route"),
        PropertyInfo(Variant::OBJECT, "entity")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ENTITY_LINGERING,
        PropertyInfo(Variant::INT, "route"),
        PropertyInfo(Variant::OBJECT, "entity")
    ));
    ADD_SIGNAL(
        MethodInfo(SIG_ENTITY_DEAD, PropertyInfo(Variant::INT, "route"))
    );
    ADD_SIGNAL(MethodInfo(
        SIG_ENTITY_HIDDEN,
        PropertyInfo(Variant::INT, "route"),
        PropertyInfo(Variant::OBJECT, "entity")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCENE_LOCAL_PLAYER_CHANGED,
        PropertyInfo(Variant::OBJECT, "player")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PARTICIPANT_JOINED,
        PropertyInfo(Variant::OBJECT, "participant")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PARTICIPANT_LOCAL_JOINED,
        PropertyInfo(Variant::OBJECT, "participant")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCENE_LOCAL_CHANGED,
        PropertyInfo(Variant::OBJECT, "from"),
        PropertyInfo(Variant::OBJECT, "to")
    ));
    ADD_SIGNAL(
        MethodInfo(SIG_SCENE_LIVE, PropertyInfo(Variant::OBJECT, "scene"))
    );
    ClassDB::bind_method(
        D_METHOD("session_is_active"),
        &NetwMultiplayer::session_is_active
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("session_get_all"),
        &NetwMultiplayer::session_get_all
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("make", "inner", "implementation"),
        &NetwMultiplayer::make,
        DEFVAL(Ref<SceneMultiplayer>()),
        DEFVAL(Ref<Script>())
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("of", "node"),
        &NetwMultiplayer::session_of
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("core_of", "node"),
        &NetwMultiplayer::core_of
    );
    ClassDB::bind_method(
        D_METHOD("entity_replicate", "node", "owner"),
        &NetwMultiplayer::entity_replicate,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD(
            "spawn_register_constructor",
            "id",
            "function",
            "arg_types",
            "quantizers"
        ),
        &NetwMultiplayer::spawn_register_constructor,
        DEFVAL(Array()),
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("spawn_registered", "id", "args", "owner"),
        &NetwMultiplayer::spawn_registered,
        DEFVAL(Array()),
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("entity_adopt", "root"),
        &NetwMultiplayer::entity_adopt
    );
    ClassDB::bind_method(
        D_METHOD("participant_joined_all"),
        &NetwMultiplayer::participant_joined_all
    );
    ClassDB::bind_method(
        D_METHOD("participant_local"),
        &NetwMultiplayer::participant_local
    );
    ClassDB::bind_method(
        D_METHOD("participant_all"),
        &NetwMultiplayer::participant_all
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "participants",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwParticipant"
        ),
        "",
        "participant_joined_all"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "connected_participants",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwParticipant"
        ),
        "",
        "participant_all"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "local_participant",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwParticipant"
        ),
        "",
        "participant_local"
    );
    ClassDB::bind_method(
        D_METHOD("participant_seat", "peer"),
        &NetwMultiplayer::participant_seat
    );
    ClassDB::bind_method(
        D_METHOD("scene_player_local"),
        &NetwMultiplayer::scene_player_local
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "local_player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwEntity"
        ),
        "",
        "scene_player_local"
    );
    BIND_ENUM_CONSTANT(SCENE_MOVE_REFUSED);
    BIND_ENUM_CONSTANT(SCENE_MOVE_ALREADY_THERE);
    BIND_ENUM_CONSTANT(RECORD_KIND_STATE);
    BIND_ENUM_CONSTANT(RECORD_KIND_INPUT);
    BIND_ENUM_CONSTANT(RECORD_KIND_BROADCAST);
    BIND_ENUM_CONSTANT(ENTITY_STATE_UNKNOWN);
    BIND_ENUM_CONSTANT(ENTITY_STATE_LIVE);
    BIND_ENUM_CONSTANT(ENTITY_STATE_LINGERING);
    BIND_ENUM_CONSTANT(ENTITY_STATE_DEAD);
    BIND_ENUM_CONSTANT(ENTITY_STATE_ABSENT);
    BIND_ENUM_CONSTANT(LAYER_POLICY_HIDE_FROM_OUTSIDERS);
    BIND_ENUM_CONSTANT(LAYER_POLICY_HIDE_FROM_INSIDERS);
    BIND_ENUM_CONSTANT(EMBED_PHASE_DECLARING);
    BIND_ENUM_CONSTANT(EMBED_PHASE_SETTLING);
    BIND_ENUM_CONSTANT(EMBED_PHASE_LIVE);
    BIND_ENUM_CONSTANT(LEAVE_POLICY_HIDE);
    BIND_ENUM_CONSTANT(LEAVE_POLICY_RETAIN);
    BIND_ENUM_CONSTANT(LEAVE_POLICY_CUSTOM);
    BIND_ENUM_CONSTANT(PERCEPTION_POLICY_HIDE);
    BIND_ENUM_CONSTANT(PERCEPTION_POLICY_SHOW);
    BIND_ENUM_CONSTANT(PERCEPTION_POLICY_CUSTOM);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_AUTO);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_REMOTE);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_PREDICTED);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_DISABLED);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_AUTHORITY);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_UNRESOLVED);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_DISABLED);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_REMOTE);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_BRACKETED);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_CHASE);
    BIND_ENUM_CONSTANT(PREDICTED_MODE_CHASE);
    BIND_ENUM_CONSTANT(PREDICTED_MODE_BRACKETED);
    BIND_ENUM_CONSTANT(TIMELINE_MODE_BUFFERED);
    BIND_ENUM_CONSTANT(TIMELINE_MODE_FORECAST);
    BIND_ENUM_CONSTANT(SYNC_MODE_SNAP);
    BIND_ENUM_CONSTANT(SYNC_MODE_STRETCH);
    BIND_ENUM_CONSTANT(MISMATCH_ACTION_WARN);
    BIND_ENUM_CONSTANT(MISMATCH_ACTION_DISCONNECT);
    BIND_ENUM_CONSTANT(MISMATCH_ACTION_SIGNAL);
    BIND_ENUM_CONSTANT(LAYER_PARAM_POLICY);
    BIND_ENUM_CONSTANT(LAYER_PARAM_LEAVE_POLICY);
    BIND_ENUM_CONSTANT(LAYER_PARAM_PERCEPTION_POLICY);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_PEER_CLASS);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_DISPLAY_NAME);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_ADDRESS_LABEL);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_ADDRESS_PLACEHOLDER);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_ADDRESS_HELP);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_CAPABILITIES);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_HOST_SETTINGS);
    BIND_ENUM_CONSTANT(TRANSPORT_PARAM_CLIENT_SETTINGS);
    BIND_ENUM_CONSTANT(TRANSPORT_AVAILABLE);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_HOST);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_PROBE);
    BIND_ENUM_CONSTANT(TRANSPORT_CAN_BROWSE);
    BIND_ENUM_CONSTANT(TRANSPORT_ACCEPTS_EMPTY_ADDRESS);

    BIND_ENUM_CONSTANT(ENDPOINT_PARAM_TRANSPORT);
    BIND_ENUM_CONSTANT(ENDPOINT_PARAM_ADDRESS);
    BIND_ENUM_CONSTANT(ENDPOINT_PARAM_DISPLAY_NAME);

    BIND_ENUM_CONSTANT(ENDPOINT_STATE_FLAGS);
    BIND_ENUM_CONSTANT(ENDPOINT_STATE_STATUS);
    BIND_ENUM_CONSTANT(ENDPOINT_STATE_INFO);

    BIND_ENUM_CONSTANT(ENDPOINT_FLAG_CALLER);
    BIND_ENUM_CONSTANT(ENDPOINT_FLAG_AVAILABLE);
    BIND_ENUM_CONSTANT(ENDPOINT_FLAG_OBSERVED);
    BIND_ENUM_CONSTANT(SCENE_PARAM_LABEL);
    BIND_ENUM_CONSTANT(SCENE_PARAM_ISOLATION);
    BIND_ENUM_CONSTANT(SCENE_PARAM_PROCESSING);
    BIND_ENUM_CONSTANT(SCENE_EVENT_PARTICIPANT);
    BIND_ENUM_CONSTANT(SCENE_EVENT_PLAYER);
    BIND_ENUM_CONSTANT(SCENE_EVENT_ENTITY);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_SESSION);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_PARTICIPANT);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_SCENE);
    BIND_ENUM_CONSTANT(SCENE_ISOLATION_NONE);
    BIND_ENUM_CONSTANT(SCENE_ISOLATION_OWN_WORLD);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_TICKRATE);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_SYNC_MODE);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_PING_INTERVAL);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_MAX_TICKS_PER_FRAME);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_STALL_THRESHOLD);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_PANIC_SNAP_THRESHOLD);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_STRETCH_NUDGE_FACTOR);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_DISPLAY_OFFSET);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_LEAD_TICKS);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_JITTER_MULTIPLIER);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_JITTER_WINDOW);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_JITTER_STABILITY_THRESHOLD);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_USE_PHYSICS_INTERPOLATION);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_ENABLE_DRIFT_LOGGING);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_MANUAL_TICK);
    BIND_ENUM_CONSTANT(CLOCK_PARAM_TICK_FACTOR_OVERRIDE);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_RTT);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_RTT_AVG);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_RTT_JITTER);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_ONE_WAY_LATENCY);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_TICKTIME);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_TICK_FACTOR);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_TICK_PHASE);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_TICK_ACCUMULATOR);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_PHYSICS_FACTOR);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_RECOMMENDED_DISPLAY_OFFSET);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_PHYSICS_FRAMES);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_POLLS);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_WALL_SECONDS);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_PHYSICS_HZ);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_POLL_HZ);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_ROLE);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_PREDICTED_MODE);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_PREDICTED_SMOOTH_TIME);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_CHASE_GLIDE_TIME);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_TIMELINE_MODE);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_MAX_FORECAST_TICKS);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_SMART_DILATION);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_MAX_EXTRA_DILATION);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_LAG_ADAPT_RATE);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_STARVATION_GROWTH);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_FLOOR_SMOOTHING);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_STARVATION_GRACE_FRAMES);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_TRACE_INTERVAL);
    BIND_ENUM_CONSTANT(DISPLAY_PARAM_VISUAL_ROOT);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_ARCHETYPE);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_SCHEDULE);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_MISSING_POLICY);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_RECOVERY_POLICY);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_SNAP_RESTORE);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_CORRECTION_MODE);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_TELEPORT_THRESHOLD);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_DIVERGENCE_EPSILON);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_BREACH_RESPONSE);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_MAX_RESTORE_TICKS);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_COLLISION_COOLDOWN_TICKS);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_MAX_CONSUME_PER_TICK);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_MAX_CONSUME_LAG_TICKS);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_CONSUME_BUFFER_TICKS);
    BIND_ENUM_CONSTANT(PREDICT_PARAM_REPLAY_BUFFER_DEPTH);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_APPROXIMATE);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_EXACT_CLAIM);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_RECONCILE);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_PROMOTION);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_PROMOTION_COUNT);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_PROMOTION_METERS);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_PACING);
    BIND_ENUM_CONSTANT(ISLAND_PARAM_INPUT_DELAY);
    BIND_ENUM_CONSTANT(MEMBER_PARAM_FIDELITY);
    BIND_ENUM_CONSTANT(MEMBER_PARAM_PREDICTOR);
    BIND_ENUM_CONSTANT(COLUMN_F32);
    BIND_ENUM_CONSTANT(COLUMN_F64);
    BIND_ENUM_CONSTANT(COLUMN_I8);
    BIND_ENUM_CONSTANT(COLUMN_U8);
    BIND_ENUM_CONSTANT(COLUMN_I16);
    BIND_ENUM_CONSTANT(COLUMN_U16);
    BIND_ENUM_CONSTANT(COLUMN_I32);
    BIND_ENUM_CONSTANT(COLUMN_I64);
    BIND_ENUM_CONSTANT(COLUMN_BOOL);
    BIND_ENUM_CONSTANT(COLUMN_VECTOR2);
    BIND_ENUM_CONSTANT(COLUMN_VECTOR3);
    BIND_ENUM_CONSTANT(COLUMN_VECTOR4);
    BIND_ENUM_CONSTANT(COLUMN_COLOR);
    BIND_ENUM_CONSTANT(COLUMN_QUATERNION);
    BIND_ENUM_CONSTANT(COLUMN_ENTITY);
    BIND_ENUM_CONSTANT(COLUMN_VARIANT);
#define NETW_SESSION_STAT_BIND(m_name, m_key) BIND_ENUM_CONSTANT(STAT_##m_name);
    NETW_SESSION_STAT_TABLE(NETW_SESSION_STAT_BIND)
#undef NETW_SESSION_STAT_BIND
    ClassDB::bind_method(
        D_METHOD("scene_activate", "destination"),
        &NetwMultiplayer::scene_activate
    );
    ClassDB::bind_method(
        D_METHOD("scene_change_to_file", "requester", "path", "scope"),
        &NetwMultiplayer::scene_change_to_file,
        DEFVAL(SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_method(
        D_METHOD("scene_change_to_packed", "requester", "packed", "scope"),
        &NetwMultiplayer::scene_change_to_packed,
        DEFVAL(SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_method(
        D_METHOD("scene_reload_current", "requester", "scope"),
        &NetwMultiplayer::scene_reload_current,
        DEFVAL(SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_method(
        D_METHOD("scene_set_carry_move", "carry"),
        &NetwMultiplayer::scene_set_carry_move
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("property_path", "source", "property", "base"),
        &NetwMultiplayer::property_path
    );
    ClassDB::bind_method(
        D_METHOD("entity_get_view", "entity"),
        &NetwMultiplayer::entity_get_view
    );
    ClassDB::bind_method(
        D_METHOD("liveness_reserve_route"),
        &NetwMultiplayer::liveness_reserve_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_allocate_route", "wrapper"),
        &NetwMultiplayer::liveness_allocate_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_bind_route", "route", "wrapper"),
        &NetwMultiplayer::liveness_bind_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_route_of", "wrapper"),
        &NetwMultiplayer::liveness_route_of
    );
    ClassDB::bind_method(
        D_METHOD("liveness_route_state", "route"),
        &NetwMultiplayer::liveness_route_state
    );
    ClassDB::bind_method(
        D_METHOD(
            "sync_admit_frame_default",
            "sender",
            "route",
            "comp",
            "channel",
            "flags",
            "tick",
            "payload"
        ),
        &NetwMultiplayer::sync_admit_frame_default
    );
    ClassDB::bind_method(
        D_METHOD(
            "predict_admit_frame_default",
            "sender",
            "route",
            "channel",
            "payload"
        ),
        &NetwMultiplayer::predict_admit_frame_default
    );
    ClassDB::bind_method(
        D_METHOD("entity_describe", "route"),
        &NetwMultiplayer::entity_describe
    );
    ClassDB::bind_method(
        D_METHOD("entity_of", "node"),
        &NetwMultiplayer::entity_of
    );
    ClassDB::bind_method(
        D_METHOD("scene_admit", "scene", "peer"),
        &NetwMultiplayer::scene_admit
    );
    ClassDB::bind_method(
        D_METHOD("scene_release", "scene", "peer"),
        &NetwMultiplayer::scene_release
    );
    ClassDB::bind_method(
        D_METHOD("scene_admits", "scene", "peer"),
        &NetwMultiplayer::scene_admits
    );
    ClassDB::bind_method(
        D_METHOD("scene_is_declared", "entity"),
        &NetwMultiplayer::scene_is_declared
    );
    ClassDB::bind_method(
        D_METHOD("display_write_default", "entity", "track", "value"),
        &NetwMultiplayer::display_write_default
    );
    GDVIRTUAL_BIND(_display_write, "entity", "track", "value");
    ClassDB::bind_method(
        D_METHOD("liveness_node_of", "route"),
        &NetwMultiplayer::liveness_node_of
    );
    ClassDB::bind_method(
        D_METHOD("liveness_get_entities"),
        &NetwMultiplayer::liveness_get_entities
    );
    ClassDB::bind_method(
        D_METHOD("liveness_pending_live_count"),
        &NetwMultiplayer::liveness_pending_live_count
    );
    ClassDB::bind_method(
        D_METHOD("liveness_poll_now"),
        &NetwMultiplayer::liveness_poll_now
    );
    ClassDB::bind_method(
        D_METHOD("session_set_join_resolver", "resolver"),
        &NetwMultiplayer::session_set_join_resolver
    );
    ClassDB::bind_method(
        D_METHOD("session_prepare_join", "username", "args"),
        &NetwMultiplayer::session_prepare_join,
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("session_leave"),
        &NetwMultiplayer::session_leave
    );
    ClassDB::bind_method(
        D_METHOD("embed_phase"),
        &NetwMultiplayer::embed_phase
    );
    ClassDB::bind_method(
        D_METHOD("embed_offer_bare_level", "level"),
        &NetwMultiplayer::embed_offer_bare_level
    );
    ClassDB::bind_method(
        D_METHOD("embed_settle"),
        &NetwMultiplayer::embed_settle
    );
    ClassDB::bind_method(
        D_METHOD("embed_poll_transport"),
        &NetwMultiplayer::embed_poll_transport
    );
    ClassDB::bind_method(
        D_METHOD("embed_is_disposing"),
        &NetwMultiplayer::embed_is_disposing
    );
    ClassDB::bind_method(
        D_METHOD("embed_adopt_inner", "inner"),
        &NetwMultiplayer::embed_adopt_inner
    );
    ClassDB::bind_method(
        D_METHOD("embed_dispose"),
        &NetwMultiplayer::embed_dispose
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_create", "name"),
        &NetwMultiplayer::interest_layer_create
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_free", "layer"),
        &NetwMultiplayer::interest_layer_free
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_add_viewer", "layer", "peer"),
        &NetwMultiplayer::interest_layer_add_viewer
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_remove_viewer", "layer", "peer"),
        &NetwMultiplayer::interest_layer_remove_viewer
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_add_entity", "layer", "entity"),
        &NetwMultiplayer::interest_layer_add_entity
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_set_param", "layer", "param", "value"),
        &NetwMultiplayer::interest_layer_set_param
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_set_driver_callback", "layer", "callback"),
        &NetwMultiplayer::interest_layer_set_driver_callback
    );
    ClassDB::bind_method(
        D_METHOD("predict_consume", "depth", "buffer"),
        &NetwMultiplayer::predict_consume
    );
    ClassDB::bind_method(
        D_METHOD(
            "predict_drive_default",
            "latest_input_tick",
            "last_driven_input_tick",
            "frame_tick"
        ),
        &NetwMultiplayer::predict_drive_default
    );
    ClassDB::bind_method(
        D_METHOD("predict_consume_default", "depth", "buffer"),
        &NetwMultiplayer::predict_consume_default
    );
    ClassDB::bind_method(
        D_METHOD(
            "predict_evaluate_default",
            "domain",
            "verdict",
            "predicted",
            "payload",
            "wiring",
            "field_sink"
        ),
        &NetwMultiplayer::predict_evaluate_default
    );
    ClassDB::bind_method(
        D_METHOD(
            "predict_recover_default",
            "payload",
            "policy",
            "correction",
            "snap_restore",
            "projection",
            "current",
            "pose_errors",
            "wiring",
            "verdict",
            "tick_delta"
        ),
        &NetwMultiplayer::predict_recover_default
    );
    GDVIRTUAL_BIND(
        _predict_drive,
        "latest_input_tick",
        "last_driven_input_tick",
        "frame_tick"
    );
    GDVIRTUAL_BIND(_predict_consume, "depth", "buffer");
    GDVIRTUAL_BIND(
        _predict_evaluate,
        "domain",
        "verdict",
        "predicted",
        "payload",
        "wiring",
        "field_sink"
    );
    GDVIRTUAL_BIND(
        _predict_recover,
        "payload",
        "policy",
        "correction",
        "snap_restore",
        "projection",
        "current",
        "pose_errors",
        "wiring",
        "verdict",
        "tick_delta"
    );
    GDVIRTUAL_BIND(_sync_encode, "peer", "tick");
    GDVIRTUAL_BIND(_sync_decode, "entity", "comp", "flags", "tick", "payload");
    GDVIRTUAL_BIND(_spawn_admit_frame, "sender", "route", "channel", "payload");
    GDVIRTUAL_BIND(_table_admit_frame, "sender", "channel", "payload");
    GDVIRTUAL_BIND(_persist_tick, "delta");
    GDVIRTUAL_BIND(_sync_note_ack, "peer", "sequence");
    GDVIRTUAL_BIND(_sync_note_sent, "peer", "sequence");
    GDVIRTUAL_BIND(_spawn_declare, "entity", "recipe");
    GDVIRTUAL_BIND(_spawn_undeclare, "entity");
    GDVIRTUAL_BIND(_spawn_construct, "entity");
    GDVIRTUAL_BIND(
        _sync_admit_frame,
        "sender",
        "route",
        "comp",
        "channel",
        "flags",
        "tick",
        "payload"
    );
    GDVIRTUAL_BIND(
        _predict_admit_frame,
        "sender",
        "route",
        "channel",
        "payload"
    );
    ClassDB::bind_method(
        D_METHOD("interest_flush_now"),
        &NetwMultiplayer::interest_flush_now
    );
    ClassDB::bind_method(
        D_METHOD("interest_join", "entity", "layer_id"),
        &NetwMultiplayer::interest_join
    );
    ClassDB::bind_method(
        D_METHOD("interest_leave", "entity", "layer_id"),
        &NetwMultiplayer::interest_leave
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_ids", "entity"),
        &NetwMultiplayer::interest_layer_ids
    );
    ClassDB::bind_method(
        D_METHOD("interest_on_enter", "entity", "layer_id", "callback"),
        &NetwMultiplayer::interest_on_enter
    );
    ClassDB::bind_method(
        D_METHOD("interest_on_leave", "entity", "layer_id", "callback"),
        &NetwMultiplayer::interest_on_leave
    );
    ClassDB::bind_method(
        D_METHOD(
            "interest_on_leave_policy",
            "entity",
            "layer_id",
            "policy",
            "custom_callback"
        ),
        &NetwMultiplayer::interest_on_leave_policy,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD(
            "interest_on_perception_policy",
            "entity",
            "layer_id",
            "policy",
            "custom_callback"
        ),
        &NetwMultiplayer::interest_on_perception_policy,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD("predict_stepper_install", "space", "stepper"),
        &NetwMultiplayer::predict_stepper_install,
        DEFVAL(Ref<RefCounted>())
    );
    ClassDB::bind_method(
        D_METHOD("interest_admits", "entity", "peer"),
        &NetwMultiplayer::interest_admits
    );
    ClassDB::bind_method(
        D_METHOD("interest_get_row", "entity"),
        &NetwMultiplayer::interest_get_row
    );
    ClassDB::bind_method(
        D_METHOD("interest_explain", "entity", "peer"),
        &NetwMultiplayer::interest_explain
    );
    ClassDB::bind_method(
        D_METHOD("interest_get_membership", "entity"),
        &NetwMultiplayer::interest_get_membership
    );
    ClassDB::bind_method(
        D_METHOD("get_root_path"),
        &NetwMultiplayer::get_root_path
    );
    ClassDB::bind_method(
        D_METHOD("set_root_path", "path"),
        &NetwMultiplayer::set_root_path
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::NODE_PATH, "root_path"),
        "set_root_path",
        "get_root_path"
    );
    ClassDB::bind_method(
        D_METHOD("is_object_decoding_allowed"),
        &NetwMultiplayer::is_object_decoding_allowed
    );
    ClassDB::bind_method(
        D_METHOD("set_allow_object_decoding", "enable"),
        &NetwMultiplayer::set_allow_object_decoding
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "allow_object_decoding"),
        "set_allow_object_decoding",
        "is_object_decoding_allowed"
    );
    ClassDB::bind_method(
        D_METHOD("get_auth_timeout"),
        &NetwMultiplayer::get_auth_timeout
    );
    ClassDB::bind_method(
        D_METHOD("set_auth_timeout", "timeout"),
        &NetwMultiplayer::set_auth_timeout
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "auth_timeout"),
        "set_auth_timeout",
        "get_auth_timeout"
    );
    ClassDB::bind_method(
        D_METHOD("is_refusing_new_connections"),
        &NetwMultiplayer::is_refusing_new_connections
    );
    ClassDB::bind_method(
        D_METHOD("set_refuse_new_connections", "refuse"),
        &NetwMultiplayer::set_refuse_new_connections
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "refuse_new_connections"),
        "set_refuse_new_connections",
        "is_refusing_new_connections"
    );
    ClassDB::bind_method(
        D_METHOD("is_server_relay_enabled"),
        &NetwMultiplayer::is_server_relay_enabled
    );
    ClassDB::bind_method(
        D_METHOD("set_server_relay_enabled", "enabled"),
        &NetwMultiplayer::set_server_relay_enabled
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "server_relay"),
        "set_server_relay_enabled",
        "is_server_relay_enabled"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_sync_packet_size"),
        &NetwMultiplayer::get_max_sync_packet_size
    );
    ClassDB::bind_method(
        D_METHOD("set_max_sync_packet_size", "size"),
        &NetwMultiplayer::set_max_sync_packet_size
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_sync_packet_size"),
        "set_max_sync_packet_size",
        "get_max_sync_packet_size"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_delta_packet_size"),
        &NetwMultiplayer::get_max_delta_packet_size
    );
    ClassDB::bind_method(
        D_METHOD("set_max_delta_packet_size", "size"),
        &NetwMultiplayer::set_max_delta_packet_size
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_delta_packet_size"),
        "set_max_delta_packet_size",
        "get_max_delta_packet_size"
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_is_configured"),
        &NetwMultiplayer::lagcomp_is_configured
    );
    ClassDB::bind_method(
        D_METHOD(
            "rpc_channel_register",
            "channel",
            "handler",
            "defer_when_unknown"
        ),
        &NetwMultiplayer::rpc_channel_register,
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("entity_bind_node", "entity", "node"),
        &NetwMultiplayer::entity_bind_node
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_rewind", "entities", "tick", "body"),
        &NetwMultiplayer::lagcomp_rewind
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_sample", "entity", "tick"),
        &NetwMultiplayer::lagcomp_sample
    );
    ClassDB::bind_method(
        D_METHOD("entity_create"),
        &NetwMultiplayer::entity_create
    );
    ClassDB::bind_method(
        D_METHOD("scene_move", "entity", "destination", "opts"),
        &NetwMultiplayer::scene_move,
        DEFVAL(Ref<NetwReparentOpts>())
    );
    ClassDB::bind_method(
        D_METHOD(
            "liveness_when_live",
            "route",
            "callback",
            "timeout_ticks",
            "on_timeout"
        ),
        &NetwMultiplayer::liveness_when_live,
        DEFVAL(0),
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD("liveness_claim_routes", "count"),
        &NetwMultiplayer::liveness_claim_routes
    );
    ClassDB::bind_method(
        D_METHOD("liveness_release_routes", "routes"),
        &NetwMultiplayer::liveness_release_routes
    );
    ClassDB::bind_method(
        D_METHOD("entity_despawn", "entity", "opts"),
        &NetwMultiplayer::entity_despawn,
        DEFVAL(Ref<NetwDespawnOpts>())
    );
    ClassDB::bind_method(
        D_METHOD("send_bytes", "bytes", "id", "mode", "channel"),
        &NetwMultiplayer::send_bytes,
        DEFVAL(0),
        DEFVAL(int(MultiplayerPeer::TRANSFER_MODE_RELIABLE)),
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("clear_roster"),
        &NetwMultiplayer::clear_roster
    );
    ClassDB::bind_method(
        D_METHOD("predict_declare", "entity"),
        &NetwMultiplayer::predict_declare
    );
    ClassDB::bind_method(
        D_METHOD("predict_undeclare", "entity"),
        &NetwMultiplayer::predict_undeclare
    );
    ClassDB::bind_method(
        D_METHOD("predict_set_param", "entity", "param", "value"),
        &NetwMultiplayer::predict_set_param
    );
    ClassDB::bind_method(
        D_METHOD("predict_get_param", "entity", "param"),
        &NetwMultiplayer::predict_get_param
    );
    ClassDB::bind_method(
        D_METHOD("predict_set_sensor_callback", "entity", "name", "callback"),
        &NetwMultiplayer::predict_set_sensor_callback
    );
    ClassDB::bind_method(
        D_METHOD("predict_set_witness_callback", "entity", "callback"),
        &NetwMultiplayer::predict_set_witness_callback
    );
    ClassDB::bind_method(
        D_METHOD("predict_set_corridor_callback", "entity", "callback"),
        &NetwMultiplayer::predict_set_corridor_callback
    );
    ClassDB::bind_method(
        D_METHOD("predict_set_simulate_callback", "entity", "callback"),
        &NetwMultiplayer::predict_set_simulate_callback
    );
    ClassDB::bind_method(
        D_METHOD("predict_island_add", "entity", "other"),
        &NetwMultiplayer::predict_island_add
    );
    ClassDB::bind_method(
        D_METHOD("predict_island_set_param", "entity", "param", "value"),
        &NetwMultiplayer::predict_island_set_param
    );
    ClassDB::bind_method(
        D_METHOD(
            "predict_island_set_member_param",
            "entity",
            "member",
            "param",
            "value"
        ),
        &NetwMultiplayer::predict_island_set_member_param
    );
    ClassDB::bind_method(
        D_METHOD("peer_get_accepted_join", "peer"),
        &NetwMultiplayer::peer_get_accepted_join
    );
    ClassDB::bind_method(
        D_METHOD("peer_forget", "peer"),
        &NetwMultiplayer::peer_forget
    );
    ClassDB::bind_method(
        D_METHOD("peer_get_participant", "peer"),
        &NetwMultiplayer::peer_get_participant
    );
    ClassDB::bind_method(
        D_METHOD("display_declare", "entity", "comp", "track", "spec"),
        &NetwMultiplayer::display_declare
    );
    ClassDB::bind_method(
        D_METHOD("predict_get_stepper", "space"),
        &NetwMultiplayer::predict_get_stepper
    );
    ClassDB::bind_method(
        D_METHOD("predict_engine_seated", "entity"),
        &NetwMultiplayer::predict_engine_seated
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_deny_action", "requester", "key"),
        &NetwMultiplayer::lagcomp_deny_action
    );
    ClassDB::bind_method(
        D_METHOD("property_set_record", "set"),
        &NetwMultiplayer::property_set_record
    );
    ClassDB::bind_method(
        D_METHOD("clock_get_config"),
        &NetwMultiplayer::clock_get_config
    );
    ClassDB::bind_method(
        D_METHOD("clock_get_param", "param"),
        &NetwMultiplayer::clock_get_param
    );
    ClassDB::bind_method(
        D_METHOD("clock_set_param", "param", "value"),
        &NetwMultiplayer::clock_set_param
    );
    ClassDB::bind_method(
        D_METHOD("clock_get_monitor", "monitor"),
        &NetwMultiplayer::clock_get_monitor
    );
    ClassDB::bind_method(
        D_METHOD("clock_get_tick"),
        &NetwMultiplayer::clock_get_tick
    );
    ClassDB::bind_method(
        D_METHOD("clock_get_display_tick"),
        &NetwMultiplayer::clock_get_display_tick
    );
    ClassDB::bind_method(
        D_METHOD("clock_get_physics_steps_per_tick"),
        &NetwMultiplayer::clock_get_physics_steps_per_tick
    );
    ClassDB::bind_method(
        D_METHOD("clock_get_simulation_behind_count"),
        &NetwMultiplayer::clock_get_simulation_behind_count
    );
    ClassDB::bind_method(
        D_METHOD("clock_is_configured"),
        &NetwMultiplayer::clock_is_configured
    );
    ClassDB::bind_method(
        D_METHOD("clock_is_synchronized"),
        &NetwMultiplayer::clock_is_synchronized
    );
    ClassDB::bind_method(
        D_METHOD("clock_is_simulating"),
        &NetwMultiplayer::clock_is_simulating
    );
    ClassDB::bind_method(
        D_METHOD("clock_is_stable"),
        &NetwMultiplayer::clock_is_stable
    );
    ClassDB::bind_method(
        D_METHOD("clock_is_gated"),
        &NetwMultiplayer::clock_is_gated
    );
    ClassDB::bind_method(
        D_METHOD("clock_step", "count"),
        &NetwMultiplayer::clock_step
    );
    ClassDB::bind_method(
        D_METHOD("clock_physics_step", "delta"),
        &NetwMultiplayer::clock_physics_step
    );
    ClassDB::bind_method(
        D_METHOD("clock_tick_loop", "open"),
        &NetwMultiplayer::clock_tick_loop
    );
    ClassDB::bind_method(
        D_METHOD("clock_set_gate", "armed"),
        &NetwMultiplayer::clock_set_gate
    );
    ClassDB::bind_method(
        D_METHOD("clock_set_synchronized", "value"),
        &NetwMultiplayer::clock_set_synchronized
    );
    ClassDB::bind_method(
        D_METHOD(
            "clock_ingest_pong",
            "sample",
            "server_tick_at_pong",
            "server_tick_phase",
            "apply_lead"
        ),
        &NetwMultiplayer::clock_ingest_pong
    );
    ClassDB::bind_method(
        D_METHOD("display_set_param", "entity", "param", "value"),
        &NetwMultiplayer::display_set_param
    );
    ClassDB::bind_method(
        D_METHOD("display_set_target_item", "entity", "item"),
        &NetwMultiplayer::display_set_target_item
    );
    ClassDB::bind_method(
        D_METHOD("display_set_callback", "entity", "callback"),
        &NetwMultiplayer::display_set_callback
    );
    ClassDB::bind_method(
        D_METHOD("display_get_param", "entity", "param"),
        &NetwMultiplayer::display_get_param
    );
    ClassDB::bind_method(
        D_METHOD("display_reset", "entity"),
        &NetwMultiplayer::display_reset
    );
    ClassDB::bind_method(
        D_METHOD("display_snap", "entity", "track", "value"),
        &NetwMultiplayer::display_snap
    );
    ClassDB::bind_method(
        D_METHOD("display_get_value", "entity", "track"),
        &NetwMultiplayer::display_get_value
    );
    ClassDB::bind_method(
        D_METHOD("display_get_track_stat", "entity", "track", "stat"),
        &NetwMultiplayer::display_get_track_stat
    );
    ClassDB::bind_method(
        D_METHOD("scene_declare", "entity"),
        &NetwMultiplayer::scene_declare
    );
    ClassDB::bind_method(
        D_METHOD("scene_undeclare", "entity"),
        &NetwMultiplayer::scene_undeclare
    );
    ClassDB::bind_method(
        D_METHOD("scene_set_param", "scene", "param", "value"),
        &NetwMultiplayer::scene_set_param
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_param", "scene", "param"),
        &NetwMultiplayer::scene_get_param
    );
    ClassDB::bind_method(
        D_METHOD("scene_find", "stem"),
        &NetwMultiplayer::scene_find
    );
    ClassDB::bind_method(
        D_METHOD("scene_find_all", "stem"),
        &NetwMultiplayer::scene_find_all
    );
    ClassDB::bind_method(D_METHOD("scene_list"), &NetwMultiplayer::scene_list);
    ClassDB::bind_method(
        D_METHOD("scene_get_node", "scene"),
        &NetwMultiplayer::scene_get_node
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_label", "scene"),
        &NetwMultiplayer::scene_get_label
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_entities", "scene"),
        &NetwMultiplayer::scene_get_entities
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_players", "scene"),
        &NetwMultiplayer::scene_get_players
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_participants", "scene"),
        &NetwMultiplayer::scene_get_participants
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_local_player", "scene"),
        &NetwMultiplayer::scene_get_local_player
    );
    ClassDB::bind_method(
        D_METHOD("scene_add_player", "scene", "player"),
        &NetwMultiplayer::scene_add_player
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_layer", "scene"),
        &NetwMultiplayer::scene_get_layer
    );
    ClassDB::bind_method(
        D_METHOD("scene_get_current"),
        &NetwMultiplayer::scene_get_current
    );
    ClassDB::bind_method(
        D_METHOD("scene_destroy", "scene"),
        &NetwMultiplayer::scene_destroy
    );
    ClassDB::bind_method(
        D_METHOD("scene_players_all"),
        &NetwMultiplayer::scene_players_all
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "players",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        "",
        "scene_players_all"
    );
    ClassDB::bind_method(
        D_METHOD("scene_create", "recipe", "isolation"),
        &NetwMultiplayer::scene_create,
        DEFVAL(SCENE_ISOLATION_NONE)
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("scene_packed_stem", "packed"),
        &NetwMultiplayer::scene_packed_stem
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("scene_set_host_view_factory", "factory"),
        &NetwMultiplayer::scene_set_host_view_factory
    );
#if defined(NETW_TESTS)
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("law_extension_named"),
        &NetwMultiplayer::law_extension_named
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("law_extension_script"),
        &NetwMultiplayer::law_extension_script
    );
#endif
    ClassDB::bind_method(
        D_METHOD("entity_add_property_set", "entity", "set", "comp"),
        &NetwMultiplayer::entity_add_property_set
    );
    ClassDB::bind_method(
        D_METHOD("property_set_create", "schema", "record"),
        &NetwMultiplayer::property_set_create
    );
    ClassDB::bind_method(
        D_METHOD("property_set_add_column", "set", "column"),
        &NetwMultiplayer::property_set_add_column
    );
    ClassDB::bind_method(
        D_METHOD("property_set_seal", "set"),
        &NetwMultiplayer::property_set_seal
    );
    ClassDB::bind_method(
        D_METHOD("property_set_get_wire_hash", "set"),
        &NetwMultiplayer::property_set_get_wire_hash
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_effect_key", "entity", "tick", "slot"),
        &NetwMultiplayer::lagcomp_effect_key,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_timeline_declare", "entity"),
        &NetwMultiplayer::lagcomp_timeline_declare
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_timeline_undeclare", "entity"),
        &NetwMultiplayer::lagcomp_timeline_undeclare
    );
    ClassDB::bind_method(
        D_METHOD("lagcomp_timeline_of", "entity"),
        &NetwMultiplayer::lagcomp_timeline_of
    );
    ClassDB::bind_method(
        D_METHOD("entity_admit", "entity"),
        &NetwMultiplayer::entity_admit
    );
    ClassDB::bind_method(
        D_METHOD("entity_bind_route", "entity", "route"),
        &NetwMultiplayer::entity_bind_route
    );
    ClassDB::bind_method(
        D_METHOD("entity_get_route", "entity"),
        &NetwMultiplayer::entity_get_route
    );
    ClassDB::bind_method(
        D_METHOD("entity_get_node", "entity"),
        &NetwMultiplayer::entity_get_node
    );
    ClassDB::bind_method(
        D_METHOD("entity_get_parent", "entity"),
        &NetwMultiplayer::entity_get_parent
    );
    ClassDB::bind_method(
        D_METHOD("entity_get_peer", "entity"),
        &NetwMultiplayer::entity_get_peer
    );
    ClassDB::bind_method(
        D_METHOD("entity_call", "entity", "comp", "method", "args", "peer"),
        &NetwMultiplayer::entity_call
    );
    ClassDB::bind_method(
        D_METHOD("entity_grant_control", "entity", "peer"),
        &NetwMultiplayer::entity_grant_control
    );
    ClassDB::bind_method(
        D_METHOD("entity_get_state", "entity"),
        &NetwMultiplayer::entity_get_state
    );
    ClassDB::bind_method(
        D_METHOD("entity_get_epoch", "entity"),
        &NetwMultiplayer::entity_get_epoch
    );
    ClassDB::bind_method(
        D_METHOD("entity_from_route", "route"),
        &NetwMultiplayer::entity_from_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_get_routes"),
        &NetwMultiplayer::liveness_get_routes
    );
    ClassDB::bind_method(
        D_METHOD("send_auth", "id", "data"),
        &NetwMultiplayer::send_auth
    );
    ClassDB::bind_method(
        D_METHOD("complete_auth", "id"),
        &NetwMultiplayer::complete_auth
    );
    ClassDB::bind_method(
        D_METHOD("get_authenticating_peers"),
        &NetwMultiplayer::get_authenticating_peers
    );
    ClassDB::bind_method(
        D_METHOD("auth_set_app_tag", "tag"),
        &NetwMultiplayer::auth_set_app_tag
    );
    ClassDB::bind_method(
        D_METHOD("set_auth_callback", "callback"),
        &NetwMultiplayer::set_auth_callback
    );
    ClassDB::bind_method(
        D_METHOD("get_auth_callback"),
        &NetwMultiplayer::get_auth_callback
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "auth_callback"),
        "set_auth_callback",
        "get_auth_callback"
    );
    ClassDB::bind_method(
        D_METHOD("auth_set_flow", "flow"),
        &NetwMultiplayer::auth_set_flow
    );
    ClassDB::bind_method(
        D_METHOD("auth_effective_flow"),
        &NetwMultiplayer::auth_effective_flow
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "auth_flow",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwAuthFlow"
        ),
        "",
        "auth_effective_flow"
    );
    ClassDB::bind_method(
        D_METHOD("auth_seat_host_identity"),
        &NetwMultiplayer::auth_seat_host_identity
    );
    ClassDB::bind_method(
        D_METHOD("session_answer_probe", "peer"),
        &NetwMultiplayer::session_answer_probe
    );
    ClassDB::bind_method(
        D_METHOD("disconnect_peer", "id"),
        &NetwMultiplayer::disconnect_peer
    );
    ClassDB::bind_method(
        D_METHOD("peer_get_bucket", "peer", "bucket_type"),
        &NetwMultiplayer::peer_get_bucket
    );
    ClassDB::bind_method(
        D_METHOD("peer_has_bucket", "peer", "bucket_type"),
        &NetwMultiplayer::peer_has_bucket
    );
    ClassDB::bind_static_method(
        "NetwMultiplayer",
        D_METHOD("session_role_name", "role"),
        &NetwMultiplayer::session_role_name
    );
    BIND_ENUM_CONSTANT(SESSION_STATE_OFFLINE);
    BIND_ENUM_CONSTANT(SESSION_STATE_CONNECTING);
    BIND_ENUM_CONSTANT(SESSION_STATE_ONLINE);
    BIND_ENUM_CONSTANT(SESSION_STATE_DISCONNECTING);
    BIND_ENUM_CONSTANT(ROLE_NONE);
    BIND_ENUM_CONSTANT(ROLE_CLIENT);
    BIND_ENUM_CONSTANT(ROLE_DEDICATED_SERVER);
    BIND_ENUM_CONSTANT(ROLE_LISTEN_SERVER);
    BIND_ENUM_CONSTANT(NAME_RENAME);
    BIND_ENUM_CONSTANT(NAME_REFUSE);

    BIND_ENUM_CONSTANT(TRANSPORT_MODE_HOST);
    BIND_ENUM_CONSTANT(TRANSPORT_MODE_CLIENT);
    ClassDB::bind_method(
        D_METHOD("session_set_state", "state"),
        &NetwMultiplayer::session_set_state
    );
    ClassDB::bind_method(
        D_METHOD("session_set_role", "role"),
        &NetwMultiplayer::session_set_role
    );
    ClassDB::bind_method(
        D_METHOD("session_submit_join", "username", "args"),
        &NetwMultiplayer::session_submit_join,
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("session_transition", "state"),
        &NetwMultiplayer::session_transition
    );
    ClassDB::bind_method(
        D_METHOD("session_pause", "reason"),
        &NetwMultiplayer::session_pause,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("session_unpause"),
        &NetwMultiplayer::session_unpause
    );
    ClassDB::bind_method(
        D_METHOD("session_notify_shutdown", "reason"),
        &NetwMultiplayer::session_notify_shutdown,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("session_request_leave", "reason"),
        &NetwMultiplayer::session_request_leave,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("peer_kick", "peer_id", "reason"),
        &NetwMultiplayer::peer_kick,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("peer_request_kick", "peer_id", "reason"),
        &NetwMultiplayer::peer_request_kick,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD(
            "spawn_admit_frame_default",
            "sender",
            "route",
            "channel",
            "payload"
        ),
        &NetwMultiplayer::spawn_admit_frame_default
    );
    ClassDB::bind_method(
        D_METHOD("table_admit_frame_default", "sender", "channel", "payload"),
        &NetwMultiplayer::table_admit_frame_default
    );
    ClassDB::bind_method(
        D_METHOD("spawn_declare_default", "entity", "recipe"),
        &NetwMultiplayer::spawn_declare_default
    );
    ClassDB::bind_method(
        D_METHOD("spawn_construct_default", "entity"),
        &NetwMultiplayer::spawn_construct_default
    );
}

} // namespace netw
