#include "netw/api/entity_record.hpp"

#include "godot/node.hpp"
#include "netw/entity/identity.hpp"
#include "netw/entity/ids.hpp"
#include "netw/entity/stage.hpp"
#include "netw/log.hpp"
#include "netw/scene_decl.hpp"
#include "netw/script/model.hpp"
#include "netw/synchronizers.hpp"

using namespace godot;

namespace netw {

namespace {

Callable *part_factories() {
    static Callable table[NetwEntityRecord::PART_MAX];
    return table;
}

} // namespace

NetwEntityRecord::NetwEntityRecord() {
    handle = entity::mint();
}

NetwEntityRecord::~NetwEntityRecord() {
    entity::release(handle);
}

bool NetwEntityRecord::adopt_handle(const RID &p_handle) {
    if (p_handle == handle) {
        return true;
    }
    if (!entity::retain(p_handle)) {
        return false;
    }
    entity::release(handle);
    handle = p_handle;
    return true;
}

Variant NetwEntityRecord::part(int64_t p_part, Object *p_wrapper) {
    NETW_ERR_COND_V(
        p_part < 0 || p_part >= PART_MAX,
        Variant(),
        sys::ENTITY,
        "part %d is not a facet a record carries",
        int(p_part)
    );
    if (Ref<RefCounted>(parts[p_part]).is_valid()) {
        return parts[p_part];
    }
    const Callable &factory = part_factories()[p_part];
    if (!factory.is_valid()) {
        NETW_TRACE(
            sys::ENTITY,
            "no factory is registered for entity part %d",
            int(p_part)
        );
        return Variant();
    }
    const Variant made = factory.call(p_wrapper);
    const Ref<RefCounted> minted = made;
    NETW_WARN_COND(
        minted.is_null(),
        sys::ENTITY,
        "the factory for entity part %d answered nothing",
        int(p_part)
    );
    if (minted.is_null()) {
        return Variant();
    }
    NETW_TRACE(sys::ENTITY, "minted entity part %d", int(p_part));
    parts[p_part] = made;
    return made;
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
}

bool NetwEntityRecord::advance(int64_t p_stage) {
    if (!entity::stage_edge_is_legal(stage, p_stage)) {
        return false;
    }
    stage = p_stage;
    return true;
}

bool NetwEntityRecord::transition(int64_t p_stage, Node *p_owner) {
    const int64_t from = stage;
    if (advance(p_stage)) {
        return true;
    }
    Node *owner = p_owner;
    NETW_ERROR(
        sys::ENTITY,
        "illegal stage transition %d -> %d on '%s'",
        int(from),
        int(p_stage),
        owner ? godot::String(owner->get_name()) : godot::String("<no owner>")
    );
    return false;
}

void NetwEntityRecord::deactivate(Node *p_owner) {
    Node *owner = p_owner;
    if (owner == nullptr) {
        return;
    }
    owner->set_process_mode(Node::PROCESS_MODE_DISABLED);
    owner->set(StringName("visible"), false);
    synchronizers::sync_only_server(owner);
}

StringName NetwEntityRecord::template_meta() {
    return StringName("_networked_spawn_template");
}

bool NetwEntityRecord::declares_template(Node *p_owner) {
    Node *owner = p_owner;
    if (owner == nullptr) {
        return false;
    }
    return owner->get_owner() != nullptr || owner->has_meta(template_meta());
}

bool NetwEntityRecord::mark_template(Node *p_owner) {
    if (stage == int64_t(entity::Stage::TEMPLATE)) {
        return true;
    }
    if (!transition(int64_t(entity::Stage::TEMPLATE), p_owner)) {
        return false;
    }
    Node *owner = p_owner;
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
    Node *p_owner,
    bool p_is_authority
) {
    Node *owner = p_owner;
    if (owner == nullptr) {
        return;
    }
    const int64_t previous = owner->get_multiplayer_authority();
    const int64_t peer = control.get_controller();
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

namespace {

SceneDecl declared_scene_of(Node *p_owner) {
    if (p_owner == nullptr) {
        return SceneDecl();
    }
    Node *owner = p_owner;
    return netw::script::model::get_scene_decl(
        Ref<Script>(Object::cast_to<Script>(owner->get_script()))
    );
}

} // namespace

StringName NetwEntityRecord::scene_label_of(Node *p_owner) {
    if (scene_label != StringName()) {
        return scene_label;
    }
    return declared_scene_of(p_owner).label;
}

int64_t NetwEntityRecord::scene_isolation_of(Node *p_owner) {
    if (scene_isolation >= 0) {
        return scene_isolation;
    }
    return declared_scene_of(p_owner).isolation;
}

void NetwEntityRecord::hydrate_identity(Node *p_owner) {
    Node *owner = p_owner;
    if (owner == nullptr) {
        return;
    }
    const String named = String(owner->get_name());
    if (entity_id == StringName()) {
        entity_id = entity::Identity::parse_entity(named);
    }
    if (peer_id == 0) {
        peer_id = entity::Identity::parse_peer(named);
    }
    const SceneDecl decl = declared_scene_of(owner);
    if (!decl.declared) {
        return;
    }
    declares_scene = true;
    if (scene_label == StringName()) {
        scene_label = decl.label;
    }
    if (scene_isolation < 0) {
        scene_isolation = decl.isolation;
    }
}

bool NetwEntityRecord::set_controller(Object *p_wrapper, int64_t p_peer) {
    const int64_t previous = control.get_controller();
    if (!control.set_controller(p_peer)) {
        return false;
    }
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(StringName("control_changed"), previous, p_peer);
    }
    return true;
}

int64_t NetwEntityRecord::admit_control_request(
    Object *p_wrapper,
    int64_t p_requester
) {
    if (!control.admits_request()) {
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
    Node *p_owner,
    const Ref<NetwDespawnOpts> &p_opts
) {
    if (!entity::stage_can_begin_despawn(stage)) {
        return false;
    }
    Ref<NetwDespawnOpts> opts = p_opts;
    if (opts.is_null()) {
        opts.instantiate();
    }
    despawning_opts = opts;
    transition(int64_t(entity::Stage::DESPAWNING), p_owner);
    NETW_TRACE(sys::ENTITY, "despawning for '%s'", opts->get_reason());
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(StringName("despawning"), opts->get_reason());
    }
    despawning_opts = Ref<NetwDespawnOpts>();
    return true;
}

bool NetwEntityRecord::finish_teardown(Object *p_wrapper) {
    if (!advance(int64_t(entity::Stage::FREED))) {
        return false;
    }
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(StringName("despawned"));
    }
    return true;
}

bool NetwEntityRecord::complete_departure(Object *p_wrapper) {
    if (stage == int64_t(entity::Stage::TEMPLATE)
        || stage == int64_t(entity::Stage::FREED)) {
        return false;
    }
    if (entity::stage_can_begin_despawn(stage)) {
        advance(int64_t(entity::Stage::DESPAWNING));
    }
    return finish_teardown(p_wrapper);
}

bool NetwEntityRecord::classify_activation(Node *p_owner) {
    if (stage == int64_t(entity::Stage::TEMPLATE)) {
        deactivate(p_owner);
        return false;
    }
    if (stage != int64_t(entity::Stage::UNBOUND)) {
        return true;
    }
    if (entity_id != StringName()) {
        return true;
    }
    if (declares_template(p_owner)) {
        transition(int64_t(entity::Stage::TEMPLATE), p_owner);
        deactivate(p_owner);
    }
    return false;
}

} // namespace netw
