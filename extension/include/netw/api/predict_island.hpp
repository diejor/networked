#pragma once

#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/predict.hpp"

namespace netw {

class NetwPredictIsland : public godot::RefCounted {
    GDCLASS(NetwPredictIsland, godot::RefCounted)

    godot::LocalVector<godot::StringName> layers;
    godot::LocalVector<godot::Ref<NetwEntity>> roster;
    godot::LocalVector<int> roster_fidelity;
    godot::LocalVector<godot::Callable> roster_predictor;

    bool approximate_declared = false;
    bool exact_declared = false;
    int promotion_policy = 0;
    int promotion_budget = 0;
    double promotion_radius = 0.0;
    int pacing_policy = 0;
    int input_delay = 0;
    int reconcile_policy = 0;
    bool from_scene = false;

    godot::ObjectID owner;
    godot::Callable on_declaration_changed;

    int index_of(const godot::Ref<NetwEntity> &p_entity) const;
    bool same_scene(const godot::Ref<NetwEntity> &p_entity) const;
    void take_ownership();
    void announce();

protected:
    static void _bind_methods();

public:
    void set_approximate(bool p_value);
    void set_exact_claim(bool p_value);
    void set_promotion(int p_value);
    void set_promotion_count(int p_value);
    void set_promotion_meters(double p_value);
    void set_pacing(int p_value);
    void set_input_delay_ticks(int p_value);
    void set_reconcile(int p_value);
    void set_inherited(bool p_value);

    bool get_approximate() const {
        return approximate_declared;
    }

    bool get_exact_claim() const {
        return exact_declared;
    }

    int get_promotion() const {
        return promotion_policy;
    }

    int get_promotion_count() const {
        return promotion_budget;
    }

    double get_promotion_meters() const {
        return promotion_radius;
    }

    int get_pacing() const {
        return pacing_policy;
    }

    int get_input_delay_ticks() const {
        return input_delay;
    }

    int get_reconcile() const {
        return reconcile_policy;
    }

    bool get_inherited() const {
        return from_scene;
    }

    bool get_declared() const {
        return !roster.is_empty() || !layers.is_empty();
    }

    godot::TypedArray<NetwEntity> get_participants() const;
    godot::PackedStringArray get_producers() const;

    void from_interest(const godot::StringName &p_layer = godot::StringName());
    void add(const godot::Ref<NetwEntity> &p_entity);
    void remove(const godot::Ref<NetwEntity> &p_entity);
    void simulate(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::Callable &p_predict_commands = godot::Callable()
    );
    void observe(const godot::Ref<NetwEntity> &p_entity);
    void predict_commands(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::Callable &p_predictor
    );
    void simulate_nearest(int p_count);
    void simulate_within(double p_meters);
    void simulate_all();

    void set_fidelity(
        const godot::Ref<NetwEntity> &p_entity,
        NetwPredict::Fidelity p_value
    );

    bool has_member(const godot::Ref<NetwEntity> &p_entity) const;
    NetwPredict::Fidelity fidelity_of(
        const godot::Ref<NetwEntity> &p_entity
    ) const;
    godot::Callable predictor_of(const godot::Ref<NetwEntity> &p_entity) const;

    void bind_owner(const godot::Ref<NetwEntity> &p_owner);
    void watch_declaration(const godot::Callable &p_callable);
    godot::Ref<NetwPredictIsland> inheritable() const;
    godot::Ref<NetwPredictIsland> duplicate_rule() const;
};

} // namespace netw
