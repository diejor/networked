#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/object_port.hpp"

namespace netw {

class NetwDisplayPort : public godot::RefCounted {
    GDCLASS(NetwDisplayPort, godot::RefCounted)

private:
    ObjectPort target;
    ObjectPort host;
    godot::StringName target_prop;
    godot::StringName source_prop;
    bool global_space = false;

    int64_t write_global(
        godot::Object *p_target,
        const godot::Variant &p_value
    );
    godot::Node *host_parent();
    godot::Vector2 host_position_2d(const godot::Vector2 &p_host_local);
    godot::Vector3 host_position_3d(const godot::Vector3 &p_host_local);
    double host_rotation_2d(double p_host_local);
    godot::Vector3 host_rotation_3d(const godot::Vector3 &p_host_local);

protected:
    static void _bind_methods();

public:
    enum Write {
        WRITE_UNBOUND,
        WRITE_LOCAL,
        WRITE_GLOBAL,
        WRITE_REFUSED,
    };

    void bind(godot::Object *p_target, godot::Object *p_host);
    void unbind();
    bool is_bound() const;

    void declare(
        const godot::StringName &p_target_prop,
        const godot::StringName &p_source_prop,
        bool p_global_space
    );

    godot::StringName get_target_prop() const;
    bool get_global_space() const;
    int64_t get_lost() const;

    int64_t write(const godot::Variant &p_value);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwDisplayPort::Write);
