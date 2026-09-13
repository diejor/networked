#include "netw/api/auth_flow.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwAuthFlow::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("prepare", "username"),
        &NetwAuthFlow::prepare
    );
    ClassDB::bind_method(
        D_METHOD("prepare_default", "username"),
        &NetwAuthFlow::prepare_default
    );
    ClassDB::bind_method(
        D_METHOD("credentials", "username"),
        &NetwAuthFlow::credentials
    );
    ClassDB::bind_method(
        D_METHOD("credentials_default", "username"),
        &NetwAuthFlow::credentials_default
    );
    ClassDB::bind_method(
        D_METHOD("verify", "peer_id", "data"),
        &NetwAuthFlow::verify
    );
    ClassDB::bind_method(
        D_METHOD("verify_default", "peer_id", "data"),
        &NetwAuthFlow::verify_default
    );
    ClassDB::bind_method(
        D_METHOD("host_identity"),
        &NetwAuthFlow::host_identity
    );
    ClassDB::bind_method(
        D_METHOD("host_identity_default"),
        &NetwAuthFlow::host_identity_default
    );

    GDVIRTUAL_BIND(_prepare, "username");
    GDVIRTUAL_BIND(_credentials, "username");
    GDVIRTUAL_BIND(_verify, "peer_id", "data");
    GDVIRTUAL_BIND(_host_identity);
}

Ref<NetwPromise> NetwAuthFlow::prepare(const StringName &p_username) {
    Ref<NetwPromise> answered;
    if (GDVIRTUAL_CALL(_prepare, p_username, answered)) {
        return answered;
    }
    return prepare_default(p_username);
}

Ref<NetwPromise> NetwAuthFlow::prepare_default(const StringName &) {
    return NetwPromise::resolved(OK);
}

PackedByteArray NetwAuthFlow::credentials(const StringName &p_username) {
    PackedByteArray answered;
    if (GDVIRTUAL_CALL(_credentials, p_username, answered)) {
        return answered;
    }
    return credentials_default(p_username);
}

PackedByteArray NetwAuthFlow::credentials_default(const StringName &) {
    return PackedByteArray();
}

Ref<AuthResult> NetwAuthFlow::verify(
    int64_t p_peer_id,
    const PackedByteArray &p_data
) {
    Ref<AuthResult> answered;
    if (GDVIRTUAL_CALL(_verify, p_peer_id, p_data, answered)) {
        return answered;
    }
    return verify_default(p_peer_id, p_data);
}

Ref<AuthResult> NetwAuthFlow::verify_default(int64_t, const PackedByteArray &) {
    return AuthResult::reject("Authentication flow did not implement verify");
}

Ref<NetwIdentity> NetwAuthFlow::host_identity() {
    Ref<NetwIdentity> answered;
    if (GDVIRTUAL_CALL(_host_identity, answered)) {
        return answered;
    }
    return host_identity_default();
}

Ref<NetwIdentity> NetwAuthFlow::host_identity_default() {
    return Ref<NetwIdentity>();
}

} // namespace netw
