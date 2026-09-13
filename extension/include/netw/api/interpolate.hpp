#pragma once

#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwInterpolate : public godot::Resource {
    GDCLASS(NetwInterpolate, godot::Resource)

public:
    enum Mode {
        MODE_NONE = 0,
        MODE_LERP = 1,
        MODE_ANGLE = 2,
        MODE_SLERP = 3,
    };

    enum Tail {
        TAIL_AUTO = 0,
        TAIL_HOLD = 1,
    };

private:
    Mode mode = MODE_LERP;
    double smoothing = 0.05;
    double snap_distance = 0.0;
    godot::StringName target;
    Tail forecast_tail = TAIL_AUTO;
    godot::StringName project_channel;

protected:
    static void _bind_methods();

public:
    void set_mode(Mode value) {
        mode = value;
    }
    Mode get_mode() const {
        return mode;
    }

    void set_smoothing(double seconds) {
        smoothing = seconds;
    }
    double get_smoothing() const {
        return smoothing;
    }

    void set_snap_distance(double distance) {
        snap_distance = distance;
    }
    double get_snap_distance() const {
        return snap_distance;
    }

    void set_target(const godot::StringName &property) {
        target = property;
    }
    godot::StringName get_target() const {
        return target;
    }

    void set_forecast_tail(Tail value) {
        forecast_tail = value;
    }
    Tail get_forecast_tail() const {
        return forecast_tail;
    }

    void set_project_channel(const godot::StringName &channel) {
        project_channel = channel;
    }
    godot::StringName get_project_channel() const {
        return project_channel;
    }

    double smoothing_weight(double p_frame_delta) const;

    godot::Ref<NetwInterpolate> none();
    godot::Ref<NetwInterpolate> lerp();
    godot::Ref<NetwInterpolate> angle();
    godot::Ref<NetwInterpolate> slerp();
    godot::Ref<NetwInterpolate> smooth(double seconds);
    godot::Ref<NetwInterpolate> snap_at(double distance);
    godot::Ref<NetwInterpolate> to(const godot::StringName &property);
    godot::Ref<NetwInterpolate> project_by(const godot::StringName &channel);
    godot::Ref<NetwInterpolate> hold();

    bool is_same_spec(const godot::Ref<NetwInterpolate> &other) const;
    bool supports_type(int type) const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwInterpolate::Mode);
VARIANT_ENUM_CAST(netw::NetwInterpolate::Tail);
