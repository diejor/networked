#include "netw/api/nodes/view/stretch.hpp"

#include "godot/math.hpp"
#include "godot/project_settings.hpp"

using namespace godot;

namespace netw::view {

namespace {

const char *SETTING_MODE = "display/window/stretch/mode";
const char *SETTING_ASPECT = "display/window/stretch/aspect";
const char *SETTING_SCALE = "display/window/stretch/scale";
const char *SETTING_SCALE_MODE = "display/window/stretch/scale_mode";
const char *SETTING_WIDTH = "display/window/size/viewport_width";
const char *SETTING_HEIGHT = "display/window/size/viewport_height";

Variant setting(const char *p_name, const Variant &p_default) {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return p_default;
    }
    return settings->get_setting(String(p_name), p_default);
}

StretchMode mode_of(const String &p_text) {
    if (p_text == "canvas_items" || p_text == "2d") {
        return STRETCH_MODE_CANVAS_ITEMS;
    }
    if (p_text == "viewport") {
        return STRETCH_MODE_VIEWPORT;
    }
    return STRETCH_MODE_DISABLED;
}

StretchAspect aspect_of(const String &p_text) {
    if (p_text == "ignore") {
        return STRETCH_ASPECT_IGNORE;
    }
    if (p_text == "keep_width") {
        return STRETCH_ASPECT_KEEP_WIDTH;
    }
    if (p_text == "keep_height") {
        return STRETCH_ASPECT_KEEP_HEIGHT;
    }
    if (p_text == "expand") {
        return STRETCH_ASPECT_EXPAND;
    }
    return STRETCH_ASPECT_KEEP;
}

StretchScaleMode scale_mode_of(const String &p_text) {
    if (p_text == "integer") {
        return STRETCH_SCALE_MODE_INTEGER;
    }
    return STRETCH_SCALE_MODE_FRACTIONAL;
}

Rect2 centered(const Vector2 &p_size, const Vector2 &p_control_size) {
    return Rect2(((p_control_size - p_size) * 0.5f).floor(), p_size);
}

Rect2 aspect_fit(const Vector2 &p_design, const Vector2 &p_control_size) {
    const real_t design_aspect = p_design.x / p_design.y;
    const real_t control_aspect = p_control_size.x / p_control_size.y;
    Vector2 fit = p_control_size;
    if (control_aspect > design_aspect) {
        fit = Vector2(p_control_size.y * design_aspect, p_control_size.y);
    } else {
        fit = Vector2(p_control_size.x, p_control_size.x / design_aspect);
    }
    return centered(fit, p_control_size);
}

Layout degenerate(const Vector2 &p_control_size) {
    Layout out;
    out.target_size = Vector2i(
        MAX(1, int(Math::ceil(p_control_size.x))),
        MAX(1, int(Math::ceil(p_control_size.y)))
    );
    out.inner_rect = Rect2(Vector2(), p_control_size);
    return out;
}

} // namespace

Stretch stretch_from_project() {
    Stretch out;
    out.mode = mode_of(String(setting(SETTING_MODE, "disabled")));
    out.aspect = aspect_of(String(setting(SETTING_ASPECT, "keep")));
    out.scale_mode
        = scale_mode_of(String(setting(SETTING_SCALE_MODE, "fractional")));
    out.scale = double(setting(SETTING_SCALE, 1.0));
    out.design_size = Vector2i(
        int(setting(SETTING_WIDTH, 0)),
        int(setting(SETTING_HEIGHT, 0))
    );
    return out;
}

Stretch stretch_over(const Stretch &p_base, const Stretch &p_override) {
    Stretch out = p_base;
    if (p_override.mode != STRETCH_MODE_INHERIT) {
        out.mode = p_override.mode;
    }
    if (p_override.aspect != STRETCH_ASPECT_INHERIT) {
        out.aspect = p_override.aspect;
    }
    if (p_override.scale_mode != STRETCH_SCALE_MODE_INHERIT) {
        out.scale_mode = p_override.scale_mode;
    }
    if (p_override.scale > 0.0) {
        out.scale = p_override.scale;
    }
    if (p_override.design_size.x > 0 && p_override.design_size.y > 0) {
        out.design_size = p_override.design_size;
    }
    return out;
}

Stretch stretch_resolve(const Stretch &p_override) {
    return stretch_over(stretch_from_project(), p_override);
}

Layout layout_compute(const Stretch &p_stretch, const Vector2 &p_control_size) {
    if (p_control_size.x <= 0.0f || p_control_size.y <= 0.0f) {
        return degenerate(p_control_size);
    }
    if (p_stretch.mode == STRETCH_MODE_DISABLED
        || p_stretch.mode == STRETCH_MODE_INHERIT
        || p_stretch.design_size.x <= 0 || p_stretch.design_size.y <= 0) {
        Layout out;
        out.target_size = Vector2i(p_control_size.ceil());
        out.inner_rect = Rect2(Vector2(), p_control_size);
        return out;
    }

    const real_t scale = MAX(real_t(p_stretch.scale), real_t(0.0001));
    Vector2 design = Vector2(p_stretch.design_size) / scale;
    Rect2 inner = Rect2(Vector2(), p_control_size);

    switch (p_stretch.aspect) {
        case STRETCH_ASPECT_KEEP:
            inner = aspect_fit(design, p_control_size);
            break;
        case STRETCH_ASPECT_KEEP_WIDTH:
            design = Vector2(
                design.x,
                design.x * p_control_size.y / p_control_size.x
            );
            break;
        case STRETCH_ASPECT_KEEP_HEIGHT:
            design = Vector2(
                design.y * p_control_size.x / p_control_size.y,
                design.y
            );
            break;
        case STRETCH_ASPECT_EXPAND: {
            const real_t cover
                = MIN(p_control_size.x / design.x, p_control_size.y / design.y);
            design = p_control_size / cover;
            break;
        }
        default:
            break;
    }

    const bool snaps = p_stretch.scale_mode == STRETCH_SCALE_MODE_INTEGER
        && p_stretch.mode == STRETCH_MODE_VIEWPORT
        && p_stretch.aspect != STRETCH_ASPECT_IGNORE
        && p_stretch.aspect != STRETCH_ASPECT_EXPAND && design.x > 0.0f
        && design.y > 0.0f;
    if (snaps) {
        const real_t ratio
            = MIN(inner.size.x / design.x, inner.size.y / design.y);
        const real_t step = MAX(real_t(1.0), Math::floor(ratio));
        inner = centered(design * step, p_control_size);
    }

    const Vector2i design_i = Vector2i(design.round());

    Layout out;
    switch (p_stretch.mode) {
        case STRETCH_MODE_CANVAS_ITEMS:
            out.target_size = Vector2i(inner.size.ceil());
            out.size_2d_override = design_i;
            out.override_stretch = true;
            out.inner_rect = inner;
            break;
        case STRETCH_MODE_VIEWPORT:
            out.target_size = design_i;
            out.inner_rect = inner;
            break;
        default:
            out.target_size = Vector2i(p_control_size.ceil());
            out.inner_rect = Rect2(Vector2(), p_control_size);
            break;
    }
    return out;
}

} // namespace netw::view
