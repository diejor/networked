#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/variant.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/property_set.hpp"

namespace netw {

class NetwPropertyConfig : public NetwMemberConfig {
    GDCLASS(NetwPropertyConfig, NetwMemberConfig)

public:
    static constexpr int64_t UNSET = -1;

private:
    bool is_property = true;
    bool is_spawn_state = false;
    NetwPropertySet::Lane lane = NetwPropertySet::VOLATILE;
    bool in_state_set = false;
    bool in_input_set = false;
    bool in_broadcast_set = false;
    double epsilon_override = -1.0;
    double teleport_at_override = -1.0;
    int64_t set_trigger = UNSET;
    double set_every_tick_interval = -1.0;
    int64_t set_heartbeat_ticks = UNSET;
    int64_t set_window = UNSET;
    NetwPropertySet::Audience set_audience = NetwPropertySet::AUDIENCE_PUBLIC;
    bool set_masked = false;
    bool is_persisted = false;
    double persist_interval = 0.0;
    NetwPropertySet::PropertyClass property_class = NetwPropertySet::CAUSAL;
    double converge_stiffness = 0.0;
    godot::StringName carry_channel;
    bool explicit_teleport_only = false;
    bool explicit_reconcile_only = false;

    void warn_double_set(bool p_already, const char *p_axis) const;

protected:
    static void _bind_methods();

public:
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

    void set_lane(NetwPropertySet::Lane p_lane) {
        lane = p_lane;
    }
    NetwPropertySet::Lane get_lane() const {
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

    void set_set_trigger(int64_t p_set_trigger) {
        set_trigger = p_set_trigger;
    }
    int64_t get_set_trigger() const {
        return set_trigger;
    }

    void set_set_every_tick_interval(double p_set_every_tick_interval) {
        set_every_tick_interval = p_set_every_tick_interval;
    }
    double get_set_every_tick_interval() const {
        return set_every_tick_interval;
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

    void set_set_audience(NetwPropertySet::Audience p_set_audience) {
        set_audience = p_set_audience;
    }
    NetwPropertySet::Audience get_set_audience() const {
        return set_audience;
    }

    void set_set_masked(bool p_set_masked) {
        set_masked = p_set_masked;
    }
    bool get_set_masked() const {
        return set_masked;
    }

    void set_is_persisted(bool p_is_persisted) {
        is_persisted = p_is_persisted;
    }
    bool get_is_persisted() const {
        return is_persisted;
    }

    void set_persist_interval(double p_persist_interval) {
        persist_interval = p_persist_interval;
    }
    double get_persist_interval() const {
        return persist_interval;
    }

    void set_property_class(NetwPropertySet::PropertyClass p_property_class) {
        property_class = p_property_class;
    }
    NetwPropertySet::PropertyClass get_property_class() const {
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
    godot::Ref<NetwPropertyConfig> every_tick(double p_interval);
    godot::Ref<NetwPropertyConfig> on_change();
    godot::Ref<NetwPropertyConfig> heartbeat(int64_t p_ticks);
    godot::Ref<NetwPropertyConfig> windowed(int64_t p_samples);
    godot::Ref<NetwPropertyConfig> audience(bool p_server_only);
    godot::Ref<NetwPropertyConfig> masked();
    godot::Ref<NetwPropertyConfig> persisted(double p_interval);
    godot::Ref<NetwPropertyConfig> on_spawn();
};

} // namespace netw
