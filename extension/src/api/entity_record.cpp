#include "netw/api/entity_record.hpp"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/entity/ids.hpp"
#include "netw/entity/stage.hpp"
#include "netw/log.hpp"
#include "netw/scene_decl.hpp"
#include "netw/script/model.hpp"
#include "netw/synchronizers.hpp"
#include "netw/tree_hook.hpp"

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
    if (interest_facet != nullptr) {
        memdelete(interest_facet);
    }
    if (display_decl != nullptr) {
        memdelete(display_decl);
    }
    entity::release(handle);
}

interest::Facet &NetwEntityRecord::get_interest_facet() {
    if (interest_facet == nullptr) {
        interest_facet = memnew(interest::Facet);
    }
    return *interest_facet;
}

display::Decl &NetwEntityRecord::display_declaration() {
    if (display_decl == nullptr) {
        display_decl = memnew(display::Decl);
    }
    return *display_decl;
}

Variant NetwEntityRecord::minted_part(int64_t p_part) const {
    for (const MintedPart &held : parts) {
        if (held.part == p_part) {
            return held.value;
        }
    }
    return Variant();
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
        "Entity part %d does not exist.",
        int(p_part)
    );
    const Variant held = minted_part(p_part);
    if (Ref<RefCounted>(held).is_valid()) {
        return held;
    }
    const Callable &factory = part_factories()[p_part];
    if (!factory.is_valid()) {
        NETW_TRACE(
            sys::ENTITY,
            "No factory is registered for entity part %d.",
            int(p_part)
        );
        return Variant();
    }
    const Variant made = factory.call(p_wrapper);
    const Ref<RefCounted> minted = made;
    NETW_WARN_COND(
        minted.is_null(),
        sys::ENTITY,
        "The factory for entity part %d returned nothing.",
        int(p_part)
    );
    if (minted.is_null()) {
        return Variant();
    }
    NETW_TRACE(sys::ENTITY, "Made entity part %d.", int(p_part));
    for (MintedPart &slot : parts) {
        if (slot.part == p_part) {
            slot.value = made;
            return made;
        }
    }
    MintedPart minted_now;
    minted_now.part = p_part;
    minted_now.value = made;
    parts.push_back(minted_now);
    return made;
}

