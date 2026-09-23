#include "netw/api/entity.hpp"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_handle.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/simulation_handle.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/entity/control.hpp"
#include "netw/entity/stage.hpp"
#include "netw/log.hpp"
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
constexpr const char *SIG_HIDDEN = "hidden";
constexpr const char *SIG_INTEREST_ENTER = "interest_enter";
constexpr const char *SIG_INTEREST_EXIT = "interest_exit";
constexpr const char *SIG_OBSERVER_ENTERED = "observer_entered";
constexpr const char *SIG_OBSERVER_LEFT = "observer_left";
constexpr const char *SIG_CONTROL_CHANGED = "control_changed";
constexpr const char *SIG_CONTROL_REQUESTED = "control_requested";
constexpr const char *SIG_REPARENTED = "reparented";
constexpr const char *SIG_VIEW_ACTIVATED = "view_activated";

NetwMultiplayer *session_on_branch(Node *p_node) {
    if (p_node == nullptr || !p_node->is_inside_tree()) {
        return nullptr;
    }
    const Ref<MultiplayerAPI> api = p_node->get_multiplayer();
    return Object::cast_to<NetwMultiplayer>(api.ptr());
}

Ref<NetwEntity> as_entity(const Ref<RefCounted> &p_wrapper) {
    return Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_wrapper.ptr()));
}

} // namespace

NetwEntity::NetwEntity() : record(memnew(NetwEntityRecord)) {
}

NetwEntity::~NetwEntity() {
    godot::memdelete(record);
}

StringName NetwEntity::meta_key() {
    return NetwMultiplayer::wrapper_meta();
}

StringName NetwEntity::template_meta() {
    return NetwEntityRecord::template_meta();
}

Ref<NetwMultiplayer> NetwEntity::session_plane_for(Node *p_node) {
    return Ref<NetwMultiplayer>(session_core_for(p_node));
}

NetwMultiplayer *NetwEntity::session_core_for(Node *p_node) {
    NetwMultiplayer *api = session_on_branch(p_node);
    if (api != nullptr) {
        return api;
    }
    return NetwMultiplayer::of(p_node);
}

Ref<NetwEntity> NetwEntity::of(Node *p_node) {
    return as_entity(NetwMultiplayer::wrapper_at(p_node));
}

Ref<NetwEntity> NetwEntity::ensure(Node *p_root) {
    return as_entity(NetwMultiplayer::wrapper_ensure(p_root));
}

Ref<NetwEntity> NetwEntity::resolve(Node *p_node) {
    return as_entity(NetwMultiplayer::wrapper_resolve(p_node));
}

Ref<NetwEntity> NetwEntity::from_rid(
    const RID &p_entity,
    const Ref<NetwMultiplayer> &p_api
) {
    if (p_api.is_null()) {
        return Ref<NetwEntity>();
    }
    return as_entity(p_api->entity_get_view(p_entity));
}

Ref<NetwEntity> NetwEntity::by_route(
    int64_t p_route,
    const Ref<NetwMultiplayer> &p_api
) {
    if (p_api.is_null()) {
        return Ref<NetwEntity>();
    }
    return as_entity(p_api->wrapper_for_route(p_route));
}

Node *NetwEntity::instantiate_from(
    Node *p_template,
    const Callable &p_configure
) {
    NetwMultiplayer *core = session_on_branch(p_template);
    if (core != nullptr) {
        return core->entity_instantiate_from(p_template, p_configure);
    }
    return NetwMultiplayer::entity_instantiate_copy(p_template, p_configure);
}

void NetwEntity::attach_to(Node *p_root) {
    if (p_root == nullptr) {
        return;
    }
    owner_id = gd::instance_id(p_root);
    p_root->set_meta(NetwMultiplayer::wrapper_meta(), this);
    const Callable entered(this, StringName("_handle_tree_entered"));
    if (!p_root->is_connected(StringName("tree_entered"), entered)) {
        p_root->connect(StringName("tree_entered"), entered);
    }
    const Callable exiting(this, StringName("_handle_tree_exiting"));
    if (!p_root->is_connected(StringName("tree_exiting"), exiting)) {
        p_root->connect(StringName("tree_exiting"), exiting);
    }
    if (p_root->is_inside_tree()) {
        entered.call_deferred();
    }
}

