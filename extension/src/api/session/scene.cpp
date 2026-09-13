#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/view/host_scene_view.hpp"
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
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
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

Callable &host_view_factory() {
    static Callable factory;
    return factory;
}

const char *SIG_ENTITY_LIVE = "entity_live";
const char *SIG_PARTICIPANT_JOINED = "participant_joined";
const char *SIG_PARTICIPANT_LOCAL_JOINED = "participant_local_joined";
const char *SIG_PHYSICS_FRAME = "physics_frame";
const char *SIG_SCENE_ACTIVATED = "scene_activated";
const char *SIG_SCENE_DESPAWNED = "scene_despawned";
const char *SIG_SCENE_ENTITY_MOVED = "scene_entity_moved";
const char *SIG_SCENE_LIVE = "scene_live";
const char *SIG_SCENE_LOCAL_CHANGED = "scene_local_changed";
const char *SIG_SCENE_SPAWNED = "scene_spawned";
const char *SIG_SCENE_STARTUP_SPAWNED = "scene_startup_spawned";
const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_RECLAIMED = "session_reclaimed";
const char *SIG_TREE_SCENE_CHANGED = "scene_changed";

bool running_under_gdunit() {
    static const bool answer = []() -> bool {
#if defined(NETW_MODULE)
        const CoreBind::Engine *engine = CoreBind::Engine::get_singleton();
#else
        const Engine *engine = Engine::get_singleton();
#endif
        if (engine != nullptr && engine->has_meta(StringName("GdUnitRunner"))) {
            return true;
        }
        const PackedStringArray args = gd::cmdline_args();
        for (int at = 0; at < args.size(); ++at) {
            if (String(args[at]).contains("GdUnit")) {
                return true;
            }
        }
        return false;
    }();
    return answer;
}

} // namespace

Ref<NetwParticipant> NetwMultiplayer::scene_requester_participant(
    Node *p_requester
) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_requester);
    if (entity.is_valid()) {
        const Ref<NetwParticipant> owner
            = participant_of(entity->get_peer_id());
        if (owner.is_valid()) {
            return owner;
        }
    }
    return participant_admitted_local();
}

RID NetwMultiplayer::scene_of(const RID &p_entity) const {
    RID walker = p_entity;
    while (walker.is_valid()) {
        NetwEntityRecord *const *record
            = wrapper_records.getptr(walker.get_id());
        if (record != nullptr && (*record)->get_declares_scene()) {
            return walker;
        }
        walker = entity_parent_of(walker);
    }
    return RID();
}

Ref<NetwSceneHandle> NetwMultiplayer::scene_handle_of(const RID &p_entity) {
    NetwEntityRecord *const *record = wrapper_records.getptr(p_entity.get_id());
    if (record == nullptr) {
        return Ref<NetwSceneHandle>();
    }
    return (*record)->part(
        NetwEntityRecord::PART_SCENE,
        entity_get_view(p_entity).ptr()
    );
}

RID NetwMultiplayer::scene_report_entity_edge(
    const RID &p_subject,
    bool p_present,
    bool p_is_player
) {
    const RID scene = scene_of(p_subject);
    if (!scene.is_valid() || scene == p_subject) {
        return RID();
    }
    scene_core
        ->dispatch(scene, NetwSceneCore::EVENT_ENTITY, p_present, p_subject);
    if (p_is_player) {
        scene_core->dispatch(
            scene,
            NetwSceneCore::EVENT_PLAYER,
            p_present,
            p_subject
        );
        const Ref<NetwSceneHandle> view = scene_handle_of(scene);
        if (view.is_valid()) {
            view->announce_player(entity_get_view(p_subject), p_present);
        }
    }
    scene_participant_display_invalidate();
    return scene;
}

