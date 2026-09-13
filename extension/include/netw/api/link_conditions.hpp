#pragma once

#include "godot/multiplayer.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"

namespace netw {

bool link_shaping_allowed();

class NetwLinkConditions : public godot::Resource {
    GDCLASS(NetwLinkConditions, godot::Resource)

    bool simulate_lag = false;
    double one_way_delay_min = 0.0;
    double one_way_delay_max = 100.0;
    double lag_packet_loss_percent = 0.0;

protected:
    static void _bind_methods();

public:
    godot::Ref<godot::MultiplayerPeer> wrap_peer(
        const godot::Ref<godot::MultiplayerPeer> &p_base
    ) const;

    void set_simulate_lag(bool p_simulate);
    bool get_simulate_lag() const;
    void set_one_way_delay_min(double p_ms);
    double get_one_way_delay_min() const;
    void set_one_way_delay_max(double p_ms);
    double get_one_way_delay_max() const;
    void set_lag_packet_loss_percent(double p_percent);
    double get_lag_packet_loss_percent() const;

    void copy_values_from(const NetwLinkConditions &p_source);
};

} // namespace netw
