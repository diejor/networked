#include "netw/entity.hpp"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/entity_control.hpp"
#include "netw/entity_identity.hpp"
#include "netw/entity_stage.hpp"
#include "netw/log.hpp"
#include "netw/netw_multiplayer.hpp"
#include "netw/repl/set_model.hpp"
#include "netw/synchronizers.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

constexpr const char *SIG_SPAWNING = "spawning";
constexpr const char *SIG_SPAWNED = "spawned";
constexpr const char *SIG_DESPAWNING = "despawning";
constexpr const char *SIG_DESPAWNED = "despawned";
constexpr const char *SIG_INTEREST_ENTER = "interest_enter";
constexpr const char *SIG_INTEREST_EXIT = "interest_exit";
constexpr const char *SIG_OBSERVER_ENTERED = "observer_entered";
constexpr const char *SIG_OBSERVER_LEFT = "observer_left";
constexpr const char *SIG_CONTROL_CHANGED = "control_changed";
constexpr const char *SIG_CONTROL_REQUESTED = "control_requested";
constexpr const char *SIG_REPARENTED = "reparented";
constexpr const char *SIG_VIEW_ACTIVATED = "view_activated";

Callable &session_lookup() {
    static Callable lookup;
    return lookup;
}

Object *session_on_branch(Object *p_node) {
    Node *node = Object::cast_to<Node>(p_node);
    if (node == nullptr || !node->is_inside_tree()) {
        return nullptr;
    }
    const Ref<MultiplayerAPI> api = node->get_multiplayer();
    if (api.is_null()) {
        return nullptr;
    }
    if (Object::cast_to<NetwMultiplayerCore>(api->get(StringName("_native_core"))
        )
        == nullptr) {
        return nullptr;
    }
    return api.ptr();
}

Ref<NetwEntity> as_entity(const Ref<RefCounted> &p_wrapper) {
    return Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_wrapper.ptr()));
}

} // namespace

NetwEntity::NetwEntity() : record(memnew(NetwEntityRecord)) { }

NetwEntity::~NetwEntity() { }

StringName NetwEntity::meta_key() {
    return NetwMultiplayerCore::wrapper_meta();
}

StringName NetwEntity::template_meta() {
    return NetwEntityRecord::template_meta();
}

void NetwEntity::set_session_lookup(const Callable &p_lookup) {
    session_lookup() = p_lookup;
}

Ref<NetwMultiplayerCore> NetwEntity::session_plane_for(Object *p_node) {
    return Ref<NetwMultiplayerCore>(session_core_for(p_node));
}

NetwMultiplayerCore *NetwEntity::session_core_for(Object *p_node) {
    Object *api = session_on_branch(p_node);
    if (api != nullptr) {
        return Object::cast_to<NetwMultiplayerCore>(
            api->get(StringName("_native_core"))
        );
    }
    if (!session_lookup().is_valid()) {
        return nullptr;
    }
    return Object::cast_to<NetwMultiplayerCore>(
        session_lookup().call(p_node)
    );
}

Ref<NetwEntity> NetwEntity::of(Object *p_node) {
    return as_entity(NetwMultiplayerCore::wrapper_at(p_node));
}

Ref<NetwEntity> NetwEntity::ensure(Object *p_root) {
    return as_entity(NetwMultiplayerCore::wrapper_ensure(p_root));
}

Ref<NetwEntity> NetwEntity::resolve(Object *p_node) {
    return as_entity(NetwMultiplayerCore::wrapper_resolve(p_node));
}

Ref<NetwEntity> NetwEntity::from_rid(const RID &p_entity, Object *p_api) {
    NetwMultiplayerCore *core = p_api == nullptr
        ? nullptr
        : Object::cast_to<NetwMultiplayerCore>(
              p_api->get(StringName("_native_core"))
          );
    if (core == nullptr) {
        return Ref<NetwEntity>();
    }
    return as_entity(core->wrapper_of(p_entity));
}

Ref<NetwEntity> NetwEntity::by_route(int64_t p_route, Object *p_api) {
    NetwMultiplayerCore *core = p_api == nullptr
        ? nullptr
        : Object::cast_to<NetwMultiplayerCore>(
              p_api->get(StringName("_native_core"))
          );
    if (core == nullptr) {
        return Ref<NetwEntity>();
    }
    return as_entity(core->wrapper_for_route(p_route));
}

StringName NetwEntity::parse_entity(const String &p_node_name) {
    return EntityIdentity::parse_entity(p_node_name);
}

int64_t NetwEntity::parse_peer(const String &p_node_name) {
    return EntityIdentity::parse_peer(p_node_name);
}

String NetwEntity::name_for(Object *p_join) {
    if (p_join == nullptr) {
        return String();
    }
    return EntityIdentity::format(
        String(p_join->get(StringName("username"))),
        int64_t(p_join->get(StringName("peer_id")))
    );
}

Node *NetwEntity::find(Object *p_root, Object *p_join) {
    Node *root = Object::cast_to<Node>(p_root);
    if (root == nullptr || p_join == nullptr) {
        return nullptr;
    }
    return root->get_node_or_null(NodePath(name_for(p_join)));
}

Node *NetwEntity::bind(
    Object *p_node,
    const StringName &p_entity_id,
    int64_t p_peer_id
) {
    return Object::cast_to<Node>(
        NetwMultiplayerCore::wrapper_bind(p_node, p_entity_id, p_peer_id)
    );
}

