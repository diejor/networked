#include "godot/class_db.hpp"
#include "godot/physics_server.hpp"

#if defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/class_db_singleton.hpp>
#endif

using namespace godot;

namespace netw::gd {

namespace {

bool server_class_has(const StringName &p_server, const StringName &p_method) {
#if defined(NETW_MODULE)
    return ::ClassDB::has_method(p_server, p_method);
#else
    ClassDBSingleton *registry = ClassDBSingleton::get_singleton();
    return registry != nullptr
        && registry->class_has_method(p_server, p_method);
#endif
}

Object *server_of(int p_dimension) {
    if (p_dimension == 3) {
        return PhysicsServer3D::get_singleton();
    }
    if (p_dimension == 2) {
        return PhysicsServer2D::get_singleton();
    }
    return nullptr;
}

const StringName &step_method() {
    static const StringName name("space_step");
    return name;
}

} // namespace

bool can_step_space(int p_dimension) {
    static const bool steps_3d
        = server_class_has(StringName("PhysicsServer3D"), step_method());
    static const bool steps_2d
        = server_class_has(StringName("PhysicsServer2D"), step_method());
    if (p_dimension == 3) {
        return steps_3d;
    }
    if (p_dimension == 2) {
        return steps_2d;
    }
    return false;
}

void step_space(const RID &p_space, int p_dimension, double p_delta) {
    Object *server = server_of(p_dimension);
    if (server == nullptr || !can_step_space(p_dimension)) {
        return;
    }
    server->call(step_method(), p_space, p_delta);
}

void flush_space_queries(const RID &p_space, int p_dimension) {
    Object *server = server_of(p_dimension);
    const StringName flush("space_flush_queries");
    if (server != nullptr && server->has_method(flush)) {
        server->call(flush, p_space);
    }
}

} // namespace netw::gd
