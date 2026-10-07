#pragma once

#include "godot/scene_tree.hpp"
#include "godot/variant.hpp"

namespace netw::view {

enum StretchMode {
    STRETCH_MODE_DISABLED = 0,
    STRETCH_MODE_CANVAS_ITEMS = 1,
    STRETCH_MODE_VIEWPORT = 2,
};

enum StretchAspect {
    STRETCH_ASPECT_IGNORE = 0,
    STRETCH_ASPECT_KEEP = 1,
    STRETCH_ASPECT_KEEP_WIDTH = 2,
    STRETCH_ASPECT_KEEP_HEIGHT = 3,
    STRETCH_ASPECT_EXPAND = 4,
};

enum StretchScaleMode {
    STRETCH_SCALE_MODE_FRACTIONAL = 0,
    STRETCH_SCALE_MODE_INTEGER = 1,
};

struct Stretch {
    StretchMode mode = STRETCH_MODE_DISABLED;
    StretchAspect aspect = STRETCH_ASPECT_KEEP;
    StretchScaleMode scale_mode = STRETCH_SCALE_MODE_FRACTIONAL;
    double scale = 1.0;
    godot::Vector2i design_size;
};

struct Layout {
    godot::Vector2i target_size;
    godot::Vector2i size_2d_override;
    bool override_stretch = false;
    godot::Rect2 inner_rect;
};

Stretch stretch_from_window(const godot::Window *p_window);

Stretch stretch_filling(const Stretch &p_stretch);

Layout layout_compute(
    const Stretch &p_stretch,
    const godot::Vector2 &p_control_size
);

godot::Transform2D inner_rect_to_target_pixels(
    const godot::Rect2 &p_inner_rect,
    const godot::Vector2i &p_target_size
);

} // namespace netw::view
