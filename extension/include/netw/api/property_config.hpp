#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/variant.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/repl/property_set.hpp"

namespace netw {

class NetwPropertyConfig : public NetwMemberConfig {
    GDCLASS(NetwPropertyConfig, NetwMemberConfig)

public:
    using Trigger = repl::PropertySet::Trigger;
    using Lane = repl::PropertySet::Lane;
    using PropertyClass = repl::PropertySet::PropertyClass;
    using Audience = repl::PropertySet::Audience;

    static constexpr Trigger TRIGGER_TICK = repl::PropertySet::TRIGGER_TICK;
    static constexpr Trigger TRIGGER_ON_CHANGE
        = repl::PropertySet::TRIGGER_ON_CHANGE;
    static constexpr Trigger TRIGGER_ON_DEMAND
        = repl::PropertySet::TRIGGER_ON_DEMAND;
    static constexpr Lane VOLATILE = repl::PropertySet::VOLATILE;
    static constexpr Lane RETAINED = repl::PropertySet::RETAINED;
    static constexpr PropertyClass CAUSAL = repl::PropertySet::CAUSAL;
    static constexpr PropertyClass DERIVED = repl::PropertySet::DERIVED;
    static constexpr PropertyClass COSMETIC = repl::PropertySet::COSMETIC;
    static constexpr Audience AUDIENCE_PUBLIC
        = repl::PropertySet::AUDIENCE_PUBLIC;
    static constexpr Audience AUDIENCE_SERVER_ONLY
        = repl::PropertySet::AUDIENCE_SERVER_ONLY;
    static constexpr int64_t UNSET = -1;

private:
    int declared_type = -1;
    bool is_property = true;
    bool is_spawn_state = false;
    repl::PropertySet::Lane lane = repl::PropertySet::VOLATILE;
    bool in_state_set = false;
    bool in_input_set = false;
    bool in_broadcast_set = false;
    double epsilon_override = -1.0;
    double teleport_at_override = -1.0;
    int64_t set_trigger = UNSET;
    int64_t set_heartbeat_ticks = UNSET;
    int64_t set_window = UNSET;
    repl::PropertySet::Audience set_audience
        = repl::PropertySet::AUDIENCE_PUBLIC;
    bool set_masked = false;
    godot::StringName saved_column;
    bool persist = false;
    repl::PropertySet::PropertyClass property_class = repl::PropertySet::CAUSAL;
    double converge_stiffness = 0.0;
    godot::StringName carry_channel;
    bool explicit_teleport_only = false;
    bool explicit_reconcile_only = false;

    void warn_double_set(bool p_replaces, const char *p_axis) const;

protected:
    static void _bind_methods();

public:
    int get_declared_type() const {
        return declared_type;
    }
    void set_declared_type(int p_type) {
        declared_type = p_type;
    }

    static void set_carry_binder(const godot::Callable &p_binder);

    void set_is_property(bool p_is_property) {
        is_property = p_is_property;
    }
    bool get_is_property() const {
        return is_property;
    }

    void set_is_spawn_state(bool p_is_spawn_state) {
        is_spawn_state = p_is_spawn_state;
    }
    bool get_is_spawn_state() const {
        return is_spawn_state;
    }

    void set_lane(repl::PropertySet::Lane p_lane) {
        lane = p_lane;
    }
    repl::PropertySet::Lane get_lane() const {
        return lane;
    }

    void set_in_state_set(bool p_in_state_set) {
        in_state_set = p_in_state_set;
    }
    bool get_in_state_set() const {
        return in_state_set;
    }

    void set_in_input_set(bool p_in_input_set) {
        in_input_set = p_in_input_set;
    }
    bool get_in_input_set() const {
        return in_input_set;
    }

    void set_in_broadcast_set(bool p_in_broadcast_set) {
        in_broadcast_set = p_in_broadcast_set;
    }
    bool get_in_broadcast_set() const {
        return in_broadcast_set;
    }

    void set_epsilon_override(double p_epsilon_override) {
        epsilon_override = p_epsilon_override;
    }
    double get_epsilon_override() const {
        return epsilon_override;
    }

    void set_teleport_at_override(double p_teleport_at_override) {
        teleport_at_override = p_teleport_at_override;
    }
    double get_teleport_at_override() const {
        return teleport_at_override;
    }

