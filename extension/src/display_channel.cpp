#include "netw/display_channel.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

void NetwDisplayChannel::copy_shape_from(
    const Ref<NetwDisplayChannel> &p_other
) {
    if (p_other.is_null()) {
        return;
    }
    spec = p_other->spec;
    source = p_other->source;
    source_prop = p_other->source_prop;
    target = p_other->target;
    target_prop = p_other->target_prop;
    self_feedback = p_other->self_feedback;
    authoring_ticks = authoring_ticks || p_other->authoring_ticks;
}

void NetwDisplayChannel::write(const Variant &p_value) {
    if (door.is_valid()) {
        const Variant verdict = door.call(entity, target_prop, p_value);
        if (int64_t(verdict) != ERR_DOES_NOT_EXIST) {
            return;
        }
    }
    if (output.is_valid()) {
        output.call(p_value);
        return;
    }
    if (port.is_null()) {
        return;
    }
    if (port->write(p_value) != NetwDisplayPort::WRITE_REFUSED) {
        return;
    }
    NETW_WARN_COND(
        !refused_global,
        sys::INTERPOLATION,
        "'%s' is a global-space channel with no global setter for %s, so it "
        "is written locally and composes with the body",
        String(port->get_target_prop()),
        String(Variant::get_type_name(p_value.get_type()))
    );
    refused_global = true;
}

Variant NetwDisplayChannel::current_source_value() {
    Object *from = source.resolve(sys::INTERPOLATION);
    if (from != nullptr) {
        return from->get(source_prop);
    }
    Object *to = target.resolve(sys::INTERPOLATION);
    if (to != nullptr) {
        return to->get(target_prop);
    }
    return Variant();
}

void NetwDisplayChannel::snap(const Variant &p_value) {
    write(p_value);
    if (history.is_valid()) {
        history->clear();
    }
    offset.clear();
    last_written = p_value;
}

void NetwDisplayChannel::set_source_obj(Object *p_object) {
    source.bind(p_object);
}

Variant NetwDisplayChannel::get_source_obj() {
    return gd::held(source.resolve(sys::INTERPOLATION));
}

void NetwDisplayChannel::set_target_obj(Object *p_object) {
    target.bind(p_object);
}

Variant NetwDisplayChannel::get_target_obj() {
    return gd::held(target.resolve(sys::INTERPOLATION));
}

void NetwDisplayChannel::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("copy_shape_from", "other"),
        &NetwDisplayChannel::copy_shape_from
    );
    ClassDB::bind_method(D_METHOD("snap", "value"), &NetwDisplayChannel::snap);
    ClassDB::bind_method(
        D_METHOD("write", "value"),
        &NetwDisplayChannel::write
    );
    ClassDB::bind_method(
        D_METHOD("current_source_value"),
        &NetwDisplayChannel::current_source_value
    );

#define NETW_CHANNEL_PROPERTY(m_type, m_name) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, #m_name), \
        &NetwDisplayChannel::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwDisplayChannel::get_##m_name \
    ); \
    ADD_PROPERTY( \
        PropertyInfo(m_type, #m_name), \
        "set_" #m_name, \
        "get_" #m_name \
    )

    NETW_CHANNEL_PROPERTY(Variant::STRING_NAME, name);
    NETW_CHANNEL_PROPERTY(Variant::STRING_NAME, state_key);
    NETW_CHANNEL_PROPERTY(Variant::OBJECT, spec);
    NETW_CHANNEL_PROPERTY(Variant::OBJECT, history);
    NETW_CHANNEL_PROPERTY(Variant::RID, entity);
    NETW_CHANNEL_PROPERTY(Variant::CALLABLE, door);
    NETW_CHANNEL_PROPERTY(Variant::CALLABLE, output);
    NETW_CHANNEL_PROPERTY(Variant::OBJECT, port);
    NETW_CHANNEL_PROPERTY(Variant::OBJECT, source_obj);
    NETW_CHANNEL_PROPERTY(Variant::STRING_NAME, source_prop);
    NETW_CHANNEL_PROPERTY(Variant::OBJECT, target_obj);
    NETW_CHANNEL_PROPERTY(Variant::STRING_NAME, target_prop);
    NETW_CHANNEL_PROPERTY(Variant::BOOL, authoring_ticks);
    NETW_CHANNEL_PROPERTY(Variant::BOOL, self_feedback);
    ClassDB::bind_method(
        D_METHOD("set_last_written", "last_written"),
        &NetwDisplayChannel::set_last_written
    );
    ClassDB::bind_method(
        D_METHOD("get_last_written"),
        &NetwDisplayChannel::get_last_written
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "last_written",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "set_last_written",
        "get_last_written"
    );

#undef NETW_CHANNEL_PROPERTY
}

} // namespace netw
