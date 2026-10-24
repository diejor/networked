#include "netw/api/scene.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/view/stretch_viewport_container.hpp"

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
#include "godot/utility.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/stage.hpp"
#include "netw/lifecycle/rule.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

const char *SIG_PHYSICS_FRAME = "physics_frame";
const char *SIG_PLAYER_SCENE_CHANGED = "scene_changed";
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

RID NetwMultiplayer::scene_of(const RID &p_entity) const {
    RID walker = p_entity;
    while (walker.is_valid()) {
        NetwEntityRecord *const record = slot_record(walker.get_id());
        if (record != nullptr && record->get_declares_scene()) {
            return walker;
        }
        walker = entity_parent_of(walker);
    }
    return RID();
}

Ref<NetwScene> NetwMultiplayer::scene_handle_of(const RID &p_entity) {
    NetwEntityRecord *const record = slot_record(p_entity.get_id());
    if (record == nullptr) {
        return Ref<NetwScene>();
    }
    return record->part(
        NetwEntityRecord::PART_SCENE,
        entity_get_view(p_entity).ptr()
    );
}

RID NetwMultiplayer::scene_report_entity_edge(
    const RID &p_subject,
    bool p_present,
    bool p_is_player
) {
    RID scene;
    {
        NETW_ZONE_NC("scene edge scene of", colors::SCENE);
        scene = scene_of(p_subject);
    }
    if (!scene.is_valid() || scene == p_subject) {
        return RID();
    }
    scene_core
        ->dispatch(scene, NetwSceneCore::EVENT_ENTITY, p_present, p_subject);
    if (p_is_player) {
        scene_core->dispatch(
            scene,
            NetwSceneCore::EVENT_PLAYER_ENTITY,
            p_present,
            p_subject
        );
        const Ref<NetwScene> view = scene_handle_of(scene);
        if (view.is_valid()) {
            view->announce_player_entity(entity_get_view(p_subject), p_present);
        }
        NETW_ZONE_NC("scene edge display invalidate", colors::SCENE);
        scene_player_display_invalidate();
    }
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
    const Ref<NetwScene> live = scene_handle_of(p_container);
    if (session_handle.is_valid()) {
        session_handle->announce_scene_ready(live);
    }
    notify(SCENE_LIVE, live);
}

RID NetwMultiplayer::scene_released_scene(
    const PackedByteArray &p_payload,
    int p_sender
) {
    if (int64_t(p_sender) != SERVER_PEER || player_admitted_local().is_null()) {
        return RID();
    }
    session::SceneReleased frame;
    if (!session::frame_read(p_payload, frame)) {
        return RID();
    }
    const RID named = scene_viewers_subject(frame.route);
    if (!named.is_valid()) {
        return RID();
    }
    return scene_membership.subscribes(get_unique_id(), named) ? named : RID();
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
        return scene_outer_of(p_root);
    }
    const String name = p_root->get_name();
    SubViewport *world = memnew(SubViewport);
    world->set_use_own_world_3d(true);
    world->set_update_mode(SubViewport::UPDATE_DISABLED);
    world->set_name(name + String("World"));
    world->set_meta(scene_container_meta(), true);
    world->add_child(p_root);
    StretchViewportContainer *view = memnew(StretchViewportContainer);
    view->set_visible(false);
    view->set_name(name + String("View"));
    view->set_meta(scene_container_meta(), true);
    view->add_child(world);
    NETW_TRACE(
        sys::SCENE,
        "'%s' owns its world, so a viewport carries it",
        p_root->get_name()
    );
    return view;
}

SubViewport *NetwMultiplayer::scene_world_of(Node *p_root) {
    Node *parent = p_root != nullptr ? p_root->get_parent() : nullptr;
    if (parent == nullptr || !parent->has_meta(scene_container_meta())) {
        return nullptr;
    }
    return Object::cast_to<SubViewport>(parent);
}

StretchViewportContainer *NetwMultiplayer::scene_view_of(Node *p_root) {
    SubViewport *world = scene_world_of(p_root);
    Node *parent = world != nullptr ? world->get_parent() : nullptr;
    if (parent == nullptr || !parent->has_meta(scene_container_meta())) {
        return nullptr;
    }
    return Object::cast_to<StretchViewportContainer>(parent);
}

Node *NetwMultiplayer::scene_outer_of(Node *p_root) {
    StretchViewportContainer *view = scene_view_of(p_root);
    if (view != nullptr) {
        return view;
    }
    SubViewport *world = scene_world_of(p_root);
    return world != nullptr ? static_cast<Node *>(world) : p_root;
}

Node *NetwMultiplayer::scene_inner_of(Node *p_node) {
    Node *inner = p_node;
    while (inner != nullptr && inner->has_meta(scene_container_meta())
           && inner->get_child_count() > 0) {
        inner = inner->get_child(0);
    }
    return inner;
}

bool NetwMultiplayer::scene_owns_its_world(const RID &p_scene) const {
    const Ref<NetwEntity> record = entity_get_view(p_scene);
    return record.is_valid()
        && record->get_scene_isolation()
        == int64_t(NetwSceneCore::ISOLATION_OWN_WORLD);
}

void NetwMultiplayer::scene_root_online(Node *p_root) {
    if (!scene_remember(p_root)) {
        return;
    }
    const RID live = entity_of(p_root);
    scene_open_admission(p_root);
    scene_drain_parked_viewers(live);
    const Ref<NetwEntity> record = NetwEntity::of(p_root);
    if (record.is_null() || p_root == nullptr) {
        return;
    }
    const Callable publish
        = callable_mp(this, &NetwMultiplayer::scene_publish_live)
              .bind(record->get_route(), live, String(p_root->get_name()));
    if (gd::node_ready(p_root)) {
        publish.call();
        return;
    }
    p_root->connect(StringName("ready"), publish, Object::CONNECT_ONE_SHOT);
}

