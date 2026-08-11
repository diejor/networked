#pragma once

#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw {

// One trajectory point plus its derivative names where the point will be a
// short time later. The forecasting display and the reconciled body share this
// projection so both land on the same predicted pose.
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
