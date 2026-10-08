#include "netw/api/display.hpp"

#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

NetwDisplay::NetwDisplay() = default;

void NetwDisplay::bind(NetwEntity *p_entity) {
    entity_id = gd::instance_id(p_entity);
}

Ref<NetwEntity> NetwDisplay::entity() const {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(gd::object_of(entity_id))
    );
}

NetwMultiplayer *NetwDisplay::core() const {
    const Ref<NetwEntity> bound = entity();
    return bound.is_valid() ? NetwEntity::session_core_for(bound->get_owner())
                            : nullptr;
}

RID NetwDisplay::entity_rid() const {
    const Ref<NetwEntity> bound = entity();
    return bound.is_valid() ? bound->get_rid_handle() : RID();
}

display::Decl *NetwDisplay::declaration() const {
    const Ref<NetwEntity> bound = entity();
    if (bound.is_null() || bound->get_record() == nullptr) {
        return nullptr;
    }
    return &bound->get_record()->display_declaration();
}

void NetwDisplay::write(int p_param, const Variant &p_value) {
    NetwMultiplayer *session = core();
    const Ref<NetwEntity> bound = entity();
    if (session != nullptr && bound.is_valid()
        && session->entity_get_view(bound->get_rid_handle()).is_valid()) {
        session->display_set_param(
            bound->get_rid_handle(),
            NetwMultiplayer::DisplayParam(p_param),
            p_value
        );
        return;
    }
    display::Decl *held = declaration();
    if (held != nullptr) {
        held->set_param(p_param, p_value);
    }
}

Variant NetwDisplay::read(int p_param) const {
    NetwMultiplayer *session = core();
    const Ref<NetwEntity> bound = entity();
    if (session != nullptr && bound.is_valid()
        && session->entity_get_view(bound->get_rid_handle()).is_valid()) {
        return session->display_get_param(
            bound->get_rid_handle(),
            NetwMultiplayer::DisplayParam(p_param)
        );
    }
    const display::Decl *held = declaration();
    return held != nullptr ? held->get_param(p_param)
                           : display::Decl().get_param(p_param);
}

NodePath NetwDisplay::get_visual_root() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_VISUAL_ROOT);
}

void NetwDisplay::set_visual_root(const NodePath &p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_VISUAL_ROOT, p_value);
}

NetwDisplay::DisplayRole NetwDisplay::get_display_role() const {
    return NetwDisplay::DisplayRole(
        int64_t(read(NetwMultiplayer::DISPLAY_PARAM_ROLE))
    );
}

void NetwDisplay::set_display_role(NetwDisplay::DisplayRole p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_ROLE, int64_t(p_value));
}

NetwDisplay::LiveMode NetwDisplay::get_live_mode() const {
    return NetwDisplay::LiveMode(
        int64_t(read(NetwMultiplayer::DISPLAY_PARAM_LIVE_MODE))
    );
}

void NetwDisplay::set_live_mode(NetwDisplay::LiveMode p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_LIVE_MODE, int64_t(p_value));
}

double NetwDisplay::get_live_smooth_time() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_LIVE_SMOOTH_TIME);
}

void NetwDisplay::set_live_smooth_time(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_LIVE_SMOOTH_TIME, p_value);
}

double NetwDisplay::get_chase_glide_time() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_CHASE_GLIDE_TIME);
}

void NetwDisplay::set_chase_glide_time(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_CHASE_GLIDE_TIME, p_value);
}

NetwDisplay::TimelineMode NetwDisplay::get_timeline_mode() const {
    return NetwDisplay::TimelineMode(
        int64_t(read(NetwMultiplayer::DISPLAY_PARAM_TIMELINE_MODE))
    );
}

void NetwDisplay::set_timeline_mode(NetwDisplay::TimelineMode p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_TIMELINE_MODE, int64_t(p_value));
}

int64_t NetwDisplay::get_max_forecast_ticks() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_MAX_FORECAST_TICKS);
}

void NetwDisplay::set_max_forecast_ticks(int64_t p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_MAX_FORECAST_TICKS, p_value);
}

bool NetwDisplay::get_enable_smart_dilation() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_SMART_DILATION);
}

