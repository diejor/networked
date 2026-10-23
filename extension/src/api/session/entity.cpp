#include "netw/api/interest.hpp"

#include <cstdlib>

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene.hpp"

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
#include "netw/api/predict_slot_engine.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/sync_compat.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/stage.hpp"
#include "netw/lifecycle/rule.hpp"
#include "netw/log.hpp"
#include "netw/object_port.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/synchronizers.hpp"
#include "netw/tree_hook.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;
using namespace netw;

namespace netw {

using repl::PropertyBindingPtr;
using repl::PropertyColumn;
using repl::PropertySet;

namespace {

Variant world_pose_of(Node *p_node) {
    if (p_node == nullptr || !p_node->is_inside_tree()) {
        return Variant();
    }
    if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
        return spatial->get_global_transform();
    }
    if (Node2D *flat = Object::cast_to<Node2D>(p_node)) {
        return flat->get_global_transform();
    }
    return Variant();
}

void world_pose_restore(Node *p_node, const Variant &p_pose) {
    if (p_node == nullptr || !p_node->is_inside_tree()) {
        return;
    }
    if (p_pose.get_type() == Variant::TRANSFORM3D) {
        if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
            spatial->set_global_transform(Transform3D(p_pose));
        }
    } else if (p_pose.get_type() == Variant::TRANSFORM2D) {
        if (Node2D *flat = Object::cast_to<Node2D>(p_node)) {
            flat->set_global_transform(Transform2D(p_pose));
        }
    }
}

} // namespace

void NetwMultiplayer::wrapper_adopt(
    const RID &p_entity,
    const Ref<NetwEntity> &p_wrapper,
    Object *p_owner
) {
    if (!p_entity.is_valid() || p_wrapper.is_null()) {
        return;
    }
    liveness_core->adopt(p_entity);
    EntitySlot &slot = entity_slots.ensure(p_entity.get_id());
    slot.live = p_wrapper;
    sim_follow_view(p_entity, p_wrapper.ptr());
    slot.retired = Ref<NetwEntity>();
    slot.indexed = gd::instance_id(p_wrapper.ptr());
    if (p_owner != nullptr) {
        slot.owner = gd::instance_id(p_owner);
    }
}

NetwEntityRecord *NetwMultiplayer::slot_record(int64_t p_id) const {
    const EntitySlot *slot = entity_slots.find(uint64_t(p_id));
    return slot != nullptr ? slot->record : nullptr;
}

void NetwMultiplayer::slot_settle(int64_t p_id) {
    const EntitySlot *slot = entity_slots.find(uint64_t(p_id));
    if (slot != nullptr && slot->vacant()) {
        entity_slots.erase(uint64_t(p_id));
    }
}

Node *NetwMultiplayer::wrapper_owner(const RID &p_entity) const {
    const EntitySlot *slot = entity_slots.find(p_entity.get_id());
    return slot != nullptr && slot->owner.is_valid()
        ? Object::cast_to<Node>(gd::instance_from_id(slot->owner))
        : nullptr;
}

RID NetwMultiplayer::handle_of_wrapper(NetwEntity *p_wrapper) const {
    if (p_wrapper == nullptr || p_wrapper->get_record() == nullptr) {
        return RID();
    }
    const EntitySlot *slot
        = entity_slots.find(p_wrapper->get_record()->get_handle().get_id());
    if (slot == nullptr || slot->record == nullptr
        || slot->indexed != gd::instance_id(p_wrapper)) {
        return RID();
    }
    return slot->record->get_handle();
}

namespace {

Callable &wrapper_mint() {
    static Callable factory;
    return factory;
}

} // namespace

StringName NetwMultiplayer::wrapper_meta() {
    return NetwEntityRecord::entity_meta();
}

void NetwMultiplayer::set_wrapper_factory(const Callable &p_factory) {
    wrapper_mint() = p_factory;
}

bool NetwMultiplayer::has_wrapper_factory() {
    return wrapper_mint().is_valid();
}

void NetwMultiplayer::clear_wrapper_factory() {
    wrapper_mint() = Callable();
}

Callable NetwMultiplayer::wrapper_factory() {
    return wrapper_mint();
}

Ref<NetwEntity> NetwMultiplayer::wrapper_at(Object *p_node) {
    Node *walker = Object::cast_to<Node>(p_node);
    while (walker != nullptr) {
        Object *found = tree::entity_object_at(walker);
        if (found != nullptr) {
            return Ref<NetwEntity>(Object::cast_to<NetwEntity>(found));
        }
        walker = walker->get_parent();
    }
    return Ref<NetwEntity>();
}

Ref<NetwEntity> NetwMultiplayer::wrapper_ensure(Node *p_root) {
    Node *root = p_root;
    if (root == nullptr) {
        return Ref<NetwEntity>();
    }
    if (Object *found = tree::entity_object_at(root)) {
        return Ref<NetwEntity>(Object::cast_to<NetwEntity>(found));
    }
    const Callable &factory = wrapper_mint();
    if (!factory.is_valid()) {
        Ref<NetwEntity> entity;
        entity.instantiate();
        entity->attach_to(root);
        return entity;
    }
    const Variant made = factory.call(root);
    Ref<NetwEntity> minted = made;
    NETW_WARN_COND(
        minted.is_null(),
        sys::ENTITY,
        "No NetwEntity could be made for '%s'.",
        root->get_name()
    );
    return minted;
}

Ref<NetwEntity> NetwMultiplayer::wrapper_resolve(Node *p_node) {
    Node *node = p_node;
    if (node == nullptr) {
        return Ref<NetwEntity>();
    }
    Ref<NetwEntity> existing = wrapper_at(node);
    if (existing.is_valid()) {
        return existing;
    }
    if (node->is_inside_tree()) {
        return Ref<NetwEntity>();
    }
    Node *root = node;
    while (root->get_parent() != nullptr) {
        if (root->get_parent()->is_inside_tree()) {
            return Ref<NetwEntity>();
        }
        root = root->get_parent();
    }
    return wrapper_ensure(root);
}

Object *NetwMultiplayer::wrapper_stamp_identity(
    Node *p_node,
    const StringName &p_entity_id,
    int64_t p_peer_id
) {
    Node *node = p_node;
    if (node == nullptr) {
        return nullptr;
    }
    NETW_ERR_COND_V(
        String(p_entity_id).is_empty(),
        p_node,
        sys::ENTITY,
        "'%s' was given an empty entity id, so it was not renamed. Pass a "
        "non-empty id.",
        node->get_name()
    );
    node->set_name(String(p_entity_id));
    const Ref<NetwEntity> wrapper = wrapper_ensure(node);
    if (wrapper.is_valid()) {
        wrapper->set_name(p_entity_id);
        wrapper->set_peer_id(p_peer_id);
    }
    return node;
}

RID NetwMultiplayer::entity_at_or_above(Node *p_node) const {
    return handle_of_wrapper(wrapper_at(p_node).ptr());
}

RID NetwMultiplayer::entity_parent_of(const RID &p_entity) const {
    Node *owner = wrapper_owner(p_entity);
    if (owner == nullptr) {
        return RID();
    }
    return entity_at_or_above(owner->get_parent());
}

Ref<NetwScene> NetwMultiplayer::entity_scene_facet(
    NetwEntityRecord *p_record,
    NetwEntity *p_wrapper
) {
    if (p_record == nullptr) {
        return Ref<NetwScene>();
    }
    Node *owner = p_wrapper != nullptr ? p_wrapper->get_owner() : nullptr;
    const RID own
        = owner != nullptr ? entity_of(owner) : p_record->get_handle();
    const RID resolved = scene_of(own);
    if (!resolved.is_valid()) {
        return Ref<NetwScene>();
    }
    return scene_handle_of(resolved);
}

void NetwMultiplayer::entity_move(Node *p_owner, Node *p_new_parent) {
    Node *owner = p_owner;
    Node *parent = p_new_parent;
    NETW_ERR_COND(
        owner == nullptr || parent == nullptr,
        sys::ENTITY,
        "An entity move needs both a node and a new parent, and one was "
        "null."
    );
    NETW_TRACE(sys::ENTITY, "Moving an entity under %s.", parent->get_name());
    if (owner->is_inside_tree()) {
        owner->reparent(parent);
    } else {
        if (owner->get_parent() != nullptr) {
            owner->get_parent()->remove_child(owner);
        }
        parent->add_child(owner);
    }
}

void NetwMultiplayer::entity_free_owner(uint64_t p_owner_id) {
    Node *owner
        = Object::cast_to<Node>(gd::instance_from_id(ObjectID(p_owner_id)));
    if (owner != nullptr) {
        owner->queue_free();
    }
}

namespace {

void rename_reserved_to_identity(Node *p_owner, const StringName &p_entity_id) {
    const String seated = String(p_owner->get_name());
    if (seated == seated.validate_node_name()) {
        return;
    }
    const String identity = String(p_entity_id).validate_node_name();
    if (identity.is_empty()) {
        return;
    }
    p_owner->set_name(identity);
    NETW_TRACE(
        sys::ENTITY,
        "'%s' is a name the engine reserves, so '%s' is taken from the "
        "entity id instead",
        seated,
        p_owner->get_name()
    );
}

} // namespace

void NetwMultiplayer::entity_enter_tree(
    Object *p_wrapper,
    Node *p_owner,
    NetwEntityRecord *p_record,
    NetwMultiplayer *p_session,
    bool p_is_authority
) {
    Node *owner = p_owner;
    if (owner == nullptr || p_wrapper == nullptr || p_record == nullptr) {
        return;
    }
    const Ref<RefCounted> keep(Object::cast_to<RefCounted>(p_wrapper));
    const Ref<NetwMultiplayer> session(p_session);
    NetwEntity *wrapper = Object::cast_to<NetwEntity>(p_wrapper);
    const ObjectID owner_id = gd::instance_id(owner);
    const ObjectID parent_id = gd::instance_id(owner->get_parent());
    RID handle = p_record->get_handle();
    Ref<MultiplayerAPI> stamped = wrapper != nullptr
        ? wrapper->get_multiplayer()
        : Ref<MultiplayerAPI>();
    const auto intact = [&]() {
        if (gd::object_of(owner_id) != owner
            || gd::instance_id(owner->get_parent()) != parent_id
            || owner->is_queued_for_deletion()
            || p_record->get_handle() != handle) {
            return false;
        }
        return wrapper == nullptr
            || (wrapper->get_record() == p_record
                && wrapper->get_owner() == owner
                && wrapper->get_multiplayer() == stamped);
    };
    const auto publish = [&](int64_t p_from) {
        const int64_t to = p_record->get_stage();
        if (session.is_valid() && session.ptr() != stamped.ptr()) {
            session->entity_note_stage(p_record, p_from, to);
        }
        if (wrapper != nullptr && wrapper->get_record() == p_record) {
            wrapper->publish_stage(p_from, to);
        }
        return intact() && p_record->get_stage() == to;
    };
    {
        NETW_ZONE_NC("entity enter identity", colors::LIVENESS);
        rename_reserved_to_identity(owner, p_record->get_entity_id());
    }
    const bool is_reparent
        = p_record->get_stage() == int64_t(entity::Stage::LIVE);
    {
        NETW_ZONE_NC("entity enter hydrate identity", colors::LIVENESS);
        p_record->hydrate_identity(owner);
    }

    if (!is_reparent
        && p_record->get_stage() == int64_t(entity::Stage::UNBOUND)) {
        NETW_ZONE_NC("entity enter arm", colors::LIVENESS);
        NetwEntity *armed = Object::cast_to<NetwEntity>(p_wrapper);
        if (armed != nullptr) {
            armed->arm(Ref<NetwMultiplayer>());
            if (!intact()
                || p_record->get_stage() != int64_t(entity::Stage::ARMED)) {
                return;
            }
        }
    }

    if (!is_reparent && p_record->get_route() == 0 && p_session != nullptr
        && p_is_authority) {
        p_record->set_route(p_session->liveness_allocate_route(p_wrapper));
    }
    if (p_record->get_entity_id() == StringName()
        && p_record->get_route() > 0) {
        p_record->set_entity_id(
            spawn::Book::default_entity_id(
                nullptr,
                owner,
                p_record->get_route()
            )
        );
    }
    if (p_record->get_route() > 0 && p_session != nullptr) {
        {
            NETW_ZONE_NC("entity enter bind route", colors::LIVENESS);
            p_session->liveness_bind_route(p_record->get_route(), p_wrapper);
        }
        if (p_is_authority) {
            NETW_ZONE_NC("entity enter nested candidate", colors::LIVENESS);
            p_session->spawn_note_nested_candidate(p_record->get_route());
        }
    }

    if (is_reparent) {
        NetwEntity *moved = Object::cast_to<NetwEntity>(p_wrapper);
        if (moved != nullptr) {
            moved->apply_control();
        }
    }

    {
        NETW_ZONE_NC("entity enter connect ready", colors::LIVENESS);
        const NetwEntity *readied = Object::cast_to<NetwEntity>(p_wrapper);
        const Callable ready(p_wrapper, StringName("_on_owner_ready"));
        if ((readied == nullptr || !readied->get_ready_fired())
            && !owner->is_connected(StringName("ready"), ready)) {
            owner
                ->connect(StringName("ready"), ready, Object::CONNECT_ONE_SHOT);
        }
    }

    if (!is_reparent) {
        handle = p_record->get_handle();
        if (wrapper != nullptr) {
            stamped = wrapper->get_multiplayer();
        }
        const int64_t from = p_record->get_stage();
        {
            NETW_ZONE_NC("entity enter transition live", colors::LIVENESS);
            if (!p_record->transition(int64_t(entity::Stage::LIVE), owner)) {
                return;
            }
        }
        if (!publish(from)) {
            return;
        }
        NETW_TRACE(sys::ENTITY, "'%s' is live", owner->get_name());
        if (p_session != nullptr) {
            NETW_ZONE_NC("entity enter spawning event", colors::LIVENESS);
            p_session->event_emit(
                EventPlane::SPAWNING,
                p_record->get_route(),
                Dictionary(),
                p_record->get_entity_id(),
                p_record->get_peer_id(),
                OK,
                Dictionary()
            );
        }
        if (!intact()
            || p_record->get_stage() != int64_t(entity::Stage::LIVE)) {
            return;
        }
        if (p_record->get_declares_scene() && p_session != nullptr) {
            p_session->scene_root_online(owner);
        }
        spawn::Pipeline *pipeline
            = p_session != nullptr ? p_session->spawn_plane() : nullptr;
        if (wrapper != nullptr && pipeline != nullptr
            && pipeline->is_booked(p_record->get_route())) {
            p_session->membership_place_entity(Ref<NetwEntity>(wrapper));
        }
    }
}

