#include "netw/store/resident.hpp"

#include "netw/store/validate.hpp"
#include "netw/tree_hook.hpp"

using namespace godot;

namespace netw::store {

namespace {

const StringName &resident_column() {
    static const StringName name("resident");
    return name;
}

} // namespace

bool NodeCell::same_as(const NodeCell &p_other) const {
    return object == p_other.object && solid == p_other.solid
        && flat == p_other.flat && frame_3d == p_other.frame_3d
        && frame_2d == p_other.frame_2d && state_3d == p_other.state_3d
        && body == p_other.body && in_tree == p_other.in_tree
        && root == p_other.root;
}

NodeCell Residents::derive(uint64_t p_node) {
    return derive_object(gd::object_of(ObjectID(p_node)));
}

NodeCell Residents::derive_object(Object *p_object) {
    NodeCell cell;
    cell.object = p_object;
    Node *node = Object::cast_to<Node>(cell.object);
    if (node == nullptr || !node->is_inside_tree()) {
        return cell;
    }
    cell.in_tree = true;
    if (RigidBody3D *solid = Object::cast_to<RigidBody3D>(node)) {
        cell.solid = solid;
        cell.body = solid->get_rid();
        cell.frame_3d = Object::cast_to<Node3D>(solid->get_parent());
        cell.state_3d
            = PhysicsServer3D::get_singleton()->body_get_direct_state(
                cell.body
            );
        cell.root = tree::is_entity_root(node);
    } else if (RigidBody2D *flat = Object::cast_to<RigidBody2D>(node)) {
        cell.flat = flat;
        cell.body = flat->get_rid();
        cell.frame_2d = Object::cast_to<CanvasItem>(flat->get_parent());
        cell.root = tree::is_entity_root(node);
    }
    return cell;
}

void Residents::watch(bool p_watching) {
    if (watching == p_watching) {
        return;
    }
    watching = p_watching;
    clear();
}

void Residents::note_removed(uint64_t p_node) {
    if (held.erase(p_node)) {
        epoch += 1;
    }
}

void Residents::clear() {
    held.clear();
    epoch += 1;
}

const NodeCell &Residents::resolve(
    uint64_t p_node,
    NodeCell &r_cell,
    bool p_validating
) {
    const bool cached = r_cell.epoch == epoch && r_cell.object != nullptr;
    if (cached && !p_validating) {
        return r_cell;
    }
    NodeCell derived = derive(p_node);
    if (cached && !derived.same_as(r_cell)) {
        Miss missed;
        missed.column = resident_column();
        missed.gate = "resident";
        report_miss(missed);
    }
    const bool keeps = watching && derived.in_tree
        && (derived.solid == nullptr || derived.state_3d != nullptr);
    if (keeps) {
        derived.epoch = epoch;
        if (!held.has(p_node)) {
            held.insert(p_node);
            tree::mark_id(p_node, tree::MARK_HEARD);
        }
    }
    r_cell = derived;
    return r_cell;
}

} // namespace netw::store
