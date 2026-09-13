#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"

namespace netw {

class Serde : public godot::Resource {
    GDCLASS(Serde, godot::Resource)

protected:
    static void _bind_methods();

public:
    virtual godot::PackedByteArray serialize();
    virtual void deserialize(const godot::PackedByteArray &bytes);

    GDVIRTUAL0R(godot::PackedByteArray, _serialize)
    GDVIRTUAL1(_deserialize, const godot::PackedByteArray &)
};

} // namespace netw