void NetwMultiplayer::entity_demote_placed() {
    spawn::Pipeline *pipeline = spawn_plane();
    if (is_host() || pipeline == nullptr) {
        return;
    }
    const auto booked_at = [pipeline](Node *p_node) {
        const NetwEntity *held
            = Object::cast_to<NetwEntity>(tree::entity_object_at(p_node));
        return held != nullptr && held->get_route() > 0
            && pipeline->is_booked(held->get_route());
    };
    LocalVector<Ref<NetwEntity>> placed;
    for (const KeyValue<uint64_t, Ref<NetwEntity>> &held : entity_book) {
        const Ref<NetwEntity> &entity = held.value;
        Node *owner = entity.is_valid() ? entity->get_owner() : nullptr;
        if (owner == nullptr || entity->get_route() <= 0
            || entity->get_stage() != int64_t(entity::Stage::LIVE)) {
            continue;
        }
        bool booked = false;
        for (Node *at = owner; at != nullptr && !booked;
             at = at->get_parent()) {
            booked = booked_at(at);
        }
        if (!booked) {
            placed.push_back(entity);
        }
    }
    for (const Ref<NetwEntity> &entity : placed) {
        Node *owner = entity->get_owner();
        const int64_t route = entity->get_route();
        liveness_core->unbind_route(entity->get_rid_handle());
        if (entity->get_name()
            == spawn::Book::default_entity_id(nullptr, owner, route)) {
            entity->set_name(StringName());
        }
        entity->set_route(0);
    }
}

void NetwMultiplayer::entity_publish_placed() {
    if (!is_host()) {
        return;
    }
    LocalVector<Ref<NetwEntity>> live;
    for (const KeyValue<uint64_t, Ref<NetwEntity>> &held : entity_book) {
        if (held.value.is_valid()
            && held.value->get_stage() == int64_t(entity::Stage::LIVE)) {
            live.push_back(held.value);
        }
    }
    for (const Ref<NetwEntity> &entity : live) {
        Node *owner = entity->get_owner();
        if (owner == nullptr || !owner->is_inside_tree()) {
            continue;
        }
        int64_t route = entity->get_route();
        if (route == 0) {
            route = liveness_allocate_route(entity.ptr());
            entity->set_route(route);
        }
        if (route <= 0) {
            continue;
        }
        if (entity->get_name() == StringName()) {
            entity->set_name(
                spawn::Book::default_entity_id(nullptr, owner, route)
            );
        }
        if (spawn::Pipeline *pipeline = spawn_plane()) {
            pipeline->note_placed_before_host(route);
        }
        spawn_note_nested_candidate(route);
    }
}

Node *NetwMultiplayer::entity_component_node(
    const RID &p_entity,
    int64_t p_comp
) const {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return nullptr;
    }
    return wrapper->comp_node_of(p_comp);
}

RID NetwMultiplayer::spawn_fn(
    const Callable &p_function,
    const Array &p_args,
    NetwPlayer *p_owner
) {
    NETW_ZONE_NC("session spawn function", colors::LIVENESS);
    spawn::Pipeline *pipeline = spawn_plane();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "Netw.spawn did nothing, because replication is not running on this "
        "peer."
    );
    Object *node
        = pipeline->spawn(p_function, p_args, Ref<NetwPlayer>(p_owner));
    return node != nullptr ? entity_of(node) : RID();
}

void NetwMultiplayer::spawn_register_constructor(
    const StringName &p_id,
    const Callable &p_function,
    const Array &p_arg_types,
    const Array &p_quantizers
) {
    spawn::Pipeline *pipeline = spawn_plane();
    NETW_ERR_COND(
        pipeline == nullptr,
        sys::SPAWN,
        "Spawn constructor '%s' was not registered, because replication is "
        "not running on this peer.",
        String(p_id)
    );
    pipeline->register_spawn_constructor(
        p_id,
        p_function,
        p_arg_types,
        p_quantizers
    );
}

RID NetwMultiplayer::spawn_registered(
    const StringName &p_id,
    const Array &p_args,
    Object *p_owner
) {
    NETW_ZONE_NC("session spawn registered", colors::LIVENESS);
    spawn::Pipeline *pipeline = spawn_plane();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "Spawn constructor '%s' did nothing, because replication is not "
        "running on this peer.",
        String(p_id)
    );
    Object *node = pipeline->spawn_registered(
        p_id,
        p_args,
        Ref<NetwPlayer>(Object::cast_to<NetwPlayer>(p_owner))
    );
    return node != nullptr ? entity_of(node) : RID();
}

RID NetwMultiplayer::entity_adopt(Object *p_root) {
    NETW_ZONE_NC("session spawn adopt", colors::LIVENESS);
    spawn::Pipeline *pipeline = spawn_plane();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "A node could not be adopted, because replication is not running on "
        "this peer."
    );
    const Ref<NetwEntity> wrapper
        = pipeline->adopt_in_place(Object::cast_to<Node>(p_root));
    return wrapper.is_valid() ? wrapper->get_rid_handle() : RID();
}

TypedArray<Dictionary> NetwMultiplayer::spawn_get_state(const RID &p_entity) {
    spawn::Pipeline *pipeline = spawn_plane();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        TypedArray<Dictionary>(),
        sys::SPAWN,
        "Spawn state was read while replication is not running on this "
        "peer."
    );
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    Node *owner = wrapper.is_valid() ? wrapper->get_owner() : nullptr;
    if (owner == nullptr) {
        return TypedArray<Dictionary>();
    }
    return pipeline->collect_spawn_state(owner);
}

PropertyBindingPtr NetwMultiplayer::entity_derived_binding(
    Node *p_owner,
    int64_t p_record,
    int64_t p_route
) {
    ReplicationCore *plane = get_replication_plane();
    if (p_owner == nullptr || plane == nullptr) {
        return PropertyBindingPtr();
    }
    const PropertyBindingPtr declared
        = plane->derived_binding(p_owner, p_record);
    if (declared != nullptr) {
        return declared;
    }
    return entity_group_binding(p_record, p_route);
}

PropertyBindingPtr NetwMultiplayer::entity_record_binding(
    NetwEntityRecord *p_held,
    Node *p_owner,
    int64_t p_record,
    int64_t p_route
) {
    if (p_owner == nullptr || get_replication_plane() == nullptr) {
        return nullptr;
    }
    PropertyBindingPtr seated = p_held != nullptr
        ? binding_book.live(p_held->get_record_binding(p_record))
        : nullptr;
    if (seated == nullptr) {
        seated = entity_group_binding(p_record, p_route);
    }
    static const bool verify
        = std::getenv("NETW_VERIFY_BINDING_INDEX") != nullptr;
    if (verify) {
        const PropertyBindingPtr scanned
            = entity_derived_binding(p_owner, p_record, p_route);
        if (scanned != seated) {
            NETW_ERROR(
                sys::WIRE,
                "Record %d of '%s' points at a different node than the "
                "entity holds.",
                p_record,
                String(p_owner->get_name())
            );
            return scanned;
        }
    }
    return seated;
}

PropertyBindingPtr NetwMultiplayer::entity_group_binding(
    int64_t p_record,
    int64_t p_route
) {
    if (p_route <= 0) {
        return PropertyBindingPtr();
    }
    NETW_ZONE_NC("entity derived group fallback", colors::WIRE);
    const LocalVector<PropertyBindingPtr> group = entity_derived_group(p_route);
    for (const PropertyBindingPtr &candidate : group) {
        if (candidate == nullptr || candidate->get_set() == nullptr) {
            continue;
        }
        if (candidate->get_set()->get_record() == p_record) {
            return candidate;
        }
    }
    return PropertyBindingPtr();
}

LocalVector<PropertyBindingPtr> NetwMultiplayer::entity_derived_group(
    int64_t p_route
) {
    ReplicationCore *plane = get_replication_plane();
    if (plane == nullptr || p_route <= 0) {
        return LocalVector<PropertyBindingPtr>();
    }
    return plane->derived_group(p_route);
}

bool NetwMultiplayer::entity_governs_property(
    Node *p_owner,
    const NodePath &p_path,
    Node *p_exclude,
    int64_t p_route
) {
    Node *owner = p_owner;
    if (owner == nullptr || p_path.is_empty()) {
        return false;
    }
    const gd::NodeProperty target = gd::node_property(owner, p_path);
    if (!target.is_property()) {
        return false;
    }
    const TypedArray<MultiplayerSynchronizer> streams
        = synchronizers::of_node(owner);
    for (int at = 0; at < streams.size(); at++) {
        Object *stream = streams[at];
        if (stream == nullptr || stream == p_exclude) {
            continue;
        }
        const Array governed = synchronizers::governed_targets(stream, owner);
        for (int row = 0; row < governed.size(); row++) {
            const Array pair = governed[row];
            Object *held = pair[0];
            if (held == target.object && NodePath(pair[1]) == target.sub) {
                return true;
            }
        }
    }
    const LocalVector<PropertyBindingPtr> group = entity_derived_group(p_route);
    for (const PropertyBindingPtr &binding : group) {
        if (binding == nullptr || binding->node() != target.object) {
            continue;
        }
        const PropertySet *declaration = binding->get_set();
        if (declaration == nullptr) {
            continue;
        }
        for (const PropertyColumn &column : declaration->columns) {
            if (NodePath(String(":") + String(column.get_key()))
                == target.sub) {
                return true;
            }
        }
    }
    return false;
}

Dictionary NetwMultiplayer::entity_describe(int64_t p_route) const {
    Dictionary out;
    const RID entity = liveness_core->rid_from_route(int(p_route));
    NetwEntityRecord *const record = slot_record(entity.get_id());
    if (record == nullptr) {
        return out;
    }
    out["route"] = p_route;
    out["entity_id"] = record->get_entity_id();
    out["peer_id"] = record->get_peer_id();
    out["stage"] = record->get_stage();
    out["controller"] = entity::Control::public_peer(
        record->get_control()->resolve()
    );
    out["liveness"] = liveness_route_state(p_route);
    out["layers"] = interest_engine.memberships(entity.get_id());
    return out;
}