void NetwMultiplayer::scene_publish_live(
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

bool NetwMultiplayer::scene_request_flooded(int peer, int64_t now_msec) {
    if (!scene_request_window.exceeded(peer, now_msec)) {
        return false;
    }
    count_verdict(ERR_BUSY, 0);
    if (claim_verdict_warning(ERR_BUSY, 0)) {
        NETW_WARN(sys::SCENE, "peer %d exceeded the scene request rate", peer);
    }
    return true;
}

Array NetwMultiplayer::scene_request_frame_row(
    const PackedByteArray &p_payload,
    int p_sender,
    int64_t p_now_msec
) {
    if (!is_server() || scene_request_flooded(p_sender, p_now_msec)) {
        return Array();
    }
    session::SceneRequest frame;
    if (!session::frame_read(p_payload, frame)) {
        return Array();
    }
    Array row;
    row.push_back(int64_t(frame.request_id));
    row.push_back(frame.path);
    row.push_back(frame.scope);
    return row;
}

RID NetwMultiplayer::scene_released_seat(
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
    session::SceneReleased frame;
    if (!session::frame_read(p_payload, frame)) {
        return RID();
    }
    const int64_t route = scene_route_of(seat);
    return route > 0 && route == frame.route ? seat : RID();
}

int NetwMultiplayer::scene_destination_kind(const Variant &p_destination) {
    return NetwSceneCore::destination_kind(p_destination);
}

Ref<PackedScene> NetwMultiplayer::scene_packed_at(const String &p_reference) {
    return NetwSceneCore::packed_at(p_reference);
}

Ref<Script> NetwMultiplayer::scene_packed_root_script(
    const Ref<PackedScene> &p_packed
) {
    return NetwSceneCore::packed_root_script(p_packed);
}

Ref<Script> NetwMultiplayer::scene_root_script_at(const String &p_reference) {
    return NetwSceneCore::scene_root_script_at(p_reference);
}

String NetwMultiplayer::scene_resolve_requested_path(
    const String &p_reference
) {
    return NetwSceneCore::resolve_requested_path(p_reference);
}

int NetwMultiplayer::scene_move_verdict(
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

StringName NetwMultiplayer::scene_container_meta() {
    return StringName("_netw_scene_container");
}

bool NetwMultiplayer::scene_root_is_isolated(Node *p_root) {
    if (p_root == nullptr) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_root);
    if (entity.is_valid()
        && entity->get_scene_isolation()
            == int64_t(NetwSceneCore::ISOLATION_OWN_WORLD)) {
        return true;
    }
    const SceneDecl decl = netw::script::model::get_scene_decl(
        Ref<Script>(Object::cast_to<Script>(p_root->get_script()))
    );
    return decl.declared
        && decl.isolation == int64_t(NetwSceneCore::ISOLATION_OWN_WORLD);
}

Node *NetwMultiplayer::scene_wrap_world(Node *p_root) {
    if (p_root == nullptr || !scene_root_is_isolated(p_root)) {
        return p_root;
    }
    if (scene_world_of(p_root) != nullptr) {
        return p_root->get_parent();
    }
    SubViewport *world = memnew(SubViewport);
    world->set_use_own_world_3d(true);
    world->set_update_mode(SubViewport::UPDATE_DISABLED);
    world->set_name(String(p_root->get_name()) + String("World"));
    world->set_meta(scene_container_meta(), true);
    world->add_child(p_root);
    NETW_TRACE(
        sys::SCENE,
        "'%s' owns its world, so a viewport carries it",
        p_root->get_name()
    );
    return world;
}

SubViewport *NetwMultiplayer::scene_world_of(Node *p_root) {
    Node *parent = p_root != nullptr ? p_root->get_parent() : nullptr;
    if (parent == nullptr || !parent->has_meta(scene_container_meta())) {
        return nullptr;
    }
    return Object::cast_to<SubViewport>(parent);
}

Node *NetwMultiplayer::scene_outer_of(Node *p_root) {
    SubViewport *world = scene_world_of(p_root);
    return world != nullptr ? static_cast<Node *>(world) : p_root;
}

Node *NetwMultiplayer::scene_inner_of(Node *p_node) {
    if (p_node == nullptr || !p_node->has_meta(scene_container_meta())
        || p_node->get_child_count() == 0) {
        return p_node;
    }
    return p_node->get_child(0);
}

StringName NetwMultiplayer::scene_packed_stem(
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

Node *NetwMultiplayer::scene_container(const StringName &p_stem) const {
    return wrapper_owner(scene_named(p_stem));
}

Node *NetwMultiplayer::scene_existing_destination(
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

Node *NetwMultiplayer::scene_spawn_node(
    const Variant &p_data,
    int p_isolation
) {
    NETW_ZONE_NC("session scene constructor", colors::SCENE);
    Node *level = nullptr;
    if (p_data.get_type() == Variant::STRING) {
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
    } else if (p_data.get_type() != Variant::NIL) {
        NETW_ERROR(sys::SCENE, "a scene spawn carried neither a path nor null");
        return nullptr;
    }
    if (level == nullptr) {
        NETW_ERR_V(nullptr, sys::SCENE, "a scene spawn produced no root node");
    }

    const bool owns_its_world
        = NetwSceneCore::isolation_owns_world(p_isolation);
    const Ref<NetwEntity> seat = NetwEntity::ensure(level);
    if (seat.is_valid()) {
        seat->set_declares_scene(true);
        seat->set_scene_label(level->get_name());
        seat->set_scene_isolation(
            owns_its_world ? int64_t(NetwSceneCore::ISOLATION_OWN_WORLD)
                           : int64_t(NetwSceneCore::ISOLATION_NONE)
        );
    }
    return level;
}

StringName NetwMultiplayer::scene_constructor_id() {
    return StringName("__netw_scene__");
}

bool NetwMultiplayer::scene_owns_its_world(const RID &p_scene) const {
    const Ref<NetwEntity> record = entity_get_view(p_scene);
    return record.is_valid()
        && record->get_scene_isolation()
        == int64_t(NetwSceneCore::ISOLATION_OWN_WORLD);
}

bool NetwMultiplayer::scene_hosts_isolated_world() const {
    const Array live = scene_core->live_scenes();
    for (int at = 0; at < live.size(); ++at) {
        const RID scene = live[at];
        if (scene_world_of(wrapper_owner(scene)) != nullptr) {
            return true;
        }
    }
    return false;
}

void NetwMultiplayer::scene_register_constructor() {
    if (scene_constructor_registered) {
        return;
    }
    Array arg_types;
    arg_types.push_back(int(Variant::NIL));
    arg_types.push_back(int(Variant::INT));
    spawn_register_constructor(
        scene_constructor_id(),
        callable_mp(this, &NetwMultiplayer::scene_spawn_node),
        arg_types
    );
    scene_constructor_registered = true;
}

void NetwMultiplayer::scene_root_online(Node *p_root) {
    if (!scene_remember(p_root)) {
        return;
    }
    const RID seat = entity_of(p_root);
    scene_open_admission(p_root);
    scene_drain_parked_seats(seat);
    scene_ensure_host_view();
    const Ref<NetwEntity> record = NetwEntity::of(p_root);
    if (record.is_valid() && p_root != nullptr) {
        scene_publish_live(
            record->get_route(),
            seat,
            String(p_root->get_name())
        );
    }
    scene_settle_sync_local();
    scene_settle_refresh();
}

void NetwMultiplayer::scene_root_offline(Object *p_root) {
    scene_close_admission(p_root);
    scene_forget(p_root);
    scene_retire_world(Object::cast_to<Node>(p_root));
}

void NetwMultiplayer::scene_retire_world(Node *p_root) {
    SubViewport *world = scene_world_of(p_root);
    if (world == nullptr || world->is_queued_for_deletion()) {
        return;
    }
    world->queue_free();
}

void NetwMultiplayer::scene_set_host_view_factory(const Callable &p_factory) {
    host_view_factory() = p_factory;
}

Callable NetwMultiplayer::scene_host_view_factory() {
    return host_view_factory();
}

ParticipantView *NetwMultiplayer::scene_placed_view(Node *p_root) {
    if (p_root == nullptr) {
        return nullptr;
    }
    for (int at = 0; at < p_root->get_child_count(); ++at) {
        ParticipantView *view
            = Object::cast_to<ParticipantView>(p_root->get_child(at));
        if (view != nullptr) {
            return view;
        }
    }
    return nullptr;
}

void NetwMultiplayer::scene_ensure_host_view() {
    if (!presents_as_listen_host()) {
        return;
    }
    Node *root = session_root();
    if (root == nullptr || !scene_hosts_isolated_world()) {
        return;
    }
    if (Object::cast_to<Node>(gd::object_of(scene_host_view_id)) != nullptr) {
        return;
    }
    const Callable factory = host_view_factory();
    Node *view = nullptr;
    if (factory.is_valid()) {
        view = Object::cast_to<Node>(gd::live_object(factory.call(root)));
    } else if (scene_placed_view(root) == nullptr) {
        view = memnew(HostSceneView);
    }
    if (view == nullptr) {
        return;
    }
    view->set_name(StringName("HostSceneView"));
    scene_host_view_id = view->get_instance_id();
    root->add_child(view);
}

void NetwMultiplayer::scene_release_host_view() {
    Node *view = Object::cast_to<Node>(gd::object_of(scene_host_view_id));
    scene_host_view_id = ObjectID();
    if (view == nullptr) {
        return;
    }
    if (view->get_parent() != nullptr) {
        view->get_parent()->remove_child(view);
    }
    memdelete(view);
}

Node *NetwMultiplayer::scene_containing(Node *p_node) {
    Node *walk = p_node;
    while (walk != nullptr) {
        const RID owned = entity_of(walk);
        if (scene_core->is_live(owned) && wrapper_owner(owned) == walk) {
            return walk;
        }
        walk = walk->get_parent();
    }
    return nullptr;
}

void NetwMultiplayer::scene_settle_refresh() {
    session_defer(
        callable_mp(this, &NetwMultiplayer::scene_refresh_current),
        StringName("scene-refresh-current")
    );
}

void NetwMultiplayer::scene_settle_sync_local() {
    session_defer(
        callable_mp(this, &NetwMultiplayer::scene_sync_local_participant),
        StringName("scene-sync-local")
    );
}

void NetwMultiplayer::scene_bind_local_participant(
    const Ref<NetwParticipant> &p_participant
) {
    if (scene_local_participant.ptr() == p_participant.ptr()) {
        scene_settle_sync_local();
        return;
    }
    scene_local_participant = p_participant;
    scene_settle_sync_local();
    scene_settle_refresh();
}

void NetwMultiplayer::scene_sync_local_participant() {
    if (scene_local_participant.is_null()) {
        return;
    }
    scene_seat_sync(scene_local_participant->get_peer_id());
}

void NetwMultiplayer::scene_refresh_current() {
    scene_core->set_current_scene(scene_resolve_current());
}

RID NetwMultiplayer::scene_resolve_current() const {
    const bool presents = session_get_role() != ROLE_DEDICATED_SERVER;
    const RID seat = scene_local_participant.is_valid()
        ? participant_seat(scene_local_participant->get_peer_id())
        : RID();
    return scene_core->resolve_current(presents, seat);
}

void NetwMultiplayer::scene_on_session_reclaimed() {
    const TypedArray<Node> live = scene_live_nodes();
    for (int at = 0; at < live.size(); ++at) {
        Node *scene_node = Object::cast_to<Node>(live[at]);
        if (scene_node == nullptr) {
            continue;
        }
        if (scene_node->get_parent() != nullptr) {
            scene_node->get_parent()->remove_child(scene_node);
        }
        memdelete(scene_node);
    }
    scene_core->request_abandon(ERR_UNAVAILABLE);
    scene_core->clear();
    scene_release_host_view();
    if (scene_local_participant.is_valid()) {
        participant_seat_move(scene_local_participant->get_peer_id(), RID());
    }
    scene_local_participant = Ref<NetwParticipant>();
    scene_core->set_current_scene(RID());
}

void NetwMultiplayer::scene_install() {
    scene_register_constructor();
    connect_once(
        Signal(this, SIG_PARTICIPANT_LOCAL_JOINED),
        callable_mp(this, &NetwMultiplayer::scene_bind_local_participant)
    );
    connect_once(
        Signal(this, SIG_SCENE_LOCAL_CHANGED),
        callable_mp(this, &NetwMultiplayer::scene_refresh_current).unbind(2)
    );
    connect_once(
        Signal(this, SIG_SESSION_ENTERED),
        callable_mp(this, &NetwMultiplayer::scene_on_session_entered)
    );
    connect_once(
        Signal(this, SIG_SESSION_RECLAIMED),
        callable_mp(this, &NetwMultiplayer::scene_on_session_reclaimed)
    );
    connect_once(
        Signal(this, SIG_ENTITY_LIVE),
        callable_mp(this, &NetwMultiplayer::scene_on_entity_live)
    );
    SceneTree *loop = gd::scene_tree();
    if (loop == nullptr) {
        return;
    }
    connect_once(
        Signal(loop, SIG_TREE_SCENE_CHANGED),
        callable_mp(this, &NetwMultiplayer::scene_on_native_change)
    );
}

void NetwMultiplayer::scene_dispose() {
    scene_core->request_abandon(ERR_UNAVAILABLE);
    scene_carry_sweep();
    spawn_carry_sweep();
    disconnect_once(
        Signal(this, SIG_PARTICIPANT_LOCAL_JOINED),
        callable_mp(this, &NetwMultiplayer::scene_bind_local_participant)
    );
    disconnect_once(
        Signal(this, SIG_SCENE_LOCAL_CHANGED),
        callable_mp(this, &NetwMultiplayer::scene_refresh_current).unbind(2)
    );
    disconnect_once(
        Signal(this, SIG_SESSION_ENTERED),
        callable_mp(this, &NetwMultiplayer::scene_on_session_entered)
    );
    disconnect_once(
        Signal(this, SIG_SESSION_RECLAIMED),
        callable_mp(this, &NetwMultiplayer::scene_on_session_reclaimed)
    );
    SceneTree *loop = gd::scene_tree();
    if (loop != nullptr) {
        disconnect_once(
            Signal(loop, SIG_TREE_SCENE_CHANGED),
            callable_mp(this, &NetwMultiplayer::scene_on_native_change)
        );
    }
    const Array retiring = scene_core->retiring_scenes();
    for (int at = 0; at < retiring.size(); ++at) {
        const RID scene = retiring[at];
        Node *retired = wrapper_owner(scene);
        if (retired != nullptr) {
            retired->queue_free();
        }
    }
    scene_core->clear();
}

void NetwMultiplayer::scene_on_session_entered() {
    if (is_server()) {
        callable_mp(this, &NetwMultiplayer::scene_announce_startup)
            .call_deferred();
    }
    callable_mp(this, &NetwMultiplayer::scene_ensure_host_view).call_deferred();
}

void NetwMultiplayer::scene_announce_startup() {
    emit_signal(SIG_SCENE_STARTUP_SPAWNED);
}

void NetwMultiplayer::scene_on_native_change() {
    if (session_get_state() != SESSION_STATE_ONLINE
        || !session_root_last_live.is_valid()
        || gd::object_of(session_root_last_live) != nullptr) {
        return;
    }
    NETW_ERROR(
        sys::SCENE,
        "SceneTree.change_scene_to_* freed the scene this session was mounted "
        "in, so its root and every world under it are gone while the peers it "
        "was talking to are not. Mount the session outside the scene it "
        "presents, and change scenes with Netw.change_scene_to_file()."
    );
}

Node *NetwMultiplayer::scene_spawn(
    const Variant &p_data,
    SceneIsolation p_isolation
) {
    NETW_ZONE_NC("session scene spawn", colors::SCENE);
    scene_core->spawn_note(StringName());
    Array args;
    args.push_back(p_data);
    args.push_back(p_isolation);
    Node *mounted = wrapper_owner(
        spawn_registered(scene_constructor_id(), args, nullptr)
    );
    if (mounted == nullptr) {
        return nullptr;
    }
    Node *parent = session_root();
    if (parent != nullptr) {
        parent->add_child(scene_outer_of(mounted));
    }
    return mounted;
}

Node *NetwMultiplayer::scene_activate(const Variant &p_destination) {
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
                active->set_process_mode(Node::PROCESS_MODE_INHERIT);
                return announce_scene_activated(active);
            }
            const SceneIsolation isolation = SceneIsolation(
                scene_decl_of(scene_packed_root_script(packed)).isolation
            );
            return announce_scene_activated(
                scene_spawn(packed->get_path(), isolation)
            );
        }
        default:
            break;
    }
    NETW_ERR_V(
        nullptr,
        sys::SCENE,
        "scene activation expects a file-backed PackedScene: a label names a "
        "live scene, not a recipe, and cannot be activated"
    );
}

