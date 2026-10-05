#include "netw/api/entity.hpp"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/api/display.hpp"
#include "netw/api/interest.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/simulation.hpp"
#include "netw/colors.hpp"
#include "netw/entity/control.hpp"
#include "netw/entity/stage.hpp"
#include "netw/lifecycle/rule.hpp"
#include "netw/log.hpp"
#include "netw/peer_rename.hpp"
#include "netw/profile.hpp"
#include "netw/repl/set_model.hpp"
#include "netw/spawn/record.hpp"
#include "netw/store/validate.hpp"
#include "netw/sync_authoring.hpp"
#include "netw/synchronizers.hpp"
#include "netw/tree_hook.hpp"
#include "netw/wire/stream.hpp"

using namespace godot;
using namespace netw;

namespace netw {

using repl::PropertyBindingPtr;
using repl::PropertySet;

namespace {

constexpr const char *SIG_SPAWNING = "spawning";
constexpr const char *SIG_SPAWNED = "spawned";
constexpr const char *SIG_DESPAWNING = "despawning";
constexpr const char *SIG_DESPAWNED = "despawned";
constexpr const char *SIG_HIDDEN = "hidden";
constexpr const char *SIG_CONTROL_CHANGED = "control_changed";
constexpr const char *SIG_CONTROL_REQUESTED = "control_requested";
constexpr const char *SIG_LIFECYCLE_REQUESTED = "lifecycle_requested";
constexpr const char *SIG_LIFECYCLE_REFUSED = "lifecycle_refused";
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

bool lifecycle_admits(
    NetwEntity *p_entity,
    NetwMultiplayer *p_core,
    lifecycle::Kind p_kind,
    const char *p_verb
) {
    if (p_core == nullptr) {
        return true;
    }
    String refusal;
    return p_core
        ->lifecycle_judge_verb(
            p_core->lifecycle_local_facts(p_entity, p_kind),
            String(p_verb),
            p_entity->get_owner(),
            refusal
        )
        .admitted();
}

} // namespace

NetwEntity::NetwEntity() : record(memnew(NetwEntityRecord)) {
}

NetwEntity::~NetwEntity() {
    if (NetwMultiplayer::is_tearing_down(session_id)) {
        session_id = ObjectID();
    }
    abandon_claims();
    structure_ops_refuse(
        0,
        ERR_UNAVAILABLE,
        String("the entity left before the session authority decided")
    );
    tree::unmark(uint64_t(owner_id), tree::MARK_ENTITY);
    if (readmission != nullptr) {
        memdelete(readmission);
    }
    godot::memdelete(record);
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

void NetwEntity::attach_to(Node *p_root) {
    if (p_root == nullptr) {
        return;
    }
    owner_id = gd::instance_id(p_root);
    p_root->set_meta(NetwMultiplayer::wrapper_meta(), this);
    tree::mark(p_root, tree::MARK_ENTITY);
    if (p_root->is_inside_tree()) {
        NetwMultiplayer::tree_adopt(p_root, Ref<NetwEntity>(this));
        callable_mp(this, &NetwEntity::_handle_tree_entered).call_deferred();
    }
}

bool NetwEntity::belongs_to(NetwMultiplayer *p_branch) const {
    NetwMultiplayer *core = session_core();
    return p_branch != nullptr && (core == nullptr || core == p_branch);
}

void NetwEntity::keep_readmission(const spawn::Record &p_recipe) {
    if (readmission == nullptr) {
        readmission = memnew(spawn::Record(p_recipe));
        return;
    }
    *readmission = p_recipe;
}

bool NetwEntity::readmit() {
    record->readmit();
    abandon_claims();
    owner_exiting_tree = false;
    despawning_watched = false;
    scene_subject = RID();
    synchronizers_dirty = true;
    synchronizers_absent = false;
    ready_once_fired = true;
    callable_mp(this, &NetwEntity::readmitted_ready).call_deferred();
    NetwMultiplayer *core = session_core();
    Node *owner = get_owner();
    if (readmission != nullptr) {
        const spawn::Record recipe = *readmission;
        memdelete(readmission);
        readmission = nullptr;
        if (core != nullptr && owner != nullptr && core->is_host()
            && core->spawn_readmit(owner, recipe)) {
            return true;
        }
    }
    if (core != nullptr) {
        arm(Ref<NetwMultiplayer>(core));
    }
    return false;
}

void NetwEntity::readmitted_ready() {
    Node *owner = get_owner();
    if (!ready_once_fired || owner == nullptr || !owner->is_inside_tree()
        || get_stage() != int64_t(entity::Stage::LIVE)) {
        return;
    }
    ready_once_fired = false;
    _on_owner_ready();
    if (!predicted_at_departure) {
        return;
    }
    predicted_at_departure = false;
    if (NetwMultiplayer *core = session_core()) {
        core->predict_readmit(this);
    }
}

void NetwEntity::tree_entered_edge() {
    const bool respawned
        = get_stage() == int64_t(entity::Stage::FREED) && readmit();
    NetwMultiplayer *core = session_core();
    const int64_t route = get_route();
    const bool hidden = !respawned && core != nullptr && route > 0
        && get_stage() == int64_t(entity::Stage::LIVE)
        && core->liveness_route_state(route)
            == NetwMultiplayer::ENTITY_STATE_ABSENT;
    if (!respawned) {
        _handle_tree_entered();
    }
    if (hidden && ready_once_fired) {
        callable_mp(this, &NetwEntity::readmitted_ready).call_deferred();
    }
    const Ref<NetwInterest> interest
        = record->minted_part(NetwEntityRecord::PART_INTEREST);
    if (interest.is_valid()) {
        interest->activate();
    }
    core = session_core();
    if (core != nullptr && scene_subject.is_valid()) {
        core->scene_report_live_entity_edge(this, scene_subject, true);
    }
}

void NetwEntity::tree_exiting_edge() {
    _handle_tree_exiting();
    const Ref<NetwInterest> interest
        = record->minted_part(NetwEntityRecord::PART_INTEREST);
    if (interest.is_valid()) {
        interest->deactivate();
    }
    NetwMultiplayer *core = session_core();
    if (core != nullptr && scene_subject.is_valid()) {
        core->scene_report_live_entity_edge(this, scene_subject, false);
    }
}

void NetwEntity::watch_scene_edges(const RID &p_subject) {
    scene_subject = p_subject;
}

void NetwEntity::watch_despawning(bool p_watched) {
    despawning_watched = p_watched;
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
        "This entity already belongs to another session and cannot move to "
        "a second one. Despawn it there and spawn it in the new session."
    );
    session_id = gd::instance_id(p_api.ptr());
}

StringName NetwEntity::get_name() const {
    return record->get_entity_id();
}

void NetwEntity::set_name(const StringName &p_name) {
    record->set_entity_id(p_name);
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

NetwEntity::InitialController NetwEntity::get_initial_controller() const {
    return InitialController(control()->get_initial());
}

void NetwEntity::set_initial_controller(InitialController p_value) {
    control()->set_initial(int64_t(p_value));
}

NetwEntity::Transfer NetwEntity::get_transfer() const {
    return Transfer(control()->get_transfer());
}

void NetwEntity::set_transfer(Transfer p_value) {
    control()->set_transfer(int64_t(p_value));
    NetwMultiplayer *core = session_core();
    if (core != nullptr) {
        core->sim_mark(get_rid_handle());
        core->sim_follow_steering(this);
    }
}

NetwEntity::DisconnectRule NetwEntity::get_on_controller_disconnect() const {
    return DisconnectRule(control()->get_on_disconnect());
}

void NetwEntity::set_on_controller_disconnect(DisconnectRule p_value) {
    control()->set_on_disconnect(int64_t(p_value));
}

NetwEntity::ParentDespawnRule NetwEntity::get_on_parent_despawn() const {
    return ParentDespawnRule(control()->get_on_parent_despawn());
}

void NetwEntity::set_on_parent_despawn(ParentDespawnRule p_value) {
    control()->set_on_parent_despawn(int64_t(p_value));
}

NetwEntity::Lifecycle NetwEntity::get_lifecycle() const {
    return Lifecycle(control()->get_lifecycle());
}

void NetwEntity::set_lifecycle(Lifecycle p_value) {
    control()->set_lifecycle(int64_t(p_value));
}

void NetwEntity::structure_op_issue(
    uint64_t p_base,
    const Ref<NetwPromise> &p_promise
) {
    StructureOp issued;
    issued.base = p_base;
    issued.promise = p_promise;
    structure_ops.push_back(issued);
}

void NetwEntity::structure_ops_accept(uint64_t p_revision) {
    uint32_t at = 0;
    while (at < structure_ops.size()) {
        if (structure_ops[at].base >= p_revision) {
            ++at;
            continue;
        }
        const Ref<NetwPromise> promise = structure_ops[at].promise;
        structure_ops.remove_at(at);
        if (promise.is_valid() && !promise->get_is_settled()) {
            promise->resolve(OK);
        }
    }
}

bool NetwEntity::structure_ops_refuse(
    uint64_t p_base,
    Error p_code,
    const String &p_detail
) {
    bool refused = false;
    uint32_t at = 0;
    while (at < structure_ops.size()) {
        if (structure_ops[at].base < p_base) {
            ++at;
            continue;
        }
        const Ref<NetwPromise> promise = structure_ops[at].promise;
        structure_ops.remove_at(at);
        refused = true;
        if (promise.is_valid() && !promise->get_is_settled()) {
            promise->reject(p_code, p_detail);
        }
    }
    return refused;
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
    return entity::Control::public_peer(steering());
}

int64_t NetwEntity::steering() const {
    return control()->resolve(get_peer_id());
}

void NetwEntity::set_controller(int64_t p_value) {
    if (get_stage() != int64_t(entity::Stage::UNBOUND)) {
        transfer_control(p_value);
        return;
    }
    NETW_ERR_COND(
        p_value <= 0,
        sys::ENTITY,
        "'%s' cannot be controlled by peer %d. Write a peer id, or 1 for the "
        "server.",
        String(get_name()),
        p_value
    );
    record_controller(entity::Control::from_public(p_value));
}

NetwEntity::ControlKind NetwEntity::get_control_kind() const {
    return steering() != entity::Control::SERVER ? CONTROL_PEER_CONTROLLED
                                                 : CONTROL_SERVER_CONTROLLED;
}

bool NetwEntity::get_is_controlled_locally() const {
    return is_controller_here() || is_claim_running_ahead();
}

bool NetwEntity::is_controller_here() const {
    return control()->controlled_by(local_peer(), get_peer_id());
}

bool NetwEntity::is_claim_running_ahead() const {
    return control()->runs_ahead() && !is_controller_here();
}

bool NetwEntity::is_claim_pending() const {
    return control()->claims_pending();
}

Ref<NetwPlayer> NetwEntity::get_controller_player() const {
    NetwMultiplayer *core = session_core();
    const int64_t steered = steering();
    if (core == nullptr || steered == entity::Control::SERVER) {
        return Ref<NetwPlayer>();
    }
    return core->player_of(steered);
}

int64_t NetwEntity::local_peer() const {
#if defined(NETW_TESTS)
    store::peer_reads_seen() += 1;
#endif
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

void NetwEntity::set_controller_player(const Ref<NetwPlayer> &p_player) {
    const int64_t peer = p_player.is_valid() ? p_player->get_peer_id() : 0;
    NETW_ERR_COND(
        p_player.is_valid() && peer <= 0,
        sys::ENTITY,
        "'%s' cannot be given to a player that has left. Pass a seated "
        "player, or null for the server.",
        String(get_name())
    );
    const int64_t steered = p_player.is_valid() ? peer : entity::Control::SERVER;
    if (get_stage() == int64_t(entity::Stage::UNBOUND)) {
        record_controller(steered);
        return;
    }
    transfer_steering(steered);
}

Error NetwEntity::transfer_control(int64_t p_peer) {
    NETW_ERR_COND_V(
        p_peer <= 0,
        ERR_INVALID_PARAMETER,
        sys::ENTITY,
        "'%s' cannot be given to peer %d. Pass a peer id, or 1 for the "
        "server.",
        String(get_name()),
        p_peer
    );
    return transfer_steering(entity::Control::from_public(p_peer));
}

Error NetwEntity::transfer_steering(int64_t p_steering) {
    NETW_ERR_COND_V(
        get_stage() == int64_t(entity::Stage::UNBOUND),
        ERR_UNCONFIGURED,
        sys::ENTITY,
        "'%s' is not spawned yet. Set NetwEntity.controller to choose its "
        "first controller.",
        String(get_name())
    );
    NETW_ERR_COND_V(
        !is_on_server(),
        ERR_UNAUTHORIZED,
        sys::ENTITY,
        "Only the server can give '%s' to another peer. A client asks for "
        "it with NetwEntity.claim_authority.",
        String(get_name())
    );
    apply_control_change(p_steering);
    return OK;
}

ReplicationCore *NetwEntity::get_replication_plane() const {
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        return nullptr;
    }
    return core->get_replication_plane();
}

void NetwEntity::record_controller(int64_t p_value) {
    const int64_t was = control()->get_controller();
    if (!record->set_controller(nullptr, p_value)) {
        return;
    }
    announce_control(was, p_value);
}

void NetwEntity::announce_control(int64_t p_was, int64_t p_peer) {
    control_moved();
    const int64_t was = entity::Control::public_peer(p_was);
    const int64_t peer = entity::Control::public_peer(p_peer);
    emit_signal(StringName(SIG_CONTROL_CHANGED), was, peer);
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        return;
    }
    Dictionary detail;
    detail["from"] = was;
    detail["to"] = peer;
    core->event_emit(
        EventPlane::CONTROL_CHANGED,
        get_route(),
        detail,
        get_name(),
        peer,
        OK,
        Dictionary()
    );
}

void NetwEntity::apply_control() {
    project_control(this);
}

void NetwEntity::project_control(Object *p_announcer) {
    NetwMultiplayer *api = session_core();
    record->apply_control(p_announcer, get_owner(), uint64_t(get_instance_id()));
    if (api != nullptr && record->projects_to_server()) {
        api->entity_watch_authority_joins();
    }
    control_moved();
}

void NetwEntity::descendant_entered(Node *p_node) {
    Node *owner = get_owner();
    if (p_node == nullptr || owner == nullptr) {
        return;
    }
    Node *parent = p_node->get_parent();
    if (parent != owner && !owner->is_ancestor_of(parent)) {
        tree::unwatch(parent);
        return;
    }
    record->project_entered(owner, p_node, uint64_t(get_instance_id()));
    control_moved();
}

void NetwEntity::follow_server(const NodePath &p_path) {
    NETW_ERR_COND(
        p_path.is_empty() || p_path.is_absolute() || p_path == NodePath("."),
        sys::ENTITY,
        "NetwEntity.follow_server got '%s'. Pass a path to a child, "
        "relative to the entity root.",
        String(p_path).utf8().get_data()
    );
    record->follow_server(p_path);
    const int64_t stage = get_stage();
    if (stage == int64_t(entity::Stage::ARMED)
        || stage == int64_t(entity::Stage::LIVE)) {
        project_control(nullptr);
    }
}

void NetwEntity::adopt_controller(int64_t p_peer) {
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
    if (resolved != entity::Control::SERVER) {
        return resolved;
    }
    Node *owner = get_owner();
    const int64_t claimed = owner != nullptr
        ? int64_t(owner->get_multiplayer_authority())
        : SERVER_PEER;
    return entity::Control::from_public(claimed);
}

void NetwEntity::_on_peer_disconnected(int64_t p_peer_id) {
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree() || !is_on_server()) {
        return;
    }
    switch (control()->disconnect_verdict(p_peer_id, get_peer_id())) {
        case entity::Control::DESPAWN_REPRESENTED: {
            NETW_INFO(
                sys::ENTITY,
                "Peer %d disconnected, so its entity '%s' is despawned.",
                p_peer_id,
                owner->get_name()
            );
            despawn(NetwDespawnOpts::create(StringName("peer_disconnected")));
        } break;
        case entity::Control::REVERT_TO_SERVER:
            apply_control_change(entity::Control::SERVER);
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

bool NetwEntity::rename_peers(const PeerRename &p_rename) {
    entity::Control *held = control();
    const int64_t peer = record->get_peer_id();
    const int64_t controller = held->controller;
    record->set_peer_id(p_rename.of(peer));
    held->controller = p_rename.of(controller);
    return record->get_peer_id() != peer || held->controller != controller;
}

void NetwEntity::follow_rename() {
    const int64_t stage = get_stage();
    if (stage == int64_t(entity::Stage::ARMED)
        || stage == int64_t(entity::Stage::LIVE)) {
        project_control(nullptr);
    }
}

void NetwEntity::forget_server_claims() {
    if (!control_claims.is_empty() || control()->has_pending()) {
        reject_claims("the server moved while '%s' waited for control");
    }
    held_claim_image = HeldClaimImage();
}

bool NetwEntity::deciding_locally() const {
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree()) {
        return false;
    }
    const Ref<MultiplayerAPI> api = owner->get_multiplayer();
    if (api.is_valid() && api->get_multiplayer_peer().is_null()) {
        return true;
    }
    return is_on_server();
}

bool NetwEntity::immediate_unavailable() {
    if (control()->get_transfer()
        != int64_t(entity::Control::Transfer::IMMEDIATE)) {
        return false;
    }
    Node *owner = get_owner();
    if (owner != nullptr && authoring::declares_prediction(owner)) {
        return true;
    }
    if (get_state_binding() != nullptr) {
        return true;
    }
    return !synchronizers().is_empty();
}

String NetwEntity::refusal_detail(int64_t p_hold) const {
    return vformat(
        "control of '%s' with hold %d was refused, peer %d holds it with "
        "hold %d",
        String(get_name()),
        p_hold,
        get_controller(),
        control()->hold
    );
}

Error NetwEntity::local_refusal(int64_t p_local, int64_t p_hold) {
    if (control()->pending.size() >= entity::Control::MAX_PENDING) {
        return ERR_UNAVAILABLE;
    }
    if (immediate_unavailable()) {
        return ERR_UNAVAILABLE;
    }
    if (deciding_locally() && !control()->admits_request()) {
        return ERR_UNAVAILABLE;
    }
    if (entity::Control::excludes(
            p_local,
            p_hold,
            steering(),
            control()->hold
        )) {
        return ERR_UNAUTHORIZED;
    }
    return OK;
}

Ref<NetwPromise> NetwEntity::claim_authority(Hold p_hold) {
    return issue_claim(p_hold, 0);
}

Ref<NetwPromise> NetwEntity::claim_by_contact(const Ref<NetwEntity> &p_source) {
    NetwMultiplayer *core = session_core();
    if (p_source.is_null() || core == nullptr
        || !p_source->get_is_controlled_locally()) {
        return NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            vformat(
                "a contact claim on '%s' names a source this peer does not "
                "control",
                String(get_name())
            )
        );
    }
    const int64_t live = core->liveness_route_of(p_source.ptr());
    return issue_claim(
        entity::Control::HOLD_YIELDABLE,
        live > 0 ? live : p_source->get_route()
    );
}

