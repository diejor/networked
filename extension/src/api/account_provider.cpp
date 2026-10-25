#include "netw/api/account_provider.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Variant NetwAccountProvider::prove(
    const String &p_challenge,
    const Ref<NetwSession> &p_session
) {
    Variant answer;
    if (GDVIRTUAL_CALL(_prove, p_challenge, p_session, answer)) {
        return answer;
    }
    return Variant();
}

Variant NetwAccountProvider::verify(
    const Ref<NetwJoinRequest> &p_request,
    const Variant &p_proof
) {
    Variant answer;
    if (GDVIRTUAL_CALL(_verify, p_request, p_proof, answer)) {
        return answer;
    }
    return Variant(int64_t(OK));
}

void NetwAccountProvider::_bind_methods() {
    GDVIRTUAL_BIND(_prove, "challenge", "session");
    GDVIRTUAL_BIND(_verify, "request", "proof");
}

} // namespace netw
