#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/utility.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwSceneCore : public godot::RefCounted {
public:
    enum Move {
        MOVE_REFUSED,
        MOVE_ALREADY_THERE,
        MOVE_CARRY,
    };

    enum Isolation {
        ISOLATION_NONE = 0,
        ISOLATION_OWN_WORLD = 1,
    };

    enum Event {
        EVENT_PLAYER = 0,
        EVENT_PLAYER_ENTITY = 1,
        EVENT_ENTITY = 2,
    };

private:
    struct Row {
        int32_t event = 0;
        godot::RID scene;
        godot::LocalVector<godot::Callable> callbacks;
    };

    struct LiveRow {
        godot::RID scene;
        godot::StringName stem;
        bool owns_its_world = false;
        godot::LocalVector<int64_t> parked;
    };

    godot::LocalVector<Row> rows;
    godot::LocalVector<LiveRow> live;
    godot::HashMap<godot::StringName, godot::RID> named;

    Row *row_for(int event, const godot::RID &scene);
    void promote_survivors();

public:
    void observe(
        const godot::RID &scene,
        int event,
        const godot::Callable &callback
    );

    void unobserve(
        const godot::RID &scene,
        int event,
        const godot::Callable &callback
    );

    int dispatch(
        const godot::RID &scene,
        int event,
        bool present,
        const godot::Variant &subject
    );

    int observer_count(const godot::RID &scene, int event) const;

    void forget_scene(const godot::RID &scene);

    void scene_enter(
        const godot::RID &scene,
        const godot::StringName &stem,
        bool owns_its_world
    );
    void scene_exit(const godot::RID &scene);

    bool is_live(const godot::RID &scene) const;
    godot::RID scene_named(const godot::StringName &stem) const;
    godot::Array scenes_named(const godot::StringName &stem) const;
    godot::Array live_scenes() const;
    godot::StringName stem_of(const godot::RID &scene) const;
    int live_count() const;

    static bool isolation_owns_world(int isolation);

    static Move move_verdict(
        bool mover_live,
        bool target_live,
        bool same_scene
    );

    bool admission_park(const godot::RID &scene, int64_t peer);
    bool admission_unpark(const godot::RID &scene, int64_t peer);
    bool admission_is_parked(const godot::RID &scene, int64_t peer) const;

    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwSceneCore::Event);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Isolation);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Move);
