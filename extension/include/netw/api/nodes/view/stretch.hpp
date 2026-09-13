#pragma once

#include "godot/variant.hpp"

namespace netw::view {

enum StretchMode {
    STRETCH_MODE_INHERIT = 0,
    STRETCH_MODE_DISABLED = 1,
    STRETCH_MODE_CANVAS_ITEMS = 2,
    STRETCH_MODE_VIEWPORT = 3,
};

enum StretchAspect {
    STRETCH_ASPECT_INHERIT = 0,
    STRETCH_ASPECT_IGNORE = 1,
    STRETCH_ASPECT_KEEP = 2,
    STRETCH_ASPECT_KEEP_WIDTH = 3,
    STRETCH_ASPECT_KEEP_HEIGHT = 4,
    STRETCH_ASPECT_EXPAND = 5,
};

enum StretchScaleMode {
    STRETCH_SCALE_MODE_INHERIT = 0,
    STRETCH_SCALE_MODE_FRACTIONAL = 1,
    STRETCH_SCALE_MODE_INTEGER = 2,
};

struct Stretch {
    StretchMode mode = STRETCH_MODE_INHERIT;
    StretchAspect aspect = STRETCH_ASPECT_INHERIT;
    StretchScaleMode scale_mode = STRETCH_SCALE_MODE_INHERIT;
    double scale = 0.0;
    godot::Vector2i design_size;
};

struct Layout {
    godot::Vector2i target_size;
    godot::Vector2i size_2d_override;
    bool override_stretch = false;
    godot::Rect2 inner_rect;
};

Stretch stretch_from_project();

Stretch stretch_over(const Stretch &p_base, const Stretch &p_override);

Stretch stretch_resolve(const Stretch &p_override);

Layout layout_compute(
    const Stretch &p_stretch,
    const godot::Vector2 &p_control_size
);

} // namespace netw::view