Node *NetwEntity::instantiate_from(
    Object *p_template,
    const Callable &p_configure
) {
    Object *api = session_on_branch(p_template);
    if (api != nullptr) {
        NetwMultiplayerCore *core = Object::cast_to<NetwMultiplayerCore>(
            api->get(StringName("_native_core"))
        );
        if (core != nullptr) {
            return core->entity_instantiate_from(p_template, p_configure);
        }
    }
    return NetwMultiplayerCore::entity_instantiate_copy(
        p_template,
        p_configure
    );
}

void NetwEntity::attach_to(Object *p_root) {
    Node *root = Object::cast_to<Node>(p_root);
    if (root == nullptr) {
        return;
    }
    owner_id = gd::instance_id(root);
    root->set_meta(NetwMultiplayerCore::wrapper_meta(), this);
    const Callable entered(this, StringName("_handle_tree_entered"));
    if (!root->is_connected(StringName("tree_entered"), entered)) {
        root->connect(StringName("tree_entered"), entered);
    }
    const Callable exiting(this, StringName("_handle_tree_exiting"));
    if (!root->is_connected(StringName("tree_exiting"), exiting)) {
        root->connect(StringName("tree_exiting"), exiting);
    }
    if (root->is_inside_tree()) {
        entered.call_deferred();
    }
}

Node *NetwEntity::get_owner() const {
    return Object::cast_to<Node>(gd::object_of(owner_id));
}

void NetwEntity::set_owner(Object *p_owner) {
    owner_id = gd::instance_id(p_owner);
}

Ref<NetwEntityControl> NetwEntity::control() const {
    return record->get_control();
}

Object *NetwEntity::session() const {
    return gd::object_of(session_id);
}

NetwMultiplayerCore *NetwEntity::session_core() const {
    Object *api = session();
    if (api == nullptr) {
        return nullptr;
    }
    return Object::cast_to<NetwMultiplayerCore>(
        api->get(StringName("_native_core"))
    );
}

Variant NetwEntity::get_multiplayer() const {
    return gd::held(session());
}

void NetwEntity::stamp_multiplayer(Object *p_api) {
    if (p_api == nullptr) {
        return;
    }
    Object *current = session();
    if (current == p_api) {
        return;
    }
    NETW_ERR_COND(
        current != nullptr,
        sys::ENTITY,
        "multiplayer is immutable once stamped"
    );
    session_id = gd::instance_id(p_api);
}

StringName NetwEntity::get_entity_id() const {
    return record->get_entity_id();
}

void NetwEntity::set_entity_id(const StringName &p_entity_id) {
    record->set_entity_id(p_entity_id);
}

int64_t NetwEntity::get_peer_id() const {
    return record->get_peer_id();
}

void NetwEntity::set_peer_id(int64_t p_peer_id) {
    record->set_peer_id(p_peer_id);
}

int64_t NetwEntity::get_route() const {
    return record->get_route();
}

void NetwEntity::set_route(int64_t p_route) {
    record->set_route(p_route);
}

RID NetwEntity::get_rid_handle() const {
    return record->get_handle();
}

void NetwEntity::set_rid_handle(const RID &p_handle) {
    record->adopt_handle(p_handle);
}

int64_t NetwEntity::get_initial_controller() const {
    return control()->get_initial();
}

void NetwEntity::set_initial_controller(int64_t p_value) {
    control()->set_initial(p_value);
}

int64_t NetwEntity::get_transfer() const {
    return control()->get_transfer();
}

void NetwEntity::set_transfer(int64_t p_value) {
    control()->set_transfer(p_value);
}

int64_t NetwEntity::get_on_controller_disconnect() const {
    return control()->get_on_disconnect();
}

void NetwEntity::set_on_controller_disconnect(int64_t p_value) {
    control()->set_on_disconnect(p_value);
}

bool NetwEntity::get_declares_scene() const {
    return record->get_declares_scene();
}

void NetwEntity::set_declares_scene(bool p_value) {
    record->set_declares_scene(p_value);
}

StringName NetwEntity::get_scene_label() const {
    return record->scene_label_of(get_owner());
}

void NetwEntity::set_scene_label(const StringName &p_value) {
    record->set_scene_label(p_value);
}

int64_t NetwEntity::get_scene_isolation() const {
    return record->scene_isolation_of(get_owner());
}

void NetwEntity::set_scene_isolation(int64_t p_value) {
    record->set_scene_isolation(p_value);
}

int64_t NetwEntity::get_controller() const {
    return control()->resolve(get_peer_id());
}

void NetwEntity::set_controller(int64_t p_value) {
    set_controller_internal(p_value);
}

int64_t NetwEntity::get_control_kind() const {
    return get_controller() != 0 ? int64_t(CONTROL_PEER_CONTROLLED)
                                 : int64_t(CONTROL_SERVER_CONTROLLED);
}

bool NetwEntity::get_is_controlled_locally() const {
    return control()->controlled_by(local_peer(), get_peer_id());
}

Variant NetwEntity::get_controller_participant() const {
    Object *api = session();
    const int64_t steering = get_controller();
    if (api == nullptr || steering == 0) {
        return Variant();
    }
    return api->call(StringName("peer_get_participant"), steering);
}

int64_t NetwEntity::local_peer() const {
    Node *owner = get_owner();
    if (owner == nullptr) {
        return 0;
    }
    Object *api = session();
    Ref<MultiplayerAPI> resolved;
    if (api != nullptr) {
        resolved = Ref<MultiplayerAPI>(api);
    } else if (owner->is_inside_tree()) {
        resolved = owner->get_multiplayer();
    }
    if (resolved.is_null() || resolved->get_multiplayer_peer().is_null()) {
        return 0;
    }
    return resolved->get_unique_id();
}

