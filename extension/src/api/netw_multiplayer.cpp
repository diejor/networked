#include "netw/api/netw_multiplayer.hpp"

#include "godot/transport_state.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/scene_handle.hpp"

#include "netw/api/participant.hpp"
#include "netw/api/server_info.hpp"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/packed_scene.hpp"
#include "godot/physics_server.hpp"
#include "godot/project_settings.hpp"
#include "godot/rendering_server.hpp"
#include "godot/resource.hpp"
#include "godot/resource_loader.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/auth_protocol.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/connect/browse_list.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/directory_transport.hpp"
#include "netw/entity/identity.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

StringName &displaced_default_interface() {
    static StringName held;
    return held;
}

godot::LocalVector<godot::ObjectID> live_registry;

constexpr double POLL_PUMP_RATE = 60.0;

const char *SIG_CLOCK_AFTER_TICK = "clock_after_tick";
const char *SIG_CLOCK_PONG_RECEIVED = "clock_pong_received";
const char *SIG_CLOCK_TICKRATE_MISMATCH = "clock_tickrate_mismatch";
const char *SIG_CONNECTED_TO_SERVER = "connected_to_server";
const char *SIG_CONNECTION_FAILED = "connection_failed";
const char *SIG_ENTITY_DEAD = "entity_dead";
const char *SIG_ENTITY_HIDDEN = "entity_hidden";
const char *SIG_ENTITY_LIVE = "entity_live";
const char *SIG_PARTICIPANT_JOINED = "participant_joined";
const char *SIG_PARTICIPANT_LOCAL_JOINED = "participant_local_joined";
const char *SIG_PEER_AUTHENTICATING = "peer_authenticating";
const char *SIG_PEER_AUTHENTICATION_FAILED = "peer_authentication_failed";
const char *SIG_PEER_KICKED = "peer_kicked";
const char *SIG_PEER_KICK_REQUESTED = "peer_kick_requested";
const char *SIG_PEER_PACKET = "peer_packet";
const char *SIG_SCENE_CHANGED = "scene_changed";
const char *SIG_SCENE_LOCAL_CHANGED = "scene_local_changed";
const char *SIG_SCENE_LOCAL_PLAYER_CHANGED = "scene_local_player_changed";
const char *SIG_SERVER_DISCONNECTED = "server_disconnected";
const char *SIG_SERVICE_REGISTERED = "service_registered";
const char *SIG_SERVICE_UNREGISTERED = "service_unregistered";
const char *SIG_SESSION_DISCONNECT_REQUESTED = "session_disconnect_requested";
const char *SIG_SESSION_ENDED = "session_ended";
const char *SIG_SESSION_JOIN_FAILED = "session_join_failed";
constexpr int POLLS_BEFORE_TURNED_AWAY_PEER_DROPS = 2;
const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_POLL_STARTED = "session_poll_started";
const char *SIG_SESSION_RECLAIMED = "session_reclaimed";
const char *SIG_SESSION_SERVER_DISCONNECTING = "session_server_disconnecting";
const char *SIG_SESSION_STATE_CHANGED = "session_state_changed";
const char *SIG_SESSION_TREE_PAUSED = "session_tree_paused";
const char *SIG_SESSION_TREE_UNPAUSED = "session_tree_unpaused";
const char *LAGGY_PEER_CLASS = "LaggyMultiplayerPeer";
const char *SIG_SCENE_STARTUP_SPAWNED = "scene_startup_spawned";

} // namespace

Object *script_of(Object *p_service) {
    if (p_service == nullptr) {
        return nullptr;
    }
    const Variant script = p_service->get_script();
    return script.get_type() == Variant::OBJECT ? static_cast<Object *>(script)
                                                : nullptr;
}

NetwMultiplayer::NetwMultiplayer() {
    Ref<SceneMultiplayer> transport;
    transport.instantiate();
    transport->set_root_path(NodePath("/root"));
    session_set_inner(transport);
    liveness_core.instantiate();
    table_core.instantiate();
    clock.sink.bind(this);
    scene_core.instantiate();
    display_book.instantiate();
    prediction_engine.bind_session(this);
    predict_runner.seat_core(this);
    connect(
        StringName("clock_before_tick_loop"),
        callable_mp(this, &NetwMultiplayer::before_frame_step)
    );
    connect(
        StringName("clock_on_tick"),
        callable_mp(this, &NetwMultiplayer::tick_step)
    );
    connect(
        StringName("clock_after_tick_loop"),
        callable_mp(this, &NetwMultiplayer::frame_step)
    );

    display_hooks.sync_intervals
        = callable_mp(this, &NetwMultiplayer::display_compute_sync_intervals);
    display_hooks.authors_streams
        = callable_mp(this, &NetwMultiplayer::display_default_authors_streams);
    display_hooks.role_reader
        = callable_mp(this, &NetwMultiplayer::display_default_role);
    display_hooks.chase_clamp
        = callable_mp(this, &NetwMultiplayer::display_default_chase_clamp);
    display_hooks.chase_hook_binder
        = callable_mp(this, &NetwMultiplayer::display_default_chase_hook);
    interest_awareness_send
        = callable_mp(this, &NetwMultiplayer::interest_send_awareness);
    interest_compat_refresh
        = callable_mp(this, &NetwMultiplayer::interest_refresh_compat_intents);
    scene_participant_edge
        = callable_mp(this, &NetwMultiplayer::scene_report_participant);
    display_hooks.display_lane
        = callable_mp(this, &NetwMultiplayer::display_lane);

    session_core.announce_to(this);

    gate_channels.spawn = declared_channel("SPAWN");
    gate_channels.despawn = declared_channel("DESPAWN");
    gate_channels.hide = declared_channel("HIDE");
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
    scene_seat_channel = declared_channel("SESSION_SCENE_SEAT");
    session_join_channel = declared_channel("SESSION_JOIN");
    session_accept_channel = declared_channel("SESSION_ACCEPT");
    session_roster_channel = declared_channel("SESSION_ROSTER");

    interest_flush = callable_mp(this, &NetwMultiplayer::interest_flush_sink);

    channel_book.register_protocol(
        scene_request_channel,
        callable_mp(this, &NetwMultiplayer::scene_receive_request_frame)
    );
    channel_book.register_protocol(
        scene_result_channel,
        callable_mp(this, &NetwMultiplayer::scene_receive_result_frame)
    );
    channel_book.register_protocol(
        scene_released_channel,
        callable_mp(this, &NetwMultiplayer::scene_receive_released_frame)
    );
    channel_book.register_protocol(
        scene_seat_channel,
        callable_mp(this, &NetwMultiplayer::scene_receive_seat_frame)
    );
    channel_book.register_protocol(
        session_join_channel,
        callable_mp(this, &NetwMultiplayer::session_receive_join)
    );
    channel_book.register_protocol(
        session_accept_channel,
        callable_mp(this, &NetwMultiplayer::session_receive_accept)
    );
    channel_book.register_protocol(
        session_roster_channel,
        callable_mp(this, &NetwMultiplayer::session_receive_roster)
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
        channel_book.register_protocol(
            channel,
            callable_mp(this, &NetwMultiplayer::session_receive_control)
                .bind(channel)
        );
    }

    connect_core.bind_session(this);

    ReplicationCore *plane = memnew(ReplicationCore);
    replication_owner = plane;
    plane->install(this);

    scene_install();
    display_bind_session();

    const Signal ended(this, SIG_SESSION_ENDED);
    connect_once(
        ended,
        callable_mp(this, &NetwMultiplayer::liveness_settle_clear_session)
    );
    connect_once(
        ended,
        callable_mp(this, &NetwMultiplayer::interest_session_ended)
    );
    connect_once(
        ended,
        callable_mp(this, &NetwMultiplayer::session_settle_clear_state)
    );
    connect_once(
        Signal(this, SIG_SCENE_LOCAL_CHANGED),
        callable_mp(
            this,
            &NetwMultiplayer::scene_participant_display_invalidate
        )
            .unbind(2)
    );
    connect_once(
        Signal(this, SIG_CLOCK_AFTER_TICK),
        callable_mp(this, &NetwMultiplayer::session_on_clock_tick)
    );
    connect_once(
        Signal(this, SIG_ENTITY_LIVE),
        callable_mp(this, &NetwMultiplayer::liveness_settle_local_player)
            .unbind(1)
    );
    connect_once(
        Signal(this, SIG_SESSION_TREE_PAUSED),
        callable_mp(this, &NetwMultiplayer::session_set_tree_paused)
            .bind(true)
            .unbind(1)
    );
    connect_once(
        Signal(this, SIG_SESSION_TREE_UNPAUSED),
        callable_mp(this, &NetwMultiplayer::session_set_tree_paused).bind(false)
    );

    const Signal joined(this, StringName("peer_connected"));
    connect_once(
        joined,
        callable_mp(this, &NetwMultiplayer::interest_peer_connected)
    );
    connect_once(
        joined,
        callable_mp(this, &NetwMultiplayer::participant_ensure)
    );
    connect_once(
        joined,
        callable_mp(this, &NetwMultiplayer::replication_replay_tables)
    );
    const Signal left(this, StringName("peer_disconnected"));
    connect_once(
        left,
        callable_mp(this, &NetwMultiplayer::interest_peer_disconnected)
    );
    connect_once(
        left,
        callable_mp(this, &NetwMultiplayer::session_clear_disconnected_peer)
    );

    adopt_schema_declarations();
    adopt_table_declarations();

    live_registry.push_back(gd::instance_id(this));
}

void NetwMultiplayer::embed_adopt_inner(const Ref<SceneMultiplayer> &p_inner) {
    if (p_inner.is_null() || p_inner == inner) {
        return;
    }
    const Callable adopted = p_inner->get_auth_callback();
    session_set_inner(p_inner);
    if (adopted.is_valid()) {
        set_auth_callback(adopted);
    }
    session_apply_auth_config();
}

void NetwMultiplayer::embed_dispose() {
    if (!embed_claim_dispose()) {
        return;
    }
    session_flush_deferred();
    set_auth_callback(Callable());
    scene_dispose();
    session_dispose();
    display_clear_runtimes();
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->dispose();
    }
    rpc_dispose();
    property_set_clear();
    clear_flat_family_state();
    service_clear();
    session_clear_roster();
    participant_clear();
}

uint8_t NetwMultiplayer::declared_channel(const char *p_name) const {
    const wire::ChannelDecl *decl
        = channels.find_channel_by_name(StringName(p_name));
    return decl ? decl->id : 0;
}

Ref<NetwParticipant> NetwMultiplayer::participant_ensure(int64_t p_peer) {
    ParticipantRow *found = participants.getptr(p_peer);
    if (found != nullptr) {
        return found->row;
    }
    Ref<NetwParticipant> minted;
    minted.instantiate();
    minted->seat_at(this, p_peer);
    NETW_TRACE(
        sys::SESSION,
        "minted the participant row for peer %d",
        int(p_peer)
    );
    participants[p_peer] = ParticipantRow{minted, RID(), false};
    return minted;
}

void NetwMultiplayer::participant_adopt(
    int64_t p_peer,
    const Ref<NetwParticipant> &p_participant
) {
    if (p_participant.is_null() || participants.has(p_peer)) {
        return;
    }
    participants[p_peer] = ParticipantRow{p_participant, RID()};
}

Ref<NetwParticipant> NetwMultiplayer::participant_of(int64_t p_peer) const {
    const ParticipantRow *found = participants.getptr(p_peer);
    return found ? found->row : Ref<NetwParticipant>();
}

bool NetwMultiplayer::participant_has(int64_t p_peer) const {
    return participants.has(p_peer);
}

TypedArray<NetwParticipant> NetwMultiplayer::participant_all() const {
    LocalVector<int64_t> peers;
    for (const KeyValue<int64_t, ParticipantRow> &row : participants) {
        peers.push_back(row.key);
    }
    peers.sort();
    TypedArray<NetwParticipant> out;
    for (uint32_t at = 0; at < peers.size(); at++) {
        out.push_back(participants[peers[at]].row);
    }
    return out;
}

Ref<NetwParticipant> NetwMultiplayer::participant_joined_of(int64_t p_peer) {
    if (session_accepted_join(p_peer).is_null()) {
        return Ref<NetwParticipant>();
    }
    participant_ensure(p_peer);
    return participant_of(p_peer);
}

TypedArray<NetwParticipant> NetwMultiplayer::participant_joined_all() {
    const Array joins = session_accepted_joins();
    TypedArray<NetwParticipant> out;
    for (int at = 0; at < joins.size(); ++at) {
        const Ref<ResolvedJoin> join = joins[at];
        if (join.is_null()) {
            continue;
        }
        const Ref<NetwParticipant> seated
            = participant_joined_of(join->get_peer_id());
        if (seated.is_valid()) {
            out.push_back(seated);
        }
    }
    return out;
}

Ref<NetwParticipant> NetwMultiplayer::participant_local() {
    return participant_joined_of(get_unique_id());
}

bool NetwMultiplayer::participant_admit(int64_t p_peer) {
    ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || found->admitted) {
        return false;
    }
    found->admitted = true;
    return true;
}

Ref<NetwParticipant> NetwMultiplayer::participant_admitted_of(
    int64_t p_peer
) const {
    const ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || !found->admitted) {
        return Ref<NetwParticipant>();
    }
    return found->row;
}

