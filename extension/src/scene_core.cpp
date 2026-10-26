#include "netw/scene_core.hpp"

#include "netw/colors.hpp"
#include "netw/game_call.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

const GameCall SCENE_OBSERVER = {
    sys::SCENE,
    "The NetwScene.observe callback",
    "func(present: bool, subject) -> bool",
    2
};

} // namespace

NetwSceneCore::Row *NetwSceneCore::row_for(int event, const RID &scene) {
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].event == event && rows[index].scene == scene) {
            return &rows[index];
        }
    }
    return nullptr;
}

void NetwSceneCore::observe(
    const RID &scene,
    int event,
    const Callable &callback
) {
    if (!scene.is_valid() || !callback.is_valid()) {
        return;
    }
    game_call_declared(callback, SCENE_OBSERVER);
    Row *row = row_for(event, scene);
    if (row == nullptr) {
        Row fresh;
        fresh.event = event;
        fresh.scene = scene;
        rows.push_back(fresh);
        row = &rows[rows.size() - 1];
    }
    for (uint32_t index = 0; index < row->callbacks.size(); ++index) {
        if (row->callbacks[index] == callback) {
            return;
        }
    }
    row->callbacks.push_back(callback);
}

void NetwSceneCore::unobserve(
    const RID &scene,
    int event,
    const Callable &callback
) {
    Row *row = row_for(event, scene);
    if (row == nullptr) {
        return;
    }
    LocalVector<Callable> kept;
    for (uint32_t index = 0; index < row->callbacks.size(); ++index) {
        if (!(row->callbacks[index] == callback)) {
            kept.push_back(row->callbacks[index]);
        }
    }
    row->callbacks = kept;
}

int NetwSceneCore::dispatch(
    const RID &scene,
    int event,
    bool present,
    const Variant &subject
) {
    NETW_ZONE_NC("NetwSceneCore dispatch", colors::SCENE);
    const Row *row = row_for(event, scene);
    if (row == nullptr || row->callbacks.is_empty()) {
        return 0;
    }
    const LocalVector<Callable> walking(row->callbacks);
    const uint32_t started_with = walking.size();

    LocalVector<Callable> surviving;
    Array args;
    args.push_back(present);
    args.push_back(subject);
    for (uint32_t index = 0; index < walking.size(); ++index) {
        const Callable callback = walking[index];
        if (!callback.is_valid()) {
            continue;
        }
        const Variant answer = game_call(callback, args, SCENE_OBSERVER);
        if (answer.get_type() == Variant::BOOL && bool(answer)) {
            NETW_TRACE(
                sys::SCENE,
                "An observer of event %d asked to stop.",
                event
            );
            continue;
        }
        surviving.push_back(callback);
    }
    const int called = int(surviving.size());
    if (surviving.size() == started_with) {
        return called;
    }
    Row *current = row_for(event, scene);
    if (current == nullptr) {
        return called;
    }
    for (uint32_t index = started_with; index < current->callbacks.size();
         ++index) {
        surviving.push_back(current->callbacks[index]);
    }
    current->callbacks = surviving;
    return called;
}

int NetwSceneCore::observer_count(const RID &scene, int event) const {
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].event == event && rows[index].scene == scene) {
            return int(rows[index].callbacks.size());
        }
    }
    return 0;
}

void NetwSceneCore::forget_scene(const RID &scene) {
    LocalVector<Row> kept;
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (!(rows[index].scene == scene)) {
            kept.push_back(rows[index]);
        }
    }
    rows = kept;
}

void NetwSceneCore::promote_survivors() {
    for (uint32_t index = 0; index < live.size(); ++index) {
        if (!named.has(live[index].stem)) {
            named[live[index].stem] = live[index].scene;
        }
    }
}

void NetwSceneCore::scene_enter(
    const RID &scene,
    const StringName &stem,
    bool owns_its_world
) {
    if (!scene.is_valid()) {
        return;
    }
    for (uint32_t index = 0; index < live.size(); ++index) {
        if (live[index].scene == scene) {
            live[index].stem = stem;
            live[index].owns_its_world = owns_its_world;
            named[stem] = scene;
            return;
        }
    }
    LiveRow row;
    row.scene = scene;
    row.stem = stem;
    row.owns_its_world = owns_its_world;
    live.push_back(row);
    named[stem] = scene;
    NETW_TRACE(sys::SCENE, "Scene %s entered as '%s'.", scene, String(stem));
}

