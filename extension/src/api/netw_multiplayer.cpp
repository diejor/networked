#include "netw/api/netw_multiplayer.hpp"

#include "netw/api/participant.hpp"

#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/join_payload.hpp"
#include "netw/entity_identity.hpp"
#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/packed_scene.hpp"
#include "godot/resource.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "godot/viewport.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/api/synchronizers.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;
using namespace netw;

namespace netw {

const char *SIG_BEFORE_TICK = "before_tick";
const char *SIG_ON_TICK = "on_tick";
const char *SIG_AFTER_TICK = "after_tick";
const char *SIG_BEFORE_TICK_LOOP = "before_tick_loop";
const char *SIG_AFTER_TICK_LOOP = "after_tick_loop";
const char *SIG_CLOCK_SYNCHRONIZED = "clock_synchronized";
const char *SIG_CLOCK_TICKRATE_MISMATCH = "clock_tickrate_mismatch";
const char *SIG_CLOCK_PONG_RECEIVED = "clock_pong_received";
const char *SIG_DISPLAY_OFFSET_INSUFFICIENT = "display_offset_insufficient";
const char *SIG_STABILITY_CHANGED = "stability_changed";
const char *SIG_STATE_CHANGED = "state_changed";
const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_ENDED = "session_ended";
const char *SIG_SESSION_RECLAIMED = "session_reclaimed";
const char *SIG_PEER_AUTHENTICATING = "peer_authenticating";
const char *SIG_PEER_AUTHENTICATION_FAILED = "peer_authentication_failed";
const char *SIG_POLL_STARTED = "poll_started";
const char *SIG_TABLE_RECEIVED = "table_received";
const char *SIG_PEER_PACKET = "peer_packet";
const char *SIG_KICKED = "kicked";
const char *SIG_SERVER_DISCONNECTING = "server_disconnecting";
const char *SIG_KICK_REQUESTED = "kick_requested";
const char *SIG_DISCONNECT_REQUESTED = "disconnect_requested";
const char *SIG_TREE_PAUSED = "tree_paused";
const char *SIG_TREE_UNPAUSED = "tree_unpaused";
const char *SIG_SERVICE_REGISTERED = "service_registered";
const char *SIG_SERVICE_UNREGISTERED = "service_unregistered";
const char *SIG_ENTITY_LIVE = "entity_live";
const char *SIG_ENTITY_LINGERING = "entity_lingering";
const char *SIG_ENTITY_DEAD = "entity_dead";
const char *SIG_LOCAL_PLAYER_CHANGED = "local_player_changed";
const char *SIG_PARTICIPANT_JOINED = "participant_joined";
const char *SIG_LOCAL_PARTICIPANT_JOINED = "local_participant_joined";
const char *SIG_LOCAL_SCENE_CHANGED = "local_scene_changed";
const char *SIG_SCENE_LIVE = "scene_live";
const char *SIG_SCENE_CHANGED = "scene_changed";

Object *script_of(Object *p_service) {
    if (p_service == nullptr) {
        return nullptr;
    }
    const Variant script = p_service->get_script();
    return script.get_type() == Variant::OBJECT
        ? static_cast<Object *>(script)
        : nullptr;
}

void NetwMultiplayerCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_peer_ids", "peer_ids"),
        &NetwMultiplayerCore::set_peer_ids
    );
    ClassDB::bind_method(
        D_METHOD("get_liveness_core"),
        &NetwMultiplayerCore::get_liveness_core
    );
    ClassDB::bind_method(
        D_METHOD("get_clock_handle"),
        &NetwMultiplayerCore::get_clock_handle
    );
    ClassDB::bind_method(
        D_METHOD("get_scene_core"),
        &NetwMultiplayerCore::get_scene_core
    );
    ClassDB::bind_method(
        D_METHOD("get_display_book"),
        &NetwMultiplayerCore::get_display_book
    );
    ClassDB::bind_method(
        D_METHOD("get_channel_book"),
        &NetwMultiplayerCore::get_channel_book
    );
    ClassDB::bind_method(
        D_METHOD("get_lagcomp_core"),
        &NetwMultiplayerCore::get_lagcomp_core
    );
    ClassDB::bind_method(
        D_METHOD("get_prediction_engine"),
        &NetwMultiplayerCore::get_prediction_engine
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "prediction_engine",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPredictionEngine"
        ),
        "",
        "get_prediction_engine"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "lagcomp_core",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwLagCompCore"
        ),
        "",
        "get_lagcomp_core"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "channel_book",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwChannelBook"
        ),
        "",
        "get_channel_book"
    );
    ClassDB::bind_method(
        D_METHOD("reset_interest"),
        &NetwMultiplayerCore::reset_interest
    );
    ClassDB::bind_method(
        D_METHOD("interest_sync_record", "wrapper"),
        &NetwMultiplayerCore::interest_sync_record
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_refresh", "refresh"),
        &NetwMultiplayerCore::set_scene_refresh
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer", "name"),
        &NetwMultiplayerCore::interest_layer
    );
    ClassDB::bind_method(
        D_METHOD("interest_layer_named", "name"),
        &NetwMultiplayerCore::interest_layer_named
    );
    ClassDB::bind_method(
        D_METHOD("interest_layers"),
        &NetwMultiplayerCore::interest_layers
    );
    ClassDB::bind_method(
        D_METHOD("interest_leave_resolve", "entity", "peer_id"),
        &NetwMultiplayerCore::interest_leave_resolve
    );
    ClassDB::bind_method(
        D_METHOD(
            "interest_leave_commit",
            "entity",
            "peer_id",
            "decision",
            "forced"
        ),
        &NetwMultiplayerCore::interest_leave_commit
    );
    ClassDB::bind_method(
        D_METHOD("interest_leave_finish_sweep"),
        &NetwMultiplayerCore::interest_leave_finish_sweep
    );
    ClassDB::bind_method(
        D_METHOD("interest_receive_awareness", "payload", "sender"),
        &NetwMultiplayerCore::interest_receive_awareness
    );
    ClassDB::bind_method(
        D_METHOD("interest_reapply_perception", "entity"),
        &NetwMultiplayerCore::interest_reapply_perception
    );
    ClassDB::bind_method(
        D_METHOD("interest_participant_sees", "peer_id", "entity"),
        &NetwMultiplayerCore::interest_participant_sees
    );
    ClassDB::bind_method(
        D_METHOD("set_interest_flush", "flush"),
        &NetwMultiplayerCore::set_interest_flush
    );
    ClassDB::bind_method(
        D_METHOD("set_interest_compat_refresh", "refresh"),
        &NetwMultiplayerCore::set_interest_compat_refresh
    );
    ClassDB::bind_method(
        D_METHOD("set_interest_awareness_send", "send"),
        &NetwMultiplayerCore::set_interest_awareness_send
    );
    ClassDB::bind_method(
        D_METHOD("set_interest_visibility_sweep", "sweep"),
        &NetwMultiplayerCore::set_interest_visibility_sweep
    );
    ClassDB::bind_method(
        D_METHOD("interest_recompute"),
        &NetwMultiplayerCore::interest_recompute
    );
    ClassDB::bind_method(
        D_METHOD("interest_commit"),
        &NetwMultiplayerCore::interest_commit
    );
    ClassDB::bind_method(
        D_METHOD("interest_flush_tail"),
        &NetwMultiplayerCore::interest_flush_tail
    );
    ClassDB::bind_method(
        D_METHOD("interest_resolved_layer_ids", "entity"),
        &NetwMultiplayerCore::interest_resolved_layer_ids
    );
    ClassDB::bind_method(
        D_METHOD("interest_shared_entities", "entity", "layer_id"),
        &NetwMultiplayerCore::interest_shared_entities,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("interest_monitor_snapshot"),
        &NetwMultiplayerCore::interest_monitor_snapshot
    );
    ClassDB::bind_method(
        D_METHOD("interest_wire_admits", "peer_id", "entity"),
        &NetwMultiplayerCore::interest_wire_admits
    );
    ClassDB::bind_method(
        D_METHOD("interest_entity_has_filter", "entity"),
        &NetwMultiplayerCore::interest_entity_has_filter
    );
    ClassDB::bind_method(
        D_METHOD("interest_has_committed_intent", "entity"),
        &NetwMultiplayerCore::interest_has_committed_intent
    );
    ClassDB::bind_method(
        D_METHOD("interest_committed_row", "entity"),
        &NetwMultiplayerCore::interest_committed_row
    );
    ClassDB::bind_method(
        D_METHOD("interest_bit_admits", "entity", "peer_bit"),
        &NetwMultiplayerCore::interest_bit_admits
    );
    ClassDB::bind_method(
        D_METHOD("interest_explain_bit", "entity", "peer_bit"),
        &NetwMultiplayerCore::interest_explain_bit
    );
    ClassDB::bind_method(
        D_METHOD("interest_forget_layer_row", "layer_id"),
        &NetwMultiplayerCore::interest_forget_layer_row
    );
    ClassDB::bind_method(
        D_METHOD("interest_peer_bit", "peer_id"),
        &NetwMultiplayerCore::interest_peer_bit
    );
    ClassDB::bind_method(
        D_METHOD("interest_has_layer", "layer_id"),
        &NetwMultiplayerCore::interest_has_layer
    );
    ClassDB::bind_method(
        D_METHOD("interest_sync_scene_membership", "entity"),
        &NetwMultiplayerCore::interest_sync_scene_membership
    );
    ClassDB::bind_method(
        D_METHOD("interest_peer_connected", "peer_id"),
        &NetwMultiplayerCore::interest_peer_connected
    );
    ClassDB::bind_method(
        D_METHOD("interest_peer_disconnected", "peer_id"),
        &NetwMultiplayerCore::interest_peer_disconnected
    );
    ClassDB::bind_method(
        D_METHOD("interest_session_ended"),
        &NetwMultiplayerCore::interest_session_ended
    );
    ClassDB::bind_method(
        D_METHOD("interest_known_peers"),
        &NetwMultiplayerCore::interest_known_peers
    );
    ClassDB::bind_method(
        D_METHOD("interest_set_entity_intent", "entity", "admitted"),
        &NetwMultiplayerCore::interest_set_entity_intent
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("interest_flush_key"),
        &NetwMultiplayerCore::interest_flush_key
    );
    ClassDB::bind_method(
        D_METHOD("interest_request_flush"),
        &NetwMultiplayerCore::interest_request_flush
    );
    ClassDB::bind_method(
        D_METHOD("interest_flush_pending"),
        &NetwMultiplayerCore::interest_flush_pending
    );
    ClassDB::bind_method(
        D_METHOD(
            "interest_awareness_queue_layer",
            "peer_id",
            "route",
            "layer_id",
            "kind"
        ),
        &NetwMultiplayerCore::interest_awareness_queue_layer
    );
    ClassDB::bind_method(
        D_METHOD(
            "interest_awareness_queue_observer",
            "peer_id",
            "route",
            "layer_id",
            "observer_peer",
            "kind"
        ),
        &NetwMultiplayerCore::interest_awareness_queue_observer
    );
    ClassDB::bind_method(
        D_METHOD("set_clock_mismatch_action", "action"),
        &NetwMultiplayerCore::set_clock_mismatch_action
    );
    ClassDB::bind_method(
        D_METHOD("clock_request_handshake"),
        &NetwMultiplayerCore::clock_request_handshake
    );
    ClassDB::bind_method(
        D_METHOD("clock_send_ping"),
        &NetwMultiplayerCore::clock_send_ping
    );
    ClassDB::bind_method(
        D_METHOD("clock_receive_handshake", "payload", "sender"),
        &NetwMultiplayerCore::clock_receive_handshake
    );
    ClassDB::bind_method(
        D_METHOD("clock_receive_handshake_reply", "payload", "sender"),
        &NetwMultiplayerCore::clock_receive_handshake_reply
    );
    ClassDB::bind_method(
        D_METHOD("clock_receive_ping", "payload", "sender"),
        &NetwMultiplayerCore::clock_receive_ping
    );
    ClassDB::bind_method(
        D_METHOD("clock_receive_pong", "payload", "sender"),
        &NetwMultiplayerCore::clock_receive_pong
    );
    ClassDB::bind_method(
        D_METHOD("interest_awareness_drain"),
        &NetwMultiplayerCore::interest_awareness_drain
    );
    ClassDB::bind_method(
        D_METHOD("interest_awareness_forget", "peer_id"),
        &NetwMultiplayerCore::interest_awareness_forget
    );
    ClassDB::bind_method(
        D_METHOD("interest_awareness_clear"),
        &NetwMultiplayerCore::interest_awareness_clear
    );
    ClassDB::bind_method(
        D_METHOD("set_display_role_resolver", "resolver"),
        &NetwMultiplayerCore::set_display_role_resolver
    );
    ClassDB::bind_method(
        D_METHOD("set_display_chase_clamp", "clamp"),
        &NetwMultiplayerCore::set_display_chase_clamp
    );
    ClassDB::bind_method(
        D_METHOD("set_display_spec_reader", "reader"),
        &NetwMultiplayerCore::set_display_spec_reader
    );
    ClassDB::bind_method(
        D_METHOD("set_display_lane", "lane"),
        &NetwMultiplayerCore::set_display_lane
    );
    ClassDB::bind_method(
        D_METHOD("set_display_sync_intervals", "compute"),
        &NetwMultiplayerCore::set_display_sync_intervals
    );
    ClassDB::bind_method(
        D_METHOD("set_display_authors_streams", "authors"),
        &NetwMultiplayerCore::set_display_authors_streams
    );
    ClassDB::bind_method(
        D_METHOD("set_display_role_facts", "facts"),
        &NetwMultiplayerCore::set_display_role_facts
    );
    ClassDB::bind_method(
        D_METHOD("set_display_chase_hook", "hook"),
        &NetwMultiplayerCore::set_display_chase_hook
    );
    ClassDB::bind_method(
        D_METHOD("display_resolve_role", "runtime"),
        &NetwMultiplayerCore::display_resolve_role
    );
    ClassDB::bind_method(
        D_METHOD("display_bind_session"),
        &NetwMultiplayerCore::display_bind_session
    );
    ClassDB::bind_method(
        D_METHOD("display_config_for", "entity"),
        &NetwMultiplayerCore::display_config_for
    );
    ClassDB::bind_method(
        D_METHOD("display_route_of", "entity"),
        &NetwMultiplayerCore::display_route_of
    );
    ClassDB::bind_method(
        D_METHOD("display_runtime_for", "route", "entity"),
        &NetwMultiplayerCore::display_runtime_for
    );
    ClassDB::bind_method(
        D_METHOD("display_on_entity_live", "route", "entity"),
        &NetwMultiplayerCore::display_on_entity_live
    );
    ClassDB::bind_method(
        D_METHOD("display_on_entity_dead", "route"),
        &NetwMultiplayerCore::display_on_entity_dead
    );
    ClassDB::bind_method(
        D_METHOD("display_clear_runtimes"),
        &NetwMultiplayerCore::display_clear_runtimes
    );
    ClassDB::bind_method(
        D_METHOD("display_on_control_changed", "previous", "peer", "route"),
        &NetwMultiplayerCore::display_on_control_changed
    );
    ClassDB::bind_method(
        D_METHOD("display_on_reparented", "opts", "route"),
        &NetwMultiplayerCore::display_on_reparented
    );
    ClassDB::bind_method(
        D_METHOD("display_mark_role_dirty", "entity"),
        &NetwMultiplayerCore::display_mark_role_dirty
    );
    ClassDB::bind_method(
        D_METHOD("display_drain_dirty"),
        &NetwMultiplayerCore::display_drain_dirty
    );
    ClassDB::bind_method(
        D_METHOD("display_on_book_dirty", "entity", "dirt"),
        &NetwMultiplayerCore::display_on_book_dirty
    );
    ClassDB::bind_method(
        D_METHOD(
            "display_record",
            "node",
            "target_property",
            "value",
            "tick",
            "spec",
            "authoring_tick"
        ),
        &NetwMultiplayerCore::display_record
    );
    ClassDB::bind_method(
        D_METHOD("display_on_clock_tick", "delta", "tick"),
        &NetwMultiplayerCore::display_on_clock_tick
    );
    ClassDB::bind_method(
        D_METHOD("display_pump", "delta"),
        &NetwMultiplayerCore::display_pump
    );
    ClassDB::bind_method(
        D_METHOD("display_wants_runtime", "owner"),
        &NetwMultiplayerCore::display_wants_runtime
    );
    ClassDB::bind_method(
        D_METHOD("display_rebuild_runtime", "runtime"),
        &NetwMultiplayerCore::display_rebuild_runtime
    );
    ClassDB::bind_method(
        D_METHOD(
            "display_ensure_state",
            "runtime",
            "node",
            "source_prop",
            "target_prop",
            "spec",
            "authoring_tick"
        ),
        &NetwMultiplayerCore::display_ensure_state
    );
    ClassDB::bind_method(
        D_METHOD("display_authors_streams", "runtime"),
        &NetwMultiplayerCore::display_authors_streams
    );
    ClassDB::bind_method(
        D_METHOD("display_pump_runtime", "runtime", "timing", "stats"),
        &NetwMultiplayerCore::display_pump_runtime,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("display_pump_entity", "entity", "timing"),
        &NetwMultiplayerCore::display_pump_entity
    );
    ClassDB::bind_method(
        D_METHOD("display_chase_smooth_time", "runtime", "timing"),
        &NetwMultiplayerCore::display_chase_smooth_time
    );
    ClassDB::bind_method(
        D_METHOD("display_absorb_recovery", "runtime", "deltas", "teleported"),
        &NetwMultiplayerCore::display_absorb_recovery
    );
    ClassDB::bind_method(
        D_METHOD("scene_despawn", "scene", "drain_pumps"),
        &NetwMultiplayerCore::scene_despawn
    );
    ClassDB::bind_method(
        D_METHOD("scene_request", "is_path", "destination", "args", "from_capture"),
        &NetwMultiplayerCore::scene_request
    );
    ClassDB::bind_method(
        D_METHOD(
            "scene_request_open",
            "is_path",
            "destination",
            "args",
            "from_capture",
            "deadline"
        ),
        &NetwMultiplayerCore::scene_request_open
    );
    ClassDB::bind_method(
        D_METHOD("scene_request_expire", "request_id"),
        &NetwMultiplayerCore::scene_request_expire
    );
    ClassDB::bind_method(
        D_METHOD("set_request_deadline_arm", "arm"),
        &NetwMultiplayerCore::set_request_deadline_arm
    );
    ClassDB::bind_method(
        D_METHOD("scene_receive_result_frame", "payload", "sender"),
        &NetwMultiplayerCore::scene_receive_result_frame
    );
    ClassDB::bind_method(
        D_METHOD("scene_send_result", "peer", "request_id", "code"),
        &NetwMultiplayerCore::scene_send_result
    );
    ClassDB::bind_method(
        D_METHOD(
            "scene_answer_when_settled",
            "operation",
            "peer",
            "request_id"
        ),
        &NetwMultiplayerCore::scene_answer_when_settled
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_mark_reader", "reader"),
        &NetwMultiplayerCore::set_scene_mark_reader
    );
    ClassDB::bind_method(
        D_METHOD("scene_mark_of", "script"),
        &NetwMultiplayerCore::scene_mark_of
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_path_reader", "reader"),
        &NetwMultiplayerCore::set_scene_path_reader
    );
    ClassDB::bind_method(
        D_METHOD("scene_request_targets", "destination", "label"),
        &NetwMultiplayerCore::scene_request_targets
    );
    ClassDB::bind_method(
        D_METHOD("scene_set_request_handler", "handler"),
        &NetwMultiplayerCore::scene_set_request_handler
    );
    ClassDB::bind_method(
        D_METHOD("set_request_reach", "reach"),
        &NetwMultiplayerCore::set_request_reach
    );
    ClassDB::bind_method(
        D_METHOD("get_request_reach"),
        &NetwMultiplayerCore::get_request_reach
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "request_reach"),
        "set_request_reach",
        "get_request_reach"
    );
    ClassDB::bind_method(
        D_METHOD("get_current_scene"),
        &NetwMultiplayerCore::get_current_scene
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::RID, "current_scene"),
        "",
        "get_current_scene"
    );
    ClassDB::bind_method(
        D_METHOD("scene_named", "stem"),
        &NetwMultiplayerCore::scene_named
    );
    ClassDB::bind_method(
        D_METHOD("scenes_named", "stem"),
        &NetwMultiplayerCore::scenes_named
    );
    ClassDB::bind_method(
        D_METHOD("live_scenes"),
        &NetwMultiplayerCore::live_scenes
    );
    ClassDB::bind_method(
        D_METHOD("scene_observe", "scene", "event", "callback"),
        &NetwMultiplayerCore::scene_observe
    );
    ClassDB::bind_method(
        D_METHOD("scene_unobserve", "scene", "event", "callback"),
        &NetwMultiplayerCore::scene_unobserve
    );
    ClassDB::bind_method(
        D_METHOD("layer_open", "name"),
        &NetwMultiplayerCore::layer_open
    );
    ClassDB::bind_method(
        D_METHOD("layer_named", "name"),
        &NetwMultiplayerCore::layer_named
    );
    ClassDB::bind_method(
        D_METHOD("layer_name_of", "layer"),
        &NetwMultiplayerCore::layer_name_of
    );
    ClassDB::bind_method(
        D_METHOD("layer_view", "layer"),
        &NetwMultiplayerCore::layer_view
    );
    ClassDB::bind_method(
        D_METHOD("layer_close", "layer"),
        &NetwMultiplayerCore::layer_close
    );
    ClassDB::bind_method(
        D_METHOD("layer_forget_all"),
        &NetwMultiplayerCore::layer_forget_all
    );
    ClassDB::bind_method(
        D_METHOD("interest_has_filter", "entity"),
        &NetwMultiplayerCore::interest_has_filter
    );
    ClassDB::bind_method(
        D_METHOD("interest_membership_ids", "entity"),
        &NetwMultiplayerCore::interest_membership_ids
    );
    ClassDB::bind_method(
        D_METHOD("set_persistence_quit_guard", "guard"),
        &NetwMultiplayerCore::set_persistence_quit_guard
    );
    ClassDB::bind_method(
        D_METHOD("set_persistence_drain", "drain"),
        &NetwMultiplayerCore::set_persistence_drain
    );
    ClassDB::bind_method(
        D_METHOD("persistence_engine_for", "entity"),
        &NetwMultiplayerCore::persistence_engine_for
    );
    ClassDB::bind_method(
        D_METHOD("persistence_tick", "delta"),
        &NetwMultiplayerCore::persistence_tick
    );
    ClassDB::bind_method(
        D_METHOD("persistence_flush_all"),
        &NetwMultiplayerCore::persistence_flush_all
    );
    ClassDB::bind_method(
        D_METHOD("persistence_owner_exiting", "entity"),
        &NetwMultiplayerCore::persistence_owner_exiting
    );
    ClassDB::bind_method(
        D_METHOD("persistence_live_engines"),
        &NetwMultiplayerCore::persistence_live_engines
    );
    ClassDB::bind_method(
        D_METHOD("persistence_shutdown"),
        &NetwMultiplayerCore::persistence_shutdown
    );
    ClassDB::bind_method(
        D_METHOD("get_state"),
        &NetwMultiplayerCore::get_state
    );
    ClassDB::bind_method(
        D_METHOD("get_role"),
        &NetwMultiplayerCore::get_role
    );
    ClassDB::bind_method(
        D_METHOD("is_online"),
        &NetwMultiplayerCore::is_online
    );
    ClassDB::bind_method(
        D_METHOD("is_host"),
        &NetwMultiplayerCore::is_host
    );
    ClassDB::bind_method(
        D_METHOD("is_local_client"),
        &NetwMultiplayerCore::is_local_client
    );
    ClassDB::bind_method(
        D_METHOD("count_sent", "bytes"),
        &NetwMultiplayerCore::count_sent
    );
    ClassDB::bind_method(
        D_METHOD("count_received", "bytes"),
        &NetwMultiplayerCore::count_received
    );
    ClassDB::bind_method(
        D_METHOD("count_state_ack_out"),
        &NetwMultiplayerCore::count_state_ack_out
    );
    ClassDB::bind_method(
        D_METHOD("count_state_ack_in"),
        &NetwMultiplayerCore::count_state_ack_in
    );
    ClassDB::bind_method(
        D_METHOD("count_standalone_ack_out"),
        &NetwMultiplayerCore::count_standalone_ack_out
    );
    ClassDB::bind_method(
        D_METHOD("get_sent_packets"),
        &NetwMultiplayerCore::get_sent_packets
    );
    ClassDB::bind_method(
        D_METHOD("get_sent_bytes"),
        &NetwMultiplayerCore::get_sent_bytes
    );
    ClassDB::bind_method(
        D_METHOD("get_received_packets"),
        &NetwMultiplayerCore::get_received_packets
    );
    ClassDB::bind_method(
        D_METHOD("get_received_bytes"),
        &NetwMultiplayerCore::get_received_bytes
    );
    ClassDB::bind_method(
        D_METHOD("get_state_acks_out"),
        &NetwMultiplayerCore::get_state_acks_out
    );
    ClassDB::bind_method(
        D_METHOD("get_state_acks_in"),
        &NetwMultiplayerCore::get_state_acks_in
    );
    ClassDB::bind_method(
        D_METHOD("get_standalone_acks_out"),
        &NetwMultiplayerCore::get_standalone_acks_out
    );
    ClassDB::bind_method(
        D_METHOD("poll_delta", "now_usec"),
        &NetwMultiplayerCore::poll_delta
    );
    ClassDB::bind_method(
        D_METHOD("advance_frame"),
        &NetwMultiplayerCore::advance_frame
    );
    ClassDB::bind_method(
        D_METHOD("get_frame_counter"),
        &NetwMultiplayerCore::get_frame_counter
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("seq_is_fresher", "a", "b"),
        &NetwMultiplayerCore::seq_is_fresher
    );
    ClassDB::bind_method(
        D_METHOD("datagram_budget"),
        &NetwMultiplayerCore::datagram_budget
    );
    ClassDB::bind_method(
        D_METHOD("set_inner", "inner"),
        &NetwMultiplayerCore::set_inner
    );
    ClassDB::bind_method(D_METHOD("get_inner"), &NetwMultiplayerCore::get_inner);
    ClassDB::bind_method(
        D_METHOD("set_session_root", "reader"),
        &NetwMultiplayerCore::set_session_root
    );
    ClassDB::bind_method(
        D_METHOD("session_root"),
        &NetwMultiplayerCore::session_root
    );
    ClassDB::bind_method(
        D_METHOD("set_desired_role_reader", "reader"),
        &NetwMultiplayerCore::set_desired_role_reader
    );
    ClassDB::bind_method(
        D_METHOD("set_identity_reader", "reader"),
        &NetwMultiplayerCore::set_identity_reader
    );
    ClassDB::bind_method(
        D_METHOD("participant_identity", "peer"),
        &NetwMultiplayerCore::participant_identity
    );
    ClassDB::bind_method(
        D_METHOD("authored_desired_role"),
        &NetwMultiplayerCore::authored_desired_role
    );
    ClassDB::bind_method(
        D_METHOD("presents_as_listen_host"),
        &NetwMultiplayerCore::presents_as_listen_host
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("frame_pack", "route", "comp", "channel", "payload", "path"),
        &NetwMultiplayerCore::frame_pack,
        DEFVAL(String())
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
        &NetwMultiplayerCore::send_to,
        DEFVAL(0),
        DEFVAL(String()),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("send_datagram", "peer", "payload", "reliable"),
        &NetwMultiplayerCore::send_datagram
    );
    ClassDB::bind_method(
        D_METHOD("channel_aggregates", "channel", "requested"),
        &NetwMultiplayerCore::channel_aggregates
    );
    ClassDB::bind_method(
        D_METHOD("carrier_append", "peer", "frame", "reliable"),
        &NetwMultiplayerCore::carrier_append
    );
    ClassDB::bind_method(
        D_METHOD("carrier_flush"),
        &NetwMultiplayerCore::carrier_flush
    );
    ClassDB::bind_method(
        D_METHOD("carrier_clear"),
        &NetwMultiplayerCore::carrier_clear
    );
    ClassDB::bind_method(
        D_METHOD("carrier_pending", "peer", "reliable"),
        &NetwMultiplayerCore::carrier_pending
    );
    ClassDB::bind_method(
        D_METHOD("frame_datagram", "peer", "payload", "reliable"),
        &NetwMultiplayerCore::frame_datagram
    );
    ClassDB::bind_method(
        D_METHOD("next_send_seq", "peer"),
        &NetwMultiplayerCore::next_send_seq
    );
    ClassDB::bind_method(
        D_METHOD("has_inbound_seq", "peer"),
        &NetwMultiplayerCore::has_inbound_seq
    );
    ClassDB::bind_method(
        D_METHOD("inbound_seq", "peer"),
        &NetwMultiplayerCore::inbound_seq
    );
    ClassDB::bind_method(
        D_METHOD("note_inbound_seq", "peer", "seq"),
        &NetwMultiplayerCore::note_inbound_seq
    );
    ClassDB::bind_method(
        D_METHOD("note_peer_ack", "peer", "ack"),
        &NetwMultiplayerCore::note_peer_ack
    );
    ClassDB::bind_method(
        D_METHOD("peer_ack", "peer"),
        &NetwMultiplayerCore::peer_ack
    );
    ClassDB::bind_method(
        D_METHOD("note_echoed_seq", "peer", "seq"),
        &NetwMultiplayerCore::note_echoed_seq
    );
    ClassDB::bind_method(
        D_METHOD("peers_owed_echo"),
        &NetwMultiplayerCore::peers_owed_echo
    );
    ClassDB::bind_method(
        D_METHOD("forget_peer_seqs", "peer"),
        &NetwMultiplayerCore::forget_peer_seqs
    );
    ClassDB::bind_method(
        D_METHOD("clear_seq_books"),
        &NetwMultiplayerCore::clear_seq_books
    );
    ClassDB::bind_method(
        D_METHOD("count_verdict", "verdict", "route"),
        &NetwMultiplayerCore::count_verdict,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("connect_once", "signal", "callback", "flags"),
        &NetwMultiplayerCore::connect_once,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("verdict_total", "verdict"),
        &NetwMultiplayerCore::verdict_total
    );
    ClassDB::bind_method(
        D_METHOD("warn_verdict", "verdict", "route"),
        &NetwMultiplayerCore::warn_verdict,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("claim_verdict_warning", "verdict", "route"),
        &NetwMultiplayerCore::claim_verdict_warning
    );
    ClassDB::bind_method(
        D_METHOD("sink_verdict", "verdict", "route"),
        &NetwMultiplayerCore::sink_verdict
    );
    ClassDB::bind_method(
        D_METHOD("stage_verdict", "stage", "verdict", "route"),
        &NetwMultiplayerCore::stage_verdict
    );
    ClassDB::bind_method(
        D_METHOD("clear_verdicts"),
        &NetwMultiplayerCore::clear_verdicts
    );
    ClassDB::bind_method(
        D_METHOD("settle_schedule_after", "fn", "key", "pumps"),
        &NetwMultiplayerCore::settle_schedule_after
    );
    ClassDB::bind_method(
        D_METHOD("settle_advance"),
        &NetwMultiplayerCore::settle_advance
    );
    ClassDB::bind_method(
        D_METHOD("settle_schedule", "fn", "key"),
        &NetwMultiplayerCore::settle_schedule
    );
    ClassDB::bind_method(
        D_METHOD("settle_cancel", "key"),
        &NetwMultiplayerCore::settle_cancel
    );
    ClassDB::bind_method(
        D_METHOD("settle_drain"),
        &NetwMultiplayerCore::settle_drain
    );
    ClassDB::bind_method(
        D_METHOD("settle_clear"),
        &NetwMultiplayerCore::settle_clear
    );
    ClassDB::bind_method(
        D_METHOD("settle_pending"),
        &NetwMultiplayerCore::settle_pending
    );
    ClassDB::bind_method(
        D_METHOD("settle_has_key", "key"),
        &NetwMultiplayerCore::settle_has_key
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("settle_max_passes"),
        &NetwMultiplayerCore::settle_max_passes
    );
    ClassDB::bind_method(
        D_METHOD("get_schema_core"),
        &NetwMultiplayerCore::get_schema_core
    );
    ClassDB::bind_method(
        D_METHOD("schema_create", "name"),
        &NetwMultiplayerCore::schema_create
    );
    ClassDB::bind_method(
        D_METHOD("schema_add_column", "schema", "key", "type", "stride"),
        &NetwMultiplayerCore::schema_add_column
    );
    ClassDB::bind_method(
        D_METHOD("schema_set_column_quantizer", "schema", "column", "quantizer"),
        &NetwMultiplayerCore::schema_set_column_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("schema_seal", "schema"),
        &NetwMultiplayerCore::schema_seal
    );
    ClassDB::bind_method(
        D_METHOD("schema_find", "name"),
        &NetwMultiplayerCore::schema_find
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_hash", "schema"),
        &NetwMultiplayerCore::schema_get_hash
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_count", "schema"),
        &NetwMultiplayerCore::schema_get_column_count
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_key", "schema", "column"),
        &NetwMultiplayerCore::schema_get_column_key
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_type", "schema", "column"),
        &NetwMultiplayerCore::schema_get_column_type
    );
    ClassDB::bind_method(
        D_METHOD("schema_get_column_stride", "schema", "column"),
        &NetwMultiplayerCore::schema_get_column_stride
    );
    ClassDB::bind_method(
        D_METHOD("get_effect_ledger"),
        &NetwMultiplayerCore::get_effect_ledger
    );
    ClassDB::bind_method(
        D_METHOD("effect_arm", "key", "revert", "timeout_ticks"),
        &NetwMultiplayerCore::effect_arm
    );
    ClassDB::bind_method(
        D_METHOD("effect_watch", "key", "confirmed", "denied"),
        &NetwMultiplayerCore::effect_watch
    );
    ClassDB::bind_method(
        D_METHOD("effect_adopt", "key"),
        &NetwMultiplayerCore::effect_adopt
    );
    ClassDB::bind_method(
        D_METHOD("effect_discard", "key"),
        &NetwMultiplayerCore::effect_discard
    );
    ClassDB::bind_method(
        D_METHOD("effect_pending", "key"),
        &NetwMultiplayerCore::effect_pending
    );
    ClassDB::bind_method(
        D_METHOD("effect_count"),
        &NetwMultiplayerCore::effect_count
    );
    ClassDB::bind_method(
        D_METHOD("effect_sweep", "tick"),
        &NetwMultiplayerCore::effect_sweep
    );
    ClassDB::bind_method(
        D_METHOD("event_watch", "events", "target", "predicate", "sink",
                 "opts"),
        &NetwMultiplayerCore::event_watch,
        DEFVAL(Dictionary()),
        DEFVAL(Dictionary()),
        DEFVAL(Callable()),
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("event_emit", "event", "route", "detail", "entity_id",
                 "peer", "verdict", "model"),
        &NetwMultiplayerCore::event_emit,
        DEFVAL(0),
        DEFVAL(Dictionary()),
        DEFVAL(StringName()),
        DEFVAL(0),
        DEFVAL(OK),
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("overrides_seam", "script", "base_name", "seam"),
        &NetwMultiplayerCore::overrides_seam
    );
    ClassDB::bind_method(
        D_METHOD("forget_seam_overrides"),
        &NetwMultiplayerCore::forget_seam_overrides
    );
    ClassDB::bind_method(
        D_METHOD("seam_entered", "seam", "event", "route", "detail"),
        &NetwMultiplayerCore::seam_entered,
        DEFVAL(0),
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("seam_settled", "seam", "event", "route", "result",
                 "fallback"),
        &NetwMultiplayerCore::seam_settled
    );
    ClassDB::bind_method(
        D_METHOD("event_unwatch", "id"),
        &NetwMultiplayerCore::event_unwatch
    );
    ClassDB::bind_method(
        D_METHOD("event_watches"),
        &NetwMultiplayerCore::event_watches
    );
    ClassDB::bind_method(
        D_METHOD("event_ring", "route"),
        &NetwMultiplayerCore::event_ring
    );
    ClassDB::bind_method(
        D_METHOD("event_ring_clear", "route"),
        &NetwMultiplayerCore::event_ring_clear
    );
    ClassDB::bind_method(
        D_METHOD("event_arm", "enabled"),
        &NetwMultiplayerCore::event_arm
    );
    ClassDB::bind_method(
        D_METHOD("event_wants", "event", "route"),
        &NetwMultiplayerCore::event_wants,
        DEFVAL(0)
    );
    for (int index = 0; index < EventPlane::taxonomy_size(); index++) {
        const int64_t value = EventPlane::value_at(index);
        ClassDB::bind_integer_constant(
            get_class_static(),
            StringName("Event"),
            StringName(EventPlane::name_of(value)),
            value
        );
    }
    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName("Phase"),
        StringName("BEFORE"),
        EventPlane::BEFORE
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        StringName("Phase"),
        StringName("AFTER"),
        EventPlane::AFTER
    );
    ClassDB::bind_method(
        D_METHOD("get_table_core"),
        &NetwMultiplayerCore::get_table_core
    );
    ClassDB::bind_method(
        D_METHOD("table_create", "schema"),
        &NetwMultiplayerCore::table_create
    );
    ClassDB::bind_method(
        D_METHOD("table_get_schema", "table"),
        &NetwMultiplayerCore::table_get_schema
    );
    ClassDB::bind_method(
        D_METHOD("table_set_param", "table", "param", "value"),
        &NetwMultiplayerCore::table_set_param
    );
    ClassDB::bind_method(
        D_METHOD("table_find", "name"),
        &NetwMultiplayerCore::table_find
    );
    ClassDB::bind_method(
        D_METHOD("table_get_wire_hash", "table"),
        &NetwMultiplayerCore::table_get_wire_hash
    );
    ClassDB::bind_method(
        D_METHOD("table_write_routes", "table", "routes"),
        &NetwMultiplayerCore::table_write_routes
    );
    ClassDB::bind_method(
        D_METHOD("table_write_column", "table", "column", "data"),
        &NetwMultiplayerCore::table_write_column
    );
    ClassDB::bind_method(
        D_METHOD("table_commit", "table"),
        &NetwMultiplayerCore::table_commit
    );
    ClassDB::bind_method(
        D_METHOD("table_read_routes", "table"),
        &NetwMultiplayerCore::table_read_routes
    );
    ClassDB::bind_method(
        D_METHOD("table_read_column", "table", "column"),
        &NetwMultiplayerCore::table_read_column
    );
    ClassDB::bind_method(
        D_METHOD("table_read_births", "table"),
        &NetwMultiplayerCore::table_read_births
    );
    ClassDB::bind_method(
        D_METHOD("table_read_deaths", "table"),
        &NetwMultiplayerCore::table_read_deaths
    );
    ClassDB::bind_method(
        D_METHOD("table_get_row", "table", "route"),
        &NetwMultiplayerCore::table_get_row
    );
    ClassDB::bind_method(
        D_METHOD("table_get_rows", "table", "routes"),
        &NetwMultiplayerCore::table_get_rows
    );
    ClassDB::bind_method(
        D_METHOD("table_get_tick", "table"),
        &NetwMultiplayerCore::table_get_tick
    );
    BIND_ENUM_CONSTANT(TABLE_PARAM_RELIABLE);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "table_core",
            PROPERTY_HINT_RESOURCE_TYPE,
            "TableCore"
        ),
        "",
        "get_table_core"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "schema_core",
            PROPERTY_HINT_RESOURCE_TYPE,
            "SchemaCore"
        ),
        "",
        "get_schema_core"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "sent_packets"),
        "",
        "get_sent_packets"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "sent_bytes"),
        "",
        "get_sent_bytes"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "received_packets"),
        "",
        "get_received_packets"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "received_bytes"),
        "",
        "get_received_bytes"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "state_acks_out"),
        "",
        "get_state_acks_out"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "state_acks_in"),
        "",
        "get_state_acks_in"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "standalone_acks_out"),
        "",
        "get_standalone_acks_out"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "frame_counter"),
        "",
        "get_frame_counter"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "state"),
        "",
        "get_state"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "role"),
        "",
        "get_role"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_online"),
        "",
        "is_online"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_host"),
        "",
        "is_host"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_local_client"),
        "",
        "is_local_client"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "liveness_core",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwLivenessCore"
        ),
        "",
        "get_liveness_core"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "clock_handle",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwClockHandle"
        ),
        "",
        "get_clock_handle"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "scene_core",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneCore"
        ),
        "",
        "get_scene_core"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "display_book",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwDisplayBook"
        ),
        "",
        "get_display_book"
    );

    ADD_SIGNAL(MethodInfo(
        SIG_BEFORE_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ON_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_AFTER_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(SIG_BEFORE_TICK_LOOP));
    ADD_SIGNAL(MethodInfo(SIG_AFTER_TICK_LOOP));
    ADD_SIGNAL(MethodInfo(SIG_CLOCK_SYNCHRONIZED));
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
        SIG_DISPLAY_OFFSET_INSUFFICIENT,
        PropertyInfo(Variant::INT, "recommended")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_STABILITY_CHANGED,
        PropertyInfo(Variant::BOOL, "is_stable")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_STATE_CHANGED,
        PropertyInfo(Variant::INT, "old_state"),
        PropertyInfo(Variant::INT, "new_state")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SESSION_ENTERED));
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
        SIG_POLL_STARTED,
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
    ADD_SIGNAL(MethodInfo(
        SIG_KICKED,
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SERVER_DISCONNECTING,
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_KICK_REQUESTED,
        PropertyInfo(Variant::INT, "requester_id"),
        PropertyInfo(Variant::INT, "target_id"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_DISCONNECT_REQUESTED,
        PropertyInfo(Variant::INT, "peer_id"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_TREE_PAUSED,
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(SIG_TREE_UNPAUSED));
    ADD_SIGNAL(MethodInfo(
        SIG_SERVICE_REGISTERED,
        PropertyInfo(Variant::OBJECT, "service", PROPERTY_HINT_NODE_TYPE, "Node")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SERVICE_UNREGISTERED,
        PropertyInfo(Variant::OBJECT, "service", PROPERTY_HINT_NODE_TYPE, "Node")
    ));
    ClassDB::bind_method(
        D_METHOD("service_register", "type", "service"),
        &NetwMultiplayerCore::service_register
    );
    ClassDB::bind_method(
        D_METHOD("service_unregister", "type", "service"),
        &NetwMultiplayerCore::service_unregister
    );
    ClassDB::bind_method(
        D_METHOD("service_of", "type"),
        &NetwMultiplayerCore::service_held
    );
    ClassDB::bind_method(
        D_METHOD("service_all", "base"),
        &NetwMultiplayerCore::service_all
    );
    ClassDB::bind_method(
        D_METHOD("service_clear"),
        &NetwMultiplayerCore::service_clear
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
    ADD_SIGNAL(MethodInfo(
        SIG_ENTITY_DEAD,
        PropertyInfo(Variant::INT, "route")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_LOCAL_PLAYER_CHANGED,
        PropertyInfo(Variant::OBJECT, "player")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PARTICIPANT_JOINED,
        PropertyInfo(Variant::OBJECT, "participant")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_LOCAL_PARTICIPANT_JOINED,
        PropertyInfo(Variant::OBJECT, "participant")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_LOCAL_SCENE_CHANGED,
        PropertyInfo(Variant::OBJECT, "from"),
        PropertyInfo(Variant::OBJECT, "to")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCENE_LIVE,
        PropertyInfo(Variant::OBJECT, "scene")
    ));
    ClassDB::bind_method(
        D_METHOD("entity_scene_of", "entity"),
        &NetwMultiplayerCore::entity_scene_of
    );
    ClassDB::bind_method(
        D_METHOD(
            "scene_report_entity_edge",
            "subject",
            "present",
            "is_player"
        ),
        &NetwMultiplayerCore::scene_report_entity_edge
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("wrapper_meta"),
        &NetwMultiplayerCore::wrapper_meta
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("set_wrapper_factory", "factory"),
        &NetwMultiplayerCore::set_wrapper_factory
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("has_wrapper_factory"),
        &NetwMultiplayerCore::has_wrapper_factory
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("wrapper_at", "node"),
        &NetwMultiplayerCore::wrapper_at
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("wrapper_ensure", "root"),
        &NetwMultiplayerCore::wrapper_ensure
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("wrapper_resolve", "node"),
        &NetwMultiplayerCore::wrapper_resolve
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("wrapper_bind", "node", "entity_id", "peer_id"),
        &NetwMultiplayerCore::wrapper_held
    );
    ClassDB::bind_method(
        D_METHOD("entity_reparent_crosses", "entity", "destination"),
        &NetwMultiplayerCore::entity_reparent_crosses
    );
    ClassDB::bind_method(
        D_METHOD("set_spawn_state_gather", "gather"),
        &NetwMultiplayerCore::set_spawn_state_gather
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD(
            "entity_enter_tree",
            "wrapper",
            "owner",
            "record",
            "shell",
            "is_authority"
        ),
        &NetwMultiplayerCore::entity_enter_tree
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("entity_request_control", "wrapper", "owner", "plane"),
        &NetwMultiplayerCore::entity_request_control
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("entity_broadcast_control", "wrapper", "plane", "peer"),
        &NetwMultiplayerCore::entity_broadcast_control
    );
    ClassDB::bind_method(
        D_METHOD("set_replication_plane", "plane"),
        &NetwMultiplayerCore::set_replication_plane
    );
    ClassDB::bind_method(
        D_METHOD("set_spawn_pipeline", "pipeline"),
        &NetwMultiplayerCore::set_spawn_pipeline
    );
    ClassDB::bind_method(
        D_METHOD("set_sync_pipeline", "pipeline"),
        &NetwMultiplayerCore::set_sync_pipeline
    );
    ClassDB::bind_method(
        D_METHOD("sync_send_property", "entity", "comp", "property"),
        &NetwMultiplayerCore::sync_send_property
    );
    ClassDB::bind_method(
        D_METHOD("sync_send_signal", "entity", "comp", "signal", "args"),
        &NetwMultiplayerCore::sync_send_signal
    );
    ClassDB::bind_method(
        D_METHOD("entity_control_request", "entity"),
        &NetwMultiplayerCore::entity_control_request
    );
    ClassDB::bind_method(
        D_METHOD("spawn_replicate", "node", "owner"),
        &NetwMultiplayerCore::spawn_replicate,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("spawn_function", "function", "args", "owner"),
        &NetwMultiplayerCore::spawn_function,
        DEFVAL(Array()),
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
        &NetwMultiplayerCore::spawn_register_constructor,
        DEFVAL(Array()),
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("spawn_registered", "id", "args", "owner"),
        &NetwMultiplayerCore::spawn_registered,
        DEFVAL(Array()),
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("spawn_adopt", "root"),
        &NetwMultiplayerCore::spawn_adopt
    );
    ClassDB::bind_method(
        D_METHOD("spawn_state_of", "entity"),
        &NetwMultiplayerCore::spawn_state_of
    );
    ClassDB::bind_method(
        D_METHOD("entity_derived_binding", "owner", "record", "route"),
        &NetwMultiplayerCore::entity_derived_binding
    );
    ClassDB::bind_method(
        D_METHOD("entity_derived_group", "route"),
        &NetwMultiplayerCore::entity_derived_group
    );
    ClassDB::bind_method(
        D_METHOD("entity_governs_property", "owner", "path", "exclude", "route"),
        &NetwMultiplayerCore::entity_governs_property
    );
    ClassDB::bind_method(
        D_METHOD("entity_instantiate_from", "template_node", "configure"),
        &NetwMultiplayerCore::entity_instantiate_from
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("entity_instantiate_copy", "template_node", "configure"),
        &NetwMultiplayerCore::entity_instantiate_copy
    );
    ClassDB::bind_method(
        D_METHOD("entity_spawn_under", "owner", "parent", "id"),
        &NetwMultiplayerCore::entity_spawn_under
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("entity_spawn_copy_under", "owner", "parent", "id"),
        &NetwMultiplayerCore::entity_spawn_copy_under
    );
    ClassDB::bind_method(
        D_METHOD("entity_instantiate_player", "owner", "participant"),
        &NetwMultiplayerCore::entity_instantiate_player
    );
    ClassDB::bind_method(
        D_METHOD("entity_spawn_player", "owner", "participant", "scene"),
        &NetwMultiplayerCore::entity_spawn_player
    );
    ClassDB::bind_method(
        D_METHOD("entity_linger", "record", "owner", "pumps"),
        &NetwMultiplayerCore::entity_linger
    );
    ClassDB::bind_method(
        D_METHOD("entity_free_owner", "owner"),
        &NetwMultiplayerCore::entity_free_owner
    );
    ClassDB::bind_method(
        D_METHOD("entity_settle_reparented", "wrapper", "owner", "opts"),
        &NetwMultiplayerCore::entity_settle_reparented
    );
    ClassDB::bind_method(
        D_METHOD("entity_announce_reparented", "wrapper", "owner", "opts"),
        &NetwMultiplayerCore::entity_announce_reparented
    );
    ClassDB::bind_method(
        D_METHOD("entity_reparent", "record", "owner", "new_parent", "opts"),
        &NetwMultiplayerCore::entity_reparent
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("entity_move", "record", "owner", "new_parent", "opts"),
        &NetwMultiplayerCore::entity_move
    );
    ClassDB::bind_method(
        D_METHOD("scene_handle_of", "entity"),
        &NetwMultiplayerCore::scene_handle_of
    );
    ClassDB::bind_method(
        D_METHOD("entity_scene_facet", "record", "wrapper"),
        &NetwMultiplayerCore::entity_scene_facet
    );
    ClassDB::bind_method(
        D_METHOD("scene_publish_live", "route", "container", "name"),
        &NetwMultiplayerCore::scene_publish_live
    );
    ClassDB::bind_method(
        D_METHOD("participant_adopt", "peer", "participant"),
        &NetwMultiplayerCore::participant_adopt
    );
    ClassDB::bind_method(
        D_METHOD("participant_ensure", "peer"),
        &NetwMultiplayerCore::participant_ensure
    );
    ClassDB::bind_method(
        D_METHOD("participant_admit", "peer"),
        &NetwMultiplayerCore::participant_admit
    );
    ClassDB::bind_method(
        D_METHOD("participant_admitted_of", "peer"),
        &NetwMultiplayerCore::participant_admitted_of
    );
    ClassDB::bind_method(
        D_METHOD("participant_admitted_all"),
        &NetwMultiplayerCore::participant_admitted_all
    );
    ClassDB::bind_method(
        D_METHOD("participant_admitted_local"),
        &NetwMultiplayerCore::participant_admitted_local
    );
    ClassDB::bind_method(
        D_METHOD("participant_of", "peer"),
        &NetwMultiplayerCore::participant_of
    );
    ClassDB::bind_method(
        D_METHOD("participant_has", "peer"),
        &NetwMultiplayerCore::participant_has
    );
    ClassDB::bind_method(
        D_METHOD("participant_all"),
        &NetwMultiplayerCore::participant_all
    );
    ClassDB::bind_method(
        D_METHOD("participant_forget", "peer"),
        &NetwMultiplayerCore::participant_forget
    );
    ClassDB::bind_method(
        D_METHOD("participant_clear"),
        &NetwMultiplayerCore::participant_clear
    );
    ClassDB::bind_method(
        D_METHOD("participant_seat", "peer"),
        &NetwMultiplayerCore::participant_seat
    );
    ClassDB::bind_method(
        D_METHOD("participant_take_seat", "peer", "scene"),
        &NetwMultiplayerCore::participant_take_seat
    );
    ClassDB::bind_method(
        D_METHOD("participant_leave_seat", "peer", "scene"),
        &NetwMultiplayerCore::participant_leave_seat
    );
    ClassDB::bind_method(
        D_METHOD("participant_seat_move", "peer", "scene"),
        &NetwMultiplayerCore::participant_seat_move
    );
    ClassDB::bind_method(
        D_METHOD("participant_seat_clear", "peer", "scene"),
        &NetwMultiplayerCore::participant_seat_clear
    );
    ClassDB::bind_method(
        D_METHOD("participant_move_seat", "peer", "scene"),
        &NetwMultiplayerCore::participant_move_seat
    );
    ClassDB::bind_method(
        D_METHOD("participant_seated_in", "scene"),
        &NetwMultiplayerCore::participant_seated_in
    );
    ClassDB::bind_method(
        D_METHOD("participant_publish_joined", "peer"),
        &NetwMultiplayerCore::participant_publish_joined
    );
    ClassDB::bind_method(
        D_METHOD("get_local_player"),
        &NetwMultiplayerCore::get_local_player
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::OBJECT, "local_player"),
        "",
        "get_local_player"
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_adopt", "entity", "wrapper", "owner"),
        &NetwMultiplayerCore::wrapper_adopt
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_owner", "entity"),
        &NetwMultiplayerCore::wrapper_owner
    );
    ClassDB::bind_method(
        D_METHOD("handle_of_wrapper", "wrapper"),
        &NetwMultiplayerCore::handle_of_wrapper
    );
    ClassDB::bind_method(
        D_METHOD("entity_parent_of", "entity"),
        &NetwMultiplayerCore::entity_parent_of
    );
    ClassDB::bind_method(
        D_METHOD("scene_request_flooded", "peer", "now_msec"),
        &NetwMultiplayerCore::scene_request_flooded
    );
    ClassDB::bind_method(
        D_METHOD("scene_request_frame_row", "payload", "sender", "now_msec"),
        &NetwMultiplayerCore::scene_request_frame_row
    );
    ClassDB::bind_method(
        D_METHOD("scene_released_seat", "payload", "sender"),
        &NetwMultiplayerCore::scene_released_seat
    );
    ClassDB::bind_method(
        D_METHOD("scene_capture_verdict", "path"),
        &NetwMultiplayerCore::scene_capture_verdict
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_destination_kind", "destination"),
        &NetwMultiplayerCore::scene_destination_kind
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_packed_at", "reference"),
        &NetwMultiplayerCore::scene_packed_at
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_packed_root_script", "packed"),
        &NetwMultiplayerCore::scene_packed_root_script
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_root_script_at", "reference"),
        &NetwMultiplayerCore::scene_root_script_at
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_resolve_requested_path", "reference"),
        &NetwMultiplayerCore::scene_resolve_requested_path
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD(
            "scene_move_verdict",
            "mover_live",
            "target_live",
            "same_scene"
        ),
        &NetwMultiplayerCore::scene_move_verdict
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_native_change_strands", "online", "marked"),
        &NetwMultiplayerCore::scene_native_change_strands
    );
    BIND_ENUM_CONSTANT(SCENE_CAPTURE_REFUSED);
    BIND_ENUM_CONSTANT(SCENE_CAPTURE_REQUEST);
    BIND_ENUM_CONSTANT(SCENE_CAPTURE_CHANGE_SESSION);
    BIND_ENUM_CONSTANT(SCENE_CAPTURE_MOVE_ME);
    BIND_ENUM_CONSTANT(SCENE_CAPTURE_ACTIVATE);
    BIND_ENUM_CONSTANT(SCENE_MOVE_REFUSED);
    BIND_ENUM_CONSTANT(SCENE_MOVE_ALREADY_THERE);
    BIND_ENUM_CONSTANT(SCENE_MOVE_CARRY);
    BIND_ENUM_CONSTANT(SCENE_DESTINATION_NONE);
    BIND_ENUM_CONSTANT(SCENE_DESTINATION_NAME);
    BIND_ENUM_CONSTANT(SCENE_DESTINATION_NODE);
    BIND_ENUM_CONSTANT(SCENE_DESTINATION_PACKED);
    BIND_ENUM_CONSTANT(SCENE_DESTINATION_PACKED_UNPATHED);
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_container_meta"),
        &NetwMultiplayerCore::scene_container_meta
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_build_container", "hosting", "own_world"),
        &NetwMultiplayerCore::scene_build_container
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_packed_stem", "packed"),
        &NetwMultiplayerCore::scene_packed_stem
    );
    ClassDB::bind_method(
        D_METHOD("scene_container", "stem"),
        &NetwMultiplayerCore::scene_container
    );
    ClassDB::bind_method(
        D_METHOD("scene_existing_destination", "destination"),
        &NetwMultiplayerCore::scene_existing_destination
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_install_level", "container", "level"),
        &NetwMultiplayerCore::scene_install_level
    );
    ClassDB::bind_method(
        D_METHOD("scene_spawn_node", "data", "isolation"),
        &NetwMultiplayerCore::scene_spawn_node
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_constructor_id"),
        &NetwMultiplayerCore::scene_constructor_id
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_level_of", "container"),
        &NetwMultiplayerCore::scene_level_of
    );
    ClassDB::bind_method(
        D_METHOD("scene_owns_its_world", "scene"),
        &NetwMultiplayerCore::scene_owns_its_world
    );
    ClassDB::bind_method(
        D_METHOD("scene_hosts_isolated_world"),
        &NetwMultiplayerCore::scene_hosts_isolated_world
    );
    ClassDB::bind_method(
        D_METHOD("scene_containing", "node"),
        &NetwMultiplayerCore::scene_containing
    );
    ClassDB::bind_method(
        D_METHOD("scene_spawn", "data", "isolation"),
        &NetwMultiplayerCore::scene_spawn,
        DEFVAL(-1)
    );
    ClassDB::bind_method(
        D_METHOD("scene_spawn_declared", "stem"),
        &NetwMultiplayerCore::scene_spawn_declared
    );
    ClassDB::bind_method(
        D_METHOD("scene_spawn_initial"),
        &NetwMultiplayerCore::scene_spawn_initial
    );
    ClassDB::bind_method(
        D_METHOD("scene_activate", "destination"),
        &NetwMultiplayerCore::scene_activate
    );
    ClassDB::bind_method(
        D_METHOD("scene_activate_named", "stem"),
        &NetwMultiplayerCore::scene_activate_named
    );
    ClassDB::bind_method(
        D_METHOD("scene_freeze", "stem"),
        &NetwMultiplayerCore::scene_freeze
    );
    ClassDB::bind_method(
        D_METHOD("scene_destroy", "stem"),
        &NetwMultiplayerCore::scene_destroy
    );
    ClassDB::bind_method(
        D_METHOD("scene_retire_named", "stem", "drain_pumps"),
        &NetwMultiplayerCore::scene_retire_named,
        DEFVAL(8)
    );
    ClassDB::bind_method(
        D_METHOD("scene_pump_retired"),
        &NetwMultiplayerCore::scene_pump_retired
    );
    ClassDB::bind_method(
        D_METHOD("scene_forget", "container"),
        &NetwMultiplayerCore::scene_forget
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("relative_path", "source", "target"),
        &NetwMultiplayerCore::relative_path
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("property_path", "source", "property", "base"),
        &NetwMultiplayerCore::property_path
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_of", "entity"),
        &NetwMultiplayerCore::wrapper_of
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_for_route", "route"),
        &NetwMultiplayerCore::wrapper_for_route
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_for_id", "id"),
        &NetwMultiplayerCore::wrapper_for_id
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_live"),
        &NetwMultiplayerCore::wrapper_live
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_sweep_retired"),
        &NetwMultiplayerCore::wrapper_sweep_retired
    );
    ClassDB::bind_method(
        D_METHOD("wrapper_clear"),
        &NetwMultiplayerCore::wrapper_clear
    );
    ClassDB::bind_method(
        D_METHOD(
            "liveness_bind",
            "entity",
            "route",
            "wrapper",
            "record",
            "owner"
        ),
        &NetwMultiplayerCore::liveness_bind
    );
    ClassDB::bind_method(
        D_METHOD(
            "liveness_adopt_route",
            "route",
            "wrapper",
            "record",
            "owner"
        ),
        &NetwMultiplayerCore::liveness_adopt_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_publish_live", "route"),
        &NetwMultiplayerCore::liveness_publish_live
    );
    ClassDB::bind_method(
        D_METHOD("liveness_settle_local_player", "route"),
        &NetwMultiplayerCore::liveness_settle_local_player
    );
    ClassDB::bind_method(
        D_METHOD("liveness_linger", "entity"),
        &NetwMultiplayerCore::liveness_linger
    );
    ClassDB::bind_method(
        D_METHOD("liveness_retire", "route"),
        &NetwMultiplayerCore::liveness_retire
    );
    ClassDB::bind_method(
        D_METHOD("liveness_reserve_route"),
        &NetwMultiplayerCore::liveness_reserve_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_allocate_route", "wrapper"),
        &NetwMultiplayerCore::liveness_allocate_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_bind_route", "route", "wrapper"),
        &NetwMultiplayerCore::liveness_bind_route
    );
    ClassDB::bind_method(
        D_METHOD("liveness_bind_routes_data", "routes"),
        &NetwMultiplayerCore::liveness_bind_routes_data
    );
    ClassDB::bind_method(
        D_METHOD("liveness_tombstone_routes_data", "routes"),
        &NetwMultiplayerCore::liveness_tombstone_routes_data
    );
    ClassDB::bind_method(
        D_METHOD("liveness_route_of", "wrapper"),
        &NetwMultiplayerCore::liveness_route_of
    );
    ClassDB::bind_method(
        D_METHOD("liveness_state_of", "wrapper"),
        &NetwMultiplayerCore::liveness_state_of
    );
    ClassDB::bind_method(
        D_METHOD("liveness_route_state", "route"),
        &NetwMultiplayerCore::liveness_route_state
    );
    ClassDB::bind_method(
        D_METHOD("entity_describe", "route"),
        &NetwMultiplayerCore::entity_describe
    );
    ClassDB::bind_method(
        D_METHOD("liveness_adopt", "wrapper"),
        &NetwMultiplayerCore::liveness_adopt
    );
    ClassDB::bind_method(
        D_METHOD("entity_of", "node"),
        &NetwMultiplayerCore::entity_of
    );
    ClassDB::bind_method(
        D_METHOD("scene_stem", "scene"),
        &NetwMultiplayerCore::scene_stem
    );
    ClassDB::bind_method(
        D_METHOD("scene_layer_id", "scene"),
        &NetwMultiplayerCore::scene_layer_id
    );
    ClassDB::bind_method(
        D_METHOD("scene_layer_view", "scene"),
        &NetwMultiplayerCore::scene_layer_view
    );
    ClassDB::bind_method(
        D_METHOD("scene_peers", "scene"),
        &NetwMultiplayerCore::scene_peers
    );
    ClassDB::bind_method(
        D_METHOD("scene_admit", "scene", "peer"),
        &NetwMultiplayerCore::scene_admit
    );
    ClassDB::bind_method(
        D_METHOD("scene_release", "scene", "peer"),
        &NetwMultiplayerCore::scene_release
    );
    ClassDB::bind_method(
        D_METHOD("scene_admits", "scene", "peer"),
        &NetwMultiplayerCore::scene_admits
    );
    ClassDB::bind_method(
        D_METHOD("scene_declared", "entity"),
        &NetwMultiplayerCore::scene_declared
    );
    ClassDB::bind_method(
        D_METHOD("scene_entity_node", "entity"),
        &NetwMultiplayerCore::scene_entity_node
    );
    ClassDB::bind_method(
        D_METHOD("scene_entities_under", "scene"),
        &NetwMultiplayerCore::scene_entities_under
    );
    ClassDB::bind_method(
        D_METHOD("scene_admit_peer", "scene", "peer"),
        &NetwMultiplayerCore::scene_admit_peer
    );
    ClassDB::bind_method(
        D_METHOD("scene_release_peer", "scene", "peer"),
        &NetwMultiplayerCore::scene_release_peer
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_participant_edge", "edge"),
        &NetwMultiplayerCore::set_scene_participant_edge
    );
    ClassDB::bind_method(
        D_METHOD("scene_notify_released", "scene", "peer"),
        &NetwMultiplayerCore::scene_notify_released
    );
    ClassDB::bind_method(
        D_METHOD(
            "scene_release_departed",
            "scene",
            "subject",
            "mover_live",
            "peer"
        ),
        &NetwMultiplayerCore::scene_release_departed
    );
    ClassDB::bind_method(
        D_METHOD("scene_seat_sync", "peer"),
        &NetwMultiplayerCore::scene_seat_sync
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_seat_clear_key", "peer", "scene"),
        &NetwMultiplayerCore::scene_seat_clear_key
    );
    ClassDB::bind_method(
        D_METHOD("scene_seat_clear_deferred", "peer", "scene"),
        &NetwMultiplayerCore::scene_seat_clear_deferred
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_seat_release_key", "peer", "scene"),
        &NetwMultiplayerCore::scene_seat_release_key
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_carry_move", "carry"),
        &NetwMultiplayerCore::set_scene_carry_move
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_move_reason"),
        &NetwMultiplayerCore::scene_move_reason
    );
    ClassDB::bind_method(
        D_METHOD("scene_move_entity", "entity", "destination", "opts"),
        &NetwMultiplayerCore::scene_move_entity
    );
    ClassDB::bind_method(
        D_METHOD("scene_move_participants", "scene", "peers"),
        &NetwMultiplayerCore::scene_move_participants
    );
    ClassDB::bind_method(
        D_METHOD("scene_report_moved", "batch", "peers"),
        &NetwMultiplayerCore::scene_report_moved
    );
    ClassDB::bind_method(
        D_METHOD("liveness_node_of", "route"),
        &NetwMultiplayerCore::liveness_node_of
    );
    ClassDB::bind_method(
        D_METHOD("liveness_live_entities"),
        &NetwMultiplayerCore::liveness_live_entities
    );
    ClassDB::bind_method(
        D_METHOD(
            "liveness_when_live",
            "route",
            "callback",
            "deadline",
            "on_clock",
            "on_timeout"
        ),
        &NetwMultiplayerCore::liveness_when_live
    );
    ClassDB::bind_method(
        D_METHOD("liveness_pending_live_count"),
        &NetwMultiplayerCore::liveness_pending_live_count
    );
    ClassDB::bind_method(
        D_METHOD("liveness_poll", "clock_tick"),
        &NetwMultiplayerCore::liveness_poll
    );
    ClassDB::bind_method(
        D_METHOD("liveness_clear_session"),
        &NetwMultiplayerCore::liveness_clear_session
    );
    ClassDB::bind_method(
        D_METHOD("liveness_owner_exiting", "wrapper"),
        &NetwMultiplayerCore::liveness_owner_exiting
    );
    ClassDB::bind_method(
        D_METHOD("liveness_resolve_tracked_exit", "route", "wrapper"),
        &NetwMultiplayerCore::liveness_resolve_tracked_exit
    );
    ClassDB::bind_method(
        D_METHOD("liveness_owner_despawning", "reason", "wrapper"),
        &NetwMultiplayerCore::liveness_owner_despawning
    );
    ClassDB::bind_method(
        D_METHOD("liveness_transition_dead", "route"),
        &NetwMultiplayerCore::liveness_transition_dead
    );
    ClassDB::bind_method(
        D_METHOD("session_publish_control", "channel", "sender", "payload"),
        &NetwMultiplayerCore::session_publish_control
    );
    ClassDB::bind_method(
        D_METHOD("set_session_join_handler", "handler"),
        &NetwMultiplayerCore::set_session_join_handler
    );
    ClassDB::bind_method(
        D_METHOD("set_session_join_resolver", "resolver"),
        &NetwMultiplayerCore::set_session_join_resolver
    );
    ClassDB::bind_method(
        D_METHOD("set_session_entered_hook", "hook"),
        &NetwMultiplayerCore::set_session_entered_hook
    );
    ClassDB::bind_method(
        D_METHOD("set_session_edge_hook", "hook"),
        &NetwMultiplayerCore::set_session_edge_hook
    );
    ClassDB::bind_method(
        D_METHOD("session_receive_join", "payload", "sender"),
        &NetwMultiplayerCore::session_receive_join
    );
    ClassDB::bind_method(
        D_METHOD("session_admit", "join"),
        &NetwMultiplayerCore::session_admit
    );
    ClassDB::bind_method(
        D_METHOD("session_accepted_join", "peer"),
        &NetwMultiplayerCore::session_accepted_join
    );
    ClassDB::bind_method(
        D_METHOD("session_accepted_joins"),
        &NetwMultiplayerCore::session_accepted_joins
    );
    ClassDB::bind_method(
        D_METHOD("session_remember_join", "join"),
        &NetwMultiplayerCore::session_remember_join
    );
    ClassDB::bind_method(
        D_METHOD("session_forget_peer", "peer"),
        &NetwMultiplayerCore::session_forget_peer
    );
    ClassDB::bind_method(
        D_METHOD("session_clear_roster"),
        &NetwMultiplayerCore::session_clear_roster
    );
    ClassDB::bind_method(
        D_METHOD("session_refuse", "peer", "reason"),
        &NetwMultiplayerCore::session_refuse
    );
    ClassDB::bind_method(
        D_METHOD("session_refusal", "peer"),
        &NetwMultiplayerCore::session_refusal
    );
    ClassDB::bind_method(
        D_METHOD(
            "session_name_verdict",
            "name",
            "taken",
            "is_debug",
            "has_identity"
        ),
        &NetwMultiplayerCore::session_name_verdict
    );
    ClassDB::bind_method(
        D_METHOD("session_free_name", "name", "taken"),
        &NetwMultiplayerCore::session_free_name
    );
    BIND_ENUM_CONSTANT(SESSION_STATE_OFFLINE);
    BIND_ENUM_CONSTANT(SESSION_STATE_CONNECTING);
    BIND_ENUM_CONSTANT(SESSION_STATE_ONLINE);
    BIND_ENUM_CONSTANT(SESSION_STATE_DISCONNECTING);
    BIND_ENUM_CONSTANT(ROLE_NONE);
    BIND_ENUM_CONSTANT(ROLE_CLIENT);
    BIND_ENUM_CONSTANT(ROLE_DEDICATED_SERVER);
    BIND_ENUM_CONSTANT(ROLE_LISTEN_SERVER);
    BIND_ENUM_CONSTANT(NAME_ADMIT);
    BIND_ENUM_CONSTANT(NAME_RENAME);
    BIND_ENUM_CONSTANT(NAME_REFUSE);
    ClassDB::bind_method(
        D_METHOD("session_set_state", "state"),
        &NetwMultiplayerCore::session_set_state
    );
    ClassDB::bind_method(
        D_METHOD("session_set_role", "role"),
        &NetwMultiplayerCore::session_set_role
    );
    ClassDB::bind_method(
        D_METHOD("session_set_desired_role", "role"),
        &NetwMultiplayerCore::session_set_desired_role
    );
    ClassDB::bind_method(
        D_METHOD("session_transition", "state"),
        &NetwMultiplayerCore::session_transition
    );
    ClassDB::bind_method(
        D_METHOD("session_peer_assigned", "live", "connected", "unique_id"),
        &NetwMultiplayerCore::session_peer_assigned
    );
    ClassDB::bind_method(
        D_METHOD("session_resolve_online", "unique_id"),
        &NetwMultiplayerCore::session_resolve_online
    );
    ClassDB::bind_method(
        D_METHOD("session_set_advertised_max_players", "cap"),
        &NetwMultiplayerCore::session_set_advertised_max_players
    );
    ClassDB::bind_method(
        D_METHOD("session_advertised_max_players"),
        &NetwMultiplayerCore::session_advertised_max_players
    );
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("session_app_tag", "app_id"),
        &NetwMultiplayerCore::session_app_tag
    );
    ClassDB::bind_method(
        D_METHOD("session_pause", "reason"),
        &NetwMultiplayerCore::session_pause,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("session_unpause"),
        &NetwMultiplayerCore::session_unpause
    );
    ClassDB::bind_method(
        D_METHOD("session_notify_shutdown", "reason"),
        &NetwMultiplayerCore::session_notify_shutdown,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("session_request_leave", "reason"),
        &NetwMultiplayerCore::session_request_leave,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("session_kick", "peer_id", "reason"),
        &NetwMultiplayerCore::session_kick,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("session_request_kick", "peer_id", "reason"),
        &NetwMultiplayerCore::session_request_kick,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("receive_header", "peer", "packet"),
        &NetwMultiplayerCore::receive_header
    );
    ClassDB::bind_method(
        D_METHOD("table_publish_intake"),
        &NetwMultiplayerCore::table_publish_intake
    );
    ClassDB::bind_method(
        D_METHOD("table_publish", "table"),
        &NetwMultiplayerCore::table_publish
    );
    ClassDB::bind_method(
        D_METHOD(
            "spawn_admit_frame_default",
            "sender",
            "route",
            "channel",
            "payload"
        ),
        &NetwMultiplayerCore::spawn_admit_frame_default
    );
    ClassDB::bind_method(
        D_METHOD("table_admit_frame_default", "sender", "channel", "payload"),
        &NetwMultiplayerCore::table_admit_frame_default
    );
}

