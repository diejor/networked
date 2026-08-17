#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/object_port.hpp"

namespace netw {

using namespace godot;

class NetwDisplayPort : public RefCounted {
    GDCLASS(NetwDisplayPort, RefCounted)

private:
    ObjectPort target;
    ObjectPort host;
    StringName target_prop;
    StringName source_prop;
    bool global_space = false;

    int64_t write_global(Object *p_target, const Variant &p_value);
    Node *host_parent();
    Vector2 host_position_2d(const Vector2 &p_host_local);
    Vector3 host_position_3d(const Vector3 &p_host_local);
    double host_rotation_2d(double p_host_local);
    Vector3 host_rotation_3d(const Vector3 &p_host_local);

protected:
    static void _bind_methods();

public:
    enum Write {
        WRITE_UNBOUND,
        WRITE_LOCAL,
        WRITE_GLOBAL,
        WRITE_REFUSED,
    };

    void bind(Object *p_target, Object *p_host);
    void unbind();
    bool is_bound() const;

    void declare(
        const StringName &p_target_prop,
        const StringName &p_source_prop,
        bool p_global_space
    );

    StringName get_target_prop() const;
    bool get_global_space() const;
    int64_t get_lost() const;

    int64_t write(const Variant &p_value);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwDisplayPort::Write);