void NetwMultiplayer::entity_note_stage(
    NetwEntityRecord *p_record,
    int64_t p_from,
    int64_t p_to
) {
    if (p_record == nullptr || p_to == p_from) {
        return;
    }
    if (entity::stage_is_leaving(p_to)) {
        kept_entities.erase(p_record->get_handle().get_id());
        kept_by_server.erase(p_record->get_handle().get_id());
    }
    const int64_t route = p_record->get_route();
    if (!plane.wants(EventPlane::STAGE_TRANSITION, route)) {
        return;
    }
    Dictionary detail;
    detail["from"] = p_from;
    detail["to"] = p_to;
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

void NetwMultiplayer::entity_announce_reparented(
    const Ref<NetwEntity> &p_wrapper
) {
    if (p_wrapper.is_null()) {
        return;
    }
    Node *owner = p_wrapper->get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    NetwEntityRecord *const record = p_wrapper->get_record();
    const int64_t route = record != nullptr ? record->get_route() : 0;
    if (plane.wants(EventPlane::REPARENTED, route)) {
        event_emit(
            EventPlane::REPARENTED,
            route,
            Dictionary(),
            record != nullptr ? record->get_entity_id() : StringName(),
            record != nullptr ? record->get_peer_id() : 0,
            OK,
            Dictionary()
        );
    }
    p_wrapper->emit_signal(StringName("reparented"));
}

void NetwMultiplayer::entity_linger(
    NetwEntityRecord *p_record,
    Node *p_owner,
    int64_t p_pumps
) {
    Node *owner = p_owner;
    if (p_record == nullptr || owner == nullptr) {
        return;
    }
    p_record->deactivate(owner);
    NETW_TRACE(sys::ENTITY, "Staying for %d more updates.", int(p_pumps));
    session_defer_after(
        callable_mp(this, &NetwMultiplayer::entity_free_owner)
            .bind(uint64_t(gd::instance_id(owner))),
        StringName(),
        int(p_pumps)
    );
}

Ref<NetwEntity> NetwMultiplayer::entity_get_view(const RID &p_entity) const {
    const EntitySlot *slot = entity_slots.find(p_entity.get_id());
    return slot != nullptr ? slot->live : Ref<NetwEntity>();
}

Ref<NetwEntity> NetwMultiplayer::wrapper_for_route(int64_t p_route) const {
    return entity_get_view(liveness_core->rid_from_route(int(p_route)));
}

Ref<NetwEntity> NetwMultiplayer::wrapper_for_id(int64_t p_id) const {
    const EntitySlot *slot = entity_slots.find(uint64_t(p_id));
    if (slot == nullptr) {
        return Ref<NetwEntity>();
    }
    return slot->live.is_valid() ? slot->live : slot->retired;
}

void NetwMultiplayer::entity_settle_peer_departure(int64_t p_peer) {
    LocalVector<Ref<NetwEntity>> settling;
    settling.reserve(entity_slots.size());
    entity_slots.each([&settling](uint64_t, const EntitySlot &p_slot) {
        if (p_slot.live.is_valid()) {
            settling.push_back(p_slot.live);
        }
    });
    const bool admitted = halt_admits_despawn;
    halt_admits_despawn = true;
    for (uint32_t at = 0; at < settling.size(); at++) {
        if (settling[at].is_valid()) {
            settling[at]->_on_peer_disconnected(p_peer);
        }
    }
    halt_admits_despawn = admitted;
}

TypedArray<NetwEntity> NetwMultiplayer::wrapper_live() const {
    TypedArray<NetwEntity> out;
    const PackedInt32Array routes = liveness_core->live_routes();
    for (int at = 0; at < routes.size(); at++) {
        const Ref<NetwEntity> wrapper = wrapper_for_route(routes[at]);
        if (wrapper.is_valid()) {
            out.push_back(wrapper);
        }
    }
    return out;
}

void NetwMultiplayer::wrapper_sweep_retired() {
    LocalVector<int64_t> retired;
    entity_slots.each([&retired](uint64_t p_id, const EntitySlot &p_slot) {
        if (p_slot.retired.is_valid()) {
            retired.push_back(int64_t(p_id));
        }
    });
    for (const int64_t id : retired) {
        EntitySlot *slot = entity_slots.find(uint64_t(id));
        const Ref<NetwEntity> wrapper = slot->retired;
        if (slot->record == nullptr) {
            wrapper_unindex_owner(id, wrapper.ptr());
            slot = entity_slots.find(uint64_t(id));
        }
        if (slot != nullptr) {
            slot->retired = Ref<NetwEntity>();
            slot_settle(id);
        }
    }
}

void NetwMultiplayer::wrapper_unindex_owner(
    int64_t p_id,
    const NetwEntity *p_wrapper
) {
    EntitySlot *slot = entity_slots.find(uint64_t(p_id));
    if (slot == nullptr) {
        return;
    }
    if (slot->owner.is_valid()) {
        const ObjectID owner = slot->owner;
        slot->owner = ObjectID();
        script::model::clear_node_overlay(
            Object::cast_to<Node>(gd::instance_from_id(owner))
        );
        slot = entity_slots.find(uint64_t(p_id));
    }
    if (slot != nullptr && p_wrapper != nullptr
        && slot->indexed == gd::instance_id(p_wrapper)) {
        slot->indexed = ObjectID();
    }
    slot_settle(p_id);
}

void NetwMultiplayer::wrapper_clear() {
    sim_rows.each(sim_scan);
    for (sim::Row *row : sim_scan) {
        row->view = nullptr;
    }
    entity_slots.clear();
}

#if defined(NETW_TESTS)
NetwMultiplayer::BookSizes NetwMultiplayer::book_sizes() const {
    BookSizes sizes;
    entity_slots.each([&sizes](uint64_t, const EntitySlot &p_slot) {
        sizes.wrapper_owners += p_slot.owner.is_valid() ? 1 : 0;
        sizes.handle_by_wrapper += p_slot.indexed.is_valid() ? 1 : 0;
    });
    sizes.attributed_routes = attribution.attributed_route_count();
    sizes.node_overlays = script::model::node_overlay_count();
    sizes.wrapper_slots = int64_t(entity_slots.size());
    sizes.liveness_routes = liveness_core->route_count();
    sizes.display_rows = display_book.is_valid() ? display_book->size() : 0;
    sizes.layer_views = int64_t(layer_views.size());
    sizes.engine_layers = interest_engine.layer_count();
    sizes.attribution_peer_keys = attribution.peer_keyed_count();
    sizes.lagcomp_timelines = lagcomp_core.timeline_registered();
    return sizes;
}
#endif

bool NetwMultiplayer::liveness_bind(
    const RID &p_entity,
    int64_t p_route,
    const Ref<NetwEntity> &p_wrapper,
    NetwEntityRecord *p_record,
    Node *p_owner
) {
    if (!liveness_core->adopt(p_entity)) {
        return false;
    }
    if (!liveness_core->bind_route(p_entity, int(p_route))) {
        return false;
    }
    wrapper_adopt(p_entity, p_wrapper, p_owner);
    if (p_record != nullptr) {
        entity_slots.ensure(p_entity.get_id()).record = p_record;
    }
    return true;
}

Error NetwMultiplayer::liveness_adopt_route(
    int64_t p_route,
    const Ref<RefCounted> &p_wrapper,
    NetwEntityRecord *p_record,
    Node *p_owner
) {
    if (p_route <= 0 || p_wrapper.is_null() || p_record == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    const RID held = liveness_core->rid_from_route(int(p_route));
    if (!p_record->get_handle().is_valid() && !held.is_valid()) {
        p_record->adopt_handle(liveness_core->entity_create());
    }
    if (held.is_valid()) {
        const NetwLivenessCore::State held_state
            = liveness_core->state_of(held);
        const bool absent = held_state == NetwLivenessCore::STATE_ABSENT;
        const Ref<NetwEntity> bound = entity_get_view(held);
        if (bound == p_wrapper && !absent
            && held_state != NetwLivenessCore::STATE_DEAD) {
            return ERR_ALREADY_EXISTS;
        }
        if (held != p_record->get_handle()) {
            if (!absent
                && (bound.is_valid()
                    || liveness_core->entity_is_valid(
                        p_record->get_handle()
                    ))) {
                NETW_TRACE(
                    sys::LIVENESS,
                    "Route %d already belongs to another entity.",
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

void NetwMultiplayer::liveness_publish_live(int64_t p_route) {
    NETW_ZONE_NC("liveness publish live", colors::LIVENESS);
    Ref<NetwEntity> wrapper;
    NetwEntityRecord *record = nullptr;
    {
        NETW_ZONE_NC("liveness resolve wrapper", colors::LIVENESS);
        wrapper = wrapper_for_route(p_route);
        record = record_of_wrapper(wrapper.ptr());
    }
    if (record != nullptr) {
        NETW_ZONE_NC("liveness emit spawned event", colors::LIVENESS);
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
    {
        NETW_ZONE_NC("liveness emit entity live", colors::LIVENESS);
        if (SyncPipeline *pipeline = sync_pipeline()) {
            if (pipeline->hears_entity_live) {
                sync_pipeline_on_entity_live(p_route, wrapper);
            }
        }
        if (SyncCompat *adapter = sync_adapter()) {
            if (adapter->hears_entity_live) {
                sync_compat_entity_live(p_route, wrapper);
            }
        }
        scene_on_entity_live(p_route, wrapper);
        if (display_session_bound) {
            display_on_entity_live(p_route, wrapper.ptr());
        }
        if (spawn::Pipeline *spawning = spawn_plane()) {
            if (spawning->retries_scene_parks()) {
                spawn_retry_scene_parked_spawns(p_route, wrapper);
            }
            if (spawning->applies_action_gate()) {
                spawn_apply_action_gate(p_route, wrapper);
            }
        }
        notify(ENTITY_LIVE, p_route, wrapper);
    }
}

bool NetwMultiplayer::liveness_linger(const RID &p_entity) {
    if (!liveness_core
             ->set_state(p_entity, NetwLivenessCore::STATE_LINGERING)) {
        return false;
    }
    const int64_t route = int64_t(liveness_core->route_of(p_entity));
    const Ref<NetwEntity> view = entity_get_view(p_entity);
    notify(ENTITY_LINGERING, route, view);
    return true;
}

void NetwMultiplayer::liveness_unindex_wrapper(const RID &p_entity) {
    const int64_t id = p_entity.get_id();
    EntitySlot *slot = entity_slots.find(uint64_t(id));
    if (slot != nullptr && slot->live.is_valid()) {
        slot->retired = slot->live;
        slot->live = Ref<NetwEntity>();
        sim_follow_view(p_entity, nullptr);
    }
}

void NetwMultiplayer::liveness_forget_wrapper(const RID &p_entity) {
    const int64_t id = p_entity.get_id();
    EntitySlot *slot = entity_slots.find(uint64_t(id));
    if (slot == nullptr) {
        return;
    }
    slot->record = nullptr;
    if (slot->live.is_valid()) {
        return;
    }
    const Ref<NetwEntity> retired = slot->retired;
    wrapper_unindex_owner(id, retired.ptr());
}

void NetwMultiplayer::liveness_release(int64_t p_route) {
    const RID entity = liveness_core->rid_from_route(int(p_route));
    liveness_core->set_state(entity, NetwLivenessCore::STATE_DEAD);
    liveness_core->fail_live(int(p_route));
    liveness_unindex_wrapper(entity);
}

void NetwMultiplayer::liveness_announce_dead(int64_t p_route) {
    const RID entity = liveness_core->rid_from_route(int(p_route));
    replication_clear_route(p_route);
    display_retire_route(p_route);
    attribution.retire_route(p_route);
    event_emit(
        EventPlane::DESPAWNED,
        p_route,
        Dictionary(),
        StringName(),
        0,
        OK,
        Dictionary()
    );
    notify(ENTITY_DEAD, p_route);
    liveness_forget_wrapper(entity);
}

bool NetwMultiplayer::liveness_retire(int64_t p_route) {
    liveness_release(p_route);
    liveness_announce_dead(p_route);
    return true;
}

bool NetwMultiplayer::liveness_hide(int64_t p_route) {
    const RID entity = liveness_core->rid_from_route(int(p_route));
    if (!liveness_core->hide_route(int(p_route))) {
        return false;
    }
    const Ref<NetwEntity> wrapper = entity_get_view(entity);
    if (wrapper.is_valid()) {
        wrapper->watch_despawning(false);
    }
    entity_release_body(entity, wrapper, p_route, true);
    liveness_unindex_wrapper(entity);
    event_emit(
        EventPlane::HIDDEN,
        p_route,
        Dictionary(),
        StringName(),
        0,
        OK,
        Dictionary()
    );
    notify(ENTITY_HIDDEN, p_route, wrapper);
    liveness_forget_wrapper(entity);
    return true;
}

NetwEntityRecord *NetwMultiplayer::record_of_wrapper(Object *p_wrapper) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    return entity == nullptr ? nullptr : entity->get_record();
}

Node *NetwMultiplayer::owner_of_wrapper(Object *p_wrapper) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr) {
        return nullptr;
    }
    return entity->get_owner();
}

int64_t NetwMultiplayer::liveness_reserve_route() {
    return liveness_core->reserve_route();
}

int64_t NetwMultiplayer::liveness_lease_remaining() const {
    return int64_t(liveness_core->lease_remaining());
}

void NetwMultiplayer::liveness_grant_lease(int64_t p_peer) {
    if (structure_halted) {
        if (!halt_leases.has(p_peer)) {
            halt_leases.push_back(p_peer);
        }
        return;
    }
    const int base = liveness_core->grant_lease(int(p_peer), ROUTE_LEASE_BLOCK);
    if (base == 0) {
        return;
    }
    session::RouteLease granted;
    granted.base = uint64_t(base);
    granted.count = uint64_t(ROUTE_LEASE_BLOCK);
    send_to(
        p_peer,
        0,
        route_lease_channel,
        session::frame_write(granted),
        true,
        0,
        String(),
        false
    );
}

Error NetwMultiplayer::liveness_spend_lease(int64_t p_peer, int64_t p_route) {
    if (!is_host()) {
        return ERR_UNAUTHORIZED;
    }
    if (!liveness_core->spend_lease(int(p_peer), int(p_route))) {
        return ERR_UNAUTHORIZED;
    }
    if (liveness_core->granted_remaining(int(p_peer))
        <= ROUTE_LEASE_REFILL_AT) {
        liveness_grant_lease(p_peer);
    }
    return OK;
}

void NetwMultiplayer::liveness_receive_lease(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != SERVER_PEER) {
        return;
    }
    session::RouteLease granted;
    if (!session::frame_read(p_payload, granted) || granted.base == 0
        || granted.count == 0) {
        return;
    }
    liveness_core->install_lease(int(granted.base), int(granted.count));
}

int64_t NetwMultiplayer::liveness_allocate_route(Object *p_wrapper) {
    const int64_t standing = liveness_route_of(p_wrapper);
    if (standing > 0) {
        return standing;
    }
    const int64_t route = liveness_core->reserve_route();
    return liveness_bind_route(route, p_wrapper) ? route : 0;
}

bool NetwMultiplayer::liveness_bind_route(int64_t p_route, Object *p_wrapper) {
    NetwEntityRecord *const record = record_of_wrapper(p_wrapper);
    if (record == nullptr) {
        return false;
    }
    const Ref<RefCounted> wrapper(p_wrapper);
    Node *owner = owner_of_wrapper(p_wrapper);
    const Error verdict = liveness_adopt_route(p_route, wrapper, record, owner);
    if (verdict == ERR_ALREADY_EXISTS) {
        return true;
    }
    if (verdict != OK) {
        return false;
    }

    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (owner != nullptr && entity != nullptr) {
        entity->watch_despawning(true);
    }

    liveness_publish_live(p_route);
    liveness_core->flush_live(int(p_route));
    return true;
}

void NetwMultiplayer::liveness_bind_routes_data(
    const PackedInt64Array &p_routes
) {
    liveness_core->bind_routes_data(p_routes);
}

void NetwMultiplayer::liveness_tombstone_routes_data(
    const PackedInt64Array &p_routes
) {
    liveness_core->tombstone_routes_data(p_routes);
}

int64_t NetwMultiplayer::liveness_route_of(Object *p_wrapper) const {
    NetwEntityRecord *const record = record_of_wrapper(p_wrapper);
    if (record == nullptr) {
        return 0;
    }
    return liveness_core->route_of(record->get_handle());
}

int64_t NetwMultiplayer::liveness_state_of(NetwEntity *p_wrapper) const {
    NetwEntityRecord *const record = record_of_wrapper(p_wrapper);
    if (record == nullptr) {
        return int64_t(NetwLivenessCore::STATE_UNKNOWN);
    }
    return int64_t(liveness_core->state_of(record->get_handle()));
}

NetwMultiplayer::EntityState NetwMultiplayer::liveness_route_state(
    int64_t p_route
) const {
    return EntityState(liveness_core->route_state(int(p_route)));
}

int64_t NetwMultiplayer::liveness_route_epoch(int64_t p_route) const {
    return int64_t(liveness_core->route_epoch(int(p_route)));
}

uint64_t NetwMultiplayer::liveness_route_anchor(int64_t p_route) const {
    return liveness_core->route_anchor(int(p_route));
}

NetwMultiplayer::AnchorRevision NetwMultiplayer::anchor_installed(
    int64_t p_route
) const {
    AnchorRevision installed;
    installed.revision = liveness_core->route_anchor(int(p_route));
    installed.author = liveness_core->route_anchor_author(int(p_route));
    return installed;
}

uint64_t NetwMultiplayer::entity_advance_anchor(int64_t p_route) {
    const uint64_t held = liveness_route_anchor(p_route);
    if (held == 0) {
        return 0;
    }
    entity_install_anchor(p_route, held + 1, uint64_t(get_unique_id()));
    return held + 1;
}

void NetwMultiplayer::entity_install_anchor(
    int64_t p_route,
    uint64_t p_anchor,
    uint64_t p_author
) {
    if (!liveness_core->set_route_anchor(int(p_route), p_anchor, p_author)) {
        return;
    }
    row_streams_follow_anchor(p_route, p_anchor, p_author);
}

int64_t NetwMultiplayer::liveness_route_wire_life(int64_t p_route) const {
    return int64_t(liveness_core->route_wire_epoch(int(p_route)));
}

bool NetwMultiplayer::liveness_epoch_admits(
    int64_t p_route,
    int64_t p_epoch
) const {
    return liveness_core->epoch_admits(int(p_route), int(p_epoch));
}

bool NetwMultiplayer::liveness_adopt_epoch(int64_t p_route, int64_t p_epoch) {
    return liveness_core->adopt_epoch(int(p_route), int(p_epoch));
}

void NetwMultiplayer::liveness_raise_epoch_to_wire(int64_t p_route) {
    liveness_core->raise_epoch_to_wire(int(p_route));
}

Error NetwMultiplayer::entity_frame_verdict(int64_t p_route) const {
    const EntityState state = liveness_route_state(p_route);
    if (state == ENTITY_STATE_UNKNOWN) {
        return ERR_DOES_NOT_EXIST;
    }
    if (state == ENTITY_STATE_LINGERING || state == ENTITY_STATE_DEAD) {
        return ERR_SKIP;
    }
    const Ref<NetwEntity> entity
        = Object::cast_to<NetwEntity>(wrapper_for_route(p_route).ptr());
    if (entity.is_null() || entity->get_owner() == nullptr) {
        return ERR_UNAVAILABLE;
    }
    return OK;
}

RID NetwMultiplayer::liveness_adopt(NetwEntity *p_wrapper) {
    NetwEntityRecord *const record = record_of_wrapper(p_wrapper);
    if (record == nullptr || !record->get_handle().is_valid()) {
        return RID();
    }
    const RID handle = record->get_handle();
    if (liveness_core->state_of(handle) == NetwLivenessCore::STATE_DEAD) {
        return handle;
    }
    wrapper_adopt(
        handle,
        Ref<NetwEntity>(p_wrapper),
        owner_of_wrapper(p_wrapper)
    );
    return handle;
}

RID NetwMultiplayer::entity_of(Object *p_node) {
    return liveness_adopt(wrapper_at(p_node).ptr());
}

Node *NetwMultiplayer::liveness_node_of(int64_t p_route) const {
    return wrapper_owner(liveness_core->rid_from_route(int(p_route)));
}

TypedArray<NetwEntity> NetwMultiplayer::liveness_get_entities() const {
    return wrapper_live();
}

void NetwMultiplayer::liveness_schedule_when_live(
    int64_t p_route,
    const Callable &p_callback,
    int64_t p_origin,
    int64_t p_timeout,
    bool p_on_clock,
    const Callable &p_on_timeout
) {
    liveness_core->when_live(
        int(p_route),
        p_callback,
        int(p_origin),
        int(p_timeout),
        p_on_clock,
        p_on_timeout
    );
}

int64_t NetwMultiplayer::liveness_pending_live_count() const {
    return liveness_core->pending_live_count();
}

void NetwMultiplayer::liveness_poll(int64_t p_clock_tick) {
    const PackedInt32Array expired = liveness_core->poll(int(p_clock_tick));
    for (int at = 0; at < expired.size(); at++) {
        NETW_WARN(
            sys::LIVENESS,
            "Route %d never became live, so its "
            "NetwMultiplayer.liveness_when_live callback was dropped.",
            expired[at]
        );
    }
}

void NetwMultiplayer::liveness_poll_now() {
    NETW_ZONE_NC("Liveness poll", colors::LIVENESS);
    liveness_poll(
        clock_engine().get_configured() ? clock_engine().get_ticks_run() : 0
    );
}

void NetwMultiplayer::liveness_clear_session() {
    display_clear_runtimes();
    LocalVector<Ref<NetwEntity>> predicted;
    predicted.reserve(predict_engines.size());
    for (const KeyValue<int64_t, NetwPredictSlotEngine *> &row :
         predict_engines) {
        predicted.push_back(row.value->seated_entity());
    }
    for (const Ref<NetwEntity> &entity : predicted) {
        unregister_prediction(entity);
    }
    entity_departures.clear();
    entity_generation += 1;
    liveness_core->clear();
    wrapper_clear();
}

void NetwMultiplayer::liveness_settle_clear_session() {
    session_defer(
        callable_mp(this, &NetwMultiplayer::liveness_clear_session),
        StringName("liveness-clear-session")
    );
}

void NetwMultiplayer::entity_capture_exit(Object *p_wrapper) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr) {
        return;
    }
    persist_capture_exit(entity->get_owner());
    if (entity->get_declares_scene()) {
        scene_root_offline(entity->get_owner());
    }
    const uint64_t instance = uint64_t(entity->get_instance_id());
    EntityDeparture *standing = entity_departures.getptr(instance);
    EntityDeparture fresh;
    EntityDeparture &row = standing != nullptr ? *standing : fresh;
    if (standing == nullptr) {
        row.applied = applying_remote_frame;
        Node *owner = entity->get_owner();
        row.parent_id
            = gd::instance_id(owner != nullptr ? owner->get_parent() : nullptr);
    } else {
        row.applied = row.applied && applying_remote_frame;
    }
    row.entity = entity->get_rid_handle();
    row.wrapper = Ref<NetwEntity>(entity);
    row.owner_id = gd::instance_id(entity->get_owner());
    row.generation = entity_generation;
    row.route = liveness_route_of(p_wrapper);
    row.terminal
        = row.terminal || !entity::stage_can_begin_despawn(entity->get_stage());
    row.hidden = row.hidden
        || (row.route > 0
            && liveness_route_state(row.route) == ENTITY_STATE_ABSENT);
    const bool admitted_here
        = applying_remote_frame && admitting_route == row.route;
    row.author = admitted_here ? admitting_author : int64_t(0);
    if (row.route > 0) {
        if (ReplicationCore *plane = get_replication_plane()) {
            spawn::Pipeline *spawns = plane->get_spawn_pipeline();
            row.received = spawns->holds_received_route(row.route);
            if (!row.terminal && !admitted_here
                && spawns->holds_spawned_route(row.route)) {
                entity_advance_anchor(row.route);
            } else if (
                !row.terminal && !row.minted && row.received
                && !applying_remote_frame && lifecycle_authors_move(entity)
            ) {
                const AnchorRevision base = anchor_installed(row.route);
                row.base = base.revision;
                row.base_author = base.author;
                row.minted = entity_advance_anchor(row.route) > 0;
                if (const HashMap<int64_t, Ref<NetwPromise>>::Iterator waiting
                    = lifecycle_promises.find(row.route)) {
                    row.promise = waiting->value;
                    lifecycle_promises.remove(waiting);
                }
            }
        }
    }
    entity_capture_residency(row, entity);
    if (standing == nullptr) {
        entity_departures.insert(instance, row);
    }
    session_defer(
        callable_mp(this, &NetwMultiplayer::entity_settle_departure)
            .bind(int64_t(instance)),
        StringName(vformat("entity-departure?%d", int64_t(instance)))
    );
}

void NetwMultiplayer::entity_capture_residency(
    EntityDeparture &r_row,
    NetwEntity *p_entity
) {
    const int64_t peer = p_entity->get_peer_id();
    if (peer == 0) {
        return;
    }
    const RID leaving = scene_of(r_row.entity);
    if (!leaving.is_valid() || leaving == r_row.entity) {
        return;
    }
    for (uint32_t at = 0; at < r_row.residencies.size(); at++) {
        if (r_row.residencies[at].scene == leaving
            && r_row.residencies[at].peer == peer) {
            return;
        }
    }
    DepartedResidency resided;
    resided.scene = leaving;
    resided.peer = peer;
    r_row.residencies.push_back(resided);
}

entity::Outcome NetwMultiplayer::entity_departure_outcome(
    const EntityDeparture &p_row
) const {
    entity::Settlement settlement;
    settlement.terminal = p_row.terminal
        || (p_row.wrapper.is_valid()
            && !entity::stage_can_begin_despawn(p_row.wrapper->get_stage()));
    settlement.hidden = p_row.hidden;
    settlement.received = p_row.received;
    Node *owner = Object::cast_to<Node>(gd::object_of(p_row.owner_id));
    settlement.owner_live = owner != nullptr;
    settlement.owner_in_tree = owner != nullptr && owner->is_inside_tree();
    settlement.session_current
        = p_row.generation == entity_generation && p_row.wrapper.is_valid();
    return entity::classify(settlement);
}

void NetwMultiplayer::entity_settle_departure(int64_t p_instance) {
    const Ref<NetwMultiplayer> keep(this);
    const uint64_t instance = uint64_t(p_instance);
    EntityDeparture *standing = entity_departures.getptr(instance);
    if (standing == nullptr) {
        return;
    }
    const EntityDeparture row = *standing;
    entity_departures.erase(instance);
#if defined(NETW_TESTS)
    membership_audit_held += 1;
#endif
    const entity::Outcome outcome = entity_departure_outcome(row);
    switch (outcome) {
        case entity::Outcome::MOVE:
            entity_commit_move(row);
            break;
        case entity::Outcome::DEATH:
            entity_commit_death(row);
            break;
        case entity::Outcome::HIDE:
            entity_commit_hide(row);
            break;
    }
    if (outcome != entity::Outcome::MOVE && row.promise.is_valid()
        && !row.promise->get_is_settled()) {
        row.promise->reject(
            ERR_UNAVAILABLE,
            String("the entity left the tree before its move was sent")
        );
    }
    persist_settle_departure(row.owner_id, outcome == entity::Outcome::DEATH);
#if defined(NETW_TESTS)
    membership_audit_held -= 1;
    membership_audit();
#endif
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->reconcile_dropped_state_timelines();
    }
}

void NetwMultiplayer::entity_release_residencies(
    const EntityDeparture &p_row,
    const RID &p_keep
) {
    for (uint32_t at = 0; at < p_row.residencies.size(); at++) {
        const DepartedResidency &resided = p_row.residencies[at];
        if (resided.scene == p_keep) {
            continue;
        }
        scene_release_departed(
            resided.scene,
            p_row.entity,
            p_row.wrapper.is_valid() && p_row.wrapper->get_owner() != nullptr,
            resided.peer
        );
    }
}

void NetwMultiplayer::entity_commit_move(const EntityDeparture &p_row) {
    if (p_row.route > 0 && !is_host()) {
        entity_settle_foreign_move(p_row);
    } else if (p_row.route > 0) {
        if (ReplicationCore *plane = get_replication_plane()) {
            plane->get_spawn_pipeline()->settle_move(p_row.route, p_row.author);
        }
        if (p_row.author != 0) {
            const uint64_t landed = liveness_route_anchor(p_row.route);
            lifecycle_decide(
                p_row.route,
                p_row.author,
                lifecycle::Kind::REPARENT,
                landed > 0 ? landed - 1 : 0,
                OK
            );
        }
    }
    entity_relocate_spatial_state(
        p_row,
        clock_engine().get_configured() ? clock_engine().get_tick() : 0
    );
    entity_refresh_moved_body(p_row.entity, p_row.route);
    entity_announce_reparented(p_row.wrapper);
}

void NetwMultiplayer::entity_relocate_spatial_state(
    const EntityDeparture &p_row,
    int64_t p_tick
) {
    Node *owner
        = p_row.wrapper.is_valid() ? p_row.wrapper->get_owner() : nullptr;
    lagcomp_core.timeline_trim_before(
        lagcomp_core.timeline_slot_of(entity_get_view(p_row.entity)),
        p_tick + 1
    );
    if (owner != nullptr) {
        owner->reset_physics_interpolation();
    }
}

void NetwMultiplayer::entity_refresh_moved_body(
    const RID &p_entity,
    int64_t p_route
) {
    if (NetwPredictSlotEngine *engine = predict_engine_for(p_entity)) {
        engine->on_reparented();
    }
    if (p_route > 0) {
        display_refresh_moved(p_route);
    }
}

void NetwMultiplayer::entity_commit_death(const EntityDeparture &p_row) {
    const bool routed = p_row.route > 0
        && liveness_core->route_state(int(p_row.route))
            != NetwLivenessCore::STATE_DEAD;
    if (routed) {
        if (ReplicationCore *plane = get_replication_plane()) {
            spawn::Pipeline *spawns = plane->get_spawn_pipeline();
            const spawn::Record *recipe = spawns->spawned_record(p_row.route);
            if (recipe != nullptr && p_row.wrapper.is_valid()
                && gd::object_of(p_row.owner_id) != nullptr) {
                p_row.wrapper->keep_readmission(*recipe);
            }
            transfer_note_death(p_row.route);
            spawns->settle_death(p_row.route);
        }
        liveness_drop_hooks(p_row.route);
        liveness_release(p_row.route);
    }
    entity_release_residencies(p_row, RID());
    entity_release_body(p_row.entity, p_row.wrapper, p_row.route, false);
    if (p_row.wrapper.is_valid()) {
        NetwEntityRecord *const record = p_row.wrapper->get_record();
        record->complete_departure(p_row.wrapper.ptr());
    }
    if (routed) {
        liveness_announce_dead(p_row.route);
    }
}

void NetwMultiplayer::entity_release_body(
    const RID &p_entity,
    const Ref<NetwEntity> &p_wrapper,
    int64_t p_route,
    bool p_owner_live
) {
    membership_drop_entity(p_wrapper);
    interest_release_body(p_wrapper);
    if (p_wrapper.is_valid()
        && predict_engine_seated(p_wrapper->get_rid_handle())) {
        p_wrapper->note_departed_prediction(true);
    }
    unregister_prediction(p_wrapper);
    lagcomp_timeline_undeclare(p_entity);
    if (p_route > 0) {
        replication_clear_route(p_route);
        if (p_owner_live) {
            display_release_route(p_route);
        } else {
            display_retire_route(p_route);
        }
    }
}

void NetwMultiplayer::entity_commit_hide(const EntityDeparture &p_row) {
    if (p_row.hidden || p_row.route <= 0) {
        return;
    }
    if (entity_get_view(p_row.entity) != p_row.wrapper) {
        return;
    }
    if (ReplicationCore *plane = get_replication_plane()) {
        plane->get_spawn_pipeline()->settle_absence(p_row.route);
    }
    entity_release_body(p_row.entity, p_row.wrapper, p_row.route, true);
    liveness_hide(p_row.route);
}

void NetwMultiplayer::liveness_owner_despawning(
    const StringName &p_reason,
    Object *p_wrapper
) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_wrapper);
    if (entity == nullptr) {
        return;
    }
    NetwEntityRecord *const record = record_of_wrapper(p_wrapper);
    const int64_t route = liveness_route_of(p_wrapper);
    if (record == nullptr || route <= 0) {
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

void NetwMultiplayer::liveness_drop_hooks(int64_t p_route) {
    const Ref<NetwEntity> wrapper = wrapper_for_route(p_route);
    if (wrapper.is_null()) {
        return;
    }
    wrapper->watch_despawning(false);
}

RID NetwMultiplayer::entity_create() {
    return liveness_core->entity_create();
}

Error NetwMultiplayer::entity_bind_node(const RID &p_entity, Node *p_node) {
    if (!liveness_core->entity_is_valid(p_entity)) {
        return ERR_DOES_NOT_EXIST;
    }
    if (p_node == nullptr) {
        return ERR_INVALID_DATA;
    }

    const Ref<NetwEntity> held = entity_get_view(p_entity);
    if (held.is_valid()) {
        return held->get_owner() == p_node ? OK : ERR_ALREADY_EXISTS;
    }
    const int64_t route = entity_get_route(p_entity);
    if (route <= 0) {
        return ERR_INVALID_DATA;
    }
    const Ref<NetwEntity> wrapper = NetwEntity::ensure(p_node);
    apply_pending_scene_facet(p_entity, wrapper);
    if (wrapper->get_multiplayer() == nullptr) {
        wrapper->arm(Object::cast_to<NetwMultiplayer>(session_api()));
    }
    liveness_bind_route(route, wrapper.ptr());
    return OK;
}

int64_t NetwMultiplayer::entity_admit(const RID &p_entity) {
    NETW_ERR_COND_V(
        !is_host(),
        0,
        sys::LIVENESS,
        "NetwMultiplayer.entity_admit runs only on the server."
    );
    if (!liveness_core->entity_is_valid(p_entity)) {
        return 0;
    }
    const int64_t standing = entity_get_route(p_entity);
    if (standing > 0) {
        return standing;
    }
    const int64_t route = liveness_reserve_route();
    return entity_bind_route(p_entity, route) == OK ? route : 0;
}

Error NetwMultiplayer::entity_bind_route(const RID &p_entity, int64_t p_route) {
    if (!liveness_core->entity_is_valid(p_entity)) {
        return ERR_DOES_NOT_EXIST;
    }
    if (p_route <= 0) {
        return ERR_INVALID_DATA;
    }
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    const bool bound = wrapper.is_valid()
        ? liveness_bind_route(p_route, wrapper.ptr())
        : liveness_core->bind_route(p_entity, int(p_route));
    return bound ? OK : ERR_ALREADY_IN_USE;
}

int64_t NetwMultiplayer::entity_get_route(const RID &p_entity) const {
    const int route = liveness_core->route_of(p_entity);
    if (route > 0) {
        return route;
    }
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    return wrapper.is_valid() ? wrapper->get_route() : 0;
}

Node *NetwMultiplayer::entity_get_node(const RID &p_entity) const {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    return wrapper.is_valid() ? wrapper->get_owner() : nullptr;
}

RID NetwMultiplayer::entity_get_parent(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return RID();
    }
    const Ref<NetwEntity> parent = wrapper->parent_entity();
    if (parent.is_null() || parent->get_owner() == nullptr) {
        return RID();
    }
    return entity_of(parent->get_owner());
}

