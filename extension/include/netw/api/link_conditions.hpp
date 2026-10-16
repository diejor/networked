#pragma once

#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include <cstdint>

namespace netw {

bool link_shaping_allowed();

class NetwLinkConditions : public godot::Resource {
    GDCLASS(NetwLinkConditions, godot::Resource)

    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    double loss_chance = 0.0;
    double reorder_chance = 0.0;
    double duplicate_chance = 0.0;
    double stall_chance = 0.0;
    double stall_ms = 0.0;
    int64_t seed = 0;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwLinkConditions> wifi();
    static godot::Ref<NetwLinkConditions> mobile_4g();
    static godot::Ref<NetwLinkConditions> poor_3g();
    static godot::Ref<NetwLinkConditions> satellite();

    void set_latency_ms(double p_value);
    double get_latency_ms() const;
    void set_jitter_ms(double p_value);
    double get_jitter_ms() const;
    void set_loss_chance(double p_value);
    double get_loss_chance() const;
    void set_reorder_chance(double p_value);
    double get_reorder_chance() const;
    void set_duplicate_chance(double p_value);
    double get_duplicate_chance() const;
    void set_stall_chance(double p_value);
    double get_stall_chance() const;
    void set_stall_ms(double p_value);
    double get_stall_ms() const;
    void set_seed(int64_t p_value);
    int64_t get_seed() const;
};

} // namespace netw