bool NetwEntity::controls_source(
    int64_t p_requester,
    int64_t p_source_route
) const {
    if (p_source_route <= 0) {
        return true;
    }
    NetwMultiplayer *core = session_core();
    const Ref<NetwEntity> source = core != nullptr
        ? core->wrapper_for_route(p_source_route)
        : Ref<NetwEntity>();
    return source.is_valid() && source->steering() == p_requester;
}

Ref<NetwPromise> NetwEntity::issue_claim(Hold p_hold, int64_t p_source_route) {
    Node *owner = get_owner();
    const Ref<MultiplayerAPI> api = owner != nullptr && owner->is_inside_tree()
        ? owner->get_multiplayer()
        : Ref<MultiplayerAPI>();
    if (api.is_null()) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            vformat(
                "'%s' is in no session, so no peer can decide its control",
                String(get_name())
            )
        );
    }
    const int64_t local = api->get_unique_id();
    const int64_t hold = int64_t(p_hold);
    const Error refused = local_refusal(local, hold);
    if (refused != OK) {
        return NetwPromise::rejected(refused, refusal_detail(hold));
    }
    if (!control()->has_pending() && steering() == local
        && control()->hold == hold) {
        return NetwPromise::resolved(Ref<NetwEntity>(this));
    }
    if (deciding_locally()) {
        const entity::Control::Outcome outcome
            = decide_request(local, 0, hold, p_source_route);
        if (outcome == entity::Control::Outcome::GRANTED) {
            return NetwPromise::resolved(Ref<NetwEntity>(this));
        }
        return NetwPromise::rejected(
            outcome == entity::Control::Outcome::UNAVAILABLE ? ERR_UNAVAILABLE
                                                             : ERR_UNAUTHORIZED,
            refusal_detail(hold)
        );
    }
    ReplicationCore *plane = get_replication_plane();
    NetwMultiplayer *core = session_core();
    if (plane == nullptr || core == nullptr) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            vformat(
                "'%s' has no session plane to carry a control request",
                String(get_name())
            )
        );
    }
    const bool was_ahead = is_claim_running_ahead();
    const entity::Control::Pending *issued = control()->issue(
        entity::Control::OpKind::REQUEST,
        hold,
        core->clock_get_tick()
    );
    if (issued == nullptr) {
        return NetwPromise::rejected(ERR_UNAVAILABLE, refusal_detail(hold));
    }
    ControlClaim claim;
    claim.op = issued->op;
    claim.promise.instantiate();
    control_claims.push_back(claim);
    NETW_TRACE(sys::ENTITY, "Asking the session authority for control.");
    send_control_op(*issued, 0, PackedByteArray(), p_source_route);
    follow_claim(was_ahead);
    return claim.promise;
}

