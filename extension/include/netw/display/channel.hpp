#pragma once

#include "godot/callable.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/display/history.hpp"
#include "netw/display/offset.hpp"
#include "netw/display/port.hpp"
#include "netw/object_port.hpp"

namespace netw::display {

class Channel {
private:
    godot::StringName name;
    godot::StringName state_key;
    godot::Ref<NetwInterpolate> spec;
    History history;
    Offset offset;
    godot::RID entity;
    godot::Callable door;
    godot::Callable output;
    Port port;
    bool refused_global = false;
    ObjectPort source;
    godot::StringName source_prop;
    ObjectPort target;
    godot::StringName target_prop;
    bool authoring_ticks = false;
    bool self_feedback = false;
    godot::Variant last_written;

public:
    void copy_shape_from(const Channel &p_other);
    void snap(const godot::Variant &p_value);

    void set_name(const godot::StringName &p_name) {
        name = p_name;
    }
    godot::StringName get_name() const {
        return name;
    }

    void set_state_key(const godot::StringName &p_key) {
        state_key = p_key;
    }
    godot::StringName get_state_key() const {
        return state_key;
    }

    void set_spec(const godot::Ref<NetwInterpolate> &p_spec) {
        spec = p_spec;
    }
    godot::Ref<NetwInterpolate> get_spec() const {
        return spec;
    }

    History &display_history() {
        return history;
    }
    const History &display_history() const {
        return history;
    }

    Offset &render_offset() {
        return offset;
    }
    const Offset &render_offset() const {
        return offset;
    }

    void set_entity(const godot::RID &p_entity) {
        entity = p_entity;
    }
    godot::RID get_entity() const {
        return entity;
    }

    void set_door(const godot::Callable &p_door) {
        door = p_door;
    }
    godot::Callable get_door() const {
        return door;
    }

    void set_output(const godot::Callable &p_output) {
        output = p_output;
    }
    godot::Callable get_output() const {
        return output;
    }

    Port &display_port() {
        return port;
    }
    const Port &display_port() const {
        return port;
    }

    void write(const godot::Variant &p_value);
    godot::Variant current_source_value();

    void set_source_obj(godot::Object *p_object);
    godot::Variant get_source_obj();

    void set_source_prop(const godot::StringName &p_prop) {
        source_prop = p_prop;
    }
    godot::StringName get_source_prop() const {
        return source_prop;
    }

    void set_target_obj(godot::Object *p_object);
    godot::Variant get_target_obj();

    void set_target_prop(const godot::StringName &p_prop) {
        target_prop = p_prop;
    }
    godot::StringName get_target_prop() const {
        return target_prop;
    }

    void set_authoring_ticks(bool p_value) {
        authoring_ticks = p_value;
    }
    bool get_authoring_ticks() const {
        return authoring_ticks;
    }

    void set_self_feedback(bool p_value) {
        self_feedback = p_value;
    }
    bool get_self_feedback() const {
        return self_feedback;
    }

    void set_last_written(const godot::Variant &p_value) {
        last_written = p_value;
    }
    godot::Variant get_last_written() const {
        return last_written;
    }
};

} // namespace netw::display
