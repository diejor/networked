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
#include "netw/synchronizers.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

const char *SIG_ENTITY_DEAD = "entity_dead";
const char *SIG_ENTITY_HIDDEN = "entity_hidden";
const char *SIG_ENTITY_LINGERING = "entity_lingering";
const char *SIG_ENTITY_LIVE = "entity_live";

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
    const int64_t id = p_entity.get_id();
    live_wrappers[id] = p_wrapper;
    retired_wrappers.erase(id);
    handle_by_wrapper[uint64_t(gd::instance_id(p_wrapper.ptr()))] = id;
    if (p_owner != nullptr) {
        wrapper_owners[id] = gd::instance_id(p_owner);
    }
}

Node *NetwMultiplayer::wrapper_owner(const RID &p_entity) const {
    const godot::ObjectID *found = wrapper_owners.getptr(p_entity.get_id());
    return found ? Object::cast_to<Node>(gd::instance_from_id(*found))
                 : nullptr;
}

RID NetwMultiplayer::handle_of_wrapper(NetwEntity *p_wrapper) const {
    if (p_wrapper == nullptr) {
        return RID();
    }
    const int64_t *id
        = handle_by_wrapper.getptr(uint64_t(gd::instance_id(p_wrapper)));
    if (id == nullptr) {
        return RID();
    }
    NetwEntityRecord *const *record = wrapper_records.getptr(*id);
    return record ? (*record)->get_handle() : RID();
}

namespace {

Callable &wrapper_mint() {
    static Callable factory;
    return factory;
}

} // namespace

StringName NetwMultiplayer::wrapper_meta() {
    return StringName("netw_entity");
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
    return Ref<NetwEntity>();
}