Ref<NetwPromise> NetwEntity::release_authority(int64_t p_successor) {
    if (p_successor <= 0) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            vformat(
                "'%s' cannot be released to peer %d. Pass a peer id, or 1 "
                "for the server",
                String(get_name()),
                p_successor
            )
        );
    }
    const int64_t successor = entity::Control::from_public(p_successor);
    const int64_t local = local_peer();
    if (!control()->claims_pending() && steering() != local) {
        return NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            vformat(
                "'%s' is not controlled by peer %d, so it has nothing to "
                "release",
                String(get_name()),
                local
            )
        );
    }
    if (!holds_copy(successor)) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            vformat(
                "peer %d holds no copy of '%s', so it cannot take it on",
                p_successor,
                String(get_name())
            )
        );
    }
    NetwMultiplayer *core = session_core();
    const PackedByteArray image
        = final_image(core != nullptr ? core->clock_get_tick() : 0);
    if (deciding_locally()) {
        const entity::Control::Outcome outcome
            = decide_release(local, 0, successor, image);
        if (outcome == entity::Control::Outcome::GRANTED) {
            return NetwPromise::resolved(Ref<NetwEntity>(this));
        }
        return NetwPromise::rejected(
            outcome == entity::Control::Outcome::UNAVAILABLE ? ERR_UNAVAILABLE
                                                             : ERR_UNAUTHORIZED,
            refusal_detail(entity::Control::HOLD_NONE)
        );
    }
    const entity::Control::Pending *issued = core == nullptr
        ? nullptr
        : control()->issue(
              entity::Control::OpKind::RELEASE,
              entity::Control::HOLD_NONE,
              core->clock_get_tick()
          );
    if (issued == nullptr) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            vformat(
                "'%s' cannot carry another control request now",
                String(get_name())
            )
        );
    }
    ControlClaim claim;
    claim.op = issued->op;
    claim.promise.instantiate();
    control_claims.push_back(claim);
    control_moved();
    send_control_op(*issued, successor, image);
    return claim.promise;
}