NetwMultiplayerCore::NetwMultiplayerCore() {
    carrier.instantiate();
    channel_book.instantiate();
    liveness_core.instantiate();
    schemas.instantiate();
    schema_core.instantiate();
    tables.instantiate();
    table_core.instantiate();
    effects.instantiate();
    clock_handle.instantiate();
    clock_handle->engine.sink.bind(this);
    scene_core.instantiate();
    display_book.instantiate();
    layer_ledger.instantiate();
    lagcomp_core.instantiate();
    prediction_engine.instantiate();

    session_core.announce_to(this);

    gate_channels.spawn = declared_channel("SPAWN");
    gate_channels.despawn = declared_channel("DESPAWN");
    gate_channels.reparent = declared_channel("REPARENT");
    gate_channels.table = declared_channel("TABLE");
    control_channels.kicked = declared_channel("SESSION_KICKED");
    control_channels.shutdown = declared_channel("SESSION_SHUTDOWN");
    control_channels.kick_request = declared_channel("SESSION_KICK_REQUEST");
    control_channels.leave_request = declared_channel("SESSION_LEAVE_REQUEST");
    control_channels.pause = declared_channel("SESSION_PAUSE");
    control_channels.unpause = declared_channel("SESSION_UNPAUSE");
    control_channels.control_request = declared_channel("CONTROL_REQUEST");
    clock_channels.handshake = declared_channel("CLOCK_HANDSHAKE");
    clock_channels.handshake_reply = declared_channel("CLOCK_HANDSHAKE_REPLY");
    clock_channels.ping = declared_channel("CLOCK_PING");
    clock_channels.pong = declared_channel("CLOCK_PONG");
    scene_request_channel = declared_channel("SESSION_SCENE_REQUEST");
    scene_result_channel = declared_channel("SESSION_SCENE_RESULT");
    scene_released_channel = declared_channel("SESSION_SCENE_RELEASED");
    session_join_channel = declared_channel("SESSION_JOIN");
    session_accept_channel = declared_channel("SESSION_ACCEPT");
    session_roster_channel = declared_channel("SESSION_ROSTER");

    channel_book->register_protocol(
        session_join_channel,
        callable_mp(this, &NetwMultiplayerCore::session_receive_join)
    );
    channel_book->register_protocol(
        session_accept_channel,
        callable_mp(this, &NetwMultiplayerCore::session_receive_accept)
    );
    channel_book->register_protocol(
        session_roster_channel,
        callable_mp(this, &NetwMultiplayerCore::session_receive_roster)
    );
    const uint8_t control[] = {
        control_channels.pause,
        control_channels.unpause,
        control_channels.kicked,
        control_channels.shutdown,
        control_channels.kick_request,
        control_channels.leave_request,
    };
    for (const uint8_t &channel : control) {
        channel_book->register_protocol(
            channel,
            callable_mp(this, &NetwMultiplayerCore::session_receive_control)
                .bind(channel)
        );
    }
}