void NetwMultiplayer::scene_root_offline(Object *p_root) {
    const RID scene = entity_of(p_root);
    if (scene_despawned_roots.erase(scene)) {
        scene_free_layer(scene);
    } else {
        scene_close_admission(scene);
        scene_forget(p_root);
    }
    scene_retire_world(Object::cast_to<Node>(p_root));
}

void NetwMultiplayer::scene_root_settle_exit(
    const RID &p_scene,
    ObjectID p_root,
    ObjectID p_outer,
    bool p_moved
) {
    Node *root = Object::cast_to<Node>(gd::object_of(p_root));
    Node *outer = Object::cast_to<Node>(gd::object_of(p_outer));
    if (p_moved) {
        if (root != nullptr && outer != nullptr && outer != root
            && scene_outer_of(root) != outer) {
            NETW_WARN(
                sys::SCENE,
                "'%s' was moved out of the SubViewport that holds its world. "
                "Reparent the node Netw.spawn returned for it, or use "
                "Netw.reparent.",
                root->get_name()
            );
        }
        return;
    }
    if (scene_core->is_live(p_scene)) {
        scene_close_admission(p_scene);
        scene_forget_viewers(p_scene);
        scene_free_layer(p_scene);
        scene_leave(p_scene, root);
    }
    if (outer != nullptr && outer != root && !outer->is_queued_for_deletion()) {
        outer->queue_free();
    }
}

void NetwMultiplayer::scene_root_despawning(
    NetwEntity *p_entity,
    bool p_send_now
) {
    if (p_entity == nullptr || !p_entity->get_declares_scene()) {
        return;
    }
    Node *root = p_entity->get_owner();
    const RID scene = entity_of(root);
    if (!scene_core->is_live(scene)) {
        return;
    }
    scene_despawned_roots.push_back(scene);
    scene_close_admission(scene);
    scene_forget_viewers(scene);
    scene_leave(scene, root);
    spawn::Pipeline *spawns = spawn_plane();
    if (p_send_now && spawns != nullptr) {
        spawns->despawn_now(liveness_route_of(p_entity), root);
    }
}

