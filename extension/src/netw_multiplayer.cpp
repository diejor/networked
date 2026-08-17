#include "netw/netw_multiplayer.hpp"

#include "netw/entity.hpp"
#include "netw/entity_identity.hpp"
#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/packed_scene.hpp"
#include "godot/utility.hpp"
#include "godot/viewport.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/synchronizers.hpp"

using namespace godot;
using namespace netw;

namespace netw {

// The session's own spelling of each fact a plane core announces. One literal
// per name, read by the ADD_SIGNAL that declares it and by the relay that
// republishes it, so the two cannot drift apart.
const char *SIG_BEFORE_TICK = "before_tick";
const char *SIG_ON_TICK = "on_tick";
const char *SIG_AFTER_TICK = "after_tick";
const char *SIG_BEFORE_TICK_LOOP = "before_tick_loop";
const char *SIG_AFTER_TICK_LOOP = "after_tick_loop";
const char *SIG_CLOCK_SYNCHRONIZED = "clock_synchronized";
const char *SIG_DISPLAY_OFFSET_INSUFFICIENT = "display_offset_insufficient";
const char *SIG_STABILITY_CHANGED = "stability_changed";
const char *SIG_STATE_CHANGED = "state_changed";
const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_ENDED = "session_ended";
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
// The participant's own name for what the session publishes as its local
// scene change. The two differ because "local" is the session's word: the
// participant only knows it changed scene, not that it is this peer.
const char *SIG_SCENE_CHANGED = "scene_changed";

// The script a service is registered under when the caller named none: its
// own. A service with no script cannot be keyed at all.
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
        D_METHOD("get_session_core"),
        &NetwMultiplayerCore::get_session_core
    );
    ClassDB::bind_method(
        D_METHOD("get_clock_core"),
        &NetwMultiplayerCore::get_clock_core
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
        D_METHOD("get_interest_engine"),
        &NetwMultiplayerCore::get_interest_engine
    );
    ClassDB::bind_method(
        D_METHOD("get_channel_book"),
        &NetwMultiplayerCore::get_channel_book
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
        D_METHOD("verdict_total", "verdict"),
        &NetwMultiplayerCore::verdict_total
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
            "session_core",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSessionCore"
        ),
        "",
        "get_session_core"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "clock_core",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwClockCore"
        ),
        "",
        "get_clock_core"
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
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "interest_engine",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwInterestEngine"
        ),
        "",
        "get_interest_engine"
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
    ClassDB::bind_static_method(
        "NetwMultiplayerCore",
        D_METHOD("scene_install_level", "container", "level"),
        &NetwMultiplayerCore::scene_install_level
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
    session_core.instantiate();
    clock_core.instantiate();
    scene_core.instantiate();
    display_book.instantiate();
    scene_request_window.instantiate();
    interest_engine.instantiate();

    relay_from(clock_core.ptr(), SIG_BEFORE_TICK, 2);
    relay_from(clock_core.ptr(), SIG_ON_TICK, 2);
    relay_from(clock_core.ptr(), SIG_AFTER_TICK, 2);
    relay_from(clock_core.ptr(), SIG_BEFORE_TICK_LOOP, 0);
    relay_from(clock_core.ptr(), SIG_AFTER_TICK_LOOP, 0);
    relay_from(clock_core.ptr(), SIG_CLOCK_SYNCHRONIZED, 0);
    relay_from(clock_core.ptr(), SIG_DISPLAY_OFFSET_INSUFFICIENT, 1);
    relay_from(clock_core.ptr(), SIG_STABILITY_CHANGED, 1);
    relay_from(session_core.ptr(), SIG_STATE_CHANGED, 2);
    relay_from(session_core.ptr(), SIG_SESSION_ENTERED, 0);
    relay_from(session_core.ptr(), SIG_SESSION_ENDED, 0);

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
}

