#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwDespawnConfig : public godot::RefCounted {
    GDCLASS(NetwDespawnConfig, godot::RefCounted)

private:
    godot::StringName hook_method;
    double linger_seconds = 0.0;

protected:
    static void _bind_methods();

public:
    void set_hook_method(const godot::StringName &p_hook_method) {
        hook_method = p_hook_method;
    }
    godot::StringName get_hook_method() const {
        return hook_method;
    }

    void set_linger_seconds(double p_linger_seconds) {
        linger_seconds = p_linger_seconds;
    }
    double get_linger_seconds() const {
        return linger_seconds;
    }

    godot::Ref<NetwDespawnConfig> before_removal(
        const godot::Callable &p_callable
    );
    godot::Ref<NetwDespawnConfig> linger(double p_seconds);
};

} // namespace netw
