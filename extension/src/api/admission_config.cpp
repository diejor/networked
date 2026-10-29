#include "netw/api/admission_config.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

Ref<NetwAdmissionConfig> NetwAdmissionConfig::accept(
    const Ref<NetwAccountProvider> &p_provider
) {
    NETW_ERR_COND_V(
        p_provider.is_null(),
        Ref<NetwAdmissionConfig>(this),
        sys::SESSION,
        "NetwAdmissionConfig.accept got null. Pass a NetwAccountProvider."
    );
    providers.push_back(p_provider);
    return Ref<NetwAdmissionConfig>(this);
}

Ref<NetwAdmissionConfig> NetwAdmissionConfig::timeout(double p_seconds) {
    NETW_ERR_COND_V(
        p_seconds <= 0.0,
        Ref<NetwAdmissionConfig>(this),
        sys::SESSION,
        "NetwAdmissionConfig.timeout got %s seconds. Pass a number of "
        "seconds above zero.",
        String::num(p_seconds)
    );
    timeout_seconds = p_seconds;
    return Ref<NetwAdmissionConfig>(this);
}

void NetwAdmissionConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("accept", "provider"),
        &NetwAdmissionConfig::accept
    );
    ClassDB::bind_method(
        D_METHOD("timeout", "seconds"),
        &NetwAdmissionConfig::timeout
    );
}

} // namespace netw
