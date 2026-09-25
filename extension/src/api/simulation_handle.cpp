#include "netw/api/simulation_handle.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/log.hpp"
#include "netw/sim/body.hpp"

using namespace godot;

namespace netw {

namespace {

const StringName &network_tick_name() {
    static const StringName name("_network_tick");
    return name;
}

String entity_name(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return String("<none>");
    }
    const String named = String(p_entity->get_entity_id());
    if (!named.is_empty()) {
        return named;
    }
    Node *owner = p_entity->get_owner();
    return owner != nullptr ? String(owner->get_name()) : String("<unnamed>");
}

} // namespace

void NetwSimulationHandle::bind(NetwEntity *p_entity) {
    entity_id = gd::instance_id(p_entity);
}

Ref<NetwEntity> NetwSimulationHandle::entity() const {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(gd::object_of(entity_id))
    );
}

NetwMultiplayer *NetwSimulationHandle::core() const {
    const Ref<NetwEntity> bound = entity();
    if (bound.is_null()) {
        return nullptr;
    }
    const Ref<MultiplayerAPI> stamped = bound->get_multiplayer();
    NetwMultiplayer *held = Object::cast_to<NetwMultiplayer>(stamped.ptr());
    return held != nullptr ? held
                           : NetwEntity::session_core_for(bound->get_owner());
}

RID NetwSimulationHandle::entity_rid() const {
    const Ref<NetwEntity> bound = entity();
    return bound.is_valid() ? bound->get_rid_handle() : RID();
}

bool NetwSimulationHandle::live() const {
    NetwMultiplayer *session = core();
    const RID rid = entity_rid();
    return session != nullptr && rid.is_valid()
        && session->entity_get_view(rid).is_valid();
}

void NetwSimulationHandle::declare() {
    declared_here = true;
    restate();
}

void NetwSimulationHandle::restate() {
    NetwMultiplayer *session = core();
    if (session != nullptr) {
        session->sim_declare(entity());
    }
    const Ref<NetwEntity> bound = entity();
    if (bound.is_valid()) {
        const Ref<NetwPredictionHandle> prediction
            = bound->prediction_if_minted();
        if (prediction.is_valid()) {
            prediction->restate_declaration();
        }
    }
}

void NetwSimulationHandle::announce(Mode p_mode) {
    if (p_mode == announced) {
        return;
    }
    const Mode previous = announced;
    announced = p_mode;
    emit_signal(StringName("mode_changed"), previous, p_mode);
}

TypedArray<NodePath> NetwSimulationHandle::get_bodies() const {
    TypedArray<NodePath> out;
    for (const NodePath &path : declared.bodies) {
        out.push_back(path);
    }
    return out;
}

void NetwSimulationHandle::set_bodies(const TypedArray<NodePath> &p_value) {
    declared.bodies.clear();
    for (int at = 0; at < p_value.size(); ++at) {
        declared.bodies.push_back(NodePath(p_value[at]));
    }
    NetwMultiplayer *session = core();
    const sim::Row *row
        = session != nullptr ? session->sim_row_of(entity_rid()) : nullptr;
    if (row != nullptr && row->bodies.recorded && !bodies_warned) {
        bodies_warned = true;
        NETW_WARN(
            sys::PREDICTION,
            "simulation.bodies on %s was written after the entity went live. "
            "The bodies it declared when it went live stay in effect.",
            entity_name(entity()).utf8().get_data()
        );
    }
    declare();
}

NetwSimulationHandle::Schedule NetwSimulationHandle::get_schedule() const {
    return Schedule(int(declared.schedule));
}

void NetwSimulationHandle::set_schedule(Schedule p_value) {
    declared.schedule = sim::Schedule(int(p_value));
    declare();
}

NetwSimulationHandle::Replicas NetwSimulationHandle::get_replicas() const {
    return Replicas(int(declared.replicas));
}

void NetwSimulationHandle::set_replicas(Replicas p_value) {
    declared.replicas = sim::Replicas(int(p_value));
    declare();
}

NetwSimulationHandle::Restore NetwSimulationHandle::get_restore() const {
    return Restore(int(declared.restore));
}

void NetwSimulationHandle::set_restore(Restore p_value) {
    declared.restore = sim::Restore(int(p_value));
    declare();
}

int NetwSimulationHandle::get_max_restore_ticks() const {
    return declared.max_restore_ticks;
}

void NetwSimulationHandle::set_max_restore_ticks(int p_value) {
    declared.max_restore_ticks = p_value;
    declare();
}

Callable NetwSimulationHandle::get_step() const {
    if (declared.step.is_valid()) {
        return declared.step;
    }
    const Ref<NetwEntity> bound = entity();
    Node *owner = bound.is_valid() ? bound->get_owner() : nullptr;
    if (owner != nullptr && owner->has_method(network_tick_name())) {
        return Callable(owner, network_tick_name());
    }
    return Callable();
}