uint8_t NetwMultiplayerCore::declared_channel(const char *p_name) const {
    const wire::ChannelDecl *decl
        = channels.find_channel_by_name(StringName(p_name));
    return decl ? decl->id : 0;
}

Ref<RefCounted> NetwMultiplayerCore::participant_ensure(int64_t p_peer) {
    ParticipantRow *found = participants.getptr(p_peer);
    if (found != nullptr) {
        return found->row;
    }
    Ref<NetwParticipant> minted;
    minted.instantiate();
    minted->seat_at(this, p_peer);
    NETW_TRACE(sys::SESSION, "minted the participant row for peer %d", int(p_peer));
    participants[p_peer] = ParticipantRow{ minted, RID(), false };
    return minted;
}

void NetwMultiplayerCore::participant_adopt(
    int64_t p_peer,
    const Ref<RefCounted> &p_participant
) {
    if (p_participant.is_null() || participants.has(p_peer)) {
        return;
    }
    participants[p_peer] = ParticipantRow{ p_participant, RID() };
}

Ref<RefCounted> NetwMultiplayerCore::participant_of(int64_t p_peer) const {
    const ParticipantRow *found = participants.getptr(p_peer);
    return found ? found->row : Ref<RefCounted>();
}

bool NetwMultiplayerCore::participant_has(int64_t p_peer) const {
    return participants.has(p_peer);
}

TypedArray<Object> NetwMultiplayerCore::participant_all() const {
    LocalVector<int64_t> peers;
    for (const KeyValue<int64_t, ParticipantRow> &row : participants) {
        peers.push_back(row.key);
    }
    peers.sort();
    TypedArray<Object> out;
    for (uint32_t at = 0; at < peers.size(); at++) {
        out.push_back(participants[peers[at]].row);
    }
    return out;
}

bool NetwMultiplayerCore::participant_admit(int64_t p_peer) {
    ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || found->admitted) {
        return false;
    }
    found->admitted = true;
    return true;
}

Ref<RefCounted> NetwMultiplayerCore::participant_admitted_of(int64_t p_peer
) const {
    const ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || !found->admitted) {
        return Ref<RefCounted>();
    }
    return found->row;
}

TypedArray<Object> NetwMultiplayerCore::participant_admitted_all() const {
    LocalVector<int64_t> peers;
    for (const KeyValue<int64_t, ParticipantRow> &row : participants) {
        if (row.value.admitted) {
            peers.push_back(row.key);
        }
    }
    peers.sort();
    TypedArray<Object> out;
    for (uint32_t at = 0; at < peers.size(); at++) {
        out.push_back(participants[peers[at]].row);
    }
    return out;
}

Ref<RefCounted> NetwMultiplayerCore::participant_admitted_local() {
    return participant_admitted_of(get_unique_id());
}

RID NetwMultiplayerCore::participant_seat(int64_t p_peer) const {
    const ParticipantRow *found = participants.getptr(p_peer);
    return found ? found->seat : RID();
}

bool NetwMultiplayerCore::participant_take_seat(
    int64_t p_peer,
    const RID &p_scene
) {
    ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || found->seat == p_scene) {
        return false;
    }
    NETW_TRACE(
        sys::SCENE,
        "peer %d takes the seat in scene %s",
        int(p_peer),
        p_scene
    );
    found->seat = p_scene;
    return true;
}

bool NetwMultiplayerCore::participant_leave_seat(
    int64_t p_peer,
    const RID &p_scene
) {
    ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || !found->seat.is_valid()
        || found->seat != p_scene) {
        return false;
    }
    NETW_TRACE(
        sys::SCENE,
        "peer %d leaves the seat in scene %s",
        int(p_peer),
        p_scene
    );
    found->seat = RID();
    return true;
}

void NetwMultiplayerCore::participant_announce_seat(
    const Ref<RefCounted> &p_row,
    const Ref<RefCounted> &p_from,
    const Ref<RefCounted> &p_to
) {
    if (p_row.is_null()) {
        return;
    }
    p_row->emit_signal(SIG_SCENE_CHANGED, p_from, p_to);
}

bool NetwMultiplayerCore::participant_seat_move(
    int64_t p_peer,
    const RID &p_scene
) {
    const ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr) {
        return false;
    }
    const Ref<RefCounted> row = found->row;
    const RID seat = found->seat;
    const Ref<RefCounted> from = scene_handle_of(seat);
    if (!participant_take_seat(p_peer, p_scene)) {
        return false;
    }
    participant_announce_seat(row, from, scene_handle_of(p_scene));
    return true;
}

bool NetwMultiplayerCore::participant_seat_clear(
    int64_t p_peer,
    const RID &p_scene
) {
    const ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr) {
        return false;
    }
    const Ref<RefCounted> row = found->row;
    const RID seat = found->seat;
    const Ref<RefCounted> from = scene_handle_of(seat);
    if (!participant_leave_seat(p_peer, p_scene)) {
        return false;
    }
    participant_announce_seat(row, from, Ref<RefCounted>());
    return true;
}

bool NetwMultiplayerCore::participant_move_seat(
    int64_t p_peer,
    const RID &p_scene
) {
    const ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || wrapper_owner(p_scene) == nullptr) {
        return false;
    }
    const RID left = found->seat;
    if (!participant_seat_move(p_peer, p_scene)) {
        return false;
    }
    if (left.is_valid()) {
        scene_release_peer(left, p_peer);
    }
    scene_admit_peer(p_scene, p_peer);
    return true;
}

PackedInt64Array NetwMultiplayerCore::participant_seated_in(const RID &p_scene
) const {
    LocalVector<int64_t> peers;
    for (const KeyValue<int64_t, ParticipantRow> &row : participants) {
        if (p_scene.is_valid() && row.value.seat == p_scene) {
            peers.push_back(row.key);
        }
    }
    peers.sort();
    PackedInt64Array out;
    for (uint32_t at = 0; at < peers.size(); at++) {
        out.push_back(peers[at]);
    }
    return out;
}

void NetwMultiplayerCore::participant_forget(int64_t p_peer) {
    if (participant_of(p_peer) == local_participant) {
        bind_local_participant(Ref<RefCounted>());
    }
    participants.erase(p_peer);
}

void NetwMultiplayerCore::participant_clear() {
    bind_local_participant(Ref<RefCounted>());
    participants.clear();
}

void NetwMultiplayerCore::bind_local_participant(
    const Ref<RefCounted> &p_row
) {
    if (local_participant == p_row) {
        return;
    }
    if (local_participant.is_valid()) {
        stop_relay_named_from(
            local_participant.ptr(),
            SIG_SCENE_CHANGED,
            SIG_LOCAL_SCENE_CHANGED,
            2
        );
    }
    local_participant = p_row;
    if (local_participant.is_valid()) {
        relay_named_from(
            local_participant.ptr(),
            SIG_SCENE_CHANGED,
            SIG_LOCAL_SCENE_CHANGED,
            2
        );
    }
}

void NetwMultiplayerCore::participant_publish_joined(int64_t p_peer) {
    const Ref<RefCounted> participant = participant_of(p_peer);
    if (participant.is_null()) {
        return;
    }
    if (p_peer == get_unique_id()) {
        bind_local_participant(participant);
        emit_signal(SIG_LOCAL_PARTICIPANT_JOINED, participant);
    }
    emit_signal(SIG_PARTICIPANT_JOINED, participant);
}

void NetwMultiplayerCore::wrapper_adopt(
    const RID &p_entity,
    const Ref<RefCounted> &p_wrapper,
    Object *p_owner
) {
    if (!p_entity.is_valid() || p_wrapper.is_null()) {
        return;
    }
    liveness_core->adopt(p_entity);
    const int64_t id = p_entity.get_id();
    live_wrappers[id] = p_wrapper;
    retired_wrappers.erase(id);
    handle_by_wrapper[uint64_t(gd::instance_id(p_wrapper.ptr()))] = id;
    if (p_owner != nullptr) {
        wrapper_owners[id] = gd::instance_id(p_owner);
    }
}

Object *NetwMultiplayerCore::wrapper_owner(const RID &p_entity) const {
    const godot::ObjectID *found = wrapper_owners.getptr(p_entity.get_id());
    return found ? gd::instance_from_id(*found) : nullptr;
}

RID NetwMultiplayerCore::handle_of_wrapper(Object *p_wrapper) const {
    if (p_wrapper == nullptr) {
        return RID();
    }
    const int64_t *id = handle_by_wrapper.getptr(
        uint64_t(gd::instance_id(p_wrapper))
    );
    if (id == nullptr) {
        return RID();
    }
    const Ref<NetwEntityRecord> *record = wrapper_records.getptr(*id);
    return record ? (*record)->get_handle() : RID();
}

namespace {

Callable &wrapper_mint() {
    static Callable factory;
    return factory;
}

}

StringName NetwMultiplayerCore::wrapper_meta() {
    return StringName("netw_entity");
}

void NetwMultiplayerCore::set_wrapper_factory(const Callable &p_factory) {
    wrapper_mint() = p_factory;
}

bool NetwMultiplayerCore::has_wrapper_factory() {
    return wrapper_mint().is_valid();
}

void NetwMultiplayerCore::clear_wrapper_factory() {
    wrapper_mint() = Callable();
}

Callable NetwMultiplayerCore::wrapper_factory() {
    return wrapper_mint();
}

Ref<RefCounted> NetwMultiplayerCore::wrapper_at(Object *p_node) {
    const StringName mark = wrapper_meta();
    Node *walker = Object::cast_to<Node>(p_node);
    while (walker != nullptr) {
        if (walker->has_meta(mark)) {
            const Variant found = walker->get_meta(mark);
            if (found.get_type() == Variant::OBJECT) {
                return found;
            }
        }
        walker = walker->get_parent();
    }
    return Ref<RefCounted>();
}

Ref<RefCounted> NetwMultiplayerCore::wrapper_ensure(Object *p_root) {
    Node *root = Object::cast_to<Node>(p_root);
    if (root == nullptr) {
        return Ref<RefCounted>();
    }
    const StringName mark = wrapper_meta();
    if (root->has_meta(mark)) {
        const Variant found = root->get_meta(mark);
        if (found.get_type() == Variant::OBJECT) {
            return found;
        }
    }
    const Callable &factory = wrapper_mint();
    if (!factory.is_valid()) {
        Ref<NetwEntity> entity;
        entity.instantiate();
        entity->attach_to(root);
        return entity;
    }
    const Variant made = factory.call(root);
    Ref<RefCounted> minted = made;
    NETW_WARN_COND(
        minted.is_null(),
        sys::ENTITY,
        "the entity wrapper factory answered nothing for '%s'",
        root->get_name()
    );
    return minted;
}

Ref<RefCounted> NetwMultiplayerCore::wrapper_resolve(Object *p_node) {
    Node *node = Object::cast_to<Node>(p_node);
    if (node == nullptr) {
        return Ref<RefCounted>();
    }
    Ref<RefCounted> existing = wrapper_at(node);
    if (existing.is_valid()) {
        return existing;
    }
    if (node->is_inside_tree()) {
        return Ref<RefCounted>();
    }
    Node *root = node;
    while (root->get_parent() != nullptr) {
        if (root->get_parent()->is_inside_tree()) {
            return Ref<RefCounted>();
        }
        root = root->get_parent();
    }
    return wrapper_ensure(root);
}

Variant NetwMultiplayerCore::wrapper_held(
    Object *p_node,
    const StringName &p_entity_id,
    int64_t p_peer_id
) {
    return gd::held(wrapper_bind(p_node, p_entity_id, p_peer_id));
}

Object *NetwMultiplayerCore::wrapper_bind(
    Object *p_node,
    const StringName &p_entity_id,
    int64_t p_peer_id
) {
    Node *node = Object::cast_to<Node>(p_node);
    if (node == nullptr) {
        return nullptr;
    }
    const String named = EntityIdentity::format(
        String(p_entity_id),
        p_peer_id
    );
    NETW_ERR_COND_V(
        named.is_empty(),
        p_node,
        sys::ENTITY,
        "'%s' cannot be named on the wire, so nothing is bound",
        p_entity_id
    );
    node->set_name(named);
    Ref<RefCounted> wrapper = wrapper_ensure(node);
    if (wrapper.is_valid()) {
        wrapper->set(StringName("entity_id"), p_entity_id);
        wrapper->set(StringName("peer_id"), p_peer_id);
    }
    return node;
}

RID NetwMultiplayerCore::entity_at_or_above(Object *p_node) const {
    return handle_of_wrapper(wrapper_at(p_node).ptr());
}

RID NetwMultiplayerCore::entity_parent_of(const RID &p_entity) const {
    Node *owner = Object::cast_to<Node>(wrapper_owner(p_entity));
    if (owner == nullptr) {
        return RID();
    }
    return entity_at_or_above(owner->get_parent());
}

RID NetwMultiplayerCore::entity_scene_of(const RID &p_entity) const {
    RID walker = p_entity;
    while (walker.is_valid()) {
        const Ref<NetwEntityRecord> *record = wrapper_records.getptr(
            walker.get_id()
        );
        if (record != nullptr && (*record)->get_declares_scene()) {
            return walker;
        }
        walker = entity_parent_of(walker);
    }
    return RID();
}

Ref<RefCounted> NetwMultiplayerCore::scene_handle_of(const RID &p_entity) {
    const Ref<NetwEntityRecord> *record = wrapper_records.getptr(
        p_entity.get_id()
    );
    if (record == nullptr) {
        return Ref<RefCounted>();
    }
    return (*record)->part(
        NetwEntityRecord::PART_SCENE,
        wrapper_of(p_entity).ptr()
    );
}

RID NetwMultiplayerCore::scene_report_entity_edge(
    const RID &p_subject,
    bool p_present,
    bool p_is_player
) {
    const RID scene = entity_scene_of(p_subject);
    if (!scene.is_valid() || scene == p_subject) {
        return RID();
    }
    scene_core->dispatch(
        scene,
        NetwSceneCore::EVENT_ENTITY,
        p_present,
        p_subject
    );
    if (p_is_player) {
        scene_core->dispatch(
            scene,
            NetwSceneCore::EVENT_PLAYER,
            p_present,
            p_subject
        );
    }
    return scene;
}

Ref<RefCounted> NetwMultiplayerCore::entity_scene_facet(
    const Ref<NetwEntityRecord> &p_record,
    Object *p_wrapper
) {
    if (p_record.is_null()) {
        return Ref<RefCounted>();
    }
    const RID own = p_record->get_handle();
    const RID resolved = entity_scene_of(own);
    if (resolved.is_valid() && resolved != own) {
        Ref<RefCounted> host = scene_handle_of(resolved);
        if (host.is_valid()) {
            return host;
        }
    }
    return p_record->part(NetwEntityRecord::PART_SCENE, p_wrapper);
}

void NetwMultiplayerCore::scene_publish_live(
    int64_t p_route,
    const RID &p_container,
    const String &p_name
) {
    Dictionary detail;
    detail["scene"] = p_name;
    event_emit(
        EventPlane::SCENE_LIVE,
        p_route,
        detail,
        StringName(),
        0,
        OK,
        Dictionary()
    );
    emit_signal(SIG_SCENE_LIVE, scene_handle_of(p_container));
}

bool NetwMultiplayerCore::entity_reparent_crosses(
    const RID &p_entity,
    const RID &p_destination
) {
    const Ref<NetwEntityRecord> *record = wrapper_records.getptr(
        p_entity.get_id()
    );
    if (record == nullptr || (*record)->get_peer_id() == 0) {
        return false;
    }
    const RID destination_scene = entity_scene_of(p_destination);
    if (!destination_scene.is_valid()) {
        return false;
    }
    return destination_scene != entity_scene_of(p_entity);
}

void NetwMultiplayerCore::entity_reparent(
    const Ref<NetwEntityRecord> &p_record,
    Object *p_owner,
    Object *p_new_parent,
    const Ref<NetwReparentOpts> &p_opts
) {
    Ref<NetwEntityRecord> record = p_record;
    Object *owner = p_owner;
    if (record.is_valid()) {
        record->set_reparenting(p_opts);
    }
    const RID destination = entity_at_or_above(p_new_parent);
    if (record.is_valid()
        && entity_reparent_crosses(record->get_handle(), destination)) {
        Ref<RefCounted> handle = scene_handle_of(entity_scene_of(destination));
        if (handle.is_valid()) {
            NETW_TRACE(
                "entity",
                "reparent carries peer %d across a scene boundary",
                int(record->get_peer_id())
            );
            handle->call(StringName("admit"), record->get_peer_id());
        }
    }
    entity_move(record, owner, p_new_parent, p_opts);
    if (record.is_valid()) {
        record->set_reparenting(Ref<NetwReparentOpts>());
    }
}

void NetwMultiplayerCore::entity_move(
    const Ref<NetwEntityRecord> &p_record,
    Object *p_owner,
    Object *p_new_parent,
    const Ref<NetwReparentOpts> &p_opts
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    Node *parent = Object::cast_to<Node>(p_new_parent);
    NETW_ERR_COND(
        owner == nullptr || parent == nullptr,
        sys::ENTITY,
        "a reparent needs an owner and a destination parent"
    );
    if (p_record.is_valid()) {
        p_record->set_reparenting(p_opts);
    }
    NETW_TRACE(sys::ENTITY, "moving an entity under %s", parent->get_name());
    if (p_opts.is_valid()
        && p_opts->target_global_position.get_type() != Variant::NIL) {
        owner->set(
            StringName("global_position"),
            p_opts->target_global_position
        );
    }
    owner->request_ready();
    if (owner->is_inside_tree()) {
        owner->reparent(parent);
    } else {
        if (owner->get_parent() != nullptr) {
            owner->get_parent()->remove_child(owner);
        }
        parent->add_child(owner);
    }
    if (p_record.is_valid()) {
        p_record->set_reparenting(Ref<NetwReparentOpts>());
    }
}

void NetwMultiplayerCore::entity_free_owner(Object *p_owner) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner != nullptr) {
        owner->queue_free();
    }
}

namespace {

Node *instantiate_scene_of(Object *p_template) {
    Node *template_node = Object::cast_to<Node>(p_template);
    if (template_node == nullptr) {
        return nullptr;
    }
    const String path = template_node->get_scene_file_path();
    NETW_ERR_COND_V(
        path.is_empty(),
        nullptr,
        sys::ENTITY,
        "'%s' is not a scene, so there is nothing to instantiate from it",
        template_node->get_name()
    );
    Ref<PackedScene> scene = netw::gd::load_scene(path);
    NETW_ERR_COND_V(
        scene.is_null(),
        nullptr,
        sys::ENTITY,
        "'%s' names a scene that will not load",
        path
    );
    return scene->instantiate();
}

void configure_copy(Node *p_copy, const Callable &p_configure) {
    if (!p_configure.is_valid()) {
        return;
    }
    p_configure.call(NetwMultiplayerCore::wrapper_ensure(p_copy));
}

}

void NetwMultiplayerCore::set_spawn_state_gather(const Callable &p_gather) {
    spawn_state = p_gather;
}

void NetwMultiplayerCore::entity_enter_tree(
    Object *p_wrapper,
    Object *p_owner,
    const Ref<NetwEntityRecord> &p_record,
    Object *p_session,
    bool p_is_authority
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr || p_wrapper == nullptr || p_record.is_null()) {
        return;
    }
    const bool is_reparent
        = p_record->get_stage() == int64_t(EntityStage::LIVE);
    p_record->hydrate_identity(owner);

    NetwMultiplayerCore *session
        = Object::cast_to<NetwMultiplayerCore>(p_session);
    if (!is_reparent
        && p_record->get_stage() == int64_t(EntityStage::UNBOUND)) {
        if (!p_record->classify_activation(owner)) {
            if (session != nullptr) {
                session->entity_note_stage(
                    p_record,
                    int64_t(EntityStage::UNBOUND)
                );
            }
            return;
        }
        p_wrapper->call(StringName("arm"));
    }

    if (!is_reparent && p_record->get_route() == 0 && session != nullptr
        && p_is_authority) {
        p_record->set_route(session->liveness_allocate_route(p_wrapper));
    }
    if (p_record->get_route() > 0 && session != nullptr) {
        session->liveness_bind_route(p_record->get_route(), p_wrapper);
    }

    if (is_reparent) {
        p_wrapper->call(
            StringName("_settle_emit_reparented"),
            p_record->get_reparenting()
        );
        p_record->apply_control(p_wrapper, owner, p_is_authority);
    } else {
        p_wrapper->call(StringName("_on_identity_hydrated"));
    }

    const Callable ready(p_wrapper, StringName("_on_owner_ready"));
    if (!owner->is_connected(StringName("ready"), ready)) {
        owner->connect(StringName("ready"), ready);
    }
    const int64_t steering = p_record->get_control()->resolve(
        p_record->get_peer_id()
    );
    Ref<MultiplayerAPI> api = owner->get_multiplayer();
    if ((p_record->get_peer_id() != 0 || steering != 0) && api.is_valid()) {
        const Callable dropped(p_wrapper, StringName("_on_peer_disconnected"));
        if (!api->is_connected(StringName("peer_disconnected"), dropped)) {
            api->connect(StringName("peer_disconnected"), dropped);
        }
    }

    if (!is_reparent) {
        const int64_t from = p_record->get_stage();
        p_record->transition(int64_t(EntityStage::LIVE), owner);
        NETW_TRACE(sys::ENTITY, "'%s' is live", owner->get_name());
        if (session != nullptr) {
            session->entity_note_stage(p_record, from);
            session->event_emit(
                EventPlane::SPAWNING,
                p_record->get_route(),
                Dictionary(),
                p_record->get_entity_id(),
                p_record->get_peer_id(),
                OK,
                Dictionary()
            );
        }
        p_wrapper->emit_signal(StringName("spawning"));
    }
}

