#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/spawn/record.hpp"

namespace netw::spawn {

class Book {
public:
    enum Recipe {
        RECIPE_SCENE = 0,
        RECIPE_FN = 1,
        RECIPE_SPAWNER = 2,
        RECIPE_ADOPT = 3,
        RECIPE_FN_REGISTRY = 4,
    };

    struct Received {
        godot::ObjectID node;
        godot::ObjectID source;
        godot::StringName fn;
        godot::Variant payload;
        int32_t recipe = RECIPE_ADOPT;
        int32_t scene_index = -1;
        bool authored = false;
    };

private:
    godot::HashMap<int64_t, Record> armed;
    godot::HashMap<int64_t, Record> spawned;
    godot::HashMap<int64_t, Received> received;
    godot::HashMap<uint64_t, int> node_holds;

    void hold_node(const Record &record);
    void release_node(const Record &record);

public:
    void arm(const Record &record);
    bool take_armed(int64_t route, Record &out);
    bool has_armed(int64_t route) const;
    bool drop_armed(int64_t route);
    bool books_node(godot::Node *node) const;
    int armed_count() const;

    Record *issue(const Record &record);
    Record *spawned_of(int64_t route);
    const Record *spawned_of(int64_t route) const;
    bool has_spawned(int64_t route) const;
    bool drop_spawned(int64_t route);
    godot::PackedInt64Array spawned_routes() const;
    int spawned_count() const;

    godot::PackedInt64Array ancestry_order() const;

    godot::PackedInt64Array despawn_order(int64_t route) const;
    godot::PackedInt64Array detach_roots(
        int64_t route,
        const godot::PackedInt64Array &detaching
    ) const;

    bool parent_admits(int64_t route, int64_t peer) const;

    bool spawn_is_duplicate(int64_t route, int64_t state) const;

    static godot::String recipe_base(
        const Record *record,
        const godot::String &fallback_scene_path,
        const godot::String &fallback_name
    );

    static Received received_of(const Record &record);
    void enroll_recv(int64_t route, const Received &recipe);
    bool is_recv(int64_t route) const;
    bool drop_recv(int64_t route);
    bool take_recv(int64_t route, Record &out);
    godot::PackedInt64Array recv_routes() const;
    godot::PackedInt64Array authored_routes() const;
    int recv_count() const;
    int64_t held_bytes() const;

    void clear();
};

} // namespace netw::spawn
