#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "netw/api/account_provider.hpp"

namespace netw {

class NetwAdmissionConfig : public godot::RefCounted {
    GDCLASS(NetwAdmissionConfig, godot::RefCounted)

    godot::LocalVector<godot::Ref<NetwAccountProvider>> providers;
    double timeout_seconds = 0.0;

protected:
    static void _bind_methods();

public:
    godot::Ref<NetwAdmissionConfig> accept(
        const godot::Ref<NetwAccountProvider> &p_provider
    );
    int64_t provider_count() const {
        return int64_t(providers.size());
    }
    godot::Ref<NetwAccountProvider> provider_at(int64_t p_index) const {
        return providers[uint32_t(p_index)];
    }
    godot::Ref<NetwAdmissionConfig> timeout(double p_seconds);
    double timeout_of() const {
        return timeout_seconds;
    }
};

} // namespace netw
