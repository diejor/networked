#include "netw/api/nodes/view/cameras.hpp"

#include "godot/camera.hpp"
#include "godot/object.hpp"

using namespace godot;

namespace netw::view {

namespace {

Node *nearest(Node *p_player, Node *p_level, const String &p_type) {
    Node *found = first_camera(p_player, p_type);
    if (found != nullptr) {
        return found;
    }
    return first_camera(p_level, p_type);
}

} // namespace

Node *first_camera(Node *p_root, const String &p_type) {
    if (p_root == nullptr) {
        return nullptr;
    }
    if (p_root->is_class(p_type)) {
        return p_root;
    }
    const TypedArray<Node> found
        = p_root->find_children("*", p_type, true, false);
    if (found.is_empty()) {
        return nullptr;
    }
    return Object::cast_to<Node>(gd::live_object(found[0]));
}

bool adopt_camera(Node *p_player, Node *p_level) {
    Camera2D *flat
        = Object::cast_to<Camera2D>(nearest(p_player, p_level, "Camera2D"));
    if (flat != nullptr) {
        flat->make_current();
        return true;
    }
    Camera3D *spatial
        = Object::cast_to<Camera3D>(nearest(p_player, p_level, "Camera3D"));
    if (spatial != nullptr) {
        spatial->make_current();
        return true;
    }
    return false;
}

} // namespace netw::view