uint8_t NetwMultiplayerCore::declared_channel(const char *p_name) const {
    const wire::ChannelDecl *decl
        = channels.find_channel_by_name(StringName(p_name));
    return decl ? decl->id : 0;
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
    // The local announcement precedes the general one, so a listener that
    // handles both sees itself arrive before it sees the roster grow.
    if (p_peer == get_unique_id()) {
        // Bound before the announcement, so a listener that reacts by changing
        // scene has its change published rather than dropped.
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
    // Through the record, because it is what carries the handle itself: an id
    // alone cannot be turned back into a RID.
    const Ref<NetwEntityRecord> *record = wrapper_records.getptr(*id);
    return record ? (*record)->get_handle() : RID();
}

namespace {

Callable &wrapper_mint() {
    static Callable factory;
    return factory;
}

} // namespace

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
    // A node the tree already holds is past the moment a record could be
    // provisioned for it, so it answers nothing rather than minting one its
    // ancestors would never learn about.
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
    // Its own, through the record rather than the index, because an entity
    // this session has not adopted still belongs to a scene: its own.
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
        // Node.reparent needs a parent to move away from, so an owner that
        // has none is added rather than moved. Out of the tree the remove and
        // the add are also the whole of what a move can preserve, which is why
        // the two paths are not one.
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

} // namespace

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
        // Nobody to ask, so the request is decided where it was made.
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

} // namespace

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
    // A participant with no join has not been accepted, so it names no player
    // to build one for.
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
    // Unkeyed, because two moves in one cascade are two discontinuities and a
    // consumer resetting its smoothing owes each of them an answer.
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
    out["layers"] = interest_engine->memberships(entity.get_id());
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
        // A move that names a destination position is a discontinuity, which
        // is the one thing about a reparent a consumer has to answer for.
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
    if (!scene_request_window->exceeded(peer, now_msec)) {
        return false;
    }
    count_verdict(int64_t(ERR_BUSY), 0);
    if (claim_verdict_warning(int64_t(ERR_BUSY), 0)) {
        NETW_WARN(sys::SCENE, "peer %d exceeded the scene request rate", peer);
    }
    return true;
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
    // A peer with no transport represents nobody, which is the same answer a
    // record carrying peer 0 gives: unowned rather than mine.
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
    // The record keeps its tombstone and its handle. The wrapper leaves the
    // live book and waits in the retired one, so an engine's next delta still
    // resolves the entity it is naming.
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
    // Only when the entity DYING is the one representing this peer. A scene
    // change binds the replacement before retiring the original, so asking by
    // route instead would drop a local player that had already moved on.
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
    // A reparent leaves the tree and comes back, so the move in flight is what
    // separates it from a teardown.
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
        // The model the terminal event hands on. This is the last edge that
        // reads a whole entity, since a death knows a route and nothing else.
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
        // A kick names its target as well as its reason, so a payload that is
        // not the pair is a frame this session cannot act on rather than one
        // it acts on with a default target.
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