TypedArray<Object> NetwMultiplayer::participant_admitted_all() const {
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

Ref<NetwParticipant> NetwMultiplayer::participant_admitted_local() {
    return participant_admitted_of(get_unique_id());
}

RID NetwMultiplayer::participant_seat(int64_t p_peer) const {
    const ParticipantRow *found = participants.getptr(p_peer);
    return found ? found->seat : RID();
}

bool NetwMultiplayer::participant_take_seat(
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

bool NetwMultiplayer::participant_leave_seat(
    int64_t p_peer,
    const RID &p_scene
) {
    ParticipantRow *found = participants.getptr(p_peer);
    if (found == nullptr || !found->seat.is_valid() || found->seat != p_scene) {
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

void NetwMultiplayer::participant_announce_seat(
    const Ref<RefCounted> &p_row,
    const Ref<RefCounted> &p_from,
    const Ref<RefCounted> &p_to
) {
    if (p_row.is_null()) {
        return;
    }
    p_row->emit_signal(SIG_SCENE_CHANGED, p_from, p_to);
}

bool NetwMultiplayer::participant_seat_move(
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
    if (seat.is_valid() && seat != p_scene) {
        scene_release_peer(seat, p_peer);
    }
    if (!participant_take_seat(p_peer, p_scene)) {
        return false;
    }
    participant_announce_seat(row, from, scene_handle_of(p_scene));
    return true;
}

bool NetwMultiplayer::participant_seat_clear(
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

bool NetwMultiplayer::participant_move_seat(
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

PackedInt64Array NetwMultiplayer::participant_seated_in(
    const RID &p_scene
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

void NetwMultiplayer::participant_forget(int64_t p_peer) {
    if (participant_of(p_peer) == local_participant) {
        bind_local_participant(Ref<NetwParticipant>());
    }
    participants.erase(p_peer);
}

void NetwMultiplayer::participant_clear() {
    bind_local_participant(Ref<NetwParticipant>());
    participants.clear();
}

void NetwMultiplayer::bind_local_participant(
    const Ref<NetwParticipant> &p_row
) {
    if (local_participant == p_row) {
        return;
    }
    if (local_participant.is_valid()) {
        stop_relay_named_from(
            local_participant.ptr(),
            SIG_SCENE_CHANGED,
            SIG_SCENE_LOCAL_CHANGED,
            2
        );
    }
    local_participant = p_row;
    if (local_participant.is_valid()) {
        relay_named_from(
            local_participant.ptr(),
            SIG_SCENE_CHANGED,
            SIG_SCENE_LOCAL_CHANGED,
            2
        );
    }
}

void NetwMultiplayer::participant_publish_joined(int64_t p_peer) {
    const Ref<NetwParticipant> participant = participant_of(p_peer);
    if (participant.is_null()) {
        return;
    }
    if (p_peer == get_unique_id()) {
        bind_local_participant(participant);
        emit_signal(SIG_PARTICIPANT_LOCAL_JOINED, participant);
    }
    emit_signal(SIG_PARTICIPANT_JOINED, participant);
}

void NetwMultiplayer::set_session_api(Object *p_api) {
    session_api_id = gd::instance_id(p_api);
}

Object *NetwMultiplayer::session_api() const {
    Object *published = gd::instance_from_id(session_api_id);
    return published != nullptr ? published
                                : const_cast<NetwMultiplayer *>(this);
}

Ref<MultiplayerAPI> NetwMultiplayer::get_session_api() const {
    return Ref<MultiplayerAPI>(Object::cast_to<MultiplayerAPI>(session_api()));
}

bool NetwMultiplayer::session_is_active() const {
    SceneTree *loop = gd::scene_tree();
    if (loop == nullptr) {
        NETW_TRACE(sys::SESSION, "no scene tree answers for a live session");
        return false;
    }
    Ref<MultiplayerAPI> published = get_session_api();
    if (published.is_null()) {
        NETW_TRACE(sys::SESSION, "the session publishes no multiplayer api");
        return false;
    }
    const Ref<MultiplayerAPI> live = loop->get_multiplayer(get_root_path());
    return live == published;
}

TypedArray<MultiplayerAPI> NetwMultiplayer::session_get_all() {
    NETW_ZONE_NC("netw::session_get_all", colors::SESSION);
    TypedArray<MultiplayerAPI> out;
    LocalVector<ObjectID> kept;
    for (const ObjectID &id : live_registry) {
        NetwMultiplayer *session
            = Object::cast_to<NetwMultiplayer>(gd::instance_from_id(id));
        if (session == nullptr) {
            NETW_TRACE(
                sys::SESSION,
                "a disposed session leaves the live registry"
            );
            continue;
        }
        kept.push_back(id);
        if (session->session_is_active()) {
            out.push_back(session->get_session_api());
        }
    }
    live_registry = kept;
    return out;
}

NetwMultiplayer *NetwMultiplayer::of(Node *p_node) {
    if (p_node == nullptr || !p_node->is_inside_tree()) {
        NETW_TRACE(sys::SESSION, "an off-tree node resolves to no session");
        return nullptr;
    }
    const Ref<MultiplayerAPI> installed = p_node->get_multiplayer();
    if (installed.is_null()) {
        NETW_TRACE(sys::SESSION, "a node's branch installs no multiplayer api");
        return nullptr;
    }
    NetwMultiplayer *direct = Object::cast_to<NetwMultiplayer>(installed.ptr());
    if (direct != nullptr) {
        return direct;
    }
    for (const ObjectID &id : live_registry) {
        NetwMultiplayer *session
            = Object::cast_to<NetwMultiplayer>(gd::instance_from_id(id));
        if (session != nullptr && session->session_api() == installed.ptr()) {
            return session;
        }
    }
    NETW_TRACE(sys::SESSION, "no live session publishes the installed api");
    return nullptr;
}

Ref<NetwMultiplayer> NetwMultiplayer::core_of(Node *p_node) {
    return Ref<NetwMultiplayer>(of(p_node));
}

NetwMultiplayer *NetwMultiplayer::core_rooted_at(Node *p_root) {
    if (p_root == nullptr) {
        return nullptr;
    }
    for (const ObjectID &id : live_registry) {
        NetwMultiplayer *session
            = Object::cast_to<NetwMultiplayer>(gd::instance_from_id(id));
        if (session != nullptr && session->session_root() == p_root) {
            return session;
        }
    }
    return nullptr;
}

Ref<MultiplayerAPI> NetwMultiplayer::session_of(Node *p_node) {
    NetwMultiplayer *core = of(p_node);
    return core != nullptr ? core->get_session_api() : Ref<MultiplayerAPI>();
}

NodePath NetwMultiplayer::relative_path(Object *p_source, Object *p_target) {
    Node *source = Object::cast_to<Node>(p_source);
    Node *target = Object::cast_to<Node>(p_target);
    if (source == nullptr || target == nullptr) {
        return NodePath();
    }
    return source->get_path_to(target);
}

Ref<NetwEntity> NetwMultiplayer::scene_player_local() const {
    return local_player;
}

void NetwMultiplayer::set_local_player(
    const Ref<NetwEntity> &p_player,
    int64_t p_id
) {
    if (local_player == p_player) {
        return;
    }
    local_player = p_player;
    local_player_id = p_id;
    emit_signal(SIG_SCENE_LOCAL_PLAYER_CHANGED, p_player);
    scene_participant_display_invalidate();
}

void NetwMultiplayer::set_scene_participant_edge(const Callable &p_edge) {
    scene_participant_edge = p_edge;
}

int NetwMultiplayer::service_row(Object *p_type) const {
    const ObjectID wanted = gd::instance_id(p_type);
    for (uint32_t at = 0; at < services.size(); at++) {
        if (services[at].type == wanted) {
            return int(at);
        }
    }
    return -1;
}

int NetwMultiplayer::service_row_named(const StringName &p_class) const {
    if (p_class == StringName()) {
        return -1;
    }
    for (uint32_t at = 0; at < services.size(); at++) {
        if (services[at].class_key == p_class) {
            return int(at);
        }
    }
    return -1;
}

Object *NetwMultiplayer::get_service_named(const StringName &p_class) const {
    const int at = service_row_named(p_class);
    return at < 0 ? nullptr : gd::instance_from_id(services[at].service);
}

void NetwMultiplayer::service_register(Object *p_service, Object *p_type) {
    if (p_service == nullptr) {
        return;
    }
    Object *type = p_type ? p_type : script_of(p_service);
    if (type == nullptr) {
        const StringName owned = p_service->get_class();
        const int held = service_row_named(owned);
        if (held >= 0) {
            services[held].service = gd::instance_id(p_service);
        } else {
            ServiceRow row;
            row.service = gd::instance_id(p_service);
            row.class_key = owned;
            services.push_back(row);
        }
        discovery_watch_directory(p_service);
        emit_signal(SIG_SERVICE_REGISTERED, p_service);
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
        discovery_watch_directory(p_service);
        emit_signal(SIG_SERVICE_REGISTERED, p_service);
        return;
    }
    ServiceRow row;
    row.type = gd::instance_id(type);
    row.service = gd::instance_id(p_service);
    services.push_back(row);
    discovery_watch_directory(p_service);
    emit_signal(SIG_SERVICE_REGISTERED, p_service);
}

void NetwMultiplayer::service_unregister(Object *p_service, Object *p_type) {
    Object *type = p_type ? p_type : script_of(p_service);
    int at = type ? service_row(type) : -1;
    if (at < 0 && type == nullptr && p_service != nullptr) {
        at = service_row_named(p_service->get_class());
    }
    if (at < 0 || gd::instance_from_id(services[at].service) != p_service) {
        return;
    }
    services.remove_at(at);
    discovery_unwatch_directory(p_service);
    emit_signal(SIG_SERVICE_UNREGISTERED, p_service);
}

Variant NetwMultiplayer::service_held(Object *p_type) const {
    return gd::held(get_service(p_type));
}

Variant NetwMultiplayer::service_held_named(const StringName &p_class) const {
    return gd::held(get_service_named(p_class));
}

StringName NetwMultiplayer::class_named_by(Object *p_type) const {
    if (p_type == nullptr
        || p_type->get_class() != StringName("GDScriptNativeClass")) {
        return StringName();
    }
    const uint64_t asked = uint64_t(gd::instance_id(p_type));
    const StringName *cached = native_class_names.getptr(asked);
    if (cached != nullptr) {
        return *cached;
    }
    StringName named;
    const Variant made = p_type->call("new");
    Object *probe = gd::live_object(made);
    if (probe != nullptr) {
        named = probe->get_class();
        if (Object::cast_to<RefCounted>(probe) == nullptr) {
            memdelete(probe);
        }
    }
    native_class_names.insert(asked, named);
    return named;
}

Object *NetwMultiplayer::get_service(Object *p_type) const {
    const int at = service_row(p_type);
    if (at >= 0) {
        return gd::instance_from_id(services[at].service);
    }
    return get_service_named(class_named_by(p_type));
}

void NetwMultiplayer::service_clear() {
    services.clear();
    for (const KeyValue<uint64_t, RID> &held : directory_slots) {
        connect::free_transport_slot(held.value);
    }
    directory_slots.clear();
}

TypedArray<Node> NetwMultiplayer::service_get_all(Object *p_base) const {
    TypedArray<Node> out;
    const StringName native_base = class_named_by(p_base);
    for (uint32_t at = 0; at < services.size(); at++) {
        Object *service = gd::instance_from_id(services[at].service);
        if (service == nullptr) {
            continue;
        }
        if (native_base != StringName()) {
            if (service->is_class(native_base)) {
                out.push_back(service);
            }
            continue;
        }
        Ref<Script> script
            = Object::cast_to<Script>(gd::instance_from_id(services[at].type));
        while (script.is_valid()) {
            if (script.ptr() == p_base) {
                out.push_back(service);
                break;
            }
            script = script->get_base_script();
        }
    }
    return out;
}

bool NetwMultiplayer::session_publish_control(
    int64_t p_channel,
    int64_t p_sender,
    const PackedByteArray &p_payload
) {
    const bool from_server = p_sender == 1;
    session::SessionReason stated;
    const bool states_reason = session::frame_read(p_payload, stated);
    if (p_channel == control_channels.pause) {
        if (from_server && states_reason) {
            emit_signal(SIG_SESSION_TREE_PAUSED, stated.reason);
        }
        return true;
    }
    if (p_channel == control_channels.unpause) {
        if (from_server && p_payload.is_empty()) {
            emit_signal(SIG_SESSION_TREE_UNPAUSED);
        }
        return true;
    }
    if (p_channel == control_channels.kicked) {
        if (from_server && states_reason) {
            if (join_awaiting_admission) {
                session_report_join_failure(ERR_UNAUTHORIZED, stated.reason);
            }
            emit_signal(SIG_PEER_KICKED, stated.reason);
        }
        return true;
    }
    if (p_channel == control_channels.shutdown) {
        if (from_server && states_reason) {
            emit_signal(SIG_SESSION_SERVER_DISCONNECTING, stated.reason);
        }
        return true;
    }
    if (p_channel == control_channels.kick_request) {
        session::KickRequest asked;
        if (is_server() && session::frame_read(p_payload, asked)) {
            emit_signal(
                SIG_PEER_KICK_REQUESTED,
                p_sender,
                asked.peer,
                asked.reason
            );
        }
        return true;
    }
    if (p_channel == control_channels.leave_request) {
        if (is_server() && states_reason) {
            emit_signal(
                SIG_SESSION_DISCONNECT_REQUESTED,
                p_sender,
                stated.reason
            );
        }
        return true;
    }
    return false;
}

void NetwMultiplayer::session_admit(const Ref<ResolvedJoin> &p_join) {
    if (p_join.is_null()) {
        return;
    }
    if (is_server() && !session_preflight_join(p_join)) {
        return;
    }
    if (!join_roster.remember(p_join)) {
        return;
    }
    const int64_t joined = p_join->get_peer_id();
    if (joined == get_unique_id()) {
        join_awaiting_admission = false;
    }
    participant_ensure(joined);
    if (participant_of(joined).is_null()) {
        return;
    }
    participant_admit(joined);
    session_run_join_handler(p_join);
    participant_publish_joined(joined);
}

bool NetwMultiplayer::session_preflight_join(const Ref<ResolvedJoin> &p_join) {
    const JoinPlan plan = session_resolve_join();
    if (!plan.available || !plan.handler.is_valid()) {
        session_fail_join(
            p_join,
            ERR_UNCONFIGURED,
            "This server's join handler is unavailable"
        );
        return false;
    }
    if (plan.declared && p_join->get_arg_values().is_empty()
        && !join_arg_types(plan.handler).is_empty()) {
        NETW_WARN(
            sys::SESSION,
            "join: peer %d sent no arguments and this server's declared "
            "handler needs %d, so the join carries nothing to place the "
            "player with",
            int(p_join->get_peer_id()),
            int(join_arg_types(plan.handler).size())
        );
        session_fail_join(
            p_join,
            ERR_INVALID_DATA,
            "This server's join handler needs arguments the join carried none "
            "of"
        );
        return false;
    }
    return true;
}

void NetwMultiplayer::session_fail_join(
    const Ref<ResolvedJoin> &p_join,
    Error p_error,
    const String &p_reason
) {
    const int64_t peer = p_join.is_valid() ? p_join->get_peer_id() : 0;
    session_refuse(peer, p_reason);
    join_roster.forget(peer);
    if (peer == get_unique_id()) {
        session_report_join_failure(p_error, p_reason);
        return;
    }
    if (is_server()) {
        session_turn_peer_away(peer, p_reason);
    }
}

void NetwMultiplayer::session_turn_peer_away(
    int64_t p_peer,
    const String &p_reason
) {
    if (!p_reason.is_empty()) {
        send_to(
            p_peer,
            0,
            control_channels.kicked,
            session::frame_write(session::SessionReason{p_reason}),
            true,
            0,
            String(),
            false
        );
    }
    session_defer_after(
        callable_mp(this, &NetwMultiplayer::session_drop_turned_away_peer)
            .bind(p_peer),
        StringName(String("netw_join_refused_") + String::num_int64(p_peer)),
        POLLS_BEFORE_TURNED_AWAY_PEER_DROPS
    );
}

void NetwMultiplayer::session_drop_turned_away_peer(int64_t p_peer) {
    if (effective_peer.is_valid()) {
        effective_peer->disconnect_peer(int32_t(p_peer));
    }
}

void NetwMultiplayer::session_report_join_failure(
    Error p_error,
    const String &p_reason
) {
    join_awaiting_admission = false;
    prepared_join.reset();
    if (preparing_join.is_valid() && !preparing_join->get_is_settled()) {
        preparing_join->resolve(int64_t(p_error));
    }
    preparing_join = Ref<NetwPromise>();
    emit_signal(SIG_SESSION_JOIN_FAILED, int64_t(p_error), p_reason);
}

void NetwMultiplayer::session_run_join_handler(
    const Ref<ResolvedJoin> &p_join
) {
    if (!is_server() || p_join.is_null()) {
        return;
    }
    const JoinPlan plan = session_resolve_join();
    if (!plan.available || !plan.handler.is_valid()) {
        return;
    }
    if (!plan.declared && p_join->get_arg_values().is_empty()
        && !join_arg_types(plan.handler).is_empty()) {
        return;
    }
    const Ref<NetwParticipant> joining = participant_of(p_join->get_peer_id());
    if (joining.is_null()) {
        session_fail_join(
            p_join,
            ERR_DOES_NOT_EXIST,
            "This server seated no participant for the join"
        );
        return;
    }
    const Callable handler = plan.handler;
    Array args;
    args.push_back(joining);
    args.append_array(p_join->get_arg_values());
    bool called = false;
    const Variant scene = gd::call_checked(handler, args, called);
    if (!called) {
        session_fail_join(
            p_join,
            ERR_INVALID_PARAMETER,
            "This server's join handler refused the arguments the join carried"
        );
        return;
    }
    const Ref<NetwPromise> seated = scene;
    if (seated.is_valid()) {
        seated->then(
            callable_mp(this, &NetwMultiplayer::session_seat_join_result)
                .bind(p_join)
        );
        seated->catch_error(
            callable_mp(this, &NetwMultiplayer::session_seat_join_rejected)
                .bind(p_join)
        );
        return;
    }
    if (!is_coroutine(scene)) {
        session_seat_join_result(scene, p_join);
        return;
    }
    Object *state = scene;
    if (state != nullptr) {
        state->connect(
            StringName("completed"),
            callable_mp(this, &NetwMultiplayer::session_seat_join_result)
                .bind(p_join),
            Object::CONNECT_ONE_SHOT
        );
    }
}

bool NetwMultiplayer::session_join_still_pending(
    const Ref<ResolvedJoin> &p_join
) const {
    if (p_join.is_null()) {
        return false;
    }
    return peer_get_accepted_join(p_join->get_peer_id()) == p_join;
}

void NetwMultiplayer::session_seat_join_rejected(
    const Variant &p_error,
    const String &p_message,
    const Ref<ResolvedJoin> &p_join
) {
    if (!session_join_still_pending(p_join)) {
        return;
    }
    const int64_t code = p_error;
    session_fail_join(
        p_join,
        code == OK ? FAILED : Error(code),
        p_message.is_empty() ? String("This server could not seat the player")
                             : p_message
    );
}

void NetwMultiplayer::session_seat_join_result(
    const Variant &p_scene,
    const Ref<ResolvedJoin> &p_join
) {
    if (!session_join_still_pending(p_join)) {
        return;
    }
    const int64_t peer = p_join->get_peer_id();
    if (p_scene.get_type() == Variant::NIL) {
        return;
    }
    Object *object = p_scene;
    const Ref<NetwSceneHandle> handed
        = Ref<NetwSceneHandle>(Object::cast_to<NetwSceneHandle>(object));
    if (handed.is_valid()) {
        const RID scene = handed->get_entity();
        if (!scene.is_valid() || scene_node_of(scene) == nullptr) {
            session_fail_join(
                p_join,
                ERR_INVALID_DATA,
                "This server's join handler answered a scene this session "
                "does not hold"
            );
            return;
        }
        session_seat_join_into(peer, scene, p_join);
        return;
    }
    Node *node = Object::cast_to<Node>(object);
    if (node == nullptr) {
        session_fail_join(
            p_join,
            ERR_INVALID_DATA,
            "This server's join handler answered something that is neither a "
            "node nor a scene"
        );
        return;
    }
    const Ref<NetwEntity> record = NetwEntity::of(node);
    const RID scene
        = record.is_valid() ? scene_of(handle_of_wrapper(record.ptr())) : RID();
    if (scene.is_valid()) {
        session_seat_join_into(peer, scene, p_join);
    }
}

void NetwMultiplayer::session_seat_join_into(
    int64_t p_peer,
    const RID &p_scene,
    const Ref<ResolvedJoin> &p_join
) {
    const Error admitted = scene_admit(p_scene, p_peer);
    if (admitted != OK) {
        session_fail_join(
            p_join,
            admitted,
            "This server could not admit the joining player to the scene its "
            "join handler answered"
        );
        return;
    }
    participant_move_seat(p_peer, p_scene);
}

void NetwMultiplayer::session_set_join_resolver(const Callable &p_resolver) {
    session_join_resolver = p_resolver;
}

void NetwMultiplayer::session_after_entered() {
    if (session_get_role() == ROLE_CLIENT) {
        session_submit_prepared_join();
        session_warn_missing_local_join();
        return;
    }
    auth_seat_host_identity();
    session_submit_host_join();
    session_warn_missing_local_join();
}

void NetwMultiplayer::session_warn_missing_local_join() {
    if (session_get_role() == ROLE_DEDICATED_SERVER) {
        return;
    }
    if ((preparing_join.is_valid() && !preparing_join->get_is_settled())
        || prepared_join.has_value()) {
        return;
    }
    if (join_awaiting_admission) {
        return;
    }
    if (participant_local().is_valid()) {
        return;
    }
    missing_local_join_warnings++;
}

int64_t session_missing_local_join_warnings(const NetwMultiplayer *p_session) {
    return p_session != nullptr ? p_session->missing_local_join_warnings : 0;
}

void NetwMultiplayer::session_submit_host_join() {
    if (!prepared_join.has_value()) {
        return;
    }
    if (scene_list().size() > 0) {
        session_submit_prepared_join();
        return;
    }
    const Callable on_startup = callable_mp(
        this,
        &NetwMultiplayer::session_submit_host_join_on_startup
    );
    if (!is_connected(StringName(SIG_SCENE_STARTUP_SPAWNED), on_startup)) {
        connect(
            StringName(SIG_SCENE_STARTUP_SPAWNED),
            on_startup,
            Object::CONNECT_ONE_SHOT
        );
    }
}

void NetwMultiplayer::session_submit_host_join_on_startup() {
    session_submit_prepared_join();
}

void NetwMultiplayer::session_after_edge(int64_t, int64_t p_new) {
    if (p_new == SESSION_STATE_OFFLINE) {
        session_clear_prepared_join();
    }
}

void NetwMultiplayer::session_relay_peer_connected(int64_t p_peer) {
    peer_mark_reachable(p_peer);
    announced_departures.erase(p_peer);
    Dictionary detail;
    detail[StringName("peer")] = p_peer;
    report_event(
        EventPlane::PEER_JOINED,
        0,
        detail,
        p_peer,
        StringName(),
        Dictionary(),
        0
    );
    emit_signal(StringName("peer_connected"), p_peer);
}

void NetwMultiplayer::session_relay_peer_disconnected(int64_t p_peer) {
    if (announced_departures.has(p_peer)) {
        return;
    }
    announced_departures.insert(p_peer);
    Dictionary detail;
    detail[StringName("peer")] = p_peer;
    report_event(
        EventPlane::PEER_LEFT,
        0,
        detail,
        p_peer,
        StringName(),
        Dictionary(),
        0
    );
    emit_signal(StringName("peer_disconnected"), p_peer);
}

void NetwMultiplayer::session_clear_disconnected_peer(int64_t p_peer) {
    forget_peer_seqs(p_peer);
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->clear_peer(p_peer);
    }
    rpc_handle_disconnect(p_peer);
}

void NetwMultiplayer::session_transport_connected() {
    if (session_get_state() != SESSION_STATE_CONNECTING) {
        NETW_TRACE(
            sys::SESSION,
            "a connected transport edge found session state %d",
            session_get_state()
        );
        return;
    }
    session_push_desired_role();
    session_resolve_online(get_unique_id());
}

void NetwMultiplayer::session_transport_failed() {
    if (session_get_state() != SESSION_STATE_CONNECTING) {
        NETW_TRACE(
            sys::SESSION,
            "a failed transport edge found session state %d",
            session_get_state()
        );
        return;
    }
    session_transition(SESSION_STATE_OFFLINE);
}

void NetwMultiplayer::session_transport_dropped() {
    if (embed_is_disposing()) {
        NETW_TRACE(
            sys::SESSION,
            "a dropped transport edge found the session already disposing"
        );
        return;
    }
    if (session_get_state() != SESSION_STATE_ONLINE) {
        NETW_TRACE(
            sys::SESSION,
            "a dropped transport edge found session state %d",
            session_get_state()
        );
        return;
    }
    session_transition(SESSION_STATE_DISCONNECTING);
    session_transition(SESSION_STATE_OFFLINE);
}

void NetwMultiplayer::session_settle_clear_state() {
    callable_mp(this, &NetwMultiplayer::clear_session_state).call_deferred();
}

void NetwMultiplayer::session_set_tree_paused(bool p_paused) {
    SceneTree *loop = gd::scene_tree();
    if (loop == nullptr) {
        NETW_TRACE(
            sys::SESSION,
            "a session pause found no scene tree, so nothing pauses"
        );
        return;
    }
    loop->set_pause(p_paused);
}

void NetwMultiplayer::session_dispose() {
    session_clear_prepared_join();
    auth_clear();
    probe_clear();
}

const char *NetwMultiplayer::session_identity_differs(
    const JoinRequest &p_request
) const {
    if (p_request.app_tag != auth_app_tag) {
        return "app";
    }
    if (p_request.wire_identity != SessionCore::compute_wire_identity()) {
        return "wire";
    }
    if (p_request.schema_identity != session_schema_identity()) {
        return "schema";
    }
    return nullptr;
}

void NetwMultiplayer::session_receive_join(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (!is_server()) {
        return;
    }
    const Time *reading = Time::get_singleton();
    if (session_core.join_flooded(
            int(p_sender),
            reading ? int64_t(reading->get_ticks_msec()) : 0
        )) {
        return;
    }
    JoinRequest requested;
    if (!requested.deserialize(p_payload)) {
        NETW_WARN(
            sys::SESSION,
            "join: unreadable request from peer %d",
            int(p_sender)
        );
        return;
    }
    requested.peer_id = p_sender;
    const char *differs = session_identity_differs(requested);
    if (differs != nullptr) {
        NETW_WARN(
            sys::SESSION,
            "join: peer %d is refused because its %s identity differs",
            int(p_sender),
            differs
        );
        session_refuse(
            p_sender,
            vformat("Incompatible build: %s", String(differs))
        );
        return;
    }
    Ref<ResolvedJoin> resolved;
    if (session_join_resolver.is_valid()) {
        if (!session_decode_join_args(requested)) {
            NETW_WARN(
                sys::SESSION,
                "join: rejected malformed or mismatched args from peer %d",
                int(p_sender)
            );
            return;
        }
        resolved = Ref<ResolvedJoin>(session_join_resolver.call(
            requested.username,
            requested.arg_values,
            p_sender
        ));
    } else {
        resolved = session_resolve_inbound_join(requested, p_sender);
    }
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
            join_roster.roster_frame(),
            true,
            0,
            String(),
            false
        );
    }
}

void NetwMultiplayer::session_receive_accept(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER) {
        return;
    }
    session_admit(ResolvedJoin::deserialize(p_payload));
}

void NetwMultiplayer::session_receive_roster(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER) {
        return;
    }
    LocalVector<AcceptFrame> rows;
    if (!session::roster_read(p_payload, rows)) {
        NETW_TRACE(
            sys::SESSION,
            "a roster that did not decode whole seats nobody"
        );
        return;
    }
    for (uint32_t at = 0; at < rows.size(); ++at) {
        session_admit(ResolvedJoin::of_frame(rows[at]));
    }
}

void NetwMultiplayer::session_receive_control(
    const PackedByteArray &p_payload,
    int64_t p_sender,
    int64_t p_channel
) {
    session_publish_control(p_channel, p_sender, p_payload);
}

SessionCore &NetwMultiplayer::session_plane() {
    return session_core;
}

void NetwMultiplayer::session_announce_entered() {
    emit_signal(SIG_SESSION_ENTERED);
    scene_participant_display_invalidate();
    session_after_entered();
}

void NetwMultiplayer::session_announce_ended() {
    emit_signal(SIG_SESSION_ENDED);
    emit_signal(SIG_SESSION_RECLAIMED);
    scene_participant_display_invalidate();
}

void NetwMultiplayer::session_announce_edge(int p_old, int p_new) {
    emit_signal(SIG_SESSION_STATE_CHANGED, p_old, p_new);
    session_after_edge(p_old, p_new);
}

Ref<ResolvedJoin> NetwMultiplayer::session_accepted_join(int64_t p_peer) const {
    return join_roster.accepted_join(p_peer);
}

Array NetwMultiplayer::session_accepted_joins() const {
    return join_roster.accepted_joins();
}

bool NetwMultiplayer::session_remember_join(const Ref<ResolvedJoin> &p_join) {
    return join_roster.remember(p_join);
}

void NetwMultiplayer::session_forget_peer(int64_t p_peer) {
    peer_buckets.erase(p_peer);
    peer_identities.erase(p_peer);
    join_roster.forget(p_peer);
}

void NetwMultiplayer::session_clear_roster() {
    peer_buckets.clear();
    peer_identities.clear();
    join_roster.clear();
    unreachable_peers.clear();
    announced_departures.clear();
    rpc_drop_warned.clear();
}

PackedInt32Array NetwMultiplayer::reachable_peer_ids() const {
    const PackedInt32Array known = gd::api_peer_ids(inner);
    if (unreachable_peers.is_empty()) {
        return known;
    }
    PackedInt32Array out;
    for (int at = 0; at < known.size(); at++) {
        if (!unreachable_peers.has(int64_t(known[at]))) {
            out.push_back(known[at]);
        }
    }
    return out;
}

void NetwMultiplayer::peer_sweep_transport_state() {
    if (!session_link_is_connected()) {
        return;
    }
    const Ref<MultiplayerPeer> link = inner->get_multiplayer_peer();
    const PackedInt32Array roster = gd::api_peer_ids(inner);
    for (int at = 0; at < roster.size(); at++) {
        const int64_t peer = int64_t(roster[at]);
        if (unreachable_peers.has(peer) || gd::peer_is_relayed(inner, peer)) {
            continue;
        }
        if (!gd::peer_can_receive(link, peer)) {
            peer_mark_unreachable(peer);
        }
    }
}

bool NetwMultiplayer::session_link_is_connected() const {
    const Ref<MultiplayerPeer> link = inner.is_valid()
        ? inner->get_multiplayer_peer()
        : Ref<MultiplayerPeer>();
    return link.is_valid()
        && link->get_connection_status()
        == MultiplayerPeer::CONNECTION_CONNECTED;
}

void NetwMultiplayer::peer_mark_unreachable(int64_t p_peer) {
    if (p_peer == 0 || unreachable_peers.has(p_peer)
        || gd::peer_is_relayed(inner, p_peer)) {
        return;
    }
    unreachable_peers.insert(p_peer);
    NETW_WARN(
        sys::TRANSPORT,
        "peer %d refused a send, so it is addressed no more until it returns",
        int(p_peer)
    );
    session_clear_disconnected_peer(p_peer);
    interest_peer_disconnected(p_peer);
    session_relay_peer_disconnected(p_peer);
}

void NetwMultiplayer::peer_mark_reachable(int64_t p_peer) {
    unreachable_peers.erase(p_peer);
}

NodePath NetwMultiplayer::get_root_path() const {
    return inner.is_valid() ? inner->get_root_path() : NodePath();
}

void NetwMultiplayer::set_root_path(const NodePath &p_path) {
    if (inner.is_valid()) {
        inner->set_root_path(p_path);
    }
}

bool NetwMultiplayer::is_object_decoding_allowed() const {
    return inner.is_valid() && inner->is_object_decoding_allowed();
}

void NetwMultiplayer::set_allow_object_decoding(bool p_value) {
    if (inner.is_valid()) {
        inner->set_allow_object_decoding(p_value);
    }
}

double NetwMultiplayer::get_auth_timeout() const {
    return inner.is_valid() ? inner->get_auth_timeout() : 0.0;
}

void NetwMultiplayer::set_auth_timeout(double p_seconds) {
    if (inner.is_valid()) {
        inner->set_auth_timeout(p_seconds);
    }
}

bool NetwMultiplayer::is_refusing_new_connections() const {
    return inner.is_valid() && inner->is_refusing_new_connections();
}

void NetwMultiplayer::set_refuse_new_connections(bool p_value) {
    if (inner.is_valid()) {
        inner->set_refuse_new_connections(p_value);
    }
}

bool NetwMultiplayer::is_server_relay_enabled() const {
    return inner.is_valid() && inner->is_server_relay_enabled();
}

void NetwMultiplayer::set_server_relay_enabled(bool p_value) {
    if (inner.is_valid()) {
        inner->set_server_relay_enabled(p_value);
    }
}

int NetwMultiplayer::get_max_sync_packet_size() const {
    return inner.is_valid() ? inner->get_max_sync_packet_size() : 0;
}

void NetwMultiplayer::set_max_sync_packet_size(int p_value) {
    if (inner.is_valid()) {
        inner->set_max_sync_packet_size(p_value);
    }
}

int NetwMultiplayer::get_max_delta_packet_size() const {
    return inner.is_valid() ? inner->get_max_delta_packet_size() : 0;
}

void NetwMultiplayer::set_max_delta_packet_size(int p_value) {
    if (inner.is_valid()) {
        inner->set_max_delta_packet_size(p_value);
    }
}

int NetwMultiplayer::get_max_future_action_ticks() const {
    return max_future_action_ticks;
}

void NetwMultiplayer::set_max_future_action_ticks(int p_value) {
    max_future_action_ticks = p_value;
}

int NetwMultiplayer::get_input_gate_deadline_ticks() const {
    return input_gate_deadline_ticks;
}

void NetwMultiplayer::set_input_gate_deadline_ticks(int p_value) {
    input_gate_deadline_ticks = p_value;
}

TypedArray<Object> NetwMultiplayer::get_connected_participants() const {
    return participant_all();
}

Error NetwMultiplayer::configuration_door_refuses_config(Object *p_config) {
    const char *verb = nullptr;
    if (Object::cast_to<NetwSessionConfig>(p_config) != nullptr) {
        verb = "Netw.configure_session";
    } else if (Object::cast_to<NetwClockConfig>(p_config) != nullptr) {
        verb = "Netw.configure_clock";
    } else if (
        Object::cast_to<NetwLagCompensationConfig>(p_config) != nullptr
    ) {
        verb = "Netw.configure_lagcomp";
    }
    if (verb == nullptr) {
        return OK;
    }
    NETW_ERROR(
        sys::SESSION,
        "%s is declared with %s(scope) before startup and the session copies "
        "it once. The engine's configuration door neither installs nor "
        "uninstalls one.",
        p_config->get_class(),
        verb
    );
    return ERR_UNAVAILABLE;
}

Error NetwMultiplayer::send_auth(
    int64_t p_peer,
    const PackedByteArray &p_data
) {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->send_auth(int32_t(p_peer), p_data);
}

Error NetwMultiplayer::complete_auth(int64_t p_peer) {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->complete_auth(int32_t(p_peer));
}

PackedInt32Array NetwMultiplayer::get_authenticating_peers() const {
    if (inner.is_null()) {
        return PackedInt32Array();
    }
#if defined(NETW_MODULE)
    return inner->get_authenticating_peer_ids();
#else
    return inner->get_authenticating_peers();
#endif
}

Ref<NetwAuthFlow> NetwMultiplayer::auth_flow() const {
    return auth_flow_object;
}

void NetwMultiplayer::auth_set_app_tag(int64_t p_tag) {
    auth_app_tag = p_tag;
}

int64_t NetwMultiplayer::auth_app_tag_of() const {
    return auth_app_tag;
}

void NetwMultiplayer::auth_set_join_request(
    const std::optional<JoinRequest> &p_request
) {
    auth_request = p_request;
}

const std::optional<JoinRequest> &NetwMultiplayer::auth_join_request() const {
    return auth_request;
}

void NetwMultiplayer::set_auth_callback(const Callable &p_callback) {
    auth_app_callback = p_callback;
}

Callable NetwMultiplayer::get_auth_callback() const {
    return auth_app_callback;
}

void NetwMultiplayer::auth_clear() {
    auth_disarm();
    auth_flow_declared = Ref<NetwAuthFlow>();
    auth_flow_generation = 0;
    auth_flow_broken = false;
    auth_flow_object = Ref<NetwAuthFlow>();
    auth_request.reset();
    auth_app_callback = Callable();
}

Ref<NetwPromise> NetwMultiplayer::auth_prepare(const StringName &p_username) {
    if (auth_app_callback.is_valid()) {
        return NetwPromise::resolved(OK);
    }
    const Ref<NetwAuthFlow> flow = auth_effective_flow();
    if (flow.is_null()) {
        if (auth_is_configured()) {
            return NetwPromise::rejected(
                ERR_UNCONFIGURED,
                "This game declares authentication and this session could "
                "not build it, so nothing is prepared to join with"
            );
        }
        return NetwPromise::resolved(OK);
    }
    NETW_INFO(
        sys::SESSION,
        "auth prepares the flow for '%s'",
        String(p_username)
    );
    const Ref<NetwPromise> prepared = flow->prepare(p_username);
    if (prepared.is_null()) {
        return NetwPromise::rejected(
            ERR_INVALID_DATA,
            "The declared authentication flow prepared no answer, and a "
            "configured flow that prepares nothing has not authenticated"
        );
    }
    return prepared;
}

void NetwMultiplayer::auth_seat_host_identity() {
    if (auth_app_callback.is_valid()) {
        return;
    }
    const Ref<NetwAuthFlow> flow = auth_effective_flow();
    if (flow.is_null()) {
        return;
    }
    const Ref<NetwIdentity> host = flow->host_identity();
    if (host.is_null()) {
        NETW_TRACE(sys::SESSION, "the auth flow claims no host identity");
        return;
    }
    NETW_INFO(
        sys::SESSION,
        "the host is seated as '%s' from service '%s'",
        String(host->get_username()),
        String(host->get_service())
    );
    peer_set_identity(int64_t(MultiplayerPeer::TARGET_PEER_SERVER), host);
}

void NetwMultiplayer::auth_resolve_identity(
    int64_t p_peer,
    JoinRequest &r_join
) {
    if (auth_app_callback.is_valid()
        || p_peer == int64_t(MultiplayerPeer::TARGET_PEER_SERVER)) {
        return;
    }
    if (auth_effective_flow().is_null()) {
        return;
    }
    const Ref<NetwIdentity> verified = peer_get_identity(p_peer);
    if (verified.is_null()) {
        NETW_DEBUG(
            sys::SESSION,
            "peer %d was accepted without an identity, so it joins under the "
            "name it claimed, '%s'",
            int(p_peer),
            String(r_join.username)
        );
        return;
    }
    NETW_INFO(
        sys::SESSION,
        "peer %d joins as the verified '%s' rather than the claimed '%s'",
        int(p_peer),
        String(verified->get_username()),
        String(r_join.username)
    );
    r_join.username = verified->get_username();
}

void NetwMultiplayer::auth_arm() {
    if (inner.is_null()) {
        return;
    }
    inner->set_auth_callback(callable_mp(this, &NetwMultiplayer::auth_receive));
}

void NetwMultiplayer::auth_disarm() {
    if (inner.is_null()) {
        return;
    }
    const Callable ours = callable_mp(this, &NetwMultiplayer::auth_receive);
    if (inner->get_auth_callback() == ours) {
        inner->set_auth_callback(Callable());
    }
}

void NetwMultiplayer::auth_receive(
    int64_t p_peer,
    const PackedByteArray &p_data
) {
    const auth::Kind kind = auth::classify(p_data);
    if (kind == auth::Kind::PROBE) {
        session_answer_probe(p_peer);
        return;
    }
    if (auth_app_callback.is_valid()) {
        auth_app_callback.call(p_peer, p_data);
        return;
    }
    if (kind == auth::Kind::HELLO) {
        auth_receive_hello(p_peer, p_data);
        return;
    }
    NETW_WARN(
        sys::SESSION,
        "peer %d sent %d bytes of auth this session cannot name, so it is "
        "dropped rather than admitted",
        int(p_peer),
        p_data.size()
    );
    disconnect_peer(p_peer);
}

void NetwMultiplayer::auth_send_hello(int64_t p_peer) {
    NETW_ZONE_NC("netw::auth_send_hello", colors::SESSION);
    if (p_peer != int64_t(MultiplayerPeer::TARGET_PEER_SERVER)) {
        return;
    }
    if (auth_app_callback.is_valid() || inner.is_null()) {
        return;
    }
    if (preparing_join.is_valid() && !preparing_join->get_is_settled()) {
        NETW_DEBUG(
            sys::SESSION,
            "the hello for peer %d waits on the join preparation",
            int(p_peer)
        );
        held_hello_peer = p_peer;
        return;
    }
    PackedByteArray credentials;
    const Ref<NetwAuthFlow> flow = auth_effective_flow();
    if (flow.is_valid() && auth_request.has_value()) {
        credentials = flow->credentials(auth_request->username);
    }
    NETW_DEBUG(
        sys::SESSION,
        "auth sends a hello for peer %d with app tag %s and %d "
        "credential bytes",
        int(p_peer),
        String::num_uint64(auth_app_tag, 16),
        credentials.size()
    );
    const Error sent = send_auth(
        p_peer,
        auth::encode_client_hello(credentials, uint64_t(auth_app_tag), 0)
    );
    if (sent != OK) {
        NETW_ERROR(
            sys::SESSION,
            "auth could not send the hello for peer %d, error %d",
            int(p_peer),
            int(sent)
        );
        disconnect_peer(p_peer);
        return;
    }
    const Error completed = complete_auth(p_peer);
    if (completed != OK) {
        NETW_ERROR(
            sys::SESSION,
            "auth could not complete locally for peer %d, error %d",
            int(p_peer),
            int(completed)
        );
        disconnect_peer(p_peer);
    }
}

void NetwMultiplayer::auth_release_hello() {
    if (preparing_join.is_valid() && !preparing_join->get_is_settled()) {
        return;
    }
    preparing_join = Ref<NetwPromise>();
    const int64_t held = held_hello_peer;
    held_hello_peer = 0;
    if (held != 0) {
        auth_send_hello(held);
    }
}

void NetwMultiplayer::auth_receive_hello(
    int64_t p_peer,
    const PackedByteArray &p_data
) {
    NETW_ZONE_NC("netw::auth_receive_hello", colors::SESSION);
    const auth::Hello decoded
        = auth::decode_client_hello(p_data, uint64_t(auth_app_tag));
    NETW_DEBUG(
        sys::SESSION,
        "auth read a hello from peer %d with app tag %s against %s",
        int(p_peer),
        String::num_uint64(int64_t(decoded.app_tag), 16),
        String::num_uint64(auth_app_tag, 16)
    );
    if (!decoded.ok()) {
        if (decoded.refusal == auth::Refusal::APP) {
            NETW_WARN(
                sys::SESSION,
                "peer %d hello carries app tag %s against %s, so it is "
                "refused as an incompatible build",
                int(p_peer),
                String::num_uint64(int64_t(decoded.app_tag), 16),
                String::num_uint64(auth_app_tag, 16)
            );
            session_refuse(p_peer, "Incompatible game build");
        } else {
            NETW_WARN(
                sys::SESSION,
                "peer %d hello did not decode (%s), so it is dropped",
                int(p_peer),
                decoded.refusal == auth::Refusal::VERSION ? "version"
                                                          : "framing"
            );
        }
        disconnect_peer(p_peer);
        return;
    }
    const Ref<NetwAuthFlow> flow = auth_effective_flow();
    if (flow.is_null()) {
        if (auth_is_configured()) {
            NETW_WARN(
                sys::SESSION,
                "peer %d is refused: this session declares authentication "
                "and could not build it, so it admits nobody",
                int(p_peer)
            );
            session_refuse(
                p_peer,
                "This server could not start its own "
                "authentication"
            );
            disconnect_peer(p_peer);
            return;
        }
        NETW_TRACE(
            sys::SESSION,
            "no auth flow, so peer %d completes on the hello alone",
            int(p_peer)
        );
        complete_auth(p_peer);
        return;
    }
    const Ref<AuthResult> verdict
        = flow->verify(p_peer, decoded.provider_payload);
    if (verdict.is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "peer %d is refused: this session's authentication flow answered "
            "no AuthResult, and a flow that decides nothing has not accepted "
            "anyone",
            int(p_peer)
        );
        session_refuse(p_peer, "Authentication failed");
        disconnect_peer(p_peer);
        return;
    }
    if (verdict->get_accepted()) {
        const Ref<NetwIdentity> identity = verdict->get_identity();
        NETW_INFO(
            sys::SESSION,
            "peer %d is accepted as '%s'",
            int(p_peer),
            identity.is_valid() ? String(identity->get_username()) : String()
        );
        peer_set_identity(p_peer, identity);
        complete_auth(p_peer);
        return;
    }
    String reason = verdict->get_rejection_reason();
    if (reason.is_empty()) {
        reason = "Authentication failed";
    }
    session_refuse(p_peer, reason);
    NETW_DEBUG(
        sys::SESSION,
        "peer %d is rejected by this session's own policy: %s",
        int(p_peer),
        reason
    );
    disconnect_peer(p_peer);
}

session_decl::Book &NetwMultiplayer::declaration_book() {
    return declaration_slots;
}

Ref<NetwAuthFlow> NetwMultiplayer::auth_effective_flow() {
    if (auth_flow_override.is_valid()) {
        auth_flow_object = auth_flow_override;
        return auth_flow_override;
    }
    const session_decl::Resolved declared
        = declaration_slots.resolve(this, session_decl::KIND_AUTH);
    if (declared.state == session_decl::READY) {
        if (declared.generation != auth_flow_generation) {
            auth_flow_generation = declared.generation;
            auth_flow_declared = Ref<NetwAuthFlow>();
            auth_flow_broken = false;
        }
        if (auth_flow_declared.is_null() && !auth_flow_broken) {
            bool called = false;
            const Variant made = gd::call_checked(declared.callable, called);
            auth_flow_declared
                = Ref<NetwAuthFlow>(Object::cast_to<NetwAuthFlow>(made));
            if (!called || auth_flow_declared.is_null()) {
                auth_flow_broken = true;
                NETW_ERROR(
                    sys::SESSION,
                    "authentication is refused. The factory declared through "
                    "Netw.configure_auth answered %s, and a session needs a "
                    "NetwAuthFlow. Every peer is turned away until it does.",
                    called ? String(Variant::get_type_name(made.get_type()))
                           : String("nothing, because the call itself failed")
                );
            }
        }
        auth_flow_object = auth_flow_declared;
        return auth_flow_declared;
    }
    if (auth_flow_declared.is_valid()) {
        auth_flow_object = auth_flow_declared;
        return auth_flow_declared;
    }
    if (declared.state != session_decl::ABSENT) {
        declaration_slots.report_unresolved(
            this,
            session_decl::KIND_AUTH,
            declared.state,
            "authentication"
        );
        auth_flow_broken = true;
    }
    auth_flow_object = Ref<NetwAuthFlow>();
    return auth_flow_object;
}

bool NetwMultiplayer::auth_is_configured() {
    if (auth_flow_override.is_valid() || auth_flow_declared.is_valid()
        || auth_flow_broken) {
        return true;
    }
    return declaration_slots.resolve(this, session_decl::KIND_AUTH).state
        != session_decl::ABSENT;
}

void NetwMultiplayer::auth_set_flow(const Ref<NetwAuthFlow> &p_flow) {
    auth_flow_override = p_flow;
    auth_flow_object = p_flow;
}

PackedByteArray NetwMultiplayer::probe_reply_payload(bool &r_answered) {
    r_answered = true;
    const Ref<NetwServerInfo> base = NetwServerInfo::from_session(this);
    const session_decl::Resolved declared
        = declaration_slots.resolve(this, session_decl::KIND_SERVER_INFO);
    if (declared.state == session_decl::ABSENT) {
        return NetwServerInfo::to_payload(base);
    }
    if (declared.state != session_decl::READY) {
        declaration_slots.report_unresolved(
            this,
            session_decl::KIND_SERVER_INFO,
            declared.state,
            "a probe"
        );
        r_answered = false;
        return PackedByteArray();
    }
    bool called = false;
    const Variant answered
        = gd::call_checked(declared.callable, Variant(base), called);
    const Ref<NetwServerInfo> info
        = Ref<NetwServerInfo>(Object::cast_to<NetwServerInfo>(answered));
    if (!called || info.is_null()) {
        NETW_ERROR(
            sys::TRANSPORT,
            "a probe is refused: the provider declared through "
            "Netw.configure_server_info answered %s, and a probe reply needs "
            "a NetwServerInfo. Edit the record the provider is handed and "
            "return it.",
            called ? String(Variant::get_type_name(answered.get_type()))
                   : String("nothing, because the call itself failed")
        );
        r_answered = false;
        return PackedByteArray();
    }
    return NetwServerInfo::to_payload(info);
}

void NetwMultiplayer::session_answer_probe(int64_t p_peer) {
    NETW_ZONE_NC("netw::session_answer_probe", colors::TRANSPORT);
    if (inner.is_null()) {
        return;
    }
    const int64_t now = int64_t(Time::get_singleton()->get_ticks_msec());
    if (!probe_guard.admit(p_peer, now)) {
        NETW_TRACE(
            sys::TRANSPORT,
            "probe from peer %d answered busy (%d in the second, %d tracked)",
            int(p_peer),
            probe_guard.window_count(),
            probe_guard.active_count()
        );
        send_auth(
            p_peer,
            auth::encode_probe_reply(
                int(auth::ProbeStatus::BUSY),
                PackedByteArray()
            )
        );
        return;
    }
    bool answered = false;
    const PackedByteArray payload = probe_reply_payload(answered);
    send_auth(
        p_peer,
        auth::encode_probe_reply(
            int(answered ? auth::ProbeStatus::OK : auth::ProbeStatus::ERROR),
            payload
        )
    );
}

bool NetwMultiplayer::probe_forget(int64_t p_peer) {
    return probe_guard.forget(p_peer);
}

void NetwMultiplayer::probe_clear() {
    probe_guard.clear();
}

void NetwMultiplayer::disconnect_peer(int64_t p_peer) {
    if (inner.is_null()) {
        return;
    }
    inner->disconnect_peer(int32_t(p_peer));
}

void NetwMultiplayer::clear() {
    if (inner.is_null()) {
        return;
    }
    inner->clear();
}

bool NetwMultiplayer::peer_has_bucket(
    int64_t p_peer,
    const Variant &p_type
) const {
    const Dictionary *held = peer_buckets.getptr(p_peer);
    return held != nullptr && held->has(p_type);
}

Variant NetwMultiplayer::peer_get_bucket(
    int64_t p_peer,
    const Variant &p_type
) {
    NETW_ERR_COND_V(
        peer_get_accepted_join(p_peer).is_null(),
        Variant(),
        sys::SESSION,
        "the session holds no peer %d, so it mints no bucket for one",
        int(p_peer)
    );
    Object *type = Object::cast_to<Object>(p_type);
    NETW_ERR_COND_V(
        type == nullptr,
        Variant(),
        sys::SESSION,
        "a bucket type must be a script object that answers new()"
    );
    if (!peer_buckets.has(p_peer)) {
        peer_buckets.insert(p_peer, Dictionary());
    }
    Dictionary &held = peer_buckets[p_peer];
    if (held.has(p_type)) {
        return held[p_type];
    }
    const Ref<RefCounted> minted
        = Ref<RefCounted>(type->call(StringName("new")));
    NETW_ERR_COND_V(
        minted.is_null(),
        Variant(),
        sys::SESSION,
        "a bucket type must extend RefCounted"
    );
    held[p_type] = minted;
    return minted;
}

void NetwMultiplayer::session_refuse(int64_t p_peer, const String &p_reason) {
    join_roster.refuse(p_peer, p_reason);
}

String NetwMultiplayer::session_refusal(int64_t p_peer) const {
    return join_roster.refusal(p_peer);
}

int NetwMultiplayer::session_name_verdict(
    const StringName &p_name,
    const PackedStringArray &p_taken,
    bool p_renames_on_collision,
    bool p_has_identity
) const {
    return join_roster
        .name_verdict(p_name, p_taken, p_renames_on_collision, p_has_identity);
}

StringName NetwMultiplayer::session_free_name(
    const StringName &p_name,
    const PackedStringArray &p_taken
) const {
    return join_roster.free_name(p_name, p_taken);
}

PackedStringArray NetwMultiplayer::session_seated_names(
    const TypedArray<NetwEntity> &p_seated
) const {
    PackedStringArray taken;
    for (int at = 0; at < p_seated.size(); at++) {
        const Ref<NetwEntity> seated = p_seated[at];
        if (seated.is_null()) {
            continue;
        }
        const StringName stamped = seated->get_entity_id();
        if (!String(stamped).is_empty()) {
            taken.push_back(String(stamped));
            continue;
        }
        Node *owner = seated->get_owner();
        if (owner == nullptr) {
            continue;
        }
        const String parsed = String(owner->get_name()).get_slice("|", 0);
        if (!parsed.is_empty()) {
            taken.push_back(parsed);
        }
    }
    return taken;
}

bool NetwMultiplayer::session_admit_username(
    const Ref<ResolvedJoin> &p_join,
    const TypedArray<NetwEntity> &p_seated,
    const Callable &p_disconnect
) {
    NETW_ZONE_NC("netw::session_admit_username", colors::SESSION);
    if (p_join.is_null()) {
        NETW_TRACE(sys::SESSION, "no join row claims a username to admit");
        return false;
    }
    const PackedStringArray taken = session_seated_names(p_seated);
    const StringName claimed = p_join->get_username();
    const int64_t peer = p_join->get_peer_id();
    const int verdict = join_roster.name_verdict(
        claimed,
        taken,
        OS::get_singleton()->has_feature("debug"),
        peer_get_identity(peer).is_valid()
    );
    if (verdict == JoinRoster::RENAME) {
        const StringName renamed = join_roster.free_name(claimed, taken);
        NETW_INFO(
            sys::SESSION,
            "Debug name collision: renaming %s to %s",
            String(claimed),
            String(renamed)
        );
        p_join->set_username(renamed);
        return true;
    }
    if (verdict == JoinRoster::REFUSE) {
        const String reason
            = String("Username '") + String(claimed) + "' is already in use";
        session_refuse(peer, reason);
        NETW_ERROR(
            sys::SESSION,
            "Authenticated username collision for '%s'. Rejecting join.",
            String(claimed)
        );
        if (p_disconnect.is_valid()) {
            p_disconnect.call(peer);
        }
        return false;
    }
    if (taken.find(String(claimed)) != -1) {
        NETW_WARN(
            sys::SESSION,
            "Username collision detected for '%s'. Topology nameplates may "
            "break.",
            String(claimed)
        );
    }
    return true;
}

String NetwMultiplayer::session_role_name(Role p_role) {
    switch (p_role) {
        case ROLE_NONE:
            return "NONE";
        case ROLE_CLIENT:
            return "CLIENT";
        case ROLE_DEDICATED_SERVER:
            return "DEDICATED_SERVER";
        case ROLE_LISTEN_SERVER:
            return "LISTEN_SERVER";
        default:
            NETW_TRACE(sys::SESSION, "no role name for role %d", p_role);
            return "";
    }
}

void NetwMultiplayer::session_set_state(SessionState p_state) {
    session_core.set_state(SessionCore::State(p_state));
}

void NetwMultiplayer::session_set_role(Role p_role) {
    session_core.set_role(SessionCore::Role(p_role));
}

void NetwMultiplayer::session_set_desired_role(Role p_role) {
    session_core.set_desired_role(SessionCore::Role(p_role));
}

void NetwMultiplayer::session_transition(SessionState p_state) {
    session_core.transition(SessionCore::State(p_state));
}

void NetwMultiplayer::session_peer_assigned(
    bool p_live,
    bool p_connected,
    int p_unique_id
) {
    session_core.on_peer_assigned(p_live, p_connected, p_unique_id);
}

void NetwMultiplayer::session_resolve_online(int p_unique_id) {
    session_core.resolve_online(p_unique_id);
}

void NetwMultiplayer::session_set_join_override(
    const Callable &p_handler,
    const Array &p_quantizers
) {
    join_handler_override = p_handler;
    join_handler_override_quantizers = p_quantizers;
}

Callable NetwMultiplayer::session_join_override() const {
    return join_handler_override.is_valid() ? join_handler_override
                                            : Callable();
}

Array NetwMultiplayer::session_join_override_quantizers() const {
    return join_handler_override.is_valid() ? join_handler_override_quantizers
                                            : Array();
}

int64_t NetwMultiplayer::session_app_tag(const StringName &p_app_id) {
    return SessionCore::compute_app_tag(p_app_id);
}

void NetwMultiplayer::session_broadcast_control(
    uint8_t p_channel,
    const PackedByteArray &p_payload
) {
    if (inner.is_valid()) {
        set_peer_ids(gd::api_peer_ids(inner));
    }
    const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
    for (int at = 0; at < int(peers.size()); ++at) {
        send_to(peers[at], 0, p_channel, p_payload, true, 0, String(), false);
    }
    session_publish_control(
        p_channel,
        MultiplayerPeer::TARGET_PEER_SERVER,
        p_payload
    );
}

void NetwMultiplayer::session_pause(const String &p_reason) {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "a pause is issued by server authority"
    );
    session_broadcast_control(
        control_channels.pause,
        session::frame_write(session::SessionReason{p_reason})
    );
}