void NetwMultiplayerCore::entity_request_control(
    Object *p_wrapper,
    Object *p_owner,
    Object *p_plane
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr || p_wrapper == nullptr) {
        return;
    }
    const Ref<MultiplayerAPI> api = owner->get_multiplayer();
    const bool connected = api.is_valid()
        && api->get_multiplayer_peer().is_valid();
    if (!connected) {
        p_wrapper->call(
            StringName("_handle_control_request"),
            api.is_valid() ? int64_t(api->get_unique_id()) : int64_t(0)
        );
        return;
    }
    if (p_plane != nullptr) {
        NETW_TRACE(sys::ENTITY, "asking the server for control");
        p_plane->call(StringName("request_control"), p_wrapper);
    }
}

void NetwMultiplayerCore::entity_broadcast_control(
    Object *p_wrapper,
    Object *p_plane,
    int64_t p_peer
) {
    if (p_wrapper == nullptr || p_plane == nullptr) {
        return;
    }
    p_plane->call(StringName("broadcast_control"), p_wrapper, p_peer);
}

void NetwMultiplayerCore::set_replication_plane(Object *p_plane) {
    replication = gd::instance_id(p_plane);
}

Object *NetwMultiplayerCore::replication_plane() const {
    return gd::instance_from_id(replication);
}

Node *NetwMultiplayerCore::entity_component_node(
    const RID &p_entity,
    int64_t p_comp
) const {
    const Ref<NetwEntity> wrapper = wrapper_of(p_entity);
    if (wrapper.is_null()) {
        return nullptr;
    }
    const Ref<RefCounted> components = wrapper->get_components();
    if (components.is_null()) {
        return nullptr;
    }
    Object *resolved = components->call(
        StringName("resolve_node"),
        wrapper->get_owner(),
        p_comp,
        String()
    );
    return Object::cast_to<Node>(resolved);
}

void NetwMultiplayerCore::set_sync_pipeline(Object *p_pipeline) {
    sync_pipeline_id = gd::instance_id(p_pipeline);
}

Object *NetwMultiplayerCore::sync_pipeline() const {
    return gd::instance_from_id(sync_pipeline_id);
}

Error NetwMultiplayerCore::sync_send_property(
    const RID &p_entity,
    int64_t p_comp,
    const StringName &p_property
) {
    Node *node = entity_component_node(p_entity, p_comp);
    NETW_ERR_COND_V(
        node == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::WIRE,
        "property %s addressed component %d of an entity that resolves to no node",
        String(p_property),
        int(p_comp)
    );
    NETW_ERR_COND_V(
        !gd::has_property(node, p_property),
        ERR_INVALID_DATA,
        sys::WIRE,
        "node %s declares no property %s",
        node->get_name(),
        String(p_property)
    );
    Object *pipeline = sync_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        ERR_UNCONFIGURED,
        sys::WIRE,
        "property %s was sent with no sync pipeline installed",
        String(p_property)
    );
    pipeline->call("send_property", node, p_property);
    return OK;
}

Error NetwMultiplayerCore::sync_send_signal(
    const RID &p_entity,
    int64_t p_comp,
    const StringName &p_signal,
    const Array &p_args
) {
    Node *node = entity_component_node(p_entity, p_comp);
    NETW_ERR_COND_V(
        node == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::WIRE,
        "signal %s addressed component %d of an entity that resolves to no node",
        String(p_signal),
        int(p_comp)
    );
    NETW_ERR_COND_V(
        !node->has_signal(p_signal),
        ERR_INVALID_DATA,
        sys::WIRE,
        "node %s declares no signal %s",
        node->get_name(),
        String(p_signal)
    );
    Object *pipeline = sync_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        ERR_UNCONFIGURED,
        sys::WIRE,
        "signal %s was sent with no sync pipeline installed",
        String(p_signal)
    );
    pipeline->call("send_signal", node, p_signal, p_args);
    return OK;
}

void NetwMultiplayerCore::entity_control_request(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = wrapper_of(p_entity);
    if (wrapper.is_null()) {
        return;
    }
    send_to(
        MultiplayerPeer::TARGET_PEER_SERVER,
        wrapper->get_route(),
        control_channels.control_request,
        PackedByteArray(),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayerCore::set_spawn_pipeline(Object *p_pipeline) {
    spawn_pipeline_id = gd::instance_id(p_pipeline);
}

Object *NetwMultiplayerCore::spawn_pipeline() const {
    return gd::instance_from_id(spawn_pipeline_id);
}

RID NetwMultiplayerCore::spawn_replicate(Object *p_node, Object *p_owner) {
    NETW_ZONE_NC("session spawn replicate", colors::LIVENESS);
    Object *pipeline = spawn_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "a node was armed for replication with no spawn pipeline installed"
    );
    const Ref<NetwEntity> wrapper = pipeline->call("replicate", p_node, p_owner);
    return wrapper.is_valid() ? wrapper->get_rid_handle() : RID();
}

RID NetwMultiplayerCore::spawn_function(
    const Callable &p_function,
    const Array &p_args,
    Object *p_owner
) {
    NETW_ZONE_NC("session spawn function", colors::LIVENESS);
    Object *pipeline = spawn_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "a spawn function ran with no spawn pipeline installed"
    );
    Object *node = pipeline->call("spawn", p_function, p_args, p_owner);
    return node != nullptr ? entity_of(node) : RID();
}

void NetwMultiplayerCore::spawn_register_constructor(
    const StringName &p_id,
    const Callable &p_function,
    const Array &p_arg_types,
    const Array &p_quantizers
) {
    Object *pipeline = spawn_pipeline();
    NETW_ERR_COND(
        pipeline == nullptr,
        sys::SPAWN,
        "constructor %s was registered with no spawn pipeline installed",
        String(p_id)
    );
    pipeline->call(
        "register_spawn_constructor",
        p_id,
        p_function,
        p_arg_types,
        p_quantizers
    );
}

RID NetwMultiplayerCore::spawn_registered(
    const StringName &p_id,
    const Array &p_args,
    Object *p_owner
) {
    NETW_ZONE_NC("session spawn registered", colors::LIVENESS);
    Object *pipeline = spawn_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "constructor %s ran with no spawn pipeline installed",
        String(p_id)
    );
    Object *node = pipeline->call("spawn_registered", p_id, p_args, p_owner);
    return node != nullptr ? entity_of(node) : RID();
}

RID NetwMultiplayerCore::spawn_adopt(Object *p_root) {
    NETW_ZONE_NC("session spawn adopt", colors::LIVENESS);
    Object *pipeline = spawn_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "a node was adopted with no spawn pipeline installed"
    );
    const Ref<NetwEntity> wrapper
        = pipeline->call("adopt_in_place", p_root);
    return wrapper.is_valid() ? wrapper->get_rid_handle() : RID();
}

TypedArray<Dictionary> NetwMultiplayerCore::spawn_state_of(const RID &p_entity
) {
    Object *pipeline = spawn_pipeline();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        TypedArray<Dictionary>(),
        sys::SPAWN,
        "spawn state was read with no spawn pipeline installed"
    );
    const Ref<NetwEntity> wrapper = wrapper_of(p_entity);
    Node *owner = wrapper.is_valid() ? wrapper->get_owner() : nullptr;
    if (owner == nullptr) {
        return TypedArray<Dictionary>();
    }
    return pipeline->call("collect_spawn_state", owner);
}

Ref<RefCounted> NetwMultiplayerCore::entity_derived_binding(
    Object *p_owner,
    int64_t p_record,
    int64_t p_route
) {
    Object *plane = replication_plane();
    if (p_owner == nullptr || plane == nullptr) {
        return Ref<RefCounted>();
    }
    Ref<RefCounted> declared = plane->call(
        StringName("derived_binding"),
        p_owner,
        p_record
    );
    if (declared.is_valid()) {
        return declared;
    }
    if (p_route <= 0) {
        return Ref<RefCounted>();
    }
    const Array group = entity_derived_group(p_route);
    for (int at = 0; at < group.size(); at++) {
        Ref<RefCounted> candidate = group[at];
        if (candidate.is_null()) {
            continue;
        }
        const Variant set = candidate->get(StringName("set"));
        Object *declaration = set;
        if (declaration != nullptr
            && int64_t(declaration->get(StringName("record"))) == p_record) {
            return candidate;
        }
    }
    return Ref<RefCounted>();
}

Array NetwMultiplayerCore::entity_derived_group(int64_t p_route) {
    Object *plane = replication_plane();
    if (plane == nullptr || p_route <= 0) {
        return Array();
    }
    return plane->call(StringName("derived_group"), p_route);
}

bool NetwMultiplayerCore::entity_governs_property(
    Object *p_owner,
    const NodePath &p_path,
    Object *p_exclude,
    int64_t p_route
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr || p_path.is_empty()) {
        return false;
    }
    const gd::NodeProperty target = gd::node_property(owner, p_path);
    if (!target.is_property()) {
        return false;
    }
    const TypedArray<MultiplayerSynchronizer> streams
        = NetwSynchronizers::of_node(owner);
    for (int at = 0; at < streams.size(); at++) {
        Object *stream = streams[at];
        if (stream == nullptr || stream == p_exclude) {
            continue;
        }
        const Array governed = NetwSynchronizers::governed_targets(
            stream,
            owner
        );
        for (int row = 0; row < governed.size(); row++) {
            const Array pair = governed[row];
            Object *held = pair[0];
            if (held == target.object && NodePath(pair[1]) == target.sub) {
                return true;
            }
        }
    }
    const Array group = entity_derived_group(p_route);
    for (int at = 0; at < group.size(); at++) {
        Ref<RefCounted> binding = group[at];
        if (binding.is_null()) {
            continue;
        }
        Object *node = binding->call(StringName("node"));
        if (node != target.object) {
            continue;
        }
        const Variant set = binding->get(StringName("set"));
        Object *declaration = set;
        if (declaration == nullptr) {
            continue;
        }
        const Array columns = declaration->get(StringName("columns"));
        for (int row = 0; row < columns.size(); row++) {
            Object *column = columns[row];
            if (column == nullptr) {
                continue;
            }
            const String key = String(column->get(StringName("key")));
            if (NodePath(String(":") + key) == target.sub) {
                return true;
            }
        }
    }
    return false;
}

Node *NetwMultiplayerCore::entity_instantiate_copy(
    Object *p_template,
    const Callable &p_configure
) {
    Node *copy = instantiate_scene_of(p_template);
    if (copy == nullptr) {
        return nullptr;
    }
    configure_copy(copy, p_configure);
    return copy;
}

Node *NetwMultiplayerCore::entity_instantiate_from(
    Object *p_template,
    const Callable &p_configure
) {
    Node *template_node = Object::cast_to<Node>(p_template);
    Node *copy = instantiate_scene_of(template_node);
    if (copy == nullptr) {
        return nullptr;
    }
    if (spawn_state.is_valid()) {
        const Array marked = spawn_state.call(template_node);
        NETW_TRACE(sys::ENTITY, "copying %d marked values", marked.size());
        for (int at = 0; at < marked.size(); at++) {
            const Dictionary row = marked[at];
            Object *carried = row["node"];
            Node *source = Object::cast_to<Node>(carried);
            const StringName property = row["prop"];
            if (source == nullptr) {
                continue;
            }
            Node *target = source == template_node
                ? copy
                : copy->get_node_or_null(template_node->get_path_to(source));
            if (target != nullptr) {
                target->set(property, source->get(property));
            }
        }
    }
    configure_copy(copy, p_configure);
    return copy;
}

namespace {

Node *seat_spawn(
    Node *p_copy,
    Node *p_owner,
    Object *p_parent,
    const StringName &p_id
) {
    if (p_copy == nullptr) {
        return nullptr;
    }
    if (!p_id.is_empty()) {
        NetwMultiplayerCore::wrapper_bind(p_copy, p_id, 0);
    } else {
        NetwMultiplayerCore::wrapper_ensure(p_copy);
    }
    Node *destination = Object::cast_to<Node>(p_parent);
    if (destination == nullptr) {
        destination = p_owner->get_parent();
    }
    NETW_ERR_COND_V(
        destination == nullptr,
        nullptr,
        sys::ENTITY,
        "a spawn needs a parent, and its template has none to borrow"
    );
    destination->add_child(p_copy);
    return p_copy;
}

}

Node *NetwMultiplayerCore::entity_spawn_under(
    Object *p_owner,
    Object *p_parent,
    const StringName &p_id
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    NETW_ERR_COND_V(
        owner == nullptr,
        nullptr,
        sys::ENTITY,
        "a spawn needs a template owner"
    );
    return seat_spawn(
        entity_instantiate_from(owner, Callable()),
        owner,
        p_parent,
        p_id
    );
}

Node *NetwMultiplayerCore::entity_spawn_copy_under(
    Object *p_owner,
    Object *p_parent,
    const StringName &p_id
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    NETW_ERR_COND_V(
        owner == nullptr,
        nullptr,
        sys::ENTITY,
        "a spawn needs a template owner"
    );
    return seat_spawn(
        entity_instantiate_copy(owner, Callable()),
        owner,
        p_parent,
        p_id
    );
}

Node *NetwMultiplayerCore::entity_instantiate_player(
    Object *p_owner,
    Object *p_participant
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr || p_participant == nullptr) {
        return nullptr;
    }
    const Variant join = p_participant->get(StringName("join"));
    if (join.get_type() == Variant::NIL) {
        return nullptr;
    }
    Node *copy = entity_instantiate_from(owner, Callable());
    if (copy == nullptr) {
        return nullptr;
    }
    wrapper_bind(
        copy,
        p_participant->get(StringName("username")),
        p_participant->get(StringName("peer_id"))
    );
    return copy;
}

Node *NetwMultiplayerCore::entity_spawn_player(
    Object *p_owner,
    Object *p_participant,
    Object *p_scene
) {
    Node *copy = entity_instantiate_player(p_owner, p_participant);
    if (copy == nullptr || p_scene == nullptr) {
        return copy;
    }
    p_scene->call(StringName("add_player"), wrapper_at(copy));
    return copy;
}

void NetwMultiplayerCore::entity_settle_reparented(
    Object *p_wrapper,
    Object *p_owner,
    const Ref<NetwReparentOpts> &p_opts
) {
    settle_schedule(
        Callable(this, StringName("entity_announce_reparented"))
            .bind(p_wrapper, p_owner, p_opts),
        StringName()
    );
}

Dictionary NetwMultiplayerCore::entity_describe(int64_t p_route) const {
    Dictionary out;
    const RID entity = liveness_core->rid_from_route(int(p_route));
    const Ref<NetwEntityRecord> *found
        = wrapper_records.getptr(entity.get_id());
    if (found == nullptr) {
        return out;
    }
    const Ref<NetwEntityRecord> &record = *found;
    out["route"] = p_route;
    out["entity_id"] = record->get_entity_id();
    out["peer_id"] = record->get_peer_id();
    out["stage"] = record->get_stage();
    out["controller"] = record->get_control()->resolve(record->get_peer_id());
    out["liveness"] = liveness_route_state(p_route);
    out["layers"] = interest_engine.memberships(entity.get_id());
    return out;
}

void NetwMultiplayerCore::entity_note_stage(
    const Ref<NetwEntityRecord> &p_record,
    int64_t p_from
) {
    if (p_record.is_null() || p_record->get_stage() == p_from) {
        return;
    }
    const int64_t route = p_record->get_route();
    if (!plane.wants(EventPlane::STAGE_TRANSITION, route)) {
        return;
    }
    Dictionary detail;
    detail["from"] = p_from;
    detail["to"] = p_record->get_stage();
    event_emit(
        EventPlane::STAGE_TRANSITION,
        route,
        detail,
        p_record->get_entity_id(),
        p_record->get_peer_id(),
        OK,
        Dictionary()
    );
}

void NetwMultiplayerCore::entity_announce_reparented(
    Object *p_wrapper,
    Object *p_owner,
    const Ref<NetwReparentOpts> &p_opts
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr || !owner->is_inside_tree() || p_wrapper == nullptr) {
        return;
    }
    const Ref<NetwEntityRecord> record = record_of_wrapper(p_wrapper);
    const int64_t route = record.is_valid() ? record->get_route() : 0;
    if (plane.wants(EventPlane::REPARENTED, route)) {
        Dictionary detail;
        detail["reason"] = p_opts.is_valid() ? p_opts->get_reason()
                                             : StringName();
        detail["moved"] = p_opts.is_valid()
            && p_opts->get_target_global_position().get_type()
                != Variant::NIL;
        event_emit(
            EventPlane::REPARENTED,
            route,
            detail,
            record.is_valid() ? record->get_entity_id() : StringName(),
            record.is_valid() ? record->get_peer_id() : 0,
            OK,
            Dictionary()
        );
    }
    p_wrapper->emit_signal(StringName("reparented"), p_opts);
}

void NetwMultiplayerCore::entity_linger(
    const Ref<NetwEntityRecord> &p_record,
    Object *p_owner,
    int64_t p_pumps
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (p_record.is_null() || owner == nullptr) {
        return;
    }
    p_record->deactivate(owner);
    NETW_TRACE(sys::ENTITY, "lingering for %d pumps", int(p_pumps));
    settle_schedule_after(
        Callable(this, StringName("entity_free_owner")).bind(owner),
        StringName(),
        int(p_pumps)
    );
}

bool NetwMultiplayerCore::scene_request_flooded(int peer, int64_t now_msec) {
    if (!scene_request_window.exceeded(peer, now_msec)) {
        return false;
    }
    count_verdict(int64_t(ERR_BUSY), 0);
    if (claim_verdict_warning(int64_t(ERR_BUSY), 0)) {
        NETW_WARN(sys::SCENE, "peer %d exceeded the scene request rate", peer);
    }
    return true;
}

Array NetwMultiplayerCore::scene_request_frame_row(
    const PackedByteArray &p_payload,
    int p_sender,
    int64_t p_now_msec
) {
    if (!is_server() || scene_request_flooded(p_sender, p_now_msec)) {
        return Array();
    }
    const Variant decoded = gd::bytes_to_var(p_payload);
    if (decoded.get_type() != Variant::ARRAY) {
        return Array();
    }
    Array row = decoded;
    if (row.size() != 4) {
        return Array();
    }
    const Variant args = row[3];
    if (args.get_type() != Variant::ARRAY) {
        row[3] = Array();
    }
    return row;
}

RID NetwMultiplayerCore::scene_released_seat(
    const PackedByteArray &p_payload,
    int p_sender
) {
    if (p_sender != 1 || participant_admitted_local().is_null()) {
        return RID();
    }
    const RID seat = participant_seat(get_unique_id());
    if (!seat.is_valid()) {
        return RID();
    }
    const StringName released = gd::bytes_to_var(p_payload);
    return scene_layer_id(seat) == released ? seat : RID();
}

int NetwMultiplayerCore::scene_capture_verdict(const String &p_path) {
    const bool has_local = participant_admitted_local().is_valid();
    const RID here = has_local ? participant_seat(get_unique_id()) : RID();
    return NetwSceneCore::native_entry_verdict(
        !p_path.is_empty(),
        is_server(),
        scene_core->get_request_reach() == NetwSceneCore::REACH_SESSION,
        scene_core->live_count() > 0,
        get_role() == ROLE_LISTEN_SERVER,
        has_local,
        scene_owns_its_world(here)
    );
}

int NetwMultiplayerCore::scene_destination_kind(const Variant &p_destination) {
    return NetwSceneCore::destination_kind(p_destination);
}

Ref<PackedScene> NetwMultiplayerCore::scene_packed_at(
    const String &p_reference
) {
    return NetwSceneCore::packed_at(p_reference);
}

Ref<Script> NetwMultiplayerCore::scene_packed_root_script(
    const Ref<PackedScene> &p_packed
) {
    return NetwSceneCore::packed_root_script(p_packed);
}

Ref<Script> NetwMultiplayerCore::scene_root_script_at(
    const String &p_reference
) {
    return NetwSceneCore::scene_root_script_at(p_reference);
}

String NetwMultiplayerCore::scene_resolve_requested_path(
    const String &p_reference
) {
    return NetwSceneCore::resolve_requested_path(p_reference);
}

int NetwMultiplayerCore::scene_move_verdict(
    bool p_mover_live,
    bool p_target_live,
    bool p_same_scene
) {
    return NetwSceneCore::move_verdict(
        p_mover_live,
        p_target_live,
        p_same_scene
    );
}

bool NetwMultiplayerCore::scene_native_change_strands(
    bool p_online,
    bool p_marked
) {
    return NetwSceneCore::native_change_strands(p_online, p_marked);
}

StringName NetwMultiplayerCore::scene_container_meta() {
    return StringName("_netw_scene_container");
}

Node *NetwMultiplayerCore::scene_build_container(
    bool p_hosting,
    bool p_own_world
) {
    Node *container = nullptr;
    if (p_hosting && p_own_world) {
        SubViewport *viewport = memnew(SubViewport);
        viewport->set_use_own_world_3d(true);
        viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
        container = viewport;
    } else {
        container = memnew(Node);
    }
    container->set_name("Scene");
    container->set_meta(scene_container_meta(), true);
    NETW_TRACE(
        sys::SCENE,
        "built a scene container hosting=%d own_world=%d",
        p_hosting,
        p_own_world
    );
    return container;
}

StringName NetwMultiplayerCore::scene_packed_stem(
    const Ref<PackedScene> &p_packed
) {
    if (p_packed.is_null()) {
        return StringName();
    }
    const Ref<SceneState> state = p_packed->get_state();
    if (state.is_null() || state->get_node_count() == 0) {
        return StringName();
    }
    return state->get_node_name(0);
}

Node *NetwMultiplayerCore::scene_container(const StringName &p_stem) const {
    return Object::cast_to<Node>(wrapper_owner(scene_named(p_stem)));
}

Node *NetwMultiplayerCore::scene_existing_destination(
    const Variant &p_destination
) {
    switch (NetwSceneCore::destination_kind(p_destination)) {
        case NetwSceneCore::DESTINATION_NONE:
            return nullptr;
        case NetwSceneCore::DESTINATION_NAME: {
            const StringName stem = p_destination;
            return scene_container(stem);
        }
        case NetwSceneCore::DESTINATION_NODE: {
            Object *object = p_destination;
            Node *node = Object::cast_to<Node>(object);
            if (node == nullptr || !scene_core->is_live(entity_of(node))) {
                return nullptr;
            }
            return node;
        }
        default:
            break;
    }
    Object *object = p_destination;
    const Ref<PackedScene> packed = Object::cast_to<PackedScene>(object);
    return scene_container(scene_packed_stem(packed));
}

void NetwMultiplayerCore::scene_install_level(
    Object *p_container,
    Object *p_level
) {
    Node *container = Object::cast_to<Node>(p_container);
    Node *level = Object::cast_to<Node>(p_level);
    if (container == nullptr || level == nullptr) {
        return;
    }
    const String mounted
        = String(level->get_name()) + String(container->get_name());
    container->set_name(mounted);
    container->add_child(level);
    level->set_owner(container);
}

Node *NetwMultiplayerCore::scene_spawn_node(
    const Variant &p_data,
    int p_isolation
) {
    NETW_ZONE_NC("session scene constructor", colors::SCENE);
    Node *level = nullptr;
    const Callable builder = scene_core->declared_level_spawn_function();
    if (builder.is_valid()) {
        level = Object::cast_to<Node>(builder.call(p_data));
        NETW_ERR_COND_V(
            level == nullptr,
            nullptr,
            sys::SCENE,
            "the declared level spawn function returned no node"
        );
    } else if (p_data.get_type() == Variant::STRING) {
        const Ref<PackedScene> packed
            = NetwSceneCore::packed_at(String(p_data));
        NETW_ERR_COND_V(
            packed.is_null(),
            nullptr,
            sys::SCENE,
            "no scene file at %s",
            String(p_data)
        );
        level = packed->instantiate();
    } else {
        NETW_ERROR(
            sys::SCENE,
            "a scene spawn carried neither a path nor a declared builder"
        );
        return nullptr;
    }

    Node *container = scene_build_container(
        is_server(),
        NetwSceneCore::isolation_owns_world(p_isolation)
    );
    scene_install_level(container, level);

    const Ref<NetwEntity> seat = NetwEntity::ensure(container);
    if (seat.is_valid()) {
        seat->set_declares_scene(true);
        seat->set_scene_label(level->get_name());
    }

    const Callable entered = scene_core->get_container_entered();
    if (entered.is_valid()) {
        connect_once(
            Signal(container, "tree_entered"),
            entered.bind(container)
        );
    }
    const Callable exited = scene_core->get_container_exited();
    if (exited.is_valid()) {
        connect_once(Signal(container, "tree_exited"), exited.bind(container));
    }
    return container;
}

StringName NetwMultiplayerCore::scene_constructor_id() {
    return StringName("__netw_scene__");
}

Node *NetwMultiplayerCore::scene_level_of(Object *p_container) {
    Node *container = Object::cast_to<Node>(p_container);
    if (container == nullptr || container->get_child_count() == 0) {
        return nullptr;
    }
    return container->get_child(0);
}

bool NetwMultiplayerCore::scene_owns_its_world(const RID &p_scene) const {
    const Ref<RefCounted> held = wrapper_of(p_scene);
    const NetwEntity *record = Object::cast_to<NetwEntity>(held.ptr());
    return record != nullptr
        && record->get_scene_isolation()
            == int64_t(NetwSceneCore::ISOLATION_OWN_WORLD);
}

bool NetwMultiplayerCore::scene_hosts_isolated_world() const {
    const Array live = scene_core->live_scenes();
    for (int at = 0; at < live.size(); ++at) {
        const RID scene = live[at];
        Node *mounted = Object::cast_to<Node>(wrapper_owner(scene));
        if (Object::cast_to<SubViewport>(mounted) != nullptr) {
            return true;
        }
    }
    return false;
}

Node *NetwMultiplayerCore::scene_containing(Object *p_node) {
    Node *walk = Object::cast_to<Node>(p_node);
    while (walk != nullptr) {
        const RID owned = entity_of(walk);
        if (scene_core->is_live(owned)
            && Object::cast_to<Node>(wrapper_owner(owned)) == walk) {
            return walk;
        }
        walk = walk->get_parent();
    }
    return nullptr;
}

void NetwMultiplayerCore::scene_settle_refresh() {
    if (scene_refresh.is_valid()) {
        settle_schedule(scene_refresh, StringName("scene-refresh-current"));
    }
}

Node *NetwMultiplayerCore::scene_spawn(const Variant &p_data, int p_isolation) {
    NETW_ZONE_NC("session scene spawn", colors::SCENE);
    scene_core->spawn_note(StringName());
    Array args;
    args.push_back(p_data);
    args.push_back(
        p_isolation < 0 ? scene_core->declared_isolation() : p_isolation
    );
    Node *mounted = Object::cast_to<Node>(
        wrapper_owner(spawn_registered(scene_constructor_id(), args, nullptr))
    );
    if (mounted == nullptr) {
        return nullptr;
    }
    Node *parent = scene_core->spawn_anchor(session_root());
    if (parent != nullptr) {
        parent->add_child(mounted);
    }
    return mounted;
}

Node *NetwMultiplayerCore::scene_spawn_declared(const StringName &p_stem) {
    Node *already = scene_container(p_stem);
    if (already != nullptr) {
        return already;
    }
    scene_core->spawn_note(p_stem);
    const String path = scene_core->declared_scene_path(p_stem);
    NETW_ERR_COND_V(
        path.is_empty(),
        nullptr,
        sys::SCENE,
        "cannot spawn scene '%s': not declared",
        String(p_stem)
    );
    return scene_spawn(path, -1);
}

void NetwMultiplayerCore::scene_spawn_initial() {
    const Array stems = scene_core->declared_initial_stems();
    for (int at = 0; at < stems.size(); ++at) {
        const StringName stem = stems[at];
        if (scene_container(stem) != nullptr) {
            continue;
        }
        const String path = scene_core->declared_scene_path(stem);
        if (!path.is_empty()) {
            scene_spawn(path, -1);
        }
    }
}

Node *NetwMultiplayerCore::scene_activate(const Variant &p_destination) {
    switch (NetwSceneCore::destination_kind(p_destination)) {
        case NetwSceneCore::DESTINATION_PACKED_UNPATHED:
            NETW_ERR_V(
                nullptr,
                sys::SCENE,
                "cannot activate an in-memory PackedScene"
            );
        case NetwSceneCore::DESTINATION_PACKED: {
            Object *object = p_destination;
            const Ref<PackedScene> packed
                = Object::cast_to<PackedScene>(object);
            Node *active = scene_container(scene_packed_stem(packed));
            if (active != nullptr) {
                Node *level = scene_level_of(active);
                if (level != nullptr) {
                    level->set_process_mode(Node::PROCESS_MODE_INHERIT);
                }
                return active;
            }
            return scene_spawn(packed->get_path(), -1);
        }
        case NetwSceneCore::DESTINATION_NAME:
            return scene_activate_named(StringName(p_destination));
        default:
            break;
    }
    NETW_ERR_V(
        nullptr,
        sys::SCENE,
        "scene activation expects a scene name or PackedScene"
    );
}

Node *NetwMultiplayerCore::scene_activate_named(const StringName &p_stem) {
    if (scene_container(p_stem) == nullptr) {
        if (scene_core->declared_level_spawn_function().is_valid()) {
            scene_spawn(scene_core->declared_spawn_data(p_stem, p_stem), -1);
        } else {
            scene_spawn_declared(p_stem);
        }
    }
    Node *active = scene_container(p_stem);
    NETW_ERR_COND_V(
        active == nullptr,
        nullptr,
        sys::SCENE,
        "failed to activate scene '%s'",
        String(p_stem)
    );
    Node *level = scene_level_of(active);
    if (level != nullptr) {
        level->set_process_mode(Node::PROCESS_MODE_INHERIT);
    }
    return active;
}

bool NetwMultiplayerCore::scene_freeze(const StringName &p_stem) {
    Node *level = scene_level_of(scene_container(p_stem));
    if (level == nullptr) {
        return false;
    }
    level->set_process_mode(Node::PROCESS_MODE_DISABLED);
    return true;
}

bool NetwMultiplayerCore::scene_destroy(const StringName &p_stem) {
    Node *active = scene_container(p_stem);
    if (active == nullptr) {
        return false;
    }
    Node *parent = active->get_parent();
    if (parent != nullptr) {
        parent->remove_child(active);
    }
    active->queue_free();
    return true;
}

bool NetwMultiplayerCore::scene_retire_named(
    const StringName &p_stem,
    int p_drain_pumps
) {
    Node *active = scene_container(p_stem);
    if (active == nullptr) {
        return false;
    }
    scene_core->scene_retire(
        entity_of(active),
        p_drain_pumps > 0 ? p_drain_pumps : 0
    );
    scene_settle_refresh();
    return true;
}

void NetwMultiplayerCore::scene_pump_retired() {
    const Array retired = scene_core->pump_retired();
    for (int at = 0; at < retired.size(); ++at) {
        const RID scene = retired[at];
        Node *mounted = Object::cast_to<Node>(wrapper_owner(scene));
        if (mounted != nullptr) {
            mounted->queue_free();
        }
    }
}

void NetwMultiplayerCore::scene_forget(Object *p_container) {
    scene_core->scene_exit(entity_of(p_container));
    scene_settle_refresh();
}

NodePath NetwMultiplayerCore::relative_path(Object *p_source, Object *p_target) {
    Node *source = Object::cast_to<Node>(p_source);
    Node *target = Object::cast_to<Node>(p_target);
    if (source == nullptr || target == nullptr) {
        return NodePath();
    }
    return source->get_path_to(target);
}

NodePath NetwMultiplayerCore::property_path(
    Object *p_source,
    const StringName &p_property,
    Object *p_base
) {
    Node *base = Object::cast_to<Node>(p_base);
    if (base == nullptr) {
        return NodePath();
    }
    const NodePath relative = relative_path(base, p_source);
    if (relative.is_empty()) {
        return NodePath();
    }
    return NodePath(String(relative) + ":" + String(p_property));
}

Ref<RefCounted> NetwMultiplayerCore::wrapper_of(const RID &p_entity) const {
    const Ref<RefCounted> *found = live_wrappers.getptr(p_entity.get_id());
    return found ? *found : Ref<RefCounted>();
}

Ref<RefCounted> NetwMultiplayerCore::wrapper_for_route(int64_t p_route) const {
    return wrapper_of(liveness_core->rid_from_route(int(p_route)));
}