Ref<NetwCarrierFrame> NetwMultiplayerCore::receive_header(
    int64_t p_peer,
    const PackedByteArray &p_packet
) {
    const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(p_packet);
    if (header->kind == NetwCarrierFrame::FOREIGN) {
        emit_signal(SIG_PEER_PACKET, p_peer, p_packet);
        return header;
    }
    // A malformed datagram is ours and unreadable, so its bytes were never
    // received in any sense a counter should report.
    if (header->kind == NetwCarrierFrame::MALFORMED) {
        if (plane.wants(EventPlane::DATAGRAM_MALFORMED, 0)) {
            EventPlane::Emission fact(
                EventPlane::DATAGRAM_MALFORMED,
                EventPlane::AFTER,
                clock_core->get_tick()
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
            clock_core->get_tick()
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

Ref<NetwSessionCore> NetwMultiplayerCore::get_session_core() const {
    return session_core;
}

Ref<NetwClockCore> NetwMultiplayerCore::get_clock_core() const {
    return clock_core;
}

Ref<NetwSceneCore> NetwMultiplayerCore::get_scene_core() const {
    return scene_core;
}

Ref<NetwDisplayBook> NetwMultiplayerCore::get_display_book() const {
    return display_book;
}

Ref<NetwInterestEngine> NetwMultiplayerCore::get_interest_engine() const {
    return interest_engine;
}

Ref<NetwChannelBook> NetwMultiplayerCore::get_channel_book() const {
    return channel_book;
}

void NetwMultiplayerCore::reset_interest() {
    interest_engine->clear();
}

void NetwMultiplayerCore::interest_sync_record(Object *p_wrapper) {
    NetwEntity *current = Object::cast_to<NetwEntity>(p_wrapper);
    LocalVector<int64_t> keys;
    LocalVector<int64_t> routes;
    HashSet<int64_t> seen;
    while (current != nullptr) {
        const RID handle = liveness_adopt(current);
        const int64_t key = handle.get_id();
        if (!NetwInterestEngine::is_key(key) || seen.has(key)) {
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
        interest_engine->set_parent(key, parent_key);
        int64_t route = routes[index];
        if (route <= 0) {
            route = interest_engine->order_route_for(key);
        }
        interest_engine->set_order_key(
            key,
            int(keys.size() - 1 - index),
            int(route)
        );
    }
}

NetwSessionCore::State NetwMultiplayerCore::get_state() const {
    return session_core->get_state();
}

NetwSessionCore::Role NetwMultiplayerCore::get_role() const {
    return session_core->get_role();
}

bool NetwMultiplayerCore::is_online() const {
    return get_state() == NetwSessionCore::STATE_ONLINE;
}

bool NetwMultiplayerCore::is_host() const {
    return session_core->is_server_role();
}

bool NetwMultiplayerCore::is_local_client() const {
    const NetwSessionCore::Role current = get_role();
    return current == NetwSessionCore::ROLE_CLIENT
        || current == NetwSessionCore::ROLE_LISTEN_SERVER;
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
    // Marking the poll is what a poll starting IS, so the announcement is here
    // rather than at a caller that could forget it or make it twice. A second
    // reader in the same moment marked nothing and announces nothing.
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
    // The handshake edges belong to the transport, so the session republishes
    // them rather than deciding them. A transport swap moves both with it,
    // which is why they bind here and not at construction.
    relay_from(inner.ptr(), SIG_PEER_AUTHENTICATING, 1);
    inner->connect(
        SIG_PEER_AUTHENTICATION_FAILED,
        callable_mp(this, &NetwMultiplayerCore::relay_auth_failed)
    );
}

int64_t NetwMultiplayerCore::datagram_budget() const {
    // The carrier header, the transport's RAW command byte and its relay
    // wrapper all ride in front of the frames, so the budget is the ceiling
    // less room for them.
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

int64_t NetwMultiplayerCore::send_datagram(
    int64_t p_peer,
    const PackedByteArray &p_payload,
    bool p_reliable
) {
    if (p_payload.is_empty() || inner.is_null()) {
        return -1;
    }

    // A peer that left mid-poll can still sit in a recipient list drawn from
    // liveness books that trail the connection by a cleanup signal. The
    // transport treats a departed target as a bug, so the carrier drops it.
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
            clock_core->get_tick()
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
            // This datagram carries the echo, so the end-of-tick standalone
            // pass owes this peer nothing.
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
            clock_core->get_tick()
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

bool NetwMultiplayerCore::count_verdict(int64_t p_verdict, int64_t p_route) {
    const bool counted = verdict_book.count(p_verdict);
    if (plane.wants(EventPlane::VERDICT, p_route)) {
        EventPlane::Emission fact(
            EventPlane::VERDICT,
            EventPlane::AFTER,
            clock_core->get_tick()
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
            clock_core->get_tick()
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
    const int64_t tick = clock_core->get_tick();
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
    effects->arm(p_key, p_revert, clock_core->get_tick() + ttl);
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
        clock_core->get_tick()
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
        clock_core->get_tick()
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
            clock_core->get_tick()
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
            clock_core->get_tick()
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

} // namespace netw