    void set_set_trigger(repl::PropertySet::Trigger p_set_trigger) {
        set_trigger = int64_t(p_set_trigger);
    }
    repl::PropertySet::Trigger get_set_trigger() const {
        return repl::PropertySet::Trigger(set_trigger);
    }

    void set_set_heartbeat_ticks(int64_t p_set_heartbeat_ticks) {
        set_heartbeat_ticks = p_set_heartbeat_ticks;
    }
    int64_t get_set_heartbeat_ticks() const {
        return set_heartbeat_ticks;
    }

    void set_set_window(int64_t p_set_window) {
        set_window = p_set_window;
    }
    int64_t get_set_window() const {
        return set_window;
    }

    void set_set_audience(repl::PropertySet::Audience p_set_audience) {
        set_audience = p_set_audience;
    }
    repl::PropertySet::Audience get_set_audience() const {
        return set_audience;
    }

    void set_set_masked(bool p_set_masked) {
        set_masked = p_set_masked;
    }
    bool get_set_masked() const {
        return set_masked;
    }

    bool is_persisted() const {
        return persist;
    }
    godot::StringName get_saved_column() const {
        return saved_column;
    }

    void set_property_class(repl::PropertySet::PropertyClass p_property_class) {
        property_class = p_property_class;
    }
    repl::PropertySet::PropertyClass get_property_class() const {
        return property_class;
    }

    void set_converge_stiffness(double p_converge_stiffness) {
        converge_stiffness = p_converge_stiffness;
    }
    double get_converge_stiffness() const {
        return converge_stiffness;
    }

    void set_carry_channel(const godot::StringName &p_carry_channel) {
        carry_channel = p_carry_channel;
    }
    godot::StringName get_carry_channel() const {
        return carry_channel;
    }

    void set_explicit_teleport_only(bool p_explicit_teleport_only) {
        explicit_teleport_only = p_explicit_teleport_only;
    }
    bool get_explicit_teleport_only() const {
        return explicit_teleport_only;
    }

    void set_explicit_reconcile_only(bool p_explicit_reconcile_only) {
        explicit_reconcile_only = p_explicit_reconcile_only;
    }
    bool get_explicit_reconcile_only() const {
        return explicit_reconcile_only;
    }

    godot::Ref<NetwPropertyConfig> state();
    godot::Ref<NetwPropertyConfig> input();
    godot::Ref<NetwPropertyConfig> broadcast();
    godot::Ref<NetwPropertyConfig> volatile_lane();
    godot::Ref<NetwPropertyConfig> retained();
    godot::Ref<NetwPropertyConfig> causal();
    godot::Ref<NetwPropertyConfig> derived();
    godot::Ref<NetwPropertyConfig> cosmetic();
    godot::Ref<NetwPropertyConfig> converge(double p_stiffness);
    godot::Ref<NetwPropertyConfig> carry_along(
        const godot::StringName &p_channel
    );
    godot::Ref<NetwPropertyConfig> carry_step(const godot::Callable &p_step);
    godot::Ref<NetwPropertyConfig> teleport_only();
    godot::Ref<NetwPropertyConfig> epsilon(double p_threshold);
    godot::Ref<NetwPropertyConfig> teleport_at(double p_distance);
    godot::Ref<NetwPropertyConfig> reconcile_only();
    godot::Ref<NetwPropertyConfig> on_change();
    godot::Ref<NetwPropertyConfig> heartbeat(int64_t p_ticks);
    godot::Ref<NetwPropertyConfig> windowed(int64_t p_samples);
    godot::Ref<NetwPropertyConfig> audience(bool p_server_only);
    godot::Ref<NetwPropertyConfig> masked();
    godot::Ref<NetwPropertyConfig> persisted(
        const godot::StringName &p_saved_column_name
    );
    godot::Ref<NetwPropertyConfig> on_spawn();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPropertyConfig::Trigger);
VARIANT_ENUM_CAST(netw::NetwPropertyConfig::Lane);
VARIANT_ENUM_CAST(netw::NetwPropertyConfig::PropertyClass);
VARIANT_ENUM_CAST(netw::NetwPropertyConfig::Audience);