int64_t NetwMultiplayer::entity_get_peer(const RID &p_entity) const {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    return wrapper.is_valid() ? wrapper->get_peer_id() : 0;
}

Error NetwMultiplayer::entity_grant_control(
    const RID &p_entity,
    int64_t p_peer
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    return wrapper->transfer_control(p_peer);
}

NetwMultiplayer::EntityState NetwMultiplayer::entity_get_state(
    const RID &p_entity
) const {
    return EntityState(liveness_core->state_of(p_entity));
}

int NetwMultiplayer::entity_get_epoch(const RID &p_entity) const {
    return liveness_core->epoch_of(p_entity);
}

RID NetwMultiplayer::entity_from_route(int p_route) const {
    return liveness_core->rid_from_route(p_route);
}

PackedInt32Array NetwMultiplayer::liveness_get_routes() const {
    return liveness_core->live_routes();
}

Error NetwMultiplayer::spawn_admit_frame_default(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    lifecycle::Kind kind = lifecycle::Kind::SPAWN;
    if (p_channel == gate_channels.spawn && is_host()
        && p_sender != SERVER_PEER) {
        return liveness_core->lease_holds(int(p_sender), int(p_route))
            ? OK
            : ERR_UNAUTHORIZED;
    }
    if (p_channel == gate_channels.reparent) {
        kind = lifecycle::Kind::REPARENT;
    } else if (
        p_channel == gate_channels.despawn || p_channel == gate_channels.hide
    ) {
        kind = lifecycle::Kind::DESPAWN;
    }
    const lifecycle::Ruling ruling
        = lifecycle::rule(lifecycle_frame_facts(p_sender, kind, p_payload));
    if (!ruling.admitted()) {
        return ruling.code;
    }
    if (p_channel != gate_channels.spawn && p_channel != gate_channels.despawn
        && p_channel != gate_channels.hide
        && p_channel != gate_channels.reparent) {
        return ERR_INVALID_DATA;
    }
    if (p_payload.is_empty()) {
        return ERR_INVALID_DATA;
    }
    return OK;
}

