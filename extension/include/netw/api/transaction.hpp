#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwTransaction : public godot::RefCounted {
    GDCLASS(NetwTransaction, godot::RefCounted)

private:
    godot::Array rows;

protected:
    static void _bind_methods();

public:
    void queue_upsert(
        const godot::StringName &table,
        const godot::StringName &id,
        const godot::Dictionary &data
    );

    const godot::Array &queued() const {
        return rows;
    }
};

} // namespace netw