void NetwMultiplayer::session_unpause() {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "an unpause is issued by server authority"
    );
    session_broadcast_control(control_channels.unpause, PackedByteArray());
}

void NetwMultiplayer::session_notify_shutdown(const String &p_reason) {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "a shutdown notice is issued by server authority"
    );
    session_broadcast_control(
        control_channels.shutdown,
        session::frame_write(session::SessionReason{p_reason})
    );
}

void NetwMultiplayer::session_request_leave(const String &p_reason) {
    const PackedByteArray payload
        = session::frame_write(session::SessionReason{p_reason});
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

void NetwMultiplayer::peer_kick(int64_t p_peer_id, const String &p_reason) {
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
            session::frame_write(session::SessionReason{p_reason}),
            true,
            0,
            String(),
            false
        );
    }
    if (effective_peer.is_valid()) {
        effective_peer->disconnect_peer(int32_t(p_peer_id));
    }
}

void NetwMultiplayer::peer_request_kick(
    int64_t p_peer_id,
    const String &p_reason
) {
    const PackedByteArray payload
        = session::frame_write(session::KickRequest{p_peer_id, p_reason});
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

NetwMultiplayer::~NetwMultiplayer() {
    capture_close();
    clock_release();
    lagcomp_release();
    declaration_slots.release();
    persistence_drain_forget();
    if (replication_owner != nullptr) {
        memdelete(replication_owner);
        replication_owner = nullptr;
    }
    for (const KeyValue<int64_t, NetwPredictSlotEngine *> &row :
         predict_engines) {
        godot::memdelete(row.value);
    }
    predict_engines.clear();
    for (const KeyValue<uint64_t, RID> &held : directory_slots) {
        connect::free_transport_slot(held.value);
    }
    directory_slots.clear();
}

Error NetwMultiplayer::session_flush_tick(int64_t p_tick) {
    NETW_ZONE_SYS(profile::SUBSYSTEM_SESSION);
    NETW_ZONE_COLOR(colors::SESSION);
    if (layer_driver_count() > 0) {
        const Error admitted = interest_flush_now();
        if (admitted != OK) {
            return admitted;
        }
    }
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->on_clock_tick(p_tick);
    }
    const Error err = embed_poll_transport();
    settle_advance();
    session_flush_deferred();
    scene_pump_retired();
    return err;
}

