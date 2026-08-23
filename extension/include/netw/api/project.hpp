#pragma once

#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwProject : public godot::Object {
    GDCLASS(NetwProject, godot::Object)

protected:
    static void _bind_methods();

public:
    static godot::Variant project(
        const godot::Variant &value,
        const godot::Variant &velocity,
        double age
    );
    static bool supports(int type);
};

} // namespace netw