bool NetwEntity::ensure_server_action(const StringName &p_action) {
    if (get_is_authority()) {
        return true;
    }
    NETW_ERROR(sys::ENTITY, "%s is server-only", String(p_action));
    return false;
}

Object *NetwEntity::replication_plane() const {
    Object *api = session();
    if (api == nullptr) {
        return nullptr;
    }
    return gd::live_object(api->get(StringName("_replication")));
}

void NetwEntity::set_controller_internal(int64_t p_value) {
    Node *owner = get_owner();
    if (p_value != 0 && owner != nullptr && get_is_authority()) {
        const Ref<MultiplayerAPI> api = owner->is_inside_tree()
            ? owner->get_multiplayer()
            : Ref<MultiplayerAPI>();
        const Callable dropped(this, StringName("_on_peer_disconnected"));
        if (api.is_valid()
            && !api->is_connected(StringName("peer_disconnected"), dropped)) {
            api->connect(StringName("peer_disconnected"), dropped);
        }
    }
    const int64_t was = control()->get_controller();
    if (!record->set_controller(this, p_value)) {
        return;
    }
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr) {
        return;
    }
    Dictionary detail;
    detail["from"] = was;
    detail["to"] = p_value;
    core->event_emit(
        EventPlane::CONTROL_CHANGED,
        get_route(),
        detail,
        get_entity_id(),
        p_value,
        OK,
        Dictionary()
    );
}

void NetwEntity::apply_control() {
    record->apply_control(this, get_owner(), get_is_authority());
}

void NetwEntity::_on_peer_disconnected(int64_t p_peer_id) {
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree() || !get_is_authority()) {
        return;
    }
    switch (control()->disconnect_verdict(p_peer_id, get_peer_id())) {
        case NetwEntityControl::DESPAWN_REPRESENTED: {
            NETW_INFO(
                sys::ENTITY,
                "peer %d disconnected, despawning represented entity '%s'",
                p_peer_id,
                owner->get_name()
            );
            despawn(NetwDespawnOpts::create(StringName("peer_disconnected")));
        } break;
        case NetwEntityControl::REVERT_TO_SERVER:
            apply_control_change(0);
            break;
        case NetwEntityControl::DESPAWN_CONTROLLER:
            despawn(
                NetwDespawnOpts::create(StringName("controller_disconnected"))
            );
            break;
        default:
            break;
    }
}

void NetwEntity::request_control() {
    NetwMultiplayerCore::entity_request_control(
        this,
        get_owner(),
        replication_plane()
    );
}

void NetwEntity::grant_control(int64_t p_peer_id) {
    if (!ensure_server_action(StringName("grant_control"))) {
        return;
    }
    apply_control_change(p_peer_id);
}

void NetwEntity::revoke_control() {
    if (!ensure_server_action(StringName("revoke_control"))) {
        return;
    }
    apply_control_change(0);
}

void NetwEntity::_handle_control_request(int64_t p_sender) {
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree() || !get_is_authority()) {
        NETW_WARN(
            sys::ENTITY,
            "ignoring a control request on a non-server peer for '%s'",
            owner != nullptr ? String(owner->get_name()) : String("<no owner>")
        );
        return;
    }
    int64_t requester = p_sender;
    const Ref<MultiplayerAPI> api = owner->get_multiplayer();
    if (requester == 0 && api.is_valid()
        && api->get_multiplayer_peer().is_valid()) {
        requester = api->get_unique_id();
    }
    const int64_t granted = record->admit_control_request(this, requester);
    NetwMultiplayerCore *core = session_core();
    if (core != nullptr) {
        Dictionary detail;
        detail["requester"] = requester;
        core->event_emit(
            EventPlane::CONTROL_REQUESTED,
            get_route(),
            detail,
            get_entity_id(),
            requester,
            granted == 0 ? ERR_UNAUTHORIZED : OK,
            Dictionary()
        );
    }
    if (granted == 0) {
        return;
    }
    apply_control_change(granted);
}

void NetwEntity::apply_control_change(int64_t p_peer) {
    set_controller_internal(p_peer);
    apply_control();
    NetwMultiplayerCore::entity_broadcast_control(
        this,
        replication_plane(),
        p_peer
    );
}

void NetwEntity::_handle_control_apply(int64_t p_peer) {
    set_controller_internal(p_peer);
    apply_control();
}

bool NetwEntity::get_is_authority() const {
    Object *api = session();
    if (api == nullptr) {
        return true;
    }
    return bool(api->call(StringName("is_server")));
}

Variant NetwEntity::get_participant() const {
    Object *api = session();
    const int64_t represented = get_peer_id();
    if (api == nullptr || represented == 0) {
        return Variant();
    }
    return api->call(StringName("peer_get_participant"), represented);
}

int64_t NetwEntity::get_ownership() const {
    return get_peer_id() != 0 ? int64_t(OWNERSHIP_PEER)
                              : int64_t(OWNERSHIP_SERVER);
}

bool NetwEntity::get_is_player() const {
    return get_peer_id() != 0;
}

Ref<NetwReparentOpts> NetwEntity::get_reparenting() const {
    return record->get_reparenting();
}

void NetwEntity::set_reparenting(const Ref<NetwReparentOpts> &p_opts) {
    record->set_reparenting(p_opts);
}

bool NetwEntity::get_is_template() const {
    return get_stage() == int64_t(EntityStage::TEMPLATE);
}

int64_t NetwEntity::get_stage() const {
    return record->get_stage();
}

Ref<NetwDespawnOpts> NetwEntity::get_active_despawn_opts() const {
    return record->get_active_despawn_opts();
}