Ref<RefCounted> NetwMultiplayerCore::wrapper_for_id(int64_t p_id) const {
    const Ref<RefCounted> *found = live_wrappers.getptr(p_id);
    if (found == nullptr) {
        found = retired_wrappers.getptr(p_id);
    }
    return found ? *found : Ref<RefCounted>();
}

TypedArray<Object> NetwMultiplayerCore::wrapper_live() const {
    TypedArray<Object> out;
    const PackedInt32Array routes = liveness_core->live_routes();
    for (int at = 0; at < routes.size(); at++) {
        const Ref<RefCounted> wrapper = wrapper_for_route(routes[at]);
        if (wrapper.is_valid()) {
            out.push_back(wrapper);
        }
    }
    return out;
}

void NetwMultiplayerCore::wrapper_sweep_retired() {
    retired_wrappers.clear();
}

void NetwMultiplayerCore::wrapper_clear() {
    live_wrappers.clear();
    retired_wrappers.clear();
}

bool NetwMultiplayerCore::liveness_bind(
    const RID &p_entity,
    int64_t p_route,
    const Ref<RefCounted> &p_wrapper,
    const Ref<NetwEntityRecord> &p_record,
    Object *p_owner
) {
    if (!liveness_core->adopt(p_entity)) {
        return false;
    }
    if (!liveness_core->bind_route(p_entity, int(p_route))) {
        return false;
    }
    wrapper_adopt(p_entity, p_wrapper, p_owner);
    if (p_record.is_valid()) {
        wrapper_records[p_entity.get_id()] = p_record;
    }
    return true;
}

Error NetwMultiplayerCore::liveness_adopt_route(
    int64_t p_route,
    const Ref<RefCounted> &p_wrapper,
    const Ref<NetwEntityRecord> &p_record,
    Object *p_owner
) {
    if (p_route <= 0 || p_wrapper.is_null() || p_record.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    const RID held = liveness_core->rid_from_route(int(p_route));
    if (!p_record->get_handle().is_valid() && !held.is_valid()) {
        p_record->adopt_handle(liveness_core->entity_create());
    }
    if (held.is_valid()) {
        const Ref<RefCounted> bound = wrapper_of(held);
        if (bound == p_wrapper
            && liveness_core->state_of(held)
                != NetwLivenessCore::STATE_DEAD) {
            return ERR_ALREADY_EXISTS;
        }
        if (held != p_record->get_handle()) {
            if (bound.is_valid()
                || liveness_core->entity_is_valid(p_record->get_handle())) {
                NETW_TRACE(
                    sys::LIVENESS,
                    "route %d already names another entity",
                    int(p_route)
                );
                return ERR_UNAVAILABLE;
            }
            p_record->adopt_handle(held);
        }
    }
    if (!liveness_bind(
            p_record->get_handle(),
            p_route,
            p_wrapper,
            p_record,
            p_owner
        )) {
        return ERR_UNAVAILABLE;
    }
    p_record->set_route(p_route);
    return OK;
}

Ref<RefCounted> NetwMultiplayerCore::get_local_player() const {
    return local_player;
}

void NetwMultiplayerCore::liveness_publish_live(int64_t p_route) {
    const Ref<RefCounted> wrapper = wrapper_for_route(p_route);
    const Ref<NetwEntityRecord> record = record_of_wrapper(wrapper.ptr());
    if (record.is_valid()) {
        event_emit(
            EventPlane::SPAWNED,
            p_route,
            Dictionary(),
            record->get_entity_id(),
            record->get_peer_id(),
            OK,
            Dictionary()
        );
    }
    emit_signal(SIG_ENTITY_LIVE, p_route, wrapper);
}

void NetwMultiplayerCore::liveness_settle_local_player(int64_t p_route) {
    if (!has_multiplayer_peer()) {
        return;
    }
    const RID entity = liveness_core->rid_from_route(int(p_route));
    const int64_t id = entity.get_id();
    const Ref<NetwEntityRecord> *record = wrapper_records.getptr(id);
    if (record == nullptr || (*record)->get_peer_id() == 0
        || (*record)->get_peer_id() != get_unique_id()) {
        return;
    }
    set_local_player(wrapper_of(entity), id);
}

void NetwMultiplayerCore::set_local_player(
    const Ref<RefCounted> &p_player,
    int64_t p_id
) {
    if (local_player == p_player) {
        return;
    }
    local_player = p_player;
    local_player_id = p_id;
    emit_signal(SIG_LOCAL_PLAYER_CHANGED, p_player);
}

bool NetwMultiplayerCore::liveness_linger(const RID &p_entity) {
    if (!liveness_core->set_state(
            p_entity,
            NetwLivenessCore::STATE_LINGERING
        )) {
        return false;
    }
    emit_signal(
        SIG_ENTITY_LINGERING,
        int64_t(liveness_core->route_of(p_entity)),
        wrapper_of(p_entity)
    );
    return true;
}

bool NetwMultiplayerCore::liveness_retire(int64_t p_route) {
    const RID entity = liveness_core->rid_from_route(int(p_route));
    liveness_core->set_state(entity, NetwLivenessCore::STATE_DEAD);
    liveness_core->abandon_live(int(p_route));
    const int64_t id = entity.get_id();
    const Ref<RefCounted> *found = live_wrappers.getptr(id);
    if (found != nullptr) {
        retired_wrappers[id] = *found;
        live_wrappers.erase(id);
    }
    event_emit(
        EventPlane::DESPAWNED,
        p_route,
        Dictionary(),
        StringName(),
        0,
        OK,
        Dictionary()
    );
    emit_signal(SIG_ENTITY_DEAD, p_route);
    if (local_player_id == id) {
        set_local_player(Ref<RefCounted>(), 0);
    }
    wrapper_records.erase(id);
    return true;
}

Ref<NetwEntityRecord> NetwMultiplayerCore::record_of_wrapper(Object *p_wrapper
) const {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr) {
        return Ref<NetwEntityRecord>();
    }
    return entity->get_record();
}

Object *NetwMultiplayerCore::owner_of_wrapper(Object *p_wrapper) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr) {
        return nullptr;
    }
    return entity->get_owner();
}

Callable NetwMultiplayerCore::owner_exit_hook(Object *p_wrapper) {
    Array bound;
    bound.push_back(p_wrapper);
    return Callable(this, StringName("liveness_owner_exiting")).bindv(bound);
}

Callable NetwMultiplayerCore::despawning_hook(Object *p_wrapper) {
    Array bound;
    bound.push_back(p_wrapper);
    return Callable(this, StringName("liveness_owner_despawning")).bindv(bound);
}

int64_t NetwMultiplayerCore::liveness_reserve_route() {
    return liveness_core->reserve_route();
}

int64_t NetwMultiplayerCore::liveness_allocate_route(Object *p_wrapper) {
    const int64_t standing = liveness_route_of(p_wrapper);
    if (standing > 0) {
        return standing;
    }
    const int64_t route = liveness_core->reserve_route();
    return liveness_bind_route(route, p_wrapper) ? route : 0;
}

bool NetwMultiplayerCore::liveness_bind_route(
    int64_t p_route,
    Object *p_wrapper
) {
    const Ref<NetwEntityRecord> record = record_of_wrapper(p_wrapper);
    if (record.is_null()) {
        return false;
    }
    const Ref<RefCounted> wrapper(p_wrapper);
    Object *owner = owner_of_wrapper(p_wrapper);
    const Error verdict
        = liveness_adopt_route(p_route, wrapper, record, owner);
    if (verdict == ERR_ALREADY_EXISTS) {
        return true;
    }
    if (verdict != OK) {
        return false;
    }

    Node *node = Object::cast_to<Node>(owner);
    if (node != nullptr) {
        const Callable exiting = owner_exit_hook(p_wrapper);
        if (!node->is_connected(StringName("tree_exiting"), exiting)) {
            node->connect(StringName("tree_exiting"), exiting);
        }
        const Callable despawning = despawning_hook(p_wrapper);
        if (!p_wrapper->is_connected(StringName("despawning"), despawning)) {
            p_wrapper->connect(StringName("despawning"), despawning);
        }
    }

    liveness_publish_live(p_route);
    liveness_core->flush_live(int(p_route));
    return true;
}

void NetwMultiplayerCore::liveness_bind_routes_data(
    const PackedInt64Array &p_routes
) {
    liveness_core->bind_routes_data(p_routes);
}

void NetwMultiplayerCore::liveness_tombstone_routes_data(
    const PackedInt64Array &p_routes
) {
    liveness_core->tombstone_routes_data(p_routes);
}

int64_t NetwMultiplayerCore::liveness_route_of(Object *p_wrapper) const {
    const Ref<NetwEntityRecord> record = record_of_wrapper(p_wrapper);
    if (record.is_null()) {
        return 0;
    }
    return liveness_core->route_of(record->get_handle());
}

int64_t NetwMultiplayerCore::liveness_state_of(Object *p_wrapper) const {
    const Ref<NetwEntityRecord> record = record_of_wrapper(p_wrapper);
    if (record.is_null()) {
        return int64_t(NetwLivenessCore::STATE_UNKNOWN);
    }
    return int64_t(liveness_core->state_of(record->get_handle()));
}

int64_t NetwMultiplayerCore::liveness_route_state(int64_t p_route) const {
    return int64_t(liveness_core->route_state(int(p_route)));
}

RID NetwMultiplayerCore::liveness_adopt(Object *p_wrapper) {
    const Ref<NetwEntityRecord> record = record_of_wrapper(p_wrapper);
    if (record.is_null() || !record->get_handle().is_valid()) {
        return RID();
    }
    const RID handle = record->get_handle();
    wrapper_adopt(handle, Ref<RefCounted>(p_wrapper), owner_of_wrapper(p_wrapper)
    );
    return handle;
}

StringName NetwMultiplayerCore::scene_stem(const RID &p_scene) const {
    const NetwEntity *entity
        = Object::cast_to<NetwEntity>(wrapper_of(p_scene).ptr());
    if (entity != nullptr && !String(entity->get_scene_label()).is_empty()) {
        return entity->get_scene_label();
    }
    const Node *container = Object::cast_to<Node>(wrapper_owner(p_scene));
    if (container == nullptr || container->get_child_count() == 0) {
        return StringName();
    }
    return StringName(container->get_child(0)->get_name());
}

StringName NetwMultiplayerCore::scene_layer_id(const RID &p_scene) const {
    const Node *container = Object::cast_to<Node>(wrapper_owner(p_scene));
    if (container == nullptr || container->get_child_count() == 0) {
        return StringName();
    }
    const String stem = String(container->get_child(0)->get_name());
    int64_t route = liveness_core->route_of(p_scene);
    if (route <= 0) {
        const NetwEntity *wrapper
            = Object::cast_to<NetwEntity>(wrapper_of(p_scene).ptr());
        route = wrapper != nullptr ? wrapper->get_route() : 0;
    }
    if (route <= 0) {
        return StringName("scene:" + stem);
    }
    return StringName("scene:" + stem + "#" + String::num_int64(route));
}

Ref<RefCounted> NetwMultiplayerCore::scene_layer_view(const RID &p_scene
) const {
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty()) {
        return Ref<RefCounted>();
    }
    return layer_view(layer_named(layer));
}

PackedInt32Array NetwMultiplayerCore::scene_peers(const RID &p_scene) const {
    PackedInt32Array out;
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty()) {
        return out;
    }
    const PackedInt64Array viewers = interest_engine.layer_viewers(layer);
    out.resize(int(viewers.size()));
    for (int at = 0; at < int(viewers.size()); ++at) {
        out.set(at, int32_t(viewers[at]));
    }
    return out;
}

void NetwMultiplayerCore::set_scene_participant_edge(const Callable &p_edge) {
    scene_participant_edge = p_edge;
}

Error NetwMultiplayerCore::scene_admit(const RID &p_scene, int64_t p_peer) {
    NETW_ZONE_NC("NetwMultiplayerCore scene_admit", colors::SCENE);
    NETW_ERR_COND_V(
        !is_server(),
        ERR_UNAUTHORIZED,
        sys::SCENE,
        "scene_admit is server-only, and peer %d asked",
        int(p_peer)
    );
    if (p_peer == 0) {
        return ERR_INVALID_PARAMETER;
    }
    if (scene_entity_node(p_scene) == nullptr) {
        NETW_TRACE(
            sys::SCENE,
            "scene %d names no live node, so peer %d is not admitted",
            int(p_scene.get_id()),
            int(p_peer)
        );
        return ERR_DOES_NOT_EXIST;
    }
    scene_admit_peer(p_scene, p_peer);
    if (!scene_admits(p_scene, p_peer)) {
        NETW_WARN_COND(
            true,
            sys::SCENE,
            "peer %d was not admitted to scene %d",
            int(p_peer),
            int(p_scene.get_id())
        );
        return ERR_UNAVAILABLE;
    }
    return OK;
}

bool NetwMultiplayerCore::scene_release(const RID &p_scene, int64_t p_peer) {
    NETW_ZONE_NC("NetwMultiplayerCore scene_release", colors::SCENE);
    NETW_ERR_COND_V(
        !is_server(),
        false,
        sys::SCENE,
        "scene_release is server-only, and peer %d asked",
        int(p_peer)
    );
    if (!is_server() || scene_entity_node(p_scene) == nullptr) {
        return false;
    }
    scene_notify_released(p_scene, p_peer);
    scene_release_peer(p_scene, p_peer);
    return true;
}

bool NetwMultiplayerCore::scene_admits(const RID &p_scene, int64_t p_peer)
    const {
    return interest_engine.layer_has_viewer(scene_layer_id(p_scene), p_peer);
}

bool NetwMultiplayerCore::scene_declared(const RID &p_entity) const {
    const Ref<NetwEntityRecord> *record = wrapper_records.getptr(
        p_entity.get_id()
    );
    return record != nullptr && (*record)->get_declares_scene();
}

Node *NetwMultiplayerCore::scene_entity_node(const RID &p_entity) const {
    return Object::cast_to<Node>(wrapper_owner(p_entity));
}

void NetwMultiplayerCore::collect_scene_entities(
    Node *p_node,
    TypedArray<RID> &r_out
) const {
    if (p_node == nullptr) {
        return;
    }
    const int64_t children = p_node->get_child_count();
    for (int64_t at = 0; at < children; ++at) {
        Node *child = p_node->get_child(int32_t(at));
        const Ref<RefCounted> wrapper = wrapper_at(child);
        const RID held = handle_of_wrapper(wrapper.ptr());
        const bool owns_record = held.is_valid()
            && Object::cast_to<Node>(wrapper_owner(held)) == child;
        if (owns_record) {
            r_out.push_back(held);
            if (scene_declared(held)) {
                continue;
            }
        }
        collect_scene_entities(child, r_out);
    }
}

TypedArray<RID> NetwMultiplayerCore::scene_entities_under(const RID &p_scene)
    const {
    TypedArray<RID> out;
    collect_scene_entities(scene_entity_node(p_scene), out);
    return out;
}

bool NetwMultiplayerCore::scene_admit_peer(const RID &p_scene, int64_t p_peer) {
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty() || p_peer == 0) {
        return false;
    }
    if (!interest_engine.layer_add_viewer(layer, p_peer)) {
        return false;
    }
    interest_request_flush();
    if (scene_participant_edge.is_valid()) {
        scene_participant_edge.call(p_scene, p_peer, true);
    }
    return true;
}

bool NetwMultiplayerCore::scene_release_peer(
    const RID &p_scene,
    int64_t p_peer
) {
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty() || p_peer == 0) {
        return false;
    }
    if (!interest_engine.layer_remove_viewer(layer, p_peer)) {
        return false;
    }
    interest_request_flush();
    if (scene_participant_edge.is_valid()) {
        scene_participant_edge.call(p_scene, p_peer, false);
    }
    return true;
}

bool NetwMultiplayerCore::scene_notify_released(
    const RID &p_scene,
    int64_t p_peer
) {
    if (participant_seat(p_peer) != p_scene) {
        return false;
    }
    const PackedByteArray payload = gd::var_to_bytes(scene_layer_id(p_scene));
    if (p_peer == int64_t(get_unique_id())) {
        const Callable local
            = channel_book->protocol_handler_of(scene_released_channel);
        if (!local.is_valid()) {
            return false;
        }
        local.call(payload, 1);
        return true;
    }
    return send_to(
               p_peer,
               0,
               scene_released_channel,
               payload,
               true,
               0,
               String(),
               false
           )
        == OK;
}

bool NetwMultiplayerCore::scene_release_departed(
    const RID &p_scene,
    const RID &p_subject,
    bool p_mover_live,
    int64_t p_peer
) {
    if (!is_server()) {
        return false;
    }
    if (p_mover_live && entity_scene_of(p_subject) == p_scene) {
        return false;
    }
    return scene_release_peer(p_scene, p_peer);
}

RID NetwMultiplayerCore::scene_seat_sync(int64_t p_peer) {
    if (!participant_has(p_peer)) {
        return RID();
    }
    const Array live = scene_core->live_scenes();
    for (int at = 0; at < live.size(); ++at) {
        const RID scene = live[at];
        if (scene_core->scene_named(scene_core->stem_of(scene)) != scene) {
            continue;
        }
        const StringName layer = scene_layer_id(scene);
        if (layer.is_empty()
            || !interest_engine.roster_has(layer, int64_t(scene.get_id()))) {
            continue;
        }
        participant_seat_move(p_peer, scene);
        return scene;
    }
    return RID();
}

StringName NetwMultiplayerCore::scene_seat_clear_key(
    int64_t p_peer,
    const RID &p_scene
) {
    return StringName(
        "scene-clear-membership?" + String::num_int64(p_peer) + "?"
        + String::num_int64(int64_t(p_scene.get_id()))
    );
}

void NetwMultiplayerCore::scene_seat_clear_deferred(
    int64_t p_peer,
    const RID &p_scene
) {
    settle_schedule(
        Callable(this, StringName("participant_seat_clear"))
            .bind(p_peer, p_scene),
        scene_seat_clear_key(p_peer, p_scene)
    );
}

StringName NetwMultiplayerCore::scene_seat_release_key(
    int64_t p_peer,
    const RID &p_scene
) {
    return StringName(
        "scene-seat-release?" + String::num_int64(p_peer) + "?"
        + String::num_int64(int64_t(p_scene.get_id()))
    );
}

void NetwMultiplayerCore::set_scene_carry_move(const Callable &p_carry) {
    scene_carry_move = p_carry;
}

Ref<NetwPromise> NetwMultiplayerCore::scene_move_entity(
    const RID &p_entity,
    const RID &p_destination,
    const Variant &p_opts
) {
    NETW_ZONE_NC("NetwMultiplayerCore scene_move_entity", colors::SCENE);
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<RefCounted> mover = wrapper_of(p_entity);
    Object *target = wrapper_owner(p_destination);
    if (mover.is_null() || wrapper_owner(p_entity) == nullptr
        || target == nullptr || !scene_carry_move.is_valid()) {
        NETW_WARN_COND(
            true,
            sys::SCENE,
            "scene_move: entity %d or destination %d is unreachable",
            int(p_entity.get_id()),
            int(p_destination.get_id())
        );
        promise->reject(
            ERR_UNAVAILABLE,
            "scene_move: entity or destination is unreachable"
        );
        return promise;
    }
    const Ref<NetwReparentOpts> opts = p_opts;
    if (opts.is_valid() && opts->get_reason() == StringName()) {
        opts->set_reason(scene_move_reason());
    }
    NETW_TRACE(
        sys::SCENE,
        "move %d into %d as '%s'",
        int(p_entity.get_id()),
        int(p_destination.get_id()),
        opts.is_valid() ? String(opts->get_reason()).utf8().get_data() : ""
    );
    scene_carry_move.call(mover, target, p_opts, promise);
    return promise;
}

StringName NetwMultiplayerCore::scene_move_reason() {
    return StringName("scene_move");
}

Ref<NetwGroupPromise> NetwMultiplayerCore::scene_move_participants(
    const RID &p_scene,
    const PackedInt32Array &p_peers
) {
    const Ref<NetwGroupPromise> batch = NetwGroupPromise::create(p_peers);
    for (int at = 0; at < p_peers.size(); at++) {
        participant_move_seat(int64_t(p_peers[at]), p_scene);
    }
    Array bound;
    bound.push_back(batch);
    bound.push_back(p_peers);
    settle_schedule(
        Callable(this, StringName("scene_report_moved")).bindv(bound),
        StringName()
    );
    return batch;
}

void NetwMultiplayerCore::scene_report_moved(
    const Ref<NetwGroupPromise> &p_batch,
    const PackedInt32Array &p_peers
) {
    if (p_batch.is_null()) {
        return;
    }
    for (int at = 0; at < p_peers.size(); at++) {
        const int64_t peer = int64_t(p_peers[at]);
        p_batch->resolve_peer(peer, participant_of(peer));
    }
    if (!p_batch->get_is_completed()) {
        p_batch->resolve_all();
    }
}

RID NetwMultiplayerCore::entity_of(Object *p_node) {
    return liveness_adopt(wrapper_at(p_node).ptr());
}

Object *NetwMultiplayerCore::liveness_node_of(int64_t p_route) const {
    return wrapper_owner(liveness_core->rid_from_route(int(p_route)));
}

TypedArray<Object> NetwMultiplayerCore::liveness_live_entities() const {
    return wrapper_live();
}

void NetwMultiplayerCore::liveness_when_live(
    int64_t p_route,
    const Callable &p_callback,
    int64_t p_deadline,
    bool p_on_clock,
    const Callable &p_on_timeout
) {
    liveness_core->when_live(
        int(p_route),
        p_callback,
        int(p_deadline),
        p_on_clock,
        p_on_timeout
    );
}

int64_t NetwMultiplayerCore::liveness_pending_live_count() const {
    return liveness_core->pending_live_count();
}

void NetwMultiplayerCore::liveness_poll(int64_t p_clock_tick) {
    const PackedInt32Array expired = liveness_core->poll(int(p_clock_tick));
    for (int at = 0; at < expired.size(); at++) {
        NETW_WARN(
            sys::LIVENESS,
            "when_live timed out for route %d",
            expired[at]
        );
    }
}

void NetwMultiplayerCore::liveness_clear_session() {
    liveness_core->clear();
    wrapper_clear();
}

void NetwMultiplayerCore::liveness_owner_exiting(Object *p_wrapper) {
    if (p_wrapper == nullptr) {
        return;
    }
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr || entity->get_reparenting().is_valid()) {
        return;
    }
    const int64_t route = liveness_route_of(p_wrapper);
    if (route <= 0) {
        return;
    }
    Object *plane = replication_plane();
    if (plane != nullptr
        && bool(plane->call(StringName("owns_spawned_route"), route))) {
        Array bound;
        bound.push_back(route);
        bound.push_back(p_wrapper);
        settle_schedule(
            Callable(this, StringName("liveness_resolve_tracked_exit"))
                .bindv(bound),
            StringName(vformat("liveness-exit?%d", route))
        );
        return;
    }
    liveness_transition_dead(route);
}

void NetwMultiplayerCore::liveness_resolve_tracked_exit(
    int64_t p_route,
    Object *p_wrapper
) {
    if (liveness_core->route_state(int(p_route))
        == NetwLivenessCore::STATE_DEAD) {
        return;
    }
    Node *owner = Object::cast_to<Node>(owner_of_wrapper(p_wrapper));
    if (owner != nullptr && owner->is_inside_tree()) {
        return;
    }
    liveness_transition_dead(p_route);
}

void NetwMultiplayerCore::liveness_owner_despawning(
    const StringName &p_reason,
    Object *p_wrapper
) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr) {
        return;
    }
    const Ref<NetwEntityRecord> record = record_of_wrapper(p_wrapper);
    const int64_t route = liveness_route_of(p_wrapper);
    if (record.is_null() || route <= 0) {
        return;
    }
    if (plane.wants(EventPlane::DESPAWNING, route)) {
        Dictionary detail;
        detail["reason"] = p_reason;
        Dictionary model;
        model["entity_id"] = record->get_entity_id();
        model["peer_id"] = record->get_peer_id();
        event_emit(
            EventPlane::DESPAWNING,
            route,
            detail,
            record->get_entity_id(),
            record->get_peer_id(),
            OK,
            model
        );
    }
    const Ref<NetwDespawnOpts> opts = entity->get_active_despawn_opts();
    if (opts.is_null() || !opts->get_linger()) {
        return;
    }
    liveness_linger(record->get_handle());
}

void NetwMultiplayerCore::liveness_transition_dead(int64_t p_route) {
    const Ref<RefCounted> wrapper = wrapper_for_route(p_route);
    if (wrapper.is_valid()) {
        Object *held = wrapper.ptr();
        Node *owner = Object::cast_to<Node>(owner_of_wrapper(held));
        const Callable exiting = owner_exit_hook(held);
        if (owner != nullptr
            && owner->is_connected(StringName("tree_exiting"), exiting)) {
            owner->disconnect(StringName("tree_exiting"), exiting);
        }
        const Callable despawning = despawning_hook(held);
        if (held->is_connected(StringName("despawning"), despawning)) {
            held->disconnect(StringName("despawning"), despawning);
        }
    }
    liveness_retire(p_route);
}

int NetwMultiplayerCore::service_row(Object *p_type) const {
    const ObjectID wanted = gd::instance_id(p_type);
    for (uint32_t at = 0; at < services.size(); at++) {
        if (services[at].type == wanted) {
            return int(at);
        }
    }
    return -1;
}

void NetwMultiplayerCore::service_register(
    Object *p_type,
    Object *p_service
) {
    Object *type = p_type ? p_type : script_of(p_service);
    if (type == nullptr || p_service == nullptr) {
        return;
    }
    const int at = service_row(type);
    if (at >= 0) {
        Object *held = gd::instance_from_id(services[at].service);
        if (held == p_service) {
            return;
        }
        if (held != nullptr) {
            NETW_WARN(
                sys::SESSION,
                "Service %s already registered, overwriting.",
                type->to_string()
            );
        }
        services[at].service = gd::instance_id(p_service);
        emit_signal(SIG_SERVICE_REGISTERED, p_service);
        return;
    }
    ServiceRow row;
    row.type = gd::instance_id(type);
    row.service = gd::instance_id(p_service);
    services.push_back(row);
    emit_signal(SIG_SERVICE_REGISTERED, p_service);
}

void NetwMultiplayerCore::service_unregister(
    Object *p_type,
    Object *p_service
) {
    Object *type = p_type ? p_type : script_of(p_service);
    const int at = type ? service_row(type) : -1;
    if (at < 0 || gd::instance_from_id(services[at].service) != p_service) {
        return;
    }
    services.remove_at(at);
    emit_signal(SIG_SERVICE_UNREGISTERED, p_service);
}

Variant NetwMultiplayerCore::service_held(Object *p_type) const {
    return gd::held(service_of(p_type));
}

Object *NetwMultiplayerCore::service_of(Object *p_type) const {
    const int at = service_row(p_type);
    return at < 0 ? nullptr : gd::instance_from_id(services[at].service);
}

void NetwMultiplayerCore::service_clear() {
    services.clear();
}

TypedArray<Object> NetwMultiplayerCore::service_all(Object *p_base) const {
    TypedArray<Object> out;
    for (uint32_t at = 0; at < services.size(); at++) {
        Ref<Script> script
            = Object::cast_to<Script>(gd::instance_from_id(services[at].type));
        while (script.is_valid()) {
            if (script.ptr() == p_base) {
                Object *service = gd::instance_from_id(services[at].service);
                if (service != nullptr) {
                    out.push_back(service);
                }
                break;
            }
            script = script->get_base_script();
        }
    }
    return out;
}

bool NetwMultiplayerCore::session_publish_control(
    int64_t p_channel,
    int64_t p_sender,
    const PackedByteArray &p_payload
) {
    const bool from_server = p_sender == 1;
    if (p_channel == control_channels.pause) {
        if (from_server) {
            emit_signal(SIG_TREE_PAUSED, String(gd::bytes_to_var(p_payload)));
        }
        return true;
    }
    if (p_channel == control_channels.unpause) {
        if (from_server) {
            emit_signal(SIG_TREE_UNPAUSED);
        }
        return true;
    }
    if (p_channel == control_channels.kicked) {
        if (from_server) {
            emit_signal(SIG_KICKED, String(gd::bytes_to_var(p_payload)));
        }
        return true;
    }
    if (p_channel == control_channels.shutdown) {
        if (from_server) {
            emit_signal(
                SIG_SERVER_DISCONNECTING,
                String(gd::bytes_to_var(p_payload))
            );
        }
        return true;
    }
    if (p_channel == control_channels.kick_request) {
        const Variant data = gd::bytes_to_var(p_payload);
        if (is_server() && data.get_type() == Variant::ARRAY
            && Array(data).size() == 2) {
            const Array pair = data;
            emit_signal(
                SIG_KICK_REQUESTED,
                p_sender,
                int64_t(pair[0]),
                String(pair[1])
            );
        }
        return true;
    }
    if (p_channel == control_channels.leave_request) {
        if (is_server()) {
            emit_signal(
                SIG_DISCONNECT_REQUESTED,
                p_sender,
                String(gd::bytes_to_var(p_payload))
            );
        }
        return true;
    }
    return false;
}

void NetwMultiplayerCore::set_session_join_handler(const Callable &p_handler) {
    session_join_handler = p_handler;
}

void NetwMultiplayerCore::session_admit(const Ref<ResolvedJoin> &p_join) {
    if (p_join.is_null() || !join_roster.remember(p_join)) {
        return;
    }
    const int64_t joined = p_join->get_peer_id();
    participant_ensure(joined);
    if (participant_of(joined).is_null()) {
        return;
    }
    participant_admit(joined);
    if (is_server() && session_join_handler.is_valid()) {
        session_join_handler.call(joined);
    }
    participant_publish_joined(joined);
}

void NetwMultiplayerCore::set_session_join_resolver(const Callable &p_resolver) {
    session_join_resolver = p_resolver;
}

void NetwMultiplayerCore::set_session_entered_hook(const Callable &p_hook) {
    session_entered_hook = p_hook;
}

void NetwMultiplayerCore::set_session_edge_hook(const Callable &p_hook) {
    session_edge_hook = p_hook;
}

void NetwMultiplayerCore::session_after_entered() {
    if (session_entered_hook.is_valid()) {
        session_entered_hook.call();
    }
}

void NetwMultiplayerCore::session_after_edge(int64_t p_old, int64_t p_new) {
    if (session_edge_hook.is_valid()) {
        session_edge_hook.call(p_old, p_new);
    }
}

void NetwMultiplayerCore::session_receive_join(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (!is_server() || !session_join_resolver.is_valid()) {
        return;
    }
    const Time *reading = Time::get_singleton();
    if (session_core.join_flooded(
            int(p_sender),
            reading ? int64_t(reading->get_ticks_msec()) : 0
        )) {
        return;
    }
    Ref<JoinPayload> requested;
    requested.instantiate();
    if (!requested->deserialize(p_payload)) {
        NETW_WARN(
            sys::SESSION,
            "join: unreadable request from peer %d",
            int(p_sender)
        );
        return;
    }
    requested->set_peer_id(p_sender);
    const Ref<ResolvedJoin> resolved
        = session_join_resolver.call(requested, p_sender);
    if (resolved.is_null()) {
        return;
    }
    session_admit(resolved);

    if (inner.is_valid()) {
        set_peer_ids(gd::api_peer_ids(inner));
    }
    const PackedByteArray accepted = resolved->serialize();
    const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
    for (int at = 0; at < int(peers.size()); ++at) {
        send_to(
            peers[at],
            0,
            session_accept_channel,
            accepted,
            true,
            0,
            String(),
            false
        );
    }
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER) {
        send_to(
            p_sender,
            0,
            session_roster_channel,
            gd::var_to_bytes(join_roster.serialize_accepted()),
            true,
            0,
            String(),
            false
        );
    }
}

void NetwMultiplayerCore::session_receive_accept(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER) {
        return;
    }
    session_admit(ResolvedJoin::deserialize(p_payload));
}

void NetwMultiplayerCore::session_receive_roster(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER) {
        return;
    }
    const Variant decoded = gd::bytes_to_var(p_payload);
    if (decoded.get_type() != Variant::ARRAY) {
        return;
    }
    const Array rows = decoded;
    for (int at = 0; at < rows.size(); ++at) {
        session_admit(ResolvedJoin::deserialize(rows[at]));
    }
}

void NetwMultiplayerCore::session_receive_control(
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    session_publish_control(p_channel, p_sender, p_payload);
}

SessionCore &NetwMultiplayerCore::session_plane() {
    return session_core;
}

