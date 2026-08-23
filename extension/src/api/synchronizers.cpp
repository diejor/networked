#include "netw/api/synchronizers.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

StringName invalidation_meta() {
    return StringName("_sc_invalidation_connected");
}

bool in_editor() {
    return Engine::get_singleton()->is_editor_hint();
}

MultiplayerSynchronizer *as_sync(const Variant &p_value) {
    Object *object = p_value;
    return Object::cast_to<MultiplayerSynchronizer>(object);
}

bool sync_targets(MultiplayerSynchronizer *p_sync, Node *p_target) {
    const NodePath root = p_sync->get_root_path();
    if (root.is_empty()) {
        return false;
    }
    if (p_sync->has_node(root)) {
        return p_sync->get_node_or_null(root) == p_target;
    }
    return !p_target->is_inside_tree() && p_sync->get_parent() == p_target;
}

bool cached_sync_is_valid(MultiplayerSynchronizer *p_sync, Node *p_target) {
    if (p_sync == nullptr || p_sync->is_queued_for_deletion()) {
        return false;
    }
    if (p_target->is_inside_tree() && !p_sync->is_inside_tree()) {
        return false;
    }
    return sync_targets(p_sync, p_target);
}

StringName clean_name(const NodePath &p_path) {
    const int count = p_path.get_subname_count();
    if (count == 0) {
        return StringName();
    }
    return p_path.get_subname(count - 1);
}

void append_target(Array &r_out, Node *p_root, const NodePath &p_path) {
    const netw::gd::NodeProperty resolved = netw::gd::node_property(
        p_root,
        p_path
    );
    if (!resolved.is_property()) {
        return;
    }
    Array pair;
    pair.push_back(resolved.object);
    pair.push_back(resolved.sub);
    r_out.push_back(pair);
}

void append_binding(Array &r_out, Node *p_root, const NodePath &p_path) {
    if (p_path.is_empty()) {
        return;
    }
    const netw::gd::NodeProperty resolved = netw::gd::node_property(
        p_root,
        p_path
    );
    Node *node = Object::cast_to<Node>(resolved.object);
    if (node == nullptr || resolved.sub.get_subname_count() <= 0) {
        return;
    }
    Array triple;
    triple.push_back(StringName(String(p_path)));
    triple.push_back(node);
    triple.push_back(resolved.sub.get_subname(0));
    r_out.push_back(triple);
}

} // namespace

StringName NetwSynchronizers::meta_key() {
    return StringName("cached_synchronizers");
}

void NetwSynchronizers::clear_cache(Object *p_target) {
    Node *target = Object::cast_to<Node>(p_target);
    if (target != nullptr && target->has_meta(meta_key())) {
        target->remove_meta(meta_key());
    }
}

TypedArray<MultiplayerSynchronizer> NetwSynchronizers::of_node(
    Object *p_target
) {
    TypedArray<MultiplayerSynchronizer> found;
    Node *target = Object::cast_to<Node>(p_target);
    if (target == nullptr) {
        return found;
    }
    if (!in_editor() && target->has_meta(meta_key())) {
        const TypedArray<MultiplayerSynchronizer> cached
            = target->get_meta(meta_key());
        bool usable = true;
        for (int at = 0; at < cached.size(); at++) {
            if (!cached_sync_is_valid(as_sync(cached[at]), target)) {
                usable = false;
                break;
            }
        }
        if (usable) {
            return cached;
        }
    }

    const TypedArray<Node> walked
        = target->find_children("*", "MultiplayerSynchronizer");
    for (int at = 0; at < walked.size(); at++) {
        MultiplayerSynchronizer *sync = as_sync(walked[at]);
        if (sync != nullptr && sync_targets(sync, target)) {
            found.push_back(sync);
        }
    }

    if (in_editor()) {
        return found;
    }
    if (!target->is_inside_tree()) {
        NETW_DEBUG(
            "sync",
            "'%s' is off-tree, so its %d synchronizers are not cached",
            target->get_name(),
            found.size()
        );
        return found;
    }
    target->set_meta(meta_key(), found);
    if (!target->has_meta(invalidation_meta())) {
        target->set_meta(invalidation_meta(), true);
        target->connect(
            StringName("child_entered_tree"),
            callable_mp_static(&NetwSynchronizers::clear_cache)
                .unbind(1)
                .bind(target)
        );
    }
    return found;
}

TypedArray<MultiplayerSynchronizer> NetwSynchronizers::owned_by(
    Object *p_target
) {
    TypedArray<MultiplayerSynchronizer> owned;
    Node *target = Object::cast_to<Node>(p_target);
    if (target == nullptr) {
        return owned;
    }
    const TypedArray<MultiplayerSynchronizer> all = of_node(target);
    for (int at = 0; at < all.size(); at++) {
        MultiplayerSynchronizer *sync = as_sync(all[at]);
        if (sync == nullptr) {
            continue;
        }
        const bool mine = sync->get_owner() == target
            || (in_editor() && sync->get_owner() == target->get_owner());
        if (mine) {
            owned.push_back(sync);
        }
    }
    return owned;
}

