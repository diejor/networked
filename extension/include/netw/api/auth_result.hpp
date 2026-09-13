#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_identity.hpp"

namespace netw {

class AuthResult : public godot::RefCounted {
    GDCLASS(AuthResult, godot::RefCounted)

    bool accepted = false;
    godot::Ref<NetwIdentity> identity;
    godot::String rejection_reason;

protected:
    static void _bind_methods();

public:
    static godot::Ref<AuthResult> accept(
        const godot::Ref<NetwIdentity> &p_identity
    );
    static godot::Ref<AuthResult> reject(const godot::String &p_reason);

    bool get_accepted() const {
        return accepted;
    }
    void set_accepted(bool p_accepted) {
        accepted = p_accepted;
    }

    godot::Ref<NetwIdentity> get_identity() const {
        return identity;
    }
    void set_identity(const godot::Ref<NetwIdentity> &p_identity) {
        identity = p_identity;
    }

    godot::String get_rejection_reason() const {
        return rejection_reason;
    }
    void set_rejection_reason(const godot::String &p_reason) {
        rejection_reason = p_reason;
    }
};

} // namespace netw