void NetwMultiplayerCore::session_announce_entered() {
    emit_signal(SIG_SESSION_ENTERED);
    session_after_entered();
}

void NetwMultiplayerCore::session_announce_ended() {
    emit_signal(SIG_SESSION_ENDED);
    emit_signal(SIG_SESSION_RECLAIMED);
}

void NetwMultiplayerCore::session_announce_edge(int p_old, int p_new) {
    emit_signal(SIG_STATE_CHANGED, p_old, p_new);
    session_after_edge(p_old, p_new);
}

Ref<ResolvedJoin> NetwMultiplayerCore::session_accepted_join(int64_t p_peer
) const {
    return join_roster.accepted_join(p_peer);
}

Array NetwMultiplayerCore::session_accepted_joins() const {
    return join_roster.accepted_joins();
}

bool NetwMultiplayerCore::session_remember_join(const Ref<ResolvedJoin> &p_join
) {
    return join_roster.remember(p_join);
}

void NetwMultiplayerCore::session_forget_peer(int64_t p_peer) {
    join_roster.forget(p_peer);
}

void NetwMultiplayerCore::session_clear_roster() {
    join_roster.clear();
}

void NetwMultiplayerCore::session_refuse(
    int64_t p_peer,
    const String &p_reason
) {
    join_roster.refuse(p_peer, p_reason);
}

String NetwMultiplayerCore::session_refusal(int64_t p_peer) const {
    return join_roster.refusal(p_peer);
}

int NetwMultiplayerCore::session_name_verdict(
    const StringName &p_name,
    const PackedStringArray &p_taken,
    bool p_is_debug,
    bool p_has_identity
) const {
    return join_roster.name_verdict(
        p_name,
        p_taken,
        p_is_debug,
        p_has_identity
    );
}

StringName NetwMultiplayerCore::session_free_name(
    const StringName &p_name,
    const PackedStringArray &p_taken
) const {
    return join_roster.free_name(p_name, p_taken);
}

void NetwMultiplayerCore::session_set_state(int p_state) {
    session_core.set_state(SessionCore::State(p_state));
}

void NetwMultiplayerCore::session_set_role(int p_role) {
    session_core.set_role(SessionCore::Role(p_role));
}

void NetwMultiplayerCore::session_set_desired_role(int p_role) {
    session_core.set_desired_role(SessionCore::Role(p_role));
}

void NetwMultiplayerCore::session_transition(int p_state) {
    session_core.transition(SessionCore::State(p_state));
}

void NetwMultiplayerCore::session_peer_assigned(
    bool p_live,
    bool p_connected,
    int p_unique_id
) {
    session_core.on_peer_assigned(p_live, p_connected, p_unique_id);
}

void NetwMultiplayerCore::session_resolve_online(int p_unique_id) {
    session_core.resolve_online(p_unique_id);
}

void NetwMultiplayerCore::session_set_advertised_max_players(int p_cap) {
    session_core.set_advertised_max_players(p_cap);
}

int NetwMultiplayerCore::session_advertised_max_players() const {
    return session_core.get_advertised_max_players();
}

int64_t NetwMultiplayerCore::session_app_tag(const StringName &p_app_id) {
    return SessionCore::compute_app_tag(p_app_id);
}

void NetwMultiplayerCore::session_broadcast_control(
    uint8_t p_channel,
    const PackedByteArray &p_payload
) {
    if (inner.is_valid()) {
        set_peer_ids(gd::api_peer_ids(inner));
    }
    const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
    for (int at = 0; at < int(peers.size()); ++at) {
        send_to(
            peers[at],
            0,
            p_channel,
            p_payload,
            true,
            0,
            String(),
            false
        );
    }
    session_publish_control(
        p_channel,
        MultiplayerPeer::TARGET_PEER_SERVER,
        p_payload
    );
}

void NetwMultiplayerCore::session_pause(const String &p_reason) {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "a pause is issued by server authority"
    );
    session_broadcast_control(
        control_channels.pause,
        gd::var_to_bytes(p_reason)
    );
}

void NetwMultiplayerCore::session_unpause() {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "an unpause is issued by server authority"
    );
    session_broadcast_control(control_channels.unpause, PackedByteArray());
}

void NetwMultiplayerCore::session_notify_shutdown(const String &p_reason) {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "a shutdown notice is issued by server authority"
    );
    session_broadcast_control(
        control_channels.shutdown,
        gd::var_to_bytes(p_reason)
    );
}

void NetwMultiplayerCore::session_request_leave(const String &p_reason) {
    const PackedByteArray payload = gd::var_to_bytes(p_reason);
    if (is_server()) {
        session_publish_control(
            control_channels.leave_request,
            MultiplayerPeer::TARGET_PEER_SERVER,
            payload
        );
        return;
    }
    send_to(
        MultiplayerPeer::TARGET_PEER_SERVER,
        0,
        control_channels.leave_request,
        payload,
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayerCore::session_kick(
    int64_t p_peer_id,
    const String &p_reason
) {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "a kick is issued by server authority"
    );
    if (!p_reason.is_empty()) {
        send_to(
            p_peer_id,
            0,
            control_channels.kicked,
            gd::var_to_bytes(p_reason),
            true,
            0,
            String(),
            false
        );
    }
    if (has_multiplayer_peer()) {
        get_multiplayer_peer()->disconnect_peer(int32_t(p_peer_id));
    }
}

void NetwMultiplayerCore::session_request_kick(
    int64_t p_peer_id,
    const String &p_reason
) {
    Array pair;
    pair.push_back(p_peer_id);
    pair.push_back(p_reason);
    const PackedByteArray payload = gd::var_to_bytes(pair);
    if (is_server()) {
        session_publish_control(control_channels.kick_request, 1, payload);
        return;
    }
    send_to(
        MultiplayerPeer::TARGET_PEER_SERVER,
        0,
        control_channels.kick_request,
        payload,
        true,
        0,
        String(),
        false
    );
}

Ref<NetwCarrierFrame> NetwMultiplayerCore::receive_header(
    int64_t p_peer,
    const PackedByteArray &p_packet
) {
    const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(p_packet);
    if (header->kind == NetwCarrierFrame::FOREIGN) {
        emit_signal(SIG_PEER_PACKET, p_peer, p_packet);
        return header;
    }
    if (header->kind == NetwCarrierFrame::MALFORMED) {
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
    count_received(p_packet.size() - header->payload_offset);
    if (plane.wants(EventPlane::DATAGRAM_RECEIVED, 0)) {
        EventPlane::Emission fact(
            EventPlane::DATAGRAM_RECEIVED,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        const bool reliable = header->kind == NetwCarrierFrame::RELIABLE;
        fact.peer = p_peer;
        Dictionary detail;
        detail["size"] = p_packet.size() - header->payload_offset;
        detail["seq"] = reliable ? int64_t(-1) : header->seq;
        detail["reliable"] = reliable;
        fact.detail = detail;
        plane.emit(fact);
    }
    return header;
}

void NetwMultiplayerCore::table_publish_intake() {
    const TypedArray<RID> touched = table_core->touched_tables();
    for (int index = 0; index < touched.size(); index++) {
        table_publish(touched[index]);
    }
    table_core->begin_intake();
}

void NetwMultiplayerCore::table_publish(const RID &p_table) {
    emit_signal(SIG_TABLE_RECEIVED, p_table, table_core->tick_of(p_table));
}

Error NetwMultiplayerCore::spawn_admit_frame_default(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    (void)p_route;
    if (p_sender != 1) {
        return ERR_UNAUTHORIZED;
    }
    if (p_channel != gate_channels.spawn && p_channel != gate_channels.despawn
        && p_channel != gate_channels.reparent) {
        return ERR_INVALID_DATA;
    }
    if (p_payload.is_empty()) {
        return ERR_INVALID_DATA;
    }
    return OK;
}

Error NetwMultiplayerCore::table_admit_frame_default(
    int64_t p_sender,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    if (p_channel != gate_channels.table) {
        return ERR_INVALID_DATA;
    }
    if (p_sender != 1) {
        table_core->count_bad_sender();
        return ERR_UNAUTHORIZED;
    }
    if (p_payload.is_empty()) {
        return ERR_INVALID_DATA;
    }
    return table_core->admit_header(TableCore::peek_header(p_payload));
}

Callable NetwMultiplayerCore::relay_for(
    const StringName &p_signal,
    int p_arity
) {
    switch (p_arity) {
        case 0:
            return callable_mp(this, &NetwMultiplayerCore::relay_bare)
                .bind(p_signal);
        case 1:
            return callable_mp(this, &NetwMultiplayerCore::relay_one)
                .bind(p_signal);
        case 2:
            return callable_mp(this, &NetwMultiplayerCore::relay_two)
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

void NetwMultiplayerCore::relay_from(
    Object *p_source,
    const StringName &p_signal,
    int p_arity
) {
    relay_named_from(p_source, p_signal, p_signal, p_arity);
}

void NetwMultiplayerCore::relay_named_from(
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

void NetwMultiplayerCore::stop_relay_named_from(
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

void NetwMultiplayerCore::stop_relay_from(
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

void NetwMultiplayerCore::relay_bare(const StringName &p_signal) {
    emit_signal(p_signal);
}

void NetwMultiplayerCore::relay_one(
    const Variant &p_first,
    const StringName &p_signal
) {
    emit_signal(p_signal, p_first);
}

void NetwMultiplayerCore::relay_two(
    const Variant &p_first,
    const Variant &p_second,
    const StringName &p_signal
) {
    emit_signal(p_signal, p_first, p_second);
}

void NetwMultiplayerCore::relay_auth_failed(int64_t p_peer) {
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
}

NetwMultiplayerCore::~NetwMultiplayerCore() {
}

Error NetwMultiplayerCore::NETW_API_VIRTUAL(poll)() {
    NETW_ZONE_SYS(profile::SUBSYSTEM_SESSION);
    NETW_ZONE_COLOR(colors::SESSION);
    if (peer.is_valid()) {
        peer->poll();
    }
    return OK;
}

void NetwMultiplayerCore::NETW_API_VIRTUAL(set_multiplayer_peer)(
    const Ref<MultiplayerPeer> &p_peer
) {
    peer = p_peer;
}

Ref<MultiplayerPeer> NetwMultiplayerCore::NETW_API_VIRTUAL(
    get_multiplayer_peer
)() {
    return peer;
}

int32_t NetwMultiplayerCore::NETW_API_VIRTUAL(get_unique_id)()
    NETW_API_CONST {
    if (!peer.is_valid()
        || peer->get_connection_status()
            == MultiplayerPeer::CONNECTION_DISCONNECTED) {
        return 1;
    }
    return peer->get_unique_id();
}

PackedInt32Array NetwMultiplayerCore::NETW_API_VIRTUAL(get_peer_ids)()
    NETW_API_CONST {
    return peer_ids;
}

int32_t NetwMultiplayerCore::NETW_API_VIRTUAL(get_remote_sender_id)()
    NETW_API_CONST {
    NETW_ERR_V(
        0,
        sys::ENTITY,
        "NetwMultiplayerCore cannot answer the remote sender: the entity plane "
        "that routes an inbound call has not crossed."
    );
}

Error NetwMultiplayerCore::NETW_API_VIRTUAL(object_configuration_add)(
    Object *,
    NETW_API_CONFIG_ARG
) {
    return ERR_UNAVAILABLE;
}

Error NetwMultiplayerCore::NETW_API_VIRTUAL(object_configuration_remove)(
    Object *,
    NETW_API_CONFIG_ARG
) {
    return ERR_UNAVAILABLE;
}

#if defined(NETW_MODULE)
Error NetwMultiplayerCore::rpcp(
    Object *,
    int,
    const StringName &,
    const Variant **,
    int
) {
    return ERR_UNAVAILABLE;
}
#else
Error NetwMultiplayerCore::_rpc(
    int32_t,
    Object *,
    const StringName &,
    const Array &
) {
    return ERR_UNAVAILABLE;
}
#endif

void NetwMultiplayerCore::set_peer_ids(
    const PackedInt32Array &p_peer_ids
) {
    peer_ids = p_peer_ids;
}

Ref<NetwLivenessCore> NetwMultiplayerCore::get_liveness_core() const {
    return liveness_core;
}

Ref<NetwClockHandle> NetwMultiplayerCore::get_clock_handle() const {
    return clock_handle;
}

Ref<NetwSceneCore> NetwMultiplayerCore::get_scene_core() const {
    return scene_core;
}

JoinRoster &NetwMultiplayerCore::join_book() {
    return join_roster;
}

Ref<NetwDisplayBook> NetwMultiplayerCore::get_display_book() const {
    return display_book;
}

Ref<NetwChannelBook> NetwMultiplayerCore::get_channel_book() const {
    return channel_book;
}

Ref<NetwLagCompCore> NetwMultiplayerCore::get_lagcomp_core() const {
    return lagcomp_core;
}

Ref<NetwPredictionEngine> NetwMultiplayerCore::get_prediction_engine() const {
    return prediction_engine;
}

InterestEngine &NetwMultiplayerCore::interest_plane() {
    return interest_engine;
}

void NetwMultiplayerCore::reset_interest() {
    interest_engine.clear();
}

void NetwMultiplayerCore::interest_sync_record(Object *p_wrapper) {
    NetwEntity *current = Object::cast_to<NetwEntity>(p_wrapper);
    LocalVector<int64_t> keys;
    LocalVector<int64_t> routes;
    HashSet<int64_t> seen;
    while (current != nullptr) {
        const RID handle = liveness_adopt(current);
        const int64_t key = handle.get_id();
        if (!InterestEngine::is_key(key) || seen.has(key)) {
            break;
        }
        seen.insert(key);
        keys.push_back(key);
        routes.push_back(current->get_route());
        Node *owner = Object::cast_to<Node>(wrapper_owner(handle));
        current = owner == nullptr
            ? nullptr
            : Object::cast_to<NetwEntity>(
                  wrapper_at(owner->get_parent()).ptr()
              );
    }
    for (uint32_t index = keys.size(); index-- > 0;) {
        const int64_t key = keys[index];
        const int64_t parent_key
            = index + 1 < keys.size() ? keys[index + 1] : 0;
        interest_engine.set_parent(key, parent_key);
        int64_t route = routes[index];
        if (route <= 0) {
            route = interest_engine.order_route_for(key);
        }
        interest_engine.set_order_key(
            key,
            int(keys.size() - 1 - index),
            int(route)
        );
    }
}

void NetwMultiplayerCore::set_scene_refresh(const Callable &p_refresh) {
    scene_refresh = p_refresh;
}

void NetwMultiplayerCore::interest_track_lifecycle(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    liveness_adopt(p_entity.ptr());
    const int64_t slot = p_entity->get_rid_handle().get_id();
    const Callable handler
        = callable_mp(this, &NetwMultiplayerCore::interest_on_entity_exiting)
              .bind(p_entity);
    if (!interest_engine.set_exit_handler(slot, handler)) {
        return;
    }
    Node *owner = p_entity->get_owner();
    if (owner != nullptr
        && !owner->is_connected("tree_exiting", handler)) {
        owner->connect("tree_exiting", handler);
    }
}

void NetwMultiplayerCore::interest_untrack_lifecycle(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    const Callable handler = interest_engine.take_exit_handler(
        p_entity->get_rid_handle().get_id()
    );
    Node *owner = p_entity->get_owner();
    if (handler.is_valid() && owner != nullptr
        && owner->is_connected("tree_exiting", handler)) {
        owner->disconnect("tree_exiting", handler);
    }
}

Dictionary NetwMultiplayerCore::interest_leave_resolve(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id
) {
    if (p_entity.is_null()) {
        return Dictionary();
    }
    const RID handle = p_entity->get_rid_handle();
    return interest_leave.resolve(
        handle.get_id(),
        p_peer_id,
        interest_decl_on(p_entity),
        interest_engine
    );
}

void NetwMultiplayerCore::interest_leave_commit(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id,
    const Dictionary &p_decision,
    bool p_forced
) {
    if (p_entity.is_null()) {
        return;
    }
    interest_leave.commit(
        p_entity->get_rid_handle().get_id(),
        p_peer_id,
        p_decision,
        p_forced
    );
}

void NetwMultiplayerCore::interest_leave_finish_sweep() {
    interest_leave.finish_sweep();
}

void NetwMultiplayerCore::interest_retire_entity(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    if (slot == 0 || !interest_engine.has_entity(slot)) {
        return;
    }
    interest_engine.remove_entity(slot);
}

void NetwMultiplayerCore::interest_on_entity_exiting(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    const Array named = interest_engine.memberships(slot);
    for (int at = 0; at < named.size(); ++at) {
        const Ref<NetwInterestLayer> exiting
            = interest_layer_named(named[at]);
        if (exiting.is_null()) {
            continue;
        }
        if (is_server()) {
            exiting->remove_entity(p_entity);
        } else {
            exiting->client_untrack_entity(p_entity);
        }
    }
    interest_untrack_lifecycle(p_entity);
    interest_retire_entity(p_entity);
    interest_leave.forget_entity(slot);
    interest_engine.set_scene_membership(slot, StringName());
    interest_clear_perception(p_entity, false);
}

void NetwMultiplayerCore::interest_refresh_perception(
    const Ref<NetwEntity> &p_entity,
    const Array &p_layer_ids
) {
    const int64_t peer_id = interest_local_participant();
    if (peer_id == 0 || p_entity.is_null()
        || p_entity->get_owner() == nullptr) {
        return;
    }
    const RID handle = p_entity->get_rid_handle();
    const int64_t slot = handle.get_id();
    if (is_server()) {
        if (!interest_engine.has_entity(slot)) {
            return;
        }
    } else if (interest_membership_ids(handle).is_empty()) {
        return;
    }
    const bool visible = interest_participant_sees(peer_id, p_entity);
    if (!interest_perception.set_visible(slot, visible)) {
        return;
    }
    if (visible) {
        perception_restore(slot);
        perception_dispatch_custom(slot, true, peer_id);
        return;
    }
    Array layers = p_layer_ids;
    if (layers.is_empty()) {
        layers = perception_layers_for(p_entity, peer_id);
    }
    const Dictionary verdict = interest_perception.resolve(
        layers,
        interest_decl_on(p_entity),
        interest_engine
    );
    if (bool(verdict[StringName("hide")])) {
        perception_hide(slot, p_entity->get_owner());
    }
    const Array actions = verdict[StringName("custom")];
    interest_perception.arm(slot, actions);
    for (int at = 0; at < actions.size(); ++at) {
        const Array action = actions[at];
        const Callable callback = action[0];
        if (callback.is_valid()) {
            callback.call(false, peer_id, action[1]);
        }
    }
}

void NetwMultiplayerCore::interest_refresh_all_perception() {
    if (interest_local_participant() == 0) {
        return;
    }
    const PackedInt64Array keys = interest_engine.membership_keys();
    for (int at = 0; at < int(keys.size()); ++at) {
        const Ref<NetwEntity> member = Ref<NetwEntity>(
            Object::cast_to<NetwEntity>(wrapper_for_id(keys[at]).ptr())
        );
        if (member.is_valid()) {
            interest_refresh_perception(member, Array());
        }
    }
}

void NetwMultiplayerCore::interest_reapply_perception(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    if (interest_perception.is_known(p_entity->get_rid_handle().get_id())) {
        interest_clear_perception(p_entity, true);
    }
    interest_refresh_perception(p_entity, Array());
}

void NetwMultiplayerCore::interest_clear_perception(
    const Ref<NetwEntity> &p_entity,
    bool p_restore
) {
    if (p_entity.is_null()) {
        return;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    if (p_restore) {
        perception_restore(slot);
        perception_dispatch_custom(slot, true, interest_local_participant());
    } else {
        perception_snapshots.erase(slot);
        interest_perception.disarm(slot);
    }
    interest_perception.forget(slot);
}

void NetwMultiplayerCore::interest_clear_all_perception() {
    LocalVector<int64_t> hidden;
    for (const KeyValue<int64_t, LocalVector<PerceptionSnapshot>> &row :
         perception_snapshots) {
        hidden.push_back(row.key);
    }
    for (uint32_t at = 0; at < hidden.size(); ++at) {
        perception_restore(hidden[at]);
    }
    const PackedInt64Array armed = interest_perception.armed_keys();
    const int64_t peer_id = interest_local_participant();
    for (int at = 0; at < int(armed.size()); ++at) {
        perception_dispatch_custom(armed[at], true, peer_id);
    }
    interest_perception.clear();
    perception_snapshots.clear();
}

namespace {

constexpr int64_t CLOCKLESS_AWARENESS_TICKRATE = 30;

} // namespace

void NetwMultiplayerCore::interest_receive_awareness(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER) {
        return;
    }
    const Variant decoded = gd::bytes_to_var(p_payload);
    if (decoded.get_type() != Variant::ARRAY) {
        return;
    }
    const Array events = decoded;
    for (int at = 0; at < events.size(); ++at) {
        InterestAwareness edge;
        if (!InterestAwareness::from_array(events[at], edge)) {
            continue;
        }
        const bool entered = edge.kind == InterestAwareness::ENTER;
        if (!entered
            && liveness_route_state(edge.route)
                != NetwLivenessCore::STATE_LIVE) {
            continue;
        }
        const bool clocked = clock_engine().get_configured();
        const int64_t origin
            = clocked ? clock_engine().get_tick() : liveness_core->frame();
        const int64_t timeout
            = clocked ? clock_engine().get_tickrate()
                      : CLOCKLESS_AWARENESS_TICKRATE;
        liveness_when_live(
            edge.route,
            callable_mp(this, &NetwMultiplayerCore::interest_apply_awareness)
                .bind(
                    edge.type,
                    edge.route,
                    edge.layer_id,
                    edge.observer_peer,
                    edge.kind
                ),
            origin + timeout,
            clocked,
            Callable()
        );
    }
}

void NetwMultiplayerCore::interest_apply_awareness(
    int p_edge_type,
    int64_t p_route,
    const StringName &p_layer_id,
    int64_t p_observer_peer,
    int p_kind
) {
    const Ref<NetwEntity> entity = Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(wrapper_for_route(p_route).ptr())
    );
    if (entity.is_null()) {
        return;
    }
    InterestAwareness edge;
    edge.type = int32_t(p_edge_type);
    edge.route = p_route;
    edge.layer_id = p_layer_id;
    edge.observer_peer = p_observer_peer;
    edge.kind = int32_t(p_kind);
    const bool entered = edge.kind == InterestAwareness::ENTER;
    if (edge.type == InterestAwareness::LAYER) {
        const Ref<NetwInterestLayer> event_layer = interest_layer(p_layer_id);
        if (event_layer.is_null()) {
            return;
        }
        interest_report_edge(edge, entity, entered, true);
        if (entered) {
            event_layer->client_admit(entity);
        } else {
            event_layer->client_revoke(entity);
        }
        return;
    }
    interest_report_edge(edge, entity, entered, false);
    entity->emit_signal(
        entered ? "observer_entered" : "observer_left",
        p_layer_id,
        p_observer_peer
    );
}

void NetwMultiplayerCore::interest_report_edge(
    const InterestAwareness &p_edge,
    const Ref<NetwEntity> &p_entity,
    bool p_entered,
    bool p_layer_edge
) {
    int64_t value = p_entered ? EventPlane::INTEREST_ENTER
                              : EventPlane::INTEREST_EXIT;
    if (!p_layer_edge) {
        value = p_entered ? EventPlane::OBSERVER_ENTERED
                          : EventPlane::OBSERVER_LEFT;
    }
    if (!event_wants(value, p_edge.route)) {
        return;
    }
    Dictionary detail;
    detail[StringName("layer")] = p_edge.layer_id;
    event_emit(
        value,
        p_edge.route,
        detail,
        p_entity->get_entity_id(),
        p_edge.observer_peer,
        OK,
        Dictionary()
    );
}

bool NetwMultiplayerCore::interest_can_send_to(int64_t p_peer_id) {
    if (p_peer_id == 0
        || p_peer_id == MultiplayerPeer::TARGET_PEER_SERVER) {
        return false;
    }
    if (!has_multiplayer_peer()) {
        return false;
    }
    const Ref<MultiplayerPeer> live = get_multiplayer_peer();
    if (live.is_null() || live->is_class("OfflineMultiplayerPeer")) {
        return false;
    }
    if (live->get_connection_status() != MultiplayerPeer::CONNECTION_CONNECTED) {
        return false;
    }
    if (is_server()) {
        return NETW_API_VIRTUAL(get_peer_ids)().has(int32_t(p_peer_id));
    }
    return false;
}

void NetwMultiplayerCore::interest_queue_layer_awareness(
    const StringName &p_layer_id,
    const Ref<NetwEntity> &p_entity,
    int64_t p_observer_peer,
    int p_kind
) {
    if (!is_server() || p_entity.is_null()) {
        return;
    }
    Node *owner = p_entity->get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    if (!interest_can_send_to(p_observer_peer)) {
        return;
    }
    interest_awareness_queue_layer(
        p_observer_peer,
        liveness_allocate_route(p_entity.ptr()),
        p_layer_id,
        p_kind
    );
    interest_request_flush();
}

void NetwMultiplayerCore::interest_queue_observer_awareness(
    const StringName &p_layer_id,
    const Ref<NetwEntity> &p_entity,
    int64_t p_observer_peer,
    int p_kind
) {
    if (!is_server() || p_entity.is_null()) {
        return;
    }
    const int64_t owner_peer = p_entity->get_peer_id();
    if (owner_peer == 0 || p_observer_peer == owner_peer) {
        return;
    }
    const Ref<NetwInterestDecl> decl = interest_decl_on(p_entity);
    if (decl.is_null() || !decl->get_reports_observers()) {
        return;
    }
    Node *owner = p_entity->get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    if (!interest_can_send_to(owner_peer)) {
        return;
    }
    interest_awareness_queue_observer(
        owner_peer,
        liveness_allocate_route(p_entity.ptr()),
        p_layer_id,
        p_observer_peer,
        p_kind
    );
    interest_request_flush();
}

void NetwMultiplayerCore::interest_layer_transition(
    const Ref<NetwInterestLayer> &p_layer,
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id,
    bool p_visible
) {
    p_layer->apply_server_transition(p_entity, p_peer_id, p_visible);
    const int64_t slot = p_entity->get_rid_handle().get_id();
    const StringName layer_id = p_layer->get_layer_id();
    if (p_visible) {
        interest_leave.release(slot, p_peer_id);
    } else {
        interest_leave.record(slot, p_peer_id, layer_id);
    }
    const int kind = p_visible ? 1 : 0;
    interest_queue_layer_awareness(layer_id, p_entity, p_peer_id, kind);
    interest_queue_observer_awareness(layer_id, p_entity, p_peer_id, kind);
}

void NetwMultiplayerCore::interest_apply_delta(
    const InterestDelta &p_delta
) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool visible = pass == 1;
        const Array rows
            = visible ? p_delta.get_layer_shows() : p_delta.get_layer_hides();
        for (int at = 0; at < rows.size(); ++at) {
            const Array row = rows[at];
            const StringName layer_id = row[0];
            const Ref<NetwEntity> entity = Ref<NetwEntity>(
                Object::cast_to<NetwEntity>(
                    wrapper_for_id(int64_t(row[1])).ptr()
                )
            );
            const int64_t peer_id = interest_engine.peer_of_bit(int(row[2]));
            const Ref<NetwInterestLayer> layer = interest_layer_named(layer_id);
            if (layer.is_null() || entity.is_null() || peer_id == 0) {
                continue;
            }
            interest_layer_transition(layer, entity, peer_id, visible);
        }
    }
    const Array shows = p_delta.get_shows();
    for (int at = 0; at < shows.size(); ++at) {
        const Array row = shows[at];
        const int64_t slot = int64_t(row[0]);
        if (wrapper_for_id(slot).is_valid()) {
            interest_leave.release(
                slot,
                interest_engine.peer_of_bit(int(row[1]))
            );
        }
    }
}

int64_t NetwMultiplayerCore::interest_local_participant() {
    if (!is_local_client()) {
        return 0;
    }
    if (get_role() == ROLE_LISTEN_SERVER) {
        return MultiplayerPeer::TARGET_PEER_SERVER;
    }
    if (has_multiplayer_peer()) {
        return get_unique_id();
    }
    return 0;
}

Array NetwMultiplayerCore::perception_layers_for(
    const Ref<NetwEntity> &p_entity,
    int64_t p_peer_id
) {
    const RID handle = p_entity->get_rid_handle();
    const Array pending
        = interest_leave.pending_layers(handle.get_id(), p_peer_id);
    if (!pending.is_empty()) {
        return pending;
    }
    return interest_membership_ids(handle);
}

void NetwMultiplayerCore::perception_hide(int64_t p_slot, Node *p_owner) {
    if (perception_snapshots.has(p_slot) || p_owner == nullptr) {
        return;
    }
    LocalVector<PerceptionSnapshot> snapshot;
    LocalVector<Node *> stack;
    stack.push_back(p_owner);
    while (!stack.is_empty()) {
        Node *node = stack[stack.size() - 1];
        stack.remove_at(stack.size() - 1);
        if (node->is_class("CanvasItem") || node->is_class("Node3D")) {
            const StringName prop = StringName("visible");
            PerceptionSnapshot row;
            row.node = node->get_instance_id();
            row.prop = prop;
            row.value = node->get(prop);
            snapshot.push_back(row);
            node->set(prop, false);
        }
        if (node->is_class("AudioStreamPlayer")
            || node->is_class("AudioStreamPlayer2D")
            || node->is_class("AudioStreamPlayer3D")) {
            const StringName prop = StringName("volume_db");
            PerceptionSnapshot row;
            row.node = node->get_instance_id();
            row.prop = prop;
            row.value = node->get(prop);
            snapshot.push_back(row);
            node->set(prop, -80.0);
        }
        const TypedArray<Node> children = node->get_children();
        for (int at = 0; at < children.size(); ++at) {
            Node *child = Object::cast_to<Node>(children[at]);
            if (child != nullptr) {
                stack.push_back(child);
            }
        }
    }
    perception_snapshots[p_slot] = snapshot;
}

void NetwMultiplayerCore::perception_restore(int64_t p_slot) {
    const LocalVector<PerceptionSnapshot> *held
        = perception_snapshots.getptr(p_slot);
    if (held == nullptr) {
        return;
    }
    LocalVector<PerceptionSnapshot> snapshot;
    for (uint32_t at = 0; at < held->size(); ++at) {
        snapshot.push_back((*held)[at]);
    }
    for (uint32_t at = 0; at < snapshot.size(); ++at) {
        Object *node = ObjectDB::get_instance(snapshot[at].node);
        if (node != nullptr) {
            node->set(snapshot[at].prop, snapshot[at].value);
        }
    }
    perception_snapshots.erase(p_slot);
}

void NetwMultiplayerCore::perception_dispatch_custom(
    int64_t p_slot,
    bool p_visible,
    int64_t p_peer_id
) {
    const Array actions = interest_perception.disarm(p_slot);
    for (int at = 0; at < actions.size(); ++at) {
        const Array action = actions[at];
        const Callable callback = action[0];
        if (callback.is_valid()) {
            callback.call(p_visible, p_peer_id, action[1]);
        }
    }
}

Ref<NetwInterestDecl> NetwMultiplayerCore::interest_decl_on(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return Ref<NetwInterestDecl>();
    }
    const Ref<RefCounted> facet = p_entity->get_interest();
    if (facet.is_null()) {
        return Ref<NetwInterestDecl>();
    }
    return facet->get(StringName("_decl"));
}

bool NetwMultiplayerCore::interest_participant_sees(
    int64_t p_peer_id,
    const Ref<NetwEntity> &p_entity
) {
    if (p_peer_id == 0 || p_entity.is_null()) {
        return false;
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    if (!is_server()) {
        return interest_engine.projection_admits(
            slot,
            interest_decl_on(p_entity)
        );
    }
    const int bit = interest_engine.peer_bit_of(p_peer_id);
    if (bit < 0) {
        return false;
    }
    return interest_engine.test(slot, bit);
}

void NetwMultiplayerCore::set_interest_compat_refresh(
    const Callable &p_refresh
) {
    interest_compat_refresh = p_refresh;
}

void NetwMultiplayerCore::set_interest_awareness_send(const Callable &p_send) {
    interest_awareness_send = p_send;
}

void NetwMultiplayerCore::set_interest_visibility_sweep(
    const Callable &p_sweep
) {
    interest_visibility_sweep = p_sweep;
}

namespace {

Array sorted_names(const Array &p_names) {
    LocalVector<StringName> ordered;
    for (int at = 0; at < p_names.size(); ++at) {
        const StringName id = p_names[at];
        uint32_t seat = ordered.size();
        while (seat > 0 && String(id) < String(ordered[seat - 1])) {
            --seat;
        }
        ordered.insert(seat, id);
    }
    Array out;
    for (uint32_t at = 0; at < ordered.size(); ++at) {
        out.push_back(ordered[at]);
    }
    return out;
}

} // namespace

Array NetwMultiplayerCore::interest_resolved_layer_ids(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return Array();
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    Array named = interest_engine.memberships(slot);
    if (named.is_empty() && !is_server()) {
        const Ref<NetwInterestDecl> decl = interest_decl_on(p_entity);
        named = decl.is_valid() ? decl->labels() : Array();
    }
    return sorted_names(named);
}

TypedArray<Object> NetwMultiplayerCore::interest_shared_entities(
    const Ref<NetwEntity> &p_entity,
    const StringName &p_layer_id
) {
    TypedArray<Object> out;
    if (p_entity.is_null()) {
        return out;
    }
    const Array resolved = interest_resolved_layer_ids(p_entity);
    Array wanted;
    if (p_layer_id.is_empty()) {
        wanted = resolved;
    } else {
        if (!resolved.has(p_layer_id)) {
            return out;
        }
        wanted.push_back(p_layer_id);
    }
    const int64_t slot = p_entity->get_rid_handle().get_id();
    LocalVector<Ref<NetwEntity>> found;
    const PackedInt64Array co = interest_engine.co_members(slot, wanted);
    for (int at = 0; at < int(co.size()); ++at) {
        const Ref<NetwEntity> candidate = Ref<NetwEntity>(
            Object::cast_to<NetwEntity>(wrapper_for_id(co[at]).ptr())
        );
        if (candidate.is_valid() && candidate->get_owner() != nullptr) {
            found.push_back(candidate);
        }
    }
    if (found.is_empty() && !is_server()) {
        HashSet<StringName> asked;
        for (int at = 0; at < wanted.size(); ++at) {
            asked.insert(wanted[at]);
        }
        const TypedArray<Object> live = liveness_live_entities();
        for (int at = 0; at < live.size(); ++at) {
            const Ref<NetwEntity> candidate = Ref<NetwEntity>(
                Object::cast_to<NetwEntity>(live[at])
            );
            if (candidate.is_null() || candidate == p_entity
                || candidate->get_owner() == nullptr) {
                continue;
            }
            const Ref<NetwInterestDecl> decl = interest_decl_on(candidate);
            const Array labels = decl.is_valid() ? decl->labels() : Array();
            for (int seat = 0; seat < labels.size(); ++seat) {
                if (asked.has(labels[seat])) {
                    found.push_back(candidate);
                    break;
                }
            }
        }
    }
    LocalVector<Ref<NetwEntity>> ordered;
    for (uint32_t at = 0; at < found.size(); ++at) {
        const Ref<NetwEntity> row = found[at];
        const String key = String(row->get_entity_id());
        uint32_t seat = ordered.size();
        while (seat > 0) {
            const Ref<NetwEntity> before = ordered[seat - 1];
            const String other = String(before->get_entity_id());
            const bool after = other < key
                || (other == key
                    && before->get_instance_id() < row->get_instance_id());
            if (after) {
                break;
            }
            --seat;
        }
        ordered.insert(seat, row);
    }
    for (uint32_t at = 0; at < ordered.size(); ++at) {
        out.push_back(ordered[at]);
    }
    return out;
}

Dictionary NetwMultiplayerCore::interest_monitor_snapshot() {
    Dictionary out;
    out[StringName("layers")] = interest_layers().size();
    out[StringName("entities_filtered")]
        = interest_engine.membership_keys().size();
    out[StringName("visible_edges")] = interest_engine.stats_edges();
    out[StringName("dirty_entities")] = interest_engine.dirty_count();
    out[StringName("relay_backlog")] = liveness_pending_live_count();
    out[StringName("transitions_total")] = interest_engine.transitions_total();
    out[StringName("vanished_dirty_skips")]
        = interest_engine.stats_vanished_dirty_skips();
    return out;
}

bool NetwMultiplayerCore::interest_wire_admits(
    int64_t p_peer_id,
    const Ref<NetwEntity> &p_entity
) {
    if (p_peer_id == MultiplayerPeer::TARGET_PEER_SERVER) {
        return true;
    }
    return interest_participant_sees(p_peer_id, p_entity);
}

bool NetwMultiplayerCore::interest_has_committed_intent(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return false;
    }
    return interest_engine.had_committed_intent(
        p_entity->get_rid_handle().get_id()
    );
}

PackedInt64Array NetwMultiplayerCore::interest_committed_row(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return PackedInt64Array();
    }
    return interest_engine.row_of(p_entity->get_rid_handle().get_id());
}

bool NetwMultiplayerCore::interest_bit_admits(
    const Ref<NetwEntity> &p_entity,
    int p_peer_bit
) {
    if (p_entity.is_null()) {
        return false;
    }
    return interest_engine.test(
        p_entity->get_rid_handle().get_id(),
        p_peer_bit
    );
}

String NetwMultiplayerCore::interest_explain_bit(
    const Ref<NetwEntity> &p_entity,
    int p_peer_bit
) {
    if (p_entity.is_null()) {
        return String("entity is not registered");
    }
    return interest_engine.explain(
        p_entity->get_rid_handle().get_id(),
        p_peer_bit
    );
}

void NetwMultiplayerCore::interest_forget_layer_row(
    const StringName &p_layer_id
) {
    interest_engine.remove_layer(p_layer_id);
}

int NetwMultiplayerCore::interest_peer_bit(int64_t p_peer_id) const {
    return interest_engine.peer_bit_of(p_peer_id);
}

bool NetwMultiplayerCore::interest_has_layer(const StringName &p_layer_id
) const {
    return interest_engine.has_layer(p_layer_id);
}

bool NetwMultiplayerCore::interest_entity_has_filter(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return false;
    }
    if (!is_server()) {
        const Ref<NetwInterestDecl> decl = interest_decl_on(p_entity);
        return decl.is_valid() && !decl->labels().is_empty();
    }
    return interest_engine.has_memberships(
        p_entity->get_rid_handle().get_id()
    );
}

void NetwMultiplayerCore::interest_peer_connected(int64_t p_peer_id) {
    interest_engine.peer_bit_for(p_peer_id);
    interest_sync_live_peers();
    if (interest_compat_refresh.is_valid()) {
        settle_schedule(
            interest_compat_refresh,
            StringName("interest_compat_intents")
        );
    }
    interest_request_flush();
}

void NetwMultiplayerCore::interest_peer_disconnected(int64_t p_peer_id) {
    interest_awareness_forget(p_peer_id);
    interest_leave.forget_peer(p_peer_id);
    interest_sync_live_peers();
    interest_request_flush();
}

void NetwMultiplayerCore::interest_session_ended() {
    callable_mp(this, &NetwMultiplayerCore::interest_clear_session)
        .call_deferred();
}

void NetwMultiplayerCore::interest_clear_session() {
    layer_forget_all();
    interest_leave.clear();
    interest_clear_all_perception();
    interest_awareness_clear();
    reset_interest();
    settle_cancel(interest_flush_key());
    interest_pending = InterestDelta();
    interest_pending_live = false;
}

void NetwMultiplayerCore::interest_sync_scene_membership(
    const Ref<NetwEntity> &p_entity
) {
    if (!is_server() || p_entity.is_null()
        || p_entity->get_owner() == nullptr) {
        return;
    }
    liveness_adopt(p_entity.ptr());
    const int64_t slot = p_entity->get_rid_handle().get_id();
    const StringName previous = interest_engine.scene_membership(slot);
    const RID scene = entity_scene_of(entity_of(p_entity->get_owner()));
    const StringName current
        = scene.is_valid() ? scene_layer_id(scene) : StringName();
    if (!interest_engine.set_scene_membership(slot, current)) {
        return;
    }
    if (!previous.is_empty()) {
        const Ref<NetwInterestLayer> before = interest_layer_named(previous);
        if (before.is_valid()) {
            before->remove_entity(p_entity);
        }
    }
    if (!current.is_empty()) {
        interest_layer(current)->add_entity(p_entity);
    }
}

PackedInt64Array NetwMultiplayerCore::interest_known_peers() const {
    return interest_engine.known_peers();
}

void NetwMultiplayerCore::interest_set_entity_intent(
    const Ref<NetwEntity> &p_entity,
    const PackedInt64Array &p_admitted
) {
    if (p_entity.is_null()) {
        return;
    }
    interest_track_lifecycle(p_entity);
    interest_sync_record(p_entity.ptr());
    liveness_adopt(p_entity.ptr());
    interest_engine.set_intent_for_peers(
        p_entity->get_rid_handle().get_id(),
        p_admitted
    );
    interest_request_flush();
}

void NetwMultiplayerCore::interest_sync_live_peers() {
    HashSet<int64_t> live;
    if (has_multiplayer_peer()) {
        if (inner.is_valid()) {
            set_peer_ids(gd::api_peer_ids(inner));
        }
        const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
        for (int at = 0; at < int(peers.size()); ++at) {
            live.insert(peers[at]);
        }
    }
    if (get_role() == ROLE_LISTEN_SERVER) {
        live.insert(MultiplayerPeer::TARGET_PEER_SERVER);
    }
    const PackedInt64Array viewers = interest_engine.viewer_peers();
    for (int at = 0; at < int(viewers.size()); ++at) {
        live.insert(viewers[at]);
    }
    PackedInt64Array ids;
    for (const int64_t &peer_id : live) {
        ids.push_back(peer_id);
    }
    if (interest_engine.set_live_peer_ids(ids)
        && interest_compat_refresh.is_valid()) {
        interest_compat_refresh.call();
    }
}

Error NetwMultiplayerCore::interest_recompute() {
    if (!is_server()) {
        interest_pending = InterestDelta();
        interest_pending_live = false;
        return OK;
    }
    interest_sync_live_peers();
    HashSet<int64_t> slots;
    const PackedInt64Array named = interest_engine.membership_keys();
    for (int at = 0; at < int(named.size()); ++at) {
        slots.insert(named[at]);
    }
    const PackedInt64Array intents = interest_engine.intent_keys();
    for (int at = 0; at < int(intents.size()); ++at) {
        slots.insert(intents[at]);
    }
    for (const int64_t &slot : slots) {
        const Ref<NetwEntity> member = Ref<NetwEntity>(
            Object::cast_to<NetwEntity>(wrapper_for_id(slot).ptr())
        );
        if (member.is_null() || member->get_owner() == nullptr) {
            continue;
        }
        if (!interest_engine.has_memberships(slot)
            && !interest_engine.has_intent(slot)) {
            interest_retire_entity(member);
            continue;
        }
        interest_sync_record(member.ptr());
    }
    interest_pending = interest_engine.recompute();
    interest_pending_live = true;
    return OK;
}

void NetwMultiplayerCore::interest_commit() {
    if (!interest_pending_live) {
        return;
    }
    interest_apply_delta(interest_pending);
    interest_engine.commit(interest_pending);
    wrapper_sweep_retired();
    interest_refresh_all_perception();
    interest_pending = InterestDelta();
    interest_pending_live = false;
    if (event_wants(EventPlane::INTEREST_COMMIT, 0)) {
        event_emit(
            EventPlane::INTEREST_COMMIT,
            0,
            Dictionary(),
            StringName(),
            0,
            OK,
            Dictionary()
        );
    }
}

void NetwMultiplayerCore::interest_relay_awareness() {
    const Array drained = interest_awareness_drain();
    if (!is_server() || !interest_awareness_send.is_valid()) {
        return;
    }
    for (int at = 0; at < drained.size(); ++at) {
        const Array row = drained[at];
        const int64_t target = row[0];
        if (!interest_can_send_to(target)) {
            continue;
        }
        interest_awareness_send.call(target, row[1]);
    }
}

Error NetwMultiplayerCore::interest_flush_tail() {
    interest_relay_awareness();
    const Ref<MultiplayerPeer> live = get_multiplayer_peer();
    if (has_multiplayer_peer() && live.is_valid()
        && live->get_connection_status()
            != MultiplayerPeer::CONNECTION_DISCONNECTED
        && interest_visibility_sweep.is_valid()) {
        interest_visibility_sweep.call();
    }
    return OK;
}

void NetwMultiplayerCore::set_interest_flush(const Callable &p_flush) {
    interest_flush = p_flush;
}

StringName NetwMultiplayerCore::interest_flush_key() {
    return StringName("interest_visibility");
}

void NetwMultiplayerCore::interest_request_flush() {
    if (!interest_flush.is_valid()) {
        NETW_ERROR(
            sys::INTEREST,
            "an interest flush was requested with none installed, so the "
            "committed rows would stay stale"
        );
        return;
    }
    settle_schedule(interest_flush, interest_flush_key());
}

bool NetwMultiplayerCore::interest_flush_pending() const {
    return settle_has_key(interest_flush_key());
}

namespace {

uint64_t clock_wall_usec() {
    const godot::Time *reading = godot::Time::get_singleton();
    return reading ? reading->get_ticks_usec() : 0;
}

} // namespace

void NetwMultiplayerCore::set_clock_mismatch_action(int p_action) {
    clock_mismatch_action = p_action;
}

void NetwMultiplayerCore::clock_request_handshake() {
    NETW_ZONE_NC("NetwMultiplayerCore clock handshake", colors::CLOCK);
    NETW_TRACE(sys::CLOCK, "Clock handshake asks the server for its tickrate.");
    send_to(
        1,
        0,
        clock_channels.handshake,
        gd::var_to_bytes(clock_engine().get_tickrate()),
        true,
        0,
        String(),
        true
    );
}

void NetwMultiplayerCore::clock_send_ping() {
    NETW_ZONE_NC("NetwMultiplayerCore clock ping", colors::CLOCK);
    PackedByteArray payload;
    payload.resize(4);
    gd::encode_u32(payload, 0, uint32_t(clock_wall_usec() & 0xFFFFFFFF));
    send_to(
        1,
        0,
        clock_channels.ping,
        payload,
        false,
        0,
        String(),
        false
    );
}

void NetwMultiplayerCore::clock_receive_handshake(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    (void)p_payload;
    if (get_unique_id() != 1) {
        return;
    }
    send_to(
        p_sender,
        0,
        clock_channels.handshake_reply,
        gd::var_to_bytes(clock_engine().get_tickrate()),
        true,
        0,
        String(),
        true
    );
}

void NetwMultiplayerCore::clock_receive_handshake_reply(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != 1) {
        return;
    }
    const Variant read = gd::bytes_to_var(p_payload);
    if (read.get_type() != Variant::INT) {
        NETW_ERROR(
            sys::CLOCK,
            "a clock handshake reply carried no tickrate, so the local rate "
            "stands uncalibrated"
        );
        return;
    }
    const int server_tickrate = int(read);
    if (server_tickrate != clock_engine().get_tickrate()) {
        if (clock_mismatch_action == 1) {
            if (peer.is_valid()) {
                peer->close();
            }
        } else if (clock_mismatch_action == 2) {
            emit_signal(SIG_CLOCK_TICKRATE_MISMATCH, p_sender, server_tickrate);
        } else {
            NETW_WARN(
                sys::CLOCK,
                "Clock tickrate mismatch, local %d server %d.",
                clock_engine().get_tickrate(),
                server_tickrate
            );
        }
    }
    clock_send_ping();
}

void NetwMultiplayerCore::clock_receive_ping(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (get_unique_id() != 1 || p_payload.size() < 4) {
        return;
    }
    const uint32_t client_usec = gd::decode_u32(p_payload, 0);
    const double phase = clock_engine().tick_phase();
    PackedByteArray reply;
    reply.resize(9);
    gd::encode_u32(reply, 0, client_usec);
    gd::encode_u32(
        reply,
        4,
        uint32_t(clock_engine().get_tick() & 0xFFFFFFFF)
    );
    gd::encode_u8(reply, 8, uint8_t(phase * 255.0));
    send_to(
        p_sender,
        0,
        clock_channels.pong,
        reply,
        false,
        0,
        String(),
        false
    );
}

void NetwMultiplayerCore::clock_receive_pong(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != 1 || p_payload.size() < 9) {
        return;
    }
    const uint32_t client_usec = gd::decode_u32(p_payload, 0);
    const uint32_t server_tick = gd::decode_u32(p_payload, 4);
    const double server_phase = double(gd::decode_u8(p_payload, 8)) / 255.0;
    const uint32_t elapsed
        = uint32_t((clock_wall_usec() & 0xFFFFFFFF) - client_usec);
    const double sample = double(elapsed) / 1'000'000.0;
    const Dictionary metrics = clock_engine().handle_pong(
        sample,
        int(server_tick),
        server_phase,
        get_unique_id() != 1
    );
    emit_signal(SIG_CLOCK_PONG_RECEIVED, metrics);
}

