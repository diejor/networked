#pragma once

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "godot/viewport.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace netw {

class MultiplayerViewport : public godot::SubViewport {
    GDCLASS(MultiplayerViewport, godot::SubViewport)

    godot::Ref<NetwMultiplayer> api;
    bool deletion_finalized = false;

    void mount_api();
    void unmount_api();
    void register_path();
    void unregister_path();
    void follow_rename();

    void enter_tree();
    void exiting();

    void close_peer_on_delete();
    void release_peer();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void set_api(const godot::Ref<NetwMultiplayer> &p_value);
    godot::Ref<NetwMultiplayer> get_api() const {
        return api;
    }

    MultiplayerViewport();
};

} // namespace netw
