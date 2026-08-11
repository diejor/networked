#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/multiplayer.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

// Primary native orchestrator class for Networked multiplayer sessions.
class NetwMultiplayerCore : public RefCounted {
    GDCLASS(NetwMultiplayerCore, RefCounted)

private:
    Ref<MultiplayerPeer> peer;
    PackedInt32Array peer_ids;

protected:
    static void _bind_methods();

public:
    NetwMultiplayerCore();
    ~NetwMultiplayerCore() override;

    Error poll();
    void set_multiplayer_peer(const Ref<MultiplayerPeer> &p_peer);
    Ref<MultiplayerPeer> get_multiplayer_peer() const;
    bool has_multiplayer_peer() const;
    bool is_server() const;
    int32_t get_unique_id() const;
    PackedInt32Array get_peer_ids() const;
    void set_peer_ids(const PackedInt32Array &p_peer_ids);
};

} // namespace netw
