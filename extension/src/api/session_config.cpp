#include "netw/api/session_config.hpp"

#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

void NetwSessionConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_app_id", "app_id"),
        &NetwSessionConfig::set_app_id
    );
    ClassDB::bind_method(
        D_METHOD("get_app_id"),
        &NetwSessionConfig::get_app_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "app_id"),
        "set_app_id",
        "get_app_id"
    );

    ClassDB::bind_method(
        D_METHOD("set_desired_role", "desired_role"),
        &NetwSessionConfig::set_desired_role
    );
    ClassDB::bind_method(
        D_METHOD("get_desired_role"),
        &NetwSessionConfig::get_desired_role
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "desired_role",
            PROPERTY_HINT_ENUM,
            "None,Client,Dedicated Server,Listen Server",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSession.Role"
        ),
        "set_desired_role",
        "get_desired_role"
    );

    ClassDB::bind_method(
        D_METHOD("set_link_conditions", "link_conditions"),
        &NetwSessionConfig::set_link_conditions
    );
    ClassDB::bind_method(
        D_METHOD("get_link_conditions"),
        &NetwSessionConfig::get_link_conditions
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "link_conditions",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwLinkConditions"
        ),
        "set_link_conditions",
        "get_link_conditions"
    );

    ClassDB::bind_method(
        D_METHOD("set_server_info", "server_info"),
        &NetwSessionConfig::set_server_info
    );
    ClassDB::bind_method(
        D_METHOD("get_server_info"),
        &NetwSessionConfig::get_server_info
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "server_info",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwServerInfo"
        ),
        "set_server_info",
        "get_server_info"
    );

    ClassDB::bind_method(D_METHOD("app", "app_id"), &NetwSessionConfig::app);
    ClassDB::bind_method(
        D_METHOD("role", "desired_role"),
        &NetwSessionConfig::role
    );
    ClassDB::bind_method(
        D_METHOD("link", "link_conditions"),
        &NetwSessionConfig::link
    );
    ClassDB::bind_method(
        D_METHOD("server", "server_info"),
        &NetwSessionConfig::server
    );

    ClassDB::bind_method(
        D_METHOD("set_bandwidth_kbps", "bandwidth_kbps"),
        &NetwSessionConfig::set_bandwidth_kbps
    );
    ClassDB::bind_method(
        D_METHOD("get_bandwidth_kbps"),
        &NetwSessionConfig::get_bandwidth_kbps
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "bandwidth_kbps",
            PROPERTY_HINT_RANGE,
            "0,100000,1,or_greater,suffix:kbit/s"
        ),
        "set_bandwidth_kbps",
        "get_bandwidth_kbps"
    );
    ClassDB::bind_method(
        D_METHOD("bandwidth", "kbps"),
        &NetwSessionConfig::bandwidth
    );

    ClassDB::bind_method(
        D_METHOD("set_transfer_mode", "transfer_mode"),
        &NetwSessionConfig::set_transfer_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_transfer_mode"),
        &NetwSessionConfig::get_transfer_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "transfer_mode",
            PROPERTY_HINT_ENUM,
            "Rejoin,Continue",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSession.TransferMode"
        ),
        "set_transfer_mode",
        "get_transfer_mode"
    );
    ClassDB::bind_method(
        D_METHOD("transfer", "transfer_mode"),
        &NetwSessionConfig::transfer
    );

    ClassDB::bind_method(
        D_METHOD("set_transfer_deadline_ticks", "transfer_deadline_ticks"),
        &NetwSessionConfig::set_transfer_deadline_ticks
    );
    ClassDB::bind_method(
        D_METHOD("get_transfer_deadline_ticks"),
        &NetwSessionConfig::get_transfer_deadline_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "transfer_deadline_ticks",
            PROPERTY_HINT_RANGE,
            "0,3000,1,or_greater,suffix:ticks"
        ),
        "set_transfer_deadline_ticks",
        "get_transfer_deadline_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("transfer_deadline", "ticks"),
        &NetwSessionConfig::transfer_deadline
    );
}

void NetwSessionConfig::set_transfer_deadline_ticks(int64_t p_ticks) {
    NETW_ERR_COND(
        p_ticks < 0,
        sys::SESSION,
        "NetwSessionConfig.transfer_deadline_ticks got %d, which is "
        "negative. Pass ticks, or 0 to wait without a limit.",
        p_ticks
    );
    if (guard.takes("transfer_deadline_ticks")) {
        values.transfer_deadline_ticks = p_ticks;
    }
}

