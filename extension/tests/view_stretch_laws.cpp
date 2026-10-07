#include "support/netw_test.h"

#include "netw/api/nodes/view/stretch.hpp"

namespace TestViewStretchLaws {

using namespace godot;
using netw::view::Layout;
using netw::view::Stretch;

Stretch designed(
    netw::view::StretchMode p_mode,
    netw::view::StretchAspect p_aspect,
    const Vector2i &p_design
) {
    Stretch out;
    out.mode = p_mode;
    out.aspect = p_aspect;
    out.scale_mode = netw::view::STRETCH_SCALE_MODE_FRACTIONAL;
    out.scale = 1.0;
    out.design_size = p_design;
    return out;
}

TEST_CASE(
    "[Networked][View][Hosted] V1 a control with no area has no aspect to fit "
    "and no ratio to divide by, so the layout answers a one pixel target and "
    "an empty inner rect rather than dividing by a zero side"
) {
    const Stretch stretch = designed(
        netw::view::STRETCH_MODE_CANVAS_ITEMS,
        netw::view::STRETCH_ASPECT_KEEP,
        Vector2i(640, 360)
    );

    const Layout flat = netw::view::layout_compute(stretch, Vector2(800, 0));
    NETW_CHECK_EQ(flat.target_size.x, 800);
    NETW_CHECK_EQ(flat.target_size.y, 1);
    NETW_CHECK_EQ(int(flat.inner_rect.size.y == 0.0f), 1);

    const Layout thin = netw::view::layout_compute(stretch, Vector2(0, 600));
    NETW_CHECK_EQ(thin.target_size.x, 1);
    NETW_CHECK_EQ(thin.target_size.y, 600);
}

TEST_CASE(
    "[Networked][View][Hosted] V2 a disabled mode and a design with no area "
    "are the same answer, one to one over the whole control with no size "
    "override, whatever the aspect and the scale ask for"
) {
    Stretch off = designed(
        netw::view::STRETCH_MODE_DISABLED,
        netw::view::STRETCH_ASPECT_KEEP,
        Vector2i(640, 360)
    );
    off.scale = 4.0;

    const Layout disabled = netw::view::layout_compute(off, Vector2(800, 600));
    NETW_CHECK_EQ(disabled.target_size.x, 800);
    NETW_CHECK_EQ(disabled.target_size.y, 600);
    NETW_CHECK_EQ(disabled.size_2d_override.x, 0);
    NETW_CHECK_EQ(int(disabled.override_stretch), 0);
    NETW_CHECK_EQ(int(disabled.inner_rect.size == Vector2(800, 600)), 1);

    const Stretch sizeless = designed(
        netw::view::STRETCH_MODE_CANVAS_ITEMS,
        netw::view::STRETCH_ASPECT_KEEP,
        Vector2i(0, 360)
    );
    const Layout undesigned
        = netw::view::layout_compute(sizeless, Vector2(800, 600));
    NETW_CHECK_EQ(undesigned.target_size.x, 800);
    NETW_CHECK_EQ(undesigned.target_size.y, 600);
    NETW_CHECK_EQ(undesigned.size_2d_override.x, 0);
}

TEST_CASE(
    "[Networked][View][Hosted] V3 the two live modes assign opposite ends of "
    "the same fit, so canvas items renders at the on screen pixel size and "
    "publishes the design as the logical override, while viewport renders at "
    "the design and leaves the texture to stretch into the rect"
) {
    const Layout crisp = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_CANVAS_ITEMS,
            netw::view::STRETCH_ASPECT_KEEP,
            Vector2i(640, 360)
        ),
        Vector2(800, 600)
    );
    NETW_CHECK_EQ(crisp.target_size.x, 800);
    NETW_CHECK_EQ(crisp.target_size.y, 450);
    NETW_CHECK_EQ(crisp.size_2d_override.x, 640);
    NETW_CHECK_EQ(crisp.size_2d_override.y, 360);
    NETW_CHECK_EQ(int(crisp.override_stretch), 1);

    const Layout chunky = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_VIEWPORT,
            netw::view::STRETCH_ASPECT_KEEP,
            Vector2i(640, 360)
        ),
        Vector2(800, 600)
    );
    NETW_CHECK_EQ(chunky.target_size.x, 640);
    NETW_CHECK_EQ(chunky.target_size.y, 360);
    NETW_CHECK_EQ(chunky.size_2d_override.x, 0);
    NETW_CHECK_EQ(int(chunky.override_stretch), 0);

    NETW_CHECK_EQ(int(crisp.inner_rect == chunky.inner_rect), 1);
}

TEST_CASE(
    "[Networked][View][Hosted] V4 keep centres the design's aspect inside the "
    "control, so the inner rect is the fit and every pixel of the control "
    "outside it is the letterbox the view leaves undrawn"
) {
    const Layout wide = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_VIEWPORT,
            netw::view::STRETCH_ASPECT_KEEP,
            Vector2i(640, 360)
        ),
        Vector2(800, 600)
    );
    NETW_CHECK_EQ(int(wide.inner_rect.position == Vector2(0, 75)), 1);
    NETW_CHECK_EQ(int(wide.inner_rect.size == Vector2(800, 450)), 1);

    const Layout tall = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_VIEWPORT,
            netw::view::STRETCH_ASPECT_KEEP,
            Vector2i(360, 640)
        ),
        Vector2(800, 600)
    );
    NETW_CHECK_EQ(int(tall.inner_rect.position == Vector2(231, 0)), 1);
    NETW_CHECK_EQ(int(tall.inner_rect.size.y == 600.0f), 1);
}

