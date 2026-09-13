#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw::entity {

class Control {
public:
    enum class InitialController : int {
        SERVER = 0,
        REPRESENTED_PEER = 1,
    };

    enum class Transfer : int {
        FIXED = 0,
        REQUESTABLE = 1,
    };

    enum class DisconnectRule : int {
        REVERT_TO_SERVER = 0,
        DESPAWN = 1,
    };

    enum class WritePolicy : int {
        AUTHORITY = 0,
        CONTROLLER = 1,
        ANY_PEER = 2,
    };

    enum Verdict {
        NOTHING,
        DESPAWN_REPRESENTED,
        REVERT_TO_SERVER,
        DESPAWN_CONTROLLER,
    };

    int64_t controller = 0;
    bool configured = false;
    int64_t initial = int(InitialController::SERVER);
    int64_t transfer = int(Transfer::FIXED);
    int64_t on_disconnect = int(DisconnectRule::REVERT_TO_SERVER);

    int64_t resolve(int64_t p_peer_id) const;

    static bool policy_admits(
        int p_policy,
        int64_t p_sender,
        int64_t p_authority,
        int64_t p_controller
    );

    static godot::StringName write_book_key();
    static godot::StringName emit_book_key();

    static void declare_policy(
        godot::Object *p_script,
        const godot::StringName &p_name,
        int64_t p_policy,
        bool p_is_signal
    );

    static bool script_admits(
        godot::Object *p_node,
        const godot::StringName &p_name,
        bool p_is_signal,
        int64_t p_sender,
        int64_t p_controller
    );

    bool set_controller(int64_t p_controller);

    bool controlled_by(int64_t p_local_peer, int64_t p_peer_id) const;

    bool admits_request() const {
        return transfer == int(Transfer::REQUESTABLE);
    }

    Verdict disconnect_verdict(int64_t p_disconnected, int64_t p_peer_id) const;

    int64_t get_controller() const {
        return controller;
    }
    bool get_configured() const {
        return configured;
    }
    int64_t get_initial() const {
        return initial;
    }
    void set_initial(int64_t p_initial) {
        initial = p_initial;
    }
    int64_t get_transfer() const {
        return transfer;
    }
    void set_transfer(int64_t p_transfer) {
        transfer = p_transfer;
    }
    int64_t get_on_disconnect() const {
        return on_disconnect;
    }
    void set_on_disconnect(int64_t p_rule) {
        on_disconnect = p_rule;
    }
};

} // namespace netw::entity
