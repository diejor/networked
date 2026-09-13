#pragma once

#include <cstdint>

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/auth_result.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwAuthFlow : public godot::RefCounted {
    GDCLASS(NetwAuthFlow, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    virtual godot::Ref<NetwPromise> prepare(
        const godot::StringName &p_username
    );
    godot::Ref<NetwPromise> prepare_default(
        const godot::StringName &p_username
    );

    virtual godot::PackedByteArray credentials(
        const godot::StringName &p_username
    );
    godot::PackedByteArray credentials_default(
        const godot::StringName &p_username
    );

    virtual godot::Ref<AuthResult> verify(
        int64_t p_peer_id,
        const godot::PackedByteArray &p_data
    );
    godot::Ref<AuthResult> verify_default(
        int64_t p_peer_id,
        const godot::PackedByteArray &p_data
    );

    virtual godot::Ref<NetwIdentity> host_identity();
    godot::Ref<NetwIdentity> host_identity_default();

    GDVIRTUAL1R(godot::Ref<NetwPromise>, _prepare, godot::StringName)
    GDVIRTUAL1R(godot::PackedByteArray, _credentials, godot::StringName)
    GDVIRTUAL2R(
        godot::Ref<AuthResult>,
        _verify,
        int64_t,
        godot::PackedByteArray
    )
    GDVIRTUAL0R(godot::Ref<NetwIdentity>, _host_identity)
};

} // namespace netw