void NetwMultiplayer::session_on_clock_tick(double, int64_t p_tick) {
    sink_verdict(session_flush_tick(p_tick), 0);
}

Error NetwMultiplayer::NETW_API_VIRTUAL(poll)() {
    NETW_ZONE_SYS(profile::SUBSYSTEM_SESSION);
    NETW_ZONE_COLOR(colors::SESSION);
    Engine *engine = Engine::get_singleton();
    if (engine != nullptr && engine->is_editor_hint()) {
        return OK;
    }
    embed_autosettle_tree_default();
    if (inner.is_null() && effective_peer.is_valid()) {
        effective_peer->poll();
    }
    peer_sweep_transport_state();
    const double frame_delta
        = poll_delta(Time::get_singleton()->get_ticks_usec());
    connect_core.on_poll(frame_delta);
    const Error err = embed_poll_transport();
    const bool clocked = clock_engine().get_configured();
    if (!clocked) {
        settle_advance();
    }
    session_flush_deferred();
    if (!clocked) {
        scene_pump_retired();
    }
    clock_engine().count_poll();
    if (config_is_consumed(session_decl::KIND_CLOCK_CONFIG)) {
        clock_attach_pump();
    }
    advance_frame();
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->on_poll();
    }
    Object *seam = session_seam(StringName("_persist_tick"));
    if (seam != nullptr) {
        seam->call("_persist_tick", frame_delta);
    } else {
        persist_pump(frame_delta);
    }
    sink_verdict(display_pump(frame_delta), 0);
    return err;
}

