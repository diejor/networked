#include "netw/api/netw_multiplayer.hpp"
#include "netw/link/shaped_peer.hpp"

#include "godot/transport_state.hpp"
#include "netw/api/interest.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/nodes/multiplayer_viewport.hpp"
#include "netw/api/scene.hpp"
#include "netw/diag/hints.hpp"
#include "netw/spawn/pipeline.hpp"

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
#include "netw/api/prediction.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/sync_compat.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/auth_protocol.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/connect/browse_list.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/directory_transport.hpp"
#include "netw/held_bytes.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/scene_loads.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/stage.hpp"
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

const StringName &installed_default_interface() {
    static StringName *installed
        = memnew(StringName(NetwMultiplayer::get_class_static()));
    return *installed;
}

godot::LocalVector<godot::ObjectID> live_registry;

struct TreePauseHold {
    int sessions = 0;
    bool paused_by_game = false;
};

TreePauseHold &tree_pause_hold() {
    static TreePauseHold hold;
    return hold;
}

constexpr double POLL_PUMP_RATE = 60.0;

const char *SIG_CONNECTED_TO_SERVER = "connected_to_server";
const char *SIG_CONNECTION_FAILED = "connection_failed";
const char *SIG_PEER_AUTHENTICATING = "peer_authenticating";
const char *SIG_PEER_AUTHENTICATION_FAILED = "peer_authentication_failed";
const char *SIG_PEER_PACKET = "peer_packet";
const char *SIG_SERVER_DISCONNECTED = "server_disconnected";
constexpr int POLLS_BEFORE_TURNED_AWAY_PEER_DROPS = 2;
constexpr int POLLS_BEFORE_SHUTDOWN_CLOSES = 30;

bool names_the_root_window(const Variant &p_configuration) {
    Node *root = gd::scene_root();
    return root != nullptr && p_configuration.get_type() == Variant::NODE_PATH
        && NodePath(p_configuration) == root->get_path();
}

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
    clock.sink.bind(this);
    clock.hook_with(
        [](void *p_context,
           ClockEngine::Event p_event,
           const Variant &p_a,
           const Variant &p_b) {
            static_cast<NetwMultiplayer *>(p_context)
                ->clock_event(p_event, p_a, p_b);
        },
        this
    );
    scene_core.instantiate();
    display_book.instantiate();
    prediction_engine.bind_session(this);
    lagcomp_core.seat_world(&store_world);
    predict_runner.seat_core(this);
    sim_rows.seed_with(
        [](void *p_context, sim::Row &r_row) {
            static_cast<NetwMultiplayer *>(p_context)->sim_seed(r_row);
        },
        this
    );
    store_world.stamp_with(&NetwMultiplayer::store_stamp_thunk, this);

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
    scene_viewer_edge
        = callable_mp(this, &NetwMultiplayer::scene_report_viewer);
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
    clock_channels.handshake = declared_channel("CLOCK_HANDSHAKE");
    clock_channels.handshake_reply = declared_channel("CLOCK_HANDSHAKE_REPLY");
    clock_channels.ping = declared_channel("CLOCK_PING");
    clock_channels.pong = declared_channel("CLOCK_PONG");
    scene_released_channel = declared_channel("SESSION_SCENE_RELEASED");
    scene_viewers_channel = declared_channel("SESSION_SCENE_VIEWERS");
    session_join_channel = declared_channel("SESSION_JOIN");
    session_accept_channel = declared_channel("SESSION_ACCEPT");
    session_roster_channel = declared_channel("SESSION_ROSTER");
    session_resync_channel = declared_channel("SESSION_RESYNC");
    route_lease_channel = declared_channel("ROUTE_LEASE");
    lifecycle_decision_channel = declared_channel("LIFECYCLE_DECISION");

    interest_flush = callable_mp(this, &NetwMultiplayer::interest_flush_sink);

    channel_book.register_protocol(
        scene_released_channel,
        callable_mp(this, &NetwMultiplayer::scene_receive_released_frame)
    );
    channel_book.register_protocol(
        scene_viewers_channel,
        callable_mp(this, &NetwMultiplayer::scene_receive_viewers_frame)
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
    channel_book.register_protocol(
        session_resync_channel,
        callable_mp(this, &NetwMultiplayer::session_receive_resync)
    );
    channel_book.register_protocol(
        route_lease_channel,
        callable_mp(this, &NetwMultiplayer::liveness_receive_lease)
    );
    channel_book.register_protocol(
        lifecycle_decision_channel,
        callable_mp(this, &NetwMultiplayer::lifecycle_receive_decision)
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
    plane->get_sync_model()->attach_book(&binding_book);

    scene_install();
    display_bind_session();

    const Signal joined(this, StringName("peer_connected"));
    connect_once(
        joined,
        callable_mp(this, &NetwMultiplayer::interest_peer_connected)
    );
    connect_once(
        joined,
        callable_mp(this, &NetwMultiplayer::replication_replay_tables)
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
    persist_dispose();
    set_auth_callback(Callable());
    scene_dispose();
    session_dispose();
    display_clear_runtimes();
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->dispose();
    }
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->clear_bindings();
    }
    rpc_dispose();
    property_set_clear();
    binding_book.clear();
    clear_flat_family_state();
    service_clear();
    session_clear_roster();
    player_clear();
    for (LocalVector<Callable> &held : observers) {
        held.clear();
    }
}

Error NetwMultiplayer::observe(
    Notification p_event,
    const Callable &p_callback
) {
    NETW_ERR_COND_V(
        p_event < 0 || p_event >= NOTIFICATION_COUNT,
        ERR_INVALID_PARAMETER,
        sys::SESSION,
        "NetwMultiplayer.observe got %d, which is not a Notification.",
        int(p_event)
    );
    NETW_ERR_COND_V(
        !p_callback.is_valid(),
        ERR_INVALID_PARAMETER,
        sys::SESSION,
        "NetwMultiplayer.observe needs a valid Callable."
    );
    if (!is_observing(p_event, p_callback)) {
        observers[p_event].push_back(p_callback);
    }
    return OK;
}

void NetwMultiplayer::unobserve(
    Notification p_event,
    const Callable &p_callback
) {
    if (p_event < 0 || p_event >= NOTIFICATION_COUNT) {
        return;
    }
    LocalVector<Callable> &held = observers[p_event];
    for (uint32_t at = 0; at < held.size(); ++at) {
        if (held[at] == p_callback) {
            held.remove_at(at);
            return;
        }
    }
}

void NetwMultiplayer::clock_event(
    ClockEngine::Event p_event,
    const Variant &p_a,
    const Variant &p_b
) {
    const double delta = p_a;
    const int64_t tick = p_b;
    switch (p_event) {
        case ClockEngine::EVENT_BEFORE_TICK:
            if (clock_handle.is_valid()) {
                clock_handle->announce_before_tick(delta, tick);
            }
            notify(CLOCK_BEFORE_TICK, p_a, p_b);
            return;
        case ClockEngine::EVENT_ON_TICK:
            tick_step(delta, tick);
            if (clock_sweeps_effects) {
                clock_sweep_effects(delta, tick);
            }
            if (spawn::Pipeline *pipeline = spawn_plane()) {
                if (pipeline->reveals_action_gates()) {
                    spawn_on_action_reveal_tick(delta, tick);
                }
                if (pipeline->awaits_unadded()) {
                    pipeline->report_unadded(clock.get_ticks_run());
                }
            }
            notify(CLOCK_ON_TICK, p_a, p_b);
            return;
        case ClockEngine::EVENT_AFTER_TICK:
            if (display_session_bound) {
                display_on_clock_tick(delta, tick);
            }
            session_on_clock_tick(delta, tick);
            if (clock_handle.is_valid()) {
                clock_handle->announce_ticked(delta, tick);
            }
            notify(CLOCK_AFTER_TICK, p_a, p_b);
            return;
        case ClockEngine::EVENT_BEFORE_TICK_LOOP:
            before_frame_step();
            notify(CLOCK_BEFORE_TICK_LOOP);
            return;
        case ClockEngine::EVENT_AFTER_TICK_LOOP:
            frame_step();
            notify(CLOCK_AFTER_TICK_LOOP);
            return;
        case ClockEngine::EVENT_SYNCHRONIZED:
            if (clock_handle.is_valid()) {
                clock_handle->announce_synchronized();
            }
            notify(CLOCK_SYNCHRONIZED);
            return;
        case ClockEngine::EVENT_STABILITY_CHANGED:
            notify(CLOCK_STABILITY_CHANGED, p_a);
            return;
    }
}

bool NetwMultiplayer::is_observing(
    Notification p_event,
    const Callable &p_callback
) const {
    for (const Callable &held : observers[p_event]) {
        if (held == p_callback) {
            return true;
        }
    }
    return false;
}

uint8_t NetwMultiplayer::declared_channel(const char *p_name) const {
    const wire::ChannelDecl *decl
        = channels.find_channel_by_name(StringName(p_name));
    return decl ? decl->id : 0;
}

NetwMultiplayer::PlayerRow *NetwMultiplayer::player_row(int64_t p_peer) {
    const int64_t *player_id = player_id_of_peer.getptr(p_peer);
    return player_id != nullptr ? players.getptr(*player_id) : nullptr;
}

const NetwMultiplayer::PlayerRow *NetwMultiplayer::player_row(
    int64_t p_peer
) const {
    const int64_t *player_id = player_id_of_peer.getptr(p_peer);
    return player_id != nullptr ? players.getptr(*player_id) : nullptr;
}

int64_t NetwMultiplayer::player_mint_id() {
    const int64_t minted = player_incarnation_next;
    player_incarnation_next += 1;
    return minted;
}

int64_t NetwMultiplayer::player_peer_of_id(int64_t p_player_id) const {
    const PlayerRow *found = players.getptr(p_player_id);
    return found != nullptr ? found->peer : 0;
}

void NetwMultiplayer::participant_index_peer(
    int64_t p_peer,
    int64_t p_player_id
) {
    player_id_of_peer[p_peer] = p_player_id;
}

void NetwMultiplayer::participant_unindex(int64_t p_peer, int64_t p_player_id) {
    const int64_t *current = player_id_of_peer.getptr(p_peer);
    if (current != nullptr && *current == p_player_id) {
        player_id_of_peer.erase(p_peer);
    }
}

Ref<NetwPlayer> NetwMultiplayer::player_ensure(
    const session::AcceptFrame &p_accepted
) {
    if (p_accepted.player_id == 0) {
        return Ref<NetwPlayer>();
    }
    const int64_t player_id = int64_t(p_accepted.player_id);
    PlayerRow *found = players.getptr(player_id);
    if (found != nullptr) {
        if (found->peer != p_accepted.peer_id) {
            participant_unindex(found->peer, player_id);
            found->peer = p_accepted.peer_id;
            if (found->row.is_valid()) {
                found->row->rebind_peer(p_accepted.peer_id);
            }
        }
        participant_index_peer(p_accepted.peer_id, player_id);
        return found->row;
    }
    if (PlayerRow *stale = player_row(p_accepted.peer_id)) {
        stale->admitted = false;
        stale->incarnation = 0;
    }
    Ref<NetwPlayer> minted;
    minted.instantiate();
    minted->bind_to(this, p_accepted.peer_id, player_id, p_accepted.username);
    minted->bind_account(p_accepted.account, p_accepted.account_name);
    NETW_TRACE(
        sys::SESSION,
        "Gave player id %d to peer %d.",
        int(player_id),
        int(p_accepted.peer_id)
    );
    PlayerRow seating;
    seating.row = minted;
    seating.peer = p_accepted.peer_id;
    seating.incarnation = player_id;
    players[player_id] = seating;
    participant_index_peer(p_accepted.peer_id, player_id);
    return minted;
}

void NetwMultiplayer::player_adopt(
    int64_t p_peer,
    const Ref<NetwPlayer> &p_player
) {
    if (p_player.is_null() || player_row(p_peer) != nullptr) {
        return;
    }
    const int64_t player_id = player_mint_id();
    p_player->bind_to(this, p_peer, player_id);
    PlayerRow seating;
    seating.row = p_player;
    seating.peer = p_peer;
    seating.incarnation = player_id;
    players[player_id] = seating;
    participant_index_peer(p_peer, player_id);
}

Ref<NetwPlayer> NetwMultiplayer::player_of(int64_t p_peer) const {
    const PlayerRow *found = player_row(p_peer);
    return found ? found->row : Ref<NetwPlayer>();
}

bool NetwMultiplayer::player_has(int64_t p_peer) const {
    return player_row(p_peer) != nullptr;
}

