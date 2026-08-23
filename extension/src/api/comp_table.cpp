#include "netw/api/comp_table.hpp"

#include <algorithm>

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

void NetwCompTable::assign(const PackedStringArray &p_paths) {
    paths.clear();
    paths.reserve(p_paths.size());
    for (int64_t i = 0; i < p_paths.size(); ++i) {
        const String path = p_paths[i];
        if (!path.is_empty()) {
            paths.push_back(path);
        }
    }
    std::stable_sort(paths.ptr(), paths.ptr() + paths.size());
}

PackedStringArray NetwCompTable::sorted_paths() const {
    PackedStringArray out;
    out.resize(paths.size());
    for (uint32_t i = 0; i < paths.size(); ++i) {
        out.set(int64_t(i), paths[i]);
    }
    return out;
}

String NetwCompTable::path_for_id(int64_t p_id) const {
    if (p_id < 1 || p_id > MAX_ID || uint32_t(p_id) > paths.size()) {
        return String();
    }
    return paths[uint32_t(p_id - 1)];
}

int64_t NetwCompTable::id_for_path(const String &p_path) const {
    if (paths.is_empty() || p_path.is_empty()) {
        return 0;
    }
    const String *begin = paths.ptr();
    const String *found = std::lower_bound(begin, begin + paths.size(), p_path);
    if (found == begin + paths.size() || *found != p_path) {
        return 0;
    }
    const int64_t id = int64_t(found - begin) + 1;
    return id <= MAX_ID ? id : 0;
}

bool NetwCompTable::has_path(const String &p_path) const {
    return id_for_path(p_path) != 0;
}

int64_t NetwCompTable::classify(int64_t p_comp, const String &p_path) const {
    if (p_comp == 0) {
        return ADDRESS_ROOT;
    }
    if (p_comp == FALLBACK_COMP) {
        return path_shape_is_safe(p_path) ? ADDRESS_RELATIVE : ADDRESS_HOSTILE;
    }
    if (poisoned || path_for_id(p_comp).is_empty()) {
        return ADDRESS_UNMAPPED;
    }
    return ADDRESS_MAPPED;
}

Node *NetwCompTable::resolve_node(
    Object *p_owner,
    int64_t p_comp,
    const String &p_path
) const {
    Node *owner = Object::cast_to<Node>(p_owner);
    if (owner == nullptr) {
        return nullptr;
    }
    switch (classify(p_comp, p_path)) {
        case ADDRESS_ROOT:
            return owner;
        case ADDRESS_MAPPED:
            return owner->get_node_or_null(NodePath(path_for_id(p_comp)));
        case ADDRESS_RELATIVE: {
            Node *found = owner->get_node_or_null(NodePath(p_path));
            if (found == nullptr) {
                return nullptr;
            }
            return found == owner || owner->is_ancestor_of(found) ? found
                                                                  : nullptr;
        }
        default:
            return nullptr;
    }
}

bool NetwCompTable::path_shape_is_safe(const String &p_path) {
    return !(
        p_path.begins_with("/") || p_path.contains("..")
        || p_path.begins_with("res://") || p_path.begins_with("user://")
    );
}

bool NetwCompTable::reconcile(bool p_is_authority) {
    if (p_is_authority) {
        wire_hash = table_hash;
        return false;
    }
    if (wire_hash == table_hash) {
        return false;
    }
    poisoned = true;
    return true;
}

void NetwCompTable::_bind_methods() {
    ClassDB::bind_method(D_METHOD("assign", "paths"), &NetwCompTable::assign);
    ClassDB::bind_method(
        D_METHOD("sorted_paths"),
        &NetwCompTable::sorted_paths
    );
    ClassDB::bind_method(
        D_METHOD("path_for_id", "id"),
        &NetwCompTable::path_for_id
    );
    ClassDB::bind_method(
        D_METHOD("id_for_path", "path"),
        &NetwCompTable::id_for_path
    );
    ClassDB::bind_method(
        D_METHOD("has_path", "path"),
        &NetwCompTable::has_path
    );
    ClassDB::bind_method(
        D_METHOD("classify", "comp", "path"),
        &NetwCompTable::classify
    );
    ClassDB::bind_method(
        D_METHOD("resolve_node", "owner", "comp", "path"),
        &NetwCompTable::resolve_node
    );
    ClassDB::bind_static_method(
        "NetwCompTable",
        D_METHOD("path_shape_is_safe", "path"),
        &NetwCompTable::path_shape_is_safe
    );
    ClassDB::bind_method(
        D_METHOD("reconcile", "is_authority"),
        &NetwCompTable::reconcile
    );

    ClassDB::bind_method(D_METHOD("get_poisoned"), &NetwCompTable::get_poisoned);
    ClassDB::bind_method(
        D_METHOD("set_poisoned", "poisoned"),
        &NetwCompTable::set_poisoned
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "poisoned"),
        "set_poisoned",
        "get_poisoned"
    );

    ClassDB::bind_method(
        D_METHOD("get_table_hash"),
        &NetwCompTable::get_table_hash
    );
    ClassDB::bind_method(
        D_METHOD("set_table_hash", "hash"),
        &NetwCompTable::set_table_hash
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "table_hash"),
        "set_table_hash",
        "get_table_hash"
    );

    ClassDB::bind_method(
        D_METHOD("get_wire_hash"),
        &NetwCompTable::get_wire_hash
    );
    ClassDB::bind_method(
        D_METHOD("set_wire_hash", "hash"),
        &NetwCompTable::set_wire_hash
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "wire_hash"),
        "set_wire_hash",
        "get_wire_hash"
    );

    BIND_ENUM_CONSTANT(ADDRESS_ROOT);
    BIND_ENUM_CONSTANT(ADDRESS_MAPPED);
    BIND_ENUM_CONSTANT(ADDRESS_RELATIVE);
    BIND_ENUM_CONSTANT(ADDRESS_HOSTILE);
    BIND_ENUM_CONSTANT(ADDRESS_UNMAPPED);
}

}