bool NetwMultiplayer::peer_assignment_repeats(
    const Ref<MultiplayerPeer> &p_peer
) const {
    if (p_peer == assigned_peer) {
        return true;
    }
    return p_peer.is_valid() && p_peer == effective_peer;
}

Ref<MultiplayerPeer> NetwMultiplayer::peer_shaped(
    const Ref<MultiplayerPeer> &p_peer
) const {
    if (p_peer.is_null() || p_peer->is_class(LAGGY_PEER_CLASS)
        || p_peer->is_class("OfflineMultiplayerPeer")) {
        return p_peer;
    }
    const Ref<NetwLinkConditions> shaping = session_settings.link_conditions;
    if (shaping.is_null()) {
        return p_peer;
    }
    const Ref<MultiplayerPeer> shaped = shaping->wrap_peer(p_peer);
    return shaped.is_valid() ? shaped : p_peer;
}

void NetwMultiplayer::NETW_API_VIRTUAL(set_multiplayer_peer)(
    const Ref<MultiplayerPeer> &p_peer
) {
    if (peer_assignment_repeats(p_peer)) {
        return;
    }
    const connect::PeerOffer *offered = connect::offer_of_peer(p_peer);
    NETW_ERR_COND(
        offered != nullptr && !offered->claimed
            && offered->session != gd::instance_id(this),
        sys::TRANSPORT,
        "a created peer is offered to the session that asked for it, and this "
        "one belongs to another session, so the standing assignment stands"
    );
    NETW_ERR_COND(
        p_peer.is_valid()
            && p_peer->get_connection_status()
                == MultiplayerPeer::CONNECTION_DISCONNECTED,
        sys::TRANSPORT,
        "an assigned peer is connecting or connected, and this one is "
        "disconnected, so the standing assignment stands"
    );
    const bool live
        = p_peer.is_valid() && !p_peer->is_class("OfflineMultiplayerPeer");
    if (live) {
        String pending;
        NETW_ERR_COND(
            config_readiness(pending) != OK,
            sys::SESSION,
            "assigning a peer refused: this session's configuration is still "
            "being authored. Start through Netw.connection(node) after "
            "declaring configuration, or wait for the deferred configuration "
            "to settle before assigning a peer directly."
        );
        session_apply_auth_config();
    }
    const Ref<MultiplayerPeer> standing_assigned = assigned_peer;
    const Ref<MultiplayerPeer> standing_effective = effective_peer;
    assigned_peer = p_peer;
    effective_peer = peer_shaped(p_peer);
    if (inner.is_valid()) {
        inner->set_multiplayer_peer(effective_peer);
        if (inner->get_multiplayer_peer() != effective_peer) {
            assigned_peer = standing_assigned;
            effective_peer = standing_effective;
            return;
        }
    }
    session_push_desired_role();
    session_peer_assigned(
        live,
        live
            && p_peer->get_connection_status()
                == MultiplayerPeer::CONNECTION_CONNECTED,
        live ? p_peer->get_unique_id() : 0
    );
    connect_core.on_peer_assigned(p_peer);
}

RID NetwMultiplayer::endpoint_add(
    const RID &p_transport,
    const String &p_address,
    const String &p_display_name
) {
    if (transport_held(p_transport) == nullptr) {
        return RID();
    }
    return connect_core.target_add(p_transport, p_address, p_display_name);
}

void NetwMultiplayer::endpoint_remove(const RID &p_endpoint) {
    connect_core.target_remove(p_endpoint);
}

RID NetwMultiplayer::endpoint_find(
    const RID &p_transport,
    const String &p_address
) {
    return connect_core.list().find(p_transport, p_address);
}

Array NetwMultiplayer::endpoint_list() {
    return connect_core.list().list();
}

Variant NetwMultiplayer::endpoint_get_param(
    const RID &p_endpoint,
    EndpointParam p_param
) {
    const connect::TargetRow *held = connect_core.list().row(p_endpoint);
    if (held == nullptr) {
        return Variant();
    }
    switch (p_param) {
        case ENDPOINT_PARAM_TRANSPORT:
            return held->transport;
        case ENDPOINT_PARAM_ADDRESS:
            return held->address;
        case ENDPOINT_PARAM_DISPLAY_NAME:
            return held->display_name;
        default:
            return Variant();
    }
}

Error NetwMultiplayer::endpoint_set_param(
    const RID &p_endpoint,
    EndpointParam p_param,
    const Variant &p_value
) {
    connect::TargetRow *held = connect_core.list().row(p_endpoint);
    if (held == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (p_param != ENDPOINT_PARAM_DISPLAY_NAME) {
        return ERR_INVALID_PARAMETER;
    }
    held->display_name = String(p_value);
    endpoint_emit_updated(p_endpoint);
    return OK;
}

Variant NetwMultiplayer::endpoint_get_state(
    const RID &p_endpoint,
    EndpointState p_state
) {
    const connect::TargetRow *held = connect_core.list().row(p_endpoint);
    if (held == nullptr) {
        return Variant();
    }
    switch (p_state) {
        case ENDPOINT_STATE_FLAGS: {
            int64_t flags = 0;
            if (connect_core.list().is_caller_row(p_endpoint)) {
                flags |= ENDPOINT_FLAG_CALLER;
            }
            if (connect_core.target_is_available(p_endpoint)) {
                flags |= ENDPOINT_FLAG_AVAILABLE;
            }
            if (held->observed) {
                flags |= ENDPOINT_FLAG_OBSERVED;
            }
            return flags;
        }
        case ENDPOINT_STATE_STATUS:
            return held->observed ? int64_t(held->status) : int64_t(FAILED);
        case ENDPOINT_STATE_INFO:
            return held->info;
        default:
            return Variant();
    }
}

void NetwMultiplayer::endpoint_probe(const RID &p_endpoint) {
    connect_core.probe_target(p_endpoint);
}

void NetwMultiplayer::endpoint_refresh() {
    connect_core.refresh();
}

const connect::TransportSlot *NetwMultiplayer::transport_held(
    const RID &p_transport
) const {
    const connect::TransportSlot *slot = connect::transport_slot(p_transport);
    if (slot == nullptr) {
        return nullptr;
    }
    if (slot->session != ObjectID() && slot->session != gd::instance_id(this)) {
        return nullptr;
    }
    return slot;
}

StringName NetwMultiplayer::transport_class_of(const RID &p_transport) const {
    const connect::TransportSlot *slot = transport_held(p_transport);
    return slot != nullptr ? slot->peer_class : StringName();
}

RID NetwMultiplayer::transport_create_peer(
    const RID &p_transport,
    int64_t p_mode,
    const String &p_address,
    const Dictionary &p_settings,
    const Callable &p_completed,
    const Callable &p_progress
) {
    return connect_core.create_peer(
        p_transport,
        int(p_mode),
        p_address,
        p_settings,
        p_completed,
        p_progress
    );
}

void NetwMultiplayer::transport_cancel_peer_creation(const RID &p_ticket) {
    connect_core.cancel_peer_creation(p_ticket);
}

RID NetwMultiplayer::transport_register(const Ref<Script> &p_type) {
    const Ref<Script> script
        = netw::script::model::declaring_script(p_type.ptr());
    if (script.is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "transport_register: expected a Script extending NetwTransport"
        );
        return RID();
    }
    return connect_core.register_transport(script);
}

Error NetwMultiplayer::transport_unregister(const RID &p_transport) {
    return connect_core.unregister_transport(p_transport);
}

RID NetwMultiplayer::transport_find_script(const Ref<Script> &p_type) const {
    return connect_core.registry().slot_of_script(
        netw::script::model::declaring_script(p_type.ptr())
    );
}

Array NetwMultiplayer::transport_list() {
    Array held = connect_core.registry().slots();
    Array named = connect_core.registry().peer_classes();
    const Array stock = connect::TransportBook::shared().slots();
    const Array stock_named = connect::TransportBook::shared().peer_classes();
    for (int64_t at = 0; at < stock.size(); at++) {
        if (named.has(stock_named[at])) {
            continue;
        }
        named.push_back(stock_named[at]);
        held.push_back(stock[at]);
    }
    for (const KeyValue<uint64_t, RID> &row : directory_slots) {
        const connect::TransportSlot *slot = connect::transport_slot(row.value);
        if (slot == nullptr || named.has(slot->peer_class)) {
            continue;
        }
        named.push_back(slot->peer_class);
        held.push_back(row.value);
    }
    return held;
}

RID NetwMultiplayer::transport_find(const StringName &p_peer_class) {
    const RID registered = connect_core.registry().slot_of(p_peer_class);
    if (registered.is_valid()) {
        return registered;
    }
    const RID booked = connect::TransportBook::shared().slot_of(p_peer_class);
    if (booked.is_valid()) {
        return booked;
    }
    for (const KeyValue<uint64_t, RID> &row : directory_slots) {
        const connect::TransportSlot *slot = connect::transport_slot(row.value);
        if (slot != nullptr && slot->peer_class == p_peer_class) {
            return row.value;
        }
    }
    return RID();
}

Variant NetwMultiplayer::transport_get_param(
    const RID &p_transport,
    TransportParam p_param
) {
    const connect::TransportSlot *slot = transport_held(p_transport);
    if (slot == nullptr) {
        return Variant();
    }
    if (p_param == TRANSPORT_PARAM_PEER_CLASS) {
        return slot->peer_class;
    }
    connect::TransportFacts facts;
    if (!connect_core.transport_facts(*slot, facts)) {
        return Variant();
    }
    switch (p_param) {
        case TRANSPORT_PARAM_DISPLAY_NAME:
            return facts.display_name;
        case TRANSPORT_PARAM_ADDRESS_LABEL:
            return facts.address_label;
        case TRANSPORT_PARAM_ADDRESS_PLACEHOLDER:
            return facts.address_placeholder;
        case TRANSPORT_PARAM_ADDRESS_HELP:
            return facts.address_help;
        case TRANSPORT_PARAM_HOST_SETTINGS:
            return facts.host_settings;
        case TRANSPORT_PARAM_CLIENT_SETTINGS:
            return facts.client_settings;
        case TRANSPORT_PARAM_CAPABILITIES: {
            int64_t flags = 0;
            flags |= facts.is_available ? TRANSPORT_AVAILABLE : 0;
            flags |= facts.can_host_here ? TRANSPORT_CAN_HOST : 0;
            flags |= facts.can_probe ? TRANSPORT_CAN_PROBE : 0;
            flags |= facts.can_browse ? TRANSPORT_CAN_BROWSE : 0;
            flags |= facts.accepts_empty_address
                ? TRANSPORT_ACCEPTS_EMPTY_ADDRESS
                : 0;
            return flags;
        }
        default:
            return Variant();
    }
}