TypedArray<NetwPlayer> NetwMultiplayer::player_all() const {
    LocalVector<int64_t> peers;
    for (const KeyValue<int64_t, PlayerRow> &row : players) {
        peers.push_back(row.value.peer);
    }
    peers.sort();
    TypedArray<NetwPlayer> out;
    for (uint32_t at = 0; at < peers.size(); at++) {
        out.push_back(player_of(peers[at]));
    }
    return out;
}

Ref<NetwPlayer> NetwMultiplayer::player_joined_of(int64_t p_peer) {
    if (!session_has_accepted(p_peer)) {
        return Ref<NetwPlayer>();
    }
    return player_of(p_peer);
}

TypedArray<NetwPlayer> NetwMultiplayer::player_joined_all() {
    const LocalVector<session::AcceptFrame> joins = session_accepted_joins();
    TypedArray<NetwPlayer> out;
    for (uint32_t at = 0; at < joins.size(); ++at) {
        const Ref<NetwPlayer> seated = player_joined_of(joins[at].peer_id);
        if (seated.is_valid()) {
            out.push_back(seated);
        }
    }
    return out;
}

Ref<NetwPlayer> NetwMultiplayer::player_local() {
    return player_joined_of(get_unique_id());
}

bool NetwMultiplayer::player_admit(int64_t p_peer) {
    PlayerRow *found = player_row(p_peer);
    if (found == nullptr || found->admitted) {
        return false;
    }
    found->admitted = true;
    return true;
}

Ref<NetwPlayer> NetwMultiplayer::player_admitted_of(int64_t p_peer) const {
    const PlayerRow *found = player_row(p_peer);
    if (found == nullptr || !found->admitted) {
        return Ref<NetwPlayer>();
    }
    return found->row;
}

TypedArray<NetwPlayer> NetwMultiplayer::player_admitted_all() const {
    LocalVector<int64_t> peers;
    for (const KeyValue<int64_t, PlayerRow> &row : players) {
        if (row.value.admitted) {
            peers.push_back(row.value.peer);
        }
    }
    peers.sort();
    TypedArray<NetwPlayer> out;
    for (uint32_t at = 0; at < peers.size(); at++) {
        out.push_back(player_admitted_of(peers[at]));
    }
    return out;
}

Ref<NetwPlayer> NetwMultiplayer::player_admitted_local() {
    return player_admitted_of(get_unique_id());
}

Ref<NetwPlayer> NetwMultiplayer::player_find_account(
    const String &p_account
) const {
    if (p_account.is_empty()) {
        return Ref<NetwPlayer>();
    }
    for (const KeyValue<int64_t, PlayerRow> &row : players) {
        if (row.value.admitted && row.value.row.is_valid()
            && row.value.row->get_account() == p_account) {
            return row.value.row;
        }
    }
    return Ref<NetwPlayer>();
}

TypedArray<NetwEntity> NetwMultiplayer::player_entities_held(
    int64_t p_peer,
    int64_t p_player_id
) {
    TypedArray<NetwEntity> held;
    const TypedArray<NetwEntity> everywhere = scene_player_entities_all();
    for (int at = 0; at < everywhere.size(); at++) {
        const Ref<NetwEntity> entity = everywhere[at];
        if (entity.is_null() || entity->get_peer_id() != p_peer) {
            continue;
        }
        if (entity->get_player_id() == p_player_id) {
            held.push_back(entity);
        }
    }
    return held;
}

TypedArray<NetwEntity> NetwMultiplayer::player_get_entities(int64_t p_peer) {
    return player_entities_held(p_peer, player_incarnation(p_peer));
}

SceneMembership &NetwMultiplayer::membership_book() {
    return scene_membership;
}

int64_t NetwMultiplayer::membership_entity_id(const RID &p_entity) {
    return int64_t(p_entity.get_id());
}

bool NetwMultiplayer::membership_member_active(int64_t p_peer) const {
    const PlayerRow *found = player_row(p_peer);
    return found != nullptr && found->incarnation != 0;
}

bool NetwMultiplayer::membership_destination_foreign(Node *p_owner) const {
    if (p_owner == nullptr) {
        return false;
    }
    for (Node *above = p_owner->get_parent(); above != nullptr;
         above = above->get_parent()) {
        const RID held = handle_of_wrapper(wrapper_at(above).ptr());
        if (!held.is_valid()) {
            continue;
        }
        return slot_record(held.get_id()) == nullptr;
    }
    return false;
}

void NetwMultiplayer::membership_place_entity(const Ref<NetwEntity> &p_entity) {
    if (!is_host() || p_entity.is_null()) {
        return;
    }
    const int64_t peer = p_entity->get_peer_id();
    const RID handle = p_entity->get_rid_handle();
    if (peer == 0 || !handle.is_valid()) {
        return;
    }
    Node *owner = wrapper_owner(handle);
    if (owner == nullptr) {
        return;
    }
    const int64_t entity = membership_entity_id(handle);
    RID scene = scene_of(handle);
    if (scene == handle) {
        scene = RID();
    }
    if (membership_destination_foreign(owner)) {
        NETW_ERROR(
            sys::SCENE,
            "Entity %d was moved under another MultiplayerViewport, which is "
            "not supported. Despawn it here and spawn it in the other "
            "viewport.",
            int(p_entity->get_route())
        );
        scene = RID();
    }
    if (scene.is_valid() && !membership_member_active(peer)) {
        NETW_TRACE(
            sys::SCENE,
            "Route %d is in a scene that peer %d is not in, so nothing "
            "watches it.",
            int(p_entity->get_route()),
            int(peer)
        );
        scene = RID();
    }
    LocalVector<RID> standing;
    scene_membership.scenes_of_entity(peer, entity, standing);
    for (uint32_t at = 0; at < standing.size(); ++at) {
        if (standing[at] != scene) {
            scene_membership.entity_exit(peer, standing[at], entity);
        }
    }
    if (scene.is_valid()) {
        scene_membership.entity_enter(peer, scene, entity);
    }
    membership_settle();
}

void NetwMultiplayer::membership_drop_entity(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    const RID handle = p_entity->get_rid_handle();
    const int64_t peer = p_entity->get_peer_id();
    if (!handle.is_valid() || peer == 0) {
        return;
    }
    const int64_t entity = membership_entity_id(handle);
    LocalVector<RID> standing;
    scene_membership.scenes_of_entity(peer, entity, standing);
    for (uint32_t at = 0; at < standing.size(); ++at) {
        scene_membership.entity_exit(peer, standing[at], entity);
    }
    membership_settle();
}

void NetwMultiplayer::membership_drop_entity_in(
    const RID &p_entity,
    const RID &p_scene,
    int64_t p_peer
) {
    if (!p_entity.is_valid() || !p_scene.is_valid() || p_peer == 0) {
        return;
    }
    scene_membership
        .entity_exit(p_peer, p_scene, membership_entity_id(p_entity));
    membership_settle();
}

void NetwMultiplayer::membership_mirror_release(int64_t p_peer) {
    scene_membership.forget_member(p_peer);
    membership_settle();
}

void NetwMultiplayer::membership_settle() {
    const LocalVector<SceneMembership::Edge> &edges
        = scene_membership.pending_edges();
    LocalVector<SceneMembership::Edge> settling;
    settling.reserve(edges.size());
    for (uint32_t at = 0; at < edges.size(); ++at) {
        settling.push_back(edges[at]);
    }
#if defined(NETW_TESTS)
    residency_audit::Ledger &book = residency_audit::ledger();
    for (uint32_t at = 0; at < settling.size(); ++at) {
        if (settling[at].subscribed) {
            book.entered++;
        } else {
            book.left++;
        }
    }
#endif
    scene_membership.clear_edges();
    for (uint32_t at = 0; at < settling.size(); ++at) {
        scene_sync_viewer(settling[at].scene, settling[at].member);
    }
    if (!settling.is_empty()) {
        scene_player_display_invalidate();
    }
#if defined(NETW_TESTS)
    membership_audit();
#endif
}

#if defined(NETW_TESTS)

RID NetwMultiplayer::membership_residency_of(int64_t p_entity) const {
    NetwEntityRecord *const record = slot_record(p_entity);
    if (record == nullptr) {
        return RID();
    }
    const RID handle = record->get_handle();
    if (wrapper_owner(handle) == nullptr) {
        return RID();
    }
    const RID scene = scene_of(handle);
    return scene == handle ? RID() : scene;
}

void NetwMultiplayer::membership_note_divergence(
    int64_t p_member,
    const RID &p_scene,
    int64_t p_entity
) {
    residency_audit::Ledger &book = residency_audit::ledger();
    book.divergences++;
    if (book.divergences > 1) {
        return;
    }
    book.member_of_first = p_member;
    book.scene_of_first = int64_t(p_scene.get_id());
    book.entity_of_first = p_entity;
}

void NetwMultiplayer::membership_audit() {
    if (!is_host() || membership_audit_held > 0
        || !entity_departures.is_empty()) {
        return;
    }
    residency_audit::Ledger &book = residency_audit::ledger();
    for (const KeyValue<int64_t, PlayerRow> &row : players) {
        LocalVector<RID> scenes;
        scene_membership.entity_scenes_of(row.value.peer, scenes);
        for (uint32_t at = 0; at < scenes.size(); ++at) {
            const PackedInt64Array entities
                = scene_membership.entities_of(row.value.peer, scenes[at]);
            for (int which = 0; which < entities.size(); ++which) {
                book.observations++;
                if (membership_residency_of(entities[which]) == scenes[at]) {
                    continue;
                }
                membership_note_divergence(
                    row.value.peer,
                    scenes[at],
                    entities[which]
                );
            }
        }
    }
    spawn::Pipeline *pipeline = spawn_plane();
    if (pipeline == nullptr) {
        return;
    }
    LocalVector<NetwEntityRecord *> held;
    entity_slots.each([&held](uint64_t, const EntitySlot &p_slot) {
        if (p_slot.record != nullptr) {
            held.push_back(p_slot.record);
        }
    });
    for (NetwEntityRecord *record : held) {
        const int64_t peer = record->get_peer_id();
        if (peer == 0 || !membership_member_active(peer)
            || !pipeline->is_booked(record->get_route())) {
            continue;
        }
        const int64_t entity = membership_entity_id(record->get_handle());
        const RID resides = membership_residency_of(entity);
        if (!resides.is_valid()) {
            continue;
        }
        book.observations++;
        if (scene_membership.entities_of(peer, resides).has(entity)) {
            continue;
        }
        membership_note_divergence(peer, resides, entity);
    }
}

#endif

void NetwMultiplayer::player_forget(int64_t p_peer) {
    const PlayerRow *found = player_row(p_peer);
    if (found == nullptr) {
        return;
    }
    const int64_t player_id = found->incarnation != 0
        ? found->incarnation
        : *player_id_of_peer.getptr(p_peer);
    if (found->row == local_player) {
        bind_local_player(Ref<NetwPlayer>());
    }
    participant_unindex(p_peer, player_id);
    players.erase(player_id);
    membership_mirror_release(p_peer);
}

void NetwMultiplayer::player_clear() {
    bind_local_player(Ref<NetwPlayer>());
    players.clear();
    player_id_of_peer.clear();
    scene_membership.clear();
}

void NetwMultiplayer::bind_local_player(const Ref<NetwPlayer> &p_row) {
    local_player = p_row;
}

int64_t NetwMultiplayer::player_incarnation(int64_t p_peer) const {
    const PlayerRow *found = player_row(p_peer);
    return found ? found->incarnation : 0;
}

bool NetwMultiplayer::player_is_active(
    int64_t p_peer,
    int64_t p_incarnation
) const {
    const PlayerRow *found = player_row(p_peer);
    return found != nullptr && found->admitted
        && found->incarnation == p_incarnation;
}

bool NetwMultiplayer::player_holds(const Ref<NetwPlayer> &p_who) const {
    if (p_who.is_null()) {
        return false;
    }
    const PlayerRow *found = player_row(p_who->get_peer_id());
    return found != nullptr && found->admitted && found->row == p_who;
}

Error NetwMultiplayer::player_kick(
    const Ref<NetwPlayer> &p_who,
    const String &p_reason
) {
    NETW_ERR_COND_V(
        !is_host(),
        ERR_UNAUTHORIZED,
        sys::SESSION,
        "NetwSession.kick runs only on the server."
    );
    NETW_ERR_COND_V(
        !player_holds(p_who),
        ERR_INVALID_PARAMETER,
        sys::SESSION,
        "NetwSession.kick got a player who already left."
    );
    const int64_t peer = p_who->get_peer_id();
    NETW_ERR_COND_V(
        peer == get_unique_id(),
        ERR_INVALID_PARAMETER,
        sys::SESSION,
        "The server cannot kick its own player. Call "
        "NetwSession.leave to end the session."
    );
    player_release_id(p_who);
    session_turn_peer_away(peer, p_reason);
    return OK;
}