void NetwSceneCore::scene_exit(const RID &scene) {
    LocalVector<LiveRow> kept;
    StringName leaving;
    for (uint32_t index = 0; index < live.size(); ++index) {
        if (live[index].scene == scene) {
            leaving = live[index].stem;
        } else {
            kept.push_back(live[index]);
        }
    }
    live = kept;
    const RID *answered = named.getptr(leaving);
    if (answered != nullptr && *answered == scene) {
        named.erase(leaving);
    }
    promote_survivors();
}

bool NetwSceneCore::is_live(const RID &scene) const {
    for (uint32_t index = 0; index < live.size(); ++index) {
        if (live[index].scene == scene) {
            return true;
        }
    }
    return false;
}

RID NetwSceneCore::scene_named(const StringName &stem) const {
    const RID *found = named.getptr(stem);
    return found ? *found : RID();
}

Array NetwSceneCore::scenes_named(const StringName &stem) const {
    Array out;
    for (uint32_t index = 0; index < live.size(); ++index) {
        if (live[index].stem == stem) {
            out.push_back(live[index].scene);
        }
    }
    return out;
}

Array NetwSceneCore::live_scenes() const {
    Array out;
    for (uint32_t index = 0; index < live.size(); ++index) {
        out.push_back(live[index].scene);
    }
    return out;
}

StringName NetwSceneCore::stem_of(const RID &scene) const {
    for (uint32_t index = 0; index < live.size(); ++index) {
        if (live[index].scene == scene) {
            return live[index].stem;
        }
    }
    return StringName();
}

int NetwSceneCore::live_count() const {
    return int(live.size());
}

NetwSceneCore::Move NetwSceneCore::move_verdict(
    bool p_mover_live,
    bool p_target_live,
    bool p_same_scene
) {
    if (!p_mover_live || !p_target_live) {
        return MOVE_REFUSED;
    }
    if (p_same_scene) {
        return MOVE_ALREADY_THERE;
    }
    return MOVE_CARRY;
}

bool NetwSceneCore::isolation_owns_world(int isolation) {
    return isolation == ISOLATION_OWN_WORLD;
}

void NetwSceneCore::clear() {
    rows.clear();
    live.clear();
    named.clear();
}

bool NetwSceneCore::admission_park(const RID &p_scene, int64_t p_peer) {
    for (uint32_t at = 0; at < live.size(); at++) {
        if (!(live[at].scene == p_scene)) {
            continue;
        }
        LocalVector<int64_t> &parked = live[at].parked;
        for (uint32_t seat = 0; seat < parked.size(); seat++) {
            if (parked[seat] == p_peer) {
                return false;
            }
        }
        NETW_TRACE(
            sys::SCENE,
            "Peer %d waits for scene %s until it joins the roster.",
            int(p_peer),
            p_scene
        );
        parked.push_back(p_peer);
        return true;
    }
    return false;
}

bool NetwSceneCore::admission_unpark(const RID &p_scene, int64_t p_peer) {
    for (uint32_t at = 0; at < live.size(); at++) {
        if (!(live[at].scene == p_scene)) {
            continue;
        }
        LocalVector<int64_t> &parked = live[at].parked;
        LocalVector<int64_t> kept;
        bool found = false;
        for (uint32_t seat = 0; seat < parked.size(); seat++) {
            if (parked[seat] == p_peer) {
                found = true;
            } else {
                kept.push_back(parked[seat]);
            }
        }
        if (found) {
            parked = kept;
        }
        return found;
    }
    return false;
}

bool NetwSceneCore::admission_is_parked(
    const RID &p_scene,
    int64_t p_peer
) const {
    for (uint32_t at = 0; at < live.size(); at++) {
        if (!(live[at].scene == p_scene)) {
            continue;
        }
        for (uint32_t seat = 0; seat < live[at].parked.size(); seat++) {
            if (live[at].parked[seat] == p_peer) {
                return true;
            }
        }
        return false;
    }
    return false;
}

} // namespace netw