void NetwMultiplayerCore::interest_awareness_queue_layer(
    int64_t p_peer_id,
    int64_t p_route,
    const StringName &p_layer_id,
    int p_kind
) {
    NETW_ZONE_NC("NetwMultiplayerCore awareness layer edge", colors::INTEREST);
    NETW_ERR_COND(
        p_peer_id == 0,
        sys::INTEREST,
        "an awareness edge was queued for peer 0, which names no peer, so the "
        "edge would sit in the relay until a drain handed it to nobody"
    );
    NETW_ERR_COND(
        p_route == 0,
        sys::INTEREST,
        "an awareness edge was queued for no route, so no receiver could "
        "resolve the entity it names"
    );
    NETW_TRACE(
        sys::INTEREST,
        "Awareness layer edge for peer %d on route %d.",
        int(p_peer_id),
        int(p_route)
    );
    interest_relay.append(
        p_peer_id,
        InterestAwareness::layer_edge(p_route, p_layer_id, p_kind)
    );
}

void NetwMultiplayerCore::interest_awareness_queue_observer(
    int64_t p_peer_id,
    int64_t p_route,
    const StringName &p_layer_id,
    int64_t p_observer_peer,
    int p_kind
) {
    NETW_ZONE_NC(
        "NetwMultiplayerCore awareness observer edge",
        colors::INTEREST
    );
    NETW_ERR_COND(
        p_peer_id == 0,
        sys::INTEREST,
        "an awareness edge was queued for peer 0, which names no peer, so the "
        "edge would sit in the relay until a drain handed it to nobody"
    );
    NETW_ERR_COND(
        p_route == 0,
        sys::INTEREST,
        "an awareness edge was queued for no route, so no receiver could "
        "resolve the entity it names"
    );
    NETW_TRACE(
        sys::INTEREST,
        "Awareness observer edge for peer %d naming observer %d.",
        int(p_peer_id),
        int(p_observer_peer)
    );
    interest_relay.append(
        p_peer_id,
        InterestAwareness::observer_edge(
            p_route,
            p_layer_id,
            p_observer_peer,
            p_kind
        )
    );
}

Array NetwMultiplayerCore::interest_awareness_drain() {
    NETW_ZONE_NC("NetwMultiplayerCore awareness drain", colors::INTEREST);
    Array out;
    const PackedInt64Array targets = interest_relay.targets();
    for (int at = 0; at < targets.size(); ++at) {
        const Array wire = interest_relay.wire_for(targets[at]);
        if (wire.is_empty()) {
            continue;
        }
        Array row;
        row.push_back(targets[at]);
        row.push_back(wire);
        out.push_back(row);
    }
    interest_relay.clear();
    return out;
}

void NetwMultiplayerCore::interest_awareness_forget(int64_t p_peer_id) {
    interest_relay.forget(p_peer_id);
}

void NetwMultiplayerCore::interest_awareness_clear() {
    interest_relay.clear();
}

void NetwMultiplayerCore::set_display_role_resolver(
    const Callable &p_resolver
) {
    display_hooks.resolve_role = p_resolver;
}

void NetwMultiplayerCore::set_display_chase_clamp(const Callable &p_clamp) {
    display_hooks.chase_clamp = p_clamp;
}

void NetwMultiplayerCore::set_display_spec_reader(const Callable &p_reader) {
    display_hooks.spec_reader = p_reader;
}

void NetwMultiplayerCore::set_display_lane(const Callable &p_lane) {
    display_hooks.display_lane = p_lane;
}

void NetwMultiplayerCore::set_display_sync_intervals(
    const Callable &p_compute
) {
    display_hooks.sync_intervals = p_compute;
}

void NetwMultiplayerCore::set_display_authors_streams(
    const Callable &p_authors
) {
    display_hooks.authors_streams = p_authors;
}

void NetwMultiplayerCore::set_display_role_facts(const Callable &p_facts) {
    display_hooks.role_facts_reader = p_facts;
}

void NetwMultiplayerCore::set_display_chase_hook(const Callable &p_hook) {
    display_hooks.chase_hook_binder = p_hook;
}

void NetwMultiplayerCore::display_resolve_role(
    const Ref<NetwDisplayRuntime> &p_runtime
) {
    if (p_runtime.is_valid()) {
        display::resolve_role(p_runtime, display_hooks);
    }
}

bool NetwMultiplayerCore::display_wants_runtime(Node *p_owner) const {
    return display::wants_runtime(p_owner, display_hooks);
}

void NetwMultiplayerCore::display_rebuild_runtime(
    const Ref<NetwDisplayRuntime> &p_runtime
) {
    display::rebuild_runtime(p_runtime, display_hooks);
}

Ref<NetwDisplayChannel> NetwMultiplayerCore::display_ensure_state(
    const Ref<NetwDisplayRuntime> &p_runtime,
    Node *p_node,
    const StringName &p_source_prop,
    const StringName &p_target_prop,
    const Ref<NetwInterpolate> &p_spec,
    bool p_authoring_tick
) {
    if (p_runtime.is_null()) {
        return Ref<NetwDisplayChannel>();
    }
    return display::ensure_state(
        p_runtime,
        p_node,
        p_source_prop,
        p_target_prop,
        p_spec,
        p_authoring_tick,
        display_hooks
    );
}

bool NetwMultiplayerCore::display_authors_streams(
    const Ref<NetwDisplayRuntime> &p_runtime
) const {
    return display_hooks.authors(p_runtime);
}

void NetwMultiplayerCore::display_pump_runtime(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Ref<NetwDisplayTiming> &p_timing,
    const Ref<NetwPumpStats> &p_stats
) {
    if (p_runtime.is_null() || p_timing.is_null()) {
        return;
    }
    display::pump_runtime(
        p_runtime,
        p_timing,
        p_stats.is_valid() ? p_stats : display_book->get_stats(),
        display_hooks
    );
}

double NetwMultiplayerCore::display_chase_smooth_time(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Ref<NetwDisplayTiming> &p_timing
) const {
    if (p_runtime.is_null() || p_timing.is_null()) {
        return 0.0;
    }
    return display::chase_smooth_time(p_runtime, p_timing);
}

Error NetwMultiplayerCore::display_pump_entity(
    const RID &p_entity,
    const Ref<NetwDisplayTiming> &p_timing
) {
    const Ref<NetwDisplayRuntime> runtime = display_book->runtime_of(p_entity);
    NETW_ERR_COND_V(
        runtime.is_null(),
        ERR_DOES_NOT_EXIST,
        sys::INTERPOLATION,
        "no display runtime for entity %d",
        int(p_entity.get_id())
    );
    NETW_ERR_COND_V(
        p_timing.is_null(),
        ERR_UNCONFIGURED,
        sys::INTERPOLATION,
        "a pump needs a timing snapshot, and this frame captured none"
    );
    display_pump_runtime(runtime, p_timing, display_book->get_stats());
    return OK;
}

void NetwMultiplayerCore::display_absorb_recovery(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Dictionary &p_deltas,
    bool p_teleported
) {
    if (p_runtime.is_null()) {
        return;
    }
    display::absorb_recovery(
        p_runtime,
        p_deltas,
        p_teleported,
        display_hooks
    );
}

Ref<NetwPromise> NetwMultiplayerCore::scene_request(
    bool p_is_path,
    const Variant &p_destination,
    const Array &p_args,
    bool p_from_capture
) {
    const Ref<NetwPromise> promise = scene_core->request_open(p_from_capture);
    const int request_id = scene_core->get_pending_request_id();
    Array frame;
    frame.push_back(request_id);
    frame.push_back(p_is_path);
    frame.push_back(p_destination);
    frame.push_back(p_args);
    send_to(
        MultiplayerPeer::TARGET_PEER_SERVER,
        0,
        scene_request_channel,
        gd::var_to_bytes(frame),
        true,
        0,
        String(),
        false
    );
    return promise;
}

Ref<NetwPromise> NetwMultiplayerCore::scene_request_open(
    bool p_is_path,
    const Variant &p_destination,
    const Array &p_args,
    bool p_from_capture,
    double p_deadline
) {
    const Ref<NetwPromise> promise
        = scene_request(p_is_path, p_destination, p_args, p_from_capture);
    if (p_deadline > 0.0 && request_deadline_arm.is_valid()) {
        request_deadline_arm.call(
            scene_core->get_pending_request_id(),
            p_deadline
        );
    }
    return promise;
}

void NetwMultiplayerCore::scene_request_expire(int p_request_id) {
    scene_core->request_settle(p_request_id, ERR_TIMEOUT);
}

void NetwMultiplayerCore::set_request_deadline_arm(const Callable &p_arm) {
    request_deadline_arm = p_arm;
}

bool NetwMultiplayerCore::scene_receive_result_frame(
    const PackedByteArray &p_payload,
    int p_sender
) {
    if (p_sender != 1) {
        warn_verdict(ERR_UNAUTHORIZED, 0);
    }
    return scene_core->receive_result_frame(p_payload, p_sender);
}

