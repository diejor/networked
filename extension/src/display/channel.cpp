#include "netw/display/channel.hpp"

#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::display {

void Channel::copy_shape_from(const Channel &p_other) {
    spec = p_other.spec;
    source = p_other.source;
    source_prop = p_other.source_prop;
    target = p_other.target;
    target_prop = p_other.target_prop;
    self_feedback = p_other.self_feedback;
    authoring_ticks = authoring_ticks || p_other.authoring_ticks;
}

void Channel::write(const Variant &p_value) {
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
    if (!port.is_bound()) {
        return;
    }
    if (port.write(p_value) != Port::WRITE_REFUSED) {
        return;
    }
    NETW_WARN_COND(
        !refused_global,
        sys::INTERPOLATION,
        "'%s' is a global-space channel with no global setter for %s, so it "
        "is written locally and composes with the body",
        String(port.get_target_prop()),
        String(Variant::get_type_name(p_value.get_type()))
    );
    refused_global = true;
}

Variant Channel::current_source_value() {
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

void Channel::snap(const Variant &p_value) {
    write(p_value);
    history.clear();
    offset.clear();
    last_written = p_value;
}

void Channel::set_source_obj(Object *p_object) {
    source.bind(p_object);
}

Variant Channel::get_source_obj() {
    return gd::held(source.resolve(sys::INTERPOLATION));
}

void Channel::set_target_obj(Object *p_object) {
    target.bind(p_object);
}

Variant Channel::get_target_obj() {
    return gd::held(target.resolve(sys::INTERPOLATION));
}

} // namespace netw::display