Node *NetwEntity::get_owner() const {
    return Object::cast_to<Node>(gd::object_of(owner_id));
}

void NetwEntity::set_owner(Node *p_owner) {
    owner_id = gd::instance_id(p_owner);
}

entity::Control *NetwEntity::control() const {
    return record->get_control();
}

NetwMultiplayer *NetwEntity::session_core() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

Ref<MultiplayerAPI> NetwEntity::get_multiplayer() const {
    return Ref<MultiplayerAPI>(session_core());
}

void NetwEntity::stamp_multiplayer(const Ref<NetwMultiplayer> &p_api) {
    if (p_api.is_null()) {
        return;
    }
    NetwMultiplayer *current = session_core();
    if (current == p_api.ptr()) {
        return;
    }
    NETW_ERR_COND(
        current != nullptr,
        sys::ENTITY,
        "multiplayer is immutable once stamped"
    );
    session_id = gd::instance_id(p_api.ptr());
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

int64_t NetwEntity::get_player_id() const {
    return record->get_player_id();
}

void NetwEntity::set_player_id(int64_t p_player_id) {
    record->set_player_id(p_player_id);
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

Ref<NetwPlayer> NetwEntity::get_controller_player() const {
    NetwMultiplayer *core = session_core();
    const int64_t steering = get_controller();
    if (core == nullptr || steering == 0) {
        return Ref<NetwPlayer>();
    }
    return core->player_of(steering);
}

int64_t NetwEntity::local_peer() const {
    Node *owner = get_owner();
    if (owner == nullptr) {
        return 0;
    }
    NetwMultiplayer *api = session_core();
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

ReplicationCore *NetwEntity::get_replication_plane() const {
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        return nullptr;
    }
    return core->get_replication_plane();
}

void NetwEntity::set_controller_internal(int64_t p_value) {
    const int64_t was = control()->get_controller();
    if (!record->set_controller(nullptr, p_value)) {
        return;
    }
    announce_control(was, p_value);
}

void NetwEntity::announce_control(int64_t p_was, int64_t p_peer) {
    emit_signal(StringName(SIG_CONTROL_CHANGED), p_was, p_peer);
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        return;
    }
    Dictionary detail;
    detail["from"] = p_was;
    detail["to"] = p_peer;
    core->event_emit(
        EventPlane::CONTROL_CHANGED,
        get_route(),
        detail,
        get_entity_id(),
        p_peer,
        OK,
        Dictionary()
    );
}

void NetwEntity::apply_control() {
    project_control(this);
}

void NetwEntity::project_control(Object *p_announcer) {
    NetwMultiplayer *api = session_core();
    const int64_t coordinator
        = api != nullptr ? api->session_authority_peer() : 1;
    record->apply_control(
        p_announcer,
        get_owner(),
        get_is_authority(),
        coordinator
    );
}

void NetwEntity::transfer_control(int64_t p_peer) {
    const int64_t was = control()->get_controller();
    if (!record->set_controller(nullptr, p_peer)) {
        apply_control();
        return;
    }
    project_control(nullptr);
    announce_control(was, p_peer);
}

int64_t NetwEntity::resolve_initial_controller() const {
    const int64_t resolved = control()->resolve(get_peer_id());
    if (resolved != 0) {
        return resolved;
    }
    Node *owner = get_owner();
    NetwMultiplayer *api = session_core();
    const int64_t coordinator
        = api != nullptr ? api->session_authority_peer() : 1;
    const int64_t claimed = owner != nullptr
        ? int64_t(owner->get_multiplayer_authority())
        : coordinator;
    return claimed != 1 && claimed != coordinator ? claimed : 0;
}

void NetwEntity::_on_peer_disconnected(int64_t p_peer_id) {
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree() || !get_is_authority()) {
        return;
    }
    switch (control()->disconnect_verdict(p_peer_id, get_peer_id())) {
        case entity::Control::DESPAWN_REPRESENTED: {
            NETW_INFO(
                sys::ENTITY,
                "peer %d disconnected, despawning represented entity '%s'",
                p_peer_id,
                owner->get_name()
            );
            despawn(NetwDespawnOpts::create(StringName("peer_disconnected")));
        } break;
        case entity::Control::REVERT_TO_SERVER:
            apply_control_change(0);
            break;
        case entity::Control::DESPAWN_CONTROLLER:
            despawn(
                NetwDespawnOpts::create(StringName("controller_disconnected"))
            );
            break;
        default:
            break;
    }
}

