#include "netw/entity_record.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "netw/entity_ids.hpp"
#include "netw/entity_identity.hpp"
#include "netw/entity_stage.hpp"
#include "netw/log.hpp"
#include "netw/synchronizers.hpp"

using namespace godot;

namespace netw {

namespace {

Callable *part_factories() {
    static Callable table[NetwEntityRecord::PART_MAX];
    return table;
}

Callable &scene_declaration() {
    static Callable reader;
    return reader;
}

} // namespace

NetwEntityRecord::NetwEntityRecord() {
    handle = entity_ids::mint();
    control.instantiate();
}

NetwEntityRecord::~NetwEntityRecord() {
    entity_ids::release(handle);
}

bool NetwEntityRecord::adopt_handle(const RID &p_handle) {
    if (p_handle == handle) {
        return true;
    }
    if (!entity_ids::retain(p_handle)) {
        return false;
    }
    entity_ids::release(handle);
    handle = p_handle;
    return true;
}

Ref<RefCounted> NetwEntityRecord::part(int64_t p_part, Object *p_wrapper) {
    NETW_ERR_COND_V(
        p_part < 0 || p_part >= PART_MAX,
        Ref<RefCounted>(),
        sys::ENTITY,
        "part %d is not a facet a record carries",
        int(p_part)
    );
    if (parts[p_part].is_valid()) {
        return parts[p_part];
    }
    const Callable &factory = part_factories()[p_part];
    if (!factory.is_valid()) {
        NETW_TRACE(
            sys::ENTITY,
            "no factory is registered for entity part %d",
            int(p_part)
        );
        return Ref<RefCounted>();
    }
    const Variant made = factory.call(p_wrapper);
    Ref<RefCounted> minted = made;
    NETW_WARN_COND(
        minted.is_null(),
        sys::ENTITY,
        "the factory for entity part %d answered nothing",
        int(p_part)
    );
    if (minted.is_null()) {
        return minted;
    }
    NETW_TRACE(sys::ENTITY, "minted entity part %d", int(p_part));
    parts[p_part] = minted;
    return minted;
}

void NetwEntityRecord::set_part_factory(
    int64_t p_part,
    const Callable &p_factory
) {
    NETW_ERR_COND(
        p_part < 0 || p_part >= PART_MAX,
        sys::ENTITY,
        "part %d is not a facet a record carries",
        int(p_part)
    );
    part_factories()[p_part] = p_factory;
}

bool NetwEntityRecord::has_part_factory(int64_t p_part) {
    if (p_part < 0 || p_part >= PART_MAX) {
        return false;
    }
    return part_factories()[p_part].is_valid();
}

Callable NetwEntityRecord::part_factory(int64_t p_part) {
    if (p_part < 0 || p_part >= PART_MAX) {
        return Callable();
    }
    return part_factories()[p_part];
}

void NetwEntityRecord::clear_part_factories() {
    for (int at = 0; at < PART_MAX; at++) {
        part_factories()[at] = Callable();
    }
    scene_declaration() = Callable();
}

bool NetwEntityRecord::advance(int64_t p_stage) {
    if (!NetwEntityStage::edge_is_legal(stage, p_stage)) {
        return false;
    }
    stage = p_stage;
    return true;
}

bool NetwEntityRecord::transition(int64_t p_stage, Object *p_owner) {
    const int64_t from = stage;
    if (advance(p_stage)) {
        return true;
    }
    Node *owner = Object::cast_to<Node>(p_owner);
    NETW_ERROR(
        sys::ENTITY,
        "illegal stage transition %d -> %d on '%s'",
        int(from),
        int(p_stage),
        owner ? godot::String(owner->get_name()) : godot::String("<no owner>")
    );
    return false;
}

void NetwEntityRecord::deactivate(Object *p_owner) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr) {
        return;
    }
    owner->set_process_mode(Node::PROCESS_MODE_DISABLED);
    owner->set(StringName("visible"), false);
    NetwSynchronizers::sync_only_server(owner);
}

