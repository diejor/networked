#include "netw/api/admission_config.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

Ref<NetwAdmissionConfig> NetwAdmissionConfig::proof(
    const Callable &p_handler
) {
    NETW_ERR_COND_V(
        !p_handler.is_valid(),
        Ref<NetwAdmissionConfig>(this),
        sys::SESSION,
        "The proof handler cannot be called. Pass a valid Callable to "
        "NetwAdmissionConfig.proof."
    );
    proof_handler = p_handler;
    return Ref<NetwAdmissionConfig>(this);
}

void NetwAdmissionConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("proof", "handler"), &NetwAdmissionConfig::proof
    );
}

} // namespace netw