void NetwEntity::request_control() {
    NetwMultiplayer::entity_wrapper_request_control(
        this,
        get_owner(),
        get_replication_plane()
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
    NetwMultiplayer *core = session_core();
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
    transfer_control(p_peer);
    NetwMultiplayer::entity_broadcast_control(
        this,
        get_replication_plane(),
        p_peer
    );
}

void NetwEntity::_handle_control_apply(int64_t p_peer) {
    transfer_control(p_peer);
}

bool NetwEntity::get_is_authority() const {
    NetwMultiplayer *api = session_core();
    if (api == nullptr) {
        return true;
    }
    return api->is_host();
}

Ref<NetwPlayer> NetwEntity::get_player() const {
    NetwMultiplayer *core = session_core();
    const int64_t represented = get_peer_id();
    if (core == nullptr || represented == 0) {
        return Ref<NetwPlayer>();
    }
    return core->player_of(represented);
}

int64_t NetwEntity::get_ownership() const {
    return get_peer_id() != 0 ? int64_t(OWNERSHIP_PEER)
                              : int64_t(OWNERSHIP_SERVER);
}

bool NetwEntity::get_is_player() const {
    return get_peer_id() != 0;
}

bool NetwEntity::get_is_template() const {
    return get_stage() == int64_t(entity::Stage::TEMPLATE);
}

int64_t NetwEntity::get_stage() const {
    return record->get_stage();
}

Ref<NetwDespawnOpts> NetwEntity::get_active_despawn_opts() const {
    return record->get_active_despawn_opts();
}

void NetwEntity::note_stage(int64_t p_from) {
    NetwMultiplayer *core = session_core();
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

void NetwEntity::arm(const Ref<NetwMultiplayer> &p_api) {
    NETW_ERR_COND(
        get_stage() != int64_t(entity::Stage::UNBOUND),
        sys::ENTITY,
        "arm requires an unbound record"
    );
    stamp_multiplayer(p_api);
    if (!control()->get_configured()) {
        transfer_control(resolve_initial_controller());
    } else {
        apply_control();
    }
    transition(int64_t(entity::Stage::ARMED));
}

void NetwEntity::linger_then_free(const Ref<NetwDespawnOpts> &p_opts) {
    Node *owner = get_owner();
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        record->deactivate(owner);
        if (owner != nullptr) {
            owner->queue_free();
        }
        return;
    }
    core->entity_linger(
        record,
        owner,
        core->linger_pumps(p_opts->get_linger_seconds())
    );
}

void NetwEntity::_go_live_if_armed() {
    if (get_stage() != int64_t(entity::Stage::ARMED)) {
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
    if (session_core() == nullptr) {
        stamp_multiplayer(session_on_branch(owner));
    }
    NetwMultiplayer::entity_enter_tree(
        this,
        owner,
        record,
        session_core_for(owner),
        get_is_authority()
    );
}

void NetwEntity::hydrate_components() {
    Node *owner = get_owner();
    LocalVector<ObjectID> &registered = record->get_comp_registered();
    PackedStringArray paths;
    for (uint32_t at = 0; at < registered.size(); at++) {
        Object *component = gd::object_of(registered[at]);
        if (component == nullptr) {
            continue;
        }
        const NodePath relative
            = NetwMultiplayer::relative_path(owner, component);
        if (!relative.is_empty()) {
            paths.push_back(String(relative));
        }
    }
    NetwCompTable &table = record->get_comp_table();
    table.assign(paths);
    table.set_table_hash(comp_structure_hash(table.sorted_paths()));
    if (table.reconcile(get_is_authority())) {
        NETW_WARN(
            sys::ENTITY,
            "component table hash mismatch on entity '%s': server=%d, "
            "client=%d, so the table is poisoned",
            String(get_entity_id()),
            int(table.get_wire_hash()),
            int(table.get_table_hash())
        );
    }
    if (NetwMultiplayer *core = session_core()) {
        core->sync_pipeline_recapture_entity(this);
    }
}

int64_t NetwEntity::comp_structure_hash(
    const PackedStringArray &p_paths
) const {
    String seed;
    for (int at = 0; at < p_paths.size(); at++) {
        seed += p_paths[at] + String(",");
    }
    seed += "|methods:";

    LocalVector<Ref<Script>> scripts;
    Node *owner = get_owner();
    if (owner != nullptr) {
        const Ref<Script> declared = owner->get_script();
        if (declared.is_valid()) {
            scripts.push_back(declared);
        }
    }
    const LocalVector<ObjectID> &registered = record->get_comp_registered();
    for (uint32_t at = 0; at < registered.size(); at++) {
        Object *component = gd::object_of(registered[at]);
        if (component == nullptr) {
            continue;
        }
        const Ref<Script> declared = component->get_script();
        if (declared.is_valid()) {
            scripts.push_back(declared);
        }
    }

    for (uint32_t at = 0; at < scripts.size(); at++) {
        PackedStringArray methods;
        gd::script_rpc_method_names(scripts[at], methods);
        methods.sort();
        seed += "|m:" + String("/").join(methods);

        PackedStringArray properties;
        PackedStringArray signals;
        gd::script_property_and_signal_names(scripts[at], properties, signals);
        properties.sort();
        signals.sort();
        seed += "|p:" + String("/").join(properties);
        seed += "|s:" + String("/").join(signals);
    }
    return seed.hash() & 0xFFFF;
}

void NetwEntity::_on_owner_ready() {
    if (ready_once_fired) {
        return;
    }
    ready_once_fired = true;
    if (get_controller() != 0) {
        apply_control();
    }
    hydrate_components();
    NetwMultiplayer *core = session_core();
    if (core != nullptr && core->predict_lacks_state_rows(this)) {
        Node *owner = get_owner();
        NETW_ERROR(
            sys::PREDICTION,
            "prediction on %s is not registered, because the entity has no "
            ".state() row for prediction to compare and restore.",
            owner != nullptr ? String(owner->get_path()).utf8().get_data()
                             : String(get_entity_id()).utf8().get_data()
        );
    }
    if (core != nullptr && !core->persist_enroll(get_owner())) {
        core->predict_reconcile_declaration(this);
    }
    if (core != nullptr) {
        core->sim_declare(this);
    }
    emit_signal(SIG_SPAWNED);
}

void NetwEntity::_handle_tree_exiting() {
    owner_exiting_tree = true;
    if (NetwMultiplayer *core = session_core()) {
        core->entity_capture_exit(this);
        return;
    }
    const int64_t from = get_stage();
    record->complete_departure(this);
    note_stage(from);
}

Node *NetwEntity::spawn_under(Node *p_parent, const StringName &p_id) {
    if (!ensure_server_action(StringName("spawn_under"))) {
        return nullptr;
    }
    NetwMultiplayer *core = session_core();
    if (core != nullptr) {
        return core->entity_spawn_under(get_owner(), p_parent, p_id);
    }
    return NetwMultiplayer::entity_spawn_copy_under(
        get_owner(),
        p_parent,
        p_id
    );
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
    if (owner == nullptr) {
        return;
    }
    NetwMultiplayer *api = session_core();
    if (api != nullptr) {
        api->persist_depart(
            api->get_bindings()->find(owner),
            opts->get_flush_save()
        );
    }
    if (owner->get_multiplayer_authority() != 1) {
        owner->set_multiplayer_authority(1);
    }
    if (opts->get_linger()) {
        transition(int64_t(entity::Stage::LINGERING));
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
        transition(int64_t(entity::Stage::LINGERING));
    }
}

void NetwEntity::_remote_hide() {
    emit_signal(SIG_HIDDEN);
}

NetwCompTable &NetwEntity::comp_table() {
    return record->get_comp_table();
}

const NetwCompTable &NetwEntity::comp_table() const {
    return record->get_comp_table();
}

bool NetwEntity::get_comps_poisoned() const {
    return record->get_comp_table().get_poisoned();
}

bool NetwEntity::has_comp_path(const NodePath &p_path) const {
    return record->get_comp_table().has_path(String(p_path));
}

Node *NetwEntity::comp_node_of(int64_t p_comp) const {
    return record->get_comp_table().resolve_node(get_owner(), p_comp, String());
}

void NetwEntity::register_component(Node *p_component) {
    Node *owner = get_owner();
    if (p_component == nullptr || p_component == owner) {
        return;
    }
    LocalVector<ObjectID> &registered = record->get_comp_registered();
    const ObjectID id = gd::instance_id(p_component);
    for (uint32_t at = 0; at < registered.size(); at++) {
        if (registered[at] == id) {
            return;
        }
    }
    registered.push_back(id);
    NETW_WARN_COND(
        record->get_comp_table().get_table_hash() != 0,
        sys::ENTITY,
        "component '%s' registered after the table hydrated, so it is too "
        "late to ride the spawn packet. Configure networked properties "
        "inside _init()",
        String(p_component->get_name())
    );
}

NodePath NetwEntity::relative_path(Node *p_source, Node *p_target) const {
    return NetwMultiplayer::relative_path(p_source, p_target);
}

int64_t NetwEntity::comp_of(Node *p_node) const {
    Node *owner = get_owner();
    if (p_node == nullptr || owner == nullptr || p_node == owner) {
        return 0;
    }
    const NodePath rel = relative_path(owner, p_node);
    const NetwCompTable &table = record->get_comp_table();
    if (table.get_poisoned() || !table.has_path(String(rel))) {
        return NetwCompTable::FALLBACK_COMP;
    }
    return table.id_for_path(String(rel));
}

String NetwEntity::comp_path_of(Node *p_node) const {
    if (comp_of(p_node) != NetwCompTable::FALLBACK_COMP) {
        return String();
    }
    return String(relative_path(get_owner(), p_node));
}

NodePath NetwEntity::property_path(
    Node *p_source,
    const StringName &p_property,
    Node *p_base
) const {
    return NetwMultiplayer::property_path(
        p_source,
        p_property,
        p_base != nullptr ? p_base : get_owner()
    );
}


Ref<NetwPropertySetBinding> NetwEntity::derived_binding(
    int64_t p_record
) const {
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        return Ref<NetwPropertySetBinding>();
    }
    return core->entity_derived_binding(
        get_owner(),
        p_record,
        core->liveness_route_of(const_cast<NetwEntity *>(this))
    );
}

Ref<NetwPropertySetBinding> NetwEntity::get_state_binding() const {
    return derived_binding(repl::SET_RECORD_STATE);
}

Ref<NetwPropertySetBinding> NetwEntity::get_input_binding() const {
    return derived_binding(repl::SET_RECORD_INPUT);
}

Ref<NetwPropertySetBinding> NetwEntity::get_broadcast_binding() const {
    return derived_binding(repl::SET_RECORD_BROADCAST);
}

Ref<NetwInterestHandle> NetwEntity::get_interest() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_INTEREST,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwSceneHandle> NetwEntity::own_scene() {
    return record->part(NetwEntityRecord::PART_SCENE, this);
}

Ref<NetwSceneHandle> NetwEntity::get_scene() const {
    NetwEntity *self = const_cast<NetwEntity *>(this);
    NetwMultiplayer *core = session_core();
    if (core == nullptr || get_owner() == nullptr) {
        return self->own_scene();
    }
    return core->entity_scene_facet(record, self);
}

Ref<NetwPredictionHandle> NetwEntity::get_prediction() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_PREDICTION,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwSimulationHandle> NetwEntity::get_simulation() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_SIMULATION,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwPersistenceHandle> NetwEntity::get_persistence() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_PERSISTENCE,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwDisplayHandle> NetwEntity::get_interpolation() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_DISPLAY,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwTimeline> NetwEntity::get_timeline() const {
    return Ref<NetwTimeline>(
        Object::cast_to<NetwTimeline>(gd::object_of(timeline_id))
    );
}

void NetwEntity::set_timeline(const Ref<NetwTimeline> &p_timeline) {
    timeline_id = gd::instance_id(p_timeline.ptr());
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
        = synchronizers::of_node(owner);
    if (!found.is_empty() || !owner_exiting_tree) {
        synchronizers_cache = found;
    }
    synchronizers_dirty = owner_exiting_tree && synchronizers_cache.is_empty();
    return synchronizers_cache;
}

bool NetwEntity::governs_property(
    const NodePath &p_real_path,
    Node *p_exclude
) const {
    NetwMultiplayer *core = session_core();
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
        synchronizers::clear_cache(owner);
    }
}

