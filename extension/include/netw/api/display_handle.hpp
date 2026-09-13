#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/ring_buffer.hpp"
#include "netw/display/decl.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;

class NetwDisplayHandle : public godot::RefCounted {
    GDCLASS(NetwDisplayHandle, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    NetwDisplayHandle();

    void bind(NetwEntity *p_entity);
    godot::Ref<NetwEntity> entity() const;
    display::Decl *declaration() const;

    godot::NodePath get_visual_root() const;
    void set_visual_root(const godot::NodePath &p_value);
    int64_t get_display_role() const;
    void set_display_role(int64_t p_value);
    int64_t get_predicted_mode() const;
    void set_predicted_mode(int64_t p_value);
    double get_predicted_smooth_time() const;
    void set_predicted_smooth_time(double p_value);
    bool get_enable_smart_dilation() const;
    void set_enable_smart_dilation(bool p_value);
    double get_max_extra_dilation() const;
    void set_max_extra_dilation(double p_value);
    double get_lag_adapt_rate() const;
    void set_lag_adapt_rate(double p_value);
    double get_starvation_growth() const;
    void set_starvation_growth(double p_value);
    double get_floor_smoothing() const;
    void set_floor_smoothing(double p_value);
    int64_t get_starvation_grace_frames() const;
    void set_starvation_grace_frames(int64_t p_value);
    int64_t get_trace_interval() const;
    void set_trace_interval(int64_t p_value);

    double get_display_lag() const;
    int64_t displayed_authoring_tick() const;
    godot::Ref<NetwRingBuffer> get_buffer(
        const godot::StringName &p_property
    ) const;
    void reset();

private:
    godot::ObjectID entity_id;

    NetwMultiplayer *core() const;
    godot::RID entity_rid() const;
    void write(int p_param, const godot::Variant &p_value);
    godot::Variant read(int p_param) const;
};

godot::Ref<NetwDisplayHandle> build_display_handle(godot::Object *p_entity);

} // namespace netw
