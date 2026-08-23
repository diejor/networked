#pragma once

#include "godot/ref_counted.hpp"
#include "netw/api/clock_handle.hpp"

namespace netw {

class NetwDisplayTiming : public godot::RefCounted {
    GDCLASS(NetwDisplayTiming, godot::RefCounted)

private:
    int tick = 0;
    int display_tick = 0;
    double tick_factor = 0.0;
    double ticktime = 0.0;
    int display_offset = 0;
    int recommended_display_offset = 0;
    double frame_delta = 0.0;
    double frame_ticks = 0.0;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwDisplayTiming> capture(
        const godot::Ref<NetwClockHandle> &p_clock,
        double p_frame_delta
    );

    void set_tick(int p_value) { tick = p_value; }
    int get_tick() const { return tick; }

    void set_display_tick(int p_value) { display_tick = p_value; }
    int get_display_tick() const { return display_tick; }

    void set_tick_factor(double p_value) { tick_factor = p_value; }
    double get_tick_factor() const { return tick_factor; }

    void set_ticktime(double p_value) { ticktime = p_value; }
    double get_ticktime() const { return ticktime; }

    void set_display_offset(int p_value) { display_offset = p_value; }
    int get_display_offset() const { return display_offset; }

    void set_recommended_display_offset(int p_value) {
        recommended_display_offset = p_value;
    }
    int get_recommended_display_offset() const {
        return recommended_display_offset;
    }

    void set_frame_delta(double p_value) { frame_delta = p_value; }
    double get_frame_delta() const { return frame_delta; }

    void set_frame_ticks(double p_value) { frame_ticks = p_value; }
    double get_frame_ticks() const { return frame_ticks; }
};

} // namespace netw
