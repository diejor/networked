#include "godot/class_db.hpp"
#include "godot/physics_body.hpp"

#if defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/class_db_singleton.hpp>
#endif

using namespace godot;

namespace netw::gd {

namespace {

bool body_class_has(const StringName &p_method) {
    const StringName body("PhysicsBody3D");
#if defined(NETW_MODULE)
    return ::ClassDB::has_method(body, p_method);
#else
    ClassDBSingleton *registry = ClassDBSingleton::get_singleton();
    return registry != nullptr && registry->class_has_method(body, p_method);
#endif
}

const StringName &flush_method() {
    static const StringName resolved = []() {
        const StringName names[] = {
            StringName("flush_kinematic_transform"),
            StringName("force_update_kinematic_transform"),
        };
        for (const StringName &name : names) {
            if (body_class_has(name)) {
                return name;
            }
        }
        return StringName();
    }();
    return resolved;
}

} // namespace

bool can_flush_kinematic() {
    return !flush_method().is_empty();
}

bool flush_kinematic(Object *p_body) {
    const StringName &method = flush_method();
    if (p_body == nullptr || method.is_empty()) {
        return false;
    }
    p_body->call(method);
    return true;
}

} // namespace netw::gd