bool NetwEntity::holds_copy(int64_t p_peer) {
    if (p_peer == entity::Control::SERVER || p_peer == SERVER_PEER) {
        return true;
    }
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        return false;
    }
    if (p_peer == local_peer()) {
        return true;
    }
    return core->rpc_get_recipients(Ref<NetwEntity>(this)).has(int32_t(p_peer));
}

LocalVector<PropertyBindingPtr> NetwEntity::image_bindings() {
    LocalVector<PropertyBindingPtr> out;
    const LocalVector<PropertyBindingPtr> authored = authored_bindings();
    for (const PropertyBindingPtr &binding : authored) {
        if (binding->get_set()->get_record() == PropertySet::RECORD_BROADCAST) {
            out.push_back(binding);
        }
    }
    return out;
}

PackedByteArray NetwEntity::final_image(int64_t p_tick) {
    int64_t measured = 0;
    const PackedByteArray bytes = state_image(p_tick, measured);
    if (!bytes.is_empty() || measured <= session::CONTROL_FINAL_STATE_CAP) {
        return bytes;
    }
    NETW_WARN(
        sys::ENTITY,
        "'%s' released authority with %d bytes of state, over the %d byte "
        "limit, so the next authority starts from its own copy. Replicate "
        "fewer or smaller properties on this entity.",
        String(get_name()),
        measured,
        session::CONTROL_FINAL_STATE_CAP
    );
    return PackedByteArray();
}

PackedByteArray NetwEntity::state_image(int64_t p_tick, int64_t &r_size) {
    r_size = 0;
    const LocalVector<PropertyBindingPtr> bindings = image_bindings();
    NetwMultiplayer *core = session_core();
    if (bindings.is_empty() || core == nullptr) {
        return PackedByteArray();
    }
    LocalVector<PackedByteArray> runs;
    int64_t size = 0;
    for (const PropertyBindingPtr &binding : bindings) {
        const PackedByteArray run = core->store_take_image(binding);
        if (run.is_empty()) {
            return PackedByteArray();
        }
        size += run.size();
        runs.push_back(run);
    }
    wire::WriteStream stream;
    uint64_t stamped = uint64_t(MAX(p_tick, int64_t(-1)) + 1);
    uint64_t count = uint64_t(runs.size());
    stream.varuint(stamped);
    stream.varuint(count);
    for (PackedByteArray &run : runs) {
        stream.bytes_capped(run, session::CONTROL_FINAL_STATE_CAP);
    }
    stream.align_verify();
    const PackedByteArray bytes = stream.to_bytes();
    r_size = MAX(size, int64_t(bytes.size()));
    if (stream.ok() && r_size <= session::CONTROL_FINAL_STATE_CAP) {
        return bytes;
    }
    return PackedByteArray();
}

int64_t NetwEntity::install_final_image(
    const PackedByteArray &p_final_state,
    int64_t p_after_tick
) {
    if (p_final_state.is_empty()) {
        return -2;
    }
    const LocalVector<PropertyBindingPtr> bindings = image_bindings();
    wire::ReadStream reader(p_final_state);
    uint64_t stamped = 0;
    uint64_t count = 0;
    if (!reader.varuint(stamped) || !reader.varuint(count)
        || count != uint64_t(bindings.size())
        || int64_t(stamped) - 1 <= p_after_tick) {
        return -2;
    }
    LocalVector<Array> keys;
    LocalVector<Array> values;
    for (const PropertyBindingPtr &binding : bindings) {
        PackedByteArray run;
        Array run_keys;
        Array run_values;
        if (!reader.bytes_capped(run, session::CONTROL_FINAL_STATE_CAP)
            || !binding->read_image(run, run_keys, run_values)) {
            return -2;
        }
        keys.push_back(run_keys);
        values.push_back(run_values);
    }
    NetwMultiplayer *core = session_core();
    for (uint32_t at = 0; at < bindings.size() && core != nullptr; ++at) {
        const PropertyBindingPtr &binding = bindings[at];
        core->store_install_image(
            binding,
            keys[at],
            values[at],
            int64_t(stamped) - 1
        );
    }
    return int64_t(stamped) - 1;
}

void NetwEntity::start_claim_images() {
    NetwMultiplayer *core = session_core();
    ReplicationCore *plane = get_replication_plane();
    if (core == nullptr || plane == nullptr
        || get_transfer() != TRANSFER_IMMEDIATE) {
        return;
    }
    const int64_t now = core->clock_engine().get_tick();
    claim_image_until = now + 2 * core->clock_engine().get_tickrate();
    plane->watch_claim_image(Ref<NetwEntity>(this));
}

bool NetwEntity::sends_claim_image(int64_t p_tick) const {
    if (get_owner() == nullptr || p_tick > claim_image_until) {
        return false;
    }
    return is_claim_running_ahead() || is_controller_here();
}

PackedByteArray NetwEntity::claim_image(int64_t p_tick) {
    int64_t measured = 0;
    session::ClaimImage frame;
    frame.observed_revision = control()->revision;
    frame.state = state_image(p_tick, measured);
    return frame.state.is_empty() ? PackedByteArray()
                                  : session::frame_write(frame);
}

bool NetwEntity::claim_image_admits(
    int64_t p_sender,
    uint64_t p_observed
) const {
    return p_sender != 0 && p_sender == steering()
        && p_sender != local_peer() && p_observed + 1 >= control()->tenure;
}

void NetwEntity::install_claim_image(const PackedByteArray &p_state) {
    const int64_t installed = install_final_image(p_state, claim_image_tick);
    if (installed > claim_image_tick) {
        claim_image_tick = installed;
    }
}

void NetwEntity::install_held_claim_image(bool p_tenure_changed) {
    if (p_tenure_changed) {
        claim_image_tick = -1;
    }
    if (steering() == local_peer()) {
        held_claim_image = HeldClaimImage();
        if (p_tenure_changed) {
            start_claim_images();
        }
        return;
    }
    if (held_claim_image.state.is_empty()) {
        return;
    }
    const HeldClaimImage held = held_claim_image;
    held_claim_image = HeldClaimImage();
    NetwMultiplayer *core = session_core();
    const int64_t now = core != nullptr ? core->clock_engine().get_tick() : -1;
    const int64_t window
        = core != nullptr ? core->clock_engine().get_tickrate() : 0;
    if (now - held.received_tick > window
        || !claim_image_admits(held.sender, held.observed_revision)) {
        return;
    }
    install_claim_image(held.state);
}