Error NetwMultiplayer::spawn_admit_frame(
    int64_t p_sender,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    NETW_ZONE_NC("spawn admit frame", colors::LIVENESS);
    Error answered = OK;
    if (GDVIRTUAL_CALL(
            _spawn_admit_frame,
            p_sender,
            p_route,
            p_channel,
            p_payload,
            answered
        )) {
        return answered;
    }
    return spawn_admit_frame_default(p_sender, p_route, p_channel, p_payload);
}

Error NetwMultiplayer::spawn_declare(
    const RID &p_entity,
    const Variant &p_recipe
) {
    Error answered = OK;
    if (GDVIRTUAL_CALL(_spawn_declare, p_entity, p_recipe, answered)) {
        return answered;
    }
    return spawn_declare_default(p_entity, p_recipe);
}

void NetwMultiplayer::spawn_undeclare(const RID &p_entity) {
    GDVIRTUAL_CALL(_spawn_undeclare, p_entity);
}

void NetwMultiplayer::spawn_construct_arm(const Callable &p_constructor) {
    spawn_constructor = p_constructor;
}

Node *NetwMultiplayer::spawn_construct_default(const RID &p_entity) {
    (void)p_entity;
    if (!spawn_constructor.is_valid()) {
        return nullptr;
    }
    return Object::cast_to<Node>(gd::live_object(spawn_constructor.call()));
}

Node *NetwMultiplayer::spawn_construct(const RID &p_entity) {
    Node *built = nullptr;
    if (GDVIRTUAL_CALL(_spawn_construct, p_entity, built)) {
        return built;
    }
    return spawn_construct_default(p_entity);
}

Error NetwMultiplayer::spawn_declare_default(
    const RID &p_entity,
    const Variant &p_recipe
) {
    (void)p_recipe;
    if (entity_get_view(p_entity).is_valid()) {
        return OK;
    }
    NETW_TRACE(
        sys::SPAWN,
        "Declaring an entity that has no NetwEntity on this peer."
    );
    return ERR_DOES_NOT_EXIST;
}

bool NetwMultiplayer::spawn_locally_desired(int64_t p_peer_id, Node *p_node) {
    if (p_node == nullptr) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_valid()) {
        if (entity->steering() == p_peer_id) {
            return true;
        }
        if (interest_has_committed_intent(entity)) {
            return interest_wire_admits(p_peer_id, entity);
        }
        if (interest_is_filtered(entity_of(p_node))
            && !interest_wire_admits(p_peer_id, entity)) {
            return false;
        }
    }
    return synchronizers::visibility_verdict(
        p_node,
        p_peer_id,
        get_multiplayer_peer().is_valid() ? int64_t(get_unique_id()) : 1
    );
}

bool NetwMultiplayer::spawn_visible_to(
    spawn::Book *p_book,
    int64_t p_route,
    int64_t p_peer_id,
    Node *p_node
) {
    if (p_book == nullptr || !p_book->parent_admits(p_route, p_peer_id)
        || persist_withholds(p_node)) {
        return false;
    }
    return spawn_locally_desired(p_peer_id, p_node);
}

