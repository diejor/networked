#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/display/decl.hpp"

namespace netw {

class NetwEntity;

class NetwDisplay : public godot::RefCounted, public enums::NetwDisplay {
    GDCLASS(NetwDisplay, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    NetwDisplay();

    void bind(NetwEntity *p_entity);
    godot::Ref<NetwEntity> entity() const;
    display::Decl *declaration() const;

    godot::NodePath get_visual_root() const;
    void set_visual_root(const godot::NodePath &p_value);
    NetwDisplay::DisplayRole get_display_role() const;
    void set_display_role(NetwDisplay::DisplayRole p_value);
    NetwDisplay::LiveMode get_live_mode() const;
    void set_live_mode(NetwDisplay::LiveMode p_value);
    double get_live_smooth_time() const;
    void set_live_smooth_time(double p_value);
    double get_chase_glide_time() const;
    void set_chase_glide_time(double p_value);
    NetwDisplay::TimelineMode get_timeline_mode() const;
    void set_timeline_mode(NetwDisplay::TimelineMode p_value);
    int64_t get_max_forecast_ticks() const;
    void set_max_forecast_ticks(int64_t p_value);
    bool get_enable_smart_dilation() const;
    void set_enable_smart_dilation(bool p_value);
    double get_lag_adapt_rate() const;
    void set_lag_adapt_rate(double p_value);
    double get_floor_smoothing() const;
    void set_floor_smoothing(double p_value);
    int64_t get_trace_interval() const;
    void set_trace_interval(int64_t p_value);

    double get_display_lag() const;
    NetwDisplay::DisplayPump get_pump_mode() const;
    godot::Variant get_track_stat(
        const godot::StringName &p_property,
        const godot::StringName &p_stat
    ) const;
    int64_t displayed_authoring_tick() const;
    void reset();

private:
    godot::ObjectID entity_id;

    NetwMultiplayer *core() const;
    godot::RID entity_rid() const;
    void write(int p_param, const godot::Variant &p_value);
    godot::Variant read(int p_param) const;
};

godot::Ref<NetwDisplay> build_display_handle(godot::Object *p_entity);

} // namespace netw
