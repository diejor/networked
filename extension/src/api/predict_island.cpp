#include "netw/api/predict_island.hpp"

#include <algorithm>

#include "godot/class_db.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

int NetwPredictIsland::index_of(const Ref<NetwEntity> &p_entity) const {
    for (uint32_t at = 0; at < roster.size(); ++at) {
        if (roster[at] == p_entity) {
            return int(at);
        }
    }
    return -1;
}

bool NetwPredictIsland::same_scene(const Ref<NetwEntity> &p_entity) const {
    if (p_entity.is_null()) {
        return false;
    }
    Object *held = ObjectDB::get_instance(owner);
    NetwEntity *bound = Object::cast_to<NetwEntity>(held);
    if (bound == nullptr) {
        return true;
    }
    const Ref<NetwSceneHandle> mine = bound->get_scene();
    const Ref<NetwSceneHandle> theirs = p_entity->get_scene();
    if (mine.is_null() || theirs.is_null()) {
        return mine.is_null() && theirs.is_null();
    }
    return mine->get_entity() == theirs->get_entity();
}

void NetwPredictIsland::take_ownership() {
    if (!from_scene) {
        return;
    }
    from_scene = false;
    layers.clear();
    roster.clear();
    roster_fidelity.clear();
    roster_predictor.clear();
    promotion_policy = NetwPredict::PROMOTION_NONE;
    promotion_budget = 0;
    promotion_radius = 0.0;
    pacing_policy = NetwPredict::PACING_SPECULATE;
    input_delay = 0;
}

void NetwPredictIsland::announce() {
    if (on_declaration_changed.is_valid()) {
        on_declaration_changed.call();
    }
}

void NetwPredictIsland::set_approximate(bool p_value) {
    approximate_declared = p_value;
    if (p_value) {
        exact_declared = false;
    }
    announce();
}

void NetwPredictIsland::set_exact_claim(bool p_value) {
    NETW_ERR_COND(
        p_value && !layers.is_empty(),
        sys::PREDICTION,
        "NetwPredictIsland.exact_claim: produced islands are always "
        "approximate."
    );
    exact_declared = p_value;
    if (p_value) {
        approximate_declared = false;
    }
    announce();
}

void NetwPredictIsland::set_promotion(int p_value) {
    promotion_policy = p_value;
}

void NetwPredictIsland::set_promotion_count(int p_value) {
    promotion_budget = p_value;
}

void NetwPredictIsland::set_promotion_meters(double p_value) {
    promotion_radius = p_value;
}

void NetwPredictIsland::set_pacing(int p_value) {
    pacing_policy = p_value;
}

void NetwPredictIsland::set_input_delay_ticks(int p_value) {
    input_delay = std::max(0, p_value);
}

void NetwPredictIsland::set_reconcile(int p_value) {
    reconcile_policy = p_value;
    announce();
}

void NetwPredictIsland::set_inherited(bool p_value) {
    from_scene = p_value;
}

TypedArray<NetwEntity> NetwPredictIsland::get_participants() const {
    TypedArray<NetwEntity> out;
    out.resize(int(roster.size()));
    for (uint32_t at = 0; at < roster.size(); ++at) {
        out[int(at)] = roster[at];
    }
    return out;
}

PackedStringArray NetwPredictIsland::get_producers() const {
    PackedStringArray out;
    for (uint32_t at = 0; at < layers.size(); ++at) {
        out.push_back(String(layers[at]));
    }
    return out;
}

void NetwPredictIsland::from_interest(const StringName &p_layer) {
    NETW_ERR_COND(
        exact_declared,
        sys::PREDICTION,
        "NetwPredictIsland.from_interest: produced islands cannot claim exact "
        "comparison."
    );
    take_ownership();
    bool held = false;
    for (uint32_t at = 0; at < layers.size(); ++at) {
        held = held || layers[at] == p_layer;
    }
    if (!held) {
        layers.push_back(p_layer);
    }
    set_approximate(true);
}

void NetwPredictIsland::add(const Ref<NetwEntity> &p_entity) {
    NETW_ERR_COND(
        !same_scene(p_entity),
        sys::PREDICTION,
        "NetwPredictIsland.add: islands cannot cross scene boundaries."
    );
    take_ownership();
    if (index_of(p_entity) >= 0) {
        return;
    }
    roster.push_back(p_entity);
    roster_fidelity.push_back(-1);
    roster_predictor.push_back(Callable());
    announce();
}

