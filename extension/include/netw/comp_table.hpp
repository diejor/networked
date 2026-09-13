#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwCompTable {
public:
    enum Address {
        ADDRESS_ROOT,
        ADDRESS_MAPPED,
        ADDRESS_RELATIVE,
        ADDRESS_HOSTILE,
        ADDRESS_UNMAPPED,
    };

    static constexpr int64_t MAX_ID = 254;

    static constexpr int64_t FALLBACK_COMP = 255;

    bool poisoned = false;

    int64_t table_hash = 0;

    int64_t wire_hash = 0;

    void assign(const godot::PackedStringArray &p_paths);

    godot::PackedStringArray sorted_paths() const;

    godot::String path_for_id(int64_t p_id) const;

    int64_t id_for_path(const godot::String &p_path) const;

    bool has_path(const godot::String &p_path) const;

    int64_t classify(int64_t p_comp, const godot::String &p_path) const;

    godot::Node *resolve_node(
        godot::Object *p_owner,
        int64_t p_comp,
        const godot::String &p_path
    ) const;

    static bool path_shape_is_safe(const godot::String &p_path);

    bool reconcile(bool p_is_authority);

    bool get_poisoned() const {
        return poisoned;
    }
    void set_poisoned(bool p_poisoned) {
        poisoned = p_poisoned;
    }
    int64_t get_table_hash() const {
        return table_hash;
    }
    void set_table_hash(int64_t p_hash) {
        table_hash = p_hash;
    }
    int64_t get_wire_hash() const {
        return wire_hash;
    }
    void set_wire_hash(int64_t p_hash) {
        wire_hash = p_hash;
    }

private:
    godot::LocalVector<godot::String> paths;
};

} // namespace netw