Ref<NetwEntity> NetwEntity::parent_entity() const {
    NetwMultiplayer *core = session_core();
    if (core == nullptr || get_owner() == nullptr) {
        return Ref<NetwEntity>();
    }
    return as_entity(
        core->entity_get_view(core->entity_parent_of(record->get_handle()))
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
            "Node",
            PROPERTY_USAGE_DEFAULT,
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
    ADD_PROPERTY(PropertyInfo(Variant::INT, "route"), "set_route", "get_route");

    ClassDB::bind_method(D_METHOD("get_rid"), &NetwEntity::get_rid_handle);
    ADD_PROPERTY(PropertyInfo(Variant::RID, "rid"), "", "get_rid");

    ClassDB::bind_method(
        D_METHOD("get_multiplayer"),
        &NetwEntity::get_multiplayer
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "multiplayer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "MultiplayerAPI",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_multiplayer"
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
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "declares_scene"),
        "",
        "get_declares_scene"
    );

    ClassDB::bind_method(
        D_METHOD("get_scene_label"),
        &NetwEntity::get_scene_label
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "scene_label"),
        "",
        "get_scene_label"
    );

    ClassDB::bind_method(
        D_METHOD("get_scene_isolation"),
        &NetwEntity::get_scene_isolation
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "scene_isolation"),
        "",
        "get_scene_isolation"
    );

    ClassDB::bind_method(
        D_METHOD("get_controller"),
        &NetwEntity::get_controller
    );
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
        D_METHOD("get_controller_player"),
        &NetwEntity::get_controller_player
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "controller_player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer"
        ),
        "",
        "get_controller_player"
    );

    ClassDB::bind_method(
        D_METHOD("get_action_spawn_tick"),
        &NetwEntity::get_action_spawn_tick
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "action_spawn_tick"),
        "",
        "get_action_spawn_tick"
    );

    ClassDB::bind_method(
        D_METHOD("get_action_requester"),
        &NetwEntity::get_action_requester
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "action_requester"),
        "",
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
        D_METHOD("get_player"),
        &NetwEntity::get_player
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer"
        ),
        "",
        "get_player"
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
        DEFVAL(Ref<NetwMultiplayer>())
    );
    ClassDB::bind_method(
        D_METHOD("spawn_under", "parent", "id"),
        &NetwEntity::spawn_under,
        DEFVAL((Node *)nullptr),
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("despawn", "opts"),
        &NetwEntity::despawn,
        DEFVAL(Ref<NetwDespawnOpts>())
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
        D_METHOD("register_component", "component"),
        &NetwEntity::register_component
    );
    ClassDB::bind_method(
        D_METHOD("comp_node_of", "comp"),
        &NetwEntity::comp_node_of
    );
    ClassDB::bind_method(
        D_METHOD("get_comps_poisoned"),
        &NetwEntity::get_comps_poisoned
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "comps_poisoned"),
        "",
        "get_comps_poisoned"
    );
    ClassDB::bind_method(D_METHOD("comp_of", "node"), &NetwEntity::comp_of);
    ClassDB::bind_method(
        D_METHOD("comp_path_of", "node"),
        &NetwEntity::comp_path_of
    );


    ClassDB::bind_method(
        D_METHOD("get_state_binding"),
        &NetwEntity::get_state_binding
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "state_binding",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPropertySetBinding",
            PROPERTY_USAGE_NONE
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
            Variant::OBJECT,
            "input_binding",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPropertySetBinding",
            PROPERTY_USAGE_NONE
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
            Variant::OBJECT,
            "broadcast_binding",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPropertySetBinding",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_broadcast_binding"
    );

    ClassDB::bind_method(D_METHOD("get_interest"), &NetwEntity::get_interest);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "interest",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwInterestHandle",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_interest"
    );
    ClassDB::bind_method(D_METHOD("get_scene"), &NetwEntity::get_scene);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_scene"
    );
    ClassDB::bind_method(
        D_METHOD("get_prediction"),
        &NetwEntity::get_prediction
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "prediction",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPredictionHandle",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_prediction"
    );
    ClassDB::bind_method(
        D_METHOD("get_simulation"),
        &NetwEntity::get_simulation
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "simulation",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSimulationHandle",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_simulation"
    );
    ClassDB::bind_method(
        D_METHOD("get_persistence"),
        &NetwEntity::get_persistence
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "persistence",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPersistenceHandle",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_persistence"
    );
    ClassDB::bind_method(
        D_METHOD("get_interpolation"),
        &NetwEntity::get_interpolation
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "interpolation",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwDisplayHandle",
            PROPERTY_USAGE_NONE
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

    ClassDB::bind_method(D_METHOD("parent_entity"), &NetwEntity::parent_entity);

    ADD_SIGNAL(MethodInfo(SIG_SPAWNING));
    ADD_SIGNAL(MethodInfo(SIG_SPAWNED));
    ADD_SIGNAL(
        MethodInfo(SIG_DESPAWNING, PropertyInfo(Variant::STRING_NAME, "reason"))
    );
    ADD_SIGNAL(MethodInfo(SIG_DESPAWNED));
    ADD_SIGNAL(MethodInfo(SIG_HIDDEN));
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
    ADD_SIGNAL(MethodInfo(SIG_REPARENTED));
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
        int(entity::Control::InitialController::SERVER)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "InitialController",
        "INITIAL_REPRESENTED_PEER",
        int(entity::Control::InitialController::REPRESENTED_PEER)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Transfer",
        "TRANSFER_FIXED",
        int(entity::Control::Transfer::FIXED)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Transfer",
        "TRANSFER_REQUESTABLE",
        int(entity::Control::Transfer::REQUESTABLE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "DisconnectRule",
        "DISCONNECT_REVERT_TO_SERVER",
        int(entity::Control::DisconnectRule::REVERT_TO_SERVER)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "DisconnectRule",
        "DISCONNECT_DESPAWN",
        int(entity::Control::DisconnectRule::DESPAWN)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_UNBOUND",
        int(entity::Stage::UNBOUND)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_TEMPLATE",
        int(entity::Stage::TEMPLATE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_ARMED",
        int(entity::Stage::ARMED)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_LIVE",
        int(entity::Stage::LIVE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_DESPAWNING",
        int(entity::Stage::DESPAWNING)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_LINGERING",
        int(entity::Stage::LINGERING)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Stage",
        "STAGE_FREED",
        int(entity::Stage::FREED)
    );
}

} // namespace netw
