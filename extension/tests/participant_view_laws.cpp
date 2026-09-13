#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "godot/viewport.hpp"
#include "netw/api/nodes/view/participant_view.hpp"

namespace TestParticipantViewLaws {

using namespace godot;
using netw::ParticipantView;

struct Stand {
    Control *holder = nullptr;
    ParticipantView *view = nullptr;
    SubViewport *target = nullptr;

    Stand() {
        holder = memnew(Control);
        holder->set_name("Holder");
        netw::gd::scene_root()->add_child(holder);

        view = memnew(ParticipantView);
        view->set_name("Stand");
        holder->add_child(view);

        target = memnew(SubViewport);
        target->set_name("Target");
        target->set_update_mode(SubViewport::UPDATE_DISABLED);
        target->set_clear_mode(SubViewport::CLEAR_MODE_NEVER);
        target->set_size_2d_override(Vector2i(111, 222));
        target->set_size_2d_override_stretch(true);
        netw::gd::scene_root()->add_child(target);
    }

    ParticipantView *mount(const char *p_name) const {
        ParticipantView *made = memnew(ParticipantView);
        made->set_name(p_name);
        holder->add_child(made);
        return made;
    }

    ~Stand() {
        if (target != nullptr) {
            netw::gd::scene_root()->remove_child(target);
            memdelete(target);
        }
        netw::gd::scene_root()->remove_child(holder);
        memdelete(holder);
    }
};

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PV1 a view borrows the render state "
    "it drives rather than owning it, so a target it lets go of reads back "
    "the update mode, the clear mode and the 2D override it arrived with"
) {
    Stand stand;

    stand.view->set_target(stand.target);
    NETW_CHECK_EQ(
        int(stand.target->get_update_mode()),
        int(SubViewport::UPDATE_ALWAYS)
    );
    NETW_CHECK_EQ(
        int(stand.target->get_clear_mode()),
        int(SubViewport::CLEAR_MODE_ALWAYS)
    );

    stand.view->clear_target();

    NETW_CHECK_EQ(
        int(stand.target->get_update_mode()),
        int(SubViewport::UPDATE_DISABLED)
    );
    NETW_CHECK_EQ(
        int(stand.target->get_clear_mode()),
        int(SubViewport::CLEAR_MODE_NEVER)
    );
    NETW_CHECK_EQ(stand.target->get_size_2d_override().x, 111);
    NETW_CHECK_EQ(stand.target->get_size_2d_override().y, 222);
    CHECK(stand.target->is_size_2d_override_stretch_enabled());
    NETW_CHECK_EQ(int(stand.view->get_target() == nullptr), 1);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PV2 two views cannot borrow one "
    "target's render state, because the second would save what the first "
    "already overwrote, so the standing view keeps both the target and the "
    "state it saved and the latecomer is left displaying nothing"
) {
    Stand stand;
    ParticipantView *second = stand.mount("Second");

    stand.view->set_target(stand.target);
    NETW_CHECK_EQ(
        int(ParticipantView::owner_of(stand.target) == stand.view),
        1
    );

    second->set_target(stand.target);

    NETW_CHECK_EQ(
        int(ParticipantView::owner_of(stand.target) == stand.view),
        1
    );
    NETW_CHECK_EQ(int(second->get_target() == nullptr), 1);

    stand.view->clear_target();
    NETW_CHECK_EQ(
        int(stand.target->get_update_mode()),
        int(SubViewport::UPDATE_DISABLED)
    );
    NETW_CHECK_EQ(int(ParticipantView::owner_of(stand.target) == nullptr), 1);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PV3 a target that leaves the tree "
    "takes the view's claim with it, so the view holds no target and stops "
    "asking for a redraw rather than reading a freed viewport every frame"
) {
    Stand stand;
    stand.view->set_target(stand.target);
    CHECK(stand.view->is_processing());

    netw::gd::scene_root()->remove_child(stand.target);
    memdelete(stand.target);
    stand.target = nullptr;

    NETW_CHECK_EQ(int(stand.view->get_target() == nullptr), 1);
    CHECK_FALSE(stand.view->is_processing());
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PV4 the layout the view solves is "
    "the one it pushes, so the target's size, 2D override and stretch flag "
    "are the three fields of that solve and they follow a resize"
) {
    Stand stand;
    stand.view->set_stretch_mode(ParticipantView::STRETCH_MODE_CANVAS_ITEMS);
    stand.view->set_stretch_aspect(ParticipantView::STRETCH_ASPECT_KEEP);
    stand.view->set_stretch_scale(1.0);
    stand.view->set_stretch_design_size(Vector2i(640, 360));
    stand.view->set_size(Vector2(800, 600));

    stand.view->set_target(stand.target);

    NETW_CHECK_EQ(stand.target->get_size().x, 800);
    NETW_CHECK_EQ(stand.target->get_size().y, 450);
    NETW_CHECK_EQ(stand.target->get_size_2d_override().x, 640);
    NETW_CHECK_EQ(stand.target->get_size_2d_override().y, 360);
    CHECK(stand.target->is_size_2d_override_stretch_enabled());
    NETW_CHECK_EQ(int(stand.view->get_inner_rect().position.y == 75.0f), 1);

    stand.view->set_size(Vector2(640, 360));

    NETW_CHECK_EQ(stand.target->get_size().x, 640);
    NETW_CHECK_EQ(stand.target->get_size().y, 360);
    NETW_CHECK_EQ(int(stand.view->get_inner_rect().position.y == 0.0f), 1);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] PV5 a view that names no stretch "
    "knob takes every one of them from the project, so the same view under "
    "one named knob differs from the project's own answer in that field "
    "alone"
) {
    Stand stand;
    stand.view->set_size(Vector2(800, 600));
    stand.view->set_target(stand.target);

    NETW_CHECK_EQ(
        int(stand.view->get_stretch_mode()),
        int(ParticipantView::STRETCH_MODE_INHERIT)
    );
    const netw::view::Layout inherited = netw::view::layout_compute(
        netw::view::stretch_from_project(),
        Vector2(800, 600)
    );
    NETW_CHECK_EQ(stand.target->get_size().x, inherited.target_size.x);
    NETW_CHECK_EQ(stand.target->get_size().y, inherited.target_size.y);

    netw::view::Stretch named;
    named.mode = netw::view::STRETCH_MODE_VIEWPORT;
    named.design_size = Vector2i(320, 180);
    const netw::view::Layout expected = netw::view::layout_compute(
        netw::view::stretch_resolve(named),
        Vector2(800, 600)
    );

    stand.view->set_stretch_mode(ParticipantView::STRETCH_MODE_VIEWPORT);
    stand.view->set_stretch_design_size(Vector2i(320, 180));

    NETW_CHECK_EQ(stand.target->get_size().x, expected.target_size.x);
    NETW_CHECK_EQ(stand.target->get_size().y, expected.target_size.y);
    NETW_CHECK_EQ(expected.target_size.x, 320);
}

} // namespace TestParticipantViewLaws