void NetwEntityRecord::set_part_factory(
    int64_t p_part,
    const Callable &p_factory
) {
    NETW_ERR_COND(
        p_part < 0 || p_part >= PART_MAX,
        sys::ENTITY,
        "Entity part %d does not exist.",
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
        "Entity '%s' cannot move from stage %d to stage %d.",
        owner ? godot::String(owner->get_name()) : godot::String("<no owner>"),
        int(from),
        int(p_stage)
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


StringName NetwEntityRecord::entity_meta() {
    return StringName("netw_entity");
}

NetwEntityRecord::Projection NetwEntityRecord::projection_to(
    int64_t p_follower
) const {
    Projection made;
    made.follower = p_follower;
    made.prior_follower
        = projected_follower != 0 ? projected_follower : SERVER_PEER;
    return made;
}

void NetwEntityRecord::write_authority(
    Node *p_node,
    int64_t p_to,
    int64_t p_prior
) {
    const int64_t now = p_node->get_multiplayer_authority();
    if (now == p_to) {
        return;
    }
    if (now != p_prior && !native_write_warned) {
        native_write_warned = true;
        const NodePath named = p_node->is_inside_tree()
            ? p_node->get_path()
            : NodePath(String(p_node->get_name()));
        NETW_WARN(
            sys::ENTITY,
            "%s had its multiplayer authority set to %d outside its entity, "
            "and the entity sets it back to %d. Choose who holds it with "
            "NetwEntity.authority.",
            String(named).utf8().get_data(),
            int(now),
            int(p_to)
        );
    }
    p_node->set_multiplayer_authority(int(p_to), false);
}

void NetwEntityRecord::project_node(
    Node *p_owner,
    Node *p_node,
    const Projection &p_projection,
    uint64_t p_watch
) {
    if (p_node != p_owner && tree::is_entity_root(p_node)) {
        return;
    }
    write_authority(p_node, p_projection.follower, p_projection.prior_follower);
    const bool watched = p_projection.follower != SERVER_PEER;
    if (watched && p_watch != 0) {
        tree::watch(p_node, p_watch);
    }
    const int children = p_node->get_child_count(true);
    for (int at = 0; at < children; ++at) {
        project_node(
            p_owner,
            p_node->get_child(at, true),
            p_projection,
            p_watch
        );
    }
}

void NetwEntityRecord::project_entered(
    Node *p_owner,
    Node *p_node,
    uint64_t p_watch
) {
    if (p_owner == nullptr || p_node == nullptr || projected_follower == 0
        || !p_owner->is_ancestor_of(p_node)) {
        return;
    }
    Projection entered;
    entered.follower = projected_follower;
    project_node(p_owner, p_node, entered, p_watch);
}

void NetwEntityRecord::apply_control(
    Object *p_wrapper,
    Node *p_owner,
    uint64_t p_watch
) {
    Node *owner = p_owner;
    if (owner == nullptr) {
        return;
    }
    const int64_t steerer
        = entity::Control::public_peer(control.get_controller());
    const int64_t follower
        = control.authority_follows_controller() ? steerer : SERVER_PEER;
    project_node(owner, owner, projection_to(follower), p_watch);
    projected_follower = follower;
    const int64_t was
        = projected_steerer != 0 ? projected_steerer : SERVER_PEER;
    projected_steerer = steerer;
    if (was != steerer && p_wrapper != nullptr) {
        NETW_TRACE(
            sys::ENTITY,
            "Control moved from peer %d to peer %d.",
            int(was),
            int(steerer)
        );
        p_wrapper->emit_signal(StringName("control_changed"), was, steerer);
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
    const SceneDecl decl = declared_scene_of(p_owner);
    return decl.declared && decl.label.is_empty() && p_owner != nullptr
        ? p_owner->get_name()
        : decl.label;
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
    const SceneDecl decl = declared_scene_of(owner);
    if (!decl.declared) {
        return;
    }
    declares_scene = true;
    if (scene_label == StringName()) {
        scene_label = decl.label.is_empty() ? owner->get_name() : decl.label;
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
    int64_t p_requester,
    int64_t p_hold
) {
    if (!control.admits_request()) {
        NETW_WARN(
            sys::ENTITY,
            "Peer %d asked for control, but NetwEntity.transfer is "
            "TRANSFER_FIXED, so it was refused. Set it to "
            "TRANSFER_REQUESTABLE to allow requests.",
            int(p_requester)
        );
        return 0;
    }
    Ref<NetwControlRequest> request;
    request.instantiate();
    request->requester = p_requester;
    request->hold = p_hold;
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(
            StringName("control_requested"),
            p_requester,
            request
        );
    }
    if (request->denied) {
        NETW_DEBUG(
            sys::ENTITY,
            "The control_requested handler denied peer %d.",
            int(p_requester)
        );
        return 0;
    }
    return p_requester;
}

bool NetwEntityRecord::publish_stage(Object *p_wrapper, int64_t p_from) {
    if (p_from == stage || p_wrapper == nullptr) {
        return true;
    }
    NetwEntity *wrapper = Object::cast_to<NetwEntity>(p_wrapper);
    if (wrapper != nullptr && wrapper->get_record() == this) {
        return wrapper->publish_stage(p_from, stage);
    }
    return true;
}

bool NetwEntityRecord::begin_despawn(
    Object *p_wrapper,
    Node *p_owner,
    const Ref<NetwDespawnOpts> &p_opts
) {
    const Ref<RefCounted> keep(Object::cast_to<RefCounted>(p_wrapper));
    if (!entity::stage_can_begin_despawn(stage)) {
        return false;
    }
    Ref<NetwDespawnOpts> opts = p_opts;
    if (opts.is_null()) {
        opts.instantiate();
    }
    despawning_opts = opts;
    const int64_t from = stage;
    transition(int64_t(entity::Stage::DESPAWNING), p_owner);
    if (!publish_stage(p_wrapper, from)) {
        despawning_opts = Ref<NetwDespawnOpts>();
        return false;
    }
    NETW_TRACE(sys::ENTITY, "Despawning because of '%s'.", opts->get_reason());
    if (p_wrapper != nullptr) {
        NetwEntity *wrapper = Object::cast_to<NetwEntity>(p_wrapper);
        const ObjectID owner = gd::instance_id(
            wrapper != nullptr ? wrapper->get_owner() : p_owner
        );
        const Ref<MultiplayerAPI> session = wrapper != nullptr
            ? wrapper->get_multiplayer()
            : Ref<MultiplayerAPI>();
        const RID captured_handle = handle;
        tree::despawning(p_wrapper, opts->get_reason());
        if (wrapper == nullptr
            || (wrapper->get_record() == this && handle == captured_handle
                && wrapper->get_multiplayer() == session
                && gd::instance_id(wrapper->get_owner()) == owner
                && stage == int64_t(entity::Stage::DESPAWNING))) {
            p_wrapper->emit_signal(
                StringName("despawning"),
                opts->get_reason()
            );
        }
    }
    despawning_opts = Ref<NetwDespawnOpts>();
    return true;
}

bool NetwEntityRecord::finish_teardown(Object *p_wrapper) {
    const Ref<RefCounted> keep(Object::cast_to<RefCounted>(p_wrapper));
    const int64_t from = stage;
    if (!advance(int64_t(entity::Stage::FREED))) {
        return false;
    }
    if (!publish_stage(p_wrapper, from)) {
        return true;
    }
    if (p_wrapper != nullptr) {
        p_wrapper->emit_signal(StringName("despawned"));
    }
    return true;
}

bool NetwEntityRecord::complete_departure(Object *p_wrapper) {
    const Ref<RefCounted> keep(Object::cast_to<RefCounted>(p_wrapper));
    if (stage == int64_t(entity::Stage::FREED)) {
        return false;
    }
    if (entity::stage_can_begin_despawn(stage)) {
        const int64_t from = stage;
        advance(int64_t(entity::Stage::DESPAWNING));
        if (!publish_stage(p_wrapper, from)) {
            return true;
        }
    }
    return finish_teardown(p_wrapper);
}

void NetwEntityRecord::readmit() {
    stage = int64_t(entity::Stage::UNBOUND);
    route = 0;
    entity::release(handle);
    handle = entity::mint();
    despawning_opts = Ref<NetwDespawnOpts>();
    projected_follower = 0;
}

} // namespace netw
