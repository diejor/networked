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
    int64_t p_controller
) {
    if (p_sender == 1) {
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
