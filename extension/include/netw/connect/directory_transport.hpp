#pragma once

#include <cstdint>

#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/connect/transport.hpp"

namespace netw::connect {

class DirectoryTransport : public Transport {
public:
    static const char *SIG_PEER_READY;
    static const char *SIG_FAILED;
    static const char *SIG_LIST_PUBLISHED;

    static bool is_directory(godot::Object *p_node);
    static godot::StringName peer_class_of_directory(godot::Object *p_node);

    explicit DirectoryTransport(godot::Object *p_directory);

    godot::ObjectID directory_id() const {
        return directory;
    }

    godot::StringName peer_class() const override;
    godot::String display_name() const override;
    bool is_available() const override;
    bool can_host_here() const override;
    bool can_probe() const override;
    bool can_browse() const override;
    godot::String address_label() const override;
    godot::String address_placeholder() const override;
    godot::String address_help() const override;
    bool accepts_empty_address() const override;
    godot::Dictionary host_settings() const override;
    godot::Dictionary client_settings() const override;

    void make_peer(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    ) override;
    void browse() override;

    godot::String join_address() const override;
    double timeout_hint() const override;
    void close() override;
    void close_query() override;

private:
    godot::ObjectID directory;

    netw::LobbyDirectory *reached() const;
};

} // namespace netw::connect
