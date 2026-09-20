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

    int64_t get_tickrate() const;
    double get_physics_factor() const;
    double get_tick_factor() const;
    double get_tick_phase() const;
    int64_t get_recommended_display_offset() const;

    int64_t get_display_offset() const;
    void set_display_offset(int64_t p_ticks);
    int64_t get_sync_mode() const;
    void set_sync_mode(int64_t p_mode);
    double get_ping_interval() const;
    void set_ping_interval(double p_seconds);

    double monitor(int64_t p_monitor) const;
};

} // namespace netw
