#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
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
        IMMEDIATE = 2,
    };

    enum Hold : int {
        HOLD_NONE = 0,
        HOLD_YIELDABLE = 1,
        HOLD_EXCLUSIVE = 2,
    };

    enum class OpKind : int {
        REQUEST = 0,
        RELEASE = 1,
    };

    enum class Outcome : int {
        GRANTED = 0,
        UNAUTHORIZED = 1,
        UNAVAILABLE = 2,
    };

    enum class Ruling {
        HOLD_CHANGE,
        UNAVAILABLE,
        EXCLUDED,
        ASK_FILTER,
    };

    struct Pending {
        uint64_t op = 0;
        OpKind kind = OpKind::REQUEST;
        int64_t hold = HOLD_NONE;
        int64_t issued_tick = 0;
    };

    static constexpr uint32_t MAX_PENDING = 8;
    static constexpr uint32_t MAX_ABANDONED = 16;

    enum class DisconnectRule : int {
        REVERT_TO_SERVER = 0,
        DESPAWN = 1,
    };

    enum class ParentDespawnRule : int {
        CASCADE = 0,
        DETACH = 1,
    };

    enum class Lifecycle : int {
        SESSION = 0,
        CONTROLLER = 1,
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
    int64_t on_parent_despawn = int(ParentDespawnRule::CASCADE);
    int64_t lifecycle = int(Lifecycle::SESSION);
    uint64_t revision = 0;
    uint64_t tenure = 0;
    int64_t hold = HOLD_NONE;
    godot::LocalVector<Pending> pending;
    godot::LocalVector<uint64_t> abandoned;
    uint64_t last_op = 0;

    int64_t resolve(int64_t p_peer_id) const;

    bool decide(int64_t p_controller, int64_t p_hold);
    bool install(uint64_t p_revision, bool p_tenure_changed, int64_t p_hold);
    void seed(uint64_t p_revision, uint64_t p_tenure, int64_t p_hold);

    Ruling rule(int64_t p_requester, int64_t p_hold, int64_t p_current) const;
    static bool excludes(
        int64_t p_requester,
        int64_t p_hold,
        int64_t p_current,
        int64_t p_current_hold
    );

    Pending *issue(OpKind p_kind, int64_t p_hold, int64_t p_tick);
    bool take_pending(uint64_t p_op, Pending &r_taken);
    void abandon(uint64_t p_op);
    bool take_abandoned(uint64_t p_op);
    bool has_pending() const {
        return !pending.is_empty();
    }
    bool claims_pending() const;
    bool runs_ahead() const {
        return transfer == int(Transfer::IMMEDIATE) && claims_pending();
    }

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
        int64_t p_controller,
        int64_t p_coordinator
    );

    bool set_controller(int64_t p_controller);

    bool controlled_by(int64_t p_local_peer, int64_t p_peer_id) const;

    bool admits_request() const {
        return transfer != int(Transfer::FIXED);
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
    int64_t get_on_parent_despawn() const {
        return on_parent_despawn;
    }
    void set_on_parent_despawn(int64_t p_rule) {
        on_parent_despawn = p_rule;
    }
    int64_t get_lifecycle() const {
        return lifecycle;
    }
    void set_lifecycle(int64_t p_lifecycle) {
        lifecycle = p_lifecycle;
    }
    bool lifecycle_follows_controller() const {
        return lifecycle == int(Lifecycle::CONTROLLER);
    }
};

} // namespace netw::entity
