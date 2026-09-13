#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/debug_join_config.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_config.hpp"

namespace netw {

class MultiplayerTree : public godot::Node {
    GDCLASS(MultiplayerTree, godot::Node)

    godot::StringName peer_class;
    godot::Dictionary transport_settings;
    godot::Ref<NetwLinkConditions> link_conditions;
    bool auto_host_headless = true;
    NetwMultiplayer::Role desired_role = NetwMultiplayer::ROLE_LISTEN_SERVER;
    godot::StringName app_id;
    godot::Ref<DebugJoinConfig> debug_join;
    godot::Ref<godot::Script> api_script;

    godot::Ref<NetwMultiplayer> api;
    NetwMultiplayer::Role construction_role = NetwMultiplayer::ROLE_NONE;
    bool deletion_finalized = false;

    godot::Ref<NetwMultiplayer> make_api() const;
    void mount_api();
    void unmount_api();
    void bind_api_signals();
    void unbind_api_signals();
    void offer_session_fallback();
    bool session_settings_are_immutable(const char *p_field) const;
    godot::Ref<NetwSessionConfig> build_session_config() const;
    godot::Node *find_bare_level() const;

    void enter_tree();
    void ready();
    void decide_bring_up();
    void exiting();
    void poll();

    void bring_up_here(
        NetwMultiplayer::TransportMode p_mode,
        const godot::String &p_address,
        const godot::StringName &p_username,
        const godot::Array &p_join_args
    );
    void debug_autoconnect();

    void settle_embedding();
    void finalize_session();
    void teardown_session();
    void close_peer_on_delete();
    void release_peer();
    void on_peer_created(
        const godot::Ref<godot::MultiplayerPeer> &p_peer,
        int64_t p_error,
        const godot::String &p_detail,
        const godot::StringName &p_username,
        const godot::Array &p_join_args
    );
    void on_peer_connected(int64_t p_peer_id);
    void on_peer_disconnected(int64_t p_peer_id);
    void on_connected_to_server();
    void on_server_disconnected();
    void generate_app_id();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    static int64_t app_tag_of(const godot::StringName &p_app_id);
    static godot::StringName random_app_id();

    void set_peer_class(const godot::StringName &p_value);
    godot::StringName get_peer_class() const {
        return peer_class;
    }

    void set_transport_settings(const godot::Dictionary &p_value) {
        transport_settings = p_value;
    }
    godot::Dictionary get_transport_settings() const {
        return transport_settings;
    }

    void set_link_conditions(const godot::Ref<NetwLinkConditions> &p_value);
    godot::Ref<NetwLinkConditions> get_link_conditions() const {
        return link_conditions;
    }

    void set_auto_host_headless(bool p_value) {
        auto_host_headless = p_value;
    }
    bool get_auto_host_headless() const {
        return auto_host_headless;
    }

    void set_desired_role(NetwMultiplayer::Role p_value);
    NetwMultiplayer::Role get_desired_role() const {
        return desired_role;
    }

    void set_app_id(const godot::StringName &p_value);
    godot::StringName get_app_id() const {
        return app_id;
    }

    void set_debug_join(const godot::Ref<DebugJoinConfig> &p_value) {
        debug_join = p_value;
    }
    godot::Ref<DebugJoinConfig> get_debug_join() const {
        return debug_join;
    }

    void set_api_script(const godot::Ref<godot::Script> &p_value);
    godot::Ref<godot::Script> get_api_script() const {
        return api_script;
    }

    godot::Ref<NetwMultiplayer> get_api() const {
        return api;
    }

    void dispose();
    MultiplayerTree *raise_embedded_server();

    MultiplayerTree();
};

} // namespace netw
