#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/server_info.hpp"

namespace netw::connect {

struct ProbeHooks {
    godot::Callable authenticating;
    godot::Callable auth_received;
    godot::Callable connection_failed;
    godot::Callable authentication_failed;
};

class ProbeClient {
    struct Outcome {
        godot::Ref<NetwServerInfo> info;
        godot::String message;
        int64_t code = 0;
        bool ok = false;
    };

    godot::Ref<godot::SceneMultiplayer> api;
    godot::Ref<godot::MultiplayerPeer> peer;
    godot::Ref<NetwPromise> done;
    ProbeHooks hooks;
    Outcome outcome;
    double remaining = 0.0;
    int64_t opened_msec = 0;
    bool settled = false;
    bool inside_pump = false;

    void unhook(
        const godot::StringName &p_signal,
        const godot::Callable &p_hook
    );
    void settle_ok(const godot::Ref<NetwServerInfo> &p_info);
    void settle_error(int64_t p_code, const godot::String &p_message);
    void hand_over();
    void release();

public:
    static double DEFAULT_TIMEOUT;

    void open(
        const godot::Ref<godot::MultiplayerPeer> &p_peer,
        double p_timeout,
        const godot::Ref<NetwPromise> &p_done,
        const ProbeHooks &p_hooks
    );
    void poll(double p_delta);
    bool is_open() const;
    void close();

    void on_authenticating(int64_t p_peer_id);
    void on_auth_received(
        int64_t p_peer_id,
        const godot::PackedByteArray &p_data
    );
    void on_connection_failed();
    void on_authentication_failed(int64_t p_peer_id);
};

} // namespace netw::connect
