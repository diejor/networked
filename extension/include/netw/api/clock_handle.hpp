#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwMultiplayer;

class NetwClockHandle : public godot::RefCounted {
    GDCLASS(NetwClockHandle, godot::RefCounted)

    godot::ObjectID session_id;

    NetwMultiplayer *session() const;

    void relay_before_tick(double p_delta, int64_t p_tick);
    void relay_on_tick(double p_delta, int64_t p_tick);

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);

    int64_t get_tick() const;
    bool get_is_synchronized() const;
    bool get_is_configured() const;
    int64_t get_behind_count() const;
    double monitor(int64_t p_monitor) const;
    godot::Variant param(int64_t p_param) const;
    godot::Error set_param(int64_t p_param, const godot::Variant &p_value);
};

} // namespace netw
