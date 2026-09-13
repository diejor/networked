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
    "[Networked][View][Hosted] V8 an override folds field by field, so a "
    "view that names one knob takes the project's answer for every other one "
    "and a view that names none is the project verbatim"
) {
    Stretch base;
    base.mode = netw::view::STRETCH_MODE_VIEWPORT;
    base.aspect = netw::view::STRETCH_ASPECT_KEEP;
    base.scale_mode = netw::view::STRETCH_SCALE_MODE_FRACTIONAL;
    base.scale = 1.0;
    base.design_size = Vector2i(640, 360);

    const Stretch untouched = netw::view::stretch_over(base, Stretch());
    NETW_CHECK_EQ(int(untouched.mode), int(netw::view::STRETCH_MODE_VIEWPORT));
    NETW_CHECK_EQ(int(untouched.aspect), int(netw::view::STRETCH_ASPECT_KEEP));
    NETW_CHECK_EQ(untouched.design_size.x, 640);
    NETW_CHECK_EQ(int(untouched.scale == 1.0), 1);

    Stretch names_aspect;
    names_aspect.aspect = netw::view::STRETCH_ASPECT_IGNORE;
    const Stretch folded = netw::view::stretch_over(base, names_aspect);
    NETW_CHECK_EQ(int(folded.aspect), int(netw::view::STRETCH_ASPECT_IGNORE));
    NETW_CHECK_EQ(int(folded.mode), int(netw::view::STRETCH_MODE_VIEWPORT));
    NETW_CHECK_EQ(folded.design_size.x, 640);

    Stretch names_all;
    names_all.mode = netw::view::STRETCH_MODE_CANVAS_ITEMS;
    names_all.aspect = netw::view::STRETCH_ASPECT_EXPAND;
    names_all.scale_mode = netw::view::STRETCH_SCALE_MODE_INTEGER;
    names_all.scale = 3.0;
    names_all.design_size = Vector2i(320, 180);
    const Stretch replaced = netw::view::stretch_over(base, names_all);
    NETW_CHECK_EQ(
        int(replaced.mode),
        int(netw::view::STRETCH_MODE_CANVAS_ITEMS)
    );
    NETW_CHECK_EQ(replaced.design_size.y, 180);
    NETW_CHECK_EQ(int(replaced.scale == 3.0), 1);
}

TEST_CASE(
    "[Networked][View][Hosted] V9 resolving reads the project for every knob "
    "the view leaves alone, so a bare override is the project's own stretch "
    "and a named knob is the only field that differs from it"
) {
    const Stretch project = netw::view::stretch_from_project();
    const Stretch bare = netw::view::stretch_resolve(Stretch());
    NETW_CHECK_EQ(int(bare.mode), int(project.mode));
    NETW_CHECK_EQ(int(bare.aspect), int(project.aspect));
    NETW_CHECK_EQ(int(bare.scale_mode), int(project.scale_mode));
    NETW_CHECK_EQ(bare.design_size.x, project.design_size.x);

    Stretch names_design;
    names_design.design_size = Vector2i(1280, 720);
    const Stretch resolved = netw::view::stretch_resolve(names_design);
    NETW_CHECK_EQ(resolved.design_size.x, 1280);
    NETW_CHECK_EQ(int(resolved.mode), int(project.mode));
    NETW_CHECK_EQ(int(resolved.aspect), int(project.aspect));
}

} // namespace TestViewStretchLaws
