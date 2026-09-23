#include "netw/entity/control.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/script.hpp"

namespace netw::entity {

using namespace godot;

int64_t Control::resolve(int64_t p_peer_id) const {
    if (configured) {
        return controller;
    }
    return initial == int(InitialController::REPRESENTED_PEER) ? p_peer_id : 0;
}

StringName Control::write_book_key() {
    return StringName("netw_write_policies");
}

StringName Control::emit_book_key() {
    return StringName("netw_emit_policies");
}

void Control::declare_policy(
    Object *p_script,
    const StringName &p_name,
    int64_t p_policy,
    bool p_is_signal
) {
    if (p_script == nullptr || p_name == StringName()) {
        return;
    }
    const StringName key = p_is_signal ? emit_book_key() : write_book_key();
    Dictionary book = p_script->has_meta(key)
        ? Dictionary(p_script->get_meta(key))
        : Dictionary();
    book[p_name] = p_policy;
    p_script->set_meta(key, book);
}

bool Control::script_admits(
    Object *p_node,
    const StringName &p_name,
    bool p_is_signal,
    int64_t p_sender,
    int64_t p_controller,
    int64_t p_coordinator
) {
    if (p_sender == p_coordinator) {
        return true;
    }
    Node *node = Object::cast_to<Node>(p_node);
    if (node == nullptr) {
        return false;
    }
    const Ref<Script> script = node->get_script();
    if (script.is_null()) {
        return false;
    }
    const int64_t authority = int64_t(node->get_multiplayer_authority());
    const StringName key = p_is_signal ? emit_book_key() : write_book_key();
    if (!script->has_meta(key)) {
        return p_sender == authority;
    }
    const Dictionary book = script->get_meta(key);
    if (!book.has(p_name)) {
        return p_sender == authority;
    }
    return policy_admits(
        int(int64_t(book[p_name])),
        p_sender,
        authority,
        p_controller
    );
}

bool Control::set_controller(int64_t p_controller) {
    const int64_t previous = controller;
    configured = true;
    controller = p_controller;
    return previous != p_controller;
}

bool Control::policy_admits(
    int p_policy,
    int64_t p_sender,
    int64_t p_authority,
    int64_t p_controller
) {
    switch (WritePolicy(p_policy)) {
        case WritePolicy::AUTHORITY:
            return p_sender == p_authority;
        case WritePolicy::CONTROLLER:
            return p_sender == p_controller;
        case WritePolicy::ANY_PEER:
            return true;
    }
    return false;
}

bool Control::decide(int64_t p_controller, int64_t p_hold) {
    const bool moved = !configured || p_controller != controller;
    revision += 1;
    if (moved) {
        tenure = revision;
    }
    hold = p_controller == 0 ? int64_t(HOLD_NONE) : p_hold;
    return moved;
}

bool Control::install(
    uint64_t p_revision,
    bool p_tenure_changed,
    int64_t p_hold
) {
    if (p_revision <= revision) {
        return false;
    }
    revision = p_revision;
    if (p_tenure_changed) {
        tenure = p_revision;
    }
    hold = p_hold;
    return true;
}

void Control::seed(uint64_t p_revision, uint64_t p_tenure, int64_t p_hold) {
    revision = p_revision;
    tenure = p_tenure;
    hold = p_hold;
}

bool Control::excludes(
    int64_t p_requester,
    int64_t p_hold,
    int64_t p_current,
    int64_t p_current_hold
) {
    if (p_current == 0 || p_current == p_requester) {
        return false;
    }
    return p_current_hold == HOLD_EXCLUSIVE || p_hold == HOLD_YIELDABLE;
}

Control::Ruling Control::rule(
    int64_t p_requester,
    int64_t p_hold,
    int64_t p_current
) const {
    if (!admits_request()) {
        return Ruling::UNAVAILABLE;
    }
    if (p_current != 0 && p_current == p_requester) {
        return Ruling::HOLD_CHANGE;
    }
    if (excludes(p_requester, p_hold, p_current, hold)) {
        return Ruling::EXCLUDED;
    }
    return Ruling::ASK_FILTER;
}

Control::Pending *Control::issue(
    OpKind p_kind,
    int64_t p_hold,
    int64_t p_tick
) {
    if (pending.size() >= MAX_PENDING) {
        return nullptr;
    }
    last_op += 1;
    Pending made;
    made.op = last_op;
    made.kind = p_kind;
    made.hold = p_hold;
    made.issued_tick = p_tick;
    pending.push_back(made);
    return &pending[pending.size() - 1];
}

bool Control::take_pending(uint64_t p_op, Pending &r_taken) {
    for (uint32_t at = 0; at < pending.size(); ++at) {
        if (pending[at].op == p_op) {
            r_taken = pending[at];
            pending.remove_at(at);
            return true;
        }
    }
    return false;
}

bool Control::claims_pending() const {
    for (const Pending &held : pending) {
        if (held.kind == OpKind::REQUEST) {
            return true;
        }
    }
    return false;
}

void Control::abandon(uint64_t p_op) {
    abandoned.push_back(p_op);
    if (abandoned.size() > MAX_ABANDONED) {
        abandoned.remove_at(0);
    }
}

bool Control::take_abandoned(uint64_t p_op) {
    for (uint32_t at = 0; at < abandoned.size(); ++at) {
        if (abandoned[at] == p_op) {
            abandoned.remove_at(at);
            return true;
        }
    }
    return false;
}

bool Control::controlled_by(int64_t p_local_peer, int64_t p_peer_id) const {
    if (p_local_peer == 0) {
        return false;
    }
    const int64_t steering = resolve(p_peer_id);
    return steering != 0 && steering == p_local_peer;
}

Control::Verdict Control::disconnect_verdict(
    int64_t p_disconnected,
    int64_t p_peer_id
) const {
    if (p_disconnected == 0) {
        return NOTHING;
    }
    if (p_peer_id == p_disconnected) {
        return DESPAWN_REPRESENTED;
    }
    if (resolve(p_peer_id) != p_disconnected) {
        return NOTHING;
    }
    return on_disconnect == int(DisconnectRule::DESPAWN) ? DESPAWN_CONTROLLER
                                                         : REVERT_TO_SERVER;
}

} // namespace netw::entity
