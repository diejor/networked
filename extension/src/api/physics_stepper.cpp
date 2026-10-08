#include "netw/api/physics_stepper.hpp"

#include "godot/class_db.hpp"
#include "godot/physics_server.hpp"

using namespace godot;

namespace netw {

void NetwPhysicsStepper::_bind_methods() {
    ClassDB::bind_method(D_METHOD("can_step"), &NetwPhysicsStepper::can_step);
    ClassDB::bind_method(
        D_METHOD("step", "space", "delta"),
        &NetwPhysicsStepper::step
    );
    ClassDB::bind_method(
        D_METHOD("snapshot", "space", "tick"),
        &NetwPhysicsStepper::snapshot
    );
    ClassDB::bind_method(
        D_METHOD("restore", "space", "tick"),
        &NetwPhysicsStepper::restore
    );

    GDVIRTUAL_BIND(_can_step);
    GDVIRTUAL_BIND(_step, "space", "delta");
    GDVIRTUAL_BIND(_snapshot, "space", "tick");
    GDVIRTUAL_BIND(_restore, "space", "tick");
}

bool NetwPhysicsStepper::can_step() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_can_step, answered)) {
        return answered;
    }
    return false;
}

void NetwPhysicsStepper::step(const RID &p_space, double p_delta) {
    GDVIRTUAL_CALL(_step, p_space, p_delta);
}

void NetwPhysicsStepper::snapshot(const RID &p_space, int64_t p_tick) {
    GDVIRTUAL_CALL(_snapshot, p_space, p_tick);
}

void NetwPhysicsStepper::restore(const RID &p_space, int64_t p_tick) {
    GDVIRTUAL_CALL(_restore, p_space, p_tick);
}

void NetwPhysicsStepper::flush_queries(const RID &p_space, int p_dimension) {
    Object *server = nullptr;
    if (p_dimension == 3) {
        server = PhysicsServer3D::get_singleton();
    } else if (p_dimension == 2) {
        server = PhysicsServer2D::get_singleton();
    }
    const StringName flush("space_flush_queries");
    if (server != nullptr && server->has_method(flush)) {
        server->call(flush, p_space);
    }
}

} // namespace netw
