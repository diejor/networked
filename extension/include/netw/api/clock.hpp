#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/enums.hpp"

namespace netw {

class NetwMultiplayer;

class NetwClock : public godot::RefCounted, public enums::NetwClock {
    GDCLASS(NetwClock, godot::RefCounted)

    godot::ObjectID session_id;

    NetwMultiplayer *session() const;

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);

    void announce_before_tick(double p_delta, int64_t p_tick);
    void announce_ticked(double p_delta, int64_t p_tick);
    void announce_synchronized();

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
    enums::NetwClock::SyncMode get_sync_mode() const;
    void set_sync_mode(enums::NetwClock::SyncMode p_mode);
    double get_ping_interval() const;
    void set_ping_interval(double p_seconds);

    double monitor(enums::NetwClock::ClockMonitor p_monitor) const;
};

} // namespace netw