Ref<NetwEntity> NetwMultiplayer::wrapper_ensure(Node *p_root) {
    Node *root = p_root;
    if (root == nullptr) {
        return Ref<NetwEntity>();
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
    Ref<NetwEntity> minted = made;
    NETW_WARN_COND(
        minted.is_null(),
        sys::ENTITY,
        "the entity wrapper factory answered nothing for '%s'",
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

Variant NetwMultiplayer::wrapper_held(
    Node *p_node,
    const StringName &p_entity_id,
    int64_t p_peer_id
) {
    return gd::held(wrapper_bind(p_node, p_entity_id, p_peer_id));
}

Object *NetwMultiplayer::wrapper_bind(
    Node *p_node,
    const StringName &p_entity_id,
    int64_t p_peer_id
) {
    Node *node = p_node;
    if (node == nullptr) {
        return nullptr;
    }
    const String named
        = entity::Identity::format(String(p_entity_id), p_peer_id);
    NETW_ERR_COND_V(
        named.is_empty(),
        p_node,
        sys::ENTITY,
        "'%s' cannot be named on the wire, so nothing is bound",
        p_entity_id
    );
    node->set_name(named);
    const Ref<NetwEntity> wrapper = wrapper_ensure(node);
    if (wrapper.is_valid()) {
        wrapper->set_entity_id(p_entity_id);
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

Ref<NetwSceneHandle> NetwMultiplayer::entity_scene_facet(
    NetwEntityRecord *p_record,
    NetwEntity *p_wrapper
) {
    if (p_record == nullptr) {
        return Ref<NetwSceneHandle>();
    }
    const RID own = p_record->get_handle();
    const RID resolved = scene_of(own);
    if (resolved.is_valid() && resolved != own) {
        const Ref<NetwSceneHandle> host = scene_handle_of(resolved);
        if (host.is_valid()) {
            return host;
        }
    }
    return p_record->part(NetwEntityRecord::PART_SCENE, p_wrapper);
}

bool NetwMultiplayer::entity_reparent_crosses(
    const RID &p_entity,
    const RID &p_destination
) {
    NetwEntityRecord *const *record = wrapper_records.getptr(p_entity.get_id());
    if (record == nullptr || (*record)->get_peer_id() == 0) {
        return false;
    }
    const RID destination_scene = scene_of(p_destination);
    if (!destination_scene.is_valid()) {
        return false;
    }
    return destination_scene != scene_of(p_entity);
}

void NetwMultiplayer::entity_reparent(
    NetwEntityRecord *p_record,
    Node *p_owner,
    Node *p_new_parent,
    const Ref<NetwReparentOpts> &p_opts
) {
    NetwEntityRecord *record = p_record;
    Node *owner = p_owner;
    const RID destination = entity_at_or_above(p_new_parent);
    if (record != nullptr
        && entity_reparent_crosses(record->get_handle(), destination)) {
        NETW_TRACE(
            sys::ENTITY,
            "reparent carries peer %d across a scene boundary",
            int(record->get_peer_id())
        );
        scene_admit(scene_of(destination), record->get_peer_id());
    }
    entity_move(record, owner, p_new_parent, p_opts);
}

void NetwMultiplayer::entity_move(
    NetwEntityRecord *p_record,
    Node *p_owner,
    Node *p_new_parent,
    const Ref<NetwReparentOpts> &p_opts
) {
    Node *owner = p_owner;
    Node *parent = p_new_parent;
    NETW_ERR_COND(
        owner == nullptr || parent == nullptr,
        sys::ENTITY,
        "a reparent needs an owner and a destination parent"
    );
    NETW_TRACE(sys::ENTITY, "moving an entity under %s", parent->get_name());
    NetwEntityRecord::MoveReport report;
    report.reason = p_opts.is_valid() ? p_opts->get_reason() : StringName();
    if (p_record != nullptr) {
        p_record->set_move_report(report);
    }
    if (owner->is_inside_tree()) {
        owner->reparent(parent);
    } else {
        if (owner->get_parent() != nullptr) {
            owner->get_parent()->remove_child(owner);
        }
        parent->add_child(owner);
    }
    if (p_record != nullptr) {
        p_record->set_move_report(NetwEntityRecord::MoveReport());
    }
}

void NetwMultiplayer::entity_free_owner(Node *p_owner) {
    if (p_owner != nullptr) {
        p_owner->queue_free();
    }
}

namespace {

Node *instantiate_scene_of(Node *p_template) {
    Node *template_node = p_template;
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
    p_configure.call(NetwMultiplayer::wrapper_ensure(p_copy));
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
    const bool is_reparent
        = p_record->get_stage() == int64_t(entity::Stage::LIVE);
    p_record->hydrate_identity(owner);

    if (!is_reparent
        && p_record->get_stage() == int64_t(entity::Stage::UNBOUND)) {
        if (!p_record->classify_activation(owner)) {
            if (p_session != nullptr) {
                p_session->entity_note_stage(
                    p_record,
                    int64_t(entity::Stage::UNBOUND)
                );
            }
            return;
        }
        NetwEntity *armed = Object::cast_to<NetwEntity>(p_wrapper);
        if (armed != nullptr) {
            armed->arm(Ref<NetwMultiplayer>());
        }
    }

    if (!is_reparent && p_record->get_route() == 0 && p_session != nullptr
        && p_is_authority) {
        p_record->set_route(p_session->liveness_allocate_route(p_wrapper));
    }
    if (p_record->get_route() > 0 && p_session != nullptr) {
        p_session->liveness_bind_route(p_record->get_route(), p_wrapper);
    }

    if (is_reparent) {
        p_record->apply_control(p_wrapper, owner, p_is_authority);
    }

    const Callable ready(p_wrapper, StringName("_on_owner_ready"));
    if (!owner->is_connected(StringName("ready"), ready)) {
        owner->connect(StringName("ready"), ready);
    }
    const int64_t steering
        = p_record->get_control()->resolve(p_record->get_peer_id());
    Ref<MultiplayerAPI> api = owner->get_multiplayer();
    if ((p_record->get_peer_id() != 0 || steering != 0) && api.is_valid()) {
        const Callable dropped(p_wrapper, StringName("_on_peer_disconnected"));
        if (!api->is_connected(StringName("peer_disconnected"), dropped)) {
            api->connect(StringName("peer_disconnected"), dropped);
        }
    }

    if (!is_reparent) {
        const int64_t from = p_record->get_stage();
        p_record->transition(int64_t(entity::Stage::LIVE), owner);
        NETW_TRACE(sys::ENTITY, "'%s' is live", owner->get_name());
        if (p_session != nullptr) {
            p_session->entity_note_stage(p_record, from);
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
        p_wrapper->emit_signal(StringName("spawning"));
        if (p_record->get_declares_scene() && p_session != nullptr) {
            p_session->scene_root_online(owner);
        }
    }
}

void NetwMultiplayer::entity_wrapper_request_control(
    const Ref<NetwEntity> &p_wrapper,
    Node *p_owner,
    ReplicationCore *p_plane
) {
    Node *owner = p_owner;
    if (owner == nullptr || p_wrapper.is_null()) {
        return;
    }
    const Ref<MultiplayerAPI> api = owner->get_multiplayer();
    const bool connected
        = api.is_valid() && api->get_multiplayer_peer().is_valid();
    if (!connected) {
        p_wrapper->_handle_control_request(
            api.is_valid() ? int64_t(api->get_unique_id()) : int64_t(0)
        );
        return;
    }
    if (p_plane != nullptr) {
        NETW_TRACE(sys::ENTITY, "asking the server for control");
        p_plane->request_control(p_wrapper);
    }
}

void NetwMultiplayer::entity_broadcast_control(
    const Ref<NetwEntity> &p_wrapper,
    ReplicationCore *p_plane,
    int64_t p_peer
) {
    if (p_wrapper.is_null() || p_plane == nullptr) {
        return;
    }
    p_plane->broadcast_control(p_wrapper, p_peer);
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

void NetwMultiplayer::entity_request_control(const RID &p_entity) {
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
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

RID NetwMultiplayer::spawn_fn(
    const Callable &p_function,
    const Array &p_args,
    NetwParticipant *p_owner
) {
    NETW_ZONE_NC("session spawn function", colors::LIVENESS);
    spawn::Pipeline *pipeline = spawn_plane();
    NETW_ERR_COND_V(
        pipeline == nullptr,
        RID(),
        sys::SPAWN,
        "a spawn function ran with no spawn pipeline installed"
    );
    Object *node
        = pipeline->spawn(p_function, p_args, Ref<NetwParticipant>(p_owner));
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
        "constructor %s was registered with no spawn pipeline installed",
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
        "constructor %s ran with no spawn pipeline installed",
        String(p_id)
    );
    Object *node = pipeline->spawn_registered(
        p_id,
        p_args,
        Ref<NetwParticipant>(Object::cast_to<NetwParticipant>(p_owner))
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
        "a node was adopted with no spawn pipeline installed"
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
        "spawn state was read with no spawn pipeline installed"
    );
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    Node *owner = wrapper.is_valid() ? wrapper->get_owner() : nullptr;
    if (owner == nullptr) {
        return TypedArray<Dictionary>();
    }
    return pipeline->collect_spawn_state(owner);
}

Ref<NetwPropertySetBinding> NetwMultiplayer::entity_derived_binding(
    Node *p_owner,
    int64_t p_record,
    int64_t p_route
) {
    ReplicationCore *plane = get_replication_plane();
    if (p_owner == nullptr || plane == nullptr) {
        return Ref<NetwPropertySetBinding>();
    }
    const Ref<NetwPropertySetBinding> declared
        = plane->derived_binding(p_owner, p_record);
    if (declared.is_valid()) {
        return declared;
    }
    if (p_route <= 0) {
        return Ref<NetwPropertySetBinding>();
    }
    const Array group = entity_derived_group(p_route);
    for (int at = 0; at < group.size(); at++) {
        const Ref<NetwPropertySetBinding> candidate = group[at];
        if (candidate.is_null() || candidate->set.is_null()) {
            continue;
        }
        if (candidate->set->get_record() == p_record) {
            return candidate;
        }
    }
    return Ref<NetwPropertySetBinding>();
}

Array NetwMultiplayer::entity_derived_group(int64_t p_route) {
    ReplicationCore *plane = get_replication_plane();
    if (plane == nullptr || p_route <= 0) {
        return Array();
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
    const Array group = entity_derived_group(p_route);
    for (int at = 0; at < group.size(); at++) {
        const Ref<NetwPropertySetBinding> binding = group[at];
        if (binding.is_null() || binding->node() != target.object) {
            continue;
        }
        const Ref<NetwPropertySet> declaration = binding->get_set();
        if (declaration.is_null()) {
            continue;
        }
        const TypedArray<NetwPropertySetColumn> columns
            = declaration->get_columns();
        for (int row = 0; row < columns.size(); row++) {
            const Ref<NetwPropertySetColumn> column = columns[row];
            if (column.is_null()) {
                continue;
            }
            if (NodePath(String(":") + String(column->get_key()))
                == target.sub) {
                return true;
            }
        }
    }
    return false;
}

Node *NetwMultiplayer::entity_instantiate_copy(
    Node *p_template,
    const Callable &p_configure
) {
    Node *copy = instantiate_scene_of(p_template);
    if (copy == nullptr) {
        return nullptr;
    }
    configure_copy(copy, p_configure);
    return copy;
}

Node *NetwMultiplayer::entity_instantiate_from(
    Node *p_template,
    const Callable &p_configure
) {
    Node *copy = instantiate_scene_of(p_template);
    if (copy == nullptr) {
        return nullptr;
    }
    spawn::Pipeline *pipeline = spawn_plane();
    if (pipeline == nullptr) {
        NETW_TRACE(
            sys::ENTITY,
            "no spawn pipeline, so the copy carries no marked value"
        );
    } else {
        NETW_ZONE_NC("carry marked spawn state", colors::LIVENESS);
        const Array marked = pipeline->collect_spawn_state(p_template);
        NETW_TRACE(sys::ENTITY, "copying %d marked values", marked.size());
        for (int at = 0; at < marked.size(); at++) {
            const Dictionary row = marked[at];
            Object *carried = row["node"];
            Node *source = Object::cast_to<Node>(carried);
            const StringName property = row["prop"];
            if (source == nullptr) {
                continue;
            }
            Node *target = source == p_template
                ? copy
                : copy->get_node_or_null(p_template->get_path_to(source));
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
    Node *p_parent,
    const StringName &p_id
) {
    if (p_copy == nullptr) {
        return nullptr;
    }
    if (!p_id.is_empty()) {
        NetwMultiplayer::wrapper_bind(p_copy, p_id, 0);
    } else {
        NetwMultiplayer::wrapper_ensure(p_copy);
    }
    Node *destination = p_parent;
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

Node *NetwMultiplayer::entity_spawn_under(
    Node *p_owner,
    Node *p_parent,
    const StringName &p_id
) {
    Node *owner = p_owner;
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

Node *NetwMultiplayer::entity_spawn_copy_under(
    Node *p_owner,
    Node *p_parent,
    const StringName &p_id
) {
    Node *owner = p_owner;
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

Node *NetwMultiplayer::entity_instantiate_player(
    Node *p_owner,
    Object *p_participant
) {
    Node *owner = p_owner;
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

Node *NetwMultiplayer::entity_spawn_player(
    Node *p_owner,
    NetwParticipant *p_participant,
    NetwSceneHandle *p_scene
) {
    Node *copy = entity_instantiate_player(p_owner, p_participant);
    if (copy == nullptr || p_scene == nullptr) {
        return copy;
    }
    p_scene->add_player(wrapper_at(copy));
    return copy;
}

Dictionary NetwMultiplayer::entity_describe(int64_t p_route) const {
    Dictionary out;
    const RID entity = liveness_core->rid_from_route(int(p_route));
    NetwEntityRecord *const *found = wrapper_records.getptr(entity.get_id());
    if (found == nullptr) {
        return out;
    }
    NetwEntityRecord *const record = *found;
    out["route"] = p_route;
    out["entity_id"] = record->get_entity_id();
    out["peer_id"] = record->get_peer_id();
    out["stage"] = record->get_stage();
    out["controller"] = record->get_control()->resolve(record->get_peer_id());
    out["liveness"] = liveness_route_state(p_route);
    out["layers"] = interest_engine.memberships(entity.get_id());
    return out;
}

void NetwMultiplayer::entity_note_stage(
    NetwEntityRecord *p_record,
    int64_t p_from
) {
    if (p_record == nullptr || p_record->get_stage() == p_from) {
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

void NetwMultiplayer::entity_announce_reparented(
    const Ref<NetwEntity> &p_wrapper,
    const NetwEntityRecord::MoveReport &p_report
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
        Dictionary detail;
        detail["reason"] = p_report.reason;
        event_emit(
            EventPlane::REPARENTED,
            route,
            detail,
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
    NETW_TRACE(sys::ENTITY, "lingering for %d pumps", int(p_pumps));
    session_defer_after(
        callable_mp(this, &NetwMultiplayer::entity_free_owner).bind(owner),
        StringName(),
        int(p_pumps)
    );
}

Ref<NetwEntity> NetwMultiplayer::entity_get_view(const RID &p_entity) const {
    const Ref<NetwEntity> *found = live_wrappers.getptr(p_entity.get_id());
    return found ? *found : Ref<NetwEntity>();
}

Ref<NetwEntity> NetwMultiplayer::wrapper_for_route(int64_t p_route) const {
    return entity_get_view(liveness_core->rid_from_route(int(p_route)));
}

Ref<NetwEntity> NetwMultiplayer::wrapper_for_id(int64_t p_id) const {
    const Ref<NetwEntity> *found = live_wrappers.getptr(p_id);
    if (found == nullptr) {
        found = retired_wrappers.getptr(p_id);
    }
    return found ? *found : Ref<NetwEntity>();
}

TypedArray<Object> NetwMultiplayer::wrapper_live() const {
    TypedArray<Object> out;
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
    retired_wrappers.clear();
}

void NetwMultiplayer::wrapper_clear() {
    live_wrappers.clear();
    retired_wrappers.clear();
    wrapper_records.clear();
}

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
        wrapper_records[p_entity.get_id()] = p_record;
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

void NetwMultiplayer::liveness_publish_live(int64_t p_route) {
    const Ref<NetwEntity> wrapper = wrapper_for_route(p_route);
    NetwEntityRecord *const record = record_of_wrapper(wrapper.ptr());
    if (record != nullptr) {
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

void NetwMultiplayer::liveness_settle_local_player(int64_t p_route) {
    if (!has_multiplayer_peer()) {
        return;
    }
    const RID entity = liveness_core->rid_from_route(int(p_route));
    const int64_t id = entity.get_id();
    NetwEntityRecord *const *record = wrapper_records.getptr(id);
    if (record == nullptr || (*record)->get_peer_id() == 0
        || (*record)->get_peer_id() != get_unique_id()) {
        return;
    }
    set_local_player(entity_get_view(entity), id);
}

bool NetwMultiplayer::liveness_linger(const RID &p_entity) {
    if (!liveness_core
             ->set_state(p_entity, NetwLivenessCore::STATE_LINGERING)) {
        return false;
    }
    emit_signal(
        SIG_ENTITY_LINGERING,
        int64_t(liveness_core->route_of(p_entity)),
        entity_get_view(p_entity)
    );
    return true;
}

void NetwMultiplayer::liveness_unindex_wrapper(const RID &p_entity) {
    const int64_t id = p_entity.get_id();
    const Ref<NetwEntity> *found = live_wrappers.getptr(id);
    if (found != nullptr) {
        retired_wrappers[id] = *found;
        live_wrappers.erase(id);
    }
}

void NetwMultiplayer::liveness_forget_wrapper(const RID &p_entity) {
    const int64_t id = p_entity.get_id();
    if (local_player_id == id) {
        set_local_player(Ref<NetwEntity>(), 0);
    }
    wrapper_records.erase(id);
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
    display_release_route(p_route);
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
        Object *held = wrapper.ptr();
        const Callable despawning = despawning_hook(held);
        if (held->is_connected(StringName("despawning"), despawning)) {
            held->disconnect(StringName("despawning"), despawning);
        }
    }
    entity_release_body(entity, wrapper, p_route);
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
    emit_signal(SIG_ENTITY_HIDDEN, p_route, wrapper);
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

Callable NetwMultiplayer::despawning_hook(Object *p_wrapper) {
    return callable_mp(this, &NetwMultiplayer::liveness_owner_despawning)
        .bind(p_wrapper);
}

int64_t NetwMultiplayer::liveness_reserve_route() {
    return liveness_core->reserve_route();
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

    Node *node = owner;
    if (node != nullptr) {
        const Callable despawning = despawning_hook(p_wrapper);
        if (!p_wrapper->is_connected(StringName("despawning"), despawning)) {
            p_wrapper->connect(StringName("despawning"), despawning);
        }
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

TypedArray<Object> NetwMultiplayer::liveness_get_entities() const {
    return wrapper_live();
}

void NetwMultiplayer::liveness_schedule_when_live(
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

int64_t NetwMultiplayer::liveness_pending_live_count() const {
    return liveness_core->pending_live_count();
}

void NetwMultiplayer::liveness_poll(int64_t p_clock_tick) {
    const PackedInt32Array expired = liveness_core->poll(int(p_clock_tick));
    for (int at = 0; at < expired.size(); at++) {
        NETW_WARN(
            sys::LIVENESS,
            "when_live timed out for route %d",
            expired[at]
        );
    }
}

void NetwMultiplayer::liveness_poll_now() {
    NETW_ZONE_NC("session liveness poll now", colors::LIVENESS);
    liveness_poll(
        clock_engine().get_configured() ? clock_engine().get_tick() : 0
    );
}

void NetwMultiplayer::liveness_clear_session() {
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
    if (entity->get_declares_scene()) {
        scene_root_offline(entity->get_owner());
    }
    const uint64_t instance = uint64_t(entity->get_instance_id());
    EntityDeparture *standing = entity_departures.getptr(instance);
    EntityDeparture fresh;
    EntityDeparture &row = standing != nullptr ? *standing : fresh;
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
    if (row.route > 0) {
        if (ReplicationCore *plane = get_replication_plane()) {
            row.received
                = plane->get_spawn_pipeline()->holds_received_route(row.route);
        }
    }
    if (NetwEntityRecord *const record = entity->get_record()) {
        const NetwEntityRecord::MoveReport &asked = record->get_move_report();
        row.report.reason = asked.reason;
    }
    entity_capture_persistence(row);
    entity_capture_seat(row, entity);
    if (standing == nullptr) {
        entity_departures.insert(instance, row);
    }
    session_defer(
        callable_mp(this, &NetwMultiplayer::entity_settle_departure)
            .bind(int64_t(instance)),
        StringName(vformat("entity-departure?%d", int64_t(instance)))
    );
}

void NetwMultiplayer::entity_capture_persistence(EntityDeparture &r_row) {
    const Ref<NetwPersistenceEngine> engine
        = persistence.engines.engine_of(r_row.entity);
    if (engine.is_null() || !persistence_serves()) {
        return;
    }
    r_row.saved = engine;
    r_row.saved_write = engine->capture_write();
}

void NetwMultiplayer::entity_capture_seat(
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
    for (uint32_t at = 0; at < r_row.seats.size(); at++) {
        if (r_row.seats[at].scene == leaving && r_row.seats[at].peer == peer) {
            return;
        }
    }
    DepartedSeat seat;
    seat.scene = leaving;
    seat.peer = peer;
    r_row.seats.push_back(seat);
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
    const uint64_t instance = uint64_t(p_instance);
    EntityDeparture *standing = entity_departures.getptr(instance);
    if (standing == nullptr) {
        return;
    }
    const EntityDeparture row = *standing;
    entity_departures.erase(instance);
    switch (entity_departure_outcome(row)) {
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
    if (SyncPipeline *pipeline = sync_pipeline()) {
        pipeline->reconcile_dropped_state_timelines();
    }
}

void NetwMultiplayer::entity_release_seats(
    const EntityDeparture &p_row,
    const RID &p_keep
) {
    for (uint32_t at = 0; at < p_row.seats.size(); at++) {
        const DepartedSeat &seat = p_row.seats[at];
        if (seat.scene == p_keep) {
            continue;
        }
        scene_release_departed(
            seat.scene,
            p_row.entity,
            p_row.wrapper.is_valid() && p_row.wrapper->get_owner() != nullptr,
            seat.peer
        );
    }
}

void NetwMultiplayer::entity_commit_move(const EntityDeparture &p_row) {
    entity_release_seats(p_row, scene_of(p_row.entity));
    if (p_row.route > 0) {
        if (ReplicationCore *plane = get_replication_plane()) {
            plane->get_spawn_pipeline()->settle_move(p_row.route);
        }
    }
    entity_relocate_spatial_state(
        p_row,
        clock_engine().get_configured() ? clock_engine().get_tick() : 0
    );
    entity_refresh_moved_body(p_row.entity, p_row.route);
    entity_announce_reparented(p_row.wrapper, p_row.report);
}

void NetwMultiplayer::entity_relocate_spatial_state(
    const EntityDeparture &p_row,
    int64_t p_tick
) {
    Node *owner
        = p_row.wrapper.is_valid() ? p_row.wrapper->get_owner() : nullptr;
    const Ref<NetwTimeline> timeline = lagcomp_timeline_of(p_row.entity);
    if (timeline.is_valid()) {
        timeline->trim_before(p_tick + 1);
    }
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
            plane->get_spawn_pipeline()->settle_death(p_row.route);
        }
        liveness_drop_hooks(p_row.route);
        liveness_release(p_row.route);
    }
    if (p_row.saved.is_valid() && p_row.saved_write.is_addressed()) {
        p_row.saved->submit(p_row.saved_write);
    }
    entity_release_seats(p_row, RID());
    entity_release_body(p_row.entity, p_row.wrapper, p_row.route);
    if (p_row.wrapper.is_valid()) {
        NetwEntityRecord *const record = p_row.wrapper->get_record();
        const int64_t from = record->get_stage();
        if (record->complete_departure(p_row.wrapper.ptr())) {
            entity_note_stage(record, from);
        }
    }
    if (routed) {
        liveness_announce_dead(p_row.route);
    }
}

void NetwMultiplayer::entity_release_body(
    const RID &p_entity,
    const Ref<NetwEntity> &p_wrapper,
    int64_t p_route
) {
    persistence.engines.drop(p_entity);
    interest_release_body(p_wrapper);
    unregister_prediction(p_wrapper);
    lagcomp_timeline_undeclare(p_entity);
    if (p_route > 0) {
        replication_clear_route(p_route);
        display_release_route(p_route);
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
    entity_release_body(p_row.entity, p_row.wrapper, p_row.route);
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
    Object *held = wrapper.ptr();
    const Callable despawning = despawning_hook(held);
    if (held->is_connected(StringName("despawning"), despawning)) {
        held->disconnect(StringName("despawning"), despawning);
    }
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
        "an entity was admitted off server authority"
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

void NetwMultiplayer::entity_grant_control(
    const RID &p_entity,
    int64_t p_peer
) {
    if (!is_host()) {
        return;
    }
    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
    if (wrapper.is_valid()) {
        wrapper->grant_control(p_peer);
    }
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
    (void)p_route;
    if (p_sender != 1) {
        return ERR_UNAUTHORIZED;
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
        "declaring an entity this peer does not hold a wrapper for"
    );
    return ERR_DOES_NOT_EXIST;
}

bool NetwMultiplayer::spawn_locally_desired(int64_t p_peer_id, Node *p_node) {
    if (p_node == nullptr) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_valid()) {
        if (entity->get_controller() == p_peer_id) {
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
    if (p_book == nullptr || !p_book->parent_admits(p_route, p_peer_id)) {
        return false;
    }
    return spawn_locally_desired(p_peer_id, p_node);
}

Ref<NetwEntity> NetwMultiplayer::spawn_arm_identity(
    spawn::Record *p_record,
    Node *p_node,
    const Ref<NetwParticipant> &p_owner,
    const Callable &p_declare
) {
    if (p_record == nullptr || p_node == nullptr) {
        return Ref<NetwEntity>();
    }
    const Ref<NetwEntity> entity = NetwEntity::ensure(p_node);
    if (entity.is_null()) {
        return entity;
    }
    const int64_t held_route = entity->get_route();
    const StringName held_id = entity->get_entity_id();
    const int64_t held_peer = entity->get_peer_id();
    const int64_t held_controller = entity->get_controller();

    int64_t route = liveness_route_of(entity.ptr());
    if (route <= 0) {
        route = entity->get_route();
    }
    if (route <= 0) {
        route = liveness_reserve_route();
    }
    entity->set_route(route);
    if (entity->get_entity_id() == StringName()) {
        const String stem = spawn::Book::recipe_base(
            p_record,
            p_node->get_scene_file_path(),
            String(p_node->get_name())
        );
        entity->set_entity_id(
            StringName(stem + String("@") + String::num_int64(route))
        );
    }
    if (p_owner.is_valid()) {
        entity->set_peer_id(p_owner->get_peer_id());
        entity->set_controller(p_owner->get_peer_id());
    }
    p_record->set_route(route);
    p_record->bind_node(p_node);
    p_record->set_entity_id(entity->get_entity_id());
    p_record->set_peer_id(entity->get_peer_id());
    p_record->set_controller(entity->get_controller());

    Dictionary facts;
    facts[StringName("recipe")] = p_record->get_recipe();
    facts[StringName("route")] = route;
    facts[StringName("entity_id")] = p_record->get_entity_id();
    facts[StringName("peer")] = p_record->get_peer_id();
    facts[StringName("controller")] = p_record->get_controller();
    if (p_declare.is_valid()
        && int64_t(p_declare.call(entity_of(p_node), facts)) != int64_t(OK)) {
        entity->set_route(held_route);
        entity->set_entity_id(held_id);
        entity->set_peer_id(held_peer);
        entity->set_controller(held_controller);
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
    const int32_t own = int32_t(get_unique_id());
    PackedInt32Array roster;
    const PackedInt32Array known = reachable_peer_ids();
    for (int at = 0; at < known.size(); at++) {
        if (known[at] != own) {
            roster.push_back(known[at]);
        }
    }
    return send_recipients(p_entity, roster);
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
            "armed route %d left the tree before its spawn flushed, dropping "
            "the arm",
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
    return p_book->issue(armed);
}

PackedInt32Array NetwMultiplayer::spawn_fan_out(
    spawn::Book *p_book,
    spawn::Record *p_record,
    Node *p_node,
    const PackedByteArray &p_payload,
    const PackedInt32Array &p_connected,
    int64_t p_channel
) {
    PackedInt32Array recipients;
    if (p_book == nullptr || p_record == nullptr || p_node == nullptr
        || p_payload.is_empty()) {
        return recipients;
    }
    for (int at = 0; at < p_connected.size(); at++) {
        if (spawn_visible_to(
                p_book,
                p_record->get_route(),
                int64_t(p_connected[at]),
                p_node
            )) {
            recipients.push_back(p_connected[at]);
        }
    }
    p_record->set_recipients(recipients);
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

bool NetwMultiplayer::spawn_reparent_node(
    Node *p_node,
    Node *p_parent,
    const Callable &p_adopt
) {
    if (p_node == nullptr || p_parent == nullptr) {
        return false;
    }
    if (p_node->get_parent() == p_parent) {
        return false;
    }
    spawn_carry_begin(p_node, p_parent, p_adopt);
    return true;
}

void NetwMultiplayer::spawn_carry_begin(
    Node *p_node,
    Node *p_parent,
    const Callable &p_adopt
) {
    NETW_ZONE_NC("NetwMultiplayer spawn_carry_begin", colors::SCENE);
    const ObjectID node = gd::instance_id(p_node);
    for (KeyValue<int64_t, SpawnCarry> &flying : spawn_carries) {
        if (flying.value.node == node && !flying.value.moved) {
            flying.value.parent = gd::instance_id(p_parent);
            flying.value.adopt = p_adopt;
            return;
        }
    }
    SpawnCarry carry;
    carry.node = node;
    carry.parent = gd::instance_id(p_parent);
    carry.adopt = p_adopt;
    carry.guard = guard_hold(p_node);
    const int64_t id = ++spawn_carry_next;
    spawn_carries.insert(id, carry);
    spawn_carry_open(id);
}

void NetwMultiplayer::spawn_carry_open(int64_t p_id) {
    if (!spawn_carry_reachable(p_id)) {
        spawn_carry_abandon(p_id);
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
        spawn_carry_abandon(p_id);
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
    Node *held = node->get_parent();
    if (held != nullptr) {
        held->remove_child(node);
    }
    spawn_place_node(parent, node);
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
    NETW_TRACE(sys::SPAWN, "reparent carry %d abandoned mid-flight", int(p_id));
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
    int64_t p_channel
) {
    if (p_book == nullptr || p_record == nullptr || p_node == nullptr) {
        return false;
    }
    wire::WriteStream stream;
    if (!verb_head_write(stream, p_record->get_route())
        || !anchor_encode(stream, p_node->get_parent())
        || !stream.align_verify()) {
        NETW_WARN(
            sys::SPAWN,
            "reparented '%s' outside the MultiplayerTree, peers keep the old "
            "parent",
            String(p_node->get_name()).utf8().get_data()
        );
        return false;
    }
    const PackedByteArray payload = stream.to_bytes();
    const PackedInt32Array recipients = p_record->recipients();
    for (int at = 0; at < recipients.size(); at++) {
        const int64_t peer_id = int64_t(recipients[at]);
        if (!p_connected.has(recipients[at])) {
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
        "route %d reparented under '%s' with no matching spawner, keeping its "
        "origin anchor",
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
        if (node == nullptr || !node->is_inside_tree()) {
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
    const Ref<Script> script = node->get_script();
    if (script.is_null()) {
        return ERR_UNCONFIGURED;
    }
    const bool declared
        = netw::script::model::get_rpc_options(script, p_method).is_valid();
    if (!declared) {
        const Dictionary annotated = script->get_rpc_config();
        if (!annotated.has(p_method)) {
            NETW_WARN(
                sys::ENTITY,
                "refused entity_call to '%s': it carries no rpc "
                "declaration",
                String(p_method)
            );
            return ERR_UNCONFIGURED;
        }
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
    const int64_t origin = clocked ? clock.get_tick() : liveness_core->frame();
    liveness_schedule_when_live(
        p_route,
        p_callback,
        origin + timeout,
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
        "routes were claimed off server authority"
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
        "routes were released off server authority"
    );

    if (p_routes.is_empty()) {
        return OK;
    }
    liveness_tombstone_routes_data(p_routes);
    table_core->retire_routes(p_routes, false);
    table_core->queue_lifecycle_removals(p_routes);
    return OK;
}

Error NetwMultiplayer::entity_despawn(
    const RID &p_entity,
    const Ref<NetwDespawnOpts> &p_opts
) {
    NETW_ERR_COND_V(
        !is_host(),
        ERR_UNAUTHORIZED,
        sys::LIVENESS,
        "an entity was despawned off server authority"
    );

    const Ref<NetwEntity> wrapper = entity_get_view(p_entity);
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
    const Ref<NetwPropertySet> record = property_set_record(p_set);
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
    const Error verdict = pipeline->register_property_set(node, record);
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
    const Ref<NetwPropertySet> record = property_set_record(*held);
    if (record == nullptr) {
        return Variant();
    }
    const TypedArray<NetwPropertySetColumn> columns = record->get_columns();
    if (p_column < 0 || p_column >= int(columns.size())) {
        return Variant();
    }
    Node *node = entity_component_node(p_entity, p_comp);
    if (node == nullptr) {
        return Variant();
    }
    const Ref<NetwPropertySetColumn> column = columns[p_column];
    return node->get(column->shape.key);
}

void NetwMultiplayer::observe_node_added(Node *p_node) {
    observe_node_entity(p_node);
    settle_observe(p_node);
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
    if (entity->get_entity_id() != StringName()) {
        lagcomp_effect_adopt(entity->get_entity_id());
    }
    const Callable adopted
        = callable_mp(this, &NetwMultiplayer::observe_entity_spawned)
              .bind(int64_t(entity->get_instance_id()));
    if (entity->is_connected(StringName("spawned"), adopted)) {
        return;
    }
    entity->connect(StringName("spawned"), adopted);
}

void NetwMultiplayer::observe_entity_spawned(int64_t p_wrapper_id) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(
        gd::object_of(godot::ObjectID(uint64_t(p_wrapper_id)))
    );
    if (entity == nullptr || entity->get_entity_id() == StringName()) {
        return;
    }
    lagcomp_effect_adopt(entity->get_entity_id());
}

} // namespace netw
