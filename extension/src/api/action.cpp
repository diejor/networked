#include "netw/api/action.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

namespace {

const StringName &sig_confirmed() {
    static const StringName name("confirmed");
    return name;
}

const StringName &sig_denied() {
    static const StringName name("denied");
    return name;
}

Node *live_node(int64_t p_id) {
    return Object::cast_to<Node>(gd::object_of(ObjectID(uint64_t(p_id))));
}

} // namespace

NetwMultiplayer *NetwActionContext::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwActionContext::open(
    NetwMultiplayer *p_session,
    int64_t p_requester,
    int64_t p_view_tick,
    int64_t p_requested_tick,
    int64_t p_execution_tick,
    const StringName &p_key
) {
    session_id = gd::instance_id(p_session);
    requester = p_requester;
    view_tick = p_view_tick;
    requested_tick = p_requested_tick;
    execution_tick = p_execution_tick;
    key = p_key;
}

void NetwActionContext::bind_result(Node *p_node) {
    NetwEntity::bind(p_node, key, 0);
    const Ref<NetwEntity> entity = NetwEntity::ensure(p_node);
    if (entity.is_valid()) {
        entity->set_action_spawn_tick(view_tick);
        entity->set_action_requester(requester);
    }
}

void NetwActionContext::deny() {
    if (denied) {
        return;
    }
    denied = true;
    if (NetwMultiplayer *core = session()) {
        core->lagcomp_deny_action(requester, key);
    }
}

void NetwActionContext::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_requester"),
        &NetwActionContext::get_requester
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "requester",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_requester"
    );
    ClassDB::bind_method(
        D_METHOD("get_view_tick"),
        &NetwActionContext::get_view_tick
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "view_tick",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_view_tick"
    );
    ClassDB::bind_method(
        D_METHOD("get_requested_tick"),
        &NetwActionContext::get_requested_tick
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "requested_tick",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_requested_tick"
    );
    ClassDB::bind_method(
        D_METHOD("get_execution_tick"),
        &NetwActionContext::get_execution_tick
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "execution_tick",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_execution_tick"
    );
    ClassDB::bind_method(
        D_METHOD("bind", "node"),
        &NetwActionContext::bind_result
    );
    ClassDB::bind_method(D_METHOD("deny"), &NetwActionContext::deny);
}

NetwMultiplayer *NetwAction::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

NodePath NetwAction::tree_relative_path(Node *p_target) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_target);
    Node *root = api != nullptr ? api->session_root() : nullptr;
    return root != nullptr ? root->get_path_to(p_target) : NodePath();
}

void NetwAction::open(
    NetwMultiplayer *p_session,
    const Callable &p_authority,
    int64_t p_slot
) {
    session_id = gd::instance_id(p_session);
    authority = p_authority;
    slot = p_slot;
    method = p_authority.get_method();
    Node *target = Object::cast_to<Node>(p_authority.get_object());
    if (target == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(target);
    entity_id = gd::instance_id(entity.ptr());
    if (target->is_inside_tree()) {
        target_path = tree_relative_path(target);
    }
}

void NetwAction::run_revert(int64_t p_ghost) {
    Node *ghost = live_node(p_ghost);
    if (ghost == nullptr) {
        return;
    }
    if (revert.is_valid()) {
        revert.call(ghost);
        return;
    }
    ghost->queue_free();
}

void NetwAction::run_confirm(int64_t p_ghost) {
    Node *ghost = live_node(p_ghost);
    if (ghost != nullptr) {
        if (confirm.is_valid()) {
            confirm.call(ghost);
        } else {
            ghost->queue_free();
        }
    }
    emit_signal(sig_confirmed());
}

void NetwAction::run_denied() {
    emit_signal(sig_denied());
}

void NetwAction::request(int64_t p_view_tick, const Variant &p_data) {
    const Ref<NetwEntity> entity
        = Object::cast_to<NetwEntity>(gd::object_of(entity_id));
    if (entity.is_null() || !entity->get_is_controlled_locally()) {
        return;
    }
    NetwMultiplayer *api = session();
    if (api == nullptr || !api->lagcomp_is_configured()) {
        return;
    }
    Node *target = Object::cast_to<Node>(authority.get_object());
    if (target != nullptr && target->is_inside_tree()) {
        target_path = tree_relative_path(target);
    }
    if (target_path.is_empty()) {
        return;
    }
    const StringName key
        = api->lagcomp_effect_key(entity->get_rid_handle(), p_view_tick, slot);
    Node *ghost = predict.is_valid()
        ? Object::cast_to<Node>(gd::live_object(predict.call()))
        : nullptr;
    const int64_t ghost_id = int64_t(uint64_t(gd::instance_id(ghost)));
    api->lagcomp_effect_arm(
        key,
        callable_mp(this, &NetwAction::run_revert).bind(ghost_id),
        int(timeout_ticks)
    );
    api->lagcomp_effect_watch(
        key,
        callable_mp(this, &NetwAction::run_confirm).bind(ghost_id),
        callable_mp(this, &NetwAction::run_denied)
    );
    api->action_send_request(
        target_path,
        method,
        p_view_tick,
        p_data,
        key,
        timing_mode
    );
}

void NetwAction::_bind_methods() {
    BIND_ENUM_CONSTANT(TIMING_TICK_ALIGNED);
    BIND_ENUM_CONSTANT(TIMING_TICK_ALIGNED_STATE_READY);
    BIND_ENUM_CONSTANT(TIMING_IMMEDIATE);

    ADD_SIGNAL(MethodInfo("confirmed"));
    ADD_SIGNAL(MethodInfo("denied"));

    ClassDB::bind_method(
        D_METHOD("set_predict", "predict"),
        &NetwAction::set_predict
    );
    ClassDB::bind_method(D_METHOD("get_predict"), &NetwAction::get_predict);
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "predict"),
        "set_predict",
        "get_predict"
    );
    ClassDB::bind_method(
        D_METHOD("set_revert", "revert"),
        &NetwAction::set_revert
    );
    ClassDB::bind_method(D_METHOD("get_revert"), &NetwAction::get_revert);
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "revert"),
        "set_revert",
        "get_revert"
    );
    ClassDB::bind_method(
        D_METHOD("set_confirm", "confirm"),
        &NetwAction::set_confirm
    );
    ClassDB::bind_method(D_METHOD("get_confirm"), &NetwAction::get_confirm);
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "confirm"),
        "set_confirm",
        "get_confirm"
    );
    ClassDB::bind_method(
        D_METHOD("set_timeout_ticks", "ticks"),
        &NetwAction::set_timeout_ticks
    );
    ClassDB::bind_method(
        D_METHOD("get_timeout_ticks"),
        &NetwAction::get_timeout_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "timeout_ticks"),
        "set_timeout_ticks",
        "get_timeout_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("set_timing_mode", "mode"),
        &NetwAction::set_timing_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_timing_mode"),
        &NetwAction::get_timing_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "timing_mode",
            PROPERTY_HINT_ENUM,
            "Tick Aligned,Tick Aligned State Ready,Immediate"
        ),
        "set_timing_mode",
        "get_timing_mode"
    );
    ClassDB::bind_method(
        D_METHOD("request", "view_tick", "data"),
        &NetwAction::request,
        DEFVAL(Variant())
    );
}

} // namespace netw