void NetwEntity::note_stage(int64_t p_from) {
    NetwMultiplayerCore *core = session_core();
    if (core != nullptr) {
        core->entity_note_stage(record, p_from);
    }
}

void NetwEntity::transition(int64_t p_stage) {
    const int64_t from = get_stage();
    record->transition(p_stage, get_owner());
    note_stage(from);
}

void NetwEntity::mark_template() {
    const int64_t from = get_stage();
    record->mark_template(get_owner());
    note_stage(from);
}

void NetwEntity::arm(Object *p_api) {
    NETW_ERR_COND(
        get_stage() != int64_t(EntityStage::UNBOUND),
        sys::ENTITY,
        "arm requires an unbound record"
    );
    stamp_multiplayer(p_api);
    if (!control()->get_configured()) {
        set_controller_internal(control()->resolve(get_peer_id()));
    }
    apply_control();
    transition(int64_t(EntityStage::ARMED));
}

void NetwEntity::linger_then_free(const Ref<NetwDespawnOpts> &p_opts) {
    Object *api = session();
    Node *owner = get_owner();
    NetwMultiplayerCore *core = session_core();
    if (api == nullptr || core == nullptr) {
        record->deactivate(owner);
        if (owner != nullptr) {
            owner->queue_free();
        }
        return;
    }
    core->entity_linger(
        record,
        owner,
        int64_t(
            api->call(
                StringName("_linger_pumps"),
                p_opts->get_linger_seconds()
            )
        )
    );
}

void NetwEntity::_go_live_if_armed() {
    if (get_stage() != int64_t(EntityStage::ARMED)) {
        return;
    }
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    _handle_tree_entered();
    if (gd::node_ready(owner) && !ready_once_fired) {
        _on_owner_ready();
    }
}

void NetwEntity::_handle_tree_entered() {
    owner_exiting_tree = false;
    Node *owner = get_owner();
    if (session() == nullptr) {
        stamp_multiplayer(session_on_branch(owner));
    }
    NetwMultiplayerCore::entity_enter_tree(
        this,
        owner,
        record,
        session_core_for(owner),
        get_is_authority()
    );
}

void NetwEntity::_on_identity_hydrated() {
    Ref<RefCounted> map = record->part(NetwEntityRecord::PART_COMPONENTS, this);
    if (map.is_null()) {
        return;
    }
    map->call(StringName("hydrate"));
    map->call(StringName("reconcile"));
}

void NetwEntity::_settle_emit_reparented(const Ref<NetwReparentOpts> &p_opts) {
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr) {
        Array bound;
        bound.push_back(p_opts);
        Callable(this, StringName("_do_emit_reparented")).bindv(bound)
            .call_deferred();
        return;
    }
    core->entity_settle_reparented(this, get_owner(), p_opts);
}

void NetwEntity::_do_emit_reparented(const Ref<NetwReparentOpts> &p_opts) {
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return;
    }
    emit_signal(SIG_REPARENTED, p_opts);
}

void NetwEntity::_on_owner_ready() {
    if (ready_once_fired) {
        return;
    }
    ready_once_fired = true;
    emit_signal(SIG_SPAWNED);
    emit_signal(SIG_REPARENTED, get_reparenting());
}

void NetwEntity::_handle_tree_exiting() {
    owner_exiting_tree = true;
    const int64_t from = get_stage();
    record->finish_teardown(this);
    note_stage(from);
}

Node *NetwEntity::spawn_under(Object *p_parent, const StringName &p_id) {
    if (!ensure_server_action(StringName("spawn_under"))) {
        return nullptr;
    }
    NetwMultiplayerCore *core = session_core();
    if (core != nullptr) {
        return core->entity_spawn_under(get_owner(), p_parent, p_id);
    }
    return NetwMultiplayerCore::entity_spawn_copy_under(
        get_owner(),
        p_parent,
        p_id
    );
}

Node *NetwEntity::instantiate_player(Object *p_participant) {
    if (!ensure_server_action(StringName("instantiate_player"))) {
        return nullptr;
    }
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr) {
        return nullptr;
    }
    return core->entity_instantiate_player(get_owner(), p_participant);
}

Node *NetwEntity::spawn_player(Object *p_participant, Object *p_scene) {
    if (!ensure_server_action(StringName("spawn_player"))) {
        return nullptr;
    }
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr) {
        return nullptr;
    }
    return core->entity_spawn_player(get_owner(), p_participant, p_scene);
}

void NetwEntity::reparent_to(
    Object *p_new_parent,
    const Ref<NetwReparentOpts> &p_opts
) {
    if (!ensure_server_action(StringName("reparent_to"))) {
        return;
    }
    Node *owner = get_owner();
    NETW_ERR_COND(owner == nullptr, sys::ENTITY, "reparent_to requires an owner");
    NETW_ERR_COND(
        Object::cast_to<Node>(p_new_parent) == nullptr,
        sys::ENTITY,
        "reparent_to requires a parent"
    );
    Ref<NetwReparentOpts> opts = p_opts;
    if (opts.is_null()) {
        opts.instantiate();
    }
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr) {
        NetwMultiplayerCore::entity_move(record, owner, p_new_parent, opts);
        return;
    }
    core->entity_reparent(record, owner, p_new_parent, opts);
}