void NetwEntity::_handle_claim_image(
    int64_t p_sender,
    const session::ClaimImage &p_image
) {
    if (p_image.state.is_empty() || p_sender == local_peer()) {
        return;
    }
    if (claim_image_admits(p_sender, p_image.observed_revision)) {
        install_claim_image(p_image.state);
        return;
    }
    if (steering() == p_sender) {
        return;
    }
    NetwMultiplayer *core = session_core();
    held_claim_image.sender = p_sender;
    held_claim_image.observed_revision = p_image.observed_revision;
    held_claim_image.received_tick
        = core != nullptr ? core->clock_engine().get_tick() : -1;
    held_claim_image.state = p_image.state;
}

LocalVector<PropertyBindingPtr> NetwEntity::authored_bindings() {
    LocalVector<PropertyBindingPtr> out;
    ReplicationCore *plane = get_replication_plane();
    NetwMultiplayer *core = session_core();
    if (plane == nullptr || core == nullptr) {
        return out;
    }
    const int64_t live = core->liveness_route_of(this);
    const LocalVector<PropertyBindingPtr> derived
        = plane->derived_group(live > 0 ? live : get_route());
    for (const PropertyBindingPtr &binding : derived) {
        if (binding != nullptr && binding->get_set() != nullptr
            && repl::record_follows_tenure(binding->get_set()->record)) {
            out.push_back(binding);
        }
    }
    return out;
}

void NetwEntity::control_moved() {
    NetwMultiplayer *core = session_core();
    if (core != nullptr) {
        core->store_resource_entity(this);
        core->sim_follow_steering(this);
    }
}

void NetwEntity::follow_claim(bool p_was_ahead) {
    control_moved();
    const bool ahead = is_claim_running_ahead();
    if (ahead == p_was_ahead) {
        return;
    }
    NetwMultiplayer *core = session_core();
    const LocalVector<PropertyBindingPtr> bindings = authored_bindings();
    for (uint32_t at = 0; at < bindings.size() && core != nullptr; ++at) {
        const PropertyBindingPtr &binding = bindings[at];
        if (ahead) {
            core->store_retain(binding);
        } else if (!is_controller_here()) {
            core->store_reinstall(binding);
        }
    }
    if (core != nullptr) {
        core->sim_follow_claim(Ref<NetwEntity>(this));
    }
    if (ahead) {
        start_claim_images();
    }
}

void NetwEntity::send_control_op(
    const entity::Control::Pending &p_op,
    int64_t p_successor,
    const PackedByteArray &p_final_state,
    int64_t p_source_route
) {
    ReplicationCore *plane = get_replication_plane();
    NetwMultiplayer *core = session_core();
    if (plane == nullptr || core == nullptr) {
        return;
    }
    core->entity_settle_captured(this);
    session::ControlRequest request;
    request.source_route = uint64_t(MAX(p_source_route, int64_t(0)));
    request.op = p_op.op;
    request.kind = uint8_t(p_op.kind);
    request.hold = uint8_t(p_op.hold);
    request.observed_revision = control()->revision;
    request.issued_tick = uint64_t(MAX(p_op.issued_tick, int64_t(0)));
    request.successor = uint64_t(MAX(p_successor, int64_t(0)));
    request.final_state = p_final_state;
    plane->request_control(this, request);
    plane->watch_control(this);
}

Ref<NetwPromise> NetwEntity::take_claim(uint64_t p_op) {
    for (uint32_t at = 0; at < control_claims.size(); ++at) {
        if (control_claims[at].op == p_op) {
            const Ref<NetwPromise> promise = control_claims[at].promise;
            control_claims.remove_at(at);
            return promise;
        }
    }
    return Ref<NetwPromise>();
}

void NetwEntity::settle_control(
    uint64_t p_op,
    entity::Control::Outcome p_outcome
) {
    const bool was_ahead = is_claim_running_ahead();
    entity::Control::Pending taken;
    if (control()->take_pending(p_op, taken)) {
        const Ref<NetwPromise> promise = take_claim(p_op);
        follow_claim(was_ahead);
        settle_claim(promise, taken, p_outcome);
        return;
    }
    if (control()->take_abandoned(p_op)
        && p_outcome == entity::Control::Outcome::GRANTED
        && steering() == local_peer()) {
        release_authority();
    }
}

void NetwEntity::settle_claim(
    const Ref<NetwPromise> &p_promise,
    const entity::Control::Pending &p_taken,
    entity::Control::Outcome p_outcome
) {
    if (p_promise.is_null()) {
        return;
    }
    const bool nothing_to_release
        = p_taken.kind == entity::Control::OpKind::RELEASE
        && p_outcome == entity::Control::Outcome::UNAUTHORIZED
        && steering() != local_peer();
    if (p_outcome == entity::Control::Outcome::GRANTED || nothing_to_release) {
        p_promise->resolve(Ref<NetwEntity>(this));
        return;
    }
    p_promise->reject(
        p_outcome == entity::Control::Outcome::UNAVAILABLE ? ERR_UNAVAILABLE
                                                           : ERR_UNAUTHORIZED,
        refusal_detail(p_taken.hold)
    );
}

bool NetwEntity::expire_control(int64_t p_tick, int64_t p_deadline) {
    if (get_owner() == nullptr) {
        abandon_claims();
        return false;
    }
    LocalVector<uint64_t> expired;
    for (uint32_t at = 0; at < control()->pending.size(); ++at) {
        const entity::Control::Pending &held = control()->pending[at];
        if (p_tick - held.issued_tick >= p_deadline) {
            expired.push_back(held.op);
        }
    }
    if (expired.is_empty()) {
        return control()->has_pending();
    }
    const bool was_ahead = is_claim_running_ahead();
    LocalVector<Ref<NetwPromise>> timed_out;
    for (uint32_t at = 0; at < expired.size(); ++at) {
        entity::Control::Pending taken;
        if (!control()->take_pending(expired[at], taken)) {
            continue;
        }
        if (taken.kind == entity::Control::OpKind::REQUEST) {
            control()->abandon(taken.op);
        }
        const Ref<NetwPromise> promise = take_claim(taken.op);
        if (promise.is_valid()) {
            timed_out.push_back(promise);
        }
    }
    follow_claim(was_ahead);
    for (const Ref<NetwPromise> &promise : timed_out) {
        promise->reject(
            ERR_TIMEOUT,
            vformat(
                "no control decision on '%s' arrived within %d ticks",
                String(get_name()),
                p_deadline
            )
        );
    }
    return control()->has_pending();
}

NetwEntity::Hold NetwEntity::get_hold() const {
    return Hold(control()->hold);
}

bool NetwEntity::get_is_control_pending() const {
    return control()->has_pending();
}

uint64_t NetwEntity::get_control_revision() const {
    return control()->revision;
}

uint64_t NetwEntity::get_control_tenure() const {
    return control()->tenure;
}

void NetwEntity::seed_control(
    uint64_t p_revision,
    uint64_t p_tenure,
    int64_t p_hold
) {
    control()->seed(p_revision, p_tenure, p_hold);
    control_moved();
}

