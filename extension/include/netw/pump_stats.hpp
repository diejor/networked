#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwPumpStats : public godot::RefCounted {
    GDCLASS(NetwPumpStats, godot::RefCounted)

private:
    int32_t runtimes = 0;
    int32_t starving = 0;
    int32_t sleeping = 0;
    int32_t projecting = 0;
    int32_t snaps = 0;
    double max_display_lag = 0.0;
    double max_forecast_age = 0.0;

protected:
    static void _bind_methods();

public:
    void reset();
    void merge(const godot::Ref<NetwPumpStats> &other);

    void set_runtimes(int value) { runtimes = value; }
    int get_runtimes() const { return runtimes; }
    void set_starving(int value) { starving = value; }
    int get_starving() const { return starving; }
    void set_sleeping(int value) { sleeping = value; }
    int get_sleeping() const { return sleeping; }
    void set_projecting(int value) { projecting = value; }
    int get_projecting() const { return projecting; }
    void set_snaps(int value) { snaps = value; }
    int get_snaps() const { return snaps; }
    void set_max_display_lag(double value) { max_display_lag = value; }
    double get_max_display_lag() const { return max_display_lag; }
    void set_max_forecast_age(double value) { max_forecast_age = value; }
    double get_max_forecast_age() const { return max_forecast_age; }
};

} // namespace netw
