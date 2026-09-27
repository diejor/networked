#include "netw/scene_membership.hpp"

using namespace godot;

namespace netw {

int SceneMembership::reason_at(
    int64_t p_member,
    const RID &p_scene,
    int64_t p_body
) const {
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.member == p_member && held.scene == p_scene
            && held.body == p_body) {
            return int(at);
        }
    }
    return -1;
}

int SceneMembership::reasons_for(int64_t p_member, const RID &p_scene) const {
    int total = 0;
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.member == p_member && held.scene == p_scene) {
            total++;
        }
    }
    return total;
}

void SceneMembership::announce(
    int64_t p_member,
    const RID &p_scene,
    bool p_subscribed,
    Removal p_removal
) {
    Edge edge;
    edge.member = p_member;
    edge.scene = p_scene;
    edge.subscribed = p_subscribed;
    edge.removal = p_removal;
    edges.push_back(edge);
}

bool SceneMembership::add(
    int64_t p_member,
    const RID &p_scene,
    int64_t p_body
) {
    if (p_member == 0 || !p_scene.is_valid()) {
        return false;
    }
    if (reason_at(p_member, p_scene, p_body) >= 0) {
        return false;
    }
    const bool opened = reasons_for(p_member, p_scene) == 0;
    Reason held;
    held.member = p_member;
    held.scene = p_scene;
    held.body = p_body;
    reasons.push_back(held);
    if (opened) {
        announce(p_member, p_scene, true, REMOVAL_NONE);
    }
    return true;
}

bool SceneMembership::drop(
    int64_t p_member,
    const RID &p_scene,
    int64_t p_body,
    Removal p_removal
) {
    const int at = reason_at(p_member, p_scene, p_body);
    if (at < 0) {
        return false;
    }
    reasons.remove_at_unordered(uint32_t(at));
    if (reasons_for(p_member, p_scene) == 0) {
        announce(p_member, p_scene, false, p_removal);
    }
    return true;
}

bool SceneMembership::body_enter(
    int64_t p_member,
    const RID &p_scene,
    int64_t p_body
) {
    if (p_body == WATCH) {
        return false;
    }
    return add(p_member, p_scene, p_body);
}

bool SceneMembership::body_exit(
    int64_t p_member,
    const RID &p_scene,
    int64_t p_body
) {
    if (p_body == WATCH) {
        return false;
    }
    return drop(p_member, p_scene, p_body, REMOVAL_LAST_BODY);
}

bool SceneMembership::watch(int64_t p_member, const RID &p_scene) {
    return add(p_member, p_scene, WATCH);
}

bool SceneMembership::unwatch(int64_t p_member, const RID &p_scene) {
    return drop(p_member, p_scene, WATCH, REMOVAL_UNWATCHED);
}

bool SceneMembership::watches(int64_t p_member, const RID &p_scene) const {
    return reason_at(p_member, p_scene, WATCH) >= 0;
}

bool SceneMembership::subscribes(int64_t p_member, const RID &p_scene) const {
    return reasons_for(p_member, p_scene) > 0;
}

int SceneMembership::bodies_in(int64_t p_member, const RID &p_scene) const {
    int total = 0;
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.member == p_member && held.scene == p_scene
            && held.body != WATCH) {
            total++;
        }
    }
    return total;
}

void SceneMembership::scenes_of(
    int64_t p_member,
    LocalVector<RID> &r_scenes
) const {
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.member != p_member || r_scenes.has(held.scene)) {
            continue;
        }
        r_scenes.push_back(held.scene);
    }
}

void SceneMembership::body_scenes_of(
    int64_t p_member,
    LocalVector<RID> &r_scenes
) const {
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.member != p_member || held.body == WATCH
            || r_scenes.has(held.scene)) {
            continue;
        }
        r_scenes.push_back(held.scene);
    }
}

void SceneMembership::scenes_of_body(
    int64_t p_member,
    int64_t p_body,
    LocalVector<RID> &r_scenes
) const {
    if (p_body == WATCH) {
        return;
    }
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.member != p_member || held.body != p_body
            || r_scenes.has(held.scene)) {
            continue;
        }
        r_scenes.push_back(held.scene);
    }
}

PackedInt64Array SceneMembership::bodies_of(
    int64_t p_member,
    const RID &p_scene
) const {
    LocalVector<int64_t> found;
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.member != p_member || held.scene != p_scene
            || held.body == WATCH || found.has(held.body)) {
            continue;
        }
        found.push_back(held.body);
    }
    found.sort();
    PackedInt64Array out;
    out.resize(int(found.size()));
    for (uint32_t at = 0; at < found.size(); ++at) {
        out.set(int(at), found[at]);
    }
    return out;
}

int SceneMembership::subscriptions_of(int64_t p_member) const {
    LocalVector<RID> scenes;
    scenes_of(p_member, scenes);
    return int(scenes.size());
}

PackedInt64Array SceneMembership::watchers_of(const RID &p_scene) const {
    LocalVector<int64_t> found;
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.scene != p_scene || held.body != WATCH
            || found.has(held.member)) {
            continue;
        }
        found.push_back(held.member);
    }
    found.sort();
    PackedInt64Array out;
    out.resize(int(found.size()));
    for (uint32_t at = 0; at < found.size(); ++at) {
        out.set(int(at), found[at]);
    }
    return out;
}

PackedInt64Array SceneMembership::members_of(const RID &p_scene) const {
    LocalVector<int64_t> found;
    for (uint32_t at = 0; at < reasons.size(); ++at) {
        const Reason &held = reasons[at];
        if (held.scene != p_scene || found.has(held.member)) {
            continue;
        }
        found.push_back(held.member);
    }
    found.sort();
    PackedInt64Array out;
    out.resize(int(found.size()));
    for (uint32_t at = 0; at < found.size(); ++at) {
        out.set(int(at), found[at]);
    }
    return out;
}

int SceneMembership::size() const {
    return int(reasons.size());
}

void SceneMembership::forget_member(int64_t p_member) {
    LocalVector<RID> held;
    scenes_of(p_member, held);
    for (uint32_t at = 0; at < reasons.size();) {
        if (reasons[at].member == p_member) {
            reasons.remove_at_unordered(at);
            continue;
        }
        at++;
    }
    for (uint32_t at = 0; at < held.size(); ++at) {
        announce(p_member, held[at], false, REMOVAL_MEMBER_GONE);
    }
}

void SceneMembership::retire_scene(const RID &p_scene) {
    const PackedInt64Array held = members_of(p_scene);
    for (uint32_t at = 0; at < reasons.size();) {
        if (reasons[at].scene == p_scene) {
            reasons.remove_at_unordered(at);
            continue;
        }
        at++;
    }
    for (int at = 0; at < held.size(); ++at) {
        announce(held[at], p_scene, false, REMOVAL_SCENE_RETIRED);
    }
}

void SceneMembership::clear() {
    reasons.clear();
    edges.clear();
}

const LocalVector<SceneMembership::Edge> &SceneMembership::
    pending_edges() const {
    return edges;
}

void SceneMembership::clear_edges() {
    edges.clear();
}

#if defined(NETW_TESTS)

namespace residency_audit {

Ledger &ledger() {
    static Ledger held;
    return held;
}

} // namespace residency_audit

#endif

} // namespace netw
