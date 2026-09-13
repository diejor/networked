#pragma once

#include <cstdint>

#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/api/config_draft.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class NetwSessionConfig : public godot::Resource {
    GDCLASS(NetwSessionConfig, godot::Resource)

    static constexpr int64_t DEFAULT_DESIRED_ROLE = 3;

    struct Values {
        godot::StringName app_id;
        int64_t desired_role = DEFAULT_DESIRED_ROLE;
        godot::Ref<NetwLinkConditions> link_conditions;
        godot::Ref<NetwServerInfo> server_info;
    };

    Values values;
    config_draft::Guard guard{"configure_session"};

protected:
    static void _bind_methods();

public:
    void set_app_id(const godot::StringName &p_app_id) {
        if (guard.takes("app_id")) {
            values.app_id = p_app_id;
        }
    }
    godot::StringName get_app_id() const {
        return values.app_id;
    }

    void set_desired_role(int64_t p_role);
    int64_t get_desired_role() const {
        return values.desired_role;
    }

    void set_link_conditions(
        const godot::Ref<NetwLinkConditions> &p_conditions
    ) {
        if (guard.takes("link_conditions")) {
            values.link_conditions = p_conditions;
        }
    }
    godot::Ref<NetwLinkConditions> get_link_conditions() const {
        return values.link_conditions;
    }

    void set_server_info(const godot::Ref<NetwServerInfo> &p_info) {
        if (guard.takes("server_info")) {
            values.server_info = p_info;
        }
    }
    godot::Ref<NetwServerInfo> get_server_info() const {
        return values.server_info;
    }

    godot::Ref<NetwSessionConfig> app_id(const godot::StringName &p_app_id);
    godot::Ref<NetwSessionConfig> desired_role(int64_t p_role);
    godot::Ref<NetwSessionConfig> link_conditions(
        const godot::Ref<NetwLinkConditions> &p_conditions
    );
    godot::Ref<NetwSessionConfig> server_info(
        const godot::Ref<NetwServerInfo> &p_info
    );

    void seal(const godot::String &p_scope) {
        guard.seal(p_scope);
    }
    void copy_values_from(const NetwSessionConfig &p_source);
    bool matches_defaults() const;
    godot::String non_default_fields() const;
};

} // namespace netw
