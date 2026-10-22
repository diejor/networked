#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/utility.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwScene;

class NetwSceneCore : public godot::RefCounted {
public:
    static constexpr int MAX_SCENE_PATH_LENGTH = 512;

    enum Scope {
        SCOPE_SESSION = 0,
        SCOPE_PLAYER = 1,
        SCOPE_SCENE = 2,
        SCOPE_MAX = 3,
    };

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

    enum Destination {
        DESTINATION_NONE,
        DESTINATION_NAME,
        DESTINATION_NODE,
        DESTINATION_PACKED,
        DESTINATION_PACKED_UNPATHED,
    };

    struct LoadingChange {
        godot::String path;
        godot::Ref<NetwPromise> promise;
        godot::ObjectID requester;
        int scope = SCOPE_SESSION;
        uint64_t generation = 0;
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

    struct RetiringRow {
        godot::RID scene;
        int32_t pumps_left = 0;
    };

    struct Transition {
        godot::Variant target;
        godot::Array sources;
        godot::Ref<NetwPromise> promise;
        int scope = SCOPE_SESSION;
        godot::PackedInt64Array arrivals;
    };

    Transition transition;
    LoadingChange loading;

    godot::LocalVector<Row> rows;
    godot::LocalVector<LiveRow> live;
    godot::LocalVector<RetiringRow> retiring;
    godot::HashMap<godot::StringName, godot::RID> named;

    bool replacing = false;
    bool transitioning = false;

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

    static bool scope_names_an_operation(int scope);
    void forget_scene(const godot::RID &scene);

    void scene_enter(
        const godot::RID &scene,
        const godot::StringName &stem,
        bool owns_its_world
    );
    void scene_exit(const godot::RID &scene);
    void scene_retire(const godot::RID &scene, int drain_pumps);
    godot::Array pump_retired();
    godot::Array retiring_scenes() const;

    bool is_live(const godot::RID &scene) const;
    godot::RID scene_named(const godot::StringName &stem) const;
    godot::Array scenes_named(const godot::StringName &stem) const;
    godot::Array live_scenes() const;
    godot::StringName stem_of(const godot::RID &scene) const;
    int live_count() const;
    bool would_share_one_world() const;

    bool transition_open();
    bool transition_busy() const {
        return transitioning;
    }
    void transition_arm(
        const godot::Variant &target,
        const godot::Array &sources,
        const godot::Ref<NetwPromise> &promise,
        int scope,
        const godot::PackedInt64Array &arrivals
    );
    godot::Variant transition_target() const;
    godot::Array transition_sources() const;
    int transition_scope() const;
    godot::PackedInt64Array transition_arrivals() const;
    void transition_fail(int code);
    void transition_land(const godot::Ref<NetwScene> &destination);
    void transition_close();

    bool change_loading() const {
        return loading.promise.is_valid();
    }
    const godot::String &loading_path() const {
        return loading.path;
    }
    void change_wait_for_load(const LoadingChange &change);
    LoadingChange change_take_loaded();

    static godot::String verify_scene_path(const godot::String &path);
    static godot::Ref<godot::PackedScene> packed_at(
        const godot::String &reference
    );
    static godot::Ref<godot::Script> packed_root_script(
        const godot::Ref<godot::PackedScene> &packed
    );
    static godot::Ref<godot::Script> scene_root_script_at(
        const godot::String &reference
    );
    static Destination destination_kind(const godot::Variant &destination);

    static bool isolation_owns_world(int isolation);

    static Move move_verdict(
        bool mover_live,
        bool target_live,
        bool same_scene
    );

    bool admission_park(const godot::RID &scene, int64_t peer);
    bool admission_unpark(const godot::RID &scene, int64_t peer);
    bool admission_is_parked(const godot::RID &scene, int64_t peer) const;

    bool spawn_warns_shared_world() const;
    void spawn_note(const godot::StringName &stem) const;
    bool is_replacing() const;
    void set_replacing(bool value);

    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwSceneCore::Event);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Isolation);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Move);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Destination);
