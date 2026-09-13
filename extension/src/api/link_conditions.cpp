#include "netw/api/link_conditions.hpp"

#include <algorithm>

#include "godot/class_db.hpp"
#include "godot/net_peers.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {
constexpr double MSEC_TO_SEC = 0.001;
constexpr double PERCENT_TO_RATIO = 0.01;

enum class ShapingOverride { UNSET, ON, OFF };

ShapingOverride shaping_override_named(const String &p_value) {
    const String held = p_value.strip_edges().to_lower();
    if (held == "on" || held == "1" || held == "true" || held == "yes") {
        return ShapingOverride::ON;
    }
    if (held == "off" || held == "0" || held == "false" || held == "no") {
        return ShapingOverride::OFF;
    }
    return ShapingOverride::UNSET;
}

ShapingOverride requested_shaping() {
    OS *os = OS::get_singleton();
    if (os == nullptr) {
        return ShapingOverride::UNSET;
    }
    const ShapingOverride held
        = shaping_override_named(os->get_environment("NETW_SHAPING"));
    if (held != ShapingOverride::UNSET) {
        return held;
    }
    const String flag = "--netw-shaping=";
    const PackedStringArray args = gd::cmdline_args();
    for (int at = 0; at < args.size(); ++at) {
        const String arg = args[at];
        if (arg.begins_with(flag)) {
            return shaping_override_named(arg.substr(flag.length()));
        }
    }
    return ShapingOverride::UNSET;
}
} // namespace

bool link_shaping_allowed() {
    const ShapingOverride requested = requested_shaping();
    if (requested != ShapingOverride::UNSET) {
        return requested == ShapingOverride::ON;
    }
    OS *os = OS::get_singleton();
    return os != nullptr && os->has_feature("debug");
}

void NetwLinkConditions::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("wrap_peer", "base"),
        &NetwLinkConditions::wrap_peer
    );

    ClassDB::bind_method(
        D_METHOD("set_simulate_lag", "simulate_lag"),
        &NetwLinkConditions::set_simulate_lag
    );
    ClassDB::bind_method(
        D_METHOD("get_simulate_lag"),
        &NetwLinkConditions::get_simulate_lag
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "simulate_lag"),
        "set_simulate_lag",
        "get_simulate_lag"
    );

    ClassDB::bind_method(
        D_METHOD("set_one_way_delay_min", "one_way_delay_min"),
        &NetwLinkConditions::set_one_way_delay_min
    );
    ClassDB::bind_method(
        D_METHOD("get_one_way_delay_min"),
        &NetwLinkConditions::get_one_way_delay_min
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "one_way_delay_min"),
        "set_one_way_delay_min",
        "get_one_way_delay_min"
    );

    ClassDB::bind_method(
        D_METHOD("set_one_way_delay_max", "one_way_delay_max"),
        &NetwLinkConditions::set_one_way_delay_max
    );
    ClassDB::bind_method(
        D_METHOD("get_one_way_delay_max"),
        &NetwLinkConditions::get_one_way_delay_max
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "one_way_delay_max"),
        "set_one_way_delay_max",
        "get_one_way_delay_max"
    );

    ClassDB::bind_method(
        D_METHOD("set_lag_packet_loss_percent", "lag_packet_loss_percent"),
        &NetwLinkConditions::set_lag_packet_loss_percent
    );
    ClassDB::bind_method(
        D_METHOD("get_lag_packet_loss_percent"),
        &NetwLinkConditions::get_lag_packet_loss_percent
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "lag_packet_loss_percent",
            PROPERTY_HINT_RANGE,
            "0,25,0.01"
        ),
        "set_lag_packet_loss_percent",
        "get_lag_packet_loss_percent"
    );
}

Ref<MultiplayerPeer> NetwLinkConditions::wrap_peer(
    const Ref<MultiplayerPeer> &p_base
) const {
    if (p_base.is_null()) {
        return Ref<MultiplayerPeer>();
    }
    if (!simulate_lag) {
        return p_base;
    }
    if (!link_shaping_allowed()) {
        NETW_INFO(
            sys::TRANSPORT,
            "lag simulation is authored but this build does not shape its "
            "link, so the peer is installed unimpaired"
        );
        return p_base;
    }

    const StringName laggy_class("LaggyMultiplayerPeer");
    if (!ClassDB::class_exists(laggy_class)) {
        NETW_WARN(
            sys::TRANSPORT,
            "lag simulation is enabled but LaggyMultiplayerPeer is missing"
        );
        return p_base;
    }

    const double min_delay = std::max(0.0, one_way_delay_min);
    const double max_delay = std::max(min_delay, one_way_delay_max);
    const double packet_loss = std::clamp(lag_packet_loss_percent, 0.0, 100.0);

    Ref<MultiplayerPeer> laggy = gd::new_peer(laggy_class);
    if (laggy.is_null()) {
        return p_base;
    }
    if (!laggy->has_method(StringName("create"))) {
        NETW_WARN(
            sys::TRANSPORT,
            "the installed LaggyMultiplayerPeer publishes no create(base), "
            "so the peer is installed unimpaired"
        );
        return p_base;
    }
    const Variant wrapped_variant = laggy->call(StringName("create"), p_base);
    Object *wrapped_object = gd::live_object(wrapped_variant);
    MultiplayerPeer *wrapped = Object::cast_to<MultiplayerPeer>(wrapped_object);
    if (wrapped == nullptr) {
        return p_base;
    }

    Ref<MultiplayerPeer> result(wrapped);
    result->set(StringName("delay_minimum"), min_delay * MSEC_TO_SEC);
    result->set(StringName("delay_maximum"), max_delay * MSEC_TO_SEC);
    result->set(StringName("packet_loss"), packet_loss * PERCENT_TO_RATIO);
    return result;
}

void NetwLinkConditions::set_simulate_lag(bool p_simulate) {
    simulate_lag = p_simulate;
}

bool NetwLinkConditions::get_simulate_lag() const {
    return simulate_lag;
}

void NetwLinkConditions::set_one_way_delay_min(double p_ms) {
    one_way_delay_min = p_ms;
}

double NetwLinkConditions::get_one_way_delay_min() const {
    return one_way_delay_min;
}

void NetwLinkConditions::set_one_way_delay_max(double p_ms) {
    one_way_delay_max = p_ms;
}

double NetwLinkConditions::get_one_way_delay_max() const {
    return one_way_delay_max;
}

void NetwLinkConditions::set_lag_packet_loss_percent(double p_percent) {
    lag_packet_loss_percent = p_percent;
}

double NetwLinkConditions::get_lag_packet_loss_percent() const {
    return lag_packet_loss_percent;
}

void NetwLinkConditions::copy_values_from(const NetwLinkConditions &p_source) {
    simulate_lag = p_source.simulate_lag;
    one_way_delay_min = p_source.one_way_delay_min;
    one_way_delay_max = p_source.one_way_delay_max;
    lag_packet_loss_percent = p_source.lag_packet_loss_percent;
}

} // namespace netw
