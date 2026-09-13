#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwJoinConfig : public godot::RefCounted {
    GDCLASS(NetwJoinConfig, godot::RefCounted)

private:
    godot::Ref<godot::Script> context_script;
    godot::StringName context_name;
    godot::Array quantizers;

protected:
    static void _bind_methods();

public:
    static void set_arg_types_reader(const godot::Callable &p_reader);

    void set_context_script(const godot::Ref<godot::Script> &p_script) {
        context_script = p_script;
    }
    godot::Ref<godot::Script> get_context_script() const {
        return context_script;
    }

    void set_context_name(const godot::StringName &p_context_name) {
        context_name = p_context_name;
    }
    godot::StringName get_context_name() const {
        return context_name;
    }

    void set_quantizers(const godot::Array &p_quantizers) {
        quantizers = p_quantizers;
    }
    godot::Array get_quantizers() const {
        return quantizers;
    }

    godot::Ref<NetwJoinConfig> quantize(const godot::Array &p_quantizers);
};

} // namespace netw
