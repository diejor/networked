#include "support/netw_test.h"

#include "godot/gui.hpp"
#include "godot/input_event.hpp"
#include "godot/rendering_server.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
#include "netw/api/nodes/view/stretch_viewport_container.hpp"
#include "support/netw_call_log.h"

namespace TestStretchViewportContainerLaws {

using namespace godot;
using netw::StretchViewportContainer;

template <typename T> struct Mounted {
    T *node = nullptr;

    explicit Mounted(T *p_node) : node(p_node) {
        netw::gd::scene_root()->add_child(node);
    }

    ~Mounted() {
        netw::gd::scene_root()->remove_child(node);
        memdelete(node);
    }

    T *operator->() const {
        return node;
    }
};

Window *canvas_items_window(const Vector2i &p_size, const Vector2i &p_design) {
    Window *window = memnew(Window);
    window->set_size(p_size);
    window->set_content_scale_mode(Window::CONTENT_SCALE_MODE_CANVAS_ITEMS);
    window->set_content_scale_aspect(Window::CONTENT_SCALE_ASPECT_KEEP);
    window->set_content_scale_size(p_design);
    return window;
}

SubViewport *viewport_sized(const Vector2i &p_size) {
    SubViewport *viewport = memnew(SubViewport);
    viewport->set_size(p_size);
    return viewport;
}

template <typename T> T *added(Node *p_parent, T *p_child) {
    p_parent->add_child(p_child);
    return p_child;
}

StretchViewportContainer *container_in(Node *p_parent) {
    return added(p_parent, memnew(StretchViewportContainer));
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC1 a container filling a canvas "
    "items window renders its SubViewport at the window's pixels with the "
    "design as the 2D override, covers the same rect as the design, and "
    "follows a new content scale size"
) {
    Mounted<Window> window(
        canvas_items_window(Vector2i(1280, 720), Vector2i(640, 360))
    );
    StretchViewportContainer *view = container_in(window.node);
    SubViewport *target = added(view, memnew(SubViewport));

    NETW_CHECK_EQ(target->get_size().x, 1280);
    NETW_CHECK_EQ(target->get_size().y, 720);
    NETW_CHECK_EQ(target->get_size_2d_override().x, 640);
    NETW_CHECK_EQ(target->get_size_2d_override().y, 360);
    CHECK(target->is_size_2d_override_stretch_enabled());
    NETW_CHECK_EQ(int(view->get_size().x * view->get_scale().x), 640);
    NETW_CHECK_EQ(int(view->get_size().y * view->get_scale().y), 360);

    window->set_content_scale_size(Vector2i(480, 360));

    NETW_CHECK_EQ(target->get_size().x, 960);
    NETW_CHECK_EQ(target->get_size().y, 720);
    NETW_CHECK_EQ(target->get_size_2d_override().x, 480);
    NETW_CHECK_EQ(target->get_size_2d_override().y, 360);
    NETW_CHECK_EQ(int(view->get_size().x * view->get_scale().x), 480);
    NETW_CHECK_EQ(int(view->get_size().y * view->get_scale().y), 360);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC2 taking a SubViewport copies "
    "the enclosing viewport's render settings into it, and the fallback "
    "environment into a world of its own that has none"
) {
    Mounted<SubViewport> host(viewport_sized(Vector2i(640, 360)));
    host->set_msaa_2d(Viewport::MSAA_4X);
    host->set_msaa_3d(Viewport::MSAA_8X);
    RenderingServer *renderer = RenderingServer::get_singleton();
    REQUIRE(renderer != nullptr);
    const String method = renderer->get_current_rendering_method();
    const bool forward_plus = method == "forward_plus";
    const Viewport::ScreenSpaceAA screen_aa = method == "gl_compatibility"
        ? Viewport::SCREEN_SPACE_AA_DISABLED
        : Viewport::SCREEN_SPACE_AA_FXAA;
    const Viewport::Scaling3DMode scaling = forward_plus
        ? Viewport::SCALING_3D_MODE_FSR
        : Viewport::SCALING_3D_MODE_BILINEAR;
    host->set_screen_space_aa(screen_aa);
    host->set_use_taa(forward_plus);
    host->set_use_debanding(true);
    host->set_scaling_3d_mode(scaling);
    host->set_scaling_3d_scale(0.5);
    host->set_use_hdr_2d(true);
    host->set_positional_shadow_atlas_size(1024);
    host->set_physics_object_picking(true);
    host->set_default_canvas_item_texture_filter(
        Viewport::DEFAULT_CANVAS_ITEM_TEXTURE_FILTER_NEAREST
    );
    host->set_snap_2d_transforms_to_pixel(true);
    host->set_snap_2d_vertices_to_pixel(true);
    host->set_use_own_world_3d(true);
    Ref<Environment> environment;
    environment.instantiate();
    host->find_world_3d()->set_fallback_environment(environment);

    StretchViewportContainer *view = container_in(host.node);
    SubViewport *target = memnew(SubViewport);
    target->set_use_own_world_3d(true);
    view->add_child(target);

    NETW_CHECK_EQ(int(target->get_msaa_2d()), int(Viewport::MSAA_4X));
    NETW_CHECK_EQ(int(target->get_msaa_3d()), int(Viewport::MSAA_8X));
    NETW_CHECK_EQ(
        int(target->get_screen_space_aa()),
        int(screen_aa)
    );
    CHECK(target->is_using_taa() == forward_plus);
    CHECK(target->is_using_debanding());
    NETW_CHECK_EQ(
        int(target->get_scaling_3d_mode()),
        int(scaling)
    );
    NETW_CHECK_EQ(int(target->get_scaling_3d_scale() * 100.0f), 50);
    CHECK(target->is_using_hdr_2d());
    NETW_CHECK_EQ(target->get_positional_shadow_atlas_size(), 1024);
    CHECK(target->get_physics_object_picking());
    NETW_CHECK_EQ(
        int(target->get_default_canvas_item_texture_filter()),
        int(Viewport::DEFAULT_CANVAS_ITEM_TEXTURE_FILTER_NEAREST)
    );
    CHECK(target->is_snap_2d_transforms_to_pixel_enabled());
    CHECK(target->is_snap_2d_vertices_to_pixel_enabled());
    NETW_CHECK_EQ(
        int(target->find_world_3d()->get_fallback_environment() == environment),
        1
    );

    StretchViewportContainer *carried = memnew(StretchViewportContainer);
    SubViewport *carried_target = added(carried, memnew(SubViewport));
    host->add_child(carried);
    NETW_CHECK_EQ(int(carried_target->get_msaa_3d()), int(Viewport::MSAA_8X));
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC3 a hidden container shrinks "
    "its SubViewport to two pixels and turns off its input and both audio "
    "listeners, and showing it again restores all of them"
) {
    Mounted<SubViewport> host(viewport_sized(Vector2i(640, 360)));
    StretchViewportContainer *view = container_in(host.node);
    SubViewport *target = added(view, memnew(SubViewport));

    view->hide();

    NETW_CHECK_EQ(target->get_size().x, 2);
    NETW_CHECK_EQ(target->get_size().y, 2);
    CHECK(target->is_input_disabled());
    CHECK_FALSE(target->is_audio_listener_2d());
    CHECK_FALSE(target->is_audio_listener_3d());

    view->show();

    NETW_CHECK_EQ(target->get_size().x, 640);
    NETW_CHECK_EQ(target->get_size().y, 360);
    CHECK_FALSE(target->is_input_disabled());
    CHECK(target->is_audio_listener_2d());
    CHECK(target->is_audio_listener_3d());
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC4 input and audio follow the "
    "container's own visible flag, so a visible container under a hidden "
    "parent stops rendering and still takes input"
) {
    Mounted<SubViewport> host(viewport_sized(Vector2i(640, 360)));
    Control *holder = added(host.node, memnew(Control));
    StretchViewportContainer *view = container_in(holder);
    view->set_size(Vector2(320, 180));
    SubViewport *target = added(view, memnew(SubViewport));

    holder->hide();

    NETW_CHECK_EQ(
        int(target->get_update_mode()),
        int(SubViewport::UPDATE_DISABLED)
    );
    CHECK_FALSE(target->is_input_disabled());
    CHECK(target->is_audio_listener_2d());
    NETW_CHECK_EQ(target->get_size().x, 320);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC5 a click inside a container "
    "under a Control reaches a full rect Control in its SubViewport at the "
    "logical position under the click"
) {
    Mounted<Window> window(
        canvas_items_window(Vector2i(1280, 720), Vector2i(320, 180))
    );
    Control *holder = added(window.node, memnew(Control));
    StretchViewportContainer *view = container_in(holder);
    view->set_size(Vector2(640, 480));
    SubViewport *target = added(view, memnew(SubViewport));
    Control *pad = added(target, memnew(Control));
    pad->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    netw_test::CallLog log;
    pad->connect("gui_input", log.callable("click"));

    Ref<InputEventMouseButton> click;
    click.instantiate();
    click->set_button_index(netw::gd::MOUSE_LEFT);
    click->set_pressed(true);
    click->set_position(Vector2(64, 96));
    view->NETW_GUI_INPUT(click);

    NETW_CHECK_EQ(log.count("click"), 1);
    const Ref<InputEventMouseButton> reached = log.args("click")[0];
    NETW_CHECK_EQ(int(reached->get_position().x), 32);
    NETW_CHECK_EQ(int(reached->get_position().y), 48);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC7 a container anchored to a "
    "Control follows the Control as it shrinks and grows, with its "
    "SubViewport at the container's size and a 2D override of the same "
    "aspect that covers the design"
) {
    Mounted<Window> window(
        canvas_items_window(Vector2i(1280, 720), Vector2i(320, 180))
    );
    Control *holder = added(window.node, memnew(Control));
    holder->set_size(Vector2(800, 600));
    StretchViewportContainer *view = container_in(holder);
    SubViewport *target = added(view, memnew(SubViewport));
    view->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

    NETW_CHECK_EQ(int(view->get_size().x), 800);
    NETW_CHECK_EQ(target->get_size().x, 800);
    NETW_CHECK_EQ(target->get_size().y, 600);
    NETW_CHECK_EQ(target->get_size_2d_override().x, 320);
    NETW_CHECK_EQ(target->get_size_2d_override().y, 240);

    holder->set_size(Vector2(400, 300));

    NETW_CHECK_EQ(int(view->get_size().x), 400);
    NETW_CHECK_EQ(int(view->get_size().y), 300);
    NETW_CHECK_EQ(target->get_size().x, 400);
    NETW_CHECK_EQ(target->get_size().y, 300);

    holder->set_size(Vector2(1000, 500));

    NETW_CHECK_EQ(int(view->get_size().x), 1000);
    NETW_CHECK_EQ(target->get_size().x, 1000);
    NETW_CHECK_EQ(target->get_size().y, 500);
    NETW_CHECK_EQ(target->get_size_2d_override().x, 360);
    NETW_CHECK_EQ(target->get_size_2d_override().y, 180);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC6 a container whose parent is "
    "not a Control fills the enclosing viewport's visible rect and follows "
    "it when the viewport resizes"
) {
    Mounted<SubViewport> host(viewport_sized(Vector2i(800, 600)));
    Node2D *level = added(host.node, memnew(Node2D));
    StretchViewportContainer *view = container_in(level);
    SubViewport *target = added(view, memnew(SubViewport));

    NETW_CHECK_EQ(int(view->get_size().x), 800);
    NETW_CHECK_EQ(int(view->get_size().y), 600);

    host->set_size(Vector2i(1024, 768));

    NETW_CHECK_EQ(int(view->get_size().x), 1024);
    NETW_CHECK_EQ(int(view->get_size().y), 768);
    NETW_CHECK_EQ(target->get_size().x, 1024);
}

TEST_CASE(
    "[Networked][View][Hosted][SceneTree] SVC8 the container manages "
    "stretch and stretch_shrink, so neither is listed and a script that "
    "sets them is undone by the next layout"
) {
    Mounted<SubViewport> host(viewport_sized(Vector2i(800, 600)));
    Node2D *level = added(host.node, memnew(Node2D));
    StretchViewportContainer *view = container_in(level);
    SubViewport *target = added(view, memnew(SubViewport));

    bool listed = false;
    const Array properties = netw::gd::property_list(view);
    for (int at = 0; at < properties.size(); ++at) {
        const Dictionary property = properties[at];
        const StringName name = property["name"];
        if (name == StringName("stretch")
            || name == StringName("stretch_shrink")) {
            listed = listed || int64_t(property["usage"]) != 0;
        }
    }
    CHECK_FALSE(listed);

    view->set_stretch(true);
    view->set_stretch_shrink(2);
    host->set_size(Vector2i(1024, 768));

    CHECK_FALSE(view->is_stretch_enabled());
    NETW_CHECK_EQ(int(view->get_stretch_shrink()), 1);
    NETW_CHECK_EQ(target->get_size().x, 1024);
}

} // namespace TestStretchViewportContainerLaws