Ref<NetwEntity> NetwMultiplayer::spawn_arm_identity(
    spawn::Record *p_record,
    Node *p_node,
    const Callable &p_declare
) {
    if (p_record == nullptr || p_node == nullptr) {
        return Ref<NetwEntity>();
    }
    const Ref<NetwEntity> entity = NetwEntity::ensure(p_node);
    if (entity.is_null()) {
        return entity;
    }
    const Ref<NetwPlayer> represented = entity->unbound_player();
    const int64_t held_route = entity->get_route();
    const StringName held_id = entity->get_name();
    const StringName held_node_name = p_node->get_name();
    const int64_t held_peer = entity->get_peer_id();
    const int64_t held_player_id = entity->get_player_id();
    const int64_t held_controller = entity->steering();

    int64_t route = liveness_route_of(entity.ptr());
    if (route <= 0) {
        route = entity->get_route();
    }
    if (route <= 0) {
        route = liveness_reserve_route();
    }
    entity->set_route(route);
    if (entity->get_name() == StringName()) {
        const StringName claimed = represented.is_valid()
            ? represented->get_username()
            : StringName();
        if (claimed != StringName()) {
            entity->set_name(claimed);
            if (!p_node->is_inside_tree()) {
                p_node->set_name(String(claimed));
            }
        } else {
            entity->set_name(
                spawn::Book::default_entity_id(p_record, p_node, route)
            );
            const String seated = String(p_node->get_name());
            if (!p_node->is_inside_tree()
                && (seated.is_empty()
                    || seated != seated.validate_node_name())) {
                const String identity
                    = String(entity->get_name()).validate_node_name();
                p_node->set_name(identity);
            }
        }
    }
    if (!is_host()) {
        lifecycle_seed_controller(entity.ptr(), get_unique_id());
    }
    p_record->set_route(route);
    p_record->bind_node(p_node);
    p_record->set_entity_id(entity->get_name());
    p_record->set_peer_id(entity->get_peer_id());
    p_record->set_controller(entity->steering());

    Dictionary facts;
    facts[StringName("recipe")] = p_record->get_recipe();
    facts[StringName("route")] = route;
    facts[StringName("entity_id")] = p_record->get_entity_id();
    facts[StringName("peer")] = p_record->get_peer_id();
    facts[StringName("controller")] = entity->get_controller();
    if (p_declare.is_valid()
        && int64_t(p_declare.call(entity_of(p_node), facts)) != int64_t(OK)) {
        entity->set_route(held_route);
        entity->set_name(held_id);
        p_node->set_name(String(held_node_name));
        entity->set_peer_id(held_peer);
        entity->set_player_id(held_player_id);
        entity->record_controller(held_controller);
        return Ref<NetwEntity>();
    }
    return entity;
}

PackedInt32Array NetwMultiplayer::rpc_get_recipients(
    const Ref<NetwEntity> &p_entity
) {
    NETW_ZONE_NC("session live peers", colors::WIRE);
    const Ref<MultiplayerPeer> peer = inner.is_valid()
        ? inner->get_multiplayer_peer()
        : Ref<MultiplayerPeer>();
    if (peer.is_null() || peer->is_class("OfflineMultiplayerPeer")) {
        PackedInt32Array offline;
        offline.push_back(1);
        return offline;
    }
    store_refresh();
    const uint32_t slot = store_slot_of(p_entity);
    if (slot == store::Table::NO_SLOT) {
        return send_recipients(p_entity, store_roster_ids());
    }
    if (store_world.slot_stale(slot)) {
        store_evaluate_slot(slot, p_entity);
    }
    return store_reach(slot);
}

PackedInt64Array NetwMultiplayer::spawn_replay_to(
    spawn::Book *p_book,
    int64_t p_peer_id,
    int64_t p_channel,
    const Callable &p_encode
) {
    NETW_ZONE_NC("session spawn replay to", colors::LIVENESS);
    PackedInt64Array unencodable;
    if (p_book == nullptr) {
        return unencodable;
    }
    const PackedInt64Array routes = p_book->ancestry_order();
    for (int at = 0; at < routes.size(); at++) {
        const int64_t route = routes[at];
        spawn::Record *record = p_book->spawned_of(route);
        if (record == nullptr) {
            continue;
        }
        Node *node = record->node();
        if (node == nullptr || !node->is_inside_tree()) {
            continue;
        }
        if (record->has_recipient(int(p_peer_id))) {
            continue;
        }
        if (!spawn_visible_to(p_book, record->get_route(), p_peer_id, node)) {
            continue;
        }
        const PackedByteArray payload = p_encode.is_valid()
            ? PackedByteArray(p_encode.call(record->get_route(), node))
            : PackedByteArray();
        if (payload.is_empty()) {
            unencodable.push_back(route);
            continue;
        }
        record->add_recipient(int(p_peer_id));
        store_world.stale_route(route);
        attribution_note_subject(record->get_route());
        send_to(p_peer_id, 0, p_channel, payload, true, 0, String(), true);
    }
    return unencodable;
}

void NetwMultiplayer::spawn_execute_plan(
    spawn::Book *p_book,
    const Array &p_plan,
    int64_t p_spawn_channel,
    int64_t p_hide_channel,
    const Callable &p_encode
) {
    NETW_ZONE_NC("session spawn execute plan", colors::LIVENESS);
    if (p_book == nullptr) {
        return;
    }
    HashMap<int64_t, PackedByteArray> spawn_frames;
    HashMap<int64_t, PackedByteArray> hide_frames;
    for (int at = 0; at < p_plan.size(); at++) {
        const Dictionary operation = p_plan[at];
        const int64_t route = int64_t(operation.get(StringName("route"), 0));
        const int64_t peer_id = int64_t(operation.get(StringName("peer"), 0));
        spawn::Record *record = p_book->spawned_of(route);
        if (record == nullptr) {
            continue;
        }
        Node *node = record->node();
        if (node == nullptr || !node->is_inside_tree()) {
            continue;
        }
        const StringName action
            = operation.get(StringName("action"), StringName());
        if (action == StringName("spawn")) {
            HashMap<int64_t, PackedByteArray>::Iterator held
                = spawn_frames.find(route);
            if (!held) {
                held = spawn_frames.insert(
                    route,
                    p_encode.is_valid()
                        ? PackedByteArray(
                              p_encode.call(record->get_route(), node)
                          )
                        : PackedByteArray()
                );
            }
            if (held->value.is_empty()) {
                continue;
            }
            record->add_recipient(int(peer_id));
            store_world.stale_route(route);
            attribution_note_subject(record->get_route());
            send_to(
                peer_id,
                0,
                p_spawn_channel,
                held->value,
                true,
                0,
                String(),
                true
            );
            continue;
        }
        if (action != StringName("retain") && action != StringName("hide")) {
            continue;
        }
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        if (entity.is_valid()) {
            interest_leave_commit(
                entity,
                peer_id,
                operation.get(StringName("decision"), Dictionary()),
                bool(operation.get(StringName("forced"), false))
            );
        }
        if (action == StringName("retain")) {
            continue;
        }
        HashMap<int64_t, PackedByteArray>::Iterator held
            = hide_frames.find(route);
        if (!held) {
            wire::WriteStream stream;
            if (!verb_head_write(stream, route) || !stream.align_verify()) {
                continue;
            }
            held = hide_frames.insert(route, stream.to_bytes());
        }
        record->remove_recipient(int(peer_id));
        store_world.stale_route(route);
        attribution_note_subject(route);
        send_to(
            peer_id,
            0,
            p_hide_channel,
            held->value,
            true,
            0,
            String(),
            true
        );
    }
}

spawn::Record *NetwMultiplayer::spawn_issue_armed(
    spawn::Book *p_book,
    int64_t p_route
) {
    spawn::Record armed;
    if (p_book == nullptr || !p_book->take_armed(p_route, armed)) {
        return nullptr;
    }
    Node *node = armed.node();
    if (node == nullptr || !node->is_inside_tree()) {
        NETW_TRACE(
            sys::SPAWN,
            "Route %d left the tree before its spawn was sent, so the spawn "
            "was dropped.",
            int(p_route)
        );
        return nullptr;
    }
    armed.set_node_name(String(node->get_name()));
    const Ref<NetwEntity> above = NetwEntity::of(node->get_parent());
    armed.set_parent_route(
        above.is_valid() ? liveness_route_of(above.ptr()) : 0
    );
    interest_sync_scene_membership(NetwEntity::of(node));
    membership_place_entity(NetwEntity::of(node));
    store_world.stale_route(p_route);
    return p_book->issue(armed);
}

PackedInt32Array NetwMultiplayer::spawn_fan_out(
    spawn::Book *p_book,
    spawn::Record *p_record,
    Node *p_node,
    const PackedByteArray &p_payload,
    const PackedInt32Array &p_connected,
    int64_t p_channel,
    int64_t p_first
) {
    PackedInt32Array recipients;
    if (p_book == nullptr || p_record == nullptr || p_node == nullptr
        || p_payload.is_empty()) {
        return recipients;
    }
    for (int at = 0; at < p_connected.size(); at++) {
        if (int64_t(p_connected[at]) != p_first
            && spawn_visible_to(
                p_book,
                p_record->get_route(),
                int64_t(p_connected[at]),
                p_node
            )) {
            recipients.push_back(p_connected[at]);
        }
    }
    p_record->set_recipients(recipients);
    if (p_first > 0) {
        p_record->add_recipient(int(p_first));
    }
    store_world.stale_route(p_record->get_route());
    for (int at = 0; at < recipients.size(); at++) {
        attribution_note_subject(p_record->get_route());
        send_to(
            int64_t(recipients[at]),
            0,
            p_channel,
            p_payload,
            true,
            0,
            String(),
            true
        );
    }
    return recipients;
}

bool NetwMultiplayer::spawn_applying_remote_frame() const {
    return applying_remote_frame;
}

void NetwMultiplayer::spawn_place_node(Node *p_parent, Node *p_node) {
    if (p_parent == nullptr || p_node == nullptr) {
        return;
    }
    const bool was_applying = applying_remote_frame;
    applying_remote_frame = true;
    p_parent->add_child(p_node);
    applying_remote_frame = was_applying;
}

void NetwMultiplayer::lifecycle_undo(
    NetwEntity *p_entity,
    Node *p_parent,
    const AnchorRevision &p_revision
) {
    const int64_t route = liveness_route_of(p_entity);
    Node *owner = p_entity->get_owner();
    row_streams_reopen(route);
    if (owner != nullptr && p_parent != nullptr
        && owner->get_parent() != p_parent && owner->is_inside_tree()) {
        spawn_replace_keeping_pose(owner, p_parent);
    }
    if (route > 0 && p_revision.revision > 0) {
        entity_install_anchor(route, p_revision.revision, p_revision.author);
    }
}

void NetwMultiplayer::spawn_replace_keeping_pose(Node *p_node, Node *p_parent) {
    const Variant pose = world_pose_of(p_node);
    spawn_unplace_node(p_node);
    spawn_place_node(p_parent, p_node);
    world_pose_restore(p_node, pose);
}

void NetwMultiplayer::spawn_unplace_node(Node *p_node) {
    Node *parent = p_node != nullptr ? p_node->get_parent() : nullptr;
    if (parent == nullptr) {
        return;
    }
    const bool was_applying = applying_remote_frame;
    applying_remote_frame = true;
    parent->remove_child(p_node);
    applying_remote_frame = was_applying;
}

bool NetwMultiplayer::spawn_reparent_node(
    Node *p_node,
    Node *p_parent,
    const Callable &p_adopt,
    int64_t p_route,
    uint64_t p_anchor,
    uint64_t p_anchor_author,
    int64_t p_author
) {
    if (p_node == nullptr || p_parent == nullptr) {
        return false;
    }
    if (p_node->get_parent() == p_parent) {
        if (p_route > 0 && p_anchor > 0) {
            entity_install_anchor(p_route, p_anchor, p_anchor_author);
        }
        return false;
    }
    spawn_carry_begin(
        p_node,
        p_parent,
        p_adopt,
        p_route,
        p_anchor,
        p_anchor_author,
        p_author
    );
    return true;
}

Node *NetwMultiplayer::spawn_carry_pending_parent(int64_t p_route) const {
    uint64_t newest = 0;
    Node *parent = nullptr;
    for (const KeyValue<int64_t, SpawnCarry> &flying : spawn_carries) {
        if (flying.value.route == p_route && !flying.value.moved
            && flying.value.anchor >= newest) {
            newest = flying.value.anchor;
            parent = Object::cast_to<Node>(gd::object_of(flying.value.parent));
        }
    }
    return parent;
}

NetwMultiplayer::AnchorRevision NetwMultiplayer::anchor_standing(
    int64_t p_route
) const {
    AnchorRevision standing = anchor_installed(p_route);
    for (const KeyValue<int64_t, SpawnCarry> &flying : spawn_carries) {
        if (flying.value.route == p_route && !flying.value.moved
            && flying.value.anchor > standing.revision) {
            standing.revision = flying.value.anchor;
            standing.author = flying.value.anchor_author;
        }
    }
    return standing;
}