void NetwDisplay::set_enable_smart_dilation(bool p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_SMART_DILATION, p_value);
}

double NetwDisplay::get_lag_adapt_rate() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_LAG_ADAPT_RATE);
}

void NetwDisplay::set_lag_adapt_rate(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_LAG_ADAPT_RATE, p_value);
}

double NetwDisplay::get_floor_smoothing() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_FLOOR_SMOOTHING);
}

void NetwDisplay::set_floor_smoothing(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_FLOOR_SMOOTHING, p_value);
}

int64_t NetwDisplay::get_trace_interval() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_TRACE_INTERVAL);
}

void NetwDisplay::set_trace_interval(int64_t p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_TRACE_INTERVAL, p_value);
}

double NetwDisplay::get_display_lag() const {
    NetwMultiplayer *session = core();
    if (session == nullptr) {
        return 0.0;
    }
    const Variant answered = session->display_get_track_stat(
        entity_rid(),
        StringName(),
        StringName("display_lag")
    );
    return answered.get_type() != Variant::NIL ? double(answered) : 0.0;
}

NetwDisplay::DisplayPump NetwDisplay::get_pump_mode() const {
    const Variant answered = get_track_stat(StringName(), "pump_mode");
    return answered.get_type() != Variant::NIL
        ? NetwDisplay::DisplayPump(int64_t(answered))
        : NetwDisplay::DISPLAY_PUMP_UNRESOLVED;
}

Variant NetwDisplay::get_track_stat(
    const StringName &p_property,
    const StringName &p_stat
) const {
    NetwMultiplayer *session = core();
    if (session == nullptr) {
        return Variant();
    }
    return session->display_get_track_stat(entity_rid(), p_property, p_stat);
}

int64_t NetwDisplay::displayed_authoring_tick() const {
    NetwMultiplayer *session = core();
    return session != nullptr ? session->display_get_tick(entity_rid()) : -1;
}

void NetwDisplay::reset() {
    NetwMultiplayer *session = core();
    if (session != nullptr) {
        session->display_reset(entity_rid());
    }
}

