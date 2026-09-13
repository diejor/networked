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

double NetwClockHandle::monitor(int64_t p_monitor) const {
    NetwMultiplayer *api = session();
    return api != nullptr
        ? api->clock_get_monitor(NetwMultiplayer::ClockMonitor(p_monitor))
        : 0.0;
}

Variant NetwClockHandle::param(int64_t p_param) const {
    NetwMultiplayer *api = session();
    return api != nullptr
        ? api->clock_get_param(NetwMultiplayer::ClockParam(p_param))
        : Variant();
}

Error NetwClockHandle::set_param(int64_t p_param, const Variant &p_value) {
    NetwMultiplayer *api = session();
    return api != nullptr
        ? api->clock_set_param(NetwMultiplayer::ClockParam(p_param), p_value)
        : ERR_UNCONFIGURED;
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
        D_METHOD("monitor", "monitor"),
        &NetwClockHandle::monitor
    );
    ClassDB::bind_method(D_METHOD("param", "param"), &NetwClockHandle::param);
    ClassDB::bind_method(
        D_METHOD("set_param", "param", "value"),
        &NetwClockHandle::set_param
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