void NetwMultiplayerCore::scene_send_result(
    int64_t p_peer,
    int p_request_id,
    int p_code
) {
    Array frame;
    frame.push_back(p_request_id);
    frame.push_back(p_code);
    send_to(
        p_peer,
        0,
        scene_result_channel,
        gd::var_to_bytes(frame),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayerCore::scene_answer_settled_result(
    int64_t p_peer,
    int p_request_id,
    const Ref<NetwPromise> &p_operation
) {
    scene_send_result(p_peer, p_request_id, p_operation->get_code());
}

void NetwMultiplayerCore::scene_answer_when_settled(
    const Ref<NetwPromise> &p_operation,
    int64_t p_peer,
    int p_request_id
) {
    if (p_operation.is_null()) {
        scene_send_result(p_peer, p_request_id, ERR_UNAVAILABLE);
        return;
    }
    p_operation->when_settled(
        callable_mp(this, &NetwMultiplayerCore::scene_answer_settled_result)
            .bind(p_peer, p_request_id, p_operation)
    );
}

void NetwMultiplayerCore::set_scene_mark_reader(const Callable &p_reader) {
    scene_mark_reader = p_reader;
}

Ref<NetwSceneMark> NetwMultiplayerCore::scene_mark_of(
    const Ref<Script> &p_script
) const {
    if (p_script.is_valid() && scene_mark_reader.is_valid()) {
        const Ref<NetwSceneMark> read = scene_mark_reader.call(p_script);
        if (read.is_valid()) {
            return read;
        }
    }
    Ref<NetwSceneMark> blank;
    blank.instantiate();
    return blank;
}

void NetwMultiplayerCore::set_scene_path_reader(const Callable &p_reader) {
    scene_path_reader = p_reader;
}

bool NetwMultiplayerCore::scene_request_targets(
    const Variant &p_destination,
    const StringName &p_label
) {
    const Variant::Type asked_type = p_destination.get_type();
    if (asked_type != Variant::STRING && asked_type != Variant::STRING_NAME) {
        return false;
    }
    const String named = String(p_destination);
    if (named == String(p_label)) {
        return true;
    }
    const String asked = gd::ensure_path(named);
    if (!asked.begins_with("res://")) {
        return false;
    }
    if (asked.get_file().get_basename() == String(p_label)) {
        return true;
    }
    if (!scene_path_reader.is_valid()) {
        return false;
    }
    const String declared = scene_path_reader.call(p_label);
    return !declared.is_empty() && gd::ensure_path(declared) == asked;
}

void NetwMultiplayerCore::scene_set_request_handler(const Callable &p_handler) {
    scene_core->set_request_handler(p_handler);
}

bool NetwMultiplayerCore::set_request_reach(int p_reach) {
    return scene_core->set_request_reach(p_reach);
}

int NetwMultiplayerCore::get_request_reach() const {
    return scene_core->get_request_reach();
}

RID NetwMultiplayerCore::get_current_scene() const {
    return scene_core->get_current_scene();
}

RID NetwMultiplayerCore::scene_named(const StringName &p_stem) const {
    return scene_core->scene_named(p_stem);
}

Array NetwMultiplayerCore::scenes_named(const StringName &p_stem) const {
    return scene_core->scenes_named(p_stem);
}

Array NetwMultiplayerCore::live_scenes() const {
    return scene_core->live_scenes();
}

void NetwMultiplayerCore::scene_observe(
    const RID &p_scene,
    int p_event,
    const Callable &p_callback
) {
    scene_core->observe(p_scene, p_event, p_callback);
}

void NetwMultiplayerCore::scene_unobserve(
    const RID &p_scene,
    int p_event,
    const Callable &p_callback
) {
    scene_core->unobserve(p_scene, p_event, p_callback);
}

Error NetwMultiplayerCore::scene_despawn(const RID &p_scene, int p_drain_pumps
) {
    Node *mounted = Object::cast_to<Node>(wrapper_owner(p_scene));
    NETW_ERR_COND_V(
        mounted == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::SCENE,
        "scene %d has no node to despawn",
        int64_t(p_scene.get_id())
    );
    Node *content = scene_level_of(mounted);
    NETW_ERR_COND_V(
        content == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::SCENE,
        "scene %d holds no content root, so it names no stem",
        int64_t(p_scene.get_id())
    );
    const StringName stem = content->get_name();
    const RID container = scene_core->scene_named(stem);
    Node *live = Object::cast_to<Node>(wrapper_owner(container));
    NETW_ERR_COND_V(
        live == nullptr,
        ERR_DOES_NOT_EXIST,
        sys::SCENE,
        "no live scene answers to the stem %s",
        String(stem)
    );
    if (p_drain_pumps <= 0) {
        Node *parent = live->get_parent();
        if (parent != nullptr) {
            parent->remove_child(live);
        }
        live->queue_free();
        return OK;
    }
    scene_core->scene_retire(container, p_drain_pumps);
    scene_settle_refresh();
    return OK;
}

RID NetwMultiplayerCore::layer_open(const StringName &p_name) {
    NETW_ERR_COND_V(
        p_name.is_empty(),
        RID(),
        sys::INTEREST,
        "an interest layer was opened with no name, so nothing could address it"
    );
    const RID *known = layer_by_name.getptr(p_name);
    if (known != nullptr) {
        return *known;
    }
    Ref<NetwInterestLayer> view;
    view.instantiate();
    view->set_layer_id(p_name);
    view->bind_session(this);
    const RID minted = layer_ledger->rid_create();
    layer_by_name[p_name] = minted;
    layer_views[minted.get_id()] = view;
    return minted;
}

RID NetwMultiplayerCore::layer_named(const StringName &p_name) const {
    const RID *known = layer_by_name.getptr(p_name);
    return known != nullptr ? *known : RID();
}

StringName NetwMultiplayerCore::layer_name_of(const RID &p_layer) const {
    for (const KeyValue<StringName, RID> &row : layer_by_name) {
        if (row.value == p_layer) {
            return row.key;
        }
    }
    return StringName();
}

Ref<RefCounted> NetwMultiplayerCore::layer_view(const RID &p_layer) const {
    const Ref<RefCounted> *held = layer_views.getptr(p_layer.get_id());
    return held != nullptr ? *held : Ref<RefCounted>();
}

void NetwMultiplayerCore::layer_close(const RID &p_layer) {
    const StringName named = layer_name_of(p_layer);
    if (!named.is_empty()) {
        layer_by_name.erase(named);
    }
    layer_views.erase(p_layer.get_id());
    layer_ledger->rid_free(p_layer);
}

void NetwMultiplayerCore::layer_forget_all() {
    layer_by_name.clear();
    layer_views.clear();
    layer_ledger->clear();
}

Ref<NetwInterestLayer> NetwMultiplayerCore::interest_layer(
    const StringName &p_name
) {
    return Ref<NetwInterestLayer>(layer_view(layer_open(p_name)));
}

Ref<NetwInterestLayer> NetwMultiplayerCore::interest_layer_named(
    const StringName &p_name
) const {
    return Ref<NetwInterestLayer>(layer_view(layer_named(p_name)));
}

Array NetwMultiplayerCore::interest_layers() const {
    Array out;
    for (const KeyValue<StringName, RID> &row : layer_by_name) {
        const Ref<RefCounted> *held = layer_views.getptr(row.value.get_id());
        if (held != nullptr) {
            out.push_back(*held);
        }
    }
    return out;
}

Ref<NetwInterestDecl> NetwMultiplayerCore::interest_decl_of(const RID &p_entity
) {
    NetwEntity *wrapper
        = Object::cast_to<NetwEntity>(wrapper_of(p_entity).ptr());
    if (wrapper == nullptr) {
        return Ref<NetwInterestDecl>();
    }
    const Ref<RefCounted> facet = wrapper->get_interest();
    NETW_ERR_COND_V(
        facet.is_null(),
        Ref<NetwInterestDecl>(),
        sys::INTEREST,
        "entity %d carries no interest facet, so its declared labels are "
        "unreadable",
        int64_t(p_entity.get_id())
    );
    const Ref<NetwInterestDecl> decl = facet->get(StringName("_decl"));
    NETW_ERR_COND_V(
        decl.is_null(),
        Ref<NetwInterestDecl>(),
        sys::INTEREST,
        "the interest facet of entity %d holds no declaration",
        int64_t(p_entity.get_id())
    );
    return decl;
}

bool NetwMultiplayerCore::interest_has_filter(const RID &p_entity) {
    if (!is_server()) {
        const Ref<NetwInterestDecl> decl = interest_decl_of(p_entity);
        return decl.is_valid() && !decl->labels().is_empty();
    }
    return interest_engine.has_memberships(p_entity.get_id());
}

Array NetwMultiplayerCore::interest_membership_ids(const RID &p_entity) {
    LocalVector<StringName> ordered;
    const Array committed = interest_engine.memberships(p_entity.get_id());
    Array source = committed;
    if (committed.is_empty() && !is_server()) {
        const Ref<NetwInterestDecl> decl = interest_decl_of(p_entity);
        source = decl.is_valid() ? decl->labels() : Array();
    }
    for (int index = 0; index < source.size(); ++index) {
        const StringName id = source[index];
        uint32_t at = ordered.size();
        while (at > 0 && String(id) < String(ordered[at - 1])) {
            --at;
        }
        ordered.insert(at, id);
    }
    Array out;
    for (uint32_t index = 0; index < ordered.size(); ++index) {
        out.push_back(ordered[index]);
    }
    return out;
}

void NetwMultiplayerCore::set_persistence_quit_guard(const Callable &p_guard) {
    persistence.quit_guard = p_guard;
}

void NetwMultiplayerCore::set_persistence_drain(const Callable &p_drain) {
    persistence.drain = p_drain;
}

bool NetwMultiplayerCore::persistence_serves() {
    return !has_multiplayer_peer() || is_server();
}

Ref<NetwPersistenceEngine> NetwMultiplayerCore::persistence_engine_for(
    Object *p_entity
) {
    Ref<NetwEntity> entity = Object::cast_to<NetwEntity>(p_entity);
    if (entity.is_null()) {
        return Ref<NetwPersistenceEngine>();
    }
    Node *owner = entity->get_owner();
    if (owner == nullptr) {
        return Ref<NetwPersistenceEngine>();
    }
    const RID handle = entity->get_rid_handle();
    const Ref<NetwPersistenceEngine> enrolled
        = persistence.engines.engine_of(handle);
    if (enrolled.is_valid()) {
        return enrolled;
    }
    const Dictionary declared = NetwPersistenceEngine::config_of(owner);
    if (declared.is_empty()) {
        return Ref<NetwPersistenceEngine>();
    }
    const Ref<NetwPersistenceEngine> engine = NetwPersistenceEngine::create(
        entity.ptr(),
        declared
    );
    if (engine.is_null()) {
        return engine;
    }
    if (engine->columns_empty()) {
        NETW_WARN(
            sys::TABLE,
            "configure_persistence on '%s' declares no persisted field, so it "
            "saves nothing: mark one with configure_property(...).persisted()",
            String(owner->get_name())
        );
    }
    persistence.engines.enroll(handle, engine);
    engine->lint();
    if (persistence.quit_guard.is_valid() && persistence_serves()) {
        persistence.quit_guard.call();
    }
    const Callable exiting
        = Callable(this, "persistence_owner_exiting").bind(handle);
    if (!owner->is_connected("tree_exiting", exiting)) {
        owner->connect("tree_exiting", exiting, Object::CONNECT_ONE_SHOT);
    }
    return engine;
}

void NetwMultiplayerCore::persistence_tick(double p_delta) {
    persist::snapshot_tick(persistence.engines, p_delta, persistence_serves());
}

void NetwMultiplayerCore::persistence_flush_all() {
    persist::flush_all(persistence.engines, persistence_serves());
}

void NetwMultiplayerCore::persistence_owner_exiting(const RID &p_entity) {
    persist::owner_exiting(
        persistence.engines,
        p_entity,
        persistence_serves()
    );
}

TypedArray<NetwPersistenceEngine>
NetwMultiplayerCore::persistence_live_engines() {
    TypedArray<NetwPersistenceEngine> live;
    const TypedArray<RID> entities = persistence.engines.entities();
    for (int at = 0; at < entities.size(); ++at) {
        const Ref<NetwPersistenceEngine> engine
            = persistence.engines.engine_of(entities[at]);
        if (engine.is_valid() && engine->owner_node() != nullptr) {
            live.push_back(engine);
        }
    }
    return live;
}

void NetwMultiplayerCore::persistence_shutdown() {
    if (persistence.shutting_down || !persistence_serves()) {
        return;
    }
    persistence.shutting_down = true;
    if (!persistence.drain.is_valid()) {
        NETW_ERROR(
            sys::TABLE,
            "no shutdown drain is installed, so %d persisted entities would "
            "have been abandoned mid-write",
            persistence.engines.size()
        );
        return;
    }
    NETW_TRACE(
        sys::TABLE,
        "persistence shutdown draining %d enrolled engine(s)",
        persistence.engines.size()
    );
    persistence.drain.call();
}

NetwMultiplayerCore::SessionState NetwMultiplayerCore::get_state() const {
    return SessionState(session_core.get_state());
}

NetwMultiplayerCore::Role NetwMultiplayerCore::get_role() const {
    return Role(session_core.get_role());
}

bool NetwMultiplayerCore::is_online() const {
    return get_state() == SESSION_STATE_ONLINE;
}

bool NetwMultiplayerCore::is_host() const {
    return session_core.is_server_role();
}

bool NetwMultiplayerCore::is_local_client() const {
    const Role current = get_role();
    return current == ROLE_CLIENT
        || current == ROLE_LISTEN_SERVER;
}

void NetwMultiplayerCore::count_sent(int64_t p_bytes) {
    sent_packets += 1;
    sent_bytes += p_bytes;
}

void NetwMultiplayerCore::count_received(int64_t p_bytes) {
    received_packets += 1;
    received_bytes += p_bytes;
}

void NetwMultiplayerCore::count_state_ack_out() {
    state_acks_out += 1;
}

void NetwMultiplayerCore::count_state_ack_in() {
    state_acks_in += 1;
}

void NetwMultiplayerCore::count_standalone_ack_out() {
    state_acks_out += 1;
    standalone_acks_out += 1;
}

int64_t NetwMultiplayerCore::get_sent_packets() const {
    return sent_packets;
}

int64_t NetwMultiplayerCore::get_sent_bytes() const {
    return sent_bytes;
}

int64_t NetwMultiplayerCore::get_received_packets() const {
    return received_packets;
}

int64_t NetwMultiplayerCore::get_received_bytes() const {
    return received_bytes;
}

int64_t NetwMultiplayerCore::get_state_acks_out() const {
    return state_acks_out;
}

int64_t NetwMultiplayerCore::get_state_acks_in() const {
    return state_acks_in;
}

int64_t NetwMultiplayerCore::get_standalone_acks_out() const {
    return standalone_acks_out;
}

double NetwMultiplayerCore::poll_delta(int64_t p_now_usec) {
    const int64_t previous = last_poll_usec;
    if (previous == p_now_usec) {
        return 0.0;
    }
    last_poll_usec = p_now_usec;
    const double delta
        = previous <= 0 ? 0.0 : double(p_now_usec - previous) / 1000000.0;
    emit_signal(SIG_POLL_STARTED, delta);
    return delta;
}

void NetwMultiplayerCore::advance_frame() {
    frame_counter += 1;
}

int64_t NetwMultiplayerCore::get_frame_counter() const {
    return frame_counter;
}

bool NetwMultiplayerCore::seq_is_fresher(int64_t a, int64_t b) {
    return DatagramSeqBook::is_fresher(uint16_t(a), uint16_t(b));
}

void NetwMultiplayerCore::set_inner(const Ref<SceneMultiplayer> &p_inner) {
    if (inner == p_inner) {
        return;
    }
    if (inner.is_valid()) {
        stop_relay_from(inner.ptr(), SIG_PEER_AUTHENTICATING, 1);
        inner->disconnect(
            SIG_PEER_AUTHENTICATION_FAILED,
            callable_mp(this, &NetwMultiplayerCore::relay_auth_failed)
        );
    }
    inner = p_inner;
    if (inner.is_null()) {
        return;
    }
    relay_from(inner.ptr(), SIG_PEER_AUTHENTICATING, 1);
    inner->connect(
        SIG_PEER_AUTHENTICATION_FAILED,
        callable_mp(this, &NetwMultiplayerCore::relay_auth_failed)
    );
}

void NetwMultiplayerCore::set_session_root(const Callable &p_reader) {
    session_root_reader = p_reader;
}

Node *NetwMultiplayerCore::session_root() const {
    if (!session_root_reader.is_valid()) {
        return nullptr;
    }
    return Object::cast_to<Node>(gd::live_object(session_root_reader.call()));
}

void NetwMultiplayerCore::set_identity_reader(const Callable &p_reader) {
    identity_reader = p_reader;
}

Ref<RefCounted> NetwMultiplayerCore::participant_identity(int64_t p_peer) const {
    if (!identity_reader.is_valid()) {
        return Ref<RefCounted>();
    }
    return identity_reader.call(p_peer);
}

void NetwMultiplayerCore::set_desired_role_reader(const Callable &p_reader) {
    desired_role_reader = p_reader;
}

NetwMultiplayerCore::Role NetwMultiplayerCore::authored_desired_role() const {
    if (!desired_role_reader.is_valid()) {
        return ROLE_LISTEN_SERVER;
    }
    const Variant authored = desired_role_reader.call();
    if (authored.get_type() != Variant::INT) {
        return ROLE_LISTEN_SERVER;
    }
    const int64_t named = authored;
    if (named < ROLE_NONE || named > ROLE_LISTEN_SERVER) {
        return ROLE_LISTEN_SERVER;
    }
    return Role(named);
}

bool NetwMultiplayerCore::presents_as_listen_host() const {
    return get_role() == ROLE_LISTEN_SERVER
        || authored_desired_role() == ROLE_LISTEN_SERVER;
}

int64_t NetwMultiplayerCore::datagram_budget() const {
    constexpr int64_t HEADROOM = 150;
    constexpr int64_t FLOOR = 128;
    if (inner.is_null()) {
        return FLOOR;
    }
    const int64_t ceiling
        = int64_t(inner->get_max_sync_packet_size()) - HEADROOM;
    return ceiling > FLOOR ? ceiling : FLOOR;
}


bool NetwMultiplayerCore::channel_aggregates(
    int64_t p_channel,
    bool p_requested
) const {
    if (p_channel < 0 || p_channel > 255) {
        return false;
    }
    return channels.aggregates(uint8_t(p_channel), p_requested);
}

PackedByteArray NetwMultiplayerCore::frame_pack(
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

Error NetwMultiplayerCore::send_to(
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

    const bool connected
        = inner.is_valid() && inner->get_multiplayer_peer().is_valid();
    if (connected && p_peer == get_unique_id()) {
        Object *plane = replication_plane();
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
        plane->call(
            "_dispatch",
            p_route,
            p_comp,
            p_channel,
            p_payload,
            p_path,
            p_peer,
            p_reliable
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

    const bool aggregating = clock_engine().get_configured()
        && channel_aggregates(p_channel, p_batched);
    if (!aggregating) {
        send_datagram(p_peer, framed, p_reliable);
        return OK;
    }

    const int64_t seq = carrier_append(p_peer, framed, p_reliable);
    if (seq < 0) {
        return OK;
    }
    Object *plane = replication_plane();
    NETW_ERR_COND_V(
        plane == nullptr,
        ERR_UNCONFIGURED,
        sys::TRANSPORT,
        "peer %d was staged at seq %d with no dispatcher to tell",
        int(p_peer),
        int(seq)
    );
    plane->call("_note_staged", p_peer, seq);
    return OK;
}

int64_t NetwMultiplayerCore::send_datagram(
    int64_t p_peer,
    const PackedByteArray &p_payload,
    bool p_reliable
) {
    if (p_payload.is_empty() || inner.is_null()) {
        return -1;
    }

    if (p_peer != 0 && netw::gd::api_peer_ids(inner).find(int32_t(p_peer)) < 0) {
        return -1;
    }

    const Ref<NetwCarrierDatagram> datagram
        = frame_datagram(p_peer, p_payload, p_reliable);
    inner->send_bytes(
        datagram->bytes,
        int(p_peer),
        p_reliable ? MultiplayerPeer::TRANSFER_MODE_RELIABLE
                   : MultiplayerPeer::TRANSFER_MODE_UNRELIABLE,
        0
    );
    if (plane.wants(EventPlane::DATAGRAM_SENT, 0)) {
        EventPlane::Emission fact(
            EventPlane::DATAGRAM_SENT,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.peer = p_peer;
        Dictionary detail;
        detail["size"] = datagram->bytes.size();
        detail["seq"] = datagram->seq;
        detail["reliable"] = p_reliable;
        fact.detail = detail;
        plane.emit(fact);
    }
    return datagram->seq;
}

int64_t NetwMultiplayerCore::carrier_append(
    int64_t p_peer,
    const PackedByteArray &p_frame,
    bool p_reliable
) {
    const PackedByteArray owed = carrier->append(
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

PackedInt64Array NetwMultiplayerCore::carrier_flush() {
    PackedInt64Array staged;
    for (const int32_t peer : carrier->peers(false)) {
        const int64_t seq
            = send_datagram(peer, carrier->take(peer, false), false);
        if (seq >= 0) {
            staged.push_back(peer);
            staged.push_back(seq);
        }
    }
    for (const int32_t peer : carrier->peers(true)) {
        send_datagram(peer, carrier->take(peer, true), true);
    }
    return staged;
}

void NetwMultiplayerCore::carrier_clear() {
    carrier->clear();
}

int64_t NetwMultiplayerCore::carrier_pending(int64_t p_peer, bool p_reliable)
    const {
    return carrier->pending(p_peer, p_reliable);
}

Ref<NetwCarrierDatagram> NetwMultiplayerCore::frame_datagram(
    int64_t p_peer,
    const PackedByteArray &p_payload,
    bool p_reliable
) {
    Ref<NetwCarrierDatagram> out;
    out.instantiate();
    count_sent(p_payload.size());

    int64_t seq = -1;
    int64_t ack = -1;
    if (!p_reliable) {
        seq = next_send_seq(p_peer);
        if (has_inbound_seq(p_peer)) {
            ack = inbound_seq(p_peer);
            count_state_ack_out();
            note_echoed_seq(p_peer, ack);
        }
    }
    out->bytes = NetwCarrierFrame::build(p_payload, p_reliable, seq, ack);
    out->seq = seq;
    return out;
}

int64_t NetwMultiplayerCore::next_send_seq(int64_t p_peer) {
    return seq_book.next_send_seq(p_peer);
}

bool NetwMultiplayerCore::has_inbound_seq(int64_t p_peer) const {
    return seq_book.has_inbound(p_peer);
}

int64_t NetwMultiplayerCore::inbound_seq(int64_t p_peer) const {
    return seq_book.inbound_seq(p_peer);
}

bool NetwMultiplayerCore::note_inbound_seq(int64_t p_peer, int64_t p_seq) {
    return seq_book.note_inbound(p_peer, uint16_t(p_seq));
}

bool NetwMultiplayerCore::note_peer_ack(int64_t p_peer, int64_t p_ack) {
    const bool advanced = seq_book.note_peer_ack(p_peer, uint16_t(p_ack));
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

int64_t NetwMultiplayerCore::peer_ack(int64_t p_peer) const {
    return seq_book.peer_ack(p_peer);
}

void NetwMultiplayerCore::note_echoed_seq(int64_t p_peer, int64_t p_seq) {
    seq_book.note_echoed(p_peer, uint16_t(p_seq));
}

PackedInt64Array NetwMultiplayerCore::peers_owed_echo() const {
    return seq_book.peers_owed_echo();
}

void NetwMultiplayerCore::forget_peer_seqs(int64_t p_peer) {
    seq_book.forget_peer(p_peer);
}

void NetwMultiplayerCore::clear_seq_books() {
    seq_book.clear();
}

bool NetwMultiplayerCore::counts_verdict(int64_t p_verdict) {
    return GateVerdictBook::counts(p_verdict);
}

bool NetwMultiplayerCore::connect_once(
    Signal p_signal,
    const Callable &p_callback,
    int64_t p_flags
) {
    if (p_signal.is_connected(p_callback)) {
        return true;
    }
    const int64_t verdict = int64_t(p_signal.connect(p_callback, p_flags));
    if (verdict == int64_t(OK)) {
        return true;
    }
    count_verdict(ERR_UNAVAILABLE, 0);
    NETW_ERROR(
        sys::SESSION,
        "connecting '%s' answered %d, so the edge is not wired",
        String(p_signal.get_name()),
        int(verdict)
    );
    return false;
}

bool NetwMultiplayerCore::count_verdict(int64_t p_verdict, int64_t p_route) {
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

int64_t NetwMultiplayerCore::verdict_total(int64_t p_verdict) const {
    return verdict_book.total(p_verdict);
}

bool NetwMultiplayerCore::claim_verdict_warning(
    int64_t p_verdict,
    int64_t p_route
) {
    return verdict_book.claim_warning(p_verdict, p_route);
}

bool NetwMultiplayerCore::warn_verdict(int64_t p_verdict, int64_t p_route) {
    count_verdict(p_verdict, p_route);
    return claim_verdict_warning(p_verdict, p_route);
}

int64_t NetwMultiplayerCore::sink_verdict(int64_t p_verdict, int64_t p_route) {
    switch (GateVerdictBook::report_of(p_verdict)) {
        case GateVerdictBook::QUIET:
            break;
        case GateVerdictBook::WARN:
            if (verdict_book.claim_warning(p_verdict, p_route)) {
                NETW_WARN(
                    sys::TRANSPORT,
                    "refused carrier input with error %d on route %d",
                    int(p_verdict),
                    int(p_route)
                );
            }
            break;
        case GateVerdictBook::DEFECT:
            NETW_ERROR(
                sys::TRANSPORT,
                "a carrier sink answered %d, which is not a verdict",
                int(p_verdict)
            );
            break;
    }
    if (p_verdict != OK) {
        count_verdict(p_verdict, p_route);
    }
    return p_verdict;
}

int64_t NetwMultiplayerCore::stage_verdict(
    int64_t p_stage,
    int64_t p_verdict,
    int64_t p_route
) {
    if (plane.wants(p_stage, p_route)) {
        EventPlane::Emission fact(
            p_stage,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.route = p_route;
        fact.verdict = p_verdict;
        plane.emit(fact);
    }
    return sink_verdict(p_verdict, p_route);
}

void NetwMultiplayerCore::clear_verdicts() {
    verdict_book.clear();
}

void NetwMultiplayerCore::settle_schedule(
    const Callable &p_fn,
    const StringName &p_key
) {
    settle_queue.schedule(p_fn, p_key);
}

void NetwMultiplayerCore::settle_schedule_after(
    const Callable &p_fn,
    const StringName &p_key,
    int p_pumps
) {
    settle_queue.schedule_after(p_fn, p_key, p_pumps);
}

void NetwMultiplayerCore::settle_advance() {
    settle_queue.advance_windows();
}

void NetwMultiplayerCore::settle_cancel(const StringName &p_key) {
    settle_queue.cancel(p_key);
}

PackedStringArray NetwMultiplayerCore::settle_drain() {
    plane.drain_staged();
    return settle_queue.drain();
}

void NetwMultiplayerCore::settle_clear() {
    settle_queue.clear();
}

int NetwMultiplayerCore::settle_pending() const {
    return settle_queue.size();
}

bool NetwMultiplayerCore::settle_has_key(const StringName &p_key) const {
    return settle_queue.has(p_key);
}

int NetwMultiplayerCore::settle_max_passes() {
    return SettleQueue::MAX_PASSES;
}

Ref<SchemaCore> NetwMultiplayerCore::get_schema_core() const {
    return schema_core;
}

RID NetwMultiplayerCore::schema_create(const StringName &p_name) {
    if (p_name == StringName()) {
        return RID();
    }
    const RID existing = schema_core->find(p_name);
    if (existing.is_valid()) {
        schema_core->declare(existing, p_name);
        return existing;
    }
    const RID minted = schemas->rid_create();
    schema_core->declare(minted, p_name);
    return minted;
}

int NetwMultiplayerCore::schema_add_column(
    const RID &p_schema,
    const StringName &p_key,
    int p_type,
    int p_stride
) {
    return schema_core->add_column(p_schema, p_key, p_type, p_stride);
}

void NetwMultiplayerCore::schema_set_column_quantizer(
    const RID &p_schema,
    int p_column,
    const Ref<NetwQuantize> &p_quantizer
) {
    schema_core->set_column_quantizer(p_schema, p_column, p_quantizer);
}

Error NetwMultiplayerCore::schema_seal(const RID &p_schema) {
    return schema_core->seal(p_schema);
}

RID NetwMultiplayerCore::schema_find(const StringName &p_name) const {
    return schema_core->find(p_name);
}

int NetwMultiplayerCore::schema_get_hash(const RID &p_schema) const {
    return schema_core->hash_of(p_schema);
}

int NetwMultiplayerCore::schema_get_column_count(const RID &p_schema) const {
    return schema_core->column_count(p_schema);
}

StringName NetwMultiplayerCore::schema_get_column_key(
    const RID &p_schema,
    int p_column
) const {
    return schema_core->column_key(p_schema, p_column);
}

int NetwMultiplayerCore::schema_get_column_type(
    const RID &p_schema,
    int p_column
) const {
    return schema_core->column_type(p_schema, p_column);
}

int NetwMultiplayerCore::schema_get_column_stride(
    const RID &p_schema,
    int p_column
) const {
    return schema_core->column_stride(p_schema, p_column);
}

Ref<TableCore> NetwMultiplayerCore::get_table_core() const {
    return table_core;
}

RID NetwMultiplayerCore::table_create(const RID &p_schema) {
    const Ref<SchemaRecord> record = schema_core->record_of(p_schema);
    if (record.is_null() || !record->get_sealed()) {
        return RID();
    }
    const RID *bound = table_by_name.getptr(record->get_name());
    if (bound && bound->is_valid()) {
        return *bound;
    }
    const RID minted = tables->rid_create();
    if (table_core->declare(minted, record) != OK) {
        return RID();
    }
    table_by_name[record->get_name()] = minted;
    table_schema[int64_t(minted.get_id())] = p_schema;
    return minted;
}

RID NetwMultiplayerCore::table_get_schema(const RID &p_table) const {
    const RID *bound = table_schema.getptr(int64_t(p_table.get_id()));
    return bound ? *bound : RID();
}

void NetwMultiplayerCore::table_set_param(
    const RID &p_table,
    int p_param,
    const Variant &p_value
) {
    if (p_param == TABLE_PARAM_RELIABLE) {
        table_core->set_reliable(p_table, bool(p_value));
    }
}

RID NetwMultiplayerCore::table_find(const StringName &p_name) const {
    const RID *bound = table_by_name.getptr(p_name);
    return bound ? *bound : RID();
}

int NetwMultiplayerCore::table_get_wire_hash(const RID &p_table) const {
    return table_core->schema_hash(p_table);
}

Error NetwMultiplayerCore::table_write_routes(
    const RID &p_table,
    const PackedInt64Array &p_routes
) {
    return table_core->write_routes(p_table, p_routes);
}

Error NetwMultiplayerCore::table_write_column(
    const RID &p_table,
    int p_column,
    const Variant &p_data
) {
    return table_core->write_column(p_table, p_column, p_data);
}

Error NetwMultiplayerCore::table_commit(const RID &p_table) {
    const int64_t tick = clock_engine().get_tick();
    const Error verdict = table_core->commit(p_table, tick);
    if (plane.wants(EventPlane::TABLE_COMMIT, 0)) {
        EventPlane::Emission fact(EventPlane::TABLE_COMMIT, EventPlane::AFTER, tick);
        fact.verdict = verdict;
        Dictionary detail;
        detail["stage"] = "table";
        detail["rows"] = table_read_routes(p_table).size();
        fact.detail = detail;
        plane.emit(fact);
    }
    return verdict;
}

PackedInt64Array NetwMultiplayerCore::table_read_routes(
    const RID &p_table
) const {
    return table_core->read_routes(p_table);
}

Variant NetwMultiplayerCore::table_read_column(
    const RID &p_table,
    int p_column
) const {
    return table_core->read_column(p_table, p_column);
}

PackedInt64Array NetwMultiplayerCore::table_read_births(
    const RID &p_table
) const {
    return table_core->read_births(p_table);
}

PackedInt64Array NetwMultiplayerCore::table_read_deaths(
    const RID &p_table
) const {
    return table_core->read_deaths(p_table);
}

int NetwMultiplayerCore::table_get_row(
    const RID &p_table,
    int64_t p_route
) const {
    return table_core->row_of(p_table, p_route);
}

PackedInt32Array NetwMultiplayerCore::table_get_rows(
    const RID &p_table,
    const PackedInt64Array &p_routes
) const {
    return table_core->rows_of(p_table, p_routes);
}

int64_t NetwMultiplayerCore::table_get_tick(const RID &p_table) const {
    return table_core->tick_of(p_table);
}

Ref<NetwEffectLedger> NetwMultiplayerCore::get_effect_ledger() const {
    return effects;
}

void NetwMultiplayerCore::effect_arm(
    const StringName &p_key,
    const Callable &p_revert,
    int p_timeout_ticks
) {
    const int ttl
        = p_timeout_ticks > 0 ? p_timeout_ticks : EFFECT_TIMEOUT_TICKS;
    effects->arm(p_key, p_revert, clock_engine().get_tick() + ttl);
}

bool NetwMultiplayerCore::effect_watch(
    const StringName &p_key,
    const Callable &p_confirmed,
    const Callable &p_denied
) {
    return effects->watch(p_key, p_confirmed, p_denied);
}

void NetwMultiplayerCore::effect_adopt(const StringName &p_key) {
    effects->adopt(p_key);
}

void NetwMultiplayerCore::effect_discard(const StringName &p_key) {
    effects->discard(p_key);
}

bool NetwMultiplayerCore::effect_pending(const StringName &p_key) const {
    return effects->pending(p_key);
}

int64_t NetwMultiplayerCore::effect_count() const {
    return effects->count();
}

int64_t NetwMultiplayerCore::event_watch(
    const PackedInt64Array &p_events,
    const Dictionary &p_target,
    const Dictionary &p_predicate,
    const Callable &p_sink,
    const Dictionary &p_opts
) {
    return plane.watch(p_events, p_target, p_predicate, p_sink, p_opts);
}

bool NetwMultiplayerCore::event_unwatch(int64_t p_id) {
    return plane.unwatch(p_id);
}

Array NetwMultiplayerCore::event_watches() const {
    return plane.watches();
}

Array NetwMultiplayerCore::event_ring(int64_t p_route) {
    return plane.ring(p_route);
}

void NetwMultiplayerCore::event_ring_clear(int64_t p_route) {
    plane.ring_clear(p_route);
}

void NetwMultiplayerCore::event_arm(bool p_enabled) {
    plane.set_armed(p_enabled);
}

bool NetwMultiplayerCore::event_wants(int64_t p_event, int64_t p_route) const {
    return plane.wants(p_event, p_route);
}

void NetwMultiplayerCore::event_emit(
    int64_t p_event,
    int64_t p_route,
    const Dictionary &p_detail,
    const StringName &p_entity_id,
    int64_t p_peer,
    int64_t p_verdict,
    const Dictionary &p_model
) {
    if (!plane.wants(p_event, p_route)) {
        return;
    }
    EventPlane::Emission fact(
        p_event,
        EventPlane::AFTER,
        clock_engine().get_tick()
    );
    fact.route = p_route;
    fact.detail = p_detail;
    fact.entity_id = p_entity_id;
    fact.peer = p_peer;
    fact.verdict = p_verdict;
    fact.model = p_model;
    plane.emit(fact);
}

bool NetwMultiplayerCore::is_coroutine(const Variant &p_value) {
    if (p_value.get_type() != Variant::OBJECT) {
        return false;
    }
    Object *object = p_value;
    return object != nullptr
        && object->get_class() == String("GDScriptFunctionState");
}

bool NetwMultiplayerCore::overrides_seam(
    const Ref<Script> &p_script,
    const StringName &p_base_name,
    const StringName &p_seam
) {
    if (p_script.is_null()) {
        return false;
    }
    const ObjectID asked = netw::gd::instance_id(p_script.ptr());
    if (asked != seam_script) {
        seam_overrides.clear();
        seam_script = asked;
    }
    const bool *cached = seam_overrides.getptr(p_seam);
    if (cached != nullptr) {
        return *cached;
    }
    Ref<Script> base = p_script;
    while (base.is_valid() && base->get_global_name() != p_base_name) {
        base = base->get_base_script();
    }
    const bool overridden = base.is_valid()
        && netw::gd::script_method_count(p_script, p_seam)
            > netw::gd::script_method_count(base, p_seam);
    seam_overrides[p_seam] = overridden;
    return overridden;
}

void NetwMultiplayerCore::forget_seam_overrides() {
    seam_overrides.clear();
    seam_script = ObjectID();
}

void NetwMultiplayerCore::seam_entered(
    const StringName &p_seam,
    int64_t p_event,
    int64_t p_route,
    const Dictionary &p_detail
) {
    if (!plane.wants(p_event, p_route)) {
        return;
    }
    EventPlane::Emission fact(
        p_event,
        EventPlane::BEFORE,
        clock_engine().get_tick()
    );
    fact.route = p_route;
    Dictionary detail = p_detail.duplicate();
    detail["seam"] = p_seam;
    fact.detail = detail;
    plane.emit(fact);
}

Variant NetwMultiplayerCore::seam_settled(
    const StringName &p_seam,
    int64_t p_event,
    int64_t p_route,
    const Variant &p_result,
    const Variant &p_fallback
) {
    const bool suspended = is_coroutine(p_result);
    const bool mistyped = !suspended
        && p_fallback.get_type() != Variant::NIL
        && p_result.get_type() != p_fallback.get_type();
    if (suspended || mistyped) {
        if (!misused_seams.has(p_seam)) {
            misused_seams.insert(p_seam);
            NETW_ERROR(
                sys::EVENT,
                "seam %s answered with %s, so the default answer stands",
                String(p_seam),
                suspended ? String("a coroutine") : String("the wrong type")
            );
        }
        EventPlane::Emission misuse(
            EventPlane::SEAM_MISUSE,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        misuse.route = p_route;
        misuse.verdict = ERR_INVALID_DATA;
        Dictionary detail;
        detail["seam"] = p_seam;
        detail["stage"] = "seam";
        detail["reason"] = suspended ? "coroutine" : "type";
        misuse.detail = detail;
        plane.emit(misuse);
        return p_fallback;
    }
    if (plane.wants(p_event, p_route)) {
        EventPlane::Emission fact(
            p_event,
            EventPlane::AFTER,
            clock_engine().get_tick()
        );
        fact.route = p_route;
        fact.verdict = p_result.get_type() == Variant::INT ? int64_t(p_result)
                                                           : int64_t(OK);
        Dictionary detail;
        detail["seam"] = p_seam;
        fact.detail = detail;
        plane.emit(fact);
    }
    return p_result;
}

void NetwMultiplayerCore::effect_sweep(int64_t p_tick) {
    effects->sweep(p_tick);
}

}
