#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwDatabaseConnection : public godot::RefCounted {
    GDCLASS(NetwDatabaseConnection, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    virtual godot::Ref<NetwPromise> read(const godot::Dictionary &p_address);
    virtual godot::Ref<NetwPromise> scan(const godot::Dictionary &p_request);
    virtual godot::Ref<NetwPromise> write_batch(
        const godot::Array &p_operations
    );
    virtual godot::Ref<NetwPromise> close();

    godot::Ref<NetwPromise> read_default(const godot::Dictionary &p_address);
    godot::Ref<NetwPromise> scan_default(const godot::Dictionary &p_request);
    godot::Ref<NetwPromise> write_batch_default(
        const godot::Array &p_operations
    );
    godot::Ref<NetwPromise> close_default();

    GDVIRTUAL1R(godot::Ref<NetwPromise>, _read, const godot::Dictionary &)
    GDVIRTUAL1R(godot::Ref<NetwPromise>, _scan, const godot::Dictionary &)
    GDVIRTUAL1R(godot::Ref<NetwPromise>, _write_batch, const godot::Array &)
    GDVIRTUAL0R(godot::Ref<NetwPromise>, _close)
};

} // namespace netw