void NetwMultiplayer::spawn_carry_land_out_of(Node *p_leaving) {
    if (p_leaving == nullptr) {
        return;
    }
    LocalVector<int64_t> leaving;
    for (const KeyValue<int64_t, SpawnCarry> &flying : spawn_carries) {
        Node *node = Object::cast_to<Node>(gd::object_of(flying.value.node));
        Node *parent
            = Object::cast_to<Node>(gd::object_of(flying.value.parent));
        if (flying.value.moved || node == nullptr || parent == nullptr) {
            continue;
        }
        if (p_leaving->is_ancestor_of(node) && parent != p_leaving
            && !p_leaving->is_ancestor_of(parent)) {
            leaving.push_back(flying.key);
        }
    }
    for (const int64_t id : leaving) {
        spawn_carry_advance(id);
    }
}

void NetwMultiplayer::spawn_carry_begin(
    Node *p_node,
    Node *p_parent,
    const Callable &p_adopt,
    int64_t p_route,
    uint64_t p_anchor,
    uint64_t p_anchor_author,
    int64_t p_author
) {
    NETW_ZONE_NC("NetwMultiplayer spawn_carry_begin", colors::SCENE);
    const ObjectID node = gd::instance_id(p_node);
    for (KeyValue<int64_t, SpawnCarry> &flying : spawn_carries) {
        if (flying.value.node == node && !flying.value.moved) {
            flying.value.parent = gd::instance_id(p_parent);
            flying.value.adopt = p_adopt;
            flying.value.route = p_route;
            flying.value.anchor = p_anchor;
            flying.value.anchor_author = p_anchor_author;
            flying.value.author = p_author;
            return;
        }
    }
    SpawnCarry carry;
    carry.node = node;
    carry.parent = gd::instance_id(p_parent);
    carry.adopt = p_adopt;
    carry.route = p_route;
    carry.anchor = p_anchor;
    carry.anchor_author = p_anchor_author;
    carry.author = p_author;
    carry.guard = guard_hold(p_node);
    const int64_t id = ++spawn_carry_next;
    spawn_carries.insert(id, carry);
    spawn_carry_open(id);
}

void NetwMultiplayer::spawn_carry_open(int64_t p_id) {
    if (!spawn_carry_reachable(p_id)) {
        spawn_carry_lose(p_id);
        return;
    }
    const SpawnCarry *carry = spawn_carries.getptr(p_id);
    guard_then(
        carry->guard,
        GUARD_WINDOW_FRAMES,
        callable_mp(this, &NetwMultiplayer::spawn_carry_advance).bind(p_id)
    );
}

bool NetwMultiplayer::spawn_carry_reachable(int64_t p_id) {
    const SpawnCarry *carry = spawn_carries.getptr(p_id);
    return carry != nullptr && gd::object_of(carry->node) != nullptr
        && gd::object_of(carry->parent) != nullptr;
}

void NetwMultiplayer::spawn_carry_advance(int64_t p_id) {
    NETW_ZONE_NC("NetwMultiplayer spawn_carry_advance", colors::SCENE);
    if (!spawn_carry_reachable(p_id)) {
        spawn_carry_lose(p_id);
        return;
    }
    SpawnCarry *carry = spawn_carries.getptr(p_id);
    if (carry->moved) {
        spawn_carry_finish(p_id);
        return;
    }
    carry->moved = true;
    Node *node = Object::cast_to<Node>(gd::object_of(carry->node));
    Node *parent = Object::cast_to<Node>(gd::object_of(carry->parent));
    const Callable adopt = carry->adopt;
    const RID guard = carry->guard;
    const int64_t route = carry->route;
    const uint64_t anchor = carry->anchor;
    const uint64_t anchor_author = carry->anchor_author;
    const Variant pose = world_pose_of(node);
    Node *held = node->get_parent();
    const bool was_applying = applying_remote_frame;
    const int64_t was_author = admitting_author;
    const int64_t was_route = admitting_route;
    applying_remote_frame = true;
    admitting_author = carry->author;
    admitting_route = carry->author != 0 ? route : 0;
    if (held != nullptr) {
        held->remove_child(node);
    }
    spawn_place_node(parent, node);
    applying_remote_frame = was_applying;
    admitting_author = was_author;
    admitting_route = was_route;
    world_pose_restore(node, pose);
    if (route > 0 && anchor > 0) {
        entity_install_anchor(route, anchor, anchor_author);
    }
    reparent_guards.resume_processing(guard);
    if (adopt.is_valid()) {
        adopt.call(entity_of(node));
    }
    spawn_carry_open(p_id);
}

void NetwMultiplayer::spawn_carry_abandon(int64_t p_id) {
    const SpawnCarry *found = spawn_carries.getptr(p_id);
    if (found == nullptr) {
        return;
    }
    const SpawnCarry carry = *found;
    spawn_carries.erase(p_id);
    guard_let_go(carry.guard);
    NETW_TRACE(
        sys::SPAWN,
        "Reparent %d was abandoned before it finished.",
        int(p_id)
    );
}

void NetwMultiplayer::spawn_carry_lose(int64_t p_id) {
    const SpawnCarry *found = spawn_carries.getptr(p_id);
    if (found == nullptr) {
        return;
    }
    const SpawnCarry carry = *found;
    spawn_carry_abandon(p_id);
    if (carry.author == 0 || carry.moved || carry.route <= 0
        || carry.anchor == 0) {
        return;
    }
    const bool node_gone = gd::object_of(carry.node) == nullptr;
    lifecycle_decide(
        carry.route,
        carry.author,
        lifecycle::Kind::REPARENT,
        carry.anchor - 1,
        node_gone ? ERR_UNAVAILABLE : ERR_INVALID_PARAMETER
    );
}

void NetwMultiplayer::spawn_carry_sweep() {
    while (!spawn_carries.is_empty()) {
        spawn_carry_abandon(spawn_carries.begin()->key);
    }
}

void NetwMultiplayer::spawn_carry_finish(int64_t p_id) {
    const SpawnCarry *found = spawn_carries.getptr(p_id);
    if (found == nullptr) {
        return;
    }
    const SpawnCarry carry = *found;
    spawn_carries.erase(p_id);
    guard_let_go(carry.guard);
}

bool NetwMultiplayer::spawn_send_reparent(
    spawn::Book *p_book,
    spawn::Record *p_record,
    Node *p_node,
    const PackedInt32Array &p_connected,
    int64_t p_channel,
    int64_t p_skip
) {
    if (p_book == nullptr || p_record == nullptr || p_node == nullptr) {
        return false;
    }
    wire::WriteStream stream;
    AnchorRevision installed = anchor_installed(p_record->get_route());
    if (!verb_head_write(stream, p_record->get_route())
        || !anchor_encode(stream, p_node->get_parent())
        || !stream.varuint(installed.revision, 5)
        || !stream.varuint(installed.author, 5) || !stream.align_verify()) {
        NETW_WARN(
            sys::SPAWN,
            "'%s' was moved outside the multiplayer root, so other peers keep "
            "its old parent. Move it under a node inside the root.",
            String(p_node->get_name()).utf8().get_data()
        );
        return false;
    }
    const PackedByteArray payload = stream.to_bytes();
    const PackedInt32Array recipients = p_record->recipients();
    for (int at = 0; at < recipients.size(); at++) {
        const int64_t peer_id = int64_t(recipients[at]);
        if (peer_id == p_skip || !p_connected.has(recipients[at])) {
            continue;
        }
        if (!spawn_visible_to(p_book, p_record->get_route(), peer_id, p_node)) {
            continue;
        }
        attribution_note_subject(p_record->get_route());
        send_to(peer_id, 0, p_channel, payload, true, 0, String(), true);
    }
    return true;
}

bool NetwMultiplayer::spawn_despawn_route(
    spawn::Book *p_book,
    int64_t p_route,
    const PackedInt32Array &p_connected,
    int64_t p_channel,
    const Callable &p_undeclare
) {
    if (p_book == nullptr) {
        return false;
    }
    const PackedInt64Array plan = p_book->despawn_order(p_route);
    if (plan.is_empty()) {
        return false;
    }
    for (int at = 0; at < plan.size(); at++) {
        const int64_t doomed = plan[at];
        spawn::Record *record = p_book->spawned_of(doomed);
        if (record == nullptr) {
            continue;
        }
        const Ref<NetwEntity> entity = NetwEntity::of(record->node());
        if (entity.is_valid() && p_undeclare.is_valid()) {
            p_undeclare.call(entity->get_rid_handle());
        }
        const PackedInt32Array recipients = record->recipients();
        p_book->drop_spawned(doomed);
        store_world.stale_route(doomed);
        if (p_connected.is_empty()) {
            continue;
        }
        wire::WriteStream stream;
        if (!verb_head_write(stream, doomed) || !stream.align_verify()) {
            continue;
        }
        const PackedByteArray payload = stream.to_bytes();
        for (int index = 0; index < recipients.size(); index++) {
            if (!p_connected.has(recipients[index])) {
                continue;
            }
            attribution_note_subject(doomed);
            send_to(
                int64_t(recipients[index]),
                0,
                p_channel,
                payload,
                true,
                0,
                String(),
                true
            );
        }
    }
    return true;
}

void NetwMultiplayer::spawn_detach_before_despawn(NetwEntity *p_dying) {
    Node *owner = p_dying != nullptr ? p_dying->get_owner() : nullptr;
    Node *above = owner != nullptr ? owner->get_parent() : nullptr;
    ReplicationCore *plane = get_replication_plane();
    if (above == nullptr || !owner->is_inside_tree() || plane == nullptr) {
        return;
    }
    spawn::Book *book = plane->get_spawn_pipeline()->get_spawn_book();
    const int64_t route = liveness_route_of(p_dying);
    const PackedInt64Array doomed = book->despawn_order(route);
    PackedInt64Array detaching;
    for (int at = 0; at < doomed.size(); ++at) {
        const spawn::Record *record = book->spawned_of(doomed[at]);
        const Ref<NetwEntity> entity
            = NetwEntity::of(record != nullptr ? record->node() : nullptr);
        if (entity.is_valid()
            && entity->get_on_parent_despawn()
                == NetwEntity::PARENT_DESPAWN_DETACH) {
            detaching.push_back(doomed[at]);
        }
    }
    const PackedInt64Array roots = book->detach_roots(route, detaching);
    for (int at = 0; at < roots.size(); ++at) {
        entity_move(book->spawned_of(roots[at])->node(), above);
    }
}

void NetwMultiplayer::spawn_reanchor(
    spawn::Record *p_record,
    Node *p_node,
    spawn::SpawnerRoster *p_roster
) {
    if (p_record == nullptr || p_roster == nullptr || p_node == nullptr) {
        return;
    }
    if (p_record->get_recipe() != spawn::Book::RECIPE_SPAWNER
        || !p_node->is_inside_tree()) {
        return;
    }
    Node *parent = p_node->get_parent();
    MultiplayerSpawner *current = p_record->spawner();
    if (current != nullptr && current->is_inside_tree()
        && current->get_node_or_null(current->get_spawn_path()) == parent) {
        return;
    }
    Node *root = session_root();
    if (root == nullptr) {
        return;
    }
    const LocalVector<Object *> live = p_roster->live();
    for (uint32_t at = 0; at < live.size(); at++) {
        MultiplayerSpawner *spawner
            = Object::cast_to<MultiplayerSpawner>(live[at]);
        if (spawner == nullptr || !spawner->is_inside_tree()
            || !root->is_ancestor_of(spawner)) {
            continue;
        }
        if (spawner->get_node_or_null(spawner->get_spawn_path()) != parent) {
            continue;
        }
        if (p_record->get_scene_index() >= 0) {
            const int index
                = spawn::SpawnerRoster::scene_index_for(spawner, p_node);
            if (index < 0) {
                continue;
            }
            p_record->set_scene_index(index);
        } else if (!spawner->get_spawn_function().is_valid()) {
            continue;
        }
        p_record->bind_spawner(spawner);
        return;
    }
    NETW_TRACE(
        sys::SPAWN,
        "Route %d moved under '%s', which has no matching spawner, so it "
        "keeps its original parent.",
        int(p_record->get_route()),
        parent != nullptr ? String(parent->get_name()).utf8().get_data() : ""
    );
}

void NetwMultiplayer::spawn_refresh_anchor(
    spawn::Record *p_record,
    Node *p_node,
    spawn::SpawnerRoster *p_roster
) {
    if (p_record == nullptr || p_node == nullptr) {
        return;
    }
    const Ref<NetwEntity> above = NetwEntity::of(p_node->get_parent());
    p_record->set_parent_route(
        above.is_valid() ? liveness_route_of(above.ptr()) : 0
    );
    interest_sync_scene_membership(NetwEntity::of(p_node));
    membership_place_entity(NetwEntity::of(p_node));
    spawn_reanchor(p_record, p_node, p_roster);
}

void NetwMultiplayer::spawn_refresh_anchors(
    spawn::Book *p_book,
    spawn::SpawnerRoster *p_roster
) {
    NETW_ZONE_NC("session spawn refresh anchors", colors::LIVENESS);
    if (p_book == nullptr) {
        return;
    }
    const PackedInt64Array routes = p_book->spawned_routes();
    for (int at = 0; at < routes.size(); at++) {
        spawn::Record *record = p_book->spawned_of(routes[at]);
        if (record == nullptr) {
            continue;
        }
        Node *node = record->node();
        if (node != nullptr && node->is_inside_tree()) {
            spawn_refresh_anchor(record, node, p_roster);
        }
    }
}