Node *NetwMultiplayer::announce_scene_activated(Node *p_active) {
    if (p_active != nullptr) {
        emit_signal(SIG_SCENE_ACTIVATED, p_active);
        scene_participant_display_invalidate();
    }
    return p_active;
}

Node *NetwMultiplayer::scene_resolve_destination(const Variant &p_destination) {
    Node *existing = scene_existing_destination(p_destination);
    return existing != nullptr ? existing : scene_activate(p_destination);
}

TypedArray<NetwEntity> NetwMultiplayer::scene_players_in(Node *p_container) {
    return p_container != nullptr ? scene_get_players(entity_of(p_container))
                                  : TypedArray<NetwEntity>();
}

Ref<NetwSceneHandle> NetwMultiplayer::scene_handle_for(Node *p_container) {
    const Ref<NetwEntity> record = NetwEntity::of(p_container);
    return record.is_valid() ? record->get_scene() : Ref<NetwSceneHandle>();
}

double NetwMultiplayer::scene_request_deadline() {
    return 10.0;
}

Ref<NetwPromise> NetwMultiplayer::scene_move_entity_to(
    const Ref<NetwEntity> &p_mover,
    Node *p_target
) {
    if (p_mover.is_null()) {
        NETW_TRACE(sys::SCENE, "scene_move: the mover is unreachable");
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("scene_move: the mover is unreachable")
        );
    }
    Ref<NetwReparentOpts> opts;
    opts.instantiate();
    opts->set_reason(scene_move_reason());
    return scene_move_entity(
        p_mover->get_rid_handle(),
        entity_of(p_target),
        opts
    );
}

Ref<NetwPromise> NetwMultiplayer::scene_replace_sources(
    const Variant &p_destination,
    const Array &p_sources
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    if (!scene_core->transition_open()) {
        NETW_TRACE(
            sys::SCENE,
            "refused a scene change: another transition is already moving "
            "the roster"
        );
        promise->reject(
            ERR_BUSY,
            String("a scene change is already moving the roster")
        );
        return promise;
    }
    Node *target = scene_existing_destination(p_destination);
    if (target == nullptr) {
        scene_core->set_replacing(true);
        target = scene_activate(p_destination);
        scene_core->set_replacing(false);
    }
    if (target == nullptr) {
        scene_core->transition_close();
        promise->reject(
            ERR_UNAVAILABLE,
            String("a scene change found no destination to move into")
        );
        return promise;
    }
    Array sources;
    for (int at = 0; at < p_sources.size(); at++) {
        Node *source = Object::cast_to<Node>(p_sources[at]);
        if (source != nullptr && source != target) {
            sources.push_back(source);
        }
    }
    scene_core->transition_arm(target, sources, promise);
    scene_carry_transition();
    return promise;
}

void NetwMultiplayer::scene_carry_transition() {
    NETW_ZONE_NC("session scene carry transition", colors::SCENE);
    Node *target = Object::cast_to<Node>(scene_core->transition_target());
    const Callable roster
        = callable_mp(this, &NetwMultiplayer::scene_players_in);
    Ref<NetwEntity> mover = scene_core->transition_next_mover(roster);
    while (mover.is_valid()) {
        const Ref<NetwPromise> moving = scene_move_entity_to(mover, target);
        if (!moving->get_is_settled()) {
            moving->connect(
                StringName("settled"),
                callable_mp(this, &NetwMultiplayer::scene_resume_transition)
                    .bind(mover, moving),
                Object::CONNECT_ONE_SHOT
            );
            return;
        }
        if (!scene_core->transition_accept(
                moving->get_code(),
                mover->get_peer_id()
            )) {
            return;
        }
        mover = scene_core->transition_next_mover(roster);
    }
    scene_land_transition();
}

void NetwMultiplayer::scene_resume_transition(
    const Ref<NetwEntity> &p_mover,
    const Ref<NetwPromise> &p_moving
) {
    if (p_mover.is_null() || p_moving.is_null()) {
        return;
    }
    if (scene_core
            ->transition_accept(p_moving->get_code(), p_mover->get_peer_id())) {
        scene_carry_transition();
    }
}

void NetwMultiplayer::scene_land_transition() {
    NETW_ZONE_NC("session scene land transition", colors::SCENE);
    Node *target = Object::cast_to<Node>(scene_core->transition_target());
    const Ref<NetwSceneHandle> arrived = scene_handle_for(target);
    const TypedArray<Object> seated = participant_admitted_all();
    for (int at = 0; at < seated.size(); at++) {
        const Ref<NetwParticipant> participant = seated[at];
        if (participant.is_null()
            || scene_core->transition_moved(participant->get_peer_id())) {
            continue;
        }
        if (arrived.is_null()) {
            scene_core->transition_fail(ERR_UNAVAILABLE);
            return;
        }
        participant_seat_move(
            participant->get_peer_id(),
            arrived->get_entity()
        );
        const Error admitted
            = scene_admit(entity_of(target), participant->get_peer_id());
        if (admitted != OK) {
            scene_core->transition_fail(admitted);
            return;
        }
    }
    const Array sources = scene_core->transition_sources();
    for (int at = 0; at < sources.size(); at++) {
        Node *source = Object::cast_to<Node>(sources[at]);
        if (source != nullptr) {
            scene_destroy(entity_of(source));
        }
    }
    scene_settle_refresh();
    scene_core->transition_land();
}

Array NetwMultiplayer::scene_sources_for_scope(
    int p_scope,
    Node *p_requester,
    const Ref<NetwParticipant> &p_participant
) {
    Array sources;
    if (p_scope == SCENE_CHANGE_SESSION) {
        const TypedArray<Node> live = scene_live_nodes();
        for (int at = 0; at < live.size(); at++) {
            sources.push_back(live[at]);
        }
        return sources;
    }
    if (p_scope != SCENE_CHANGE_SCENE) {
        return sources;
    }
    Node *here
        = p_requester == nullptr ? nullptr : scene_containing(p_requester);
    if (here == nullptr && p_participant.is_valid()) {
        here = scene_node_of(participant_seat(p_participant->get_peer_id()));
    }
    if (here != nullptr) {
        sources.push_back(here);
    }
    return sources;
}

Ref<NetwPromise> NetwMultiplayer::scene_apply_change(
    const Ref<NetwParticipant> &p_participant,
    const Variant &p_destination,
    int p_scope,
    Node *p_requester
) {
    if (p_scope == SCENE_CHANGE_PARTICIPANT) {
        if (p_participant.is_null()) {
            return NetwPromise::rejected(
                ERR_INVALID_PARAMETER,
                String(
                    "a participant scene change names no participant, and "
                    "authority is never a substitute for one"
                )
            );
        }
        Node *target = scene_resolve_destination(p_destination);
        if (target == nullptr) {
            return NetwPromise::rejected(
                ERR_UNAVAILABLE,
                String("a participant scene change found no destination")
            );
        }
        return participant_travel(p_participant, scene_handle_for(target));
    }
    const Array sources
        = scene_sources_for_scope(p_scope, p_requester, p_participant);
    if (p_scope == SCENE_CHANGE_SCENE && sources.is_empty()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            String(
                "a scene-scoped change names no source world, and the "
                "session is never a substitute for one"
            )
        );
    }
    return scene_replace_sources(p_destination, sources);
}

Ref<NetwPromise> NetwMultiplayer::scene_front_door_change(
    Node *p_requester,
    const String &p_path,
    int p_scope
) {
    if (!NetwSceneCore::scope_names_an_operation(p_scope)) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            String("a scene change scope of ") + itos(p_scope)
                + String(" names no operation")
        );
    }
    if (p_path.is_empty()) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("a scene change needs a live session and a scene path")
        );
    }
    if (!is_server()) {
        return scene_request_open(p_path, p_scope, scene_request_deadline());
    }
    const Ref<PackedScene> packed = scene_packed_at(p_path);
    if (packed.is_null()) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("no scene loads from ") + p_path
        );
    }
    return scene_apply_change(
        scene_requester_participant(p_requester),
        packed,
        p_scope,
        p_requester
    );
}

Ref<NetwPromise> NetwMultiplayer::scene_change_to_file(
    Node *p_requester,
    const String &p_path,
    SceneChange p_scope
) {
    return scene_front_door_change(
        p_requester,
        gd::ensure_path(p_path),
        p_scope
    );
}

Ref<NetwPromise> NetwMultiplayer::scene_change_to_packed(
    Node *p_requester,
    const Ref<PackedScene> &p_packed,
    SceneChange p_scope
) {
    if (NetwSceneCore::destination_kind(p_packed)
        != NetwSceneCore::DESTINATION_PACKED) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("scene_change_to_packed needs a file-backed PackedScene")
        );
    }
    return scene_front_door_change(p_requester, p_packed->get_path(), p_scope);
}

Ref<NetwPromise> NetwMultiplayer::scene_reload_current(
    Node *p_requester,
    SceneChange p_scope
) {
    Node *level = scene_current_node();
    if (level == nullptr || level->get_scene_file_path().is_empty()) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("scene_reload_current needs a file-backed current scene")
        );
    }
    return scene_front_door_change(
        p_requester,
        gd::ensure_path(level->get_scene_file_path()),
        p_scope
    );
}

bool NetwMultiplayer::scene_destroy(const RID &p_scene) {
    Node *live = wrapper_owner(p_scene);
    if (live == nullptr) {
        return false;
    }
    Node *parent = live->get_parent();
    if (parent != nullptr) {
        parent->remove_child(live);
    }
    live->queue_free();
    return true;
}