void NetwMultiplayer::player_release_id(const Ref<NetwPlayer> &p_who) {
    const int64_t peer = p_who->get_peer_id();
    PlayerRow *found = player_row(peer);
    if (found != nullptr) {
        found->admitted = false;
        found->incarnation = 0;
        membership_mirror_release(peer);
    }
    join_roster.forget(peer);
    player_publish_left(p_who);
    player_forget(peer);
}

void NetwMultiplayer::player_publish_left(const Ref<NetwPlayer> &p_who) {
    if (p_who.is_null()) {
        return;
    }
    if (session_handle.is_valid()) {
        session_handle->announce_player_left(p_who);
    }
    notify(PLAYER_LEFT, p_who);
}

void NetwMultiplayer::player_publish_joined(int64_t p_peer) {
    const Ref<NetwPlayer> player = player_of(p_peer);
    if (player.is_null()) {
        return;
    }
    if (p_peer == get_unique_id()) {
        bind_local_player(player);
        if (session_handle.is_valid()) {
            session_handle->announce_local_joined(player);
        }
        notify(PLAYER_LOCAL_JOINED, player);
    }
    if (scene_admits_on_join) {
        scene_on_player_joined(player);
    }
    if (session_handle.is_valid()) {
        session_handle->announce_player_joined(player);
    }
    notify(PLAYER_JOINED, player);
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
        NETW_TRACE(sys::SESSION, "A live session has no SceneTree.");
        return false;
    }
    Ref<MultiplayerAPI> published = get_session_api();
    if (published.is_null()) {
        NETW_TRACE(sys::SESSION, "The session has no MultiplayerAPI.");
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
                "A disposed session left the list of live sessions."
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
        NETW_TRACE(
            sys::SESSION,
            "A node outside the scene tree has no session."
        );
        return nullptr;
    }
    return of_installed(p_node->get_multiplayer());
}

NetwMultiplayer *NetwMultiplayer::of_leaving(Node *p_node) {
    if (p_node == nullptr || !p_node->is_inside_tree()) {
        return nullptr;
    }
    String path;
    for (Node *at = p_node; at != nullptr; at = at->get_parent()) {
        path = "/" + String(at->get_name()) + path;
    }
    const Ref<MultiplayerAPI> installed
        = p_node->get_tree()->get_multiplayer(NodePath(path));
    return of_installed(installed);
}

NetwMultiplayer *NetwMultiplayer::of_installed(
    const Ref<MultiplayerAPI> &p_installed
) {
    const Ref<MultiplayerAPI> installed = p_installed;
    if (installed.is_null()) {
        NETW_TRACE(sys::SESSION, "A node's branch has no MultiplayerAPI.");
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
    NETW_TRACE(
        sys::SESSION,
        "No live session owns the MultiplayerAPI on that branch."
    );
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

void NetwMultiplayer::set_scene_viewer_edge(const Callable &p_edge) {
    scene_viewer_edge = p_edge;
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
        notify(SERVICE_REGISTERED, p_service);
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
                "A service of type %s was already registered, and the new "
                "one replaces it. Call NetwSession.unregister_service "
                "first.",
                type->to_string()
            );
        }
        services[at].service = gd::instance_id(p_service);
        discovery_watch_directory(p_service);
        notify(SERVICE_REGISTERED, p_service);
        return;
    }
    ServiceRow row;
    row.type = gd::instance_id(type);
    row.service = gd::instance_id(p_service);
    services.push_back(row);
    discovery_watch_directory(p_service);
    notify(SERVICE_REGISTERED, p_service);
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
    notify(SERVICE_UNREGISTERED, p_service);
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
    const bool from_server = p_sender == SERVER_PEER;
    session::SessionReason stated;
    const bool states_reason = session::frame_read(p_payload, stated);
    if (p_channel == control_channels.pause) {
        if (from_server && states_reason) {
            session_apply_pause(true, stated.reason);
        }
        return true;
    }
    if (p_channel == control_channels.unpause) {
        if (from_server && p_payload.is_empty()) {
            session_apply_pause(false, String());
        }
        return true;
    }
    if (p_channel == control_channels.kicked) {
        if (from_server && states_reason) {
            if (join_awaiting_admission) {
                session_report_join_failure(ERR_UNAUTHORIZED, stated.reason);
            }
            session_note_exit(enums::NetwSession::KICKED, stated.reason);
            notify(PEER_KICKED, stated.reason);
        }
        return true;
    }
    if (p_channel == control_channels.shutdown) {
        if (from_server && states_reason) {
            session_note_exit(
                enums::NetwSession::SERVER_SHUTDOWN,
                stated.reason
            );
            notify(SESSION_SERVER_DISCONNECTING, stated.reason);
        }
        return true;
    }
    if (p_channel == control_channels.kick_request) {
        session::KickRequest asked;
        if (is_host() && session::frame_read(p_payload, asked)) {
            notify(PEER_KICK_REQUESTED, p_sender, asked.peer, asked.reason);
        }
        return true;
    }
    if (p_channel == control_channels.leave_request) {
        if (is_host() && states_reason) {
            notify(SESSION_DISCONNECT_REQUESTED, p_sender, stated.reason);
        }
        return true;
    }
    return false;
}

void NetwMultiplayer::session_admit(const session::AcceptFrame &p_accepted) {
    if (p_accepted.peer_id == 0 || p_accepted.player_id == 0) {
        return;
    }
    if (!join_roster.remember(p_accepted)) {
        return;
    }
    const int64_t joined = p_accepted.peer_id;
    if (joined == get_unique_id()) {
        join_awaiting_admission = false;
        submitted_join.reset();
    }
    if (player_ensure(p_accepted).is_null()) {
        return;
    }
    player_admit(joined);
    player_publish_joined(joined);
}

bool NetwMultiplayer::session_preflight_join(
    const session::AcceptFrame &p_accepted,
    const Array &p_args
) {
    const JoinPlan plan = session_resolve_join();
    if (!plan.available) {
        session_fail_join(
            p_accepted.peer_id,
            ERR_UNCONFIGURED,
            "This server declares a join handler whose object is unavailable"
        );
        return false;
    }
    if (!plan.handler.is_valid()) {
        return true;
    }
    if (plan.declared && p_args.is_empty()
        && !join_arg_types(plan.handler).is_empty()) {
        NETW_WARN(
            sys::SESSION,
            "Peer %d joined with no arguments, but the Netw.configure_join "
            "handler needs %d. Pass the arguments to Netw.prepare_join on "
            "the client.",
            int(p_accepted.peer_id),
            int(join_arg_types(plan.handler).size())
        );
        session_fail_join(
            p_accepted.peer_id,
            ERR_INVALID_DATA,
            "This server's join handler needs arguments the join carried none "
            "of"
        );
        return false;
    }
    return true;
}

void NetwMultiplayer::session_fail_join(
    int64_t p_peer,
    Error p_error,
    const String &p_reason
) {
    const int64_t peer = p_peer;
    join_roster.forget(peer);
    session_refuse(peer, p_reason);
    if (peer == get_unique_id()) {
        session_report_join_failure(p_error, p_reason);
        return;
    }
    if (is_host()) {
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
    for (const TurnedAwayPeer &row : turned_away_peers) {
        if (row.peer == p_peer) {
            return;
        }
    }
    turned_away_peers.push_back(TurnedAwayPeer{p_peer, 0});
}

void NetwMultiplayer::session_drop_turned_away_peer(int64_t p_peer) {
    if (effective_peer.is_valid()) {
        effective_peer->disconnect_peer(int32_t(p_peer));
    }
    session_settle_peer_loss(p_peer);
}

void NetwMultiplayer::session_poll_turned_away() {
    LocalVector<int64_t> due;
    uint32_t kept = 0;
    for (uint32_t at = 0; at < turned_away_peers.size(); ++at) {
        TurnedAwayPeer row = turned_away_peers[at];
        row.polls += 1;
        if (row.polls < POLLS_BEFORE_TURNED_AWAY_PEER_DROPS) {
            turned_away_peers[kept++] = row;
            continue;
        }
        due.push_back(row.peer);
    }
    turned_away_peers.resize(kept);
    for (const int64_t peer : due) {
        session_drop_turned_away_peer(peer);
    }
}

void NetwMultiplayer::session_report_join_failure(
    Error p_error,
    const String &p_reason
) {
    join_awaiting_admission = false;
    prepared_join.reset();
    submitted_join.reset();
    if (session_handle.is_valid()) {
        session_handle->announce_join_failed(int64_t(p_error), p_reason);
    }
    notify(SESSION_JOIN_FAILED, int64_t(p_error), p_reason);
}

void NetwMultiplayer::session_run_join_handler(
    const session::AcceptFrame &p_accepted,
    const Array &p_args
) {
    if (!is_host() || p_accepted.peer_id == 0) {
        return;
    }
    const int64_t peer = p_accepted.peer_id;
    PlayerRow *seated = player_row(peer);
    if (seated == nullptr || !seated->admitted || seated->handled) {
        return;
    }
    const JoinPlan plan = session_resolve_join();
    if (!plan.available || !plan.handler.is_valid()) {
        seated->handled = true;
        return;
    }
    const Ref<NetwPlayer> joining = seated->row;
    if (joining.is_null()) {
        return;
    }
    seated->handled = true;
    Array args;
    args.push_back(joining);
    args.append_array(p_args);
    bool called = false;
    {
        NETW_ZONE_NC("session run join handler", colors::SESSION);
        gd::call_checked(plan.handler, args, called);
    }
    if (!called) {
        NETW_ERROR(
            sys::SESSION,
            "Peer %d joined, but its join arguments do not match the "
            "Netw.configure_join handler. Make the handler's parameters "
            "match what the client passes to Netw.prepare_join.",
            int(peer)
        );
    }
}

void NetwMultiplayer::session_after_entered() {
    const int64_t role = session_get_role();
    bool joins_by_default = role != ROLE_DEDICATED_SERVER;
#if defined(NETW_TESTS)
    joins_by_default = joins_by_default && !session_default_join_held;
#endif
    if (!prepared_join.has_value() && joins_by_default) {
        prepared_join = JoinRequest();
    }
    if (role == ROLE_CLIENT) {
        session_submit_prepared_join();
        return;
    }
    session_submit_host_join();
}

void NetwMultiplayer::session_submit_host_join() {
    if (!prepared_join.has_value()) {
        return;
    }
    if (scene_list().size() > 0) {
        session_submit_prepared_join();
        return;
    }
    host_join_waits_startup = true;
}

void NetwMultiplayer::session_submit_host_join_on_startup() {
    session_submit_prepared_join();
}

void NetwMultiplayer::session_after_edge(int64_t, int64_t p_new) {
    if (p_new == SESSION_STATE_OFFLINE && session_exited_awaits_offline >= 0) {
        callable_mp(this, &NetwMultiplayer::session_announce_exited)
            .call_deferred(
                session_exited_awaits_offline,
                session_exited_detail
            );
        session_exited_awaits_offline = -1;
        session_exited_detail = String();
    }
    if (p_new == SESSION_STATE_OFFLINE) {
        session_clear_prepared_join();
        serverless_mesh_polls = 0;
        transfer_awaits_roster = false;
        transfer_authored = PackedInt64Array();
        clock_engine().set_held(false);
    }
}

void NetwMultiplayer::session_relay_peer_connected(int64_t p_peer) {
    if (installing_transfer) {
        return;
    }
    peer_mark_reachable(p_peer);
    settled_peer_departures.erase(p_peer);
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
    if (installing_transfer) {
        return;
    }
    session_settle_peer_loss(p_peer);
}

void NetwMultiplayer::session_clear_disconnected_peer(int64_t p_peer) {
    auth_forget_link(p_peer);
    session_clear_link_delivery(p_peer);
}

void NetwMultiplayer::session_clear_link_delivery(int64_t p_peer) {
    forget_peer_seqs(p_peer);
    liveness_core->abandon_lease(int(p_peer));
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->clear_peer(p_peer);
    }
    rpc_handle_disconnect(p_peer);
}

void NetwMultiplayer::session_teardown_online() {
    session_transition(SESSION_STATE_DISCONNECTING);
    session_transition(SESSION_STATE_OFFLINE);
}