TypedArray<Dictionary> NetwMultiplayer::spawn_reconcile_rows(
    spawn::Book *p_book,
    const PackedInt32Array &p_peers
) {
    NETW_ZONE_NC("session spawn reconcile rows", colors::LIVENESS);
    TypedArray<Dictionary> rows;
    if (p_book == nullptr) {
        return rows;
    }
    const PackedInt64Array routes = p_book->ancestry_order();
    for (int at = 0; at < routes.size(); at++) {
        const int64_t route = routes[at];
        spawn::Record *record = p_book->spawned_of(route);
        if (record == nullptr) {
            continue;
        }
        Node *node = record->node();
        if (node == nullptr || !node->is_inside_tree()
            || persist_withholds(node)) {
            continue;
        }
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        Dictionary local_desired;
        Dictionary leave;
        for (int index = 0; index < p_peers.size(); index++) {
            const int64_t peer_id = int64_t(p_peers[index]);
            const bool desired = spawn_locally_desired(peer_id, node);
            local_desired[peer_id] = desired;
            if (entity.is_valid() && !desired
                && record->has_recipient(int(peer_id))) {
                leave[peer_id] = interest_leave_resolve(entity, peer_id);
            }
        }
        Dictionary row;
        row[StringName("route")] = route;
        row[StringName("parent_route")] = record->get_parent_route();
        row[StringName("recipients")] = record->recipients();
        row[StringName("local_desired")] = local_desired;
        row[StringName("leave")] = leave;
        rows.push_back(row);
    }
    return rows;
}

Array NetwMultiplayer::live_scenes() const {
    return scene_core->live_scenes();
}

NetwMultiplayer::EntitySpace NetwMultiplayer::entity_space_of(
    const Ref<NetwEntity> &p_entity
) const {
    EntitySpace out;
    Node *node = p_entity.is_valid() ? p_entity->get_owner() : nullptr;
    if (node == nullptr || !node->is_inside_tree()) {
        return out;
    }
    if (Node3D *spatial = Object::cast_to<Node3D>(node)) {
        const Ref<World3D> world = spatial->get_world_3d();
        if (world.is_valid()) {
            out.space = world->get_space();
            out.dimension = 3;
        }
        return out;
    }
    if (CanvasItem *canvas = Object::cast_to<CanvasItem>(node)) {
        const Ref<World2D> world = canvas->get_world_2d();
        if (world.is_valid()) {
            out.space = world->get_space();
            out.dimension = 2;
        }
    }
    return out;
}

Dictionary NetwMultiplayer::entity_space(const RID &p_entity) const {
    const EntitySpace held = entity_space_of(entity_get_view(p_entity));
    Dictionary out;
    out[StringName("space")] = held.space;
    out[StringName("dimension")] = held.dimension;
    return out;
}

bool NetwMultiplayer::verb_head_write(
    wire::WriteStream &p_stream,
    int64_t p_route
) {
    if (p_route <= 0) {
        return false;
    }
    const int64_t held = liveness_route_epoch(p_route);
    spawn::VerbHead head;
    head.route = uint64_t(p_route);
    head.epoch = uint64_t(held > 0 ? held : 0);
    return spawn::VerbHead::wire.run(p_stream, head);
}

bool NetwMultiplayer::verb_head_read(
    wire::ReadStream &p_stream,
    int64_t &r_route,
    int64_t &r_epoch
) {
    spawn::VerbHead head;
    if (!spawn::VerbHead::wire.run(p_stream, head) || head.route == 0) {
        return false;
    }
    r_route = int64_t(head.route);
    r_epoch = int64_t(head.epoch);
    return true;
}

bool NetwMultiplayer::anchor_encode(
    wire::WriteStream &p_stream,
    Node *p_target
) {
    Node *target = p_target;
    if (target == nullptr) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(target);
    const int64_t route
        = entity.is_valid() ? liveness_route_of(entity.ptr()) : int64_t(0);
    if (entity.is_valid() && route > 0) {
        Node *owner = entity->get_owner();
        if (owner != nullptr) {
            bool entity_relative = true;
            uint64_t staged = uint64_t(route);
            String subpath = String(owner->get_path_to(target));
            return p_stream.bool1(entity_relative)
                && p_stream.varuint(staged, 5)
                && wire::string_field(p_stream, subpath);
        }
    }
    Node *root = session_root();
    if (root == nullptr || !(target == root || root->is_ancestor_of(target))) {
        return false;
    }
    bool entity_relative = false;
    String path = String(root->get_path_to(target));
    return p_stream.bool1(entity_relative)
        && wire::string_field(p_stream, path);
}

bool NetwMultiplayer::anchor_decode(
    wire::ReadStream &p_stream,
    Dictionary &r_anchor
) const {
    bool entity_relative = false;
    if (!p_stream.bool1(entity_relative)) {
        return false;
    }
    uint64_t route = 0;
    if (entity_relative && !p_stream.varuint(route, 5)) {
        return false;
    }
    String path;
    if (!wire::string_field(p_stream, path)) {
        return false;
    }
    r_anchor[StringName("kind")] = entity_relative ? int64_t(1) : int64_t(0);
    r_anchor[StringName("route")]
        = entity_relative ? int64_t(route) : int64_t(0);
    r_anchor[StringName("path")] = path;
    return true;
}

Node *NetwMultiplayer::anchor_resolve(const Dictionary &p_anchor) {
    const String path = p_anchor.get(StringName("path"), String());
    if (int64_t(p_anchor.get(StringName("kind"), int64_t(0))) == 1) {
        const Ref<NetwEntity> entity = wrapper_for_route(
            int64_t(p_anchor.get(StringName("route"), int64_t(0)))
        );
        if (entity.is_null()) {
            return nullptr;
        }
        Node *owner = entity->get_owner();
        return owner != nullptr ? owner->get_node_or_null(NodePath(path))
                                : nullptr;
    }
    Node *root = session_root();
    return root != nullptr ? root->get_node_or_null(NodePath(path)) : nullptr;
}

Error NetwMultiplayer::entity_call(
    const RID &p_entity,
    int64_t p_comp,
    const StringName &p_method,
    const Array &p_args,
    int64_t p_peer
) {
    Node *node = entity_component_node(p_entity, p_comp);
    if (node == nullptr) {
        return entity_get_state(p_entity) == ENTITY_STATE_LIVE
            ? ERR_UNAVAILABLE
            : ERR_DOES_NOT_EXIST;
    }
    if (String(p_method).is_empty() || !node->has_method(p_method)) {
        return ERR_DOES_NOT_EXIST;
    }
    const CallTarget target = rpc_target(node, p_method);
    if (target.code != OK) {
        return target.code;
    }
    String refusal;
    if (rpc_declared(node, p_method, refusal) != OK) {
        return ERR_UNCONFIGURED;
    }
    rpc_call(Callable(node, p_method), p_args, p_peer);
    return OK;
}

void NetwMultiplayer::liveness_when_live(
    int64_t p_route,
    const Callable &p_callback,
    int64_t p_timeout_ticks,
    const Callable &p_on_timeout
) {
    const ClockEngine &clock = clock_engine();
    const bool clocked = clock.get_configured();
    int64_t timeout = p_timeout_ticks;
    if (timeout == 0) {
        timeout = clocked ? clock.get_tickrate() : CLOCKLESS_TICKRATE;
    }
    const int64_t origin
        = clocked ? clock.get_ticks_run() : liveness_core->frame();
    liveness_schedule_when_live(
        p_route,
        p_callback,
        origin,
        timeout,
        clocked,
        p_on_timeout
    );
}

PackedInt64Array NetwMultiplayer::liveness_claim_routes(int p_count) {
    PackedInt64Array out;
    NETW_ERR_COND_V(
        !is_host(),
        out,
        sys::LIVENESS,
        "NetwMultiplayer.liveness_claim_routes runs only on the server."
    );

    if (p_count <= 0) {
        return out;
    }
    out.resize(p_count);
    for (int at = 0; at < p_count; at++) {
        out.set(at, liveness_reserve_route());
    }
    liveness_bind_routes_data(out);
    return out;
}

Error NetwMultiplayer::liveness_release_routes(
    const PackedInt64Array &p_routes
) {
    NETW_ERR_COND_V(
        !is_host(),
        ERR_UNCONFIGURED,
        sys::LIVENESS,
        "NetwMultiplayer.liveness_release_routes runs only on the server."
    );

    if (p_routes.is_empty()) {
        return OK;
    }
    liveness_tombstone_routes_data(p_routes);
    store_world.arrays.retire_routes(p_routes, false);
    store_world.arrays.queue_lifecycle_removals(p_routes);
    return OK;
}

Error NetwMultiplayer::entity_despawn(
    const RID &p_entity,
    const Ref<NetwDespawnOpts> &p_opts
) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    String refusal;
    const lifecycle::Ruling ruling = lifecycle_judge_verb(
        lifecycle_local_facts(wrapper.ptr(), lifecycle::Kind::DESPAWN),
        String("Netw.despawn"),
        wrapper.is_valid() ? wrapper->get_owner() : nullptr,
        refusal
    );
    if (!ruling.admitted()) {
        return ruling.code;
    }
    if (wrapper.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    wrapper->despawn(p_opts);
    return OK;
}

Error NetwMultiplayer::entity_add_property_set(
    const RID &p_entity,
    const RID &p_set,
    int64_t p_comp
) {
    if (!liveness_core->entity_is_valid(p_entity)
        || !property_sets.rid_is_valid(p_set)) {
        return ERR_DOES_NOT_EXIST;
    }
    const PropertySet *record = property_set_of(p_set);
    if (record == nullptr || !record->get_sealed()) {
        return ERR_INVALID_DATA;
    }
    if (entity_get_view(p_entity).is_null()) {
        return ERR_UNAVAILABLE;
    }
    Node *node = entity_component_node(p_entity, p_comp);
    if (node == nullptr) {
        return ERR_UNAVAILABLE;
    }
    SyncPipeline *pipeline = sync_pipeline();
    if (pipeline == nullptr) {
        return ERR_UNCONFIGURED;
    }
    const Error verdict
        = pipeline->register_property_set(node, property_set_id_of(p_set));
    if (verdict != OK) {
        return verdict;
    }
    entity_property_sets[p_entity.get_id()][p_comp] = p_set;
    return OK;
}

void NetwMultiplayer::entity_remove_property_set(
    const RID &p_entity,
    int64_t p_comp
) {
    HashMap<int64_t, RID> *attached
        = entity_property_sets.getptr(p_entity.get_id());
    if (attached == nullptr || !attached->has(p_comp)) {
        return;
    }
    Node *node = entity_component_node(p_entity, p_comp);
    SyncPipeline *pipeline = sync_pipeline();
    if (node != nullptr && pipeline != nullptr) {
        pipeline->unregister_derived(node);
        pipeline->reconcile_state_timeline(node);
    }
    attached->erase(p_comp);
    if (attached->is_empty()) {
        entity_property_sets.erase(p_entity.get_id());
    }
}

Variant NetwMultiplayer::entity_get_property(
    const RID &p_entity,
    int64_t p_comp,
    int p_column
) {
    const HashMap<int64_t, RID> *attached
        = entity_property_sets.getptr(p_entity.get_id());
    if (attached == nullptr) {
        return Variant();
    }
    const RID *held = attached->getptr(p_comp);
    if (held == nullptr) {
        return Variant();
    }
    const PropertySet *record = property_set_of(*held);
    if (record == nullptr) {
        return Variant();
    }
    if (p_column < 0 || p_column >= int(record->columns.size())) {
        return Variant();
    }
    Node *node = entity_component_node(p_entity, p_comp);
    if (node == nullptr) {
        return Variant();
    }
    return port_get(node, record->columns[uint32_t(p_column)].shape.key);
}

void NetwMultiplayer::observe_node_added(Node *p_node) {
    observe_node_entity(p_node);
    settle_observe(p_node);
}

void NetwMultiplayer::entity_watch_authority_joins() {
    tree_watch_authority_joins();
}

void NetwMultiplayer::entity_note_node_added(Node *p_node) {
    if (tree::is_entity_root(p_node)) {
        return;
    }
    const Ref<NetwEntity> entity = wrapper_at(p_node->get_parent());
    if (entity.is_null() || entity->get_record() == nullptr
        || !entity->get_record()->projects_to_server()) {
        return;
    }
    entity->descendant_entered(p_node);
}

void NetwMultiplayer::observe_node_entity_ref(const Variant &p_node_ref) {
    Node *node = Object::cast_to<Node>(gd::held_by_weak_ref(p_node_ref));
    if (node == nullptr) {
        return;
    }
    observe_node_entity(node);
}

void NetwMultiplayer::observe_node_entity(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null()) {
        return;
    }
    if (entity->get_name() != StringName()) {
        lagcomp_effect_adopt(entity->get_name());
    }
}

void NetwMultiplayer::observe_entity_spawned(int64_t p_wrapper_id) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(
        gd::object_of(godot::ObjectID(uint64_t(p_wrapper_id)))
    );
    if (entity == nullptr || entity->get_name() == StringName()) {
        return;
    }
    lagcomp_effect_adopt(entity->get_name());
}

} // namespace netw