void NetwMultiplayer::scene_pump_retired() {
    NETW_ZONE_NC("session scene pump retired", colors::SCENE);
    const Array retired = scene_core->pump_retired();
    for (int at = 0; at < retired.size(); ++at) {
        const RID scene = retired[at];
        Node *mounted = wrapper_owner(scene);
        if (mounted != nullptr) {
            mounted->queue_free();
        }
    }
}

bool NetwMultiplayer::scene_remember(Node *p_root) {
    if (p_root == nullptr) {
        return false;
    }
    const RID seat = entity_of(p_root);
    if (!seat.is_valid()) {
        return false;
    }
    scene_core->scene_enter(seat, scene_stem(seat), scene_owns_its_world(seat));
    emit_signal(SIG_SCENE_SPAWNED, p_root);
    scene_participant_display_invalidate();
    return true;
}

void NetwMultiplayer::scene_arrive(
    const Ref<NetwEntity> &p_entity,
    Node *p_source,
    Node *p_target,
    const Ref<NetwPromise> &p_promise
) {
    NETW_ZONE_NC("session scene arrive", colors::SCENE);
    bool flushed_persistence = false;
    if (p_entity.is_valid()) {
        participant_seat_move(
            p_entity->get_peer_id(),
            scene_of(entity_of(p_target))
        );
        const Ref<NetwPersistenceEngine> stored
            = persistence_engine_for(p_entity.ptr());
        if (stored.is_valid()) {
            stored->flush(Array());
            flushed_persistence = true;
        }
    }
    NETW_ZONE_VALUE(int64_t(flushed_persistence));
    emit_signal(SIG_SCENE_ENTITY_MOVED, p_entity, p_source, p_target);
    if (p_promise.is_valid()) {
        p_promise->resolve(OK);
    }
}

void NetwMultiplayer::scene_forget(Object *p_container) {
    if (p_container == nullptr) {
        return;
    }
    scene_core->scene_exit(entity_of(p_container));
    scene_settle_refresh();
    emit_signal(SIG_SCENE_DESPAWNED, p_container);
    scene_participant_display_invalidate();
}

StringName NetwMultiplayer::scene_stem(const RID &p_scene) const {
    const NetwEntity *entity
        = Object::cast_to<NetwEntity>(entity_get_view(p_scene).ptr());
    if (entity != nullptr && !String(entity->get_scene_label()).is_empty()) {
        return entity->get_scene_label();
    }
    const Node *root = wrapper_owner(p_scene);
    return root != nullptr ? StringName(root->get_name()) : StringName();
}

int64_t NetwMultiplayer::scene_route_of(const RID &p_scene) const {
    const int64_t bound = liveness_core->route_of(p_scene);
    if (bound > 0) {
        return bound;
    }
    const NetwEntity *wrapper
        = Object::cast_to<NetwEntity>(entity_get_view(p_scene).ptr());
    return wrapper != nullptr ? wrapper->get_route() : 0;
}

StringName NetwMultiplayer::scene_layer_id(const RID &p_scene) const {
    const Node *root = wrapper_owner(p_scene);
    if (root == nullptr) {
        return StringName();
    }
    const String stem = String(root->get_name());
    const int64_t route = scene_route_of(p_scene);
    if (route <= 0) {
        return StringName("scene:" + stem);
    }
    return StringName("scene:" + stem + "#" + String::num_int64(route));
}

Ref<NetwInterestLayer> NetwMultiplayer::scene_layer_view(
    const RID &p_scene
) const {
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty()) {
        return Ref<NetwInterestLayer>();
    }
    return interest_layer_view(interest_layer_find(layer));
}

PackedInt32Array NetwMultiplayer::scene_get_peers(const RID &p_scene) const {
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

void NetwMultiplayer::scene_adopt_entity(const RID &p_entity) {
    Node *node = entity_get_node(p_entity);
    const Ref<NetwEntity> wrapper
        = Object::cast_to<NetwEntity>(entity_get_view(p_entity).ptr());
    if (node == nullptr || wrapper.is_null()) {
        return;
    }
    const RID destination = scene_of(p_entity);
    if (!destination.is_valid() || destination == p_entity) {
        return;
    }
    if (wrapper->get_peer_id() != 0 && is_server()) {
        scene_admit(destination, wrapper->get_peer_id());
    }
}

Error NetwMultiplayer::scene_admit(const RID &p_scene, int64_t p_peer) {
    NETW_ZONE_NC("NetwMultiplayer scene_admit", colors::SCENE);
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
    interest_flush_now();
    return OK;
}

bool NetwMultiplayer::scene_release(const RID &p_scene, int64_t p_peer) {
    NETW_ZONE_NC("NetwMultiplayer scene_release", colors::SCENE);
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
    interest_flush_now();
    return true;
}

bool NetwMultiplayer::scene_admits(const RID &p_scene, int64_t p_peer) const {
    return interest_engine.layer_has_viewer(scene_layer_id(p_scene), p_peer);
}

bool NetwMultiplayer::scene_leaves_route_unadmitted(int64_t p_route) const {
    const RID entity = entity_from_route(p_route);
    if (!entity.is_valid()) {
        return false;
    }
    NetwEntityRecord *const *record = wrapper_records.getptr(entity.get_id());
    const int64_t peer = record != nullptr ? (*record)->get_peer_id() : 0;
    if (peer <= 0) {
        return false;
    }
    const RID scene = scene_of(entity);
    return scene.is_valid() && !scene_admits(scene, peer);
}

bool NetwMultiplayer::scene_is_declared(const RID &p_entity) const {
    NetwEntityRecord *const *record = wrapper_records.getptr(p_entity.get_id());
    return record != nullptr && (*record)->get_declares_scene();
}

Node *NetwMultiplayer::scene_entity_node(const RID &p_entity) const {
    return wrapper_owner(p_entity);
}

void NetwMultiplayer::collect_scene_entities(
    Node *p_node,
    TypedArray<RID> &r_out
) const {
    if (p_node == nullptr) {
        return;
    }
    const int64_t children = p_node->get_child_count();
    for (int64_t at = 0; at < children; ++at) {
        Node *child = p_node->get_child(int32_t(at));
        const Ref<NetwEntity> wrapper = wrapper_at(child);
        const RID held = handle_of_wrapper(wrapper.ptr());
        const bool owns_record
            = held.is_valid() && wrapper_owner(held) == child;
        if (owns_record) {
            r_out.push_back(held);
            if (scene_is_declared(held)) {
                continue;
            }
        }
        collect_scene_entities(child, r_out);
    }
}

TypedArray<RID> NetwMultiplayer::scene_entities_under(
    const RID &p_scene
) const {
    TypedArray<RID> out;
    collect_scene_entities(scene_entity_node(p_scene), out);
    return out;
}

bool NetwMultiplayer::scene_admit_peer(const RID &p_scene, int64_t p_peer) {
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty() || p_peer == 0) {
        return false;
    }
    if (!interest_engine.layer_add_viewer(layer, p_peer)) {
        return false;
    }
    interest_request_flush();
    scene_send_seat_roster(p_scene, p_peer);
    if (scene_participant_edge.is_valid()) {
        scene_participant_edge.call(p_scene, p_peer, true);
    }
    scene_publish_seat(p_scene, p_peer, true);
    return true;
}

bool NetwMultiplayer::scene_release_peer(const RID &p_scene, int64_t p_peer) {
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty() || p_peer == 0) {
        return false;
    }
    const bool dropped = interest_engine.layer_remove_viewer(layer, p_peer);
    if (!dropped && participant_seat(p_peer) != p_scene) {
        return false;
    }
    if (dropped) {
        interest_request_flush();
    }
    if (scene_participant_edge.is_valid()) {
        scene_participant_edge.call(p_scene, p_peer, false);
    }
    scene_publish_seat(p_scene, p_peer, false);
    return true;
}

RID NetwMultiplayer::scene_seat_subject(int64_t p_route) const {
    const RID scene = liveness_core->rid_from_route(int(p_route));
    return scene_is_declared(scene) ? scene : RID();
}

