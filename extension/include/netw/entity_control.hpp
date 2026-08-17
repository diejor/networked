#pragma once

/* Who steers one entity, and what happens to that when a peer leaves.
 *
 * The three policies are data: an entity declares who starts in control,
 * whether control may transfer at all, and what a controller's disconnect
 * does. Every question the rest of the system asks about control is answered
 * from those three plus the peer the entity represents, so this core takes the
 * representing peer and the local peer as arguments rather than reaching for a
 * node or a session.
 *
 * The one rule that is not obvious from the fields: the controller is resolved
 * LAZILY. An entity nobody wrote a controller onto is not uncontrolled, it is
 * controlled by whatever its initial rule says, and reading it before arm has
 * to answer what arm will. `configured` is what tells a pre-arm write apart
 * from the absence of one, so arm cannot overwrite a caller's explicit choice
 * with the rule's default.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

// The control vocabulary. NetwEntity publishes it to ClassDB and nothing else
// does, so a bound method here takes and returns `int` rather than binding a
// second enum beside the one a caller reads.

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

// Who a declaration lets write one field. The ordinals are the ones
// NetwScriptModel.Policy publishes, mirrored rather than bound so there stays
// one published spelling.
enum class WritePolicy : int {
    AUTHORITY = 0,
    CONTROLLER = 1,
    ANY_PEER = 2,
};

// One entity's control state and the decisions over it.
class NetwEntityControl : public godot::RefCounted {
    GDCLASS(NetwEntityControl, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // What a peer's disconnect asks the caller to do about this entity. The
    // two despawns are distinct because they despawn for different reasons and
    // the reason reaches gameplay through the despawn record.
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

    bool set_controller(int64_t p_controller);

    bool controlled_by(int64_t p_local_peer, int64_t p_peer_id) const;

    bool admits_request() const {
        return transfer == int(Transfer::REQUESTABLE);
    }

    Verdict disconnect_verdict(int64_t p_disconnected, int64_t p_peer_id) const;

    int64_t get_controller() const { return controller; }
    bool get_configured() const { return configured; }
    int64_t get_initial() const { return initial; }
    void set_initial(int64_t p_initial) { initial = p_initial; }
    int64_t get_transfer() const { return transfer; }
    void set_transfer(int64_t p_transfer) { transfer = p_transfer; }
    int64_t get_on_disconnect() const { return on_disconnect; }
    void set_on_disconnect(int64_t p_rule) { on_disconnect = p_rule; }
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwEntityControl::Verdict);