Array NetwMultiplayer::session_get_join_schema() {
    Array result;
    const JoinPlan plan = session_resolve_join();
    if (!plan.available || !plan.handler.is_valid()) {
        return result;
    }
    const Callable handler = plan.handler;
    const Array types = join_arg_types(handler);

    PackedStringArray names;
    Object *target = handler.get_object();
    const Variant script_var
        = target != nullptr ? target->get_script() : Variant();
    const Ref<Script> script = script_var;
    if (script.is_valid()) {
        const Array methods = gd::script_method_list(script);
        for (int at = 0; at < methods.size(); ++at) {
            const Dictionary method = methods[at];
            if (StringName(method.get("name", StringName()))
                != handler.get_method()) {
                continue;
            }
            const Array args = method.get("args", Array());
            for (int arg_at = 1; arg_at < args.size(); ++arg_at) {
                const Dictionary arg = args[arg_at];
                names.push_back(String(arg.get("name", String())));
            }
            break;
        }
    }

    for (int at = 0; at < types.size(); ++at) {
        Dictionary entry;
        entry["name"] = at < names.size() && !names[at].is_empty()
            ? names[at]
            : String("arg") + String::num_int64(at);
        entry["type"] = types[at];
        entry["class_name"] = StringName();
        entry["default"] = Variant();
        result.push_back(entry);
    }
    return result;
}

connect::ProbeHooks NetwMultiplayer::discovery_probe_hooks() {
    connect::ProbeHooks made;
    made.authenticating
        = callable_mp(this, &NetwMultiplayer::discovery_probe_authenticating);
    made.auth_received
        = callable_mp(this, &NetwMultiplayer::discovery_probe_auth_received);
    made.connection_failed = callable_mp(
        this,
        &NetwMultiplayer::discovery_probe_connection_failed
    );
    made.authentication_failed = callable_mp(
        this,
        &NetwMultiplayer::discovery_probe_authentication_failed
    );
    return made;
}

void NetwMultiplayer::discovery_probe_authenticating(int64_t p_peer_id) {
    connect::ProbeClient *reached = connect_core.probe_client();
    if (reached != nullptr) {
        reached->on_authenticating(p_peer_id);
    }
}

void NetwMultiplayer::discovery_probe_auth_received(
    int64_t p_peer_id,
    const PackedByteArray &p_data
) {
    connect::ProbeClient *reached = connect_core.probe_client();
    if (reached != nullptr) {
        reached->on_auth_received(p_peer_id, p_data);
    }
}

void NetwMultiplayer::discovery_probe_connection_failed() {
    connect::ProbeClient *reached = connect_core.probe_client();
    if (reached != nullptr) {
        reached->on_connection_failed();
    }
}

void NetwMultiplayer::discovery_probe_authentication_failed(int64_t p_peer_id) {
    connect::ProbeClient *reached = connect_core.probe_client();
    if (reached != nullptr) {
        reached->on_authentication_failed(p_peer_id);
    }
}

void NetwMultiplayer::discovery_publish(
    const StringName &p_peer_class,
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const Array &p_infos
) {
    connect_core.publish_targets(p_peer_class, p_addresses, p_names, p_infos);
}

void NetwMultiplayer::endpoint_emit_added(const RID &p_endpoint) {
    emit_signal(StringName("endpoint_added"), p_endpoint);
}

void NetwMultiplayer::endpoint_emit_removed(const RID &p_endpoint) {
    emit_signal(StringName("endpoint_removed"), p_endpoint);
}

void NetwMultiplayer::endpoint_emit_updated(const RID &p_endpoint) {
    emit_signal(StringName("endpoint_updated"), p_endpoint);
}

String NetwMultiplayer::peer_join_address() {
    return connect_core.join_address();
}

Dictionary NetwMultiplayer::peer_diagnostics(int64_t p_peer_id) {
    return connect_core.diagnostics(p_peer_id);
}

Object *NetwMultiplayer::discovery_find_directory(
    const StringName &p_peer_class
) const {
    for (uint32_t at = 0; at < services.size(); at++) {
        Object *service = gd::instance_from_id(services[at].service);
        if (connect::DirectoryTransport::peer_class_of_directory(service)
            == p_peer_class) {
            return service;
        }
    }
    return nullptr;
}

Array NetwMultiplayer::discovery_directory_peer_classes() const {
    Array classes;
    for (uint32_t at = 0; at < services.size(); at++) {
        Object *service = gd::instance_from_id(services[at].service);
        const StringName named
            = connect::DirectoryTransport::peer_class_of_directory(service);
        if (named != StringName() && !classes.has(named)) {
            classes.push_back(named);
        }
    }
    return classes;
}

void NetwMultiplayer::discovery_watch_directory(Object *p_directory) {
    if (!connect::DirectoryTransport::is_directory(p_directory)) {
        return;
    }
    const int64_t held = int64_t(uint64_t(gd::instance_id(p_directory)));
    const StringName ready(connect::DirectoryTransport::SIG_PEER_READY);
    const StringName failed(connect::DirectoryTransport::SIG_FAILED);
    const StringName listed(connect::DirectoryTransport::SIG_LIST_PUBLISHED);
    const Callable on_ready
        = callable_mp(this, &NetwMultiplayer::discovery_directory_delivered)
              .bind(held);
    const Callable on_failed
        = callable_mp(this, &NetwMultiplayer::discovery_directory_failed)
              .bind(held);
    const Callable on_listed
        = callable_mp(this, &NetwMultiplayer::discovery_directory_listed)
              .bind(held);
    if (p_directory->has_signal(ready)
        && !p_directory->is_connected(ready, on_ready)) {
        p_directory->connect(ready, on_ready);
    }
    if (p_directory->has_signal(failed)
        && !p_directory->is_connected(failed, on_failed)) {
        p_directory->connect(failed, on_failed);
    }
    if (p_directory->has_signal(listed)
        && !p_directory->is_connected(listed, on_listed)) {
        p_directory->connect(listed, on_listed);
    }
    const StringName named
        = connect::DirectoryTransport::peer_class_of_directory(p_directory);
    if (named != StringName() && !directory_slots.has(uint64_t(held))) {
        directory_slots.insert(
            uint64_t(held),
            connect::mint_directory_slot(
                named,
                gd::instance_id(this),
                gd::instance_id(p_directory)
            )
        );
    }
}

void NetwMultiplayer::discovery_unwatch_directory(Object *p_directory) {
    if (p_directory == nullptr) {
        return;
    }
    const HashMap<uint64_t, RID>::Iterator held
        = directory_slots.find(uint64_t(gd::instance_id(p_directory)));
    if (held == directory_slots.end()) {
        return;
    }
    connect::free_transport_slot(held->value);
    directory_slots.remove(held);
}

void NetwMultiplayer::discovery_directory_delivered(
    const Ref<MultiplayerPeer> &p_peer,
    int64_t p_directory
) {
    connect_core.on_directory_delivered(p_peer, p_directory);
}

void NetwMultiplayer::discovery_directory_failed(
    int64_t p_error,
    const String &p_message,
    int64_t p_directory
) {
    connect_core.on_directory_failed(p_error, p_message, p_directory);
}

void NetwMultiplayer::discovery_directory_listed(
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const Array &p_infos,
    int64_t p_directory
) {
    connect_core
        .on_directory_listed(p_addresses, p_names, p_infos, p_directory);
}

Ref<NetwConnectHandle> NetwMultiplayer::get_connection() {
    if (connection_handle.is_null()) {
        connection_handle.instantiate();
        connection_handle->bind_session(this);
    }
    return connection_handle;
}

Ref<NetwSessionHandle> NetwMultiplayer::get_session() {
    if (session_handle.is_null()) {
        session_handle.instantiate();
        session_handle->bind_session(this);
    }
    return session_handle;
}

Ref<NetwClockHandle> NetwMultiplayer::get_clock() {
    if (clock_handle.is_null()) {
        clock_handle.instantiate();
        clock_handle->bind_session(this);
    }
    return clock_handle;
}

Ref<MultiplayerPeer> NetwMultiplayer::NETW_API_VIRTUAL(get_multiplayer_peer)() {
    return assigned_peer;
}

int32_t NetwMultiplayer::NETW_API_VIRTUAL(get_unique_id)() NETW_API_CONST {
    if (!assigned_peer.is_valid()
        || assigned_peer->get_connection_status()
            == MultiplayerPeer::CONNECTION_DISCONNECTED) {
        return 1;
    }
    return assigned_peer->get_unique_id();
}

PackedInt32Array NetwMultiplayer::
    NETW_API_VIRTUAL(get_peer_ids)() NETW_API_CONST {
    if (assigned_peer.is_valid() && inner.is_valid()) {
        return gd::api_peer_ids(inner);
    }
    return peer_ids;
}

int32_t NetwMultiplayer::
    NETW_API_VIRTUAL(get_remote_sender_id)() NETW_API_CONST {
    if (dispatching_sender != 0) {
        return int32_t(dispatching_sender);
    }
    if (inner.is_null()) {
        return 0;
    }
    return int32_t(inner->get_remote_sender_id());
}

Error NetwMultiplayer::NETW_API_VIRTUAL(object_configuration_add)(
    Object *p_object,
    NETW_API_CONFIG_ARG p_configuration
) {
    Object *declared = p_configuration.get_validated_object();
    const Error refused = configuration_door_refuses_config(declared);
    if (refused != OK) {
        return refused;
    }
    ReplicationCore *plane = (get_replication_plane());
    Node *node = Object::cast_to<Node>(p_object);
    MultiplayerSpawner *spawner = Object::cast_to<MultiplayerSpawner>(declared);
    if (plane != nullptr && spawner != nullptr) {
        return plane->get_spawner_compat()->consume(node, spawner);
    }
    MultiplayerSynchronizer *sync
        = Object::cast_to<MultiplayerSynchronizer>(declared);
    if (plane != nullptr && node != nullptr && sync != nullptr) {
        return plane->get_sync_compat()->consume(node, sync);
    }
    return inner->object_configuration_add(p_object, p_configuration);
}

Error NetwMultiplayer::NETW_API_VIRTUAL(object_configuration_remove)(
    Object *p_object,
    NETW_API_CONFIG_ARG p_configuration
) {
    Object *declared = p_configuration.get_validated_object();
    const Error refused = configuration_door_refuses_config(declared);
    if (refused != OK) {
        return refused;
    }
    ReplicationCore *plane = (get_replication_plane());
    Node *node = Object::cast_to<Node>(p_object);
    MultiplayerSpawner *spawner = Object::cast_to<MultiplayerSpawner>(declared);
    if (plane != nullptr && spawner != nullptr) {
        return plane->get_spawner_compat()->consume_remove(node, spawner);
    }
    MultiplayerSynchronizer *sync
        = Object::cast_to<MultiplayerSynchronizer>(declared);
    if (plane != nullptr && node != nullptr && sync != nullptr) {
        return plane->get_sync_compat()->consume_remove(node, sync);
    }
    return inner->object_configuration_remove(p_object, p_configuration);
}

void NetwMultiplayer::set_peer_ids(const PackedInt32Array &p_peer_ids) {
    peer_ids = p_peer_ids;
}

Ref<NetwLivenessCore> NetwMultiplayer::get_liveness_core() const {
    return liveness_core;
}

Ref<NetwSceneCore> NetwMultiplayer::get_scene_core() const {
    return scene_core;
}

JoinRoster &NetwMultiplayer::join_book() {
    return join_roster;
}

Ref<display::Book> NetwMultiplayer::get_display_book() const {
    return display_book;
}

NetwChannelBook *NetwMultiplayer::get_channel_book() {
    return &channel_book;
}

NetwLagCompCore *NetwMultiplayer::get_lagcomp_core() {
    return &lagcomp_core;
}

NetwPredictionEngine *NetwMultiplayer::get_prediction_engine() {
    return &prediction_engine;
}

void NetwMultiplayer::set_interest_compat_refresh(const Callable &p_refresh) {
    interest_compat_refresh = p_refresh;
}

void NetwMultiplayer::set_interest_awareness_send(const Callable &p_send) {
    interest_awareness_send = p_send;
}

void NetwMultiplayer::interest_flush_sink() {
    sink_verdict(interest_flush_now(), 0);
}

void NetwMultiplayer::set_interest_flush(const Callable &p_flush) {
    interest_flush = p_flush;
}

namespace {

uint64_t clock_wall_usec() {
    const godot::Time *reading = godot::Time::get_singleton();
    return reading ? reading->get_ticks_usec() : 0;
}

PackedByteArray clock_rate_frame(int p_tickrate) {
    session::ClockRate rate;
    rate.tickrate = uint64_t(MAX(p_tickrate, 0));
    return session::frame_write(rate);
}

} // namespace

void NetwMultiplayer::clock_set_mismatch_action(MismatchAction p_action) {
    clock_mismatch_action = p_action;
}

void NetwMultiplayer::clock_request_handshake() {
    NETW_ZONE_NC("NetwMultiplayer clock handshake", colors::CLOCK);
    NETW_TRACE(sys::CLOCK, "Clock handshake asks the server for its tickrate.");
    send_to(
        1,
        0,
        clock_channels.handshake,
        clock_rate_frame(clock_engine().get_tickrate()),
        true,
        0,
        String(),
        true
    );
}

