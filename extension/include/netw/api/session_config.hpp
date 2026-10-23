#pragma once

#include <cstdint>

#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/api/config_draft.hpp"
#include "netw/api/enums.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class NetwSessionConfig : public godot::Resource {
    GDCLASS(NetwSessionConfig, godot::Resource)

    static constexpr int64_t DEFAULT_DESIRED_ROLE = 3;

public:
    static constexpr int64_t DEFAULT_TRANSFER_DEADLINE_TICKS = 300;

private:
    struct Values {
        godot::StringName app_id;
        enums::NetwSession::Role desired_role
            = enums::NetwSession::Role(DEFAULT_DESIRED_ROLE);
        godot::Ref<NetwLinkConditions> link_conditions;
        godot::Ref<NetwServerInfo> server_info;
        int64_t bandwidth_kbps = 0;
        enums::NetwSession::TransferMode transfer_mode
            = enums::NetwSession::TRANSFER_REJOIN;
        int64_t transfer_deadline_ticks = DEFAULT_TRANSFER_DEADLINE_TICKS;
        double return_window_seconds = 0.0;
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

    void set_desired_role(enums::NetwSession::Role p_role);
    enums::NetwSession::Role get_desired_role() const {
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

    void set_bandwidth_kbps(int64_t p_kbps);
    int64_t get_bandwidth_kbps() const {
        return values.bandwidth_kbps;
    }

    void set_transfer_mode(enums::NetwSession::TransferMode p_mode);
    enums::NetwSession::TransferMode get_transfer_mode() const {
        return values.transfer_mode;
    }

    void set_transfer_deadline_ticks(int64_t p_ticks);
    int64_t get_transfer_deadline_ticks() const {
        return values.transfer_deadline_ticks;
    }

    void set_return_window_seconds(double p_seconds);
    double get_return_window_seconds() const {
        return values.return_window_seconds;
    }

    godot::Ref<NetwSessionConfig> app(const godot::StringName &p_app_id);
    godot::Ref<NetwSessionConfig> role(enums::NetwSession::Role p_role);
    godot::Ref<NetwSessionConfig> link(
        const godot::Ref<NetwLinkConditions> &p_conditions
    );
    godot::Ref<NetwSessionConfig> server(
        const godot::Ref<NetwServerInfo> &p_info
    );
    godot::Ref<NetwSessionConfig> bandwidth(int64_t p_kbps);
    godot::Ref<NetwSessionConfig> transfer(
        enums::NetwSession::TransferMode p_mode
    );
    godot::Ref<NetwSessionConfig> transfer_deadline(int64_t p_ticks);
    godot::Ref<NetwSessionConfig> return_window(double p_seconds);

    void seal(const godot::String &p_scope) {
        guard.seal(p_scope);
    }
    bool matches_defaults() const;
    godot::String non_default_fields() const;
};

} // namespace netw