Dictionary NetwSynchronizers::synchronized_properties(Object *p_target) {
    Dictionary out;
    Node *target = Object::cast_to<Node>(p_target);
    if (target == nullptr) {
        return out;
    }
    const TypedArray<MultiplayerSynchronizer> owned = owned_by(target);
    for (int at = 0; at < owned.size(); at++) {
        MultiplayerSynchronizer *sync = as_sync(owned[at]);
        if (sync == nullptr || sync->get_replication_config().is_null()) {
            continue;
        }
        const TypedArray<NodePath> paths
            = sync->get_replication_config()->get_properties();
        for (int row = 0; row < paths.size(); row++) {
            const NodePath path = paths[row];
            const StringName key = clean_name(path);
            if (key != StringName()) {
                out[key] = path;
            }
        }
    }
    return out;
}

Array NetwSynchronizers::governed_targets(Object *p_sync, Object *p_root) {
    Array out;
    MultiplayerSynchronizer *sync
        = Object::cast_to<MultiplayerSynchronizer>(p_sync);
    Node *root = Object::cast_to<Node>(p_root);
    if (sync == nullptr || root == nullptr
        || sync->get_replication_config().is_null()) {
        return out;
    }
    const TypedArray<NodePath> paths
        = sync->get_replication_config()->get_properties();
    for (int at = 0; at < paths.size(); at++) {
        const NodePath path = paths[at];
        if (path.is_empty()) {
            continue;
        }
        append_target(out, root, path);
    }
    return out;
}

Array NetwSynchronizers::display_bindings(Object *p_sync, Object *p_root) {
    Array out;
    MultiplayerSynchronizer *sync
        = Object::cast_to<MultiplayerSynchronizer>(p_sync);
    Node *root = Object::cast_to<Node>(p_root);
    if (sync == nullptr || root == nullptr
        || sync->get_replication_config().is_null()) {
        return out;
    }
    const TypedArray<NodePath> paths
        = sync->get_replication_config()->get_properties();
    for (int at = 0; at < paths.size(); at++) {
        append_binding(out, root, paths[at]);
    }
    return out;
}

Variant NetwSynchronizers::resolve_value(
    Object *p_target,
    const NodePath &p_path
) {
    Node *target = Object::cast_to<Node>(p_target);
    if (target == nullptr || p_path.is_empty()) {
        return Variant();
    }
    const netw::gd::NodeProperty resolved = netw::gd::node_property(
        target,
        p_path
    );
    if (!resolved.is_property()) {
        return Variant();
    }
    return netw::gd::get_property(resolved.object, resolved.sub);
}

void NetwSynchronizers::assign_value(
    Object *p_target,
    const NodePath &p_path,
    const Variant &p_value
) {
    Node *target = Object::cast_to<Node>(p_target);
    if (target == nullptr || p_path.is_empty()) {
        return;
    }
    const netw::gd::NodeProperty resolved = netw::gd::node_property(
        target,
        p_path
    );
    if (resolved.is_property()) {
        netw::gd::set_property(resolved.object, resolved.sub, p_value);
    }
}

void NetwSynchronizers::sync_only_server(Object *p_target) {
    const TypedArray<MultiplayerSynchronizer> all = of_node(p_target);
    for (int at = 0; at < all.size(); at++) {
        MultiplayerSynchronizer *sync = as_sync(all[at]);
        if (sync == nullptr) {
            continue;
        }
        sync->set_visibility_for(0, false);
        sync->set_visibility_for(1, true);
        sync->update_visibility(0);
    }
}

void NetwSynchronizers::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("meta_key"),
        &NetwSynchronizers::meta_key
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("of_node", "target"),
        &NetwSynchronizers::of_node
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("owned_by", "target"),
        &NetwSynchronizers::owned_by
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("synchronized_properties", "target"),
        &NetwSynchronizers::synchronized_properties
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("governed_targets", "sync", "root"),
        &NetwSynchronizers::governed_targets
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("display_bindings", "sync", "root"),
        &NetwSynchronizers::display_bindings
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("resolve_value", "target", "path"),
        &NetwSynchronizers::resolve_value
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("assign_value", "target", "path", "value"),
        &NetwSynchronizers::assign_value
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("sync_only_server", "target"),
        &NetwSynchronizers::sync_only_server
    );
    ClassDB::bind_static_method(
        "NetwSynchronizers",
        D_METHOD("clear_cache", "target"),
        &NetwSynchronizers::clear_cache
    );
}

} // namespace netw
