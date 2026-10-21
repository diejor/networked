#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"

namespace netw {

class NetwAdmissionConfig : public godot::RefCounted {
    GDCLASS(NetwAdmissionConfig, godot::RefCounted)

    godot::Callable proof_handler;

protected:
    static void _bind_methods();

public:
    godot::Ref<NetwAdmissionConfig> proof(const godot::Callable &p_handler);
    godot::Callable get_proof_handler() const {
        return proof_handler;
    }
};

} // namespace netw