Ref<NetwSessionConfig> NetwSessionConfig::transfer_deadline(int64_t p_ticks) {
    set_transfer_deadline_ticks(p_ticks);
    return Ref<NetwSessionConfig>(this);
}

void NetwSessionConfig::set_transfer_mode(
    enums::NetwSession::TransferMode p_mode
) {
    if (guard.takes_enum("transfer_mode", int64_t(p_mode), 2)) {
        values.transfer_mode = p_mode;
    }
}

Ref<NetwSessionConfig> NetwSessionConfig::transfer(
    enums::NetwSession::TransferMode p_mode
) {
    set_transfer_mode(p_mode);
    return Ref<NetwSessionConfig>(this);
}

void NetwSessionConfig::set_bandwidth_kbps(int64_t p_kbps) {
    NETW_ERR_COND(
        p_kbps < 0,
        sys::SESSION,
        "NetwSessionConfig.bandwidth_kbps got %d, which is negative. Pass "
        "kbit/s, or 0 for one packet a tick.",
        p_kbps
    );
    if (guard.takes("bandwidth_kbps")) {
        values.bandwidth_kbps = p_kbps;
    }
}

Ref<NetwSessionConfig> NetwSessionConfig::bandwidth(int64_t p_kbps) {
    set_bandwidth_kbps(p_kbps);
    return Ref<NetwSessionConfig>(this);
}

void NetwSessionConfig::set_desired_role(NetwMultiplayer::Role p_role) {
    if (guard.takes_enum("desired_role", int64_t(p_role), 4)) {
        values.desired_role = p_role;
    }
}

Ref<NetwSessionConfig> NetwSessionConfig::app(const StringName &p_app_id) {
    set_app_id(p_app_id);
    return Ref<NetwSessionConfig>(this);
}

Ref<NetwSessionConfig> NetwSessionConfig::role(NetwMultiplayer::Role p_role) {
    set_desired_role(p_role);
    return Ref<NetwSessionConfig>(this);
}

Ref<NetwSessionConfig> NetwSessionConfig::link(
    const Ref<NetwLinkConditions> &p_conditions
) {
    set_link_conditions(p_conditions);
    return Ref<NetwSessionConfig>(this);
}

Ref<NetwSessionConfig> NetwSessionConfig::server(
    const Ref<NetwServerInfo> &p_info
) {
    set_server_info(p_info);
    return Ref<NetwSessionConfig>(this);
}

void NetwSessionConfig::copy_values_from(const NetwSessionConfig &p_source) {
    values.app_id = p_source.values.app_id;
    values.desired_role = p_source.values.desired_role;
    values.link_conditions = Ref<NetwLinkConditions>();
    if (p_source.values.link_conditions.is_valid()) {
        values.link_conditions.instantiate();
        values.link_conditions->copy_values_from(
            **p_source.values.link_conditions
        );
    }
    values.server_info = Ref<NetwServerInfo>();
    if (p_source.values.server_info.is_valid()) {
        values.server_info.instantiate();
        values.server_info->copy_values_from(**p_source.values.server_info);
    }
    values.bandwidth_kbps = p_source.values.bandwidth_kbps;
    values.transfer_mode = p_source.values.transfer_mode;
    values.transfer_deadline_ticks = p_source.values.transfer_deadline_ticks;
}

bool NetwSessionConfig::matches_defaults() const {
    return non_default_fields().is_empty();
}

String NetwSessionConfig::non_default_fields() const {
    String named;
    if (!String(values.app_id).is_empty()) {
        named += "app_id";
    }
    if (values.desired_role != DEFAULT_DESIRED_ROLE) {
        if (!named.is_empty()) {
            named += ", ";
        }
        named += "desired_role";
    }
    if (values.link_conditions.is_valid()
        && values.link_conditions->get_simulate_lag()) {
        if (!named.is_empty()) {
            named += ", ";
        }
        named += "link_conditions";
    }
    if (values.server_info.is_valid()) {
        if (!named.is_empty()) {
            named += ", ";
        }
        named += "server_info";
    }
    if (values.bandwidth_kbps != 0) {
        if (!named.is_empty()) {
            named += ", ";
        }
        named += "bandwidth_kbps";
    }
    if (values.transfer_mode != enums::NetwSession::TRANSFER_REJOIN) {
        if (!named.is_empty()) {
            named += ", ";
        }
        named += "transfer_mode";
    }
    if (values.transfer_deadline_ticks != DEFAULT_TRANSFER_DEADLINE_TICKS) {
        if (!named.is_empty()) {
            named += ", ";
        }
        named += "transfer_deadline_ticks";
    }
    return named;
}

} // namespace netw
