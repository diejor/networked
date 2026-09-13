#include "netw/api/property_config.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "netw/log.hpp"
#include "netw/script/model.hpp"

using namespace godot;

namespace netw {

namespace {

Callable &carry_binder() {
    static Callable binder;
    return binder;
}

} // namespace

void NetwPropertyConfig::set_carry_binder(const Callable &p_binder) {
    carry_binder() = p_binder;
}

void NetwPropertyConfig::warn_double_set(
    bool p_already,
    const char *p_axis
) const {
    if (!p_already) {
        return;
    }
    NETW_WARN(
        sys::SESSION,
        "NetwPropertyConfig: the set-level %s of '%s' was already declared.",
        String(p_axis),
        String(get_context_name())
    );
}

Ref<NetwPropertyConfig> NetwPropertyConfig::state() {
    in_state_set = true;
    lane = NetwPropertySet::VOLATILE;
    set_trigger = NetwPropertySet::TRIGGER_ON_CHANGE;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::input() {
    in_input_set = true;
    lane = NetwPropertySet::VOLATILE;
    set_trigger = NetwPropertySet::TRIGGER_ON_CHANGE;
    set_audience = NetwPropertySet::AUDIENCE_SERVER_ONLY;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::broadcast() {
    in_broadcast_set = true;
    lane = NetwPropertySet::VOLATILE;
    set_trigger = NetwPropertySet::TRIGGER_ON_CHANGE;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::volatile_lane() {
    lane = NetwPropertySet::VOLATILE;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::retained() {
    lane = NetwPropertySet::RETAINED;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::causal() {
    property_class = NetwPropertySet::CAUSAL;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::derived() {
    property_class = NetwPropertySet::DERIVED;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::cosmetic() {
    property_class = NetwPropertySet::COSMETIC;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::converge(double p_stiffness) {
    converge_stiffness = p_stiffness;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::carry_along(
    const StringName &p_channel
) {
    carry_channel = p_channel;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::carry_step(const Callable &p_step) {
    Node *owner = Object::cast_to<Node>(p_step.get_object());
    if (!p_step.is_valid() || owner == nullptr) {
        NETW_ERROR(
            sys::SESSION,
            "NetwPropertyConfig.carry_step: '%s' needs a Callable bound to "
            "the Node it advances, so the rule belongs to one body rather "
            "than to every instance of its script.",
            String(get_context_name())
        );
        return Ref<NetwPropertyConfig>(this);
    }
    const Callable &binder = carry_binder();
    if (binder.is_valid()) {
        binder.call(owner, get_context_name(), p_step);
    } else {
        netw::script::model::bind_node_property_carry(
            owner,
            get_context_name(),
            p_step
        );
    }
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::teleport_only() {
    explicit_teleport_only = true;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::epsilon(double p_threshold) {
    epsilon_override = p_threshold;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::teleport_at(double p_distance) {
    teleport_at_override = p_distance;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::reconcile_only() {
    explicit_reconcile_only = true;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::every_tick(double p_interval) {
    warn_double_set(set_trigger != UNSET, "trigger");
    set_trigger = NetwPropertySet::TRIGGER_TICK;
    set_every_tick_interval = p_interval;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::on_change() {
    warn_double_set(set_trigger != UNSET, "trigger");
    set_trigger = NetwPropertySet::TRIGGER_ON_CHANGE;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::heartbeat(int64_t p_ticks) {
    warn_double_set(set_heartbeat_ticks != UNSET, "heartbeat");
    set_heartbeat_ticks = p_ticks;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::windowed(int64_t p_samples) {
    warn_double_set(set_window != UNSET, "window");
    set_window = p_samples;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::audience(bool p_server_only) {
    set_audience = p_server_only ? NetwPropertySet::AUDIENCE_SERVER_ONLY
                                 : NetwPropertySet::AUDIENCE_PUBLIC;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::masked() {
    set_masked = true;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::persisted(double p_interval) {
    is_persisted = true;
    persist_interval = p_interval;
    return Ref<NetwPropertyConfig>(this);
}

Ref<NetwPropertyConfig> NetwPropertyConfig::on_spawn() {
    is_spawn_state = true;
    return Ref<NetwPropertyConfig>(this);
}

void NetwPropertyConfig::_bind_methods() {
    ClassDB::bind_method(D_METHOD("state"), &NetwPropertyConfig::state);
    ClassDB::bind_method(D_METHOD("input"), &NetwPropertyConfig::input);
    ClassDB::bind_method(D_METHOD("broadcast"), &NetwPropertyConfig::broadcast);
    ClassDB::bind_method(
        D_METHOD("volatile"),
        &NetwPropertyConfig::volatile_lane
    );
    ClassDB::bind_method(D_METHOD("retained"), &NetwPropertyConfig::retained);
    ClassDB::bind_method(D_METHOD("causal"), &NetwPropertyConfig::causal);
    ClassDB::bind_method(D_METHOD("derived"), &NetwPropertyConfig::derived);
    ClassDB::bind_method(D_METHOD("cosmetic"), &NetwPropertyConfig::cosmetic);
    ClassDB::bind_method(
        D_METHOD("converge", "stiffness"),
        &NetwPropertyConfig::converge
    );
    ClassDB::bind_method(
        D_METHOD("carry_along", "channel"),
        &NetwPropertyConfig::carry_along
    );
    ClassDB::bind_method(
        D_METHOD("carry_step", "step"),
        &NetwPropertyConfig::carry_step
    );
    ClassDB::bind_method(
        D_METHOD("teleport_only"),
        &NetwPropertyConfig::teleport_only
    );
    ClassDB::bind_method(
        D_METHOD("epsilon", "threshold"),
        &NetwPropertyConfig::epsilon
    );
    ClassDB::bind_method(
        D_METHOD("teleport_at", "distance"),
        &NetwPropertyConfig::teleport_at
    );
    ClassDB::bind_method(
        D_METHOD("reconcile_only"),
        &NetwPropertyConfig::reconcile_only
    );
    ClassDB::bind_method(
        D_METHOD("every_tick", "interval"),
        &NetwPropertyConfig::every_tick,
        DEFVAL(0.0)
    );
    ClassDB::bind_method(D_METHOD("on_change"), &NetwPropertyConfig::on_change);
    ClassDB::bind_method(
        D_METHOD("heartbeat", "ticks"),
        &NetwPropertyConfig::heartbeat
    );
    ClassDB::bind_method(
        D_METHOD("windowed", "samples"),
        &NetwPropertyConfig::windowed
    );
    ClassDB::bind_method(
        D_METHOD("audience", "server_only"),
        &NetwPropertyConfig::audience,
        DEFVAL(true)
    );
    ClassDB::bind_method(D_METHOD("masked"), &NetwPropertyConfig::masked);
    ClassDB::bind_method(
        D_METHOD("persisted", "interval"),
        &NetwPropertyConfig::persisted,
        DEFVAL(0.0)
    );
    ClassDB::bind_method(D_METHOD("on_spawn"), &NetwPropertyConfig::on_spawn);

    ClassDB::bind_method(
        D_METHOD("set_lane", "lane"),
        &NetwPropertyConfig::set_lane
    );
    ClassDB::bind_method(D_METHOD("get_lane"), &NetwPropertyConfig::get_lane);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "lane",
            PROPERTY_HINT_ENUM,
            "Volatile,Retained"
        ),
        "set_lane",
        "get_lane"
    );

    ClassDB::bind_method(
        D_METHOD("set_set_audience", "set_audience"),
        &NetwPropertyConfig::set_set_audience
    );
    ClassDB::bind_method(
        D_METHOD("get_set_audience"),
        &NetwPropertyConfig::get_set_audience
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "set_audience",
            PROPERTY_HINT_ENUM,
            "Public,Server Only"
        ),
        "set_set_audience",
        "get_set_audience"
    );

    ClassDB::bind_method(
        D_METHOD("set_property_class", "property_class"),
        &NetwPropertyConfig::set_property_class
    );
    ClassDB::bind_method(
        D_METHOD("get_property_class"),
        &NetwPropertyConfig::get_property_class
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "property_class",
            PROPERTY_HINT_ENUM,
            "Causal,Derived,Cosmetic"
        ),
        "set_property_class",
        "get_property_class"
    );

#define NETW_PROPERTY_CONFIG_PROPERTY(m_type, m_name) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, #m_name), \
        &NetwPropertyConfig::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwPropertyConfig::get_##m_name \
    ); \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name), "set_" #m_name, "get_" #m_name)

    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, is_property);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, is_spawn_state);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, in_state_set);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, in_input_set);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, in_broadcast_set);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::FLOAT, epsilon_override);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::FLOAT, teleport_at_override);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::INT, set_trigger);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::FLOAT, set_every_tick_interval);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::INT, set_heartbeat_ticks);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::INT, set_window);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, set_masked);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, is_persisted);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::FLOAT, persist_interval);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::FLOAT, converge_stiffness);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::STRING_NAME, carry_channel);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, explicit_teleport_only);
    NETW_PROPERTY_CONFIG_PROPERTY(Variant::BOOL, explicit_reconcile_only);

#undef NETW_PROPERTY_CONFIG_PROPERTY

    BIND_CONSTANT(UNSET);
}

} // namespace netw
