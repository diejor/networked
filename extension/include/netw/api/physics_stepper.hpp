#pragma once

#include <cstdint>

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"

namespace netw {

class NetwPhysicsStepper : public godot::RefCounted {
    GDCLASS(NetwPhysicsStepper, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    virtual bool can_step();
    virtual void step(const godot::RID &p_space, double p_delta);
    virtual void snapshot(const godot::RID &p_space, int64_t p_tick);
    virtual void restore(const godot::RID &p_space, int64_t p_tick);

    GDVIRTUAL0R(bool, _can_step)
    GDVIRTUAL2(_step, godot::RID, double)
    GDVIRTUAL2(_snapshot, godot::RID, int64_t)
    GDVIRTUAL2(_restore, godot::RID, int64_t)
};

} // namespace netw