void NetwMultiplayer::scene_send_seat(
    int64_t p_target,
    int64_t p_route,
    int64_t p_peer,
    bool p_present
) {
    if (p_target == int64_t(get_unique_id())) {
        return;
    }
    send_to(
        p_target,
        0,
        scene_seat_channel,
        session::frame_write(session::SceneSeat{p_route, p_peer, p_present}),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::scene_publish_seat(
    const RID &p_scene,
    int64_t p_peer,
    bool p_present
) {
    const int64_t route = scene_route_of(p_scene);
    if (!is_server() || route <= 0) {
        return;
    }
    const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
    for (int at = 0; at < peers.size(); ++at) {
        scene_send_seat(peers[at], route, p_peer, p_present);
    }
}

void NetwMultiplayer::scene_send_seat_roster(
    const RID &p_scene,
    int64_t p_target
) {
    const int64_t route = scene_route_of(p_scene);
    if (!is_server() || route <= 0) {
        return;
    }
    const PackedInt32Array seated = scene_get_peers(p_scene);
    for (int at = 0; at < seated.size(); ++at) {
        scene_send_seat(p_target, route, seated[at], true);
    }
}

void NetwMultiplayer::scene_apply_seat(
    int64_t p_route,
    int64_t p_peer,
    bool p_present
) {
    const RID scene = scene_seat_subject(p_route);
    if (!scene.is_valid()) {
        scene_seat_parked[p_route][p_peer] = p_present;
        return;
    }
    if (p_present) {
        scene_admit_peer(scene, p_peer);
    } else {
        scene_release_peer(scene, p_peer);
    }
}

void NetwMultiplayer::scene_drain_parked_seats(const RID &p_scene) {
    const int64_t route = scene_route_of(p_scene);
    if (route <= 0 || !scene_seat_parked.has(route)) {
        return;
    }
    const HashMap<int64_t, bool> waiting(scene_seat_parked[route]);
    scene_seat_parked.erase(route);
    for (const KeyValue<int64_t, bool> &row : waiting) {
        scene_apply_seat(route, row.key, row.value);
    }
}

void NetwMultiplayer::scene_receive_seat_frame(
    const PackedByteArray &p_payload,
    int p_sender
) {
    if (p_sender != MultiplayerPeer::TARGET_PEER_SERVER || is_server()) {
        return;
    }
    session::SceneSeat frame;
    if (!session::frame_read(p_payload, frame)) {
        NETW_TRACE(sys::SCENE, "a seat frame that did not decode seats nobody");
        return;
    }
    scene_apply_seat(frame.route, frame.peer, frame.present);
}

bool NetwMultiplayer::scene_notify_released(
    const RID &p_scene,
    int64_t p_peer
) {
    if (participant_seat(p_peer) != p_scene) {
        return false;
    }
    const int64_t route = scene_route_of(p_scene);
    if (route <= 0) {
        return false;
    }
    const PackedByteArray payload
        = session::frame_write(session::SceneReleased{route});
    if (p_peer == int64_t(get_unique_id())) {
        const Callable local
            = channel_book.protocol_handler_of(scene_released_channel);
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

bool NetwMultiplayer::scene_release_departed(
    const RID &p_scene,
    const RID &p_subject,
    bool p_mover_live,
    int64_t p_peer
) {
    if (!is_server()) {
        return false;
    }
    if (p_mover_live && scene_of(p_subject) == p_scene) {
        return false;
    }
    return scene_release_peer(p_scene, p_peer);
}

RID NetwMultiplayer::scene_seat_sync(int64_t p_peer) {
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

StringName NetwMultiplayer::scene_seat_clear_key(
    int64_t p_peer,
    const RID &p_scene
) {
    return StringName(
        "scene-clear-membership?" + String::num_int64(p_peer) + "?"
        + String::num_int64(int64_t(p_scene.get_id()))
    );
}

void NetwMultiplayer::scene_seat_clear_deferred(
    int64_t p_peer,
    const RID &p_scene
) {
    session_defer(
        callable_mp(this, &NetwMultiplayer::participant_seat_clear)
            .bind(p_peer, p_scene),
        scene_seat_clear_key(p_peer, p_scene)
    );
}

StringName NetwMultiplayer::scene_seat_release_key(
    int64_t p_peer,
    const RID &p_scene
) {
    return StringName(
        "scene-seat-release?" + String::num_int64(p_peer) + "?"
        + String::num_int64(int64_t(p_scene.get_id()))
    );
}

Ref<NetwPromise> NetwMultiplayer::scene_move_entity(
    const RID &p_entity,
    const RID &p_destination,
    const Variant &p_opts
) {
    NETW_ZONE_NC("NetwMultiplayer scene_move_entity", colors::SCENE);
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwEntity> mover = entity_get_view(p_entity);
    Node *body = wrapper_owner(p_entity);
    Node *target = wrapper_owner(p_destination);
    Node *source = body != nullptr ? scene_containing(body) : nullptr;
    const int verdict = scene_move_verdict(
        body != nullptr,
        target != nullptr,
        source == target
    );
    if (verdict == SCENE_MOVE_REFUSED) {
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
    if (verdict == SCENE_MOVE_ALREADY_THERE) {
        NETW_TRACE(
            sys::SCENE,
            "move %d already belongs to %d",
            int(p_entity.get_id()),
            int(p_destination.get_id())
        );
        promise->resolve(OK);
        return promise;
    }
    Ref<NetwReparentOpts> opts = p_opts;
    if (opts.is_null()) {
        opts.instantiate();
    }
    if (opts->get_reason() == StringName()) {
        opts->set_reason(scene_move_reason());
    }
    NETW_TRACE(
        sys::SCENE,
        "move %d into %d as '%s'",
        int(p_entity.get_id()),
        int(p_destination.get_id()),
        String(opts->get_reason()).utf8().get_data()
    );
    if (scene_carry_move.is_valid()) {
        scene_carry_move.call(mover, target, p_opts, promise);
        return promise;
    }
    scene_carry_begin(mover, body, source, target, opts, promise);
    return promise;
}

void NetwMultiplayer::scene_carry_begin(
    const Ref<NetwEntity> &p_entity,
    godot::Node *p_body,
    Node *p_source,
    Node *p_target,
    const Ref<NetwReparentOpts> &p_opts,
    const Ref<NetwPromise> &p_promise
) {
    NETW_ZONE_NC("NetwMultiplayer scene_carry_begin", colors::SCENE);
    Node *body = p_body;
    if (body == nullptr || p_entity.is_null()) {
        NETW_TRACE(sys::SCENE, "carry refused: the mover has no live body");
        p_promise->reject(
            ERR_UNAVAILABLE,
            "scene_carry: the mover has no body to reparent"
        );
        return;
    }
    SceneCarry carry;
    carry.entity = p_entity;
    carry.body = gd::instance_id(body);
    carry.source = gd::instance_id(p_source);
    carry.target = gd::instance_id(p_target);
    carry.opts = p_opts;
    carry.promise = p_promise;
    carry.guard = guard_hold(body);
    const int64_t id = ++scene_carry_next;
    scene_carries.insert(id, carry);
    scene_carry_open(id);
}

void NetwMultiplayer::scene_set_carry_move(const Callable &p_carry) {
    scene_carry_move = p_carry;
}

RID NetwMultiplayer::guard_hold(Node *p_body) {
    return reparent_guards.open(p_body);
}

void NetwMultiplayer::guard_then(
    const RID &p_guard,
    int p_frames,
    const Callable &p_answered
) {
    guard_close_window(p_guard);
    Node *body = reparent_guards.body_of(p_guard);
    SceneTree *tree = body != nullptr && body->is_inside_tree()
        ? body->get_tree()
        : nullptr;
    if (tree == nullptr || p_frames <= 0
        || !reparent_guards.tracks_physics(p_guard)) {
        p_answered.call();
        return;
    }
    const Callable spent
        = callable_mp(this, &NetwMultiplayer::guard_spend_frame).bind(p_guard);
    reparent_guards.arm(p_guard, tree, p_frames, p_answered, spent);
    connect_once(Signal(tree, SIG_PHYSICS_FRAME), spent);
}

void NetwMultiplayer::guard_spend_frame(RID p_guard) {
    NETW_ZONE_NC("NetwMultiplayer guard_spend_frame", colors::SCENE);
    if (!reparent_guards.spend(p_guard)) {
        return;
    }
    const Callable answered = reparent_guards.take_answer(p_guard);
    guard_close_window(p_guard);
    answered.call();
}

void NetwMultiplayer::guard_close_window(const RID &p_guard) {
    SceneTree *tree
        = Object::cast_to<SceneTree>(reparent_guards.window_tree(p_guard));
    const Callable spent = reparent_guards.close_window(p_guard);
    if (tree != nullptr && spent.is_valid()) {
        disconnect_once(Signal(tree, SIG_PHYSICS_FRAME), spent);
    }
}

void NetwMultiplayer::guard_let_go(const RID &p_guard) {
    guard_close_window(p_guard);
    reparent_guards.release(p_guard);
}

bool NetwMultiplayer::scene_carry_reachable(int64_t p_id) {
    const SceneCarry *carry = scene_carries.getptr(p_id);
    return carry != nullptr && carry->entity.is_valid()
        && gd::object_of(carry->body) != nullptr
        && gd::object_of(carry->target) != nullptr;
}

void NetwMultiplayer::scene_carry_open(int64_t p_id) {
    if (!scene_carry_reachable(p_id)) {
        scene_carry_abandon(p_id);
        return;
    }
    const SceneCarry *carry = scene_carries.getptr(p_id);
    guard_then(
        carry->guard,
        GUARD_WINDOW_FRAMES,
        callable_mp(this, &NetwMultiplayer::scene_carry_advance).bind(p_id)
    );
}

void NetwMultiplayer::scene_carry_advance(int64_t p_id) {
    NETW_ZONE_NC("NetwMultiplayer scene_carry_advance", colors::SCENE);
    if (!scene_carry_reachable(p_id)) {
        scene_carry_abandon(p_id);
        return;
    }
    SceneCarry *carry = scene_carries.getptr(p_id);
    if (carry->moved) {
        scene_carry_finish(p_id);
        return;
    }
    carry->moved = true;
    Node *target = Object::cast_to<Node>(gd::object_of(carry->target));
    const Ref<NetwEntity> mover = carry->entity;
    const Ref<NetwReparentOpts> opts = carry->opts;
    mover->reparent_to(target, opts);
    scene_carry_open(p_id);
}

void NetwMultiplayer::scene_carry_abandon(int64_t p_id) {
    const SceneCarry *found = scene_carries.getptr(p_id);
    if (found == nullptr) {
        return;
    }
    const SceneCarry carry = *found;
    scene_carries.erase(p_id);
    guard_let_go(carry.guard);
    NETW_TRACE(sys::SCENE, "carry %d abandoned mid-flight", int(p_id));
    if (carry.promise.is_valid()) {
        carry.promise->reject(
            ERR_UNAVAILABLE,
            "scene_carry: the mover, its destination or its tree left before "
            "the carry could finish"
        );
    }
}

void NetwMultiplayer::scene_carry_sweep() {
    while (!scene_carries.is_empty()) {
        scene_carry_abandon(scene_carries.begin()->key);
    }
}

void NetwMultiplayer::scene_carry_finish(int64_t p_id) {
    const SceneCarry *found = scene_carries.getptr(p_id);
    if (found == nullptr) {
        return;
    }
    const SceneCarry carry = *found;
    scene_carries.erase(p_id);
    guard_let_go(carry.guard);
    scene_arrive(
        carry.entity,
        Object::cast_to<Node>(gd::object_of(carry.source)),
        Object::cast_to<Node>(gd::object_of(carry.target)),
        carry.promise
    );
}

StringName NetwMultiplayer::scene_move_reason() {
    return StringName("scene_move");
}

TypedArray<NetwEntity> NetwMultiplayer::scene_players_of(int64_t p_peer) {
    TypedArray<NetwEntity> owned;
    const TypedArray<NetwEntity> everywhere = scene_players_all();
    for (int at = 0; at < everywhere.size(); at++) {
        const Ref<NetwEntity> player = everywhere[at];
        if (player.is_valid() && player->get_peer_id() == p_peer) {
            owned.push_back(player);
        }
    }
    return owned;
}

Ref<NetwPromise> NetwMultiplayer::participant_travel(
    const Ref<NetwParticipant> &p_participant,
    const Ref<NetwSceneHandle> &p_destination
) {
    NETW_ZONE_NC("NetwMultiplayer participant_travel", colors::SCENE);
    if (p_participant.is_null()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            String("travel names no participant")
        );
    }
    if (!is_server()) {
        return NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            String(
                "travel is an authority operation. A client asks through "
                "Netw.change_scene_to_file at participant scope."
            )
        );
    }
    if (p_destination.is_null()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            String("travel names no destination scene")
        );
    }
    const RID destination = p_destination->get_entity();
    Node *target = scene_node_of(destination);
    if (target == nullptr || !scene_core->is_live(destination)) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("travel names a destination this session does not hold")
        );
    }
    const int64_t peer = p_participant->get_peer_id();
    if (participant_admitted_of(peer).is_null()) {
        return NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            String("travel names a peer this session has not admitted")
        );
    }
    if (scene_travel_reserved.has(peer)) {
        return NetwPromise::rejected(
            ERR_BUSY,
            String("this participant is already travelling")
        );
    }
    if (participant_seat(peer) == destination) {
        Ref<NetwPromise> arrived;
        arrived.instantiate();
        arrived->resolve(OK);
        return arrived;
    }

    const TypedArray<NetwEntity> travellers = scene_players_of(peer);
    scene_travel_reserved.insert(peer);
    const Ref<NetwGroupPromise> carried
        = NetwGroupPromise::create(PackedInt32Array());
    Array pending;
    for (int at = 0; at < travellers.size(); at++) {
        const Ref<NetwEntity> player = travellers[at];
        if (player.is_null()) {
            continue;
        }
        pending.push_back(scene_move_entity_to(player, target));
    }
    Ref<NetwPromise> settled;
    settled.instantiate();
    scene_travel_land(peer, destination, pending, 0, settled);
    return settled;
}

