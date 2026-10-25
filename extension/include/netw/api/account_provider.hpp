#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/session.hpp"

namespace netw {

class NetwAccountProvider : public godot::RefCounted {
    GDCLASS(NetwAccountProvider, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    virtual godot::Variant prove(
        const godot::String &p_challenge,
        const godot::Ref<NetwSession> &p_session
    );
    virtual godot::Variant verify(
        const godot::Ref<NetwJoinRequest> &p_request,
        const godot::Variant &p_proof
    );

    GDVIRTUAL2R(
        godot::Variant,
        _prove,
        const godot::String &,
        godot::Ref<NetwSession>
    )
    GDVIRTUAL2R(
        godot::Variant,
        _verify,
        godot::Ref<NetwJoinRequest>,
        const godot::Variant &
    )
};

} // namespace netw