void NetwMultiplayer::clock_send_ping() {
    NETW_ZONE_NC("NetwMultiplayer clock ping", colors::CLOCK);
    session::ClockPing ping;
    ping.origin = clock_wall_usec() & 0xFFFFFFFF;
    send_to(
        1,
        0,
        clock_channels.ping,
        session::frame_write(ping),
        false,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::clock_receive_handshake(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (get_unique_id() != 1) {
        return;
    }
    session::ClockRate asked;
    if (!session::frame_read(p_payload, asked)) {
        NETW_TRACE(
            sys::CLOCK,
            "a clock handshake that did not decode whole is refused"
        );
        return;
    }
    send_to(
        p_sender,
        0,
        clock_channels.handshake_reply,
        clock_rate_frame(clock_engine().get_tickrate()),
        true,
        0,
        String(),
        true
    );
}

void NetwMultiplayer::clock_receive_handshake_reply(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != 1) {
        return;
    }
    session::ClockRate answered;
    if (!session::frame_read(p_payload, answered)) {
        NETW_ERROR(
            sys::CLOCK,
            "a clock handshake reply carried no tickrate, so the local rate "
            "stands uncalibrated"
        );
        return;
    }
    const int server_tickrate = int(answered.tickrate);
    if (server_tickrate != clock_engine().get_tickrate()) {
        if (clock_mismatch_action == MISMATCH_ACTION_DISCONNECT) {
            if (effective_peer.is_valid()) {
                effective_peer->close();
            }
        } else if (clock_mismatch_action == MISMATCH_ACTION_SIGNAL) {
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

void NetwMultiplayer::clock_receive_ping(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    session::ClockPing ping;
    if (get_unique_id() != 1 || !session::frame_read(p_payload, ping)) {
        return;
    }
    session::ClockPong pong;
    pong.origin = ping.origin;
    pong.tick = uint64_t(clock_engine().get_tick()) & 0xFFFFFFFF;
    pong.phase = uint64_t(clock_engine().tick_phase() * 255.0) & 0xFF;
    send_to(
        p_sender,
        0,
        clock_channels.pong,
        session::frame_write(pong),
        false,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::clock_receive_pong(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    session::ClockPong pong;
    if (p_sender != 1 || !session::frame_read(p_payload, pong)) {
        return;
    }
    const uint32_t client_usec = uint32_t(pong.origin);
    const uint32_t server_tick = uint32_t(pong.tick);
    const double server_phase = double(pong.phase) / 255.0;
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

void NetwMultiplayer::set_display_role_resolver(const Callable &p_resolver) {
    display_hooks.resolve_role = p_resolver;
}

void NetwMultiplayer::set_display_chase_clamp(const Callable &p_clamp) {
    display_hooks.chase_clamp = p_clamp;
}

void NetwMultiplayer::set_display_spec_override(
    const LocalVector<display::SpecRow> &p_rows
) {
    display_hooks.spec_override.clear();
    for (uint32_t at = 0; at < p_rows.size(); ++at) {
        display_hooks.spec_override.push_back(p_rows[at]);
    }
    display_hooks.spec_override_armed = true;
    display_hooks.spec_asks = 0;
}

int NetwMultiplayer::display_spec_asks() const {
    return display_hooks.spec_asks;
}

void NetwMultiplayer::set_display_lane(const Callable &p_lane) {
    display_hooks.display_lane = p_lane;
}

void NetwMultiplayer::set_display_sync_intervals(const Callable &p_compute) {
    display_hooks.sync_intervals = p_compute;
}

void NetwMultiplayer::set_display_authors_streams(const Callable &p_authors) {
    display_hooks.authors_streams = p_authors;
}

void NetwMultiplayer::set_display_role_reader(const Callable &p_reader) {
    display_hooks.role_reader = p_reader;
}

void NetwMultiplayer::set_display_chase_hook(const Callable &p_hook) {
    display_hooks.chase_hook_binder = p_hook;
}

RID NetwMultiplayer::get_current_scene() const {
    return scene_core->get_current_scene();
}

int NetwMultiplayer::declared_quantum() const {
    return MAX(1, int(Math::round(clock_engine().physics_factor())));
}

void NetwMultiplayer::persist_set_quit_guard(const Callable &p_guard) {
    persistence.quit_guard = p_guard;
}

void NetwMultiplayer::persist_set_drain(const Callable &p_drain) {
    persistence.drain = p_drain;
}

NetwMultiplayer::SessionState NetwMultiplayer::session_get_state() const {
    return SessionState(session_core.get_state());
}

NetwMultiplayer::Role NetwMultiplayer::session_get_role() const {
    return Role(session_core.get_role());
}

bool NetwMultiplayer::is_online() const {
    return session_get_state() == SESSION_STATE_ONLINE;
}

bool NetwMultiplayer::is_host() const {
    return session_core.holds_server_authority();
}

bool NetwMultiplayer::has_server_role() const {
    return session_core.is_server_role();
}

bool NetwMultiplayer::is_local_client() const {
    const Role current = session_get_role();
    return current == ROLE_CLIENT || current == ROLE_LISTEN_SERVER;
}

int64_t NetwMultiplayer::get_sent_packets() const {
    return sent_packets;
}

int64_t NetwMultiplayer::get_sent_bytes() const {
    return sent_bytes;
}

int64_t NetwMultiplayer::get_received_packets() const {
    return received_packets;
}

int64_t NetwMultiplayer::get_received_bytes() const {
    return received_bytes;
}

int64_t NetwMultiplayer::get_state_acks_out() const {
    return state_acks_out;
}

int64_t NetwMultiplayer::get_state_acks_in() const {
    return state_acks_in;
}

int64_t NetwMultiplayer::get_standalone_acks_out() const {
    return standalone_acks_out;
}

double NetwMultiplayer::poll_delta(int64_t p_now_usec) {
    const int64_t previous = last_poll_usec;
    if (previous == p_now_usec) {
        return 0.0;
    }
    last_poll_usec = p_now_usec;
    const double delta
        = previous <= 0 ? 0.0 : double(p_now_usec - previous) / 1000000.0;
    emit_signal(SIG_SESSION_POLL_STARTED, delta);
    return delta;
}

int64_t NetwMultiplayer::get_frame_counter() const {
    return frame_counter;
}

void NetwMultiplayer::clock_apply_config(const Ref<NetwClockConfig> &p_config) {
    if (p_config.is_null()) {
        return;
    }
    ClockEngine &clock = clock_engine();
    clock.set_tickrate(int(p_config->get_tickrate()));
    clock.set_max_ticks_per_frame(int(p_config->get_max_ticks_per_frame()));
    clock.set_stall_threshold(p_config->get_stall_threshold());
    clock.set_use_physics_interpolation(
        p_config->get_use_physics_interpolation()
    );
    clock.set_sync_mode(ClockEngine::SyncMode(p_config->get_sync_mode()));
    clock.set_panic_snap_threshold(int(p_config->get_panic_snap_threshold()));
    clock.set_stretch_nudge_factor(p_config->get_stretch_nudge_factor());
    clock.set_ping_interval(p_config->get_ping_interval());
    clock.set_display_offset(int(p_config->get_display_offset()));
    clock.set_jitter_multiplier(p_config->get_jitter_multiplier());
    clock.set_jitter_window(int(p_config->get_jitter_window()));
    clock.set_jitter_stability_threshold(
        p_config->get_jitter_stability_threshold()
    );
    clock.set_enable_drift_logging(p_config->get_enable_drift_logging());
}

void NetwMultiplayer::clock_sweep_effects(double, int64_t p_tick) {
    lagcomp_effect_sweep(p_tick);
}

bool NetwMultiplayer::_get(const StringName &p_name, Variant &r_ret) const {
    if (p_name != StringName("_native_core")) {
        return false;
    }
    r_ret = Variant(const_cast<NetwMultiplayer *>(this));
    return true;
}

void NetwMultiplayer::session_set_inner(const Ref<SceneMultiplayer> &p_inner) {
    if (inner == p_inner) {
        return;
    }
    if (inner.is_valid()) {
        disconnect_once(
            Signal(inner.ptr(), SIG_CONNECTED_TO_SERVER),
            callable_mp(this, &NetwMultiplayer::session_transport_connected)
        );
        disconnect_once(
            Signal(inner.ptr(), SIG_CONNECTION_FAILED),
            callable_mp(this, &NetwMultiplayer::session_transport_failed)
        );
        disconnect_once(
            Signal(inner.ptr(), SIG_SERVER_DISCONNECTED),
            callable_mp(this, &NetwMultiplayer::session_transport_dropped)
        );
        disconnect_once(
            Signal(inner.ptr(), SIG_PEER_PACKET),
            callable_mp(this, &NetwMultiplayer::session_on_inner_packet)
        );
        disconnect_once(
            Signal(inner.ptr(), StringName("peer_connected")),
            callable_mp(this, &NetwMultiplayer::session_relay_peer_connected)
        );
        disconnect_once(
            Signal(inner.ptr(), StringName("peer_disconnected")),
            callable_mp(this, &NetwMultiplayer::session_relay_peer_disconnected)
        );
        stop_relay_from(inner.ptr(), SIG_CONNECTED_TO_SERVER, 0);
        stop_relay_from(inner.ptr(), SIG_CONNECTION_FAILED, 0);
        stop_relay_from(inner.ptr(), SIG_SERVER_DISCONNECTED, 0);
        auth_disarm();
        stop_relay_from(inner.ptr(), SIG_PEER_AUTHENTICATING, 1);
        inner->disconnect(
            SIG_PEER_AUTHENTICATING,
            callable_mp(this, &NetwMultiplayer::auth_send_hello)
        );
        inner->disconnect(
            SIG_PEER_AUTHENTICATION_FAILED,
            callable_mp(this, &NetwMultiplayer::relay_auth_failed)
        );
    }
    inner = p_inner;
    if (inner.is_null()) {
        return;
    }
    connect_once(
        Signal(inner.ptr(), SIG_CONNECTED_TO_SERVER),
        callable_mp(this, &NetwMultiplayer::session_transport_connected)
    );
    connect_once(
        Signal(inner.ptr(), SIG_CONNECTION_FAILED),
        callable_mp(this, &NetwMultiplayer::session_transport_failed)
    );
    connect_once(
        Signal(inner.ptr(), SIG_SERVER_DISCONNECTED),
        callable_mp(this, &NetwMultiplayer::session_transport_dropped)
    );
    connect_once(
        Signal(inner.ptr(), SIG_PEER_PACKET),
        callable_mp(this, &NetwMultiplayer::session_on_inner_packet)
    );
    connect_once(
        Signal(inner.ptr(), StringName("peer_connected")),
        callable_mp(this, &NetwMultiplayer::session_relay_peer_connected)
    );
    connect_once(
        Signal(inner.ptr(), StringName("peer_disconnected")),
        callable_mp(this, &NetwMultiplayer::session_relay_peer_disconnected)
    );
    relay_from(inner.ptr(), SIG_CONNECTED_TO_SERVER, 0);
    relay_from(inner.ptr(), SIG_CONNECTION_FAILED, 0);
    relay_from(inner.ptr(), SIG_SERVER_DISCONNECTED, 0);
    relay_from(inner.ptr(), SIG_PEER_AUTHENTICATING, 1);
    inner->connect(
        SIG_PEER_AUTHENTICATING,
        callable_mp(this, &NetwMultiplayer::auth_send_hello)
    );
    inner->connect(
        SIG_PEER_AUTHENTICATION_FAILED,
        callable_mp(this, &NetwMultiplayer::relay_auth_failed)
    );
}

void NetwMultiplayer::session_set_root(const Callable &p_reader) {
    session_root_reader = p_reader;
}

int64_t NetwMultiplayer::linger_pumps(double p_seconds) const {
    const double rate = clock_engine().get_configured()
        ? double(clock_engine().get_tickrate())
        : POLL_PUMP_RATE;
    return ClockEngine::pumps_for(p_seconds, rate);
}

bool NetwMultiplayer::action_gate_arm(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity,
    int64_t p_display_tick
) {
    if (p_entity.is_null()) {
        return false;
    }
    const int64_t requester = p_entity->get_action_requester();
    if (requester != 0 && has_multiplayer_peer()
        && requester == get_unique_id()) {
        return false;
    }
    return action_gates.arm(
        p_route,
        p_entity->get_owner(),
        p_entity->get_action_spawn_tick(),
        p_display_tick
    );
}

PackedInt64Array NetwMultiplayer::action_gate_sweep(int64_t p_display_tick) {
    return action_gates.reveal_reached(p_display_tick);
}

void NetwMultiplayer::action_gate_drop(int64_t p_route) {
    action_gates.reveal(p_route);
}

void NetwMultiplayer::action_gate_clear() {
    action_gates.clear();
}

int64_t NetwMultiplayer::action_gate_count() const {
    return int64_t(action_gates.size());
}

Node *NetwMultiplayer::session_root() const {
    if (session_root_reader.is_valid()) {
        Node *declared = Object::cast_to<Node>(
            gd::live_object(session_root_reader.call())
        );
        if (declared != nullptr) {
            session_root_last_live = gd::instance_id(declared);
        }
        return declared;
    }
    if (inner.is_null()) {
        return nullptr;
    }
    Node *root = gd::scene_root();
    if (root == nullptr) {
        return nullptr;
    }
    return root->get_node_or_null(inner->get_root_path());
}

void NetwMultiplayer::peer_set_identity(
    int64_t p_peer,
    const Ref<NetwIdentity> &p_identity
) {
    NETW_ZONE_NC("peer_set_identity", colors::SESSION);
    if (p_identity.is_null()) {
        NETW_TRACE(sys::SESSION, "peer %d loses its identity row", int(p_peer));
        peer_identities.erase(p_peer);
        return;
    }
    peer_identities[p_peer] = p_identity;
}

Ref<NetwIdentity> NetwMultiplayer::peer_get_identity(int64_t p_peer) const {
    const HashMap<int64_t, Ref<NetwIdentity>>::ConstIterator found
        = peer_identities.find(p_peer);
    if (found == peer_identities.end()) {
        NETW_TRACE(sys::SESSION, "peer %d has no identity row", int(p_peer));
        return Ref<NetwIdentity>();
    }
    return found->value;
}

Ref<NetwIdentity> NetwMultiplayer::participant_identity(int64_t p_peer) const {
    return peer_get_identity(p_peer);
}

Ref<NetwSessionConfig> NetwMultiplayer::session_get_config() const {
    Ref<NetwSessionConfig> snapshot;
    snapshot.instantiate();
    snapshot->set_app_id(session_settings.app_id);
    snapshot->set_desired_role(session_settings.desired_role);
    if (session_settings.link_conditions.is_valid()) {
        Ref<NetwLinkConditions> link;
        link.instantiate();
        link->copy_values_from(**session_settings.link_conditions);
        snapshot->set_link_conditions(link);
    }
    if (session_settings.server_info.is_valid()) {
        Ref<NetwServerInfo> info;
        info.instantiate();
        info->copy_values_from(**session_settings.server_info);
        snapshot->set_server_info(info);
    }
    return snapshot;
}

void NetwMultiplayer::session_set_server_info(
    const Ref<NetwServerInfo> &p_info
) {
    if (p_info.is_null()) {
        session_settings.server_info = Ref<NetwServerInfo>();
        return;
    }
    session_settings.server_info.instantiate();
    session_settings.server_info->copy_values_from(**p_info);
}

StringName NetwMultiplayer::session_get_app_id() const {
    return session_settings.app_id;
}

Error NetwMultiplayer::session_initialize(
    const Ref<NetwSessionConfig> &p_from
) {
    const int slot = int(session_decl::KIND_SESSION_CONFIG)
        - int(session_decl::KIND_SESSION_CONFIG);
    if (config_consumption[slot].consumed) {
        NETW_WARN(
            sys::SESSION,
            "Netw.configure_session: 'session configuration' on '%s' was "
            "configured after this session consumed its configuration. The "
            "running values are unchanged. Configure before startup and "
            "finish the fluent chain without awaiting.",
            String("this session")
        );
        return ERR_ALREADY_IN_USE;
    }
    config_consumption[slot].consumed = true;
    if (p_from.is_valid()) {
        session_settings.app_id = p_from->get_app_id();
        session_settings.desired_role = p_from->get_desired_role();
        const Ref<NetwLinkConditions> link = p_from->get_link_conditions();
        if (link.is_valid()) {
            session_settings.link_conditions.instantiate();
            session_settings.link_conditions->copy_values_from(**link);
        }
        const Ref<NetwServerInfo> info = p_from->get_server_info();
        if (info.is_valid()) {
            session_settings.server_info.instantiate();
            session_settings.server_info->copy_values_from(**info);
        }
    }
    connect_core.bind_session(this);
    session_push_desired_role();
    session_apply_auth_config();
    return OK;
}

void NetwMultiplayer::session_offer_fallback(
    const Ref<NetwSessionConfig> &p_draft,
    Object *p_source
) {
    const ObjectID source = gd::instance_id(p_source);
    NETW_ERR_COND(
        session_fallback.is_valid() && session_fallback_source != source
            && gd::object_of(session_fallback_source) != nullptr,
        sys::SESSION,
        "two mounts export session settings for one session, so this session "
        "has no single configuration. Keep one declaration on the session's "
        "branch."
    );
    session_fallback = p_draft;
    session_fallback_source = source;
    declarations_changed();
}

void NetwMultiplayer::session_constrain_role(Role p_role) {
    session_role_constraint = int64_t(p_role);
    if (config_is_consumed(session_decl::KIND_SESSION_CONFIG)) {
        session_apply_role_constraint();
    }
}

void NetwMultiplayer::session_apply_role_constraint() {
    if (session_role_constraint < 0) {
        return;
    }
    session_settings.desired_role = session_role_constraint;
    session_push_desired_role();
}

NetwMultiplayer::JoinPlan NetwMultiplayer::session_resolve_join() {
    JoinPlan plan;
    plan.available = true;
    const Callable override = session_join_override();
    if (override.is_valid()) {
        plan.handler = override;
        plan.quantizers = session_join_override_quantizers();
        plan.declared = true;
        return plan;
    }
    const session_decl::Resolved declared
        = declaration_slots.resolve(this, session_decl::KIND_JOIN);
    if (declared.state == session_decl::READY) {
        const Ref<NetwJoinConfig> config = declared.payload;
        if (config.is_valid()) {
            plan.quantizers = config->get_quantizers();
        }
        plan.handler = declared.callable;
        plan.declared = true;
        return plan;
    }
    if (declared.state != session_decl::ABSENT) {
        declaration_slots.report_unresolved(
            this,
            session_decl::KIND_JOIN,
            declared.state,
            "a join"
        );
        plan.available = false;
        return plan;
    }
    if (default_join.is_null()) {
        default_join.instantiate();
        default_join->bind_session(this);
    }
    plan.handler = Callable(default_join.ptr(), StringName("spawn"));
    return plan;
}

Array NetwMultiplayer::join_declared_arg_types(const Callable &p_handler) {
    Object *target = p_handler.get_object();
    const Ref<Script> script = netw::script::model::declaring_script(target);
    return script.is_valid()
        ? netw::script::model::get_method_arg_types(
              script,
              p_handler.get_method()
          )
        : gd::bound_method_arg_types(target, p_handler.get_method());
}

Array NetwMultiplayer::join_arg_types(const Callable &p_handler) {
    const Array types = join_declared_arg_types(p_handler);
    return types.is_empty() ? Array() : types.slice(1);
}

int64_t NetwMultiplayer::join_schema_hash(
    const Callable &p_handler,
    const Array &p_quantizers
) {
    String signature;
    const Array types = join_arg_types(p_handler);
    for (int at = 0; at < types.size(); ++at) {
        signature += String::num_int64(int64_t(types[at])) + ",";
    }
    signature += "|";
    for (int at = 0; at < p_quantizers.size(); ++at) {
        const Ref<NetwQuantize> quantizer = p_quantizers[at];
        signature
            += (quantizer.is_valid() ? quantizer->get_class() : String("_"))
            + ",";
    }
    return int64_t(int32_t(signature.hash()));
}

void NetwMultiplayer::session_encode_join_args(JoinRequest &r_request) {
    const JoinPlan plan = session_resolve_join();
    const Callable handler = plan.handler;
    const Array quantizers = plan.quantizers;
    if (r_request.arg_values.is_empty() || !plan.available
        || !handler.is_valid()) {
        r_request.arg_bytes = PackedByteArray();
        r_request.schema_hash = 0;
        return;
    }
    wire::WriteStream stream;
    if (!call_args::values_write(
            stream,
            r_request.arg_values,
            quantizers,
            join_arg_types(handler)
        )
        || !stream.align_verify()) {
        r_request.arg_bytes = PackedByteArray();
        r_request.schema_hash = 0;
        return;
    }
    r_request.arg_bytes = stream.to_bytes();
    r_request.schema_hash = join_schema_hash(handler, quantizers);
}

bool NetwMultiplayer::session_decode_join_args(JoinRequest &r_request) {
    if (r_request.arg_bytes.is_empty()) {
        r_request.arg_values = Array();
        return true;
    }
    const JoinPlan plan = session_resolve_join();
    const Callable handler = plan.handler;
    const Array quantizers = plan.quantizers;
    if (!plan.available || !handler.is_valid()) {
        NETW_TRACE(
            sys::SESSION,
            "a join carried encoded args with no handler to read them"
        );
        return false;
    }
    if (r_request.schema_hash != join_schema_hash(handler, quantizers)) {
        NETW_TRACE(
            sys::SESSION,
            "a join's arg schema disagrees with this server's handler"
        );
        return false;
    }
    wire::ReadStream stream(r_request.arg_bytes);
    Array values;
    if (!call_args::values_read(
            stream,
            quantizers,
            join_arg_types(handler),
            values
        )
        || !stream.align_verify() || stream.bits_remaining() != 0) {
        NETW_TRACE(sys::SESSION, "a join's encoded args did not decode whole");
        return false;
    }
    r_request.arg_values = values;
    return true;
}

Ref<ResolvedJoin> NetwMultiplayer::session_resolve_inbound_join(
    JoinRequest &r_request,
    int64_t p_sender
) {
    if (!session_decode_join_args(r_request)) {
        NETW_WARN(
            sys::SESSION,
            "join: rejected malformed or mismatched args from peer %d",
            int(p_sender)
        );
        return Ref<ResolvedJoin>();
    }
    auth_resolve_identity(p_sender, r_request);
    const Ref<ResolvedJoin> resolved = r_request.resolve();
    if (resolved.is_null()) {
        NETW_WARN(
            sys::SESSION,
            "join: invalid payload from peer %d",
            int(p_sender)
        );
        return Ref<ResolvedJoin>();
    }
    if (!session_admit_username(
            resolved,
            scene_players_all(),
            Callable(inner.ptr(), StringName("disconnect_peer"))
        )) {
        return Ref<ResolvedJoin>();
    }
    return resolved;
}

void NetwMultiplayer::session_set_prepared_join(
    const StringName &p_username,
    const Array &p_args
) {
    prepared_join = connect::request_of(p_username, p_args);
}

const std::optional<JoinRequest> &NetwMultiplayer::
    session_prepared_join() const {
    return prepared_join;
}

void NetwMultiplayer::session_submit_join(
    const StringName &p_username,
    const Array &p_args
) {
    const std::optional<JoinRequest> request
        = connect::request_of(p_username, p_args);
    if (!request.has_value()) {
        NETW_TRACE(sys::SESSION, "a join with no username submits nothing");
        return;
    }
    session_submit_request(*request);
}

void NetwMultiplayer::session_submit_request(const JoinRequest &p_request) {
    NETW_ZONE_NC("netw::session_submit_join", colors::SESSION);
    JoinRequest sending = p_request;
    session_encode_join_args(sending);
    sending.app_tag = auth_app_tag;
    sending.wire_identity = SessionCore::compute_wire_identity();
    sending.schema_identity = session_schema_identity();
    join_awaiting_admission = true;
    emit_signal(
        StringName("session_join_submitted"),
        sending.username,
        sending.arg_values
    );
    if (is_server()) {
        session_receive_join(sending.serialize(), 1);
        return;
    }
    send_to(
        1,
        0,
        session_join_channel,
        sending.serialize(),
        true,
        0,
        String(),
        false
    );
}

Ref<NetwPromise> NetwMultiplayer::session_leave() {
    NETW_ZONE_NC("netw::session_leave", colors::SESSION);
    if (session_get_state() == SESSION_STATE_OFFLINE) {
        return NetwPromise::resolved(OK);
    }
    NETW_TRACE(sys::SESSION, "Session: leave called.");
    NETW_INFO(sys::SESSION, "Disconnecting player.");
    persistence_flush_all();
    session_transition(SESSION_STATE_DISCONNECTING);
    if (effective_peer.is_valid()) {
        effective_peer->close();
    }

    Ref<NetwPromise> left;
    left.instantiate();
    const Callable settle
        = callable_mp(this, &NetwMultiplayer::session_settle_leave).bind(left);
    SceneTree *tree = gd::scene_tree();
    if (tree == nullptr) {
        session_settle_leave(left);
        return left;
    }
    const Ref<SceneTreeTimer> window = tree->create_timer(3.0);
    if (window.is_null()) {
        session_settle_leave(left);
        return left;
    }
    window->connect(StringName("timeout"), settle, Object::CONNECT_ONE_SHOT);
    connect(
        StringName("server_disconnected"),
        settle,
        Object::CONNECT_ONE_SHOT
    );
    return left;
}

void NetwMultiplayer::session_settle_leave(const Ref<NetwPromise> &p_left) {
    if (p_left->get_is_settled()) {
        return;
    }
    const Callable settle
        = callable_mp(this, &NetwMultiplayer::session_settle_leave)
              .bind(p_left);
    if (is_connected(StringName("server_disconnected"), settle)) {
        disconnect(StringName("server_disconnected"), settle);
    }
    session_transition(SESSION_STATE_OFFLINE);
    p_left->resolve(OK);
}

void NetwMultiplayer::session_clear_prepared_join() {
    prepared_join.reset();
    preparing_join = Ref<NetwPromise>();
    held_hello_peer = 0;
    auth_request.reset();
    resubmit_join.reset();
    join_awaiting_admission = false;
    const Callable on_server = callable_mp(
        this,
        &NetwMultiplayer::session_resubmit_join_on_server_peer
    );
    if (is_connected(StringName("peer_connected"), on_server)) {
        disconnect(StringName("peer_connected"), on_server);
    }
}

void NetwMultiplayer::session_submit_prepared_join() {
    if (!prepared_join.has_value()) {
        return;
    }
    const JoinRequest request = *prepared_join;
    prepared_join.reset();
    auth_request.reset();
    session_submit_request(request);
    if (is_server() || NETW_API_VIRTUAL(get_peer_ids)().has(1)) {
        return;
    }
    NETW_TRACE(
        sys::SESSION,
        "the join is held for one resend, the server peer is not yet seen"
    );
    resubmit_join = request;
    const Callable on_server = callable_mp(
        this,
        &NetwMultiplayer::session_resubmit_join_on_server_peer
    );
    if (!is_connected(StringName("peer_connected"), on_server)) {
        connect(StringName("peer_connected"), on_server);
    }
}

void NetwMultiplayer::session_resubmit_join_on_server_peer(int64_t p_peer) {
    if (p_peer != 1) {
        return;
    }
    const Callable on_server = callable_mp(
        this,
        &NetwMultiplayer::session_resubmit_join_on_server_peer
    );
    if (is_connected(StringName("peer_connected"), on_server)) {
        disconnect(StringName("peer_connected"), on_server);
    }
    if (!resubmit_join.has_value()) {
        return;
    }
    const JoinRequest request = *resubmit_join;
    resubmit_join.reset();
    session_submit_request(request);
}

Ref<NetwPromise> NetwMultiplayer::session_prepare_join(
    const StringName &p_username,
    const Array &p_args
) {
    NETW_ZONE_NC("netw::session_prepare_join", colors::SESSION);
    session_clear_prepared_join();
    if (String(p_username).is_empty()) {
        NETW_ERROR(sys::SESSION, "username is empty.");
        return NetwPromise::resolved(ERR_INVALID_PARAMETER);
    }

    auth_arm();
    Ref<NetwPromise> prepared;
    prepared.instantiate();
    preparing_join = prepared;
    const Ref<NetwPromise> flow_ready = auth_prepare(p_username);
    flow_ready->then(
        callable_mp(this, &NetwMultiplayer::session_settle_prepared_join)
            .bind(p_username, p_args, prepared)
    );
    flow_ready->catch_error(
        callable_mp(this, &NetwMultiplayer::session_settle_refused_join)
            .bind(p_username, p_args, prepared)
    );
    return prepared;
}

void NetwMultiplayer::session_settle_prepared_join(
    const Variant &p_prepare_result,
    const StringName &p_username,
    const Array &p_args,
    const Ref<NetwPromise> &p_prepared
) {
    const int64_t prepare_err = p_prepare_result;
    if (prepare_err != OK) {
        NETW_ERROR(
            sys::SESSION,
            "Auth prepare failed: %s",
            gd::error_name(prepare_err)
        );
        p_prepared->resolve(prepare_err);
        auth_release_hello();
        return;
    }
    const std::optional<JoinRequest> request
        = connect::request_of(p_username, p_args);
    auth_set_join_request(request);
    prepared_join = request;
    p_prepared->resolve(OK);
    auth_release_hello();
}

void NetwMultiplayer::session_settle_refused_join(
    int64_t p_error,
    const String &p_detail,
    const StringName &,
    const Array &,
    const Ref<NetwPromise> &p_prepared
) {
    const Error refused = p_error == OK ? FAILED : Error(p_error);
    NETW_WARN(
        sys::SESSION,
        "the join preparation was refused (%s): %s",
        gd::error_name(refused),
        p_detail
    );
    prepared_join.reset();
    auth_set_join_request(std::optional<JoinRequest>());
    held_hello_peer = 0;
    p_prepared->resolve(int64_t(refused));
    session_report_join_failure(refused, p_detail);
}

void NetwMultiplayer::session_push_desired_role() {
    session_set_desired_role(session_get_authored_role());
}

void NetwMultiplayer::session_apply_auth_config() {
    auth_set_app_tag(session_app_tag(session_settings.app_id));
    auth_arm();
}

NetwMultiplayer::Role NetwMultiplayer::session_get_authored_role() const {
    const int64_t named = session_settings.desired_role;
    if (named < ROLE_NONE || named > ROLE_LISTEN_SERVER) {
        return ROLE_LISTEN_SERVER;
    }
    return Role(named);
}

bool NetwMultiplayer::presents_as_listen_host() const {
    return session_get_role() == ROLE_LISTEN_SERVER
        || session_get_authored_role() == ROLE_LISTEN_SERVER;
}

void NetwMultiplayer::set_relay_sender(int64_t p_sender) {
    dispatching_sender = p_sender;
}

int64_t NetwMultiplayer::rpc_get_relay_sender() const {
    return dispatching_sender;
}

int64_t NetwMultiplayer::peer_ack(int64_t p_peer) const {
    return seq_book.peer_ack(p_peer);
}

void NetwMultiplayer::clear_seq_books() {
    seq_book.clear();
}

bool NetwMultiplayer::connect_once(
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

bool NetwMultiplayer::disconnect_once(
    Signal p_signal,
    const Callable &p_callback
) {
    if (!p_signal.is_connected(p_callback)) {
        return false;
    }
    p_signal.disconnect(p_callback);
    return true;
}

void NetwMultiplayer::clear_flat_family_state() {
    layer_forget_all();
    scene_clear_pending_facets();
    if (table_core.is_valid()) {
        table_core->clear_session();
    }
    display_clear_declarations();
    sync_encode_armed = false;
    sync_encode_stock = PackedByteArray();
    sync_decoder = Callable();
}

void NetwMultiplayer::clear_session_state() {
    clear_seq_books();
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->clear_session();
    }
    rpc_clear_session();
    clear_verdicts();
    clear_flat_family_state();
}

SchemaCore *NetwMultiplayer::get_schema_core() {
    return &schema_core;
}

Ref<table::Core> NetwMultiplayer::get_table_core() const {
    return table_core;
}

void NetwMultiplayer::clear_roster() {
    session_clear_roster();
    participant_clear();
}

Ref<NetwTimeline> NetwMultiplayer::register_timeline(
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return Ref<NetwTimeline>();
    }
    const int64_t slot
        = lagcomp_core.timeline_register(p_entity, NetwTimeline::DEFAULT_LIMIT);
    return slot >= 0 ? lagcomp_core.timeline_history(slot)
                     : Ref<NetwTimeline>();
}

bool NetwMultiplayer::is_extension_script(const Ref<Script> &p_script) {
    if (p_script.is_null()) {
        return false;
    }
    if (p_script->get_instance_base_type() == StringName("NetwMultiplayer")) {
        return true;
    }
    Ref<Script> walked = p_script;
    while (walked.is_valid()) {
        if (walked->get_global_name() == StringName("NetwMultiplayer")) {
            return true;
        }
        walked = walked->get_base_script();
    }
    return false;
}

Ref<NetwMultiplayer> NetwMultiplayer::make(
    const Ref<SceneMultiplayer> &p_inner,
    const Ref<Script> &p_implementation
) {
    Ref<Script> selected = p_implementation;
#if defined(NETW_TESTS)
    if (selected.is_null()) {
        selected = law_extension_script();
    }
#endif
    Ref<NetwMultiplayer> made;
    if (selected.is_valid()) {
        if (!is_extension_script(selected)) {
            NETW_ERROR(
                sys::SESSION,
                "a session implementation must extend NetwMultiplayer"
            );
            return Ref<NetwMultiplayer>();
        }
        made = Ref<NetwMultiplayer>(
            Object::cast_to<NetwMultiplayer>(selected->call("new"))
        );
    } else {
        Ref<NetwMultiplayer> stock;
        stock.instantiate();
        made = stock;
    }
    if (made.is_valid()) {
        made->embed_autosettle_pending = false;
    }
    if (made.is_valid() && p_inner.is_valid()) {
        made->session_set_inner(p_inner);
        made->NETW_API_VIRTUAL(set_multiplayer_peer)(
            p_inner->get_multiplayer_peer()
        );
    }
    return made;
}

void NetwMultiplayer::install_default_interface() {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    const bool wanted = settings == nullptr
        || bool(settings->get_setting(INSTALL_AS_DEFAULT_SETTING, true));
    if (!wanted) {
        return;
    }
    displaced_default_interface() = MultiplayerAPI::get_default_interface();
    MultiplayerAPI::set_default_interface(get_class_static());
}

void NetwMultiplayer::restore_default_interface() {
    const StringName restoring = displaced_default_interface();
    displaced_default_interface() = StringName();
    if (restoring == StringName()
        || MultiplayerAPI::get_default_interface() != get_class_static()) {
        return;
    }
    MultiplayerAPI::set_default_interface(restoring);
}

void NetwMultiplayer::embed_autosettle_tree_default() {
    if (!embed_autosettle_pending) {
        return;
    }
    embed_autosettle_pending = false;
    if (embed_phase_value != EMBED_PHASE_DECLARING) {
        return;
    }
    SceneTree *tree = gd::scene_tree();
    if (tree != nullptr && tree->get_multiplayer().ptr() == this) {
        embed_settle();
    }
}

Ref<ResolvedJoin> NetwMultiplayer::peer_get_accepted_join(
    int64_t p_peer
) const {
    return session_accepted_join(p_peer);
}

void NetwMultiplayer::peer_forget(int64_t p_peer) {
    session_forget_peer(p_peer);
    participant_forget(p_peer);
}

Ref<NetwParticipant> NetwMultiplayer::peer_get_participant(int64_t p_peer) {
    if (session_accepted_join(p_peer).is_null()) {
        return Ref<NetwParticipant>();
    }
    participant_ensure(p_peer);
    return participant_of(p_peer);
}

Ref<Script> NetwMultiplayer::law_extension_script() {
    static Ref<Script> resolved;
    static bool asked = false;
    if (asked) {
        return resolved;
    }
    asked = true;
    const String path = OS::get_singleton()->get_environment(LAW_EXTENSION_ENV);
    if (path.is_empty()) {
        return resolved;
    }
#if defined(NETW_MODULE)
    const Ref<Script> named = ResourceLoader::load(path, "Script");
#else
    const Ref<Script> named = ResourceLoader::get_singleton()->load(path);
#endif
    if (named.is_null()) {
        NETW_ERR_V(
            resolved,
            sys::SESSION,
            "{} names '{}', which is not a script",
            LAW_EXTENSION_ENV,
            path
        );
    }
    if (!is_extension_script(named)) {
        NETW_ERR_V(
            resolved,
            sys::SESSION,
            "{} names '{}', which does not extend the session class",
            LAW_EXTENSION_ENV,
            path
        );
    }
    resolved = named;
    return resolved;
}

bool NetwMultiplayer::law_extension_named() {
    return !OS::get_singleton()->get_environment(LAW_EXTENSION_ENV).is_empty();
}

void NetwMultiplayer::handle_deny(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    session::DenyKey denied;
    if (p_sender != 1 || !session::frame_read(p_payload, denied)) {
        return;
    }
    lagcomp_effect_discard(denied.key);
}

bool NetwMultiplayer::is_coroutine(const Variant &p_value) {
    if (p_value.get_type() != Variant::OBJECT) {
        return false;
    }
    Object *object = p_value;
    return object != nullptr
        && object->get_class() == String("GDScriptFunctionState");
}

bool NetwMultiplayer::overrides_seam(const StringName &p_seam) {
    return script_overrides_seam(
        get_script(),
        StringName(get_class_static()),
        p_seam
    );
}

bool NetwMultiplayer::script_overrides_seam(
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
    const String base_key(p_base_name);
    const bool by_path = base_key.begins_with("res://");
    Ref<Script> base = p_script;
    while (base.is_valid()
           && (by_path ? base->get_path() : String(base->get_global_name()))
               != base_key) {
        base = base->get_base_script();
    }
    const int inherited
        = base.is_valid() ? netw::gd::script_method_count(base, p_seam) : 0;
    const bool overridden
        = netw::gd::script_method_count(p_script, p_seam) > inherited;
    seam_overrides[p_seam] = overridden;
    return overridden;
}

void NetwMultiplayer::forget_seam_overrides() {
    seam_overrides.clear();
    seam_script = ObjectID();
}

void NetwMultiplayer::seam_entered(
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

Variant NetwMultiplayer::seam_settled(
    const StringName &p_seam,
    int64_t p_event,
    int64_t p_route,
    const Variant &p_result,
    const Variant &p_fallback
) {
    const bool suspended = is_coroutine(p_result);
    const bool mistyped = !suspended && p_fallback.get_type() != Variant::NIL
        && p_result.get_type() != p_fallback.get_type();
    if (suspended || mistyped) {
        seam_refused(
            p_seam,
            p_route,
            suspended ? String("coroutine") : String("type")
        );
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

void NetwMultiplayer::seam_refused(
    const StringName &p_seam,
    int64_t p_route,
    const String &p_reason
) {
    if (!misused_seams.has(p_seam)) {
        misused_seams.insert(p_seam);
        NETW_ERROR(
            sys::EVENT,
            "seam %s answered with %s, so the default answer stands",
            String(p_seam),
            p_reason
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
    detail["reason"] = p_reason;
    misuse.detail = detail;
    plane.emit(misuse);
}

} // namespace netw