void NetwMultiplayer::scene_travel_land(
    int64_t p_peer,
    const RID &p_destination,
    const Array &p_pending,
    int p_at,
    const Ref<NetwPromise> &p_settled
) {
    for (int at = p_at; at < p_pending.size(); at++) {
        const Ref<NetwPromise> carrying = p_pending[at];
        if (carrying.is_null()) {
            continue;
        }
        if (!carrying->get_is_settled()) {
            carrying->when_settled(
                callable_mp(this, &NetwMultiplayer::scene_travel_land)
                    .bind(p_peer, p_destination, p_pending, at + 1, p_settled)
            );
            return;
        }
        if (carrying->get_code() != OK) {
            scene_travel_reserved.erase(p_peer);
            p_settled->reject(
                Error(carrying->get_code()),
                String("travel could not carry every player entity")
            );
            return;
        }
    }
    scene_travel_reserved.erase(p_peer);
    if (!scene_core->is_live(p_destination)) {
        p_settled->reject(
            ERR_UNAVAILABLE,
            String("the destination retired while travel was preparing")
        );
        return;
    }
    const Error admitted = scene_admit(p_destination, p_peer);
    if (admitted != OK) {
        p_settled->reject(
            admitted,
            String("travel could not admit the participant on arrival")
        );
        return;
    }
    participant_move_seat(p_peer, p_destination);
    interest_request_flush();
    scene_settle_refresh();
    p_settled->resolve(OK);
}

Ref<NetwGroupPromise> NetwMultiplayer::scene_move_participants(
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
    session_defer(
        callable_mp(this, &NetwMultiplayer::scene_report_moved).bindv(bound),
        StringName()
    );
    return batch;
}