void NetwPredictIsland::remove(const Ref<NetwEntity> &p_entity) {
    const int at = index_of(p_entity);
    if (at < 0) {
        return;
    }
    roster.remove_at(uint32_t(at));
    roster_fidelity.remove_at(uint32_t(at));
    roster_predictor.remove_at(uint32_t(at));
    announce();
}

void NetwPredictIsland::simulate(
    const Ref<NetwEntity> &p_entity,
    const Callable &p_predict_commands
) {
    add(p_entity);
    const int at = index_of(p_entity);
    if (at >= 0) {
        roster_fidelity[uint32_t(at)] = NetwPredict::FIDELITY_SIMULATED;
    }
    if (p_predict_commands.is_valid()) {
        predict_commands(p_entity, p_predict_commands);
    }
}

void NetwPredictIsland::observe(const Ref<NetwEntity> &p_entity) {
    add(p_entity);
    const int at = index_of(p_entity);
    if (at >= 0) {
        roster_fidelity[uint32_t(at)] = NetwPredict::FIDELITY_PROXY;
    }
}

void NetwPredictIsland::predict_commands(
    const Ref<NetwEntity> &p_entity,
    const Callable &p_predictor
) {
    add(p_entity);
    NETW_ERR_COND(
        !p_predictor.is_valid(),
        sys::PREDICTION,
        "NetwPredictIsland.predict_commands: predictor must be a valid "
        "Callable."
    );
    const int at = index_of(p_entity);
    if (at >= 0) {
        roster_predictor[uint32_t(at)] = p_predictor;
    }
}

void NetwPredictIsland::simulate_nearest(int p_count) {
    promotion_policy = NetwPredict::PROMOTION_NEAREST;
    promotion_budget = std::max(0, p_count);
}

void NetwPredictIsland::simulate_within(double p_meters) {
    promotion_policy = NetwPredict::PROMOTION_WITHIN;
    promotion_radius = std::max(0.0, p_meters);
}

void NetwPredictIsland::simulate_all() {
    promotion_policy = NetwPredict::PROMOTION_ALL;
}

void NetwPredictIsland::set_fidelity(
    const Ref<NetwEntity> &p_entity,
    NetwPredict::Fidelity p_value
) {
    const int at = index_of(p_entity);
    if (at >= 0) {
        roster_fidelity[uint32_t(at)] = p_value;
    }
}

bool NetwPredictIsland::has_member(const Ref<NetwEntity> &p_entity) const {
    return index_of(p_entity) >= 0;
}

NetwPredict::Fidelity NetwPredictIsland::fidelity_of(
    const Ref<NetwEntity> &p_entity
) const {
    const int at = index_of(p_entity);
    return static_cast<NetwPredict::Fidelity>(
        at < 0 ? -1 : roster_fidelity[uint32_t(at)]
    );
}

Callable NetwPredictIsland::predictor_of(
    const Ref<NetwEntity> &p_entity
) const {
    const int at = index_of(p_entity);
    return at < 0 ? Callable() : roster_predictor[uint32_t(at)];
}

void NetwPredictIsland::bind_owner(const Ref<NetwEntity> &p_owner) {
    owner = p_owner.is_valid() ? p_owner->get_instance_id() : ObjectID();
}

void NetwPredictIsland::watch_declaration(const Callable &p_callable) {
    on_declaration_changed = p_callable;
}

Ref<NetwPredictIsland> NetwPredictIsland::inheritable() const {
    if (!get_declared()) {
        return Ref<NetwPredictIsland>();
    }
    Ref<NetwPredictIsland> copy = duplicate_rule();
    copy->from_scene = true;
    return copy;
}

Ref<NetwPredictIsland> NetwPredictIsland::duplicate_rule() const {
    Ref<NetwPredictIsland> copy;
    copy.instantiate();
    copy->layers = layers;
    copy->roster = roster;
    copy->roster_fidelity = roster_fidelity;
    copy->roster_predictor = roster_predictor;
    copy->promotion_policy = promotion_policy;
    copy->promotion_budget = promotion_budget;
    copy->promotion_radius = promotion_radius;
    copy->pacing_policy = pacing_policy;
    copy->input_delay = input_delay;
    copy->set_approximate(approximate_declared);
    copy->set_exact_claim(exact_declared);
    copy->reconcile_policy = reconcile_policy;
    copy->on_declaration_changed = Callable();
    return copy;
}

