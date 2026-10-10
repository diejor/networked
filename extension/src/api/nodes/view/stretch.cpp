#include "netw/api/nodes/view/stretch.hpp"

#include "godot/math.hpp"

using namespace godot;

namespace netw::view {

namespace {

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

StretchMode mode_of(Window::ContentScaleMode p_mode) {
    switch (p_mode) {
        case Window::CONTENT_SCALE_MODE_CANVAS_ITEMS:
            return STRETCH_MODE_CANVAS_ITEMS;
        case Window::CONTENT_SCALE_MODE_VIEWPORT:
            return STRETCH_MODE_VIEWPORT;
        default:
            return STRETCH_MODE_DISABLED;
    }
}

StretchAspect aspect_of(Window::ContentScaleAspect p_aspect) {
    switch (p_aspect) {
        case Window::CONTENT_SCALE_ASPECT_IGNORE:
            return STRETCH_ASPECT_IGNORE;
        case Window::CONTENT_SCALE_ASPECT_KEEP_WIDTH:
            return STRETCH_ASPECT_KEEP_WIDTH;
        case Window::CONTENT_SCALE_ASPECT_KEEP_HEIGHT:
            return STRETCH_ASPECT_KEEP_HEIGHT;
        case Window::CONTENT_SCALE_ASPECT_EXPAND:
            return STRETCH_ASPECT_EXPAND;
        default:
            return STRETCH_ASPECT_KEEP;
    }
}

StretchScaleMode scale_mode_of(Window::ContentScaleStretch p_stretch) {
    if (p_stretch == Window::CONTENT_SCALE_STRETCH_INTEGER) {
        return STRETCH_SCALE_MODE_INTEGER;
    }
    return STRETCH_SCALE_MODE_FRACTIONAL;
}

} // namespace

Stretch stretch_from_window(const Window *p_window) {
    Stretch out;
    if (p_window == nullptr) {
        return out;
    }
    out.mode = mode_of(p_window->get_content_scale_mode());
    out.aspect = aspect_of(p_window->get_content_scale_aspect());
    out.scale_mode = scale_mode_of(p_window->get_content_scale_stretch());
    out.scale = p_window->get_content_scale_factor();
    out.design_size = p_window->get_content_scale_size();
    return out;
}

Transform2D inner_rect_to_target_pixels(
    const Rect2 &p_inner_rect,
    const Vector2i &p_target_size
) {
    if (p_inner_rect.size.x <= 0.0f || p_inner_rect.size.y <= 0.0f) {
        return Transform2D();
    }
    const Vector2 scale = Vector2(p_target_size) / p_inner_rect.size;
    return Transform2D().scaled(scale).translated(
        -p_inner_rect.position * scale
    );
}

Stretch stretch_filling(const Stretch &p_stretch) {
    Stretch out = p_stretch;
    if (out.mode == STRETCH_MODE_VIEWPORT) {
        out.mode = STRETCH_MODE_CANVAS_ITEMS;
    }
    if (out.aspect == STRETCH_ASPECT_KEEP) {
        out.aspect = STRETCH_ASPECT_EXPAND;
    }
    out.scale_mode = STRETCH_SCALE_MODE_FRACTIONAL;
    return out;
}

Layout layout_compute(const Stretch &p_stretch, const Vector2 &p_control_size) {
    if (p_control_size.x <= 0.0f || p_control_size.y <= 0.0f) {
        return degenerate(p_control_size);
    }
    if (p_stretch.mode == STRETCH_MODE_DISABLED || p_stretch.design_size.x <= 0
        || p_stretch.design_size.y <= 0) {
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