void NetwMultiplayer::scene_report_moved(
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

Node *NetwMultiplayer::scene_node_of(const RID &p_scene) const {
    return wrapper_owner(p_scene);
}

Dictionary NetwMultiplayer::scene_nodes_by_label() const {
    Dictionary out;
    const Array held = scene_core->live_scenes();
    for (int at = 0; at < held.size(); at++) {
        const RID scene = held[at];
        const StringName stem = scene_core->stem_of(scene);
        if (scene_core->scene_named(stem) != scene) {
            continue;
        }
        Node *node = scene_node_of(scene);
        if (node != nullptr) {
            out[stem] = node;
        }
    }
    return out;
}

TypedArray<Node> NetwMultiplayer::scene_live_nodes() const {
    TypedArray<Node> out;
    const Array held = scene_core->live_scenes();
    for (int at = 0; at < held.size(); at++) {
        Node *node = scene_node_of(held[at]);
        if (node != nullptr) {
            out.push_back(node);
        }
    }
    return out;
}

Node *NetwMultiplayer::scene_current_node() const {
    return scene_node_of(scene_core->get_current_scene());
}

void NetwMultiplayer::scene_report_participant(
    const RID &p_scene,
    int64_t p_peer,
    bool p_present
) {
    if (participant_admitted_of(p_peer).is_null()) {
        if (p_present) {
            scene_core->admission_park(p_scene, p_peer);
        }
        return;
    }
    scene_core->admission_unpark(p_scene, p_peer);
    if (p_present) {
        participant_seat_move(p_peer, p_scene);
    } else {
        scene_seat_clear_deferred(p_peer, p_scene);
    }
    scene_core->dispatch(p_scene, SCENE_EVENT_PARTICIPANT, p_present, p_peer);
    const Ref<NetwSceneHandle> view = scene_handle_of(p_scene);
    if (view.is_valid()) {
        view->announce_participant(participant_of(p_peer), p_present);
    }
}

void NetwMultiplayer::scene_open_admission(Node *p_container) {
    const RID scene = entity_of(p_container);
    if (!scene.is_valid() || scene_admission_layers.has(scene)) {
        return;
    }
    scene_admission_layers.insert(scene, Ref<NetwInterestLayer>());
    if (is_server()) {
        const PackedInt32Array peers = scene_get_peers(scene);
        for (int i = 0; i < peers.size(); i++) {
            scene_report_participant(scene, peers[i], true);
        }
        return;
    }
    connect_once(
        Signal(this, SIG_PARTICIPANT_JOINED),
        callable_mp(this, &NetwMultiplayer::scene_on_participant_joined)
    );
    const Ref<NetwInterestLayer> boundary = scene_layer_view(scene);
    if (boundary.is_null()) {
        return;
    }
    scene_admission_layers[scene] = boundary;
    connect_once(
        Signal(boundary.ptr(), "entity_visible"),
        callable_mp(this, &NetwMultiplayer::scene_on_admission_visible)
            .bind(scene)
    );
    connect_once(
        Signal(boundary.ptr(), "entity_hidden"),
        callable_mp(this, &NetwMultiplayer::scene_on_admission_hidden)
            .bind(scene)
    );
    const Ref<NetwEntity> record = NetwEntity::of(p_container);
    if (record.is_valid() && boundary->has_entity(record)) {
        scene_report_local_participant(scene, true);
    }
}

void NetwMultiplayer::scene_close_admission(Object *p_container) {
    const RID scene = entity_of(p_container);
    if (!scene_admission_layers.has(scene)) {
        return;
    }
    const Ref<NetwInterestLayer> *found = scene_admission_layers.getptr(scene);
    const Ref<NetwInterestLayer> layer
        = found == nullptr ? Ref<NetwInterestLayer>() : *found;
    if (layer.is_valid()) {
        Signal shown(layer.ptr(), "entity_visible");
        const Callable on_shown
            = callable_mp(this, &NetwMultiplayer::scene_on_admission_visible)
                  .bind(scene);
        if (shown.is_connected(on_shown)) {
            shown.disconnect(on_shown);
        }
        Signal hidden(layer.ptr(), "entity_hidden");
        const Callable on_hidden
            = callable_mp(this, &NetwMultiplayer::scene_on_admission_hidden)
                  .bind(scene);
        if (hidden.is_connected(on_hidden)) {
            hidden.disconnect(on_hidden);
        }
    }
    scene_admission_layers.erase(scene);
    if (participant_admitted_local().is_valid()) {
        participant_seat_clear(get_unique_id(), scene);
    }
    const PackedInt32Array peers = scene_get_peers(scene);
    for (int i = 0; i < peers.size(); i++) {
        if (participant_admitted_of(peers[i]).is_valid()) {
            participant_seat_clear(peers[i], scene);
        }
    }
}

void NetwMultiplayer::scene_report_local_participant(
    const RID &p_scene,
    bool p_present
) {
    if (participant_admitted_local().is_valid()) {
        scene_report_participant(p_scene, get_unique_id(), p_present);
    }
}

void NetwMultiplayer::scene_on_participant_joined(
    const Ref<NetwParticipant> &p_participant
) {
    const Ref<NetwParticipant> local = participant_admitted_local();
    const Array scenes = scene_core->live_scenes();
    for (int i = 0; i < scenes.size(); i++) {
        const RID scene = scenes[i];
        const int64_t peer = p_participant->get_peer_id();
        if (scene_core->admission_is_parked(scene, peer)) {
            scene_report_participant(scene, peer, true);
            continue;
        }
        const Ref<NetwInterestLayer> *layer
            = scene_admission_layers.getptr(scene);
        if (layer == nullptr || layer->is_null()
            || p_participant.ptr() != local.ptr()) {
            continue;
        }
        const Ref<NetwEntity> record
            = Object::cast_to<NetwEntity>(entity_get_view(scene).ptr());
        if (record.is_valid() && (*layer)->has_entity(record)) {
            scene_report_participant(scene, peer, true);
        }
    }
}

void NetwMultiplayer::scene_on_admission_visible(
    const Ref<NetwEntity> &p_entity,
    const RID &p_scene
) {
    if (p_entity.ptr() == entity_get_view(p_scene).ptr()) {
        scene_report_local_participant(p_scene, true);
    }
}

void NetwMultiplayer::scene_on_admission_hidden(
    const Ref<NetwEntity> &p_entity,
    const RID &p_scene
) {
    if (p_entity.ptr() == entity_get_view(p_scene).ptr()) {
        scene_report_local_participant(p_scene, false);
    }
}

void NetwMultiplayer::scene_on_entity_live(
    int64_t,
    const Ref<NetwEntity> &p_entity
) {
    scene_watch_entity(p_entity);
}

void NetwMultiplayer::scene_watch_entity(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    Node *node = p_entity->get_owner();
    if (node == nullptr) {
        return;
    }
    const RID subject = entity_of(node);
    if (!subject.is_valid()) {
        return;
    }
    const Callable entered
        = callable_mp(this, &NetwMultiplayer::scene_report_live_entity_edge)
              .bind(p_entity, subject, true);
    const Callable exited
        = callable_mp(this, &NetwMultiplayer::scene_report_live_entity_edge)
              .bind(p_entity, subject, false);
    if (!node->is_connected("tree_entered", entered)) {
        node->connect("tree_entered", entered);
    }
    if (!node->is_connected("tree_exiting", exited)) {
        node->connect("tree_exiting", exited);
    }
    if (node->is_inside_tree()) {
        scene_report_live_entity_edge(p_entity, subject, true);
    }
    if (p_entity->get_peer_id() == 0) {
        return;
    }
    const RID seat = scene_of(subject);
    if (!seat.is_valid() || seat == subject) {
        return;
    }
}

void NetwMultiplayer::scene_report_live_entity_edge(
    const Ref<NetwEntity> &p_entity,
    const RID &p_subject,
    bool p_present
) {
    if (p_entity.is_null() || p_entity->get_owner() == nullptr) {
        return;
    }
    scene_report_entity_edge(
        p_subject,
        p_present,
        p_entity->get_peer_id() != 0
    );
}

Ref<NetwPromise> NetwMultiplayer::scene_request_send(
    const String &p_path,
    int p_scope
) {
    const Ref<NetwPromise> promise = scene_core->request_open();
    session::SceneRequest frame;
    frame.request_id = uint64_t(scene_core->get_pending_request_id());
    frame.path = p_path;
    frame.scope = p_scope;
    send_to(
        MultiplayerPeer::TARGET_PEER_SERVER,
        0,
        scene_request_channel,
        session::frame_write(frame),
        true,
        0,
        String(),
        false
    );
    return promise;
}

Ref<NetwPromise> NetwMultiplayer::scene_request(
    const String &p_path,
    SceneChange p_scope
) {
    return scene_request_open(p_path, p_scope, SCENE_REQUEST_DEADLINE);
}

Ref<NetwPromise> NetwMultiplayer::scene_request_open(
    const String &p_path,
    int p_scope,
    double p_deadline
) {
    const Ref<NetwPromise> promise = scene_request_send(p_path, p_scope);
    if (p_deadline > 0.0) {
        scene_request_arm_deadline(
            scene_core->get_pending_request_id(),
            p_deadline
        );
    }
    return promise;
}

void NetwMultiplayer::scene_request_arm_deadline(
    int p_request_id,
    double p_deadline
) {
    NETW_ZONE_NC("scene request deadline", colors::SCENE);
    SceneTree *tree = gd::scene_tree();
    if (tree == nullptr) {
        NETW_TRACE(
            sys::SCENE,
            "no scene tree, so scene request %d never expires",
            p_request_id
        );
        return;
    }
    const Ref<SceneTreeTimer> countdown = tree->create_timer(p_deadline);
    if (countdown.is_null()) {
        NETW_TRACE(
            sys::SCENE,
            "the tree minted no timer, so scene request %d never expires",
            p_request_id
        );
        return;
    }
    countdown->connect(
        "timeout",
        callable_mp(this, &NetwMultiplayer::scene_request_expire)
            .bind(p_request_id)
    );
    scene_request_armed = p_request_id;
    scene_request_armed_wait = p_deadline;
}

int NetwMultiplayer::scene_request_armed_id() const {
    return scene_request_armed;
}

double NetwMultiplayer::scene_request_armed_deadline() const {
    return scene_request_armed_wait;
}

void NetwMultiplayer::scene_request_expire(int p_request_id) {
    scene_core->request_settle(p_request_id, ERR_TIMEOUT);
}

bool NetwMultiplayer::scene_receive_result_frame(
    const PackedByteArray &p_payload,
    int p_sender
) {
    if (p_sender != 1) {
        warn_verdict(ERR_UNAUTHORIZED, 0);
    }
    return scene_core->receive_result_frame(p_payload, p_sender);
}

void NetwMultiplayer::scene_receive_request_frame(
    const PackedByteArray &p_payload,
    int p_sender
) {
    const Array row = scene_request_frame_row(
        p_payload,
        p_sender,
        Time::get_singleton()->get_ticks_msec()
    );
    if (row.is_empty()) {
        return;
    }
    scene_receive_request(p_sender, row[0], row[1], row[2]);
}

void NetwMultiplayer::scene_receive_released_frame(
    const PackedByteArray &p_payload,
    int p_sender
) {
    const RID seat = scene_released_seat(p_payload, p_sender);
    if (!seat.is_valid()) {
        NETW_TRACE(
            sys::SCENE,
            "a released frame named no seat this peer holds"
        );
        return;
    }
    if (participant_admitted_local().is_valid()) {
        participant_seat_clear(get_unique_id(), seat);
    }
}

void NetwMultiplayer::scene_receive_request(
    int64_t p_peer,
    int p_request_id,
    const String &p_scene_path,
    int p_scope
) {
    const Ref<NetwParticipant> participant = Object::cast_to<NetwParticipant>(
        participant_admitted_of(p_peer).ptr()
    );
    if (participant.is_null()) {
        NETW_WARN(
            sys::SCENE,
            "peer %d asked for a scene change with no admitted participant",
            int(p_peer)
        );
        scene_send_result(p_peer, p_request_id, ERR_UNAUTHORIZED);
        return;
    }
    if (!NetwSceneCore::scope_names_an_operation(p_scope)) {
        NETW_WARN(
            sys::SCENE,
            "peer %d asked for a scene change at scope %d, which names no "
            "operation",
            int(p_peer),
            p_scope
        );
        scene_send_result(p_peer, p_request_id, ERR_INVALID_PARAMETER);
        return;
    }
    scene_receive_path_request(
        p_peer,
        p_request_id,
        participant,
        p_scene_path,
        p_scope
    );
}

void NetwMultiplayer::scene_receive_path_request(
    int64_t p_peer,
    int p_request_id,
    const Ref<NetwParticipant> &p_participant,
    const String &p_scene_path,
    int p_scope
) {
    const String resolved = scene_resolve_requested_path(p_scene_path);
    if (resolved.is_empty()) {
        scene_send_result(p_peer, p_request_id, ERR_UNAVAILABLE);
        return;
    }
    const Error refusal
        = scene_admits_request(p_participant, resolved, p_scope);
    if (refusal != OK) {
        scene_send_result(p_peer, p_request_id, refusal);
        return;
    }
    const Ref<PackedScene> packed = scene_packed_at(resolved);
    if (packed.is_null()) {
        scene_send_result(p_peer, p_request_id, ERR_UNAVAILABLE);
        return;
    }
    if (scene_packed_root_script(packed).is_null()) {
        NETW_TRACE(
            sys::SCENE,
            "an approved scene request loaded %s, whose root declares no "
            "multiplayer scene",
            resolved
        );
        scene_send_result(p_peer, p_request_id, ERR_INVALID_DATA);
        return;
    }
    scene_answer_when_settled(
        scene_apply_change(p_participant, packed, p_scope, nullptr),
        p_peer,
        p_request_id
    );
}

Error NetwMultiplayer::scene_admits_request(
    const Ref<NetwParticipant> &p_participant,
    const Variant &p_destination,
    int p_scope
) {
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_SCENE_REQUESTS);
    if (declared.state == session_decl::READY) {
        scene_core->set_request_handler(declared.callable);
    } else if (declared.state != session_decl::ABSENT) {
        declaration_book().report_unresolved(
            this,
            session_decl::KIND_SCENE_REQUESTS,
            declared.state,
            "a scene request"
        );
        return ERR_UNAUTHORIZED;
    }
    if (!scene_core->get_request_handler().is_valid()) {
        if (claim_verdict_warning(ERR_UNAUTHORIZED, 0)) {
            NETW_WARN(
                sys::SCENE,
                "refusing a client's scene request for %s because this "
                "session installs no handler. Netw.configure_scene_requests "
                "is what decides these, and without one every request is "
                "refused.",
                String(p_destination)
            );
        }
        return ERR_UNAUTHORIZED;
    }
    const Error verdict
        = scene_core->decide_request(p_participant, p_destination, p_scope);
    if (verdict == ERR_INVALID_DATA
        && claim_verdict_warning(ERR_INVALID_DATA, 0)) {
        NETW_WARN(
            sys::SCENE,
            "a scene request handler answered no Error, so the request is "
            "refused. Answer OK to admit and an Error to refuse."
        );
    }
    return verdict;
}