void NetwEntity::_handle_control_request(
    int64_t p_sender,
    const session::ControlRequest &p_request
) {
    Node *owner = get_owner();
    if (owner == nullptr || !owner->is_inside_tree() || !is_on_server()) {
        NETW_WARN(
            sys::ENTITY,
            "A control request for '%s' reached a peer that is not the "
            "server, so it was ignored.",
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
    if (p_request.kind == uint8_t(entity::Control::OpKind::RELEASE)) {
        decide_release(
            requester,
            p_request.op,
            int64_t(p_request.successor),
            p_request.final_state
        );
        return;
    }
    decide_request(
        requester,
        p_request.op,
        int64_t(p_request.hold),
        int64_t(p_request.source_route)
    );
}

entity::Control::Outcome NetwEntity::decide_request(
    int64_t p_requester,
    uint64_t p_op,
    int64_t p_hold,
    int64_t p_source_route
) {
    using Outcome = entity::Control::Outcome;
    using Ruling = entity::Control::Ruling;
    const Ruling ruling
        = control()->rule(p_requester, p_hold, steering());
    Outcome outcome = Outcome::GRANTED;
    if (ruling == Ruling::UNAVAILABLE || immediate_unavailable()) {
        NETW_WARN(
            sys::ENTITY,
            ruling == Ruling::UNAVAILABLE
                ? "Peer %d asked for control of '%s', but its "
                  "NetwEntity.transfer is TRANSFER_FIXED. Set it to "
                  "TRANSFER_REQUESTABLE to allow requests."
                : "Peer %d asked for control of '%s', but TRANSFER_IMMEDIATE "
                  "is not available on an entity with state, prediction or "
                  "a MultiplayerSynchronizer. Set NetwEntity.transfer to "
                  "TRANSFER_REQUESTABLE.",
            int(p_requester),
            String(get_name())
        );
        outcome = Outcome::UNAVAILABLE;
    } else if (ruling == Ruling::EXCLUDED) {
        outcome = Outcome::UNAUTHORIZED;
    } else if (!controls_source(p_requester, p_source_route)) {
        outcome = Outcome::UNAUTHORIZED;
    } else if (
        ruling == Ruling::ASK_FILTER
        && record->admit_control_request(this, p_requester, p_hold) == 0
    ) {
        outcome = Outcome::UNAUTHORIZED;
    }
    NetwMultiplayer *core = session_core();
    if (core != nullptr) {
        Dictionary detail;
        detail["requester"] = p_requester;
        detail["hold"] = p_hold;
        core->event_emit(
            EventPlane::CONTROL_REQUESTED,
            get_route(),
            detail,
            get_name(),
            p_requester,
            outcome == Outcome::GRANTED           ? OK
                : outcome == Outcome::UNAVAILABLE ? ERR_UNAVAILABLE
                                                  : ERR_UNAUTHORIZED,
            Dictionary()
        );
    }
    if (outcome == Outcome::GRANTED) {
        decide_control(p_requester, p_hold, p_requester, p_op);
    } else {
        refuse_control(p_requester, p_op, outcome);
    }
    return outcome;
}

entity::Control::Outcome NetwEntity::decide_release(
    int64_t p_requester,
    uint64_t p_op,
    int64_t p_successor,
    const PackedByteArray &p_final_state
) {
    using Outcome = entity::Control::Outcome;
    if (steering() != p_requester) {
        refuse_control(p_requester, p_op, Outcome::UNAUTHORIZED);
        return Outcome::UNAUTHORIZED;
    }
    if (!holds_copy(p_successor)) {
        refuse_control(p_requester, p_op, Outcome::UNAVAILABLE);
        return Outcome::UNAVAILABLE;
    }
    decide_control(
        p_successor,
        entity::Control::HOLD_NONE,
        p_requester,
        p_op,
        p_final_state
    );
    return Outcome::GRANTED;
}

void NetwEntity::refuse_control(
    int64_t p_requester,
    uint64_t p_op,
    entity::Control::Outcome p_outcome
) {
    ReplicationCore *plane = get_replication_plane();
    if (p_op == 0 || plane == nullptr) {
        return;
    }
    session::ControlApply reply;
    reply.controller = uint64_t(MAX(steering(), int64_t(0)));
    reply.revision = control()->revision;
    reply.hold = uint8_t(control()->hold);
    reply.op = p_op;
    reply.outcome = uint8_t(p_outcome);
    plane->reply_control(this, reply, p_requester);
}

void NetwEntity::decide_control(
    int64_t p_peer,
    int64_t p_hold,
    int64_t p_issuer,
    uint64_t p_op,
    const PackedByteArray &p_final_state
) {
    const bool tenure_changed = control()->decide(p_peer, p_hold);
    control_moved();
    adopt_controller(p_peer);
    NetwMultiplayer *core = session_core();
    if (tenure_changed && core != nullptr) {
        core->row_streams_follow_tenure(get_route(), control()->tenure);
    }
    install_final_image(p_final_state);
    install_held_claim_image(tenure_changed);
    ReplicationCore *plane = get_replication_plane();
    if (plane == nullptr) {
        return;
    }
    session::ControlApply decision;
    decision.controller = uint64_t(MAX(p_peer, int64_t(0)));
    decision.revision = control()->revision;
    decision.tenure_changed = tenure_changed;
    decision.hold = uint8_t(control()->hold);
    decision.op = p_op;
    decision.outcome = uint8_t(entity::Control::Outcome::GRANTED);
    decision.final_state = p_final_state;
    plane->broadcast_control(this, decision, p_issuer);
}

void NetwEntity::apply_control_change(int64_t p_peer) {
    decide_control(p_peer, entity::Control::HOLD_NONE, 0, 0);
}

void NetwEntity::raise_control(uint64_t p_floor) {
    entity::Control *held = control();
    held->revision = MAX(held->revision, p_floor) + 1;
    held->tenure = held->revision;
    control_moved();
    NetwMultiplayer *core = session_core();
    if (core != nullptr) {
        core->row_streams_follow_tenure(get_route(), held->tenure);
    }
    ReplicationCore *plane = get_replication_plane();
    if (plane == nullptr) {
        return;
    }
    session::ControlApply decision;
    decision.controller = uint64_t(MAX(steering(), int64_t(0)));
    decision.revision = held->revision;
    decision.tenure_changed = true;
    decision.hold = uint8_t(held->hold);
    plane->broadcast_control(this, decision, 0);
}

void NetwEntity::_handle_control_apply(const session::ControlApply &p_applied) {
    if (control()->install(
            p_applied.revision,
            p_applied.tenure_changed,
            int64_t(p_applied.hold)
        )) {
        control_moved();
        adopt_controller(int64_t(p_applied.controller));
        NetwMultiplayer *core = session_core();
        if (p_applied.tenure_changed && core != nullptr) {
            core->row_streams_follow_tenure(get_route(), control()->tenure);
        }
        install_final_image(p_applied.final_state);
        install_held_claim_image(p_applied.tenure_changed);
    }
    if (p_applied.op != 0) {
        settle_control(
            p_applied.op,
            entity::Control::Outcome(p_applied.outcome)
        );
    }
}

void NetwEntity::set_replication_priority(double p_weight) {
    NETW_ERR_COND(
        !(p_weight > 0.0),
        sys::WIRE,
        "NetwEntity.replication_priority must be above 0, so %f was "
        "ignored. To hide an entity from peers, use "
        "NetwEntity.interest.",
        p_weight
    );
    replication_priority = p_weight;
    control_moved();
}

bool NetwEntity::is_on_server() const {
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

NetwEntity::Ownership NetwEntity::get_ownership() const {
    return get_peer_id() != 0 ? OWNERSHIP_PEER : OWNERSHIP_SERVER;
}

bool NetwEntity::get_is_player() const {
    return get_peer_id() != 0;
}

bool NetwEntity::get_is_template() const {
    return get_stage() == int64_t(entity::Stage::TEMPLATE);
}

NetwEntity::Stage NetwEntity::get_stage() const {
    return Stage(record->get_stage());
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

void NetwEntity::abandon_claims() {
    reject_claims("'%s' was freed with a control request outstanding");
}

void NetwEntity::reject_claims(const char *p_detail) {
    LocalVector<ControlClaim> held;
    held.reserve(control_claims.size());
    for (uint32_t at = 0; at < control_claims.size(); ++at) {
        held.push_back(control_claims[at]);
    }
    control_claims.clear();
    control()->pending.clear();
    control_moved();
    for (uint32_t at = 0; at < held.size(); ++at) {
        held[at].promise->reject(
            ERR_UNAVAILABLE,
            vformat(p_detail, String(get_name()))
        );
    }
}

void NetwEntity::transition(int64_t p_stage) {
    const int64_t from = get_stage();
    record->transition(p_stage, get_owner());
    note_stage(from);
}

void NetwEntity::set_is_template(bool p_value) {
    if (!p_value) {
        NETW_ERR_COND(
            get_is_template(),
            sys::ENTITY,
            "NetwEntity.is_template cannot be cleared once it is set. Spawn "
            "a new entity from the template instead."
        );
        return;
    }
    const int64_t from = get_stage();
    record->mark_template(get_owner());
    note_stage(from);
}

void NetwEntity::arm(const Ref<NetwMultiplayer> &p_api) {
    NETW_ERR_COND(
        get_stage() != int64_t(entity::Stage::UNBOUND),
        sys::ENTITY,
        "An entity was set up for a session twice."
    );
    stamp_multiplayer(p_api);
    if (!control()->get_configured()) {
        adopt_controller(resolve_initial_controller());
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
    NETW_ZONE_NC("NetwEntity tree entered", colors::LIVENESS);
    owner_exiting_tree = false;
    synchronizers_absent = false;
    control_moved();
    Node *owner = get_owner();
    if (session_core() == nullptr) {
        stamp_multiplayer(session_on_branch(owner));
    }
    NetwMultiplayer::entity_enter_tree(
        this,
        owner,
        record,
        session_core_for(owner),
        is_on_server()
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
    int64_t structure = 0;
    {
        NETW_ZONE_NC("NetwEntity comp structure hash", colors::LIVENESS);
        structure = comp_structure_hash(table.sorted_paths());
    }
    table.set_table_hash(structure);
    if (table.reconcile(is_on_server())) {
        NETW_WARN(
            sys::ENTITY,
            "'%s' has different networked components here than on the "
            "server (%d against %d), so its components do not replicate. "
            "Build the entity with the same nodes on every peer.",
            String(get_name()),
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
    NETW_ZONE_NC("NetwEntity owner ready", colors::LIVENESS);
    if (steering() != entity::Control::SERVER) {
        apply_control();
    }
    {
        NETW_ZONE_NC("NetwEntity hydrate components", colors::LIVENESS);
        hydrate_components();
    }
    NetwMultiplayer *core = session_core();
    if (core != nullptr && core->predict_lacks_state_rows(this)) {
        Node *owner = get_owner();
        NETW_ERROR(
            sys::PREDICTION,
            "Prediction on %s is off, because no property is declared as "
            "state. Call NetwPropertyConfig.state on the properties it "
            "restores.",
            owner != nullptr ? String(owner->get_path()).utf8().get_data()
                             : String(get_name()).utf8().get_data()
        );
    }
    {
        NETW_ZONE_NC("NetwEntity persist and predict", colors::LIVENESS);
        if (core != nullptr && !core->persist_enroll(get_owner())) {
            core->predict_reconcile_declaration(this);
        }
    }
    if (core != nullptr) {
        NETW_ZONE_NC("NetwEntity sim declare", colors::LIVENESS);
        core->sim_declare(this);
    }
    {
        NETW_ZONE_NC("NetwEntity emit spawned", colors::LIVENESS);
        emit_signal(SIG_SPAWNED);
    }
}

void NetwEntity::_handle_tree_exiting() {
    owner_exiting_tree = true;
    control_moved();
    if (NetwMultiplayer *core = session_core()) {
        core->entity_capture_exit(this);
        return;
    }
    const int64_t from = get_stage();
    record->complete_departure(this);
    note_stage(from);
}

Node *NetwEntity::spawn_under(
    Node *p_parent,
    const StringName &p_id,
    const Callable &p_configure
) {
    NetwMultiplayer *core = session_core();
    if (!lifecycle_admits(
            this,
            core,
            lifecycle::Kind::SPAWN,
            "NetwEntity.spawn_under"
        )) {
        return nullptr;
    }
    if (core != nullptr) {
        return core
            ->entity_spawn_under(get_owner(), p_parent, p_id, p_configure);
    }
    return NetwMultiplayer::entity_spawn_copy_under(
        get_owner(),
        p_parent,
        p_id,
        p_configure
    );
}

void NetwEntity::despawn(const Ref<NetwDespawnOpts> &p_opts) {
    if (!lifecycle_admits(
            this,
            session_core(),
            lifecycle::Kind::DESPAWN,
            "NetwEntity.despawn"
        )) {
        return;
    }
    Ref<NetwDespawnOpts> opts = p_opts;
    if (opts.is_null()) {
        opts.instantiate();
    }
    NetwMultiplayer *api = session_core();
    if (api != nullptr && !api->is_host()) {
        api->entity_settle_captured(this);
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
    if (api != nullptr) {
        api->spawn_detach_before_despawn(this);
        api->lifecycle_send_despawn(this);
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
        "'%s' was configured after its entity spawned, so the spawn did not "
        "carry it. Configure networked properties in _init.",
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

PropertyBindingPtr NetwEntity::derived_binding(int64_t p_record) const {
    NetwMultiplayer *core = session_core();
    if (core == nullptr) {
        return PropertyBindingPtr();
    }
    return core->entity_record_binding(
        record,
        get_owner(),
        p_record,
        core->liveness_route_of(const_cast<NetwEntity *>(this))
    );
}

PropertyBindingPtr NetwEntity::get_state_binding() const {
    return derived_binding(repl::SET_RECORD_STATE);
}

PropertyBindingPtr NetwEntity::get_input_binding() const {
    return derived_binding(repl::SET_RECORD_INPUT);
}

PropertyBindingPtr NetwEntity::get_broadcast_binding() const {
    return derived_binding(repl::SET_RECORD_BROADCAST);
}

Ref<NetwInterest> NetwEntity::get_interest() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_INTEREST,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwInterest> NetwEntity::held_interest() const {
    return record->minted_part(NetwEntityRecord::PART_INTEREST);
}

Ref<NetwScene> NetwEntity::own_scene() {
    return record->part(NetwEntityRecord::PART_SCENE, this);
}

Ref<NetwScene> NetwEntity::get_scene() const {
    NetwEntity *self = const_cast<NetwEntity *>(this);
    NetwMultiplayer *core = session_core();
    if (core == nullptr || get_owner() == nullptr) {
        return self->own_scene();
    }
    return core->entity_scene_facet(record, self);
}

Ref<NetwPrediction> NetwEntity::get_prediction() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_PREDICTION,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwSimulation> NetwEntity::get_simulation() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_SIMULATION,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwPrediction> NetwEntity::prediction_if_minted() const {
    return record->minted_part(NetwEntityRecord::PART_PREDICTION);
}

Ref<NetwSimulation> NetwEntity::simulation_if_minted() const {
    return record->minted_part(NetwEntityRecord::PART_SIMULATION);
}

Ref<NetwPersistence> NetwEntity::get_persistence() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_PERSISTENCE,
        const_cast<NetwEntity *>(this)
    );
}

Ref<NetwDisplay> NetwEntity::get_display() const {
    return const_cast<NetwEntity *>(this)->record->part(
        NetwEntityRecord::PART_DISPLAY,
        const_cast<NetwEntity *>(this)
    );
}

TypedArray<MultiplayerSynchronizer> NetwEntity::synchronizers() {
    if (!synchronizers_dirty
        && (synchronizers_absent || !synchronizers_cache.is_empty())) {
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
    synchronizers_absent = synchronizers_cache.is_empty() && !owner_exiting_tree
        && owner->is_inside_tree();
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
    synchronizers_absent = false;
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
        D_METHOD("of", "node"),
        &NetwEntity::of
    );

    ClassDB::bind_method(D_METHOD("get_owner"), &NetwEntity::get_owner);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "owner",
            PROPERTY_HINT_NODE_TYPE,
            "Node",
            PROPERTY_USAGE_DEFAULT,
            "Node"
        ),
        "",
        "get_owner"
    );

    ClassDB::bind_method(D_METHOD("get_name"), &NetwEntity::get_name);
    ClassDB::bind_method(D_METHOD("set_name", "name"), &NetwEntity::set_name);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "name"),
        "set_name",
        "get_name"
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
        PropertyInfo(
            Variant::INT,
            "initial_controller",
            PROPERTY_HINT_ENUM,
            "Server,Represented Peer",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.InitialController"
        ),
        "set_initial_controller",
        "get_initial_controller"
    );

    ClassDB::bind_method(D_METHOD("get_transfer"), &NetwEntity::get_transfer);
    ClassDB::bind_method(
        D_METHOD("set_transfer", "value"),
        &NetwEntity::set_transfer
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "transfer",
            PROPERTY_HINT_ENUM,
            "Fixed,Requestable,Immediate",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.Transfer"
        ),
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
        PropertyInfo(
            Variant::INT,
            "on_controller_disconnect",
            PROPERTY_HINT_ENUM,
            "Revert To Server,Despawn",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.DisconnectRule"
        ),
        "set_on_controller_disconnect",
        "get_on_controller_disconnect"
    );

    ClassDB::bind_method(
        D_METHOD("get_on_parent_despawn"),
        &NetwEntity::get_on_parent_despawn
    );
    ClassDB::bind_method(
        D_METHOD("set_on_parent_despawn", "value"),
        &NetwEntity::set_on_parent_despawn
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "on_parent_despawn",
            PROPERTY_HINT_ENUM,
            "Cascade,Detach",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.ParentDespawnRule"
        ),
        "set_on_parent_despawn",
        "get_on_parent_despawn"
    );

    ClassDB::bind_method(D_METHOD("get_lifecycle"), &NetwEntity::get_lifecycle);
    ClassDB::bind_method(
        D_METHOD("set_lifecycle", "value"),
        &NetwEntity::set_lifecycle
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "lifecycle",
            PROPERTY_HINT_ENUM,
            "Session,Controller",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.Lifecycle"
        ),
        "set_lifecycle",
        "get_lifecycle"
    );

    ClassDB::bind_method(
        D_METHOD("get_is_scene"),
        &NetwEntity::get_declares_scene
    );
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_scene"), "", "get_is_scene");

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
            PROPERTY_HINT_ENUM,
            "Peer Controlled,Server Controlled",
            PROPERTY_USAGE_NONE | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.ControlKind"
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
    ClassDB::bind_method(
        D_METHOD("set_controller_player", "player"),
        &NetwEntity::set_controller_player
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "controller_player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer",
            PROPERTY_USAGE_NONE
        ),
        "set_controller_player",
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
        D_METHOD("claim_authority", "hold"),
        &NetwEntity::claim_authority,
        DEFVAL(entity::Control::HOLD_EXCLUSIVE)
    );
    ClassDB::bind_method(
        D_METHOD("release_authority", "successor"),
        &NetwEntity::release_authority,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("transfer_control", "peer_id"),
        &NetwEntity::transfer_control
    );
    ClassDB::bind_method(D_METHOD("get_hold"), &NetwEntity::get_hold);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "hold",
            PROPERTY_HINT_ENUM,
            "None,Yieldable,Exclusive",
            PROPERTY_USAGE_NONE | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.Hold"
        ),
        String(),
        "get_hold"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_control_pending"),
        &NetwEntity::get_is_control_pending
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::BOOL,
            "is_control_pending",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_is_control_pending"
    );
    ClassDB::bind_method(
        D_METHOD("follow_server", "path"),
        &NetwEntity::follow_server
    );

    ClassDB::bind_method(
        D_METHOD("set_replication_priority", "weight"),
        &NetwEntity::set_replication_priority
    );
    ClassDB::bind_method(
        D_METHOD("get_replication_priority"),
        &NetwEntity::get_replication_priority
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "replication_priority",
            PROPERTY_HINT_RANGE,
            "0.001,1000,0.001,or_greater"
        ),
        "set_replication_priority",
        "get_replication_priority"
    );

    ClassDB::bind_method(D_METHOD("get_player"), &NetwEntity::get_player);
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
            PROPERTY_HINT_ENUM,
            "Peer,Server",
            PROPERTY_USAGE_NONE | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.Ownership"
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
    ClassDB::bind_method(
        D_METHOD("set_is_template", "value"),
        &NetwEntity::set_is_template
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::BOOL,
            "is_template",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "set_is_template",
        "get_is_template"
    );

    ClassDB::bind_method(D_METHOD("get_stage"), &NetwEntity::get_stage);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "stage",
            PROPERTY_HINT_ENUM,
            "Unbound,Template,Armed,Live,Despawning,Lingering,Freed",
            PROPERTY_USAGE_NONE | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwEntity.Stage"
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

    ClassDB::bind_method(
        D_METHOD("spawn_under", "parent", "id", "configure"),
        &NetwEntity::spawn_under,
        DEFVAL((Node *)nullptr),
        DEFVAL(StringName()),
        DEFVAL(Callable())
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
    ClassDB::bind_method(D_METHOD("get_interest"), &NetwEntity::get_interest);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "interest",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwInterest",
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
            "NetwScene",
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
            "NetwPrediction",
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
            "NetwSimulation",
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
            "NetwPersistence",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_persistence"
    );
    ClassDB::bind_method(D_METHOD("get_display"), &NetwEntity::get_display);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "display",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwDisplay",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_display"
    );

    ClassDB::bind_method(D_METHOD("parent_entity"), &NetwEntity::parent_entity);

    ADD_SIGNAL(MethodInfo(SIG_SPAWNING));
    ADD_SIGNAL(MethodInfo(SIG_SPAWNED));
    ADD_SIGNAL(
        MethodInfo(SIG_DESPAWNING, PropertyInfo(Variant::STRING_NAME, "reason"))
    );
    ADD_SIGNAL(MethodInfo(SIG_DESPAWNED));
    ADD_SIGNAL(MethodInfo(SIG_HIDDEN));
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
        SIG_LIFECYCLE_REQUESTED,
        PropertyInfo(Variant::INT, "peer_id"),
        PropertyInfo(
            Variant::OBJECT,
            "request",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwLifecycleRequest"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_LIFECYCLE_REFUSED,
        PropertyInfo(
            Variant::OBJECT,
            "request",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwLifecycleRequest"
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
        "Transfer",
        "TRANSFER_IMMEDIATE",
        int(entity::Control::Transfer::IMMEDIATE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Hold",
        "HOLD_NONE",
        entity::Control::HOLD_NONE
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Hold",
        "HOLD_YIELDABLE",
        entity::Control::HOLD_YIELDABLE
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Hold",
        "HOLD_EXCLUSIVE",
        entity::Control::HOLD_EXCLUSIVE
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
        "ParentDespawnRule",
        "PARENT_DESPAWN_CASCADE",
        int(entity::Control::ParentDespawnRule::CASCADE)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "ParentDespawnRule",
        "PARENT_DESPAWN_DETACH",
        int(entity::Control::ParentDespawnRule::DETACH)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Lifecycle",
        "LIFECYCLE_SESSION",
        int(entity::Control::Lifecycle::SESSION)
    );
    ClassDB::bind_integer_constant(
        get_class_static(),
        "Lifecycle",
        "LIFECYCLE_CONTROLLER",
        int(entity::Control::Lifecycle::CONTROLLER)
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