void NetwMultiplayer::session_settle_peer_loss(int64_t p_peer) {
    if (p_peer == 0 || settled_peer_departures.has(p_peer)) {
        return;
    }
    settled_peer_departures.insert(p_peer);

    const bool terminates_session = p_peer == SERVER_PEER
        && !embed_is_disposing() && session_get_state() == SESSION_STATE_ONLINE;

    unreachable_peers.insert(p_peer);
    session_clear_disconnected_peer(p_peer);

    interest_peer_disconnected(p_peer);
    entity_settle_peer_departure(p_peer);
    halt_forget_peer(p_peer);

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
    peer_forget(p_peer);

    if (terminates_session) {
        session_teardown_online();
    }
}

void NetwMultiplayer::session_transport_connected() {
    if (installing_transfer) {
        return;
    }
    if (session_get_state() != SESSION_STATE_CONNECTING) {
        NETW_TRACE(
            sys::SESSION,
            "The transport connected while the session is in state %d.",
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
            "The transport failed while the session is in state %d.",
            session_get_state()
        );
        return;
    }
    connect_core.on_transport_failed();
    session_transition(SESSION_STATE_OFFLINE);
}

void NetwMultiplayer::session_transport_dropped() {
    if (embed_is_disposing()) {
        NETW_TRACE(
            sys::SESSION,
            "The transport dropped while the session is already closing."
        );
        return;
    }
    if (session_get_state() == SESSION_STATE_CONNECTING) {
        session_transport_failed();
        return;
    }
    if (session_get_state() != SESSION_STATE_ONLINE) {
        NETW_TRACE(
            sys::SESSION,
            "The transport dropped while the session is in state %d.",
            session_get_state()
        );
        return;
    }
    session_teardown_online();
}

void NetwMultiplayer::session_settle_clear_state() {
    callable_mp(this, &NetwMultiplayer::clear_session_state).call_deferred();
}

void NetwMultiplayer::session_set_tree_paused(bool p_paused) {
    TreePauseHold &hold = tree_pause_hold();
    SceneTree *loop = gd::scene_tree();
    if (p_paused) {
        if (hold.sessions++ == 0) {
            hold.paused_by_game = loop != nullptr && loop->is_paused();
        }
        if (loop != nullptr) {
            loop->set_pause(true);
        }
        return;
    }
    if (hold.sessions == 0) {
        return;
    }
    hold.sessions -= 1;
    if (hold.sessions == 0 && !hold.paused_by_game && loop != nullptr) {
        loop->set_pause(false);
    }
}

void NetwMultiplayer::session_apply_pause(
    bool p_paused,
    const String &p_reason
) {
    if (session_paused == p_paused) {
        return;
    }
    session_paused = p_paused;
    session_pause_reason = p_paused ? p_reason : String();
    session_set_tree_paused(p_paused);
    if (p_paused) {
        notify(SESSION_TREE_PAUSED, p_reason);
    } else {
        notify(SESSION_TREE_UNPAUSED);
    }
    if (session_handle.is_valid()) {
        session_handle->announce_paused_changed(p_paused, session_pause_reason);
    }
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
    if (!is_host()) {
        return;
    }
    const Time *reading = Time::get_singleton();
    if (session_core.join_flooded(
            int(p_sender),
            reading ? int64_t(reading->get_ticks_msec()) : 0
        )) {
        return;
    }
    if (structure_halted) {
        halt_park_join(p_payload, p_sender);
        return;
    }
    session_accept_join(p_payload, p_sender);
}

void NetwMultiplayer::session_accept_join(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    JoinRequest requested;
    if (!requested.deserialize(p_payload)) {
        NETW_WARN(
            sys::SESSION,
            "Peer %d sent a join request that could not be read, so it "
            "was dropped.",
            int(p_sender)
        );
        return;
    }
    requested.peer_id = p_sender;
    const char *differs = session_identity_differs(requested);
    if (differs != nullptr) {
        NETW_WARN(
            sys::SESSION,
            "Peer %d was refused, because its %s identity differs from "
            "this server's. Run the same build on both peers.",
            int(p_sender),
            differs
        );
        session_refuse(
            p_sender,
            vformat("Incompatible build: %s", String(differs))
        );
        return;
    }
    if (!session_decode_join_args(requested)) {
        session_fail_join(p_sender, ERR_INVALID_DATA, "Invalid join arguments.");
        return;
    }
    session_start_admission(requested);
}

void NetwMultiplayer::session_finish_join(
    const JoinRequest &p_request,
    const Ref<NetwJoinRequest> &p_admission
) {
    JoinRequest requested = p_request;
    const int64_t p_sender = requested.peer_id;
    const String account = p_admission->get_account();
    const Ref<NetwPlayer> playing = player_find_account(account);
    if (playing.is_valid() && playing->get_peer_id() != p_sender) {
        session_fail_join(
            p_sender,
            ERR_ALREADY_IN_USE,
            "This account is already playing in the session."
        );
        return;
    }
    session::AcceptFrame accepting;
    if (!session_resolve_inbound_join(requested, p_sender, accepting)) {
        return;
    }
    accepting.account = account;
    accepting.account_name
        = account.is_empty() ? String() : p_admission->get_account_name();
    const Array decoded = requested.arg_values;
    if (!session_preflight_join(accepting, decoded)) {
        return;
    }
    session_admit(accepting);

    if (inner.is_valid()) {
        set_peer_ids(gd::api_peer_ids(inner));
    }
    const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
    for (int at = 0; at < int(peers.size()); ++at) {
        session_send_accept(accepting, peers[at]);
    }
    if (p_sender != get_unique_id()) {
        send_to(
            p_sender,
            0,
            session_roster_channel,
            join_roster.roster_frame(p_sender),
            true,
            0,
            String(),
            false
        );
        liveness_grant_lease(p_sender);
        if (session_paused) {
            send_to(
                p_sender,
                0,
                control_channels.pause,
                session::frame_write(
                    session::SessionReason{session_pause_reason}
                ),
                true,
                0,
                String(),
                false
            );
        }
    }
    session_run_join_handler(accepting, decoded);
}

PackedByteArray NetwMultiplayer::session_accept_payload(
    session::AcceptFrame p_row,
    int64_t p_destination
) const {
    if (p_destination == p_row.peer_id) {
        p_row.join_address = connect_core.join_address();
        p_row.max_players = uint64_t(connection_max_players());
    }
    return session::frame_write(session::accept_seen_by(p_row, p_destination));
}

void NetwMultiplayer::session_send_accept(
    const session::AcceptFrame &p_row,
    int64_t p_destination
) {
    send_to(
        p_destination,
        0,
        session_accept_channel,
        session_accept_payload(p_row, p_destination),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::session_receive_accept(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != SERVER_PEER) {
        return;
    }
    session::AcceptFrame accepted;
    if (!session::frame_read(p_payload, accepted)) {
        NETW_TRACE(
            sys::SESSION,
            "A join acceptance that did not decode fully was ignored."
        );
        return;
    }
    if (accepted.peer_id == get_unique_id()) {
        connect_core.advertise(
            accepted.join_address, int64_t(accepted.max_players)
        );
    }
    session_admit(accepted);
}

int64_t NetwMultiplayer::connection_max_players() const {
    if (!has_server_role()) {
        return connect_core.advertised_max_players();
    }
    return session_settings.server_info.is_valid()
        ? session_settings.server_info->get_max_players()
        : 0;
}

Error NetwMultiplayer::connection_set_join_address(const String &p_address) {
    if (!has_server_role() || !is_online()) {
        NETW_ERROR(
            sys::TRANSPORT,
            "Only an online server sets NetwConnection.join_address. Set it "
            "after NetwConnection.host settles."
        );
        return ERR_UNAVAILABLE;
    }
    connect_core.advertise(p_address, 0);
    const int64_t mine = get_unique_id();
    for (const session::AcceptFrame &row : join_roster.accepted_joins()) {
        if (row.peer_id != mine) {
            session_send_accept(row, row.peer_id);
        }
    }
    return OK;
}

void NetwMultiplayer::session_receive_roster(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != SERVER_PEER) {
        return;
    }
    LocalVector<session::AcceptFrame> rows;
    if (!session::roster_read(p_payload, rows)) {
        NETW_TRACE(
            sys::SESSION,
            "A roster that did not decode fully was ignored."
        );
        return;
    }
    for (uint32_t at = 0; at < rows.size(); ++at) {
        session_admit(rows[at]);
    }
    if (transfer_awaits_roster) {
        transfer_readmitted();
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
    session_exit_reason = -1;
    session_exit_detail = String();
    transfer_label = get_unique_id();
    transfer_installed = TransferInstall();
    spawner_session_entered();
    scene_on_session_entered();
    if (clock_handshakes_on_enter) {
        clock_start_handshake();
    }
    if (session_handle.is_valid()) {
        session_handle->announce_entered();
    }
    notify(SESSION_ENTERED);
    scene_player_display_invalidate();
    entity_publish_placed();
    entity_demote_placed();
    session_after_entered();
    connect_core.on_session_online();
}

void NetwMultiplayer::session_announce_ended() {
    const int reason = session_exit_reason >= 0
        ? session_exit_reason
        : int(enums::NetwSession::CONNECTION_LOST);
    const String detail = session_exit_detail;
    session_exit_reason = -1;
    session_exit_detail = String();
    if (session_handle.is_valid()) {
        session_handle->announce_exiting(
            enums::NetwSession::ExitReason(reason),
            detail
        );
    }
    session_apply_pause(false, String());
    spawner_session_ended();
    liveness_settle_clear_session();
    interest_session_ended();
    session_settle_clear_state();
    clear_roster();
    session_set_role(ROLE_NONE);
    notify(SESSION_ENDED);
    scene_on_session_reclaimed();
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->free_session_nodes();
    }
    notify(SESSION_RECLAIMED);
    scene_player_display_invalidate();
    callable_mp(this, &NetwMultiplayer::session_announce_exited)
        .call_deferred(reason, detail);
}

void NetwMultiplayer::session_announce_exited(
    int p_reason,
    const String &p_detail
) {
    if (session_get_state() == SESSION_STATE_DISCONNECTING) {
        session_exited_awaits_offline = p_reason;
        session_exited_detail = p_detail;
        return;
    }
    if (session_handle.is_valid()) {
        session_handle->announce_exited(
            enums::NetwSession::ExitReason(p_reason),
            p_detail
        );
    }
}

void NetwMultiplayer::session_note_exit(
    enums::NetwSession::ExitReason p_reason,
    const String &p_detail
) {
    if (session_exit_reason >= 0
        || session_get_state() != SESSION_STATE_ONLINE) {
        return;
    }
    session_exit_reason = int(p_reason);
    session_exit_detail = p_detail;
}

void NetwMultiplayer::session_announce_edge(int p_old, int p_new) {
    sim_follow_server();
    session_after_edge(p_old, p_new);
    connect_core.on_session_edge(p_old, p_new);
    session_publish_state();
}

void NetwMultiplayer::session_publish_state() {
    const SessionState now = session_get_computed_state();
    if (now == session_published_state) {
        return;
    }
    const SessionState was = session_published_state;
    session_published_state = now;
    notify(SESSION_STATE_CHANGED, int64_t(was), int64_t(now));
    if (session_handle.is_valid()) {
        session_handle->announce_state_changed(now);
    }
}

session::AcceptFrame NetwMultiplayer::session_accepted_join(
    int64_t p_peer
) const {
    return join_roster.accepted_join(p_peer);
}

bool NetwMultiplayer::session_has_accepted(int64_t p_peer) const {
    return join_roster.has_accepted(p_peer);
}

LocalVector<session::AcceptFrame> NetwMultiplayer::
    session_accepted_joins() const {
    return join_roster.accepted_joins();
}

bool NetwMultiplayer::session_remember_join(
    const session::AcceptFrame &p_accepted
) {
    return join_roster.remember(p_accepted);
}

#if defined(NETW_TESTS)
PackedByteArray NetwMultiplayer::session_accept_bytes(
    int64_t p_peer,
    int64_t p_destination
) const {
    if (!join_roster.has_accepted(p_peer)) {
        return PackedByteArray();
    }
    return session_accept_payload(
        join_roster.accepted_join(p_peer), p_destination
    );
}

PackedByteArray NetwMultiplayer::session_roster_bytes(
    int64_t p_destination
) const {
    return join_roster.roster_frame(p_destination);
}
#endif

void NetwMultiplayer::session_forget_peer(int64_t p_peer) {
    peer_buckets.erase(p_peer);
    join_roster.forget(p_peer);
}

