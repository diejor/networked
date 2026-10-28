#include "netw/api/clock.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_BEFORE_TICK = "before_tick";
const char *SIG_TICKED = "ticked";
const char *SIG_SYNCHRONIZED = "synchronized";

} // namespace

NetwMultiplayer *NetwClock::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwClock::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
    if (p_session == nullptr) {
        return;
    }
}

void NetwClock::announce_before_tick(double p_delta, int64_t p_tick) {
    emit_signal(StringName(SIG_BEFORE_TICK), p_delta, p_tick);
}

void NetwClock::announce_ticked(double p_delta, int64_t p_tick) {
    emit_signal(StringName(SIG_TICKED), p_delta, p_tick);
}

void NetwClock::announce_synchronized() {
    emit_signal(StringName(SIG_SYNCHRONIZED));
}

int64_t NetwClock::get_tick() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_tick() : 0;
}

bool NetwClock::get_is_synchronized() const {
    NetwMultiplayer *api = session();
    return api != nullptr && (api->is_host() || api->clock_is_synchronized());
}

bool NetwClock::get_is_configured() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->clock_is_configured();
}

int64_t NetwClock::get_behind_count() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_simulation_behind_count() : 0;
}

int64_t NetwClock::get_tickrate() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        Ref<NetwClockConfig> defaults;
        defaults.instantiate();
        return defaults->get_tickrate();
    }
    return int64_t(api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE));
}

double NetwClock::get_physics_factor() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_physics_factor() : 1.0;
}

double NetwClock::get_tick_factor() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_tick_factor() : 0.0;
}

double NetwClock::get_tick_phase() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_tick_phase() : 0.0;
}

int64_t NetwClock::get_recommended_display_offset() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_recommended_display_offset() : 0;
}

int64_t NetwClock::get_display_offset() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        Ref<NetwClockConfig> defaults;
        defaults.instantiate();
        return defaults->get_display_offset();
    }
    return int64_t(
        api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET)
    );
}

void NetwClock::set_display_offset(int64_t p_ticks) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    api->clock_set_param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET, p_ticks);
}

NetwClock::SyncMode NetwClock::get_sync_mode() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return NetwClock::SYNC_MODE_STRETCH;
    }
    return NetwClock::SyncMode(
        int64_t(api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_SYNC_MODE))
    );
}

void NetwClock::set_sync_mode(NetwClock::SyncMode p_mode) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    api->clock_set_param(NetwMultiplayer::CLOCK_PARAM_SYNC_MODE, p_mode);
}

double NetwClock::get_ping_interval() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        Ref<NetwClockConfig> defaults;
        defaults.instantiate();
        return defaults->get_ping_interval();
    }
    return double(
        api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_PING_INTERVAL)
    );
}

void NetwClock::set_ping_interval(double p_seconds) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    api->clock_set_param(NetwMultiplayer::CLOCK_PARAM_PING_INTERVAL, p_seconds);
}

double NetwClock::monitor(NetwClock::ClockMonitor p_monitor) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_monitor(p_monitor) : 0.0;
}

void NetwClock::_bind_methods() {
    BIND_ENUM_CONSTANT(SYNC_MODE_SNAP);
    BIND_ENUM_CONSTANT(SYNC_MODE_STRETCH);
    BIND_ENUM_CONSTANT(MISMATCH_ACTION_WARN);
    BIND_ENUM_CONSTANT(MISMATCH_ACTION_DISCONNECT);
    BIND_ENUM_CONSTANT(MISMATCH_ACTION_SIGNAL);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_RTT);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_RTT_AVG);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_RTT_JITTER);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_ONE_WAY_LATENCY);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_TICKTIME);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_TICK_ACCUMULATOR);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_PHYSICS_FRAMES);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_POLLS);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_WALL_SECONDS);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_PHYSICS_HZ);
    BIND_ENUM_CONSTANT(CLOCK_MONITOR_POLL_HZ);
    ClassDB::bind_method(D_METHOD("get_tick"), &NetwClock::get_tick);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tick"), "", "get_tick");
    ClassDB::bind_method(
        D_METHOD("get_is_synchronized"),
        &NetwClock::get_is_synchronized
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_synchronized"),
        "",
        "get_is_synchronized"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_configured"),
        &NetwClock::get_is_configured
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_configured"),
        "",
        "get_is_configured"
    );
    ClassDB::bind_method(
        D_METHOD("get_behind_count"),
        &NetwClock::get_behind_count
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "behind_count"),
        "",
        "get_behind_count"
    );

    ClassDB::bind_method(D_METHOD("get_tickrate"), &NetwClock::get_tickrate);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tickrate"), "", "get_tickrate");
    ClassDB::bind_method(
        D_METHOD("get_physics_factor"),
        &NetwClock::get_physics_factor
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "physics_factor"),
        "",
        "get_physics_factor"
    );
    ClassDB::bind_method(
        D_METHOD("get_tick_factor"),
        &NetwClock::get_tick_factor
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "tick_factor"),
        "",
        "get_tick_factor"
    );
    ClassDB::bind_method(
        D_METHOD("get_tick_phase"),
        &NetwClock::get_tick_phase
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "tick_phase"),
        "",
        "get_tick_phase"
    );
    ClassDB::bind_method(
        D_METHOD("get_recommended_display_offset"),
        &NetwClock::get_recommended_display_offset
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "recommended_display_offset"),
        "",
        "get_recommended_display_offset"
    );

    ClassDB::bind_method(
        D_METHOD("get_display_offset"),
        &NetwClock::get_display_offset
    );
    ClassDB::bind_method(
        D_METHOD("set_display_offset", "ticks"),
        &NetwClock::set_display_offset
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "display_offset"),
        "set_display_offset",
        "get_display_offset"
    );
    ClassDB::bind_method(D_METHOD("get_sync_mode"), &NetwClock::get_sync_mode);
    ClassDB::bind_method(
        D_METHOD("set_sync_mode", "mode"),
        &NetwClock::set_sync_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "sync_mode",
            PROPERTY_HINT_ENUM,
            "Snap,Stretch"
        ),
        "set_sync_mode",
        "get_sync_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_ping_interval"),
        &NetwClock::get_ping_interval
    );
    ClassDB::bind_method(
        D_METHOD("set_ping_interval", "seconds"),
        &NetwClock::set_ping_interval
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "ping_interval"),
        "set_ping_interval",
        "get_ping_interval"
    );

    ClassDB::bind_method(D_METHOD("monitor", "monitor"), &NetwClock::monitor);

    ADD_SIGNAL(MethodInfo(
        SIG_BEFORE_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_TICKED,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SYNCHRONIZED));
}

} // namespace netw