void NetwDisplay::_bind_methods() {
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_AUTO);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_REMOTE);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_PREDICTED);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_DISABLED);
    BIND_ENUM_CONSTANT(DISPLAY_ROLE_AUTHORITY);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_UNRESOLVED);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_DISABLED);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_REMOTE);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_BRACKETED);
    BIND_ENUM_CONSTANT(DISPLAY_PUMP_CHASE);
    BIND_ENUM_CONSTANT(LIVE_MODE_CHASE);
    BIND_ENUM_CONSTANT(LIVE_MODE_BRACKETED);
    BIND_ENUM_CONSTANT(TIMELINE_MODE_BUFFERED);
    BIND_ENUM_CONSTANT(TIMELINE_MODE_FORECAST);
    ClassDB::bind_method(
        D_METHOD("get_visual_root"),
        &NetwDisplay::get_visual_root
    );
    ClassDB::bind_method(
        D_METHOD("set_visual_root", "value"),
        &NetwDisplay::set_visual_root
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::NODE_PATH, "visual_root"),
        "set_visual_root",
        "get_visual_root"
    );
    ClassDB::bind_method(
        D_METHOD("get_display_role"),
        &NetwDisplay::get_display_role
    );
    ClassDB::bind_method(
        D_METHOD("set_display_role", "value"),
        &NetwDisplay::set_display_role
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "display_role",
            PROPERTY_HINT_ENUM,
            "Auto,Remote,Predicted,Disabled,Authority",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwDisplay.DisplayRole"
        ),
        "set_display_role",
        "get_display_role"
    );
    ClassDB::bind_method(
        D_METHOD("get_live_mode"),
        &NetwDisplay::get_live_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_live_mode", "value"),
        &NetwDisplay::set_live_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "live_mode",
            PROPERTY_HINT_ENUM,
            "Chase,Bracketed",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwDisplay.LiveMode"
        ),
        "set_live_mode",
        "get_live_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_live_smooth_time"),
        &NetwDisplay::get_live_smooth_time
    );
    ClassDB::bind_method(
        D_METHOD("set_live_smooth_time", "value"),
        &NetwDisplay::set_live_smooth_time
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "live_smooth_time"),
        "set_live_smooth_time",
        "get_live_smooth_time"
    );
    ClassDB::bind_method(
        D_METHOD("get_chase_glide_time"),
        &NetwDisplay::get_chase_glide_time
    );
    ClassDB::bind_method(
        D_METHOD("set_chase_glide_time", "value"),
        &NetwDisplay::set_chase_glide_time
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "chase_glide_time"),
        "set_chase_glide_time",
        "get_chase_glide_time"
    );
    ClassDB::bind_method(
        D_METHOD("get_timeline_mode"),
        &NetwDisplay::get_timeline_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_timeline_mode", "value"),
        &NetwDisplay::set_timeline_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "timeline_mode",
            PROPERTY_HINT_ENUM,
            "Buffered,Forecast",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwDisplay.TimelineMode"
        ),
        "set_timeline_mode",
        "get_timeline_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_forecast_ticks"),
        &NetwDisplay::get_max_forecast_ticks
    );
    ClassDB::bind_method(
        D_METHOD("set_max_forecast_ticks", "value"),
        &NetwDisplay::set_max_forecast_ticks
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_forecast_ticks"),
        "set_max_forecast_ticks",
        "get_max_forecast_ticks"
    );
    ClassDB::bind_method(
        D_METHOD("get_enable_smart_dilation"),
        &NetwDisplay::get_enable_smart_dilation
    );
    ClassDB::bind_method(
        D_METHOD("set_enable_smart_dilation", "value"),
        &NetwDisplay::set_enable_smart_dilation
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "enable_smart_dilation"),
        "set_enable_smart_dilation",
        "get_enable_smart_dilation"
    );
    ClassDB::bind_method(
        D_METHOD("get_lag_adapt_rate"),
        &NetwDisplay::get_lag_adapt_rate
    );
    ClassDB::bind_method(
        D_METHOD("set_lag_adapt_rate", "value"),
        &NetwDisplay::set_lag_adapt_rate
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "lag_adapt_rate"),
        "set_lag_adapt_rate",
        "get_lag_adapt_rate"
    );
    ClassDB::bind_method(
        D_METHOD("get_floor_smoothing"),
        &NetwDisplay::get_floor_smoothing
    );
    ClassDB::bind_method(
        D_METHOD("set_floor_smoothing", "value"),
        &NetwDisplay::set_floor_smoothing
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "floor_smoothing"),
        "set_floor_smoothing",
        "get_floor_smoothing"
    );
    ClassDB::bind_method(
        D_METHOD("get_trace_interval"),
        &NetwDisplay::get_trace_interval
    );
    ClassDB::bind_method(
        D_METHOD("set_trace_interval", "value"),
        &NetwDisplay::set_trace_interval
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "trace_interval"),
        "set_trace_interval",
        "get_trace_interval"
    );
    ClassDB::bind_method(
        D_METHOD("get_display_lag"),
        &NetwDisplay::get_display_lag
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "display_lag"),
        "",
        "get_display_lag"
    );
    ClassDB::bind_method(
        D_METHOD("get_pump_mode"),
        &NetwDisplay::get_pump_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "pump_mode",
            PROPERTY_HINT_ENUM,
            "Unresolved:-1,Disabled:0,Remote:1,Bracketed:2,Chase:3",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwDisplay.DisplayPump"
        ),
        "",
        "get_pump_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_track_stat", "property", "stat"),
        &NetwDisplay::get_track_stat
    );
    ClassDB::bind_method(
        D_METHOD("displayed_authoring_tick"),
        &NetwDisplay::displayed_authoring_tick
    );
    ClassDB::bind_method(D_METHOD("reset"), &NetwDisplay::reset);
    ClassDB::bind_method(D_METHOD("entity"), &NetwDisplay::entity);
}

Ref<NetwDisplay> build_display_handle(Object *p_entity) {
    Ref<NetwDisplay> made;
    made.instantiate();
    made->bind(Object::cast_to<NetwEntity>(p_entity));
    return made;
}

} // namespace netw
