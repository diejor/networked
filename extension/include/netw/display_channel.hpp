#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/display_history.hpp"
#include "netw/display_offset.hpp"
#include "netw/interpolate.hpp"
#include "netw/object_port.hpp"

namespace netw {

using namespace godot;

class NetwDisplayChannel : public RefCounted {
    GDCLASS(NetwDisplayChannel, RefCounted)

private:
    StringName name;
    StringName state_key;
    Ref<NetwInterpolate> spec;
    Ref<NetwDisplayHistory> history;
    Ref<NetwDisplayOffset> offset;
    Ref<RefCounted> output;
    ObjectPort source;
    StringName source_prop;
    ObjectPort target;
    StringName target_prop;
    bool authoring_ticks = false;
    bool self_feedback = false;
    Variant last_written;

protected:
    static void _bind_methods();

public:
    NetwDisplayChannel();

    void copy_shape_from(const Ref<NetwDisplayChannel> &p_other);
    void snap(const Variant &p_value);

    void set_name(const StringName &p_name) { name = p_name; }
    StringName get_name() const { return name; }

    void set_state_key(const StringName &p_key) { state_key = p_key; }
    StringName get_state_key() const { return state_key; }

    void set_spec(const Ref<NetwInterpolate> &p_spec) { spec = p_spec; }
    Ref<NetwInterpolate> get_spec() const { return spec; }

    void set_history(const Ref<NetwDisplayHistory> &p_history) {
        history = p_history;
    }
    Ref<NetwDisplayHistory> get_history() const { return history; }

    void set_offset(const Ref<NetwDisplayOffset> &p_offset) {
        offset = p_offset;
    }
    Ref<NetwDisplayOffset> get_offset() const { return offset; }

    void set_output(const Ref<RefCounted> &p_output) { output = p_output; }
    Ref<RefCounted> get_output() const { return output; }

    void set_source_obj(Object *p_object);
    Variant get_source_obj();

    void set_source_prop(const StringName &p_prop) { source_prop = p_prop; }
    StringName get_source_prop() const { return source_prop; }

    void set_target_obj(Object *p_object);
    Variant get_target_obj();

    void set_target_prop(const StringName &p_prop) { target_prop = p_prop; }
    StringName get_target_prop() const { return target_prop; }

    void set_authoring_ticks(bool p_value) { authoring_ticks = p_value; }
    bool get_authoring_ticks() const { return authoring_ticks; }

    void set_self_feedback(bool p_value) { self_feedback = p_value; }
    bool get_self_feedback() const { return self_feedback; }

    void set_last_written(const Variant &p_value) { last_written = p_value; }
    Variant get_last_written() const { return last_written; }
};

} // namespace netw
