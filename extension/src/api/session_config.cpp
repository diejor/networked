#include "netw/api/session_config.hpp"

#include "godot/class_db.hpp"

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
            "None,Client,Dedicated Server,Listen Server"
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

    ClassDB::bind_method(
        D_METHOD("app_id", "app_id"),
        &NetwSessionConfig::app_id
    );
    ClassDB::bind_method(
        D_METHOD("desired_role", "desired_role"),
        &NetwSessionConfig::desired_role
    );
    ClassDB::bind_method(
        D_METHOD("link_conditions", "link_conditions"),
        &NetwSessionConfig::link_conditions
    );
    ClassDB::bind_method(
        D_METHOD("server_info", "server_info"),
        &NetwSessionConfig::server_info
    );
}

void NetwSessionConfig::set_desired_role(int64_t p_role) {
    if (guard.takes_enum("desired_role", p_role, 4)) {
        values.desired_role = p_role;
    }
}

Ref<NetwSessionConfig> NetwSessionConfig::app_id(const StringName &p_app_id) {
    set_app_id(p_app_id);
    return Ref<NetwSessionConfig>(this);
}

Ref<NetwSessionConfig> NetwSessionConfig::desired_role(int64_t p_role) {
    set_desired_role(p_role);
    return Ref<NetwSessionConfig>(this);
}

Ref<NetwSessionConfig> NetwSessionConfig::link_conditions(
    const Ref<NetwLinkConditions> &p_conditions
) {
    set_link_conditions(p_conditions);
    return Ref<NetwSessionConfig>(this);
}

Ref<NetwSessionConfig> NetwSessionConfig::server_info(
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
    return named;
}

} // namespace netw
