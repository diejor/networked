#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/spawn_record.hpp"

namespace netw {

class NetwSpawnBook : public godot::RefCounted {
    GDCLASS(NetwSpawnBook, godot::RefCounted)

public:
    enum Recipe {
        RECIPE_SCENE = 0,
        RECIPE_FN = 1,
        RECIPE_SPAWNER = 2,
        RECIPE_ADOPT = 3,
        RECIPE_FN_REGISTRY = 4,
    };

private:
    godot::HashMap<int64_t, godot::Ref<NetwSpawnRecord>> armed;
    godot::HashMap<int64_t, godot::Ref<NetwSpawnRecord>> spawned;
    godot::HashMap<int64_t, godot::ObjectID> received;

protected:
    static void _bind_methods();

public:
    void arm(const godot::Ref<NetwSpawnRecord> &record);
    godot::Ref<NetwSpawnRecord> take_armed(int64_t route);
    bool has_armed(int64_t route) const;
    bool drop_armed(int64_t route);
    godot::Array armed_records() const;
    int armed_count() const;

    void issue(const godot::Ref<NetwSpawnRecord> &record);
    godot::Ref<NetwSpawnRecord> spawned_of(int64_t route) const;
    bool has_spawned(int64_t route) const;
    bool drop_spawned(int64_t route);
    godot::PackedInt64Array spawned_routes() const;
    godot::Array spawned_records() const;
    int spawned_count() const;

    godot::PackedInt64Array ancestry_order() const;

    godot::PackedInt64Array despawn_order(int64_t route) const;

    bool parent_admits(int64_t route, int64_t peer) const;

    bool spawn_is_duplicate(int64_t route, int64_t state) const;

    static godot::String recipe_base(
        const godot::Ref<NetwSpawnRecord> &record,
        const godot::String &fallback_scene_path,
        const godot::String &fallback_name
    );

    void enroll_recv(int64_t route, godot::Node *node);
    bool is_recv(int64_t route) const;
    bool drop_recv(int64_t route);
    int recv_count() const;

    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwSpawnBook::Recipe);