void NetwPredictIsland::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("from_interest", "layer"),
        &NetwPredictIsland::from_interest,
        DEFVAL(StringName())
    );
    ClassDB::bind_method(D_METHOD("add", "entity"), &NetwPredictIsland::add);
    ClassDB::bind_method(
        D_METHOD("remove", "entity"),
        &NetwPredictIsland::remove
    );
    ClassDB::bind_method(
        D_METHOD("simulate", "entity", "predict_commands"),
        &NetwPredictIsland::simulate,
        DEFVAL(Callable())
    );
    ClassDB::bind_method(
        D_METHOD("observe", "entity"),
        &NetwPredictIsland::observe
    );
    ClassDB::bind_method(
        D_METHOD("predict_commands", "entity", "predictor"),
        &NetwPredictIsland::predict_commands
    );
    ClassDB::bind_method(
        D_METHOD("simulate_nearest", "count"),
        &NetwPredictIsland::simulate_nearest
    );
    ClassDB::bind_method(
        D_METHOD("simulate_within", "meters"),
        &NetwPredictIsland::simulate_within
    );
    ClassDB::bind_method(
        D_METHOD("simulate_all"),
        &NetwPredictIsland::simulate_all
    );
    ClassDB::bind_method(
        D_METHOD("set_fidelity", "entity", "value"),
        &NetwPredictIsland::set_fidelity
    );
    ClassDB::bind_method(
        D_METHOD("has_member", "entity"),
        &NetwPredictIsland::has_member
    );
    ClassDB::bind_method(
        D_METHOD("fidelity_of", "entity"),
        &NetwPredictIsland::fidelity_of
    );
    ClassDB::bind_method(
        D_METHOD("predictor_of", "entity"),
        &NetwPredictIsland::predictor_of
    );
    ClassDB::bind_method(
        D_METHOD("bind_owner", "owner"),
        &NetwPredictIsland::bind_owner
    );
    ClassDB::bind_method(
        D_METHOD("watch_declaration", "callable"),
        &NetwPredictIsland::watch_declaration
    );
    ClassDB::bind_method(
        D_METHOD("inheritable"),
        &NetwPredictIsland::inheritable
    );
    ClassDB::bind_method(
        D_METHOD("duplicate_rule"),
        &NetwPredictIsland::duplicate_rule
    );

    ClassDB::bind_method(
        D_METHOD("get_participants"),
        &NetwPredictIsland::get_participants
    );
    ClassDB::bind_method(
        D_METHOD("get_producers"),
        &NetwPredictIsland::get_producers
    );
    ClassDB::bind_method(
        D_METHOD("get_declared"),
        &NetwPredictIsland::get_declared
    );

#define NETW_ISLAND_PROPERTY(m_type, m_name, m_getter, m_setter) \
    ClassDB::bind_method(D_METHOD(#m_getter), &NetwPredictIsland::m_getter); \
    ClassDB::bind_method( \
        D_METHOD(#m_setter, "value"), \
        &NetwPredictIsland::m_setter \
    ); \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name), #m_setter, #m_getter);

    NETW_ISLAND_PROPERTY(
        Variant::BOOL,
        approximate,
        get_approximate,
        set_approximate
    )
    NETW_ISLAND_PROPERTY(
        Variant::BOOL,
        exact_claim,
        get_exact_claim,
        set_exact_claim
    )
    NETW_ISLAND_PROPERTY(Variant::INT, promotion, get_promotion, set_promotion)
    NETW_ISLAND_PROPERTY(
        Variant::INT,
        promotion_count,
        get_promotion_count,
        set_promotion_count
    )
    NETW_ISLAND_PROPERTY(
        Variant::FLOAT,
        promotion_meters,
        get_promotion_meters,
        set_promotion_meters
    )
    NETW_ISLAND_PROPERTY(Variant::INT, pacing, get_pacing, set_pacing)
    NETW_ISLAND_PROPERTY(
        Variant::INT,
        input_delay_ticks,
        get_input_delay_ticks,
        set_input_delay_ticks
    )
    NETW_ISLAND_PROPERTY(Variant::INT, reconcile, get_reconcile, set_reconcile)
    NETW_ISLAND_PROPERTY(Variant::BOOL, inherited, get_inherited, set_inherited)

#undef NETW_ISLAND_PROPERTY

    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "participants",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        godot::String(),
        "get_participants"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "producers"),
        godot::String(),
        "get_producers"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "declared"),
        godot::String(),
        "get_declared"
    );
}

} // namespace netw