void NetwMultiplayer::scene_send_result(
    int64_t p_peer,
    int p_request_id,
    int p_code
) {
    session::SceneResult frame;
    frame.request_id = uint64_t(p_request_id);
    frame.code = p_code;
    send_to(
        p_peer,
        0,
        scene_result_channel,
        session::frame_write(frame),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::scene_answer_settled_result(
    int64_t p_peer,
    int p_request_id,
    const Ref<NetwPromise> &p_operation
) {
    scene_send_result(p_peer, p_request_id, p_operation->get_code());
}

void NetwMultiplayer::scene_answer_when_settled(
    const Ref<NetwPromise> &p_operation,
    int64_t p_peer,
    int p_request_id
) {
    if (p_operation.is_null()) {
        scene_send_result(p_peer, p_request_id, ERR_UNAVAILABLE);
        return;
    }
    p_operation->when_settled(
        callable_mp(this, &NetwMultiplayer::scene_answer_settled_result)
            .bind(p_peer, p_request_id, p_operation)
    );
}

SceneDecl NetwMultiplayer::scene_decl_of(const Ref<Script> &p_script) const {
    return netw::script::model::get_scene_decl(p_script);
}

void NetwMultiplayer::scene_set_request_handler(const Callable &p_handler) {
    scene_core->set_request_handler(p_handler);
}

Callable NetwMultiplayer::scene_get_request_handler() const {
    return scene_core->get_request_handler();
}

RID NetwMultiplayer::scene_named(const StringName &p_stem) const {
    return scene_core->scene_named(p_stem);
}

Array NetwMultiplayer::scenes_named(const StringName &p_stem) const {
    return scene_core->scenes_named(p_stem);
}

void NetwMultiplayer::scene_observe(
    const RID &p_scene,
    SceneEvent p_event,
    const Callable &p_callback
) {
    scene_core->observe(p_scene, p_event, p_callback);
}

void NetwMultiplayer::scene_unobserve(
    const RID &p_scene,
    SceneEvent p_event,
    const Callable &p_callback
) {
    scene_core->unobserve(p_scene, p_event, p_callback);
}

Ref<NetwPromise> NetwMultiplayer::scene_move(
    const RID &p_entity,
    const RID &p_destination,
    const Ref<NetwReparentOpts> &p_opts
) {
    NETW_ERR_COND_V(
        !is_host(),
        NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            String("scene_move is server authority's")
        ),
        sys::SCENE,
        "an entity was moved between scenes off server authority"
    );

    return scene_move_entity(p_entity, p_destination, p_opts);
}

Error NetwMultiplayer::write_scene_facet(const RID &p_entity, bool p_declared) {
    NETW_ERR_COND_V(
        !is_host() && entity_get_route(p_entity) <= 0,
        ERR_UNAUTHORIZED,
        sys::SCENE,
        "a scene facet was written off server authority for an unrouted entity"
    );

    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        if (!liveness_core->entity_is_valid(p_entity)) {
            return ERR_DOES_NOT_EXIST;
        }
        pending_scene_facets.insert(p_entity.get_id(), p_declared);
        return OK;
    }
    if (wrapper->get_stage() != int64_t(entity::Stage::UNBOUND)) {
        return ERR_UNCONFIGURED;
    }
    wrapper->set_declares_scene(p_declared);
    if (!p_declared) {
        wrapper->set_scene_label(StringName());
    }
    return OK;
}

void NetwMultiplayer::apply_pending_scene_facet(
    const RID &p_entity,
    const Ref<NetwEntity> &p_wrapper
) {
    const bool *parked = pending_scene_facets.getptr(p_entity.get_id());
    if (parked == nullptr || p_wrapper.is_null()) {
        return;
    }
    const bool declared = *parked;
    pending_scene_facets.erase(p_entity.get_id());
    p_wrapper->set_declares_scene(declared);
    if (!declared) {
        p_wrapper->set_scene_label(StringName());
    }
}

Error NetwMultiplayer::scene_declare(const RID &p_entity) {
    return write_scene_facet(p_entity, true);
}

void NetwMultiplayer::scene_clear_pending_facets() {
    pending_scene_facets.clear();
}

Error NetwMultiplayer::scene_undeclare(const RID &p_entity) {
    return write_scene_facet(p_entity, false);
}

Error NetwMultiplayer::scene_set_param(
    const RID &p_scene,
    SceneParam p_param,
    const Variant &p_value
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_scene);
    if (wrapper.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    switch (p_param) {
        case SCENE_PARAM_LABEL:
            wrapper->set_scene_label(StringName(p_value));
            return OK;
        case SCENE_PARAM_ISOLATION:
            if (wrapper->get_stage() != int64_t(entity::Stage::UNBOUND)) {
                return ERR_UNCONFIGURED;
            }
            wrapper->set_scene_isolation(int64_t(p_value));
            return OK;
        case SCENE_PARAM_PROCESSING: {
            Node *node = entity_get_node(p_scene);
            if (node == nullptr) {
                return ERR_DOES_NOT_EXIST;
            }
            node->set_process_mode(
                bool(p_value) ? Node::PROCESS_MODE_INHERIT
                              : Node::PROCESS_MODE_DISABLED
            );
            return OK;
        }
        default:
            NETW_ERR_V(
                ERR_INVALID_PARAMETER,
                sys::SCENE,
                "scene parameter {} names no scene setting",
                p_param
            );
    }
}

Variant NetwMultiplayer::scene_get_param(
    const RID &p_scene,
    SceneParam p_param
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_scene);
    if (wrapper.is_null()) {
        return Variant();
    }
    switch (p_param) {
        case SCENE_PARAM_LABEL:
            return scene_stem(p_scene);
        case SCENE_PARAM_ISOLATION:
            return wrapper->get_scene_isolation();
        case SCENE_PARAM_PROCESSING: {
            Node *node = entity_get_node(p_scene);
            return node != nullptr
                && node->get_process_mode() != Node::PROCESS_MODE_DISABLED;
        }
        default:
            return Variant();
    }
}

RID NetwMultiplayer::scene_find(const StringName &p_stem) {
    const RID found = scene_named(p_stem);
    return entity_get_node(found) != nullptr ? found : RID();
}

TypedArray<RID> NetwMultiplayer::scene_find_all(const StringName &p_stem) {
    TypedArray<RID> out;
    const Array named = scenes_named(p_stem);
    for (int at = 0; at < named.size(); at++) {
        const RID scene = named[at];
        if (entity_get_node(scene) != nullptr) {
            out.push_back(scene);
        }
    }
    return out;
}

TypedArray<RID> NetwMultiplayer::scene_list() {
    TypedArray<RID> out;
    const Array held = live_scenes();
    for (int at = 0; at < held.size(); at++) {
        const RID scene = held[at];
        if (entity_get_node(scene) != nullptr) {
            out.push_back(scene);
        }
    }
    return out;
}

TypedArray<RID> NetwMultiplayer::scene_get_entities(const RID &p_scene) {
    return scene_entities_under(p_scene);
}

RID NetwMultiplayer::scene_get_layer(const RID &p_scene) {
    const StringName layer_id = scene_layer_id(p_scene);
    const RID found = interest_layer_find(layer_id);
    if (found.is_valid() || !interest_has_layer(layer_id)) {
        return found;
    }
    return interest_layer_create(layer_id);
}

RID NetwMultiplayer::scene_get_current() {
    const RID scene = get_current_scene();
    return entity_get_node(scene) != nullptr ? scene : RID();
}

Error NetwMultiplayer::scene_add_player(
    const RID &p_scene,
    const RID &p_player
) {
    const Ref<NetwEntity> player
        = Object::cast_to<NetwEntity>(entity_get_view(p_player).ptr());
    return scene_add_player_record(p_scene, player);
}

Error NetwMultiplayer::scene_add_player_record(
    const RID &p_scene,
    const Ref<NetwEntity> &p_player
) {
    Node *content = wrapper_owner(p_scene);
    if (p_player.is_null() || p_player->get_owner() == nullptr
        || content == nullptr) {
        return ERR_UNAVAILABLE;
    }
    const bool authority = is_server();
    Error verdict = OK;
    if (authority) {
        verdict = scene_admit(p_scene, p_player->get_peer_id());
    }
    Node *owner = p_player->get_owner();
    content->add_child(owner);
    owner->set_owner(content);
    if (authority) {
        scene_watch_entity(p_player);
        interest_flush_immediate();
    }
    return verdict;
}

Node *NetwMultiplayer::scene_get_node(const RID &p_scene) const {
    return scene_node_of(p_scene);
}

StringName NetwMultiplayer::scene_get_label(const RID &p_scene) const {
    return scene_stem(p_scene);
}

TypedArray<NetwEntity> NetwMultiplayer::scene_get_players(const RID &p_scene) {
    TypedArray<NetwEntity> out;
    const TypedArray<RID> members = scene_entities_under(p_scene);
    for (int at = 0; at < members.size(); at++) {
        const Ref<NetwEntity> record
            = Object::cast_to<NetwEntity>(entity_get_view(members[at]).ptr());
        if (record.is_valid() && record->get_owner() != nullptr
            && record->get_peer_id() != 0) {
            out.push_back(record);
        }
    }
    return out;
}

TypedArray<NetwParticipant> NetwMultiplayer::scene_get_participants(
    const RID &p_scene
) {
    TypedArray<NetwParticipant> out;
    const PackedInt32Array seated = scene_get_peers(p_scene);
    for (int at = 0; at < seated.size(); at++) {
        const Ref<NetwParticipant> row = participant_of(seated[at]);
        if (row.is_valid()) {
            out.push_back(row);
        }
    }
    return out;
}

RID NetwMultiplayer::scene_get_local_player(const RID &p_scene) {
    const TypedArray<NetwEntity> players = scene_get_players(p_scene);
    for (int at = 0; at < players.size(); at++) {
        const Ref<NetwEntity> player = players[at];
        if (player.is_valid() && player->get_peer_id() == get_unique_id()) {
            return player->get_rid_handle();
        }
    }
    return RID();
}

TypedArray<NetwEntity> NetwMultiplayer::scene_players_all() {
    TypedArray<NetwEntity> out;
    const TypedArray<RID> scenes = scene_list();
    for (int at = 0; at < scenes.size(); at++) {
        const TypedArray<NetwEntity> players = scene_get_players(scenes[at]);
        for (int seat = 0; seat < players.size(); seat++) {
            out.push_back(players[seat]);
        }
    }
    return out;
}

RID NetwMultiplayer::scene_create(
    const Variant &p_recipe,
    SceneIsolation p_isolation
) {
    NETW_ERR_COND_V(
        !is_host(),
        RID(),
        sys::SCENE,
        "a scene was created off server authority"
    );
    Node *node = scene_spawn(p_recipe, p_isolation);
    if (node == nullptr) {
        return RID();
    }
    const RID entity = entity_of(node);
    scene_declare(entity);
    return entity;
}

} // namespace netw