StringName NetwEntityRecord::template_meta() {
    return StringName("_networked_spawn_template");
}

bool NetwEntityRecord::declares_template(Object *p_owner) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr) {
        return false;
    }
    return owner->get_owner() != nullptr || owner->has_meta(template_meta());
}

bool NetwEntityRecord::mark_template(Object *p_owner) {
    if (stage == int64_t(EntityStage::TEMPLATE)) {
        return true;
    }
    if (!transition(int64_t(EntityStage::TEMPLATE), p_owner)) {
        return false;
    }
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner != nullptr && owner->is_inside_tree()) {
        deactivate(owner);
    }
    return true;
}

bool NetwEntityRecord::control_recurses(
    bool p_is_authority,
    bool p_inside_tree,
    bool p_node_ready
) {
    return p_is_authority || !p_inside_tree || p_node_ready;
}

void NetwEntityRecord::apply_control(
    Object *p_wrapper,
    Object *p_owner,
    bool p_is_authority
) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr) {
        return;
    }
    const int64_t previous = owner->get_multiplayer_authority();
    const int64_t peer = control->get_controller();
    const int64_t authority = peer != 0 ? peer : 1;
    owner->set_multiplayer_authority(
        int(authority),
        control_recurses(
            p_is_authority,
            owner->is_inside_tree(),
            netw::gd::node_ready(owner)
        )
    );
    const int64_t was = previous == 1 ? 0 : previous;
    if (was != peer && p_wrapper != nullptr) {
        NETW_TRACE(
            sys::ENTITY,
            "control moved from %d to %d",
            int(was),
            int(peer)
        );
        p_wrapper->emit_signal(StringName("control_changed"), was, peer);
    }
}

void NetwEntityRecord::set_scene_declaration_reader(const Callable &p_reader) {
    scene_declaration() = p_reader;
}

Callable NetwEntityRecord::scene_declaration_reader() {
    return scene_declaration();
}

namespace {

Dictionary declared_scene_of(Object *p_owner) {
    const Callable &reader = scene_declaration();
    if (p_owner == nullptr || !reader.is_valid()) {
        return Dictionary();
    }
    return reader.call(p_owner);
}

} // namespace

StringName NetwEntityRecord::scene_label_of(Object *p_owner) {
    if (scene_label != StringName()) {
        return scene_label;
    }
    const Dictionary declared = declared_scene_of(p_owner);
    return declared.has("label") ? StringName(declared["label"]) : StringName();
}

int64_t NetwEntityRecord::scene_isolation_of(Object *p_owner) {
    if (scene_isolation >= 0) {
        return scene_isolation;
    }
    const Dictionary declared = declared_scene_of(p_owner);
    return declared.has("isolation") ? int64_t(declared["isolation"]) : 0;
}

void NetwEntityRecord::hydrate_identity(Object *p_owner) {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr) {
        return;
    }
    const String named = String(owner->get_name());
    if (entity_id == StringName()) {
        entity_id = EntityIdentity::parse_entity(named);
    }
    if (peer_id == 0) {
        peer_id = EntityIdentity::parse_peer(named);
    }
}

bool NetwEntityRecord::set_controller(Object *p_wrapper, int64_t p_peer) {
    const int64_t previous = control->get_controller();
    if (!control->set_controller(p_peer)) {
        return false;
    }
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(
            StringName("control_changed"),
            previous,
            p_peer
        );
    }
    return true;
}

int64_t NetwEntityRecord::admit_control_request(
    Object *p_wrapper,
    int64_t p_requester
) {
    if (!control->admits_request()) {
        NETW_WARN(
            sys::ENTITY,
            "rejecting a control request from peer %d, transfer is fixed",
            int(p_requester)
        );
        return 0;
    }
    Ref<NetwControlRequest> request;
    request.instantiate();
    request->requester = p_requester;
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(
            StringName("control_requested"),
            p_requester,
            request
        );
    }
    if (request->denied) {
        NETW_WARN(
            sys::ENTITY,
            "the control request from peer %d was denied",
            int(p_requester)
        );
        return 0;
    }
    return p_requester;
}

