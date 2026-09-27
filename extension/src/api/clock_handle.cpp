#include "netw/api/clock_handle.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_BEFORE_TICK = "before_tick";
const char *SIG_ON_TICK = "on_tick";

} // namespace

NetwMultiplayer *NetwClockHandle::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwClockHandle::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
    if (p_session == nullptr) {
        return;
    }
    p_session->connect(
        StringName("clock_before_tick"),
        callable_mp(this, &NetwClockHandle::relay_before_tick)
    );
    p_session->connect(
        StringName("clock_on_tick"),
        callable_mp(this, &NetwClockHandle::relay_on_tick)
    );
}

void NetwClockHandle::relay_before_tick(double p_delta, int64_t p_tick) {
    emit_signal(StringName(SIG_BEFORE_TICK), p_delta, p_tick);
}

void NetwClockHandle::relay_on_tick(double p_delta, int64_t p_tick) {
    emit_signal(StringName(SIG_ON_TICK), p_delta, p_tick);
}

int64_t NetwClockHandle::get_tick() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_tick() : 0;
}

bool NetwClockHandle::get_is_synchronized() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->clock_is_synchronized();
}

bool NetwClockHandle::get_is_configured() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->clock_is_configured();
}

int64_t NetwClockHandle::get_behind_count() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_simulation_behind_count() : 0;
}

int64_t NetwClockHandle::get_tickrate() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return 0;
    }
    return int64_t(api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE));
}

double NetwClockHandle::get_physics_factor() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_physics_factor() : 1.0;
}

double NetwClockHandle::get_tick_factor() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_tick_factor() : 0.0;
}

double NetwClockHandle::get_tick_phase() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_tick_phase() : 0.0;
}

int64_t NetwClockHandle::get_recommended_display_offset() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->clock_get_recommended_display_offset() : 0;
}

int64_t NetwClockHandle::get_display_offset() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return 0;
    }
    return int64_t(
        api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET)
    );
}

void NetwClockHandle::set_display_offset(int64_t p_ticks) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    api->clock_set_param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET, p_ticks);
}

int64_t NetwClockHandle::get_sync_mode() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return NetwMultiplayer::SYNC_MODE_STRETCH;
    }
    return int64_t(
        api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_SYNC_MODE)
    );
}

void NetwClockHandle::set_sync_mode(int64_t p_mode) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    api->clock_set_param(NetwMultiplayer::CLOCK_PARAM_SYNC_MODE, p_mode);
}

double NetwClockHandle::get_ping_interval() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return 0.0;
    }
    return double(
        api->clock_get_param(NetwMultiplayer::CLOCK_PARAM_PING_INTERVAL)
    );
}

void NetwClockHandle::set_ping_interval(double p_seconds) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    api->clock_set_param(NetwMultiplayer::CLOCK_PARAM_PING_INTERVAL, p_seconds);
}

double NetwClockHandle::monitor(int64_t p_monitor) const {
    NetwMultiplayer *api = session();
    return api != nullptr
        ? api->clock_get_monitor(NetwMultiplayer::ClockMonitor(p_monitor))
        : 0.0;
}

void NetwClockHandle::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_tick"), &NetwClockHandle::get_tick);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tick"), "", "get_tick");
    ClassDB::bind_method(
        D_METHOD("get_is_synchronized"),
        &NetwClockHandle::get_is_synchronized
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_synchronized"),
        "",
        "get_is_synchronized"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_configured"),
        &NetwClockHandle::get_is_configured
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_configured"),
        "",
        "get_is_configured"
    );
    ClassDB::bind_method(
        D_METHOD("get_behind_count"),
        &NetwClockHandle::get_behind_count
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "behind_count"),
        "",
        "get_behind_count"
    );

    ClassDB::bind_method(
        D_METHOD("get_tickrate"),
        &NetwClockHandle::get_tickrate
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tickrate"), "", "get_tickrate");
    ClassDB::bind_method(
        D_METHOD("get_physics_factor"),
        &NetwClockHandle::get_physics_factor
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "physics_factor"),
        "",
        "get_physics_factor"
    );
    ClassDB::bind_method(
        D_METHOD("get_tick_factor"),
        &NetwClockHandle::get_tick_factor
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "tick_factor"),
        "",
        "get_tick_factor"
    );
    ClassDB::bind_method(
        D_METHOD("get_tick_phase"),
        &NetwClockHandle::get_tick_phase
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "tick_phase"),
        "",
        "get_tick_phase"
    );
    ClassDB::bind_method(
        D_METHOD("get_recommended_display_offset"),
        &NetwClockHandle::get_recommended_display_offset
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "recommended_display_offset"),
        "",
        "get_recommended_display_offset"
    );

    ClassDB::bind_method(
        D_METHOD("get_display_offset"),
        &NetwClockHandle::get_display_offset
    );
    ClassDB::bind_method(
        D_METHOD("set_display_offset", "ticks"),
        &NetwClockHandle::set_display_offset
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "display_offset"),
        "set_display_offset",
        "get_display_offset"
    );
    ClassDB::bind_method(
        D_METHOD("get_sync_mode"),
        &NetwClockHandle::get_sync_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_sync_mode", "mode"),
        &NetwClockHandle::set_sync_mode
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
        &NetwClockHandle::get_ping_interval
    );
    ClassDB::bind_method(
        D_METHOD("set_ping_interval", "seconds"),
        &NetwClockHandle::set_ping_interval
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "ping_interval"),
        "set_ping_interval",
        "get_ping_interval"
    );

    ClassDB::bind_method(
        D_METHOD("monitor", "monitor"),
        &NetwClockHandle::monitor
    );

    ADD_SIGNAL(MethodInfo(
        SIG_BEFORE_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ON_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
}

} // namespace netw