void NetwMultiplayer::session_clear_roster() {
    peer_buckets.clear();
    link_handshakes.clear();
    pending_admissions.clear();
    join_roster.clear();
    unreachable_peers.clear();
    settled_peer_departures.clear();
    call_faults_reported.clear();
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

PackedInt32Array NetwMultiplayer::spawn_peer_ids() const {
    const PackedInt32Array reachable = reachable_peer_ids();
    if (resync_awaited.is_empty()) {
        return reachable;
    }
    PackedInt32Array out;
    for (int at = 0; at < reachable.size(); at++) {
        if (!resync_awaited.has(int64_t(reachable[at]))) {
            out.push_back(reachable[at]);
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

bool NetwMultiplayer::session_links_peers_but_not_server() const {
    if (!session_link_is_connected()
        || inner->get_multiplayer_peer()->get_unique_id() == SERVER_PEER) {
        return false;
    }
    const PackedInt32Array admitted = gd::api_peer_ids(inner);
    const PackedInt32Array authenticating = get_authenticating_peers();
    if (admitted.has(int32_t(SERVER_PEER))
        || authenticating.has(int32_t(SERVER_PEER))) {
        return false;
    }
    return !admitted.is_empty() || !authenticating.is_empty();
}

void NetwMultiplayer::session_watch_serverless_mesh() {
    if (serverless_mesh_polls >= SERVERLESS_MESH_WARN_POLLS
        || session_get_state() != SESSION_STATE_CONNECTING
        || !session_links_peers_but_not_server()) {
        return;
    }
    serverless_mesh_polls += 1;
    if (serverless_mesh_polls == SERVERLESS_MESH_WARN_POLLS) {
        NETW_WARN(
            sys::SESSION,
            "This peer is linked to other peers but not to peer 1, so it is "
            "not online. Link it to the endpoint created with id 1, which is "
            "the server."
        );
    }
}

void NetwMultiplayer::session_watch_late_server() {
    if (session_get_state() != SESSION_STATE_CONNECTING
        || !session_link_is_connected()
        || NETW_API_VIRTUAL(get_unique_id)() != SERVER_PEER) {
        return;
    }
    session_transport_connected();
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
    if (p_peer == SERVER_PEER && relabel_source.is_valid()) {
        return;
    }
    if (peer_released_by_transport(p_peer)) {
        NETW_TRACE(
            sys::TRANSPORT,
            "Peer %d is leaving, so a packet for it was dropped.",
            int(p_peer)
        );
    } else {
        NETW_WARN(
            sys::TRANSPORT,
            "Peer %d refused a packet, so nothing more is sent to it until it "
            "connects again.",
            int(p_peer)
        );
    }
    session_settle_peer_loss(p_peer);
}

bool NetwMultiplayer::peer_released_by_transport(int64_t p_peer) const {
    if (!session_link_is_connected()) {
        return false;
    }
    const Ref<MultiplayerPeer> link = inner->get_multiplayer_peer();
    const LocalMultiplayerPeer *local
        = Object::cast_to<LocalMultiplayerPeer>(link.ptr());
    if (local != nullptr) {
        return !local->is_linked_to(int(p_peer));
    }
    return !gd::peer_can_receive(link, p_peer);
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

TypedArray<NetwPlayer> NetwMultiplayer::get_connected_players() const {
    return player_all();
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
        "%s cannot be passed to MultiplayerAPI.object_configuration_add or "
        "object_configuration_remove. Declare it with %s before the "
        "session starts.",
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

void NetwMultiplayer::auth_set_app_tag(int64_t p_tag) {
    auth_app_tag = p_tag;
}

int64_t NetwMultiplayer::auth_app_tag_of() const {
    return auth_app_tag;
}

void NetwMultiplayer::set_auth_callback(const Callable &p_callback) {
    auth_app_callback = p_callback;
}

Callable NetwMultiplayer::get_auth_callback() const {
    return auth_app_callback;
}

void NetwMultiplayer::auth_clear() {
    auth_disarm();
    auth_app_callback = Callable();
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
        "Peer %d sent %d bytes of authentication this peer does not "
        "understand, so it was disconnected. Run the same build on both "
        "peers.",
        int(p_peer),
        p_data.size()
    );
    disconnect_peer(p_peer);
}

void NetwMultiplayer::auth_send_hello(int64_t p_peer) {
    NETW_ZONE_NC("netw::auth_send_hello", colors::SESSION);
    if (p_peer == 0 || has_server_role()) {
        return;
    }
    if (auth_app_callback.is_valid() || inner.is_null()) {
        return;
    }
    NETW_DEBUG(
        sys::SESSION,
        "Sending a hello to peer %d with app tag %s.",
        int(p_peer),
        String::num_uint64(auth_app_tag, 16)
    );
    const Error sent = send_auth(
        p_peer,
        auth::encode_client_hello(
            uint64_t(auth_app_tag),
            p_peer == SERVER_PEER && session_requires_proof()
                ? auth::HELLO_PROOF_CAPABLE : 0
        )
    );
    if (sent != OK) {
        NETW_WARN(
            sys::SESSION,
            "Authentication could not reach peer %d (error %d). The "
            "connection stays unauthenticated until "
            "SceneMultiplayer.auth_timeout closes it.",
            int(p_peer),
            int(sent)
        );
        return;
    }
    if (p_peer == SERVER_PEER && session_requires_proof()) {
        return;
    }
    const Error completed = auth_complete_link(p_peer);
    if (completed != OK) {
        NETW_ERROR(
            sys::SESSION,
            "Authentication with peer %d could not finish (error %d), so it "
            "was disconnected.",
            int(p_peer),
            int(completed)
        );
        disconnect_peer(p_peer);
    }
}

Error NetwMultiplayer::auth_complete_link(int64_t p_peer) {
    LinkHandshake &link = link_handshakes[p_peer];
    if (link.completed) {
        return OK;
    }
    link.completed = true;
    return complete_auth(p_peer);
}

void NetwMultiplayer::auth_forget_link(int64_t p_peer) {
    link_handshakes.erase(p_peer);
    pending_admissions.erase(p_peer);
}

#if defined(NETW_TESTS)
bool NetwMultiplayer::auth_link_is_completed(int64_t p_peer) const {
    const HashMap<int64_t, LinkHandshake>::ConstIterator found
        = link_handshakes.find(p_peer);
    return found != link_handshakes.end() && found->value.completed;
}
#endif

void NetwMultiplayer::auth_receive_hello(
    int64_t p_peer,
    const PackedByteArray &p_data
) {
    NETW_ZONE_NC("netw::auth_receive_hello", colors::SESSION);
    const auth::Hello decoded
        = auth::decode_client_hello(p_data, uint64_t(auth_app_tag));
    NETW_DEBUG(
        sys::SESSION,
        "Read a hello from peer %d with app tag %s, expecting %s.",
        int(p_peer),
        String::num_uint64(int64_t(decoded.app_tag), 16),
        String::num_uint64(auth_app_tag, 16)
    );
    if (!decoded.ok()) {
        if (decoded.refusal == auth::Refusal::APP) {
            NETW_WARN(
                sys::SESSION,
                "Peer %d was refused, because its app tag %s differs from "
                "this server's %s. Set the same tag on both with "
                "NetwMultiplayer.auth_set_app_tag.",
                int(p_peer),
                String::num_uint64(int64_t(decoded.app_tag), 16),
                String::num_uint64(auth_app_tag, 16)
            );
            session_refuse(p_peer, "Incompatible game build");
        } else {
            NETW_WARN(
                sys::SESSION,
                "Peer %d was disconnected, because its authentication could "
                "not be read (%s). Run the same Networked version on both "
                "peers.",
                int(p_peer),
                decoded.refusal == auth::Refusal::VERSION ? "version"
                                                          : "framing"
            );
        }
        disconnect_peer(p_peer);
        return;
    }
    if (!has_server_role()) {
        if (p_peer != SERVER_PEER
            || (decoded.flags & auth::HELLO_PROOF_CHALLENGE) == 0) {
            auth_complete_link(p_peer);
            return;
        }
        if (p_data.size() != auth::HELLO_HEADER_LEN + auth::CHALLENGE_LENGTH) {
            disconnect_peer(p_peer);
            return;
        }
        session_request_proof(p_peer, String::utf8(
            reinterpret_cast<const char *>(p_data.ptr())
                + auth::HELLO_HEADER_LEN, auth::CHALLENGE_LENGTH
        ));
        return;
    }
    const bool required = session_requires_proof();
    if (required || (decoded.flags & auth::HELLO_PROOF_CAPABLE) != 0) {
        PackedByteArray reply = auth::encode_client_hello(
            uint64_t(auth_app_tag), required ? auth::HELLO_PROOF_CHALLENGE : 0
        );
        if (required) {
            LinkHandshake &link = link_handshakes[p_peer];
            if (link.challenge.is_empty()) {
                link.challenge = session_mint_challenge();
            }
            if (link.challenge.is_empty()) {
                disconnect_peer(p_peer);
                return;
            }
            const CharString text = link.challenge.utf8();
            for (int at = 0; at < text.length(); ++at) {
                reply.push_back(uint8_t(text[at]));
            }
        }
        if (send_auth(p_peer, reply) != OK) {
            disconnect_peer(p_peer);
            return;
        }
    }
    auth_complete_link(p_peer);
}

session_decl::Book &NetwMultiplayer::declaration_book() {
    return declaration_slots;
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
            "A probe got no reply, because the Netw.configure_server_info "
            "handler returned %s. Edit the NetwServerInfo it is given and "
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
            "A probe from peer %d got a busy answer (%d this second, %d "
            "tracked).",
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
        !session_has_accepted(p_peer),
        Variant(),
        sys::SESSION,
        "NetwSession.bucket_of got peer %d, which has not joined "
        "this session.",
        int(p_peer)
    );
    Object *type = Object::cast_to<Object>(p_type);
    NETW_ERR_COND_V(
        type == nullptr,
        Variant(),
        sys::SESSION,
        "NetwSession.bucket_of needs a script to build the bucket "
        "from. Pass a script class that extends RefCounted."
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
        "NetwSession.bucket_of could not build the bucket. Its "
        "script must extend RefCounted."
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
            NETW_TRACE(sys::SESSION, "Role %d has no name.", p_role);
            return "";
    }
}

namespace {

bool is_session_core_state(int64_t p_state) {
    return p_state == SessionCore::STATE_OFFLINE
        || p_state == SessionCore::STATE_CONNECTING
        || p_state == SessionCore::STATE_ONLINE
        || p_state == SessionCore::STATE_DISCONNECTING;
}

} // namespace

void NetwMultiplayer::session_set_state(SessionState p_state) {
    NETW_ERR_COND(
        !is_session_core_state(p_state),
        sys::SESSION,
        "State %d cannot be set on a session. STATE_CREATING only reports a "
        "peer being built, so pass STATE_OFFLINE, STATE_CONNECTING, "
        "STATE_ONLINE or STATE_DISCONNECTING.",
        int(p_state)
    );
    session_core.set_state(SessionCore::State(p_state));
}

void NetwMultiplayer::session_set_role(Role p_role) {
    session_core.set_role(SessionCore::Role(p_role));
    sim_follow_server();
}

void NetwMultiplayer::session_set_desired_role(Role p_role) {
    session_core.set_desired_role(SessionCore::Role(p_role));
}

void NetwMultiplayer::session_transition(SessionState p_state) {
    NETW_ERR_COND(
        !is_session_core_state(p_state),
        sys::SESSION,
        "State %d cannot be set on a session. STATE_CREATING only reports a "
        "peer being built, so pass STATE_OFFLINE, STATE_CONNECTING, "
        "STATE_ONLINE or STATE_DISCONNECTING.",
        int(p_state)
    );
    session_core.transition(SessionCore::State(p_state));
}

void NetwMultiplayer::session_peer_assigned(bool p_live, int p_unique_id) {
    session_core.on_peer_assigned(p_live, p_unique_id);
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
        "Only the server can pause the session. Check multiplayer.is_server() "
        "before calling NetwSession.pause."
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
        "Only the server can unpause the session. Check "
        "multiplayer.is_server() before calling NetwSession.unpause."
    );
    session_broadcast_control(control_channels.unpause, PackedByteArray());
}

bool NetwMultiplayer::session_is_paused() const {
    return session_paused;
}

void NetwMultiplayer::session_notify_shutdown(const String &p_reason) {
    NETW_ERR_COND(
        !session_core.is_server_role(),
        sys::SESSION,
        "NetwMultiplayer.session_notify_shutdown runs only on the server."
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
        !is_host(),
        sys::SESSION,
        "NetwMultiplayer.peer_kick runs only on the server."
    );
    session_turn_peer_away(p_peer_id, p_reason);
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

namespace {

LocalVector<uint64_t> &sessions_tearing_down() {
    static LocalVector<uint64_t> sessions;
    return sessions;
}

void forget_torn_down_sessions() {
    LocalVector<uint64_t> &sessions = sessions_tearing_down();
    uint32_t at = 0;
    while (at < sessions.size()) {
        if (gd::object_registered(ObjectID(sessions[at]))) {
            ++at;
        } else {
            sessions.remove_at_unordered(at);
        }
    }
}

} // namespace

bool NetwMultiplayer::is_tearing_down(ObjectID p_session) {
    forget_torn_down_sessions();
    return sessions_tearing_down().has(uint64_t(p_session));
}

NetwMultiplayer::~NetwMultiplayer() {
    forget_torn_down_sessions();
    sessions_tearing_down().push_back(uint64_t(gd::instance_id(this)));
    tree_forget_session(this);
    if (session_paused) {
        session_set_tree_paused(false);
    }
    capture_close();
    clock_release();
    lagcomp_release();
    declaration_slots.release();
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
    NETW_STAGE(OTHER, "Session tick", colors::SESSION);
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
#if defined(NETW_PROFILING)
    if (NETW_PROFILE_CONNECTED()) {
        profile_book_bytes();
    }
#endif
}

#if defined(NETW_PROFILING)
void NetwMultiplayer::profile_book_bytes() const {
    NETW_ZONE_NC("Memory sample", colors::SESSION);
    NETW_PLOT(profile::names::MEMORY_STORE_ROWS, store_world.row_bytes());
    NETW_PLOT(profile::names::MEMORY_PEER_ROWS, store_world.peer_bytes());
    NETW_PLOT(profile::names::MEMORY_PEER_FLIGHTS, store_world.flight_bytes());
    int64_t transitions = 0;
    int64_t inputs = 0;
    int64_t journal = 0;
    int64_t slots = 0;
    prediction_engine.held_bytes(transitions, inputs, journal, slots);
    NETW_PLOT(profile::names::MEMORY_PREDICT_TRANSITIONS, transitions);
    NETW_PLOT(profile::names::MEMORY_PREDICT_INPUTS, inputs);
    NETW_PLOT(profile::names::MEMORY_PREDICT_JOURNAL, journal);
    NETW_PLOT(profile::names::MEMORY_PREDICT_SLOTS, slots);
    NETW_PLOT(
        profile::names::MEMORY_DISPLAY,
        display_book.is_valid() ? display_book->held_bytes() : int64_t(0)
    );
    NETW_PLOT(profile::names::MEMORY_BINDINGS, binding_book.held_bytes());
    NETW_PLOT(profile::names::MEMORY_SETS, set_book_bytes());
    NETW_PLOT(profile::names::MEMORY_ENTITIES, entity_bytes());
    const SyncCompat *compat = sync_adapter();
    NETW_PLOT(
        profile::names::MEMORY_COMPAT,
        compat != nullptr ? compat->held_bytes() : int64_t(0)
    );
}
#endif

#if defined(NETW_PROFILING) || defined(DEBUG_ENABLED)
int64_t NetwMultiplayer::set_book_bytes() const {
    return set_book.held_bytes([](const repl::PropertySet &p_set) {
        return held::vector(p_set.columns);
    });
}

int64_t NetwMultiplayer::entity_bytes() const {
    int64_t bytes = held::map(entity_book)
        + entity_slots.held_bytes([](const EntitySlot &p_slot) {
              const int64_t object = int64_t(sizeof(NetwEntity));
              int64_t owned = p_slot.live.is_valid() ? object : 0;
              owned += p_slot.retired.is_valid() ? object : 0;
              if (p_slot.record != nullptr) {
                  owned += p_slot.record->held_bytes();
              }
              return owned;
          });
    ReplicationCore *replication = get_replication_plane();
    if (replication != nullptr) {
        bytes += replication->get_spawn_pipeline()
                     ->get_spawn_book()
                     ->held_bytes();
    }
    return bytes;
}
#endif

Error NetwMultiplayer::NETW_API_VIRTUAL(poll)() {
    NETW_FRAME(profile::names::POLL_FRAME);
    NETW_STAGE(OTHER, "Network poll", colors::SESSION);
    Engine *engine = Engine::get_singleton();
    if (engine != nullptr && engine->is_editor_hint()) {
        return OK;
    }
    NETW_NETWORK_PROFILE(profiler_poll());
    embed_settle_on_first_poll();
    {
        NETW_STAGE(TRANSPORT, "Transport poll", colors::TRANSPORT);
        if (inner.is_null() && effective_peer.is_valid()) {
            effective_peer->poll();
        }
        peer_sweep_transport_state();
        session_watch_serverless_mesh();
        session_watch_late_server();
    }
    const double frame_delta
        = poll_delta(Time::get_singleton()->get_ticks_usec());
    session_poll_admissions(
        int64_t(Time::get_singleton()->get_ticks_msec())
    );
    connect_core.on_poll(frame_delta);
    const Error err = embed_poll_transport();
    session_poll_shutdown();
    session_poll_turned_away();
    SceneLoads::shared().poll(engine->get_process_frames());
    if (spawn::Pipeline *spawning = spawn_plane()) {
        spawning->retry_build_parked();
        spawning->sweep_pending_spawns();
    }
    scene_poll_loading_change();
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
    Object *seam = session_seam(StringName("_persist_tick"));
    if (seam != nullptr) {
        seam->call("_persist_tick", frame_delta);
    } else {
        persist_pump(frame_delta);
    }
    ReplicationCore *plane = (get_replication_plane());
    if (plane != nullptr) {
        plane->on_poll();
    }
    sink_verdict(display_pump(frame_delta), 0);
    NETW_HINTS(diag_poll());
    NETW_STAGE_FLUSH();
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
#if defined(DEBUG_ENABLED)
    if (p_peer.is_null()
        || Object::cast_to<link::ShapedPeer>(*p_peer) != nullptr
        || p_peer->is_class("OfflineMultiplayerPeer")) {
        return p_peer;
    }
    const Ref<NetwLinkConditions> shaping = session_settings.link_conditions;
    if (shaping.is_null()) {
        return p_peer;
    }
    if (peer_relabels(p_peer)) {
        if (!relabel_shaping_warned) {
            relabel_shaping_warned = true;
            NETW_WARN(
                sys::TRANSPORT,
                "%s can move the server, so the session link conditions are "
                "not applied to it. Simulate lag inside the transport "
                "instead.",
                String(p_peer->get_class())
            );
        }
        return p_peer;
    }
    Ref<link::ShapedPeer> shaped;
    shaped.instantiate();
    shaped->initialize(p_peer, shaping);
    return shaped;
#else
    return p_peer;
#endif
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
        "This peer was created for another session, so the assignment was "
        "ignored. Assign it to the session that created it."
    );
    NETW_ERR_COND(
        p_peer.is_valid()
            && p_peer->get_connection_status()
                == MultiplayerPeer::CONNECTION_DISCONNECTED,
        sys::TRANSPORT,
        "The assigned peer is disconnected, so the assignment was ignored. "
        "Assign the peer after create_server or create_client succeeds."
    );
    const bool live
        = p_peer.is_valid() && !p_peer->is_class("OfflineMultiplayerPeer");
    NETW_ERR_COND(
        config_applying,
        sys::SESSION,
        "The peer was assigned while the session applied its Netw.configure_* "
        "settings, so the assignment was ignored. Assign it outside the "
        "callbacks that react to those settings."
    );
    if (live) {
        if (apply_pending_config(false, "the peer assignment") != OK) {
            return;
        }
        session_apply_auth_config();
    }
    if (session_get_state() == SESSION_STATE_ONLINE) {
        session_note_exit(
            live ? enums::NetwSession::REPLACED
                 : enums::NetwSession::LOCAL_LEAVE,
            String()
        );
        session_teardown_online();
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
    relabel_follow(live ? p_peer : Ref<MultiplayerPeer>());
    session_push_desired_role();
    connect_core.on_peer_assigned(p_peer);
    session_peer_assigned(live, live ? p_peer->get_unique_id() : 0);
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
    return connect_core.target_find(p_transport, p_address);
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
        case ENDPOINT_PARAM_SOURCE:
            return endpoint_source(p_endpoint);
        default:
            return Variant();
    }
}

String NetwMultiplayer::endpoint_source(const RID &p_endpoint) const {
    const connect::TargetRow *held = connect_core.list().row(p_endpoint);
    if (held == nullptr) {
        return String();
    }
    LobbyDirectory *found
        = Object::cast_to<LobbyDirectory>(gd::object_of(held->source));
    return found == nullptr ? String() : found->get_display_name();
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
            if (connect_core.list().is_removable(p_endpoint)) {
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

void NetwMultiplayer::endpoint_set_browsing(bool p_enabled) {
    connect_core.set_browsing(p_enabled);
}

bool NetwMultiplayer::endpoint_is_browsing() const {
    return connect_core.is_browsing();
}

void NetwMultiplayer::endpoint_set_probe_interval(double p_seconds) {
    connect_core.set_probe_interval(p_seconds);
}

double NetwMultiplayer::endpoint_get_probe_interval() const {
    return connect_core.get_probe_interval();
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
    TransportMode p_mode,
    const String &p_address,
    const Dictionary &p_settings,
    const Callable &p_completed
) {
    return connect_core.create_peer(
        p_transport,
        int(p_mode),
        p_address,
        p_settings,
        p_completed
    );
}

void NetwMultiplayer::transport_cancel_peer_creation(const RID &p_ticket) {
    connect_core.cancel_peer_creation(p_ticket);
}

Ref<NetwPromise> NetwMultiplayer::transport_join(
    const RID &p_transport,
    const String &p_address,
    const Dictionary &p_settings
) {
    return connect_core.join(p_transport, p_address, p_settings);
}

Ref<NetwPromise> NetwMultiplayer::endpoint_join(
    const RID &p_endpoint,
    const Dictionary &p_settings
) {
    return connect_core.join_target(p_endpoint, p_settings);
}

Ref<NetwPromise> NetwMultiplayer::transport_host(
    const RID &p_transport,
    const Dictionary &p_settings
) {
    return connect_core.host(p_transport, p_settings);
}

void NetwMultiplayer::transport_cancel_attempt() {
    connect_core.cancel_attempt();
}

Error NetwMultiplayer::transport_set_defaults(
    const RID &p_transport,
    const Dictionary &p_settings
) {
    if (transport_held(p_transport) == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    return connect_core.set_defaults(p_transport, p_settings);
}

Dictionary NetwMultiplayer::transport_get_defaults(const RID &p_transport) {
    if (transport_held(p_transport) == nullptr) {
        return Dictionary();
    }
    return connect_core.defaults_of(p_transport);
}

void NetwMultiplayer::connection_attempt_moved(int64_t p_state) {
    if (connection_handle.is_valid()) {
        connection_handle->announce_attempt_moved(p_state);
    }
    session_publish_state();
}

void NetwMultiplayer::connection_emit_progressed(
    const String &p_message,
    double p_ratio
) {
    if (connection_handle.is_valid()) {
        connection_handle->announce_progressed(p_message, p_ratio);
    }
    notify(CONNECTION_PROGRESSED, p_message, p_ratio);
}

bool NetwMultiplayer::connection_emit_failed(
    Error p_error,
    const String &p_reason
) {
    bool heard = !observers[CONNECTION_FAILED].is_empty();
    if (connection_handle.is_valid()
        && connection_handle->announce_failed(int64_t(p_error), p_reason)) {
        heard = true;
    }
    notify(CONNECTION_FAILED, int64_t(p_error), p_reason);
    return heard;
}

void NetwMultiplayer::connection_emit_join_address_changed(
    const String &p_address
) {
    if (connection_handle.is_valid()) {
        connection_handle->announce_join_address_changed(p_address);
    }
    notify(CONNECTION_JOIN_ADDRESS_CHANGED, p_address);
}

void NetwMultiplayer::connection_emit_invited(const RID &p_endpoint) {
    if (connection_handle.is_valid()) {
        connection_handle->announce_invited(p_endpoint);
    }
    notify(CONNECTION_INVITED, p_endpoint);
}

RID NetwMultiplayer::transport_register(const Ref<Script> &p_type) {
    const Ref<Script> script
        = netw::script::model::declaring_script(p_type.ptr());
    if (script.is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "NetwMultiplayer.transport_register needs a script that extends "
            "NetwTransport."
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
        case TRANSPORT_PARAM_TIMEOUT_HINT:
            return facts.timeout_hint;
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
    connect_core.publish_targets(
        p_peer_class,
        ObjectID(),
        p_addresses,
        p_names,
        p_infos
    );
}

void NetwMultiplayer::endpoint_emit_added(const RID &p_endpoint) {
    if (connection_handle.is_valid()) {
        connection_handle->announce_endpoint_added(p_endpoint);
    }
    notify(ENDPOINT_ADDED, p_endpoint);
}

void NetwMultiplayer::endpoint_emit_removed(const RID &p_endpoint) {
    if (connection_handle.is_valid()) {
        connection_handle->announce_endpoint_removed(p_endpoint);
    }
    notify(ENDPOINT_REMOVED, p_endpoint);
}

void NetwMultiplayer::endpoint_emit_updated(const RID &p_endpoint) {
    if (connection_handle.is_valid()) {
        connection_handle->announce_endpoint_updated(p_endpoint);
    }
    notify(ENDPOINT_UPDATED, p_endpoint);
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

LocalVector<ObjectID> NetwMultiplayer::discovery_directories() const {
    LocalVector<ObjectID> found;
    for (uint32_t at = 0; at < services.size(); at++) {
        Object *service = gd::instance_from_id(services[at].service);
        if (connect::DirectoryTransport::is_directory(service)) {
            found.push_back(services[at].service);
        }
    }
    return found;
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
    Object::cast_to<LobbyDirectory>(p_directory)->watched_by(this);
    connect_core.watch_directory(p_directory);
}

void NetwMultiplayer::discovery_unwatch_directory(Object *p_directory) {
    if (p_directory == nullptr) {
        return;
    }
    LobbyDirectory *directory = Object::cast_to<LobbyDirectory>(p_directory);
    if (directory != nullptr) {
        directory->unwatched_by(this);
    }
    connect_core.forget_directory(gd::instance_id(p_directory));
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

Ref<NetwConnection> NetwMultiplayer::get_connection() {
    if (connection_handle.is_null()) {
        connection_handle.instantiate();
        connection_handle->bind_session(this);
    }
    return connection_handle;
}

Ref<NetwSession> NetwMultiplayer::get_session() {
    if (session_handle.is_null()) {
        session_handle.instantiate();
        session_handle->bind_session(this);
    }
    return session_handle;
}

Ref<NetwClock> NetwMultiplayer::get_clock() {
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
    const Error removed
        = inner->object_configuration_remove(p_object, p_configuration);
    if (p_object == nullptr && names_the_root_window(p_configuration)) {
        embed_dispose();
    }
    return removed;
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
        SERVER_PEER,
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
    NETW_ZONE_NC("Clock ping", colors::CLOCK);
    session::ClockPing ping;
    ping.origin = clock_wall_usec() & 0xFFFFFFFF;
    send_to(
        SERVER_PEER,
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
    if (!is_host()) {
        return;
    }
    session::ClockRate asked;
    if (!session::frame_read(p_payload, asked)) {
        NETW_TRACE(
            sys::CLOCK,
            "A clock handshake that did not decode fully was refused."
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
    if (p_sender != SERVER_PEER) {
        return;
    }
    session::ClockRate answered;
    if (!session::frame_read(p_payload, answered)) {
        NETW_ERROR(
            sys::CLOCK,
            "The server's clock reply could not be read, so this peer kept "
            "its own tickrate. Run the same build on both peers."
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
            notify(CLOCK_TICKRATE_MISMATCH, p_sender, server_tickrate);
        } else {
            NETW_WARN(
                sys::CLOCK,
                "This peer runs at tickrate %d and the server at %d. Set the "
                "same NetwClockConfig.tickrate on both.",
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
    if (!is_host() || !session::frame_read(p_payload, ping)) {
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
    if (p_sender != SERVER_PEER || !session::frame_read(p_payload, pong)) {
        return;
    }
    const uint32_t client_usec = uint32_t(pong.origin);
    const uint32_t server_tick = uint32_t(pong.tick);
    const double server_phase = double(pong.phase) / 255.0;
    const uint32_t elapsed
        = uint32_t((clock_wall_usec() & 0xFFFFFFFF) - client_usec);
    const double sample = double(elapsed) / 1'000'000.0;
    const Dictionary metrics
        = clock_engine()
              .handle_pong(sample, int(server_tick), server_phase, !is_host());
    notify(CLOCK_PONG_RECEIVED, metrics);
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

int NetwMultiplayer::declared_quantum() const {
    return MAX(1, int(Math::round(clock_engine().physics_factor())));
}

NetwMultiplayer::SessionState NetwMultiplayer::session_get_state() const {
    return SessionState(session_core.get_state());
}

NetwMultiplayer::SessionState NetwMultiplayer::
    session_get_computed_state() const {
    const SessionState own = session_get_state();
    if (own == SESSION_STATE_OFFLINE
        && connect_core.attempt_state() == connect::ATTEMPT_CREATING) {
        return SESSION_STATE_CREATING;
    }
    return own;
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

void NetwMultiplayer::structure_halt_enter() {
    structure_halted = true;
}

void NetwMultiplayer::structure_halt_leave() {
    structure_halted = false;
    halt_release();
}

bool NetwMultiplayer::is_structure_halted() const {
    return structure_halted;
}

void NetwMultiplayer::halt_park_join(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    for (HaltJoin &held : halt_joins) {
        if (held.sender == p_sender) {
            held.payload = p_payload;
            return;
        }
    }
    HaltJoin parked;
    parked.payload = p_payload;
    parked.sender = p_sender;
    halt_joins.push_back(parked);
}

void NetwMultiplayer::halt_forget_peer(int64_t p_peer) {
    for (int64_t at = int64_t(halt_joins.size()) - 1; at >= 0; --at) {
        if (halt_joins[uint32_t(at)].sender == p_peer) {
            halt_joins.remove_at(uint32_t(at));
        }
    }
    halt_leases.erase(p_peer);
    if (resync_awaited.erase(p_peer)) {
        transfer_settle_resync();
    }
}

void NetwMultiplayer::halt_release() {
    if (!is_host()) {
        halt_joins.clear();
        halt_leases.clear();
        return;
    }
    while (!halt_leases.is_empty() && !structure_halted) {
        const int64_t peer = halt_leases[0];
        halt_leases.remove_at(0);
        liveness_grant_lease(peer);
    }
    while (!halt_joins.is_empty() && !structure_halted) {
        const HaltJoin parked = halt_joins[0];
        halt_joins.remove_at(0);
        session_accept_join(parked.payload, parked.sender);
    }
}

int64_t NetwMultiplayer::structure_ops_outstanding() const {
    int64_t outstanding = 0;
    entity_slots.each([&outstanding](uint64_t, const EntitySlot &p_slot) {
        if (p_slot.owner.is_null()) {
            return;
        }
        Node *owner = Object::cast_to<Node>(gd::object_of(p_slot.owner));
        const Ref<NetwEntity> entity = NetwEntity::of(owner);
        if (entity.is_valid()) {
            outstanding += int64_t(entity->structure_ops_outstanding());
        }
    });
    return outstanding;
}

int64_t NetwMultiplayer::route_floor() const {
    const Ref<NetwLivenessCore> liveness = get_liveness_core();
    return liveness.is_valid() ? int64_t(liveness->route_floor()) : 0;
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

void NetwMultiplayer::auth_relay_authenticating(int64_t p_peer) {
    if (p_peer == SERVER_PEER) {
        connect_core.on_server_answered();
    }
    emit_signal(SIG_PEER_AUTHENTICATING, p_peer);
    notify(PEER_AUTHENTICATING, p_peer);
}

double NetwMultiplayer::poll_delta(int64_t p_now_usec) {
    const int64_t previous = last_poll_usec;
    if (previous == p_now_usec) {
        return 0.0;
    }
    last_poll_usec = p_now_usec;
    const double delta
        = previous <= 0 ? 0.0 : double(p_now_usec - previous) / 1000000.0;
    notify(SESSION_POLL_STARTED, delta);
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
        inner->disconnect(
            SIG_PEER_AUTHENTICATING,
            callable_mp(this, &NetwMultiplayer::auth_relay_authenticating)
        );
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
    inner->connect(
        SIG_PEER_AUTHENTICATING,
        callable_mp(this, &NetwMultiplayer::auth_relay_authenticating)
    );
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

Ref<NetwSessionConfig> NetwMultiplayer::session_get_config() const {
    Ref<NetwSessionConfig> snapshot;
    snapshot.instantiate();
    snapshot->set_app_id(session_settings.app_id);
    snapshot->set_desired_role(Role(session_settings.desired_role));
    if (session_settings.link_conditions.is_valid()) {
        const Ref<NetwLinkConditions> link
            = session_settings.link_conditions->duplicate();
        snapshot->set_link_conditions(link);
    }
    if (session_settings.server_info.is_valid()) {
        Ref<NetwServerInfo> info;
        info.instantiate();
        info->copy_values_from(**session_settings.server_info);
        snapshot->set_server_info(info);
    }
    snapshot->set_bandwidth_kbps(session_settings.bandwidth_kbps);
    snapshot->set_transfer_mode(session_settings.transfer_mode);
    snapshot->set_transfer_deadline_ticks(
        session_settings.transfer_deadline_ticks
    );
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
            "Netw.configure_session ran after the session started, so the "
            "running values are unchanged. Call it before startup and "
            "finish the chain without awaiting."
        );
        return ERR_ALREADY_IN_USE;
    }
    config_consumption[slot].consumed = true;
    if (p_from.is_valid()) {
        session_settings.app_id = p_from->get_app_id();
        session_settings.desired_role = p_from->get_desired_role();
        const Ref<NetwLinkConditions> link = p_from->get_link_conditions();
        if (link.is_valid()) {
            session_settings.link_conditions = link->duplicate();
        }
        const Ref<NetwServerInfo> info = p_from->get_server_info();
        if (info.is_valid()) {
            session_settings.server_info.instantiate();
            session_settings.server_info->copy_values_from(**info);
        }
        session_settings.bandwidth_kbps = p_from->get_bandwidth_kbps();
        session_settings.transfer_mode = p_from->get_transfer_mode();
        session_settings.transfer_deadline_ticks
            = p_from->get_transfer_deadline_ticks();
    }
    connect_core.bind_session(this);
    session_push_desired_role();
    session_apply_auth_config();
    return OK;
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
            "A join carried arguments, but no handler reads them."
        );
        return false;
    }
    if (r_request.schema_hash != join_schema_hash(handler, quantizers)) {
        NETW_TRACE(
            sys::SESSION,
            "A join's arguments do not match this server's handler."
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
        NETW_TRACE(sys::SESSION, "A join's arguments did not decode fully.");
        return false;
    }
    r_request.arg_values = values;
    return true;
}

bool NetwMultiplayer::session_resolve_inbound_join(
    JoinRequest &r_request,
    int64_t p_sender,
    session::AcceptFrame &r_accepted
) {
    if (!session_decode_join_args(r_request)) {
        NETW_WARN(
            sys::SESSION,
            "Peer %d was refused, because its join arguments do not match "
            "the Netw.configure_join handler. Pass matching arguments to "
            "Netw.prepare_join.",
            int(p_sender)
        );
        return false;
    }
    r_accepted.peer_id = p_sender;
    r_accepted.username = r_request.username;
    r_accepted.player_id = uint64_t(player_mint_id());
    return true;
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
        NETW_TRACE(sys::SESSION, "A join with no username sends nothing.");
        return;
    }
    session_submit_request(*request);
}

void NetwMultiplayer::session_submit_request(const JoinRequest &p_request) {
    NETW_ZONE_NC("netw::session_submit_join", colors::SESSION);
    JoinRequest sending = p_request;
    const int64_t mine = get_unique_id();
    if (is_host() && session_requires_proof() && !session_has_proof(mine)) {
        LinkHandshake &local = link_handshakes[mine];
        local.local_join = p_request;
        if (local.proof.result.is_null()) {
            session_request_proof(
                mine,
                local.challenge.is_empty() ? session_mint_challenge()
                                           : local.challenge
            );
        }
        return;
    }
    const int64_t proven = is_host() ? mine : int64_t(SERVER_PEER);
    if (session_has_proof(proven)) {
        sending.proof_bytes = gd::var_to_bytes(
            link_handshakes[proven].proof.result->get_result()
        );
        if (sending.proof_bytes.size() > 16383) {
            session_report_join_failure(
                ERR_INVALID_DATA, "The admission proof exceeds 16383 bytes."
            );
            return;
        }
    }
    session_encode_join_args(sending);
    sending.app_tag = auth_app_tag;
    sending.wire_identity = SessionCore::compute_wire_identity();
    sending.schema_identity = session_schema_identity();
    submitted_join = p_request;
    join_awaiting_admission = true;
    notify(SESSION_JOIN_SUBMITTED, sending.username, sending.arg_values);
    if (is_host()) {
        session_receive_join(sending.serialize(), get_unique_id());
        return;
    }
    send_to(
        SERVER_PEER,
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
    if (session_leaving.is_valid()) {
        return session_leaving;
    }
    if (!is_host() || get_bindings()->enrolled().is_empty()) {
        return session_close_peer();
    }
    Ref<NetwPromise> left;
    left.instantiate();
    session_leaving = left;
    const Ref<NetwPromise> drained = persist_flush_all();
    drained->when_settled(
        callable_mp(this, &NetwMultiplayer::session_leave_drained)
            .bind(drained, left)
    );
    return left;
}

void NetwMultiplayer::session_leave_drained(
    const Ref<NetwPromise> &p_drained,
    const Ref<NetwPromise> &p_left
) {
    session_leaving.unref();
    const Error drained = Error(int(p_drained->get_result()));
    if (drained != OK) {
        NETW_ERROR(
            sys::SESSION,
            "The session stayed online, because an entity could not be "
            "saved. Call NetwSession.leave again to retry, or close "
            "the peer to leave without saving."
        );
        p_left->resolve(int(drained));
        return;
    }
    const Ref<NetwPromise> closed = session_close_peer();
    closed->when_settled(
        callable_mp(this, &NetwMultiplayer::session_leave_closed)
            .bind(closed, p_left)
    );
}

void NetwMultiplayer::session_leave_closed(
    const Ref<NetwPromise> &p_closed,
    const Ref<NetwPromise> &p_left
) {
    p_left->resolve(p_closed->get_result());
}

Ref<NetwPromise> NetwMultiplayer::session_close_peer() {
    if (session_get_state() == SESSION_STATE_OFFLINE) {
        return NetwPromise::resolved(OK);
    }
    if (session_shutting_down.is_valid()) {
        return session_shutting_down;
    }
    NETW_TRACE(sys::SESSION, "Leaving the session.");
    NETW_INFO(sys::SESSION, "Disconnecting player.");
    session_note_exit(enums::NetwSession::LOCAL_LEAVE, String());
    const bool shuts_down = is_host() && inner.is_valid()
        && effective_peer.is_valid() && !gd::api_peer_ids(inner).is_empty();
    if (shuts_down) {
        session_notify_shutdown(String());
    }
    session_transition(SESSION_STATE_DISCONNECTING);
    if (!shuts_down) {
        return session_close_now();
    }
    effective_peer->set_refuse_new_connections(true);
    session_shutting_down.instantiate();
    session_shutdown_peer = effective_peer;
    session_shutdown_polls = 0;
    return session_shutting_down;
}

void NetwMultiplayer::session_poll_shutdown() {
    if (session_shutting_down.is_null()) {
        return;
    }
    session_shutdown_polls += 1;
    const bool same_peer = effective_peer.is_valid()
        && effective_peer == session_shutdown_peer && inner.is_valid();
    const PackedInt32Array clients
        = same_peer ? gd::api_peer_ids(inner) : PackedInt32Array();
    if (session_shutdown_polls == POLLS_BEFORE_TURNED_AWAY_PEER_DROPS) {
        for (int at = 0; at < clients.size(); ++at) {
            effective_peer->disconnect_peer(clients[at], false);
        }
    }
    const bool waiting = !clients.is_empty()
        || session_shutdown_polls <= POLLS_BEFORE_TURNED_AWAY_PEER_DROPS;
    if (same_peer && waiting
        && session_shutdown_polls < POLLS_BEFORE_SHUTDOWN_CLOSES) {
        return;
    }
    const Ref<NetwPromise> left = session_shutting_down;
    session_shutting_down.unref();
    session_shutdown_peer.unref();
    if (!same_peer) {
        session_settle_leave(left);
        return;
    }
    const Ref<NetwPromise> closed = session_close_now();
    closed->when_settled(
        callable_mp(this, &NetwMultiplayer::session_leave_closed)
            .bind(closed, left)
    );
}

Ref<NetwPromise> NetwMultiplayer::session_close_now() {
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
    resubmit_join.reset();
    submitted_join.reset();
    join_awaiting_admission = false;
    const Callable on_authority = callable_mp(
        this,
        &NetwMultiplayer::session_resubmit_join_on_authority_peer
    );
    if (is_connected(StringName("peer_connected"), on_authority)) {
        disconnect(StringName("peer_connected"), on_authority);
    }
}

void NetwMultiplayer::session_submit_prepared_join() {
    if (!prepared_join.has_value()) {
        return;
    }
    const JoinRequest request = *prepared_join;
    prepared_join.reset();
    session_submit_request(request);
    if (is_host() || NETW_API_VIRTUAL(get_peer_ids)().has(SERVER_PEER)) {
        return;
    }
    NETW_TRACE(
        sys::SESSION,
        "The join waits for one resend, because the server is not seen yet."
    );
    resubmit_join = request;
    const Callable on_authority = callable_mp(
        this,
        &NetwMultiplayer::session_resubmit_join_on_authority_peer
    );
    if (!is_connected(StringName("peer_connected"), on_authority)) {
        connect(StringName("peer_connected"), on_authority);
    }
}

void NetwMultiplayer::session_resubmit_join_on_authority_peer(int64_t p_peer) {
    if (!resubmit_join.has_value() || p_peer != SERVER_PEER) {
        return;
    }
    const Callable on_authority = callable_mp(
        this,
        &NetwMultiplayer::session_resubmit_join_on_authority_peer
    );
    if (is_connected(StringName("peer_connected"), on_authority)) {
        disconnect(StringName("peer_connected"), on_authority);
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
        NETW_ERROR(
            sys::SESSION,
            "Netw.prepare_join needs a username, and it was empty."
        );
        return NetwPromise::resolved(ERR_INVALID_PARAMETER);
    }

    auth_arm();
    prepared_join = connect::request_of(p_username, p_args);
    return NetwPromise::resolved(OK);
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
        "Connecting to the signal '%s' failed with error %d, so its "
        "callback will not run.",
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
    scene_drop_loading_change();
    scene_clear_pending_facets();
    store_world.arrays.clear_session();
    display_clear_declarations();
    sync_encode_armed = false;
    sync_encode_stock = PackedByteArray();
    sync_decoder = Callable();
}

void NetwMultiplayer::clear_session_state() {
    turned_away_peers.clear();
    display_last_timing.reset();
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

void NetwMultiplayer::clear_roster() {
    session_clear_roster();
    player_clear();
}

int64_t NetwMultiplayer::register_rewind(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return -1;
    }
    const int64_t slot
        = lagcomp_core.timeline_register(p_entity, LagBook::DEFAULT_WINDOW);
    store_bind_entity(p_entity);
    return slot;
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

Ref<NetwMultiplayer> NetwMultiplayer::create(
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
                "The script for a new NetwMultiplayer does not extend it. "
                "Use a script that starts with 'extends NetwMultiplayer'."
            );
            return Ref<NetwMultiplayer>();
        }
        made = Ref<NetwMultiplayer>(
            Object::cast_to<NetwMultiplayer>(selected->call("new"))
        );
    } else {
        made.instantiate();
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
    MultiplayerAPI::set_default_interface(installed_default_interface());
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

void NetwMultiplayer::embed_settle_on_first_poll() {
    if (embed_phase_value != EMBED_PHASE_DECLARING
        || session_root() == nullptr) {
        return;
    }
    embed_settle();
}

void NetwMultiplayer::peer_forget(int64_t p_peer) {
    const Ref<NetwPlayer> leaving = player_admitted_of(p_peer);
    if (PlayerRow *found = player_row(p_peer)) {
        found->admitted = false;
        found->incarnation = 0;
    }
    session_forget_peer(p_peer);
    player_publish_left(leaving);
    player_forget(p_peer);
    attribution.forget_peer(p_peer);
}

Ref<NetwPlayer> NetwMultiplayer::peer_get_player(int64_t p_peer) {
    if (!session_has_accepted(p_peer)) {
        return Ref<NetwPlayer>();
    }
    return player_of(p_peer);
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
            "%s names '%s', which is not a script. Point it at a script "
            "that extends NetwMultiplayer.",
            LAW_EXTENSION_ENV,
            path
        );
    }
    if (!is_extension_script(named)) {
        NETW_ERR_V(
            resolved,
            sys::SESSION,
            "%s names '%s', which does not extend NetwMultiplayer. Point it "
            "at a script that does.",
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
    if (p_sender != SERVER_PEER || !session::frame_read(p_payload, denied)) {
        return;
    }
    lagcomp_effect_discard(denied.key);
}

void NetwMultiplayer::handle_confirm(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    session::ConfirmKey confirmed;
    if (p_sender != SERVER_PEER || !session::frame_read(p_payload, confirmed)) {
        return;
    }
    lagcomp_effect_adopt(confirmed.key);
}

bool NetwMultiplayer::is_coroutine(const Variant &p_value) {
    if (p_value.get_type() != Variant::OBJECT) {
        return false;
    }
    Object *object = p_value;
    return object != nullptr
        && object->get_class() == String("GDScriptFunctionState");
}

bool NetwMultiplayer::is_waiting(const Variant &p_value) {
    return is_coroutine(p_value)
        || Object::cast_to<NetwPromise>(gd::live_object(p_value)) != nullptr;
}

void NetwMultiplayer::free_when_returned(const Variant &p_waiting) {
    Object *waiting = gd::live_object(p_waiting);
    if (NetwPromise *promise = Object::cast_to<NetwPromise>(waiting)) {
        promise->then(callable_mp_static(&NetwMultiplayer::free_returned)
                          .bind(p_waiting));
        return;
    }
    if (is_coroutine(p_waiting)) {
        waiting->connect(
            StringName("completed"),
            callable_mp_static(&NetwMultiplayer::free_returned)
                .bind(p_waiting),
            Object::CONNECT_ONE_SHOT
        );
    }
}

void NetwMultiplayer::free_returned(const Variant &p_value, const Variant &) {
    Node *node = Object::cast_to<Node>(gd::live_object(p_value));
    if (node == nullptr || node->is_queued_for_deletion()) {
        return;
    }
    if (node->is_inside_tree()) {
        node->queue_free();
        return;
    }
    if (Node *parent = node->get_parent()) {
        parent->remove_child(node);
    }
    memdelete(node);
}

void NetwMultiplayer::promise_refuse(
    const Ref<NetwPromise> &p_promise,
    Error p_code,
    const String &p_detail
) {
    p_promise->reject(p_code, p_detail);
    p_promise->mark_failure_observed();
}

void NetwMultiplayer::spawn_async_returned(
    const Variant &p_value,
    const Variant &,
    uint64_t p_plane,
    int64_t p_serial
) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(
        gd::instance_from_id(ObjectID(p_plane))
    );
    spawn::Pipeline *pipeline
        = plane != nullptr ? plane->spawn_plane() : nullptr;
    if (pipeline == nullptr
        || !pipeline->spawn_async_returned(p_value, p_serial)) {
        free_returned(p_value, Variant());
    }
}

void NetwMultiplayer::spawn_async_failed(
    int64_t p_code,
    const String &p_detail,
    const Variant &,
    uint64_t p_plane,
    int64_t p_serial
) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(
        gd::instance_from_id(ObjectID(p_plane))
    );
    if (spawn::Pipeline *pipeline
        = plane != nullptr ? plane->spawn_plane() : nullptr) {
        pipeline->spawn_async_failed(p_code, p_detail, p_serial);
    }
}

void NetwMultiplayer::spawn_build_returned(
    const Variant &p_value,
    const Variant &,
    uint64_t p_plane,
    int64_t p_route,
    int64_t p_serial
) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(
        gd::instance_from_id(ObjectID(p_plane))
    );
    spawn::Pipeline *pipeline
        = plane != nullptr ? plane->spawn_plane() : nullptr;
    if (pipeline == nullptr
        || !pipeline->build_returned(p_value, p_route, p_serial)) {
        free_returned(p_value, Variant());
    }
}

void NetwMultiplayer::spawn_build_failed(
    int64_t,
    const String &p_detail,
    const Variant &,
    uint64_t p_plane,
    int64_t p_route,
    int64_t p_serial
) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(
        gd::instance_from_id(ObjectID(p_plane))
    );
    if (spawn::Pipeline *pipeline
        = plane != nullptr ? plane->spawn_plane() : nullptr) {
        pipeline->build_failed(p_detail, p_route, p_serial);
    }
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
            "The override of %s returned %s, so the default ran instead. "
            "Return what its documentation asks for.",
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