bool NetwEntityRecord::begin_despawn(
    Object *p_wrapper,
    Object *p_owner,
    const Ref<NetwDespawnOpts> &p_opts
) {
    if (!NetwEntityStage::can_begin_despawn(stage)) {
        return false;
    }
    Ref<NetwDespawnOpts> opts = p_opts;
    if (opts.is_null()) {
        opts.instantiate();
    }
    despawning_opts = opts;
    transition(int64_t(EntityStage::DESPAWNING), p_owner);
    NETW_TRACE(sys::ENTITY, "despawning for '%s'", opts->get_reason());
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(StringName("despawning"), opts->get_reason());
    }
    despawning_opts = Ref<NetwDespawnOpts>();
    return true;
}

bool NetwEntityRecord::finish_teardown(Object *p_wrapper) {
    if (!advance(int64_t(EntityStage::FREED))) {
        return false;
    }
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(StringName("despawned"));
    }
    return true;
}

bool NetwEntityRecord::classify_activation(Object *p_owner) {
    if (stage == int64_t(EntityStage::TEMPLATE)) {
        deactivate(p_owner);
        return false;
    }
    if (stage != int64_t(EntityStage::UNBOUND)) {
        return true;
    }
    if (entity_id != StringName()) {
        return true;
    }
    if (declares_template(p_owner)) {
        transition(int64_t(EntityStage::TEMPLATE), p_owner);
        deactivate(p_owner);
    }
    return false;
}

