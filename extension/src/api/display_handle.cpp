#include "netw/api/display_handle.hpp"

#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

NetwDisplayHandle::NetwDisplayHandle() = default;

void NetwDisplayHandle::bind(NetwEntity *p_entity) {
    entity_id = gd::instance_id(p_entity);
}

Ref<NetwEntity> NetwDisplayHandle::entity() const {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(gd::object_of(entity_id))
    );
}

NetwMultiplayer *NetwDisplayHandle::core() const {
    const Ref<NetwEntity> bound = entity();
    return bound.is_valid() ? NetwEntity::session_core_for(bound->get_owner())
                            : nullptr;
}

RID NetwDisplayHandle::entity_rid() const {
    const Ref<NetwEntity> bound = entity();
    return bound.is_valid() ? bound->get_rid_handle() : RID();
}

display::Decl *NetwDisplayHandle::declaration() const {
    const Ref<NetwEntity> bound = entity();
    if (bound.is_null() || bound->get_record() == nullptr) {
        return nullptr;
    }
    return &bound->get_record()->display_declaration();
}

void NetwDisplayHandle::write(int p_param, const Variant &p_value) {
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

Variant NetwDisplayHandle::read(int p_param) const {
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
    return held != nullptr ? held->get_param(p_param) : Variant();
}

NodePath NetwDisplayHandle::get_visual_root() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_VISUAL_ROOT);
}

void NetwDisplayHandle::set_visual_root(const NodePath &p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_VISUAL_ROOT, p_value);
}

int64_t NetwDisplayHandle::get_display_role() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_ROLE);
}

void NetwDisplayHandle::set_display_role(int64_t p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_ROLE, p_value);
}

int64_t NetwDisplayHandle::get_predicted_mode() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_PREDICTED_MODE);
}

void NetwDisplayHandle::set_predicted_mode(int64_t p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_PREDICTED_MODE, p_value);
}

double NetwDisplayHandle::get_predicted_smooth_time() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_PREDICTED_SMOOTH_TIME);
}

void NetwDisplayHandle::set_predicted_smooth_time(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_PREDICTED_SMOOTH_TIME, p_value);
}

bool NetwDisplayHandle::get_enable_smart_dilation() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_SMART_DILATION);
}

void NetwDisplayHandle::set_enable_smart_dilation(bool p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_SMART_DILATION, p_value);
}

double NetwDisplayHandle::get_max_extra_dilation() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_MAX_EXTRA_DILATION);
}

void NetwDisplayHandle::set_max_extra_dilation(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_MAX_EXTRA_DILATION, p_value);
}

double NetwDisplayHandle::get_lag_adapt_rate() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_LAG_ADAPT_RATE);
}

void NetwDisplayHandle::set_lag_adapt_rate(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_LAG_ADAPT_RATE, p_value);
}

double NetwDisplayHandle::get_starvation_growth() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_STARVATION_GROWTH);
}

void NetwDisplayHandle::set_starvation_growth(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_STARVATION_GROWTH, p_value);
}

double NetwDisplayHandle::get_floor_smoothing() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_FLOOR_SMOOTHING);
}

void NetwDisplayHandle::set_floor_smoothing(double p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_FLOOR_SMOOTHING, p_value);
}

int64_t NetwDisplayHandle::get_starvation_grace_frames() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_STARVATION_GRACE_FRAMES);
}

void NetwDisplayHandle::set_starvation_grace_frames(int64_t p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_STARVATION_GRACE_FRAMES, p_value);
}

int64_t NetwDisplayHandle::get_trace_interval() const {
    return read(NetwMultiplayer::DISPLAY_PARAM_TRACE_INTERVAL);
}

void NetwDisplayHandle::set_trace_interval(int64_t p_value) {
    write(NetwMultiplayer::DISPLAY_PARAM_TRACE_INTERVAL, p_value);
}