void NetwSimulationHandle::set_step(const Callable &p_value) {
    declared.step = p_value;
    declare();
    adopt_step(p_value);
}

void NetwSimulationHandle::preset(
    Schedule p_schedule,
    bool p_restores,
    Restore p_restore
) {
    declared.schedule = sim::Schedule(int(p_schedule));
    if (p_restores) {
        declared.restore = sim::Restore(int(p_restore));
    }
    restate();
}

void NetwSimulationHandle::adopt_step(const Callable &p_value) {
    declared.step = p_value;
    const Ref<NetwEntity> bound = entity();
    if (bound.is_valid()) {
        const Ref<NetwPredictionHandle> prediction
            = bound->prediction_if_minted();
        if (prediction.is_valid()) {
            prediction->step_changed();
        }
    }
}

bool NetwSimulationHandle::get_claim_on_contact() const {
    return declared.claim_on_contact;
}

void NetwSimulationHandle::set_claim_on_contact(bool p_value) {
    declared.claim_on_contact = p_value;
    declare();
}

double NetwSimulationHandle::get_release_on_rest() const {
    return declared.release_on_rest;
}

void NetwSimulationHandle::set_release_on_rest(double p_value) {
    declared.release_on_rest = MAX(p_value, 0.0);
    declare();
}

NetwSimulationHandle::Mode NetwSimulationHandle::get_mode() const {
    NetwMultiplayer *session = core();
    const sim::Row *row
        = session != nullptr ? session->sim_row_of(entity_rid()) : nullptr;
    return row != nullptr ? Mode(int(row->mode)) : MODE_NONE;
}

TypedArray<NetwEntity> NetwSimulationHandle::get_selected() const {
    NetwMultiplayer *session = core();
    return session != nullptr ? session->sim_selected(entity_rid())
                              : TypedArray<NetwEntity>();
}

void NetwSimulationHandle::simulate(const Ref<NetwEntity> &p_entity) {
    NETW_ERR_COND(
        p_entity.is_null(),
        sys::PREDICTION,
        "simulation.simulate on %s names no entity.",
        entity_name(entity()).utf8().get_data()
    );
    if (live() && p_entity->get_owner() != nullptr
        && core()->sim_simulate(entity_rid(), p_entity) != OK) {
        return;
    }
    sim::choose(choice, p_entity->get_rid_handle(), sim::Pick::CHOSEN);
    declare();
}

void NetwSimulationHandle::forget(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    sim::forget(choice, p_entity->get_rid_handle());
    declare();
}

void NetwSimulationHandle::simulate_nearest(
    int p_count,
    const StringName &p_layer
) {
    sim::simulate_nearest(choice, p_count, p_layer);
    declare();
}

void NetwSimulationHandle::simulate_within(
    double p_meters,
    const StringName &p_layer
) {
    sim::simulate_within(choice, p_meters, p_layer);
    declare();
}

void NetwSimulationHandle::simulate_all(const StringName &p_layer) {
    sim::simulate_all(choice, p_layer);
    declare();
}

void NetwSimulationHandle::simulate_none() {
    sim::simulate_none(choice);
    declare();
}

NetwSimulationHandle::Schedule NetwSimulationHandle::resolved_schedule() const {
    if (declared.schedule != sim::Schedule::AUTO) {
        return get_schedule();
    }
    const Ref<NetwEntity> bound = entity();
    Node *owner = bound.is_valid() ? bound->get_owner() : nullptr;
    const bool has_body = !declared.bodies.is_empty()
        || (owner != nullptr && sim::is_solver_body(owner));
    return has_body ? SCHEDULE_FRAME : SCHEDULE_TICK;
}