void NetwEntity::despawn(const Ref<NetwDespawnOpts> &p_opts) {
    if (!ensure_server_action(StringName("despawn"))) {
        return;
    }
    Ref<NetwDespawnOpts> opts = p_opts;
    if (opts.is_null()) {
        opts.instantiate();
    }
    Node *owner = get_owner();
    const int64_t from = get_stage();
    if (!record->begin_despawn(this, owner, opts)) {
        return;
    }
    note_stage(from);
    if (opts->get_flush_save()) {
        Object *engine = gd::live_object(get_persistence());
        if (engine != nullptr) {
            engine->call(StringName("flush"));
        }
    }
    if (owner == nullptr) {
        return;
    }
    if (owner->get_multiplayer_authority() != 1) {
        owner->set_multiplayer_authority(1);
    }
    if (opts->get_linger()) {
        transition(int64_t(EntityStage::LINGERING));
        linger_then_free(opts);
        return;
    }
    if (opts->get_defer_free()) {
        Callable(owner, StringName("queue_free")).call_deferred();
    } else {
        owner->queue_free();
    }
}

void NetwEntity::_remote_despawn(
    const StringName &p_reason,
    double p_linger_seconds
) {
    Ref<NetwDespawnOpts> opts = NetwDespawnOpts::create(p_reason);
    opts->set_linger(p_linger_seconds > 0.0);
    opts->set_linger_seconds(p_linger_seconds);
    const int64_t from = get_stage();
    if (!record->begin_despawn(this, get_owner(), opts)) {
        return;
    }
    note_stage(from);
    if (opts->get_linger()) {
        transition(int64_t(EntityStage::LINGERING));
    }
}

Variant NetwEntity::get_components() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_COMPONENTS,
        const_cast<NetwEntity *>(this)
    );
}

void NetwEntity::register_component(Object *p_component) {
    Ref<RefCounted> map = record->part(NetwEntityRecord::PART_COMPONENTS, this);
    if (map.is_valid()) {
        map->call(StringName("register"), p_component);
    }
}

NodePath NetwEntity::relative_path(Object *p_source, Object *p_target) const {
    return NetwMultiplayerCore::relative_path(p_source, p_target);
}

NodePath NetwEntity::property_path(
    Object *p_source,
    const StringName &p_property,
    Object *p_base
) const {
    return NetwMultiplayerCore::property_path(
        p_source,
        p_property,
        p_base != nullptr ? p_base : get_owner()
    );
}

Variant NetwEntity::get_persistence() const {
    Object *api = session();
    if (api == nullptr) {
        return Variant();
    }
    Object *plane = gd::live_object(api->get(StringName("_persistence")));
    if (plane == nullptr) {
        return Variant();
    }
    return plane->call(
        StringName("engine_for"),
        const_cast<NetwEntity *>(this)
    );
}

Variant NetwEntity::derived_binding(int64_t p_record) const {
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr) {
        return Variant();
    }
    return core->entity_derived_binding(
        get_owner(),
        p_record,
        core->liveness_route_of(const_cast<NetwEntity *>(this))
    );
}

Variant NetwEntity::get_state_binding() const {
    return derived_binding(repl::SET_RECORD_STATE);
}

Variant NetwEntity::get_input_binding() const {
    return derived_binding(repl::SET_RECORD_INPUT);
}

Variant NetwEntity::get_broadcast_binding() const {
    return derived_binding(repl::SET_RECORD_BROADCAST);
}

Variant NetwEntity::get_interest() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_INTEREST,
        const_cast<NetwEntity *>(this)
    );
}

Variant NetwEntity::own_scene() {
    return record->part(NetwEntityRecord::PART_SCENE, this);
}

Variant NetwEntity::get_scene() const {
    NetwEntity *self = const_cast<NetwEntity *>(this);
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr || get_owner() == nullptr) {
        return self->own_scene();
    }
    return core->entity_scene_facet(record, self);
}

Variant NetwEntity::get_prediction() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_PREDICTION,
        const_cast<NetwEntity *>(this)
    );
}

Variant NetwEntity::get_interpolation() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_DISPLAY,
        const_cast<NetwEntity *>(this)
    );
}

Variant NetwEntity::get_timeline() const {
    return gd::held(gd::object_of(timeline_id));
}

void NetwEntity::set_timeline(Object *p_timeline) {
    timeline_id = gd::instance_id(p_timeline);
}

TypedArray<MultiplayerSynchronizer> NetwEntity::synchronizers() {
    if (!synchronizers_dirty && !synchronizers_cache.is_empty()) {
        return synchronizers_cache;
    }
    Node *owner = get_owner();
    if (owner == nullptr) {
        return synchronizers_cache;
    }
    const TypedArray<MultiplayerSynchronizer> found
        = NetwSynchronizers::of_node(owner);
    if (!found.is_empty() || !owner_exiting_tree) {
        synchronizers_cache = found;
    }
    synchronizers_dirty = owner_exiting_tree && synchronizers_cache.is_empty();
    return synchronizers_cache;
}

bool NetwEntity::governs_property(
    const NodePath &p_real_path,
    Object *p_exclude
) const {
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr) {
        return false;
    }
    return core->entity_governs_property(
        get_owner(),
        p_real_path,
        p_exclude,
        core->liveness_route_of(const_cast<NetwEntity *>(this))
    );
}

void NetwEntity::invalidate_synchronizers_cache() {
    synchronizers_dirty = true;
    Node *owner = get_owner();
    if (owner != nullptr) {
        NetwSynchronizers::clear_cache(owner);
    }
}

Ref<NetwEntity> NetwEntity::parent_entity() const {
    NetwMultiplayerCore *core = session_core();
    if (core == nullptr || get_owner() == nullptr) {
        return Ref<NetwEntity>();
    }
    return as_entity(
        core->wrapper_of(core->entity_parent_of(record->get_handle()))
    );
}