double NetwDisplayHandle::get_display_lag() const {
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

int64_t NetwDisplayHandle::displayed_authoring_tick() const {
    NetwMultiplayer *session = core();
    return session != nullptr ? session->display_get_tick(entity_rid()) : -1;
}

Ref<NetwRingBuffer> NetwDisplayHandle::get_buffer(
    const StringName &p_property
) const {
    NetwMultiplayer *session = core();
    if (session == nullptr) {
        return Ref<NetwRingBuffer>();
    }
    const Variant answered = session->display_get_track_stat(
        entity_rid(),
        p_property,
        StringName("buffer")
    );
    return Ref<NetwRingBuffer>(answered);
}

void NetwDisplayHandle::reset() {
    NetwMultiplayer *session = core();
    if (session != nullptr) {
        session->display_reset(entity_rid());
    }
}

void NetwDisplayHandle::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_visual_root"),
        &NetwDisplayHandle::get_visual_root
    );
    ClassDB::bind_method(
        D_METHOD("set_visual_root", "value"),
        &NetwDisplayHandle::set_visual_root
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::NODE_PATH, "visual_root"),
        "set_visual_root",
        "get_visual_root"
    );
    ClassDB::bind_method(
        D_METHOD("get_display_role"),
        &NetwDisplayHandle::get_display_role
    );
    ClassDB::bind_method(
        D_METHOD("set_display_role", "value"),
        &NetwDisplayHandle::set_display_role
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "display_role",
            PROPERTY_HINT_ENUM,
            "Auto,Remote,Predicted,Disabled,Authority"
        ),
        "set_display_role",
        "get_display_role"
    );
    ClassDB::bind_method(
        D_METHOD("get_predicted_mode"),
        &NetwDisplayHandle::get_predicted_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_predicted_mode", "value"),
        &NetwDisplayHandle::set_predicted_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "predicted_mode",
            PROPERTY_HINT_ENUM,
            "Chase,Bracketed"
        ),
        "set_predicted_mode",
        "get_predicted_mode"
    );
    ClassDB::bind_method(
        D_METHOD("get_predicted_smooth_time"),
        &NetwDisplayHandle::get_predicted_smooth_time
    );
    ClassDB::bind_method(
        D_METHOD("set_predicted_smooth_time", "value"),
        &NetwDisplayHandle::set_predicted_smooth_time
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "predicted_smooth_time"),
        "set_predicted_smooth_time",
        "get_predicted_smooth_time"
    );
    ClassDB::bind_method(
        D_METHOD("get_enable_smart_dilation"),
        &NetwDisplayHandle::get_enable_smart_dilation
    );
    ClassDB::bind_method(
        D_METHOD("set_enable_smart_dilation", "value"),
        &NetwDisplayHandle::set_enable_smart_dilation
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "enable_smart_dilation"),
        "set_enable_smart_dilation",
        "get_enable_smart_dilation"
    );
    ClassDB::bind_method(
        D_METHOD("get_max_extra_dilation"),
        &NetwDisplayHandle::get_max_extra_dilation
    );
    ClassDB::bind_method(
        D_METHOD("set_max_extra_dilation", "value"),
        &NetwDisplayHandle::set_max_extra_dilation
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "max_extra_dilation"),
        "set_max_extra_dilation",
        "get_max_extra_dilation"
    );
    ClassDB::bind_method(
        D_METHOD("get_lag_adapt_rate"),
        &NetwDisplayHandle::get_lag_adapt_rate
    );
    ClassDB::bind_method(
        D_METHOD("set_lag_adapt_rate", "value"),
        &NetwDisplayHandle::set_lag_adapt_rate
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "lag_adapt_rate"),
        "set_lag_adapt_rate",
        "get_lag_adapt_rate"
    );
    ClassDB::bind_method(
        D_METHOD("get_starvation_growth"),
        &NetwDisplayHandle::get_starvation_growth
    );
    ClassDB::bind_method(
        D_METHOD("set_starvation_growth", "value"),
        &NetwDisplayHandle::set_starvation_growth
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "starvation_growth"),
        "set_starvation_growth",
        "get_starvation_growth"
    );
    ClassDB::bind_method(
        D_METHOD("get_floor_smoothing"),
        &NetwDisplayHandle::get_floor_smoothing
    );
    ClassDB::bind_method(
        D_METHOD("set_floor_smoothing", "value"),
        &NetwDisplayHandle::set_floor_smoothing
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "floor_smoothing"),
        "set_floor_smoothing",
        "get_floor_smoothing"
    );
    ClassDB::bind_method(
        D_METHOD("get_starvation_grace_frames"),
        &NetwDisplayHandle::get_starvation_grace_frames
    );
    ClassDB::bind_method(
        D_METHOD("set_starvation_grace_frames", "value"),
        &NetwDisplayHandle::set_starvation_grace_frames
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "starvation_grace_frames"),
        "set_starvation_grace_frames",
        "get_starvation_grace_frames"
    );
    ClassDB::bind_method(
        D_METHOD("get_trace_interval"),
        &NetwDisplayHandle::get_trace_interval
    );
    ClassDB::bind_method(
        D_METHOD("set_trace_interval", "value"),
        &NetwDisplayHandle::set_trace_interval
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "trace_interval"),
        "set_trace_interval",
        "get_trace_interval"
    );
    ClassDB::bind_method(
        D_METHOD("get_display_lag"),
        &NetwDisplayHandle::get_display_lag
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "display_lag"),
        "",
        "get_display_lag"
    );
    ClassDB::bind_method(
        D_METHOD("displayed_authoring_tick"),
        &NetwDisplayHandle::displayed_authoring_tick
    );
    ClassDB::bind_method(
        D_METHOD("get_buffer", "property"),
        &NetwDisplayHandle::get_buffer
    );
    ClassDB::bind_method(D_METHOD("reset"), &NetwDisplayHandle::reset);
    ClassDB::bind_method(D_METHOD("entity"), &NetwDisplayHandle::entity);
}

Ref<NetwDisplayHandle> build_display_handle(Object *p_entity) {
    Ref<NetwDisplayHandle> made;
    made.instantiate();
    made->bind(Object::cast_to<NetwEntity>(p_entity));
    return made;
}

} // namespace netw