void NetwSimulationHandle::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_bodies"),
        &NetwSimulationHandle::get_bodies
    );
    ClassDB::bind_method(
        D_METHOD("set_bodies", "value"),
        &NetwSimulationHandle::set_bodies
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "bodies",
            PROPERTY_HINT_ARRAY_TYPE,
            "NodePath"
        ),
        "set_bodies",
        "get_bodies"
    );
    ClassDB::bind_method(
        D_METHOD("get_schedule"),
        &NetwSimulationHandle::get_schedule
    );
    ClassDB::bind_method(
        D_METHOD("set_schedule", "value"),
        &NetwSimulationHandle::set_schedule
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "schedule",
            PROPERTY_HINT_ENUM,
            "Tick,Frame,Stepped,Auto",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSimulationHandle.Schedule"
        ),
        "set_schedule",
        "get_schedule"
    );
    ClassDB::bind_method(
        D_METHOD("get_replicas"),
        &NetwSimulationHandle::get_replicas
    );
    ClassDB::bind_method(
        D_METHOD("set_replicas", "value"),
        &NetwSimulationHandle::set_replicas
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "replicas",
            PROPERTY_HINT_ENUM,
            "Proxy,Active",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSimulationHandle.Replicas"
        ),
        "set_replicas",
        "get_replicas"
    );
    ClassDB::bind_method(
        D_METHOD("get_restore"),
        &NetwSimulationHandle::get_restore
    );
    ClassDB::bind_method(
        D_METHOD("set_restore", "value"),
        &NetwSimulationHandle::set_restore
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "restore",
            PROPERTY_HINT_ENUM,
            "Exact,Extrapolated,Buffered",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSimulationHandle.Restore"
        ),
        "set_restore",
        "get_restore"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_restore_ticks"),
        &NetwSimulationHandle::get_max_restore_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_max_restore_ticks", "value"),
        &NetwSimulationHandle::set_max_restore_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_restore_ticks"),
        "set_max_restore_ticks",
        "get_max_restore_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_claim_on_contact"),
        &NetwSimulationHandle::get_claim_on_contact
    );
    ClassDB::bind_method(
        D_METHOD("set_claim_on_contact", "value"),
        &NetwSimulationHandle::set_claim_on_contact
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "claim_on_contact"),
        "set_claim_on_contact",
        "get_claim_on_contact"
    );
    ClassDB::bind_method(
        D_METHOD("get_release_on_rest"),
        &NetwSimulationHandle::get_release_on_rest
    );
    ClassDB::bind_method(
        D_METHOD("set_release_on_rest", "value"),
        &NetwSimulationHandle::set_release_on_rest
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "release_on_rest"),
        "set_release_on_rest",
        "get_release_on_rest"
    );
    ClassDB::bind_method(
        D_METHOD("get_step"),
        &NetwSimulationHandle::get_step
    );
    ClassDB::bind_method(
        D_METHOD("set_step", "value"),
        &NetwSimulationHandle::set_step
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "step"),
        "set_step",
        "get_step"
    );
    ClassDB::bind_method(
        D_METHOD("get_mode"),
        &NetwSimulationHandle::get_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "mode",
            PROPERTY_HINT_ENUM,
            "None,Authority,Predict,Active,Proxy",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSimulationHandle.Mode"
        ),
        String(),
        "get_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_selected"),
        &NetwSimulationHandle::get_selected
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "selected",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        String(),
        "get_selected"
    );

    ClassDB::bind_method(
        D_METHOD("simulate", "entity"),
        &NetwSimulationHandle::simulate
    );
    ClassDB::bind_method(
        D_METHOD("forget", "entity"),
        &NetwSimulationHandle::forget
    );
    ClassDB::bind_method(
        D_METHOD("simulate_nearest", "count", "layer"),
        &NetwSimulationHandle::simulate_nearest,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("simulate_within", "meters", "layer"),
        &NetwSimulationHandle::simulate_within,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("simulate_all", "layer"),
        &NetwSimulationHandle::simulate_all,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(
        D_METHOD("simulate_none"),
        &NetwSimulationHandle::simulate_none
    );

    ADD_SIGNAL(MethodInfo(
        "mode_changed",
        PropertyInfo(
            Variant::INT,
            "previous",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSimulationHandle.Mode"
        ),
        PropertyInfo(
            Variant::INT,
            "mode",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSimulationHandle.Mode"
        )
    ));

    BIND_ENUM_CONSTANT(MODE_NONE);
    BIND_ENUM_CONSTANT(MODE_AUTHORITY);
    BIND_ENUM_CONSTANT(MODE_PREDICT);
    BIND_ENUM_CONSTANT(MODE_ACTIVE);
    BIND_ENUM_CONSTANT(MODE_PROXY);
    BIND_ENUM_CONSTANT(SCHEDULE_TICK);
    BIND_ENUM_CONSTANT(SCHEDULE_FRAME);
    BIND_ENUM_CONSTANT(SCHEDULE_STEPPED);
    BIND_ENUM_CONSTANT(SCHEDULE_AUTO);
    BIND_ENUM_CONSTANT(REPLICAS_PROXY);
    BIND_ENUM_CONSTANT(REPLICAS_ACTIVE);
    BIND_ENUM_CONSTANT(RESTORE_EXACT);
    BIND_ENUM_CONSTANT(RESTORE_EXTRAPOLATED);
    BIND_ENUM_CONSTANT(RESTORE_BUFFERED);
}

Ref<NetwSimulationHandle> build_simulation_handle(Object *p_entity) {
    Ref<NetwSimulationHandle> made;
    made.instantiate();
    made->bind(Object::cast_to<NetwEntity>(p_entity));
    return made;
}

} // namespace netw
