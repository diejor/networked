#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "netw/api/nodes/view/participant_viewport.hpp"
#include "netw/api/nodes/view/participant_window.hpp"

namespace TestParticipantWindowLaws {

using namespace godot;
using netw::ParticipantView;
using netw::ParticipantViewport;
using netw::ParticipantWindow;

struct Tiling {
    ParticipantViewport *tiler = nullptr;

    Tiling() {
        tiler = memnew(ParticipantViewport);
        tiler->set_name("Tiler");
        netw::gd::scene_root()->add_child(tiler);
    }

    ParticipantWindow *slot(const char *p_name) const {
        ParticipantWindow *made = memnew(ParticipantWindow);
        made->set_name(p_name);
        tiler->add_child(made);
        return made;
    }

    ~Tiling() {
        netw::gd::scene_root()->remove_child(tiler);
        memdelete(tiler);
    }
};

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PW1 a participant window arrives "
    "hidden and borderless, because a tiler places it and a test that never "
    "asks for a display must never open one"
) {
    ParticipantWindow *slot = memnew(ParticipantWindow);
    CHECK_FALSE(slot->is_visible());
    CHECK(slot->get_flag(Window::FLAG_BORDERLESS));
    memdelete(slot);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PW2 a window's stretch knobs land "
    "on the engine's own content scaling, so naming one is the same act as "
    "setting the matching content_scale field and naming none inherits the "
    "project"
) {
    Tiling tiling;
    ParticipantWindow *slot = tiling.slot("Slot");

    slot->set_stretch_mode(ParticipantView::STRETCH_MODE_CANVAS_ITEMS);
    slot->set_stretch_aspect(ParticipantView::STRETCH_ASPECT_KEEP_WIDTH);
    slot->set_stretch_scale_mode(ParticipantView::STRETCH_SCALE_MODE_INTEGER);
    slot->set_stretch_scale(2.0);
    slot->set_stretch_design_size(Vector2i(320, 180));

    NETW_CHECK_EQ(
        int(slot->get_content_scale_mode()),
        int(Window::CONTENT_SCALE_MODE_CANVAS_ITEMS)
    );
    NETW_CHECK_EQ(
        int(slot->get_content_scale_aspect()),
        int(Window::CONTENT_SCALE_ASPECT_KEEP_WIDTH)
    );
    NETW_CHECK_EQ(
        int(slot->get_content_scale_stretch()),
        int(Window::CONTENT_SCALE_STRETCH_INTEGER)
    );
    NETW_CHECK_EQ(int(slot->get_content_scale_factor() == 2.0f), 1);
    NETW_CHECK_EQ(slot->get_content_scale_size().x, 320);

    const netw::view::Stretch project = netw::view::stretch_from_project();
    ParticipantWindow *bare = tiling.slot("Bare");
    bare->set_tiled_rect(Rect2i(Vector2i(), Vector2i(640, 360)));

    NETW_CHECK_EQ(bare->get_content_scale_size().x, project.design_size.x);
    NETW_CHECK_EQ(
        int(bare->get_content_scale_stretch()),
        int(Window::CONTENT_SCALE_STRETCH_FRACTIONAL)
    );
    NETW_CHECK_EQ(
        int(bare->get_content_scale_mode()
            == Window::CONTENT_SCALE_MODE_VIEWPORT),
        int(project.mode == netw::view::STRETCH_MODE_VIEWPORT)
    );
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PW3 tiling is a grid over however "
    "many slots are registered, so slots partition the enclosing rect with "
    "no gaps at the left and top edges and adding one relays out the rest"
) {
    Tiling tiling;
    ParticipantWindow *first = tiling.slot("First");
    tiling.tiler->add_slot(first);
    CHECK(first->is_visible());
    const Rect2 rect
        = netw::gd::scene_root()->get_viewport()->get_visible_rect();
    NETW_CHECK_EQ(first->get_size().x, int(rect.size.x));
    NETW_CHECK_EQ(first->get_size().y, int(rect.size.y));

    ParticipantWindow *second = tiling.slot("Second");
    tiling.tiler->add_slot(second);
    NETW_CHECK_EQ(first->get_size().x, int(rect.size.x / 2.0f));
    NETW_CHECK_EQ(first->get_size().y, int(rect.size.y));
    NETW_CHECK_EQ(second->get_position().x, int(rect.size.x / 2.0f));
    NETW_CHECK_EQ(second->get_position().y, 0);

    ParticipantWindow *third = tiling.slot("Third");
    tiling.tiler->add_slot(third);
    NETW_CHECK_EQ(third->get_position().x, 0);
    NETW_CHECK_EQ(third->get_position().y, int(rect.size.y / 2.0f));
    NETW_CHECK_EQ(tiling.tiler->get_slots().size(), 3);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PW4 a slot leaving the tiling takes "
    "its joypad bindings with it, so a device bound to a removed window "
    "routes nowhere rather than into a window nothing is drawing"
) {
    Tiling tiling;
    ParticipantWindow *first = tiling.tiler->add_slot(tiling.slot("First"));
    ParticipantWindow *second = tiling.tiler->add_slot(tiling.slot("Second"));

    tiling.tiler->assign_device(0, first);
    tiling.tiler->assign_device(1, second);
    NETW_CHECK_EQ(int(tiling.tiler->device_slot(0) == first), 1);
    NETW_CHECK_EQ(int(tiling.tiler->device_slot(1) == second), 1);

    tiling.tiler->remove_slot(first);

    NETW_CHECK_EQ(int(tiling.tiler->device_slot(0) == nullptr), 1);
    NETW_CHECK_EQ(int(tiling.tiler->device_slot(1) == second), 1);
    CHECK_FALSE(tiling.tiler->has_slot(first));
    CHECK_FALSE(first->is_visible());
    CHECK(tiling.tiler->has_slot(second));
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PW5 a window this viewport does "
    "not tile takes no device binding, because routing a joypad into an "
    "unplaced window would drop its input silently rather than loudly"
) {
    Tiling tiling;
    ParticipantWindow *placed = tiling.tiler->add_slot(tiling.slot("Placed"));
    ParticipantWindow *stray = tiling.slot("Stray");

    tiling.tiler->assign_device(3, stray);
    NETW_CHECK_EQ(int(tiling.tiler->device_slot(3) == nullptr), 1);

    tiling.tiler->assign_device(3, placed);
    NETW_CHECK_EQ(int(tiling.tiler->device_slot(3) == placed), 1);

    NETW_CHECK_EQ(int(tiling.tiler->add_slot(nullptr) == nullptr), 1);
    CHECK_FALSE(tiling.tiler->has_slot(nullptr));
}

} // namespace TestParticipantWindowLaws
