#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::display {

struct Offset {
    godot::Variant residual;
    bool armed = false;

    void clear();
    bool is_held() const;

    void absorb(const godot::Variant &p_recovery, double p_limit);

    godot::Variant apply(
        const godot::Variant &p_target,
        double p_glide,
        double p_limit,
        const godot::Variant &p_displayed,
        int64_t p_mode
    );
};

} // namespace netw::display