void NetwEntityRecord::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_handle"),
        &NetwEntityRecord::get_handle
    );
    ClassDB::bind_method(
        D_METHOD("adopt_handle", "handle"),
        &NetwEntityRecord::adopt_handle
    );
    ClassDB::bind_method(
        D_METHOD("get_entity_id"),
        &NetwEntityRecord::get_entity_id
    );
    ClassDB::bind_method(
        D_METHOD("set_entity_id", "entity_id"),
        &NetwEntityRecord::set_entity_id
    );
    ClassDB::bind_method(D_METHOD("get_peer_id"), &NetwEntityRecord::get_peer_id);
    ClassDB::bind_method(
        D_METHOD("set_peer_id", "peer_id"),
        &NetwEntityRecord::set_peer_id
    );
    ClassDB::bind_method(D_METHOD("get_route"), &NetwEntityRecord::get_route);
    ClassDB::bind_method(
        D_METHOD("set_route", "route"),
        &NetwEntityRecord::set_route
    );
    ClassDB::bind_method(D_METHOD("get_stage"), &NetwEntityRecord::get_stage);
    ClassDB::bind_method(
        D_METHOD("advance", "stage"),
        &NetwEntityRecord::advance
    );
    ClassDB::bind_method(D_METHOD("get_control"), &NetwEntityRecord::get_control);

    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "entity_id"),
        "set_entity_id",
        "get_entity_id"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "peer_id"),
        "set_peer_id",
        "get_peer_id"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "route"), "set_route", "get_route");
    ClassDB::bind_method(
        D_METHOD("get_declares_scene"),
        &NetwEntityRecord::get_declares_scene
    );
    ClassDB::bind_method(
        D_METHOD("set_declares_scene", "declares"),
        &NetwEntityRecord::set_declares_scene
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "declares_scene"),
        "set_declares_scene",
        "get_declares_scene"
    );

    ClassDB::bind_method(
        D_METHOD("part", "part", "wrapper"),
        &NetwEntityRecord::part
    );
    ClassDB::bind_static_method(
        "NetwEntityRecord",
        D_METHOD("set_part_factory", "part", "factory"),
        &NetwEntityRecord::set_part_factory
    );
    ClassDB::bind_static_method(
        "NetwEntityRecord",
        D_METHOD("has_part_factory", "part"),
        &NetwEntityRecord::has_part_factory
    );

    ClassDB::bind_method(
        D_METHOD("get_reparenting"),
        &NetwEntityRecord::get_reparenting
    );
    ClassDB::bind_method(
        D_METHOD("set_reparenting", "opts"),
        &NetwEntityRecord::set_reparenting
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "reparenting",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwReparentOpts"
        ),
        "set_reparenting",
        "get_reparenting"
    );

    ClassDB::bind_method(
        D_METHOD("transition", "stage", "owner"),
        &NetwEntityRecord::transition
    );
    ClassDB::bind_method(
        D_METHOD("deactivate", "owner"),
        &NetwEntityRecord::deactivate
    );
    ClassDB::bind_method(
        D_METHOD("mark_template", "owner"),
        &NetwEntityRecord::mark_template
    );
    ClassDB::bind_method(
        D_METHOD("apply_control", "wrapper", "owner", "is_authority"),
        &NetwEntityRecord::apply_control
    );
    ClassDB::bind_static_method(
        "NetwEntityRecord",
        D_METHOD(
            "control_recurses",
            "is_authority",
            "inside_tree",
            "node_ready"
        ),
        &NetwEntityRecord::control_recurses
    );
    ClassDB::bind_method(
        D_METHOD("scene_label_of", "owner"),
        &NetwEntityRecord::scene_label_of
    );
    ClassDB::bind_method(
        D_METHOD("scene_isolation_of", "owner"),
        &NetwEntityRecord::scene_isolation_of
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_label", "label"),
        &NetwEntityRecord::set_scene_label
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_isolation", "isolation"),
        &NetwEntityRecord::set_scene_isolation
    );
    ClassDB::bind_static_method(
        "NetwEntityRecord",
        D_METHOD("set_scene_declaration_reader", "reader"),
        &NetwEntityRecord::set_scene_declaration_reader
    );
    ClassDB::bind_method(
        D_METHOD("hydrate_identity", "owner"),
        &NetwEntityRecord::hydrate_identity
    );
    ClassDB::bind_method(
        D_METHOD("set_controller", "wrapper", "peer"),
        &NetwEntityRecord::set_controller
    );
    ClassDB::bind_method(
        D_METHOD("admit_control_request", "wrapper", "requester"),
        &NetwEntityRecord::admit_control_request
    );
    ClassDB::bind_method(
        D_METHOD("begin_despawn", "wrapper", "owner", "opts"),
        &NetwEntityRecord::begin_despawn
    );
    ClassDB::bind_method(
        D_METHOD("get_active_despawn_opts"),
        &NetwEntityRecord::get_active_despawn_opts
    );
    ClassDB::bind_method(
        D_METHOD("finish_teardown", "wrapper"),
        &NetwEntityRecord::finish_teardown
    );
    ClassDB::bind_method(
        D_METHOD("classify_activation", "owner"),
        &NetwEntityRecord::classify_activation
    );
    ClassDB::bind_static_method(
        "NetwEntityRecord",
        D_METHOD("declares_template", "owner"),
        &NetwEntityRecord::declares_template
    );
    ClassDB::bind_static_method(
        "NetwEntityRecord",
        D_METHOD("template_meta"),
        &NetwEntityRecord::template_meta
    );

    BIND_ENUM_CONSTANT(PART_SCENE);
    BIND_ENUM_CONSTANT(PART_INTEREST);
    BIND_ENUM_CONSTANT(PART_PREDICTION);
    BIND_ENUM_CONSTANT(PART_DISPLAY);
    BIND_ENUM_CONSTANT(PART_COMPONENTS);
    BIND_ENUM_CONSTANT(PART_MAX);
}

} // namespace netw