void NetwMultiplayer::scene_retire_world(Node *p_root) {
    Node *outer = scene_outer_of(p_root);
    if (outer == p_root || outer->is_queued_for_deletion()) {
        return;
    }
    outer->queue_free();
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

void NetwMultiplayer::scene_reached_by(
    int64_t p_peer,
    LocalVector<RID> &r_reached,
    bool &r_stands
) {
    r_reached.clear();
    r_stands = false;
    if (p_peer == 0) {
        return;
    }
    const Array live = scene_core->live_scenes();
    LocalVector<RID> watched;
    for (int at = 0; at < live.size(); ++at) {
        const RID scene = live[at];
        const TypedArray<NetwEntity> entities
            = scene_get_player_entities(scene);
        bool holds = false;
        for (int seat = 0; seat < entities.size() && !holds; ++seat) {
            const Ref<NetwEntity> entity = entities[seat];
            holds = entity.is_valid() && entity->get_peer_id() == p_peer;
        }
        if (holds) {
            r_reached.push_back(scene);
            continue;
        }
        if (scene_membership.subscribes(p_peer, scene)) {
            watched.push_back(scene);
        }
    }
    r_stands = !r_reached.is_empty();
    if (!r_stands) {
        r_reached = watched;
    }
}

RID NetwMultiplayer::scene_of_peer(int64_t p_peer) {
    if (p_peer == int64_t(get_unique_id())) {
        return scene_presentation;
    }
    LocalVector<RID> reached;
    bool stands = false;
    scene_reached_by(p_peer, reached, stands);
    return reached.size() == 1 ? reached[0] : RID();
}

RID NetwMultiplayer::scene_presentation_derive() {
    LocalVector<RID> reached;
    bool stands = false;
    scene_reached_by(int64_t(get_unique_id()), reached, stands);
    scene_presentation_split = reached.size() > 1;
    if (reached.size() == 1) {
        scene_presentation_split_warned = false;
        return reached[0];
    }
    return RID();
}

void NetwMultiplayer::scene_presentation_warn_split() {
    if (!scene_presentation_split || scene_presentation_split_warned) {
        return;
    }
    LocalVector<RID> reached;
    bool stands = false;
    scene_reached_by(int64_t(get_unique_id()), reached, stands);
    scene_presentation_split = reached.size() > 1;
    if (scene_presentation_split) {
        scene_presentation_split_warned = true;
        String named;
        for (uint32_t at = 0; at < reached.size(); ++at) {
            named += at == 0 ? String("'") : String(", '");
            named += String(scene_stem(reached[at])) + String("'");
        }
        if (stands) {
            NETW_WARN(
                sys::SCENE,
                "This peer has players in %d scenes at once, %s, so its "
                "player is in none of them. Leave all but one of them.",
                int(reached.size()),
                named
            );
        } else {
            NETW_TRACE(
                sys::SCENE,
                "This peer watches %d scenes (%s) and its player is in none "
                "of them.",
                int(reached.size()),
                named
            );
        }
    }
}

void NetwMultiplayer::scene_presentation_settle() {
    const RID reached = scene_presentation_derive();
    if (reached == scene_presentation) {
        return;
    }
    const Ref<NetwScene> from = scene_handle_of(scene_presentation);
    scene_presentation = reached;
    const Ref<NetwPlayer> local = player_local();
    if (local.is_valid()) {
        local->emit_signal(
            SIG_PLAYER_SCENE_CHANGED,
            from,
            scene_handle_of(reached)
        );
    }
}

RID NetwMultiplayer::scene_presented() const {
    return scene_presentation;
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
    scene_core->clear();
    scene_player_display_invalidate();
}

void NetwMultiplayer::scene_install() {
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
    scene_carry_sweep();
    spawn_carry_sweep();
    guard_hush_sweep();
    SceneTree *loop = gd::scene_tree();
    if (loop != nullptr) {
        disconnect_once(
            Signal(loop, SIG_TREE_SCENE_CHANGED),
            callable_mp(this, &NetwMultiplayer::scene_on_native_change)
        );
    }
    scene_core->clear();
}

void NetwMultiplayer::scene_on_session_entered() {
    scene_viewers_server_generation = 0;
    scene_viewers_parked.clear();
    if (is_host()) {
        callable_mp(this, &NetwMultiplayer::scene_announce_startup)
            .call_deferred();
    }
}

void NetwMultiplayer::scene_announce_startup() {
    if (host_join_waits_startup) {
        host_join_waits_startup = false;
        session_submit_host_join_on_startup();
    }
    notify(SCENE_STARTUP_SPAWNED);
}

void NetwMultiplayer::scene_on_native_change() {
    if (session_get_state() != SESSION_STATE_ONLINE
        || !session_root_last_live.is_valid()
        || gd::object_of(session_root_last_live) != nullptr) {
        return;
    }
    NETW_ERROR(
        sys::SCENE,
        "A SceneTree scene change freed the scene that held this session, so "
        "the session ended while its peers were still connected. Keep the "
        "session in a scene you never change, and spawn levels with "
        "Netw.spawn."
    );
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
    notify(SCENE_SPAWNED, p_root);
    scene_player_display_invalidate();
    return true;
}

void NetwMultiplayer::scene_arrive(
    const Ref<NetwEntity> &p_entity,
    Node *p_source,
    Node *p_target,
    const Ref<NetwPromise> &p_promise
) {
    NETW_ZONE_NC("session scene arrive", colors::SCENE);
    notify(SCENE_ENTITY_MOVED, p_entity, p_source, p_target);
    if (p_promise.is_valid()) {
        p_promise->resolve(OK);
    }
}

void NetwMultiplayer::scene_forget(Object *p_container) {
    if (p_container == nullptr) {
        return;
    }
    const RID scene = entity_of(p_container);
    scene_forget_viewers(scene);
    scene_free_layer(scene);
    scene_leave(scene, p_container);
}

void NetwMultiplayer::scene_free_layer(const RID &p_scene) {
    const RID layer = interest_layer_find(scene_layer_id(p_scene));
    if (layer.is_valid()) {
        interest_layer_free(layer);
    }
}

void NetwMultiplayer::scene_leave(const RID &p_scene, Object *p_container) {
    scene_core->scene_exit(p_scene);
    notify(SCENE_DESPAWNED, p_container);
    scene_player_display_invalidate();
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
    return layer_record_named(layer);
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
    if (wrapper->get_peer_id() != 0 && is_host()) {
        scene_watch(destination, wrapper->get_peer_id());
    }
}

Error NetwMultiplayer::scene_watch(const RID &p_scene, int64_t p_peer) {
    NETW_ZONE_NC("NetwMultiplayer scene_watch", colors::SCENE);
    NETW_ERR_COND_V(
        !is_host(),
        ERR_UNAUTHORIZED,
        sys::SCENE,
        "NetwMultiplayer.scene_watch runs only on the server, and was called "
        "for peer %d on a client.",
        int(p_peer)
    );
    if (p_peer == 0) {
        return ERR_INVALID_PARAMETER;
    }
    if (scene_entity_node(p_scene) == nullptr) {
        NETW_TRACE(
            sys::SCENE,
            "Scene %d has no live node, so peer %d does not watch it.",
            int(p_scene.get_id()),
            int(p_peer)
        );
        return ERR_DOES_NOT_EXIST;
    }
    scene_admit_peer(p_scene, p_peer);
    if (!scene_subscribes(p_scene, p_peer)) {
        NETW_WARN(
            sys::SCENE,
            "Peer %d could not be added to scene %d.",
            int(p_peer),
            int(p_scene.get_id())
        );
        return ERR_UNAVAILABLE;
    }
    interest_flush_now();
    return OK;
}

bool NetwMultiplayer::scene_unwatch(const RID &p_scene, int64_t p_peer) {
    NETW_ZONE_NC("NetwMultiplayer scene_unwatch", colors::SCENE);
    NETW_ERR_COND_V(
        !is_host(),
        false,
        sys::SCENE,
        "NetwMultiplayer.scene_unwatch runs only on the server, and was "
        "called for peer %d on a client.",
        int(p_peer)
    );
    if (!is_host() || scene_entity_node(p_scene) == nullptr) {
        return false;
    }
    const bool last_reason = scene_membership.watches(p_peer, p_scene)
        && scene_membership.entities_in(p_peer, p_scene) == 0;
    if (last_reason) {
        scene_notify_released(p_scene, p_peer);
    }
    scene_release_peer(p_scene, p_peer);
    interest_flush_now();
    return true;
}

bool NetwMultiplayer::scene_subscribes(
    const RID &p_scene,
    int64_t p_peer
) const {
    return interest_engine.layer_has_viewer(scene_layer_id(p_scene), p_peer);
}

bool NetwMultiplayer::scene_watches(const RID &p_scene, int64_t p_peer) const {
    return scene_membership.watches(p_peer, p_scene);
}

bool NetwMultiplayer::scene_is_declared(const RID &p_entity) const {
    NetwEntityRecord *const record = slot_record(p_entity.get_id());
    return record != nullptr && record->get_declares_scene();
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

void NetwMultiplayer::scene_sync_viewer(const RID &p_scene, int64_t p_peer) {
    const StringName layer = scene_layer_id(p_scene);
    if (layer.is_empty() || p_peer == 0) {
        return;
    }
    const bool wanted = scene_membership.subscribes(p_peer, p_scene)
        && !spawn_failed_for(scene_route_of(p_scene), p_peer);
    const bool changed = wanted
        ? interest_engine.layer_add_viewer(layer, p_peer)
        : interest_engine.layer_remove_viewer(layer, p_peer);
    if (!changed) {
        return;
    }
    interest_request_flush();
    if (scene_viewer_edge.is_valid()) {
        scene_viewer_edge.call(p_scene, p_peer, wanted);
    }
    scene_publish_viewers(p_scene);
    if (wanted) {
        scene_send_viewers(p_scene, p_peer);
    }
}

bool NetwMultiplayer::scene_admit_peer(const RID &p_scene, int64_t p_peer) {
    if (scene_layer_id(p_scene).is_empty() || p_peer == 0) {
        return false;
    }
    const bool opened = scene_membership.watch(p_peer, p_scene);
    membership_settle();
    return opened;
}

bool NetwMultiplayer::scene_release_peer(const RID &p_scene, int64_t p_peer) {
    if (scene_layer_id(p_scene).is_empty() || p_peer == 0) {
        return false;
    }
    const bool held = scene_membership.subscribes(p_peer, p_scene);
    scene_membership.unwatch(p_peer, p_scene);
    membership_settle();
    return held && !scene_membership.subscribes(p_peer, p_scene);
}

RID NetwMultiplayer::scene_viewers_subject(int64_t p_route) const {
    const RID scene = liveness_core->rid_from_route(int(p_route));
    return scene_is_declared(scene) ? scene : RID();
}

bool NetwMultiplayer::scene_collect_viewers(
    const RID &p_scene,
    LocalVector<uint64_t> &r_members
) const {
    const PackedInt64Array held = scene_membership.members_of(p_scene);
    r_members.clear();
    for (int at = 0; at < held.size(); ++at) {
        const PlayerRow *row = player_row(held[at]);
        if (row == nullptr || row->incarnation == 0) {
            continue;
        }
        r_members.push_back(uint64_t(row->incarnation));
    }
    r_members.sort();
    return uint64_t(r_members.size()) <= session::VIEWERS_MAX;
}

session::SceneViewersHead NetwMultiplayer::scene_viewers_head(
    const RID &p_scene,
    uint64_t p_revision
) const {
    session::SceneViewersHead head;
    head.route = scene_route_of(p_scene);
    head.epoch = uint64_t(liveness_route_epoch(head.route));
    head.generation = session_core.get_generation();
    head.revision = p_revision;
    return head;
}

void NetwMultiplayer::scene_publish_viewers(const RID &p_scene) {
    const int64_t route = scene_route_of(p_scene);
    if (!is_host() || route <= 0) {
        return;
    }
    uint64_t *held = scene_viewers_revision.getptr(route);
    const uint64_t revision = held != nullptr ? *held + 1 : 1;
    scene_viewers_revision[route] = revision;
    LocalVector<uint64_t> members;
    if (!scene_collect_viewers(p_scene, members)) {
        NETW_ERROR(
            sys::SCENE,
            "Scene %d has %d players and its player list holds at most %d, so "
            "the list was not sent. Split the players across more scenes.",
            int(route),
            int(members.size()),
            int(session::VIEWERS_MAX)
        );
        return;
    }
    const PackedByteArray payload = session::viewers_write(
        scene_viewers_head(p_scene, revision),
        members
    );
    if (payload.is_empty()) {
        return;
    }
    const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
    for (int at = 0; at < peers.size(); ++at) {
        if (int64_t(peers[at]) == int64_t(get_unique_id())) {
            continue;
        }
        send_to(
            peers[at],
            0,
            scene_viewers_channel,
            payload,
            true,
            0,
            String(),
            false
        );
    }
}

void NetwMultiplayer::scene_send_viewers(const RID &p_scene, int64_t p_target) {
    const int64_t route = scene_route_of(p_scene);
    if (!is_host() || route <= 0 || p_target == int64_t(get_unique_id())) {
        return;
    }
    const uint64_t *held = scene_viewers_revision.getptr(route);
    LocalVector<uint64_t> members;
    if (!scene_collect_viewers(p_scene, members)) {
        return;
    }
    const PackedByteArray payload = session::viewers_write(
        scene_viewers_head(p_scene, held != nullptr ? *held : 1),
        members
    );
    if (payload.is_empty()) {
        return;
    }
    send_to(
        p_target,
        0,
        scene_viewers_channel,
        payload,
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::scene_apply_viewers(
    const session::SceneViewersHead &p_head,
    const LocalVector<uint64_t> &p_members
) {
    if (!is_host() && scene_viewers_server_generation == 0) {
        scene_viewers_server_generation = p_head.generation;
    }
    const uint64_t expected = is_host() ? session_core.get_generation()
                                        : scene_viewers_server_generation;
    if (p_head.generation != expected) {
        NETW_TRACE(
            sys::SCENE,
            "A viewer list from session generation %d arrived in generation "
            "%d, so it was ignored.",
            int(p_head.generation),
            int(expected)
        );
        return;
    }
    const RID scene = scene_viewers_subject(p_head.route);
    if (!scene.is_valid()) {
        ParkedViewers *parked = scene_viewers_parked.getptr(p_head.route);
        if (parked != nullptr && parked->head.revision >= p_head.revision) {
            return;
        }
        ParkedViewers waiting;
        waiting.head = p_head;
        waiting.members = p_members;
        scene_viewers_parked[p_head.route] = waiting;
        return;
    }
    if (!liveness_epoch_admits(p_head.route, int64_t(p_head.epoch))) {
        NETW_TRACE(
            sys::SCENE,
            "A viewer list for route %d names epoch %d, which this peer has "
            "already replaced.",
            int(p_head.route),
            int(p_head.epoch)
        );
        return;
    }
    const uint64_t *applied = scene_viewers_applied.getptr(p_head.route);
    if (applied != nullptr && *applied >= p_head.revision) {
        return;
    }
    scene_viewers_applied[p_head.route] = p_head.revision;

    PackedInt64Array wanted;
    for (uint32_t at = 0; at < p_members.size(); ++at) {
        const int64_t peer = player_peer_of_id(int64_t(p_members[at]));
        if (peer != 0) {
            wanted.push_back(peer);
        }
    }
    const PackedInt64Array standing = scene_membership.members_of(scene);
    for (int at = 0; at < standing.size(); ++at) {
        if (!wanted.has(standing[at])) {
            scene_release_peer(scene, standing[at]);
        }
    }
    for (int at = 0; at < wanted.size(); ++at) {
        if (!scene_membership.subscribes(wanted[at], scene)) {
            scene_admit_peer(scene, wanted[at]);
        }
    }
}

void NetwMultiplayer::scene_receive_viewers_frame(
    const PackedByteArray &p_payload,
    int p_sender
) {
    if (p_sender != SERVER_PEER || is_host()) {
        return;
    }
    session::SceneViewersHead head;
    LocalVector<uint64_t> members;
    if (!session::viewers_read(p_payload, head, members)) {
        NETW_TRACE(
            sys::SCENE,
            "A viewer list that did not decode fully was ignored."
        );
        return;
    }
    scene_apply_viewers(head, members);
}

void NetwMultiplayer::scene_drain_parked_viewers(const RID &p_scene) {
    const int64_t route = scene_route_of(p_scene);
    if (route <= 0) {
        return;
    }
    const ParkedViewers *parked = scene_viewers_parked.getptr(route);
    if (parked == nullptr) {
        return;
    }
    const session::SceneViewersHead head = parked->head;
    LocalVector<uint64_t> members;
    members.reserve(parked->members.size());
    for (uint32_t at = 0; at < parked->members.size(); ++at) {
        members.push_back(parked->members[at]);
    }
    scene_viewers_parked.erase(route);
    scene_apply_viewers(head, members);
}

void NetwMultiplayer::scene_forget_viewers(const RID &p_scene) {
    if (!p_scene.is_valid()) {
        return;
    }
    const int64_t route = scene_route_of(p_scene);
    if (route > 0) {
        scene_viewers_parked.erase(route);
        scene_viewers_revision.erase(route);
        scene_viewers_applied.erase(route);
    }
    scene_membership.retire_scene(p_scene);
    scene_membership.clear_edges();
}

#if defined(NETW_TESTS)
void NetwMultiplayer::scene_park_viewers_for_test(
    const session::SceneViewersHead &p_head,
    const LocalVector<uint64_t> &p_members
) {
    ParkedViewers waiting;
    waiting.head = p_head;
    waiting.members = p_members;
    scene_viewers_parked[p_head.route] = waiting;
}
#endif

bool NetwMultiplayer::scene_notify_released(
    const RID &p_scene,
    int64_t p_peer
) {
    if (!scene_membership.subscribes(p_peer, p_scene)) {
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
    if (!is_host()) {
        return false;
    }
    if (p_mover_live && scene_of(p_subject) == p_scene) {
        return false;
    }
    const bool held = scene_membership.subscribes(p_peer, p_scene);
    membership_drop_entity_in(p_subject, p_scene, p_peer);
    return held && !scene_membership.subscribes(p_peer, p_scene);
}

Ref<NetwPromise> NetwMultiplayer::scene_move_entity(
    const RID &p_entity,
    const RID &p_destination
) {
    NETW_ZONE_NC("NetwMultiplayer scene_move_entity", colors::SCENE);
    Ref<NetwPromise> promise;
    promise.instantiate();
    lifecycle::Facts facts;
    facts.kind = lifecycle::Kind::REPARENT;
    facts.peer_is_server = is_host();
    facts.minting_halted = structure_halted;
    const lifecycle::Ruling ruling = lifecycle::rule(facts);
    if (!ruling.admitted()) {
        const String text = structure_halted
            ? String(
                  "scene_move: session authority is moving, so no entity "
                  "changes scene until it lands"
              )
            : String(
                  "scene_move: only the session authority moves an entity "
                  "between scenes"
              );
        NETW_ERROR(sys::SCENE, "%s", text);
        promise->reject(ruling.code, text);
        return promise;
    }
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
        NETW_WARN(
            sys::SCENE,
            "Entity %d could not move to scene %d, because one of them is not "
            "in the tree.",
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
            "Entity %d is already in scene %d.",
            int(p_entity.get_id()),
            int(p_destination.get_id())
        );
        promise->resolve(OK);
        return promise;
    }
    NETW_TRACE(
        sys::SCENE,
        "Moving entity %d into scene %d.",
        int(p_entity.get_id()),
        int(p_destination.get_id())
    );
    if (scene_carry_move.is_valid()) {
        scene_carry_move.call(mover, target, promise);
        return promise;
    }
    scene_carry_begin(mover, body, source, target, target, promise);
    return promise;
}

Ref<NetwPromise> NetwMultiplayer::entity_reparent(
    const RID &p_entity,
    Node *p_parent
) {
    NETW_ZONE_NC("NetwMultiplayer entity_reparent", colors::SCENE);
    Ref<NetwPromise> promise;
    promise.instantiate();
    const Ref<NetwEntity> mover = entity_get_view(p_entity);
    Node *body = wrapper_owner(p_entity);
    lifecycle::Facts facts
        = lifecycle_local_facts(mover.ptr(), lifecycle::Kind::REPARENT);
    if (!facts.peer_is_server) {
        facts.destination = lifecycle_destination(body, p_parent);
    }
    String refusal;
    const lifecycle::Ruling ruling
        = lifecycle_judge_verb(facts, String("Netw.reparent"), body, refusal);
    if (!ruling.admitted()) {
        promise->reject(ruling.code, refusal);
        return promise;
    }
    if (mover.is_null() || body == nullptr || p_parent == nullptr) {
        NETW_WARN(
            sys::SCENE,
            "Netw.reparent could not move entity %d, because it or its new "
            "parent is not in the tree.",
            int(p_entity.get_id())
        );
        promise->reject(
            ERR_UNAVAILABLE,
            "reparent: the entity or its new parent is unreachable"
        );
        return promise;
    }
    if (scene_outer_of(body)->get_parent() == p_parent) {
        NETW_TRACE(
            sys::SCENE,
            "Reparent %d is already under its new parent.",
            int(p_entity.get_id())
        );
        promise->resolve(OK);
        return promise;
    }
    NETW_TRACE(
        sys::SCENE,
        "Reparenting %d under %s.",
        int(p_entity.get_id()),
        String(p_parent->get_name())
    );
    const bool awaits_session = !facts.peer_is_server;
    if (awaits_session) {
        lifecycle_promises[liveness_route_of(mover.ptr())] = promise;
    }
    scene_carry_begin(
        mover,
        body,
        scene_containing(body),
        scene_containing(p_parent),
        p_parent,
        promise,
        awaits_session
    );
    return promise;
}

void NetwMultiplayer::scene_carry_begin(
    const Ref<NetwEntity> &p_entity,
    godot::Node *p_body,
    Node *p_source,
    Node *p_target,
    Node *p_parent,
    const Ref<NetwPromise> &p_promise,
    bool p_awaits_session
) {
    NETW_ZONE_NC("NetwMultiplayer scene_carry_begin", colors::SCENE);
    Node *body = p_body;
    if (body == nullptr || p_entity.is_null()) {
        NETW_TRACE(
            sys::SCENE,
            "A move was refused, because the moving entity has no live body."
        );
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
    carry.parent = gd::instance_id(p_parent);
    carry.promise = p_promise;
    carry.awaits_session = p_awaits_session;
    carry.hush = guard_hush_areas(body);
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

namespace {

bool area_monitoring(Node *p_node) {
    return (p_node->is_class("Area2D") || p_node->is_class("Area3D"))
        && bool(p_node->get(StringName("monitoring")));
}

bool area_overlaps(
    Node *p_area,
    const char *p_method,
    Node *p_moved,
    LocalVector<ObjectID> &r_ids
) {
    bool touches_moved = false;
    const Array overlapping = p_area->call(StringName(p_method));
    for (int at = 0; at < overlapping.size(); ++at) {
        Node *other = Object::cast_to<Node>(gd::live_object(overlapping[at]));
        if (other == nullptr) {
            continue;
        }
        r_ids.push_back(gd::instance_id(other));
        if (p_moved != nullptr
            && (other == p_moved || p_moved->is_ancestor_of(other))) {
            touches_moved = true;
        }
    }
    return touches_moved;
}

void area_replay(
    Node *p_area,
    const LocalVector<ObjectID> &p_before,
    const LocalVector<ObjectID> &p_now,
    const char *p_exited,
    const char *p_entered
) {
    for (const ObjectID id : p_before) {
        Object *other = gd::object_of(id);
        if (other != nullptr && p_now.find(id) < 0) {
            p_area->emit_signal(StringName(p_exited), other);
        }
    }
    for (const ObjectID id : p_now) {
        Object *other = gd::object_of(id);
        if (other != nullptr && p_before.find(id) < 0) {
            p_area->emit_signal(StringName(p_entered), other);
        }
    }
}

} // namespace

int64_t NetwMultiplayer::guard_hush_areas(Node *p_body) {
    if (p_body == nullptr) {
        return 0;
    }
    Node *moved = scene_outer_of(p_body);
    Viewport *world = moved->is_inside_tree() ? moved->get_viewport() : nullptr;
    AreaHush hush;
    LocalVector<Node *> open;
    open.push_back(world != nullptr ? static_cast<Node *>(world) : moved);
    while (!open.is_empty()) {
        Node *node = open[open.size() - 1];
        open.remove_at(open.size() - 1);
        for (int at = 0; at < node->get_child_count(); ++at) {
            open.push_back(node->get_child(at));
        }
        if (!area_monitoring(node)) {
            continue;
        }
        HushedArea seen;
        const bool inside = node == moved || moved->is_ancestor_of(node);
        const bool touches_body
            = area_overlaps(node, "get_overlapping_bodies", moved, seen.bodies);
        const bool touches_area
            = area_overlaps(node, "get_overlapping_areas", moved, seen.areas);
        if (!inside && !touches_body && !touches_area) {
            continue;
        }
        const uint64_t area = uint64_t(gd::instance_id(node));
        if (HushedArea *held = hushed_areas.getptr(area)) {
            held->holders += 1;
        } else if (node->is_blocking_signals()) {
            continue;
        } else {
            node->set_block_signals(true);
            seen.holders = 1;
            hushed_areas.insert(area, seen);
        }
        hush.areas.push_back(ObjectID(area));
    }
    if (hush.areas.is_empty()) {
        return 0;
    }
    const int64_t id = ++area_hush_next;
    area_hushes.insert(id, hush);
    return id;
}

void NetwMultiplayer::guard_hush_release(int64_t p_id) {
    AreaHush *hush = area_hushes.getptr(p_id);
    if (hush == nullptr) {
        return;
    }
    SceneTree *tree = gd::scene_tree();
    if (tree == nullptr) {
        guard_hush_end(p_id);
        return;
    }
    hush->tree = gd::instance_id(tree);
    hush->frames = GUARD_WINDOW_FRAMES;
    hush->spent
        = callable_mp(this, &NetwMultiplayer::guard_hush_spend).bind(p_id);
    connect_once(Signal(tree, SIG_PHYSICS_FRAME), hush->spent);
}

void NetwMultiplayer::guard_hush_spend(int64_t p_id) {
    AreaHush *hush = area_hushes.getptr(p_id);
    if (hush == nullptr) {
        return;
    }
    hush->frames -= 1;
    if (hush->frames <= 0) {
        guard_hush_end(p_id);
    }
}

void NetwMultiplayer::guard_hush_end(int64_t p_id, bool p_replay) {
    AreaHush *hush = area_hushes.getptr(p_id);
    if (hush == nullptr) {
        return;
    }
    SceneTree *tree = Object::cast_to<SceneTree>(gd::object_of(hush->tree));
    if (tree != nullptr) {
        disconnect_once(Signal(tree, SIG_PHYSICS_FRAME), hush->spent);
    }
    const LocalVector<ObjectID> areas(hush->areas);
    area_hushes.erase(p_id);
    for (const ObjectID area : areas) {
        HushedArea *held = hushed_areas.getptr(uint64_t(area));
        if (held == nullptr) {
            continue;
        }
        held->holders -= 1;
        if (held->holders > 0) {
            continue;
        }
        const HushedArea before = *held;
        hushed_areas.erase(uint64_t(area));
        Node *live = Object::cast_to<Node>(gd::object_of(area));
        if (live == nullptr) {
            continue;
        }
        live->set_block_signals(false);
        if (!p_replay || !area_monitoring(live)) {
            continue;
        }
        HushedArea now;
        area_overlaps(live, "get_overlapping_bodies", nullptr, now.bodies);
        area_overlaps(live, "get_overlapping_areas", nullptr, now.areas);
        area_replay(
            live,
            before.bodies,
            now.bodies,
            "body_exited",
            "body_entered"
        );
        area_replay(
            live,
            before.areas,
            now.areas,
            "area_exited",
            "area_entered"
        );
    }
}

void NetwMultiplayer::guard_hush_sweep() {
    while (!area_hushes.is_empty()) {
        guard_hush_end(area_hushes.begin()->key, false);
    }
}

bool NetwMultiplayer::scene_carry_reachable(int64_t p_id) {
    const SceneCarry *carry = scene_carries.getptr(p_id);
    return carry != nullptr && carry->entity.is_valid()
        && gd::object_of(carry->body) != nullptr
        && gd::object_of(carry->parent) != nullptr;
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
    Node *parent = Object::cast_to<Node>(gd::object_of(carry->parent));
    const Ref<NetwEntity> mover = carry->entity;
    entity_move(mover->get_owner(), parent);
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
    guard_hush_release(carry.hush);
    NETW_TRACE(
        sys::SCENE,
        "Move %d was abandoned before it finished.",
        int(p_id)
    );
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
    guard_hush_release(carry.hush);
    scene_arrive(
        carry.entity,
        Object::cast_to<Node>(gd::object_of(carry.source)),
        Object::cast_to<Node>(gd::object_of(carry.target)),
        carry.awaits_session ? Ref<NetwPromise>() : carry.promise
    );
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

Node *NetwMultiplayer::scene_presented_node() const {
    return scene_node_of(scene_presentation);
}

void NetwMultiplayer::scene_report_viewer(
    const RID &p_scene,
    int64_t p_peer,
    bool p_present
) {
    if (player_admitted_of(p_peer).is_null()) {
        if (p_present) {
            scene_core->admission_park(p_scene, p_peer);
        }
        return;
    }
    scene_core->admission_unpark(p_scene, p_peer);
    scene_core->dispatch(p_scene, SCENE_EVENT_VIEWER, p_present, p_peer);
    const Ref<NetwScene> view = scene_handle_of(p_scene);
    if (view.is_valid()) {
        view->announce_viewer(player_of(p_peer), p_present);
    }
}

void NetwMultiplayer::scene_open_admission(Node *p_container) {
    const RID scene = entity_of(p_container);
    if (!scene.is_valid() || scene_admission_layers.has(scene)) {
        return;
    }
    scene_admission_layers.insert(scene, Ref<NetwInterestLayer>());
    if (is_host()) {
        const PackedInt32Array peers = scene_get_peers(scene);
        for (int i = 0; i < peers.size(); i++) {
            scene_report_viewer(scene, peers[i], true);
        }
        return;
    }
    scene_admits_on_join = true;
    const Ref<NetwInterestLayer> boundary = scene_layer_view(scene);
    if (boundary.is_null()) {
        return;
    }
    scene_admission_layers[scene] = boundary;
    connect_once(
        Signal(boundary.ptr(), "visibility_changed"),
        callable_mp(this, &NetwMultiplayer::scene_on_admission_visibility)
            .bind(scene)
    );
    const Ref<NetwEntity> record = NetwEntity::of(p_container);
    if (record.is_valid() && boundary->has_entity(record)) {
        scene_report_local_viewer(scene, true);
    }
}

void NetwMultiplayer::scene_close_admission(const RID &p_scene) {
    if (!scene_admission_layers.has(p_scene)) {
        return;
    }
    const Ref<NetwInterestLayer> *found
        = scene_admission_layers.getptr(p_scene);
    const Ref<NetwInterestLayer> layer
        = found == nullptr ? Ref<NetwInterestLayer>() : *found;
    if (layer.is_valid()) {
        Signal changed(layer.ptr(), "visibility_changed");
        const Callable on_changed
            = callable_mp(this, &NetwMultiplayer::scene_on_admission_visibility)
                  .bind(p_scene);
        if (changed.is_connected(on_changed)) {
            changed.disconnect(on_changed);
        }
    }
    scene_admission_layers.erase(p_scene);
    scene_player_display_invalidate();
    scene_forget_viewers(p_scene);
}

void NetwMultiplayer::scene_report_local_viewer(
    const RID &p_scene,
    bool p_present
) {
    if (player_admitted_local().is_valid()) {
        scene_report_viewer(p_scene, get_unique_id(), p_present);
    }
}

void NetwMultiplayer::scene_on_player_joined(const Ref<NetwPlayer> &p_player) {
    const Ref<NetwPlayer> local = player_admitted_local();
    const Array scenes = scene_core->live_scenes();
    for (int i = 0; i < scenes.size(); i++) {
        const RID scene = scenes[i];
        const int64_t peer = p_player->get_peer_id();
        if (scene_core->admission_is_parked(scene, peer)) {
            scene_report_viewer(scene, peer, true);
            continue;
        }
        const Ref<NetwInterestLayer> *layer
            = scene_admission_layers.getptr(scene);
        if (layer == nullptr || layer->is_null()
            || p_player.ptr() != local.ptr()) {
            continue;
        }
        const Ref<NetwEntity> record
            = Object::cast_to<NetwEntity>(entity_get_view(scene).ptr());
        if (record.is_valid() && (*layer)->has_entity(record)) {
            scene_report_viewer(scene, peer, true);
        }
    }
}

void NetwMultiplayer::scene_on_admission_visibility(
    const Ref<NetwEntity> &p_entity,
    bool p_visible,
    const RID &p_scene
) {
    if (p_entity.ptr() == entity_get_view(p_scene).ptr()) {
        scene_report_local_viewer(p_scene, p_visible);
    }
}

void NetwMultiplayer::scene_on_entity_live(
    int64_t,
    const Ref<NetwEntity> &p_entity
) {
    NETW_ZONE_NC("scene on entity live", colors::SCENE);
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
    RID subject;
    {
        NETW_ZONE_NC("scene watch entity of", colors::SCENE);
        subject = entity_of(node);
    }
    if (!subject.is_valid()) {
        return;
    }
    p_entity->watch_scene_edges(subject);
    if (node->is_inside_tree()) {
        NETW_ZONE_NC("scene watch report edge", colors::SCENE);
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

void NetwMultiplayer::scene_receive_released_frame(
    const PackedByteArray &p_payload,
    int p_sender
) {
    const RID released = scene_released_scene(p_payload, p_sender);
    if (!released.is_valid()) {
        NETW_TRACE(
            sys::SCENE,
            "A released frame names no scene this peer watches."
        );
        return;
    }
    scene_release_peer(released, get_unique_id());
}

SceneDecl NetwMultiplayer::scene_decl_of(const Ref<Script> &p_script) const {
    return netw::script::model::get_scene_decl(p_script);
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

Error NetwMultiplayer::write_scene_facet(const RID &p_entity, bool p_declared) {
    NETW_ERR_COND_V(
        !is_host() && entity_get_route(p_entity) <= 0,
        ERR_UNAUTHORIZED,
        sys::SCENE,
        "A client cannot set the scene of an entity that has not spawned yet."
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
                "Scene setting %d does not exist.",
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

Node *NetwMultiplayer::scene_get_node(const RID &p_scene) const {
    return scene_node_of(p_scene);
}

StringName NetwMultiplayer::scene_get_label(const RID &p_scene) const {
    return scene_stem(p_scene);
}

TypedArray<NetwEntity> NetwMultiplayer::scene_get_player_entities(
    const RID &p_scene
) {
    TypedArray<NetwEntity> out;
    const TypedArray<RID> members = scene_entities_under(p_scene);
    for (int at = 0; at < members.size(); at++) {
        const Ref<NetwEntity> record
            = Object::cast_to<NetwEntity>(entity_get_view(members[at]).ptr());
        if (record.is_valid() && record->get_owner() != nullptr
            && record->get_peer_id() != 0
            && !entity::stage_is_leaving(record->get_stage())) {
            out.push_back(record);
        }
    }
    return out;
}

TypedArray<NetwPlayer> NetwMultiplayer::scene_get_viewers(const RID &p_scene) {
    TypedArray<NetwPlayer> out;
    const PackedInt32Array watching = scene_get_peers(p_scene);
    for (int at = 0; at < watching.size(); at++) {
        const Ref<NetwPlayer> row = player_of(watching[at]);
        if (row.is_valid()) {
            out.push_back(row);
        }
    }
    return out;
}

TypedArray<NetwEntity> NetwMultiplayer::scene_get_local_player_entities(
    const RID &p_scene
) {
    TypedArray<NetwEntity> out;
    const TypedArray<NetwEntity> entities = scene_get_player_entities(p_scene);
    for (int at = 0; at < entities.size(); at++) {
        const Ref<NetwEntity> entity = entities[at];
        if (entity.is_valid() && entity->get_peer_id() == get_unique_id()) {
            out.push_back(entity);
        }
    }
    return out;
}

TypedArray<NetwEntity> NetwMultiplayer::scene_player_entities_all() {
    TypedArray<NetwEntity> out;
    const TypedArray<RID> scenes = scene_list();
    for (int at = 0; at < scenes.size(); at++) {
        const TypedArray<NetwEntity> entities
            = scene_get_player_entities(scenes[at]);
        for (int seat = 0; seat < entities.size(); seat++) {
            out.push_back(entities[seat]);
        }
    }
    return out;
}

} // namespace netw