TEST_CASE(
    "[Networked][View][Hosted] V5 an aspect either letterboxes the rect or "
    "grows the logical design, never both, so ignore fills with the design "
    "untouched, the two keeps hold one axis and stretch the other, and expand "
    "answers whichever keep the control is tighter against"
) {
    const Vector2 control(800, 600);
    const Vector2i design(640, 360);

    const Layout fill = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_CANVAS_ITEMS,
            netw::view::STRETCH_ASPECT_IGNORE,
            design
        ),
        control
    );
    NETW_CHECK_EQ(int(fill.inner_rect.size == control), 1);
    NETW_CHECK_EQ(fill.size_2d_override.x, 640);
    NETW_CHECK_EQ(fill.size_2d_override.y, 360);

    const Layout keep_width = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_CANVAS_ITEMS,
            netw::view::STRETCH_ASPECT_KEEP_WIDTH,
            design
        ),
        control
    );
    NETW_CHECK_EQ(int(keep_width.inner_rect.size == control), 1);
    NETW_CHECK_EQ(keep_width.size_2d_override.x, 640);
    NETW_CHECK_EQ(keep_width.size_2d_override.y, 480);

    const Layout keep_height = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_CANVAS_ITEMS,
            netw::view::STRETCH_ASPECT_KEEP_HEIGHT,
            design
        ),
        control
    );
    NETW_CHECK_EQ(keep_height.size_2d_override.x, 480);
    NETW_CHECK_EQ(keep_height.size_2d_override.y, 360);

    const Layout expand = netw::view::layout_compute(
        designed(
            netw::view::STRETCH_MODE_CANVAS_ITEMS,
            netw::view::STRETCH_ASPECT_EXPAND,
            design
        ),
        control
    );
    NETW_CHECK_EQ(
        int(expand.size_2d_override == keep_width.size_2d_override),
        1
    );
}

TEST_CASE(
    "[Networked][View][Hosted] V6 integer snapping is a viewport mode rule "
    "with a letterbox to snap inside, so it shrinks the rect to a whole "
    "multiple of the design there, never falls below one to one, and leaves "
    "canvas items, ignore and expand exactly where they were"
) {
    Stretch snapped = designed(
        netw::view::STRETCH_MODE_VIEWPORT,
        netw::view::STRETCH_ASPECT_KEEP,
        Vector2i(320, 180)
    );
    snapped.scale_mode = netw::view::STRETCH_SCALE_MODE_INTEGER;

    const Layout whole = netw::view::layout_compute(snapped, Vector2(800, 600));
    NETW_CHECK_EQ(int(whole.inner_rect.position == Vector2(80, 120)), 1);
    NETW_CHECK_EQ(int(whole.inner_rect.size == Vector2(640, 360)), 1);
    NETW_CHECK_EQ(whole.target_size.x, 320);

    const Layout cramped
        = netw::view::layout_compute(snapped, Vector2(200, 120));
    NETW_CHECK_EQ(int(cramped.inner_rect.size == Vector2(320, 180)), 1);

    Stretch unsnapped = snapped;
    unsnapped.mode = netw::view::STRETCH_MODE_CANVAS_ITEMS;
    const Layout fractional
        = netw::view::layout_compute(unsnapped, Vector2(800, 600));
    NETW_CHECK_EQ(int(fractional.inner_rect.size == Vector2(800, 450)), 1);
}

TEST_CASE(
    "[Networked][View][Hosted] V7 the scale divides the design rather than "
    "multiplying the rect, so doubling it halves the logical viewport the "
    "game draws into and leaves the on screen rect alone"
) {
    Stretch doubled = designed(
        netw::view::STRETCH_MODE_VIEWPORT,
        netw::view::STRETCH_ASPECT_IGNORE,
        Vector2i(640, 360)
    );
    doubled.scale = 2.0;

    const Layout half = netw::view::layout_compute(doubled, Vector2(800, 600));
    NETW_CHECK_EQ(half.target_size.x, 320);
    NETW_CHECK_EQ(half.target_size.y, 180);
    NETW_CHECK_EQ(int(half.inner_rect.size == Vector2(800, 600)), 1);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] V10 a window's stretch is its own "
    "five content scale fields, and no window at all is a disabled stretch"
) {
    Window *window = memnew(Window);
    window->set_content_scale_mode(Window::CONTENT_SCALE_MODE_VIEWPORT);
    window->set_content_scale_aspect(Window::CONTENT_SCALE_ASPECT_KEEP_HEIGHT);
    window->set_content_scale_stretch(Window::CONTENT_SCALE_STRETCH_INTEGER);
    window->set_content_scale_factor(3.0);
    window->set_content_scale_size(Vector2i(480, 270));

    const Stretch read = netw::view::stretch_from_window(window);
    memdelete(window);

    NETW_CHECK_EQ(int(read.mode), int(netw::view::STRETCH_MODE_VIEWPORT));
    NETW_CHECK_EQ(
        int(read.aspect),
        int(netw::view::STRETCH_ASPECT_KEEP_HEIGHT)
    );
    NETW_CHECK_EQ(
        int(read.scale_mode),
        int(netw::view::STRETCH_SCALE_MODE_INTEGER)
    );
    NETW_CHECK_EQ(int(read.scale), 3);
    NETW_CHECK_EQ(read.design_size.x, 480);
    NETW_CHECK_EQ(read.design_size.y, 270);

    const Stretch none = netw::view::stretch_from_window(nullptr);
    NETW_CHECK_EQ(int(none.mode), int(netw::view::STRETCH_MODE_DISABLED));
}

} // namespace TestViewStretchLaws