void NetwEntity::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("meta_key"),
        &NetwEntity::meta_key
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("session_plane_for", "node"),
        &NetwEntity::session_plane_for
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("template_meta"),
        &NetwEntity::template_meta
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("set_session_lookup", "lookup"),
        &NetwEntity::set_session_lookup
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("of", "node"),
        &NetwEntity::of
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("ensure", "root"),
        &NetwEntity::ensure
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("resolve", "node"),
        &NetwEntity::resolve
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("from_rid", "entity", "api"),
        &NetwEntity::from_rid
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("by_route", "route", "api"),
        &NetwEntity::by_route
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("parse_entity", "node_name"),
        &NetwEntity::parse_entity
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("parse_peer", "node_name"),
        &NetwEntity::parse_peer
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("name_for", "join"),
        &NetwEntity::name_for
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("find", "root", "join"),
        &NetwEntity::find
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("bind", "node", "entity_id", "peer_id"),
        &NetwEntity::bind
    );
    ClassDB::bind_static_method(
        "NetwEntity",
        D_METHOD("instantiate_from", "template", "configure"),
        &NetwEntity::instantiate_from,
        DEFVAL(Callable())
    );

    ClassDB::bind_method(D_METHOD("get_owner"), &NetwEntity::get_owner);
    ClassDB::bind_method(
        D_METHOD("set_owner", "owner"),
        &NetwEntity::set_owner
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "owner",
            PROPERTY_HINT_NODE_TYPE,
            "Node"
        ),
        "set_owner",
        "get_owner"
    );

    ClassDB::bind_method(D_METHOD("get_entity_id"), &NetwEntity::get_entity_id);
    ClassDB::bind_method(
        D_METHOD("set_entity_id", "entity_id"),
        &NetwEntity::set_entity_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "entity_id"),
        "set_entity_id",
        "get_entity_id"
    );

    ClassDB::bind_method(D_METHOD("get_peer_id"), &NetwEntity::get_peer_id);
    ClassDB::bind_method(
        D_METHOD("set_peer_id", "peer_id"),
        &NetwEntity::set_peer_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "peer_id"),
        "set_peer_id",
        "get_peer_id"
    );

    ClassDB::bind_method(D_METHOD("get_route"), &NetwEntity::get_route);
    ClassDB::bind_method(
        D_METHOD("set_route", "route"),
        &NetwEntity::set_route
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "route"),
        "set_route",
        "get_route"
    );

    ClassDB::bind_method(D_METHOD("get_rid"), &NetwEntity::get_rid_handle);
    ClassDB::bind_method(
        D_METHOD("set_rid", "rid"),
        &NetwEntity::set_rid_handle
    );
    ADD_PROPERTY(PropertyInfo(Variant::RID, "rid"), "set_rid", "get_rid");

    ClassDB::bind_method(
        D_METHOD("get_multiplayer"),
        &NetwEntity::get_multiplayer
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "multiplayer",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_multiplayer"
    );
    ClassDB::bind_method(
        D_METHOD("_stamp_multiplayer", "api"),
        &NetwEntity::stamp_multiplayer
    );

    ClassDB::bind_method(
        D_METHOD("get_initial_controller"),
        &NetwEntity::get_initial_controller
    );
    ClassDB::bind_method(
        D_METHOD("set_initial_controller", "value"),
        &NetwEntity::set_initial_controller
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "initial_controller"),
        "set_initial_controller",
        "get_initial_controller"
    );

    ClassDB::bind_method(D_METHOD("get_transfer"), &NetwEntity::get_transfer);
    ClassDB::bind_method(
        D_METHOD("set_transfer", "value"),
        &NetwEntity::set_transfer
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "transfer"),
        "set_transfer",
        "get_transfer"
    );

    ClassDB::bind_method(
        D_METHOD("get_on_controller_disconnect"),
        &NetwEntity::get_on_controller_disconnect
    );
    ClassDB::bind_method(
        D_METHOD("set_on_controller_disconnect", "value"),
        &NetwEntity::set_on_controller_disconnect
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "on_controller_disconnect"),
        "set_on_controller_disconnect",
        "get_on_controller_disconnect"
    );

    ClassDB::bind_method(
        D_METHOD("get_declares_scene"),
        &NetwEntity::get_declares_scene
    );
    ClassDB::bind_method(
        D_METHOD("set_declares_scene", "value"),
        &NetwEntity::set_declares_scene
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "declares_scene"),
        "set_declares_scene",
        "get_declares_scene"
    );

    ClassDB::bind_method(
        D_METHOD("get_scene_label"),
        &NetwEntity::get_scene_label
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_label", "value"),
        &NetwEntity::set_scene_label
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "scene_label"),
        "set_scene_label",
        "get_scene_label"
    );

    ClassDB::bind_method(
        D_METHOD("get_scene_isolation"),
        &NetwEntity::get_scene_isolation
    );
    ClassDB::bind_method(
        D_METHOD("set_scene_isolation", "value"),
        &NetwEntity::set_scene_isolation
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "scene_isolation"),
        "set_scene_isolation",
        "get_scene_isolation"
    );

    ClassDB::bind_method(D_METHOD("get_controller"), &NetwEntity::get_controller);
    ClassDB::bind_method(
        D_METHOD("set_controller", "value"),
        &NetwEntity::set_controller
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "controller"),
        "set_controller",
        "get_controller"
    );

    ClassDB::bind_method(
        D_METHOD("get_control_kind"),
        &NetwEntity::get_control_kind
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "control_kind",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_control_kind"
    );

    ClassDB::bind_method(
        D_METHOD("get_is_controlled_locally"),
        &NetwEntity::get_is_controlled_locally
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::BOOL,
            "is_controlled_locally",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_is_controlled_locally"
    );

    ClassDB::bind_method(
        D_METHOD("get_controller_participant"),
        &NetwEntity::get_controller_participant
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "controller_participant",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_controller_participant"
    );

    ClassDB::bind_method(
        D_METHOD("get_action_spawn_tick"),
        &NetwEntity::get_action_spawn_tick
    );
    ClassDB::bind_method(
        D_METHOD("set_action_spawn_tick", "tick"),
        &NetwEntity::set_action_spawn_tick
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "action_spawn_tick"),
        "set_action_spawn_tick",
        "get_action_spawn_tick"
    );

    ClassDB::bind_method(
        D_METHOD("get_action_requester"),
        &NetwEntity::get_action_requester
    );
    ClassDB::bind_method(
        D_METHOD("set_action_requester", "peer"),
        &NetwEntity::set_action_requester
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "action_requester"),
        "set_action_requester",
        "get_action_requester"
    );

    ClassDB::bind_method(
        D_METHOD("request_control"),
        &NetwEntity::request_control
    );
    ClassDB::bind_method(
        D_METHOD("grant_control", "peer_id"),
        &NetwEntity::grant_control
    );
    ClassDB::bind_method(
        D_METHOD("revoke_control"),
        &NetwEntity::revoke_control
    );
    ClassDB::bind_method(
        D_METHOD("_handle_control_request", "sender"),
        &NetwEntity::_handle_control_request
    );
    ClassDB::bind_method(
        D_METHOD("_handle_control_apply", "peer"),
        &NetwEntity::_handle_control_apply
    );

    ClassDB::bind_method(
        D_METHOD("get_is_authority"),
        &NetwEntity::get_is_authority
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::BOOL,
            "is_authority",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_is_authority"
    );

    ClassDB::bind_method(
        D_METHOD("get_participant"),
        &NetwEntity::get_participant
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "participant",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_participant"
    );

    ClassDB::bind_method(D_METHOD("get_ownership"), &NetwEntity::get_ownership);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "ownership",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_ownership"
    );

    ClassDB::bind_method(D_METHOD("get_is_player"), &NetwEntity::get_is_player);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::BOOL,
            "is_player",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_is_player"
    );

    ClassDB::bind_method(
        D_METHOD("get_reparenting"),
        &NetwEntity::get_reparenting
    );
    ClassDB::bind_method(
        D_METHOD("set_reparenting", "opts"),
        &NetwEntity::set_reparenting
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
        D_METHOD("get_is_template"),
        &NetwEntity::get_is_template
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::BOOL,
            "is_template",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_is_template"
    );

    ClassDB::bind_method(D_METHOD("get_stage"), &NetwEntity::get_stage);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "stage",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_stage"
    );

    ClassDB::bind_method(
        D_METHOD("get_active_despawn_opts"),
        &NetwEntity::get_active_despawn_opts
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "active_despawn_opts",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwDespawnOpts",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_active_despawn_opts"
    );

    ClassDB::bind_method(D_METHOD("mark_template"), &NetwEntity::mark_template);
    ClassDB::bind_method(
        D_METHOD("arm", "api"),
        &NetwEntity::arm,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("spawn_under", "parent", "id"),
        &NetwEntity::spawn_under,
        DEFVAL(Variant()),
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("instantiate_player", "participant"),
        &NetwEntity::instantiate_player
    );
    ClassDB::bind_method(
        D_METHOD("spawn_player", "participant", "scene"),
        &NetwEntity::spawn_player
    );
    ClassDB::bind_method(
        D_METHOD("reparent_to", "new_parent", "opts"),
        &NetwEntity::reparent_to,
        DEFVAL(Ref<NetwReparentOpts>())
    );
    ClassDB::bind_method(
        D_METHOD("despawn", "opts"),
        &NetwEntity::despawn,
        DEFVAL(Ref<NetwDespawnOpts>())
    );
    ClassDB::bind_method(
        D_METHOD("_remote_despawn", "reason", "linger_seconds"),
        &NetwEntity::_remote_despawn
    );
    ClassDB::bind_method(
        D_METHOD("_go_live_if_armed"),
        &NetwEntity::_go_live_if_armed
    );
    ClassDB::bind_method(
        D_METHOD("_handle_tree_entered"),
        &NetwEntity::_handle_tree_entered
    );
    ClassDB::bind_method(
        D_METHOD("_handle_tree_exiting"),
        &NetwEntity::_handle_tree_exiting
    );
    ClassDB::bind_method(
        D_METHOD("_on_owner_ready"),
        &NetwEntity::_on_owner_ready
    );
    ClassDB::bind_method(
        D_METHOD("_on_peer_disconnected", "peer_id"),
        &NetwEntity::_on_peer_disconnected
    );
    ClassDB::bind_method(
        D_METHOD("_on_identity_hydrated"),
        &NetwEntity::_on_identity_hydrated
    );
    ClassDB::bind_method(
        D_METHOD("_settle_emit_reparented", "opts"),
        &NetwEntity::_settle_emit_reparented
    );
    ClassDB::bind_method(
        D_METHOD("_do_emit_reparented", "opts"),
        &NetwEntity::_do_emit_reparented
    );

    ClassDB::bind_method(D_METHOD("get_components"), &NetwEntity::get_components);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "components",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_components"
    );
    ClassDB::bind_method(
        D_METHOD("register_component", "component"),
        &NetwEntity::register_component
    );
    ClassDB::bind_method(
        D_METHOD("relative_path", "source", "target"),
        &NetwEntity::relative_path
    );
    ClassDB::bind_method(
        D_METHOD("property_path", "source", "property", "base"),
        &NetwEntity::property_path,
        DEFVAL(Variant())
    );

    ClassDB::bind_method(D_METHOD("get_persistence"), &NetwEntity::get_persistence);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "persistence",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_persistence"
    );

    ClassDB::bind_method(
        D_METHOD("get_state_binding"),
        &NetwEntity::get_state_binding
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "state_binding",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_state_binding"
    );
    ClassDB::bind_method(
        D_METHOD("get_input_binding"),
        &NetwEntity::get_input_binding
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "input_binding",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_input_binding"
    );
    ClassDB::bind_method(
        D_METHOD("get_broadcast_binding"),
        &NetwEntity::get_broadcast_binding
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "broadcast_binding",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_broadcast_binding"
    );

    ClassDB::bind_method(D_METHOD("get_interest"), &NetwEntity::get_interest);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "interest",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_interest"
    );
    ClassDB::bind_method(D_METHOD("get_scene"), &NetwEntity::get_scene);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "scene",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_scene"
    );
    ClassDB::bind_method(D_METHOD("get_prediction"), &NetwEntity::get_prediction);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "prediction",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_prediction"
    );
    ClassDB::bind_method(
        D_METHOD("get_interpolation"),
        &NetwEntity::get_interpolation
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "interpolation",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_interpolation"
    );

    ClassDB::bind_method(D_METHOD("get_timeline"), &NetwEntity::get_timeline);
    ClassDB::bind_method(
        D_METHOD("set_timeline", "timeline"),
        &NetwEntity::set_timeline
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "timeline",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwTimeline"
        ),
        "set_timeline",
        "get_timeline"
    );

    ClassDB::bind_method(
        D_METHOD("synchronizers"),
        &NetwEntity::synchronizers
    );
    ClassDB::bind_method(
        D_METHOD("governs_property", "real_path", "exclude"),
        &NetwEntity::governs_property,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("invalidate_synchronizers_cache"),
        &NetwEntity::invalidate_synchronizers_cache
    );
    ClassDB::bind_method(
        D_METHOD("parent_entity"),
        &NetwEntity::parent_entity
    );

    ADD_SIGNAL(MethodInfo(SIG_SPAWNING));
    ADD_SIGNAL(MethodInfo(SIG_SPAWNED));
    ADD_SIGNAL(
        MethodInfo(
            SIG_DESPAWNING,
            PropertyInfo(Variant::STRING_NAME, "reason")
        )
    );
    ADD_SIGNAL(MethodInfo(SIG_DESPAWNED));
    ADD_SIGNAL(
        MethodInfo(SIG_INTEREST_ENTER, PropertyInfo(Variant::INT, "peer_id"))
    );
    ADD_SIGNAL(
        MethodInfo(SIG_INTEREST_EXIT, PropertyInfo(Variant::INT, "peer_id"))
    );
    ADD_SIGNAL(MethodInfo(
        SIG_OBSERVER_ENTERED,
        PropertyInfo(Variant::STRING_NAME, "layer_id"),
        PropertyInfo(Variant::INT, "peer_id")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_OBSERVER_LEFT,
        PropertyInfo(Variant::STRING_NAME, "layer_id"),
        PropertyInfo(Variant::INT, "peer_id")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_CONTROL_CHANGED,
        PropertyInfo(Variant::INT, "previous_peer"),
        PropertyInfo(Variant::INT, "peer")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_CONTROL_REQUESTED,
        PropertyInfo(Variant::INT, "peer_id"),
        PropertyInfo(
            Variant::OBJECT,
            "request",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwControlRequest"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_REPARENTED,
        PropertyInfo(
            Variant::OBJECT,
            "reparent",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwReparentOpts"
        )
    ));
    ADD_SIGNAL(MethodInfo(SIG_VIEW_ACTIVATED));

    ClassDB::bind_integer_constant(
        get_class_static(),
        "Ownership",
        "OWNERSHIP_PEER",
        OWNERSHIP_PEER
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Ownership",
        "OWNERSHIP_SERVER",
        OWNERSHIP_SERVER
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "ControlKind",
        "CONTROL_PEER_CONTROLLED",
        CONTROL_PEER_CONTROLLED
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "ControlKind",
        "CONTROL_SERVER_CONTROLLED",
        CONTROL_SERVER_CONTROLLED
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "InitialController",
        "INITIAL_SERVER",
        int(InitialController::SERVER)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "InitialController",
        "INITIAL_REPRESENTED_PEER",
        int(InitialController::REPRESENTED_PEER)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Transfer",
        "TRANSFER_FIXED",
        int(Transfer::FIXED)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Transfer",
        "TRANSFER_REQUESTABLE",
        int(Transfer::REQUESTABLE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "DisconnectRule",
        "DISCONNECT_REVERT_TO_SERVER",
        int(DisconnectRule::REVERT_TO_SERVER)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "DisconnectRule",
        "DISCONNECT_DESPAWN",
        int(DisconnectRule::DESPAWN)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_UNBOUND",
        int(EntityStage::UNBOUND)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_TEMPLATE",
        int(EntityStage::TEMPLATE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_ARMED",
        int(EntityStage::ARMED)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_LIVE",
        int(EntityStage::LIVE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_DESPAWNING",
        int(EntityStage::DESPAWNING)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_LINGERING",
        int(EntityStage::LINGERING)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_FREED",
        int(EntityStage::FREED)
    );
}

} // namespace netw
