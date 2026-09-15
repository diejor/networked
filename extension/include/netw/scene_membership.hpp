#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class SceneMembership {
public:
    enum Removal {
        REMOVAL_NONE = 0,
        REMOVAL_LAST_BODY = 1,
        REMOVAL_UNWATCHED = 2,
        REMOVAL_MEMBER_GONE = 3,
        REMOVAL_SCENE_RETIRED = 4,
    };

    struct Edge {
        int64_t member = 0;
        godot::RID scene;
        bool subscribed = false;
        Removal removal = REMOVAL_NONE;
    };

    static constexpr int64_t WATCH = 0;

private:
    struct Reason {
        int64_t member = 0;
        godot::RID scene;
        int64_t body = WATCH;
    };

    godot::LocalVector<Reason> reasons;
    godot::LocalVector<Edge> edges;

    int reason_at(int64_t p_member, const godot::RID &p_scene, int64_t p_body)
        const;
    int reasons_for(int64_t p_member, const godot::RID &p_scene) const;
    void announce(
        int64_t p_member,
        const godot::RID &p_scene,
        bool p_subscribed,
        Removal p_removal
    );
    bool add(int64_t p_member, const godot::RID &p_scene, int64_t p_body);
    bool drop(
        int64_t p_member,
        const godot::RID &p_scene,
        int64_t p_body,
        Removal p_removal
    );

public:
    bool body_enter(
        int64_t p_member,
        const godot::RID &p_scene,
        int64_t p_body
    );
    bool body_exit(int64_t p_member, const godot::RID &p_scene, int64_t p_body);
    bool watch(int64_t p_member, const godot::RID &p_scene);
    bool unwatch(int64_t p_member, const godot::RID &p_scene);

    bool watches(int64_t p_member, const godot::RID &p_scene) const;
    bool subscribes(int64_t p_member, const godot::RID &p_scene) const;
    int bodies_in(int64_t p_member, const godot::RID &p_scene) const;
    int subscriptions_of(int64_t p_member) const;
    void scenes_of(
        int64_t p_member,
        godot::LocalVector<godot::RID> &r_scenes
    ) const;
    void body_scenes_of(
        int64_t p_member,
        godot::LocalVector<godot::RID> &r_scenes
    ) const;
    void scenes_of_body(
        int64_t p_member,
        int64_t p_body,
        godot::LocalVector<godot::RID> &r_scenes
    ) const;
    godot::PackedInt64Array bodies_of(
        int64_t p_member,
        const godot::RID &p_scene
    ) const;
    godot::PackedInt64Array members_of(const godot::RID &p_scene) const;
    godot::PackedInt64Array watchers_of(const godot::RID &p_scene) const;
    int size() const;

    void forget_member(int64_t p_member);
    void retire_scene(const godot::RID &p_scene);
    void clear();

    const godot::LocalVector<Edge> &pending_edges() const;
    void clear_edges();
};

#if defined(NETW_TESTS)

namespace residency_audit {

struct Ledger {
    int64_t observations = 0;
    int64_t divergences = 0;
    int64_t entered = 0;
    int64_t left = 0;
    int64_t member_of_first = 0;
    int64_t scene_of_first = 0;
    int64_t body_of_first = 0;
};

Ledger &ledger();

} // namespace residency_audit

#endif

} // namespace netw
