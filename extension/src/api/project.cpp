#include "netw/api/project.hpp"

#include "godot/class_db.hpp"
#include "godot/math.hpp"

namespace netw {

using namespace godot;

void NetwProject::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwProject",
        D_METHOD("project", "value", "velocity", "age"),
        &NetwProject::project
    );
    ClassDB::bind_static_method(
        "NetwProject",
        D_METHOD("supports", "type"),
        &NetwProject::supports
    );
}

Variant NetwProject::project(
    const Variant &value,
    const Variant &velocity,
    double age
) {
    switch (value.get_type()) {
    case Variant::FLOAT:
        return double(value) + double(velocity) * age;
    case Variant::VECTOR2:
        return Vector2(value) + Vector2(velocity) * real_t(age);
    case Variant::VECTOR3:
        return Vector3(value) + Vector3(velocity) * real_t(age);
    case Variant::QUATERNION: {
        const Vector3 omega = velocity;
        const real_t speed = omega.length();
        if (speed < 0.0001 || age == 0.0) {
            return value;
        }
        const Quaternion turn(omega / speed, speed * real_t(age));
        return (turn * Quaternion(value)).normalized();
    }
    default:
        return value;
    }
}

bool NetwProject::supports(int type) {
    return type == Variant::FLOAT || type == Variant::VECTOR2
        || type == Variant::VECTOR3 || type == Variant::QUATERNION;
}

} // namespace netw
