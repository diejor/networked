#include "netw/scene_core.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

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
    // The walk runs over a COPY, and the row is found again afterwards. A
    // callback is arbitrary user code: it may register another observer, and a
    // registration that grows the table moves every row in it, so both the
    // vector being iterated and the pointer to the row would dangle mid-walk.
    // Copying is what makes the dispatch survive its own callbacks.
    // Direct-initialised, never copy-initialised: the engine's LocalVector copy
    // constructor is `explicit` where godot-cpp's is not, so `= row->callbacks`
    // compiles in the library tier and fails the module build twelve minutes
    // later.
    const LocalVector<Callable> walking(row->callbacks);
    const uint32_t started_with = walking.size();

    // The live set is built as the walk goes, so a callable whose object died
    // since the last edge is dropped by the one dispatch that finds it.
    LocalVector<Callable> live;
    Array args;
    args.push_back(present);
    args.push_back(subject);
    for (uint32_t index = 0; index < walking.size(); ++index) {
        const Callable callback = walking[index];
        if (!callback.is_valid()) {
            continue;
        }
        live.push_back(callback);
        callback.callv(args);
    }
    const int called = int(live.size());
    if (live.size() == started_with) {
        return called;
    }
    Row *current = row_for(event, scene);
    if (current == nullptr) {
        return called;
    }
    // Anything registered DURING the walk is kept: the pruning is only entitled
    // to drop what it found dead, not to roll the row back to what it saw.
    for (uint32_t index = started_with; index < current->callbacks.size();
         ++index) {
        live.push_back(current->callbacks[index]);
    }
    current->callbacks = live;
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

int NetwSceneCore::open_request() {
    pending_request_id = next_request_id;
    next_request_id += 1;
    return pending_request_id;
}

bool NetwSceneCore::is_current(int request_id) const {
    return request_id != 0 && request_id == pending_request_id;
}

void NetwSceneCore::close_request() {
    pending_request_id = 0;
}

int NetwSceneCore::get_pending_request_id() const {
    return pending_request_id;
}

int NetwSceneCore::get_next_request_id() const {
    return next_request_id;
}

void NetwSceneCore::set_next_request_id(int value) {
    next_request_id = value;
}

void NetwSceneCore::clear() {
    rows.clear();
    next_request_id = 1;
    pending_request_id = 0;
}

void NetwSceneCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("observe", "scene", "event", "callback"),
        &NetwSceneCore::observe
    );
    ClassDB::bind_method(
        D_METHOD("unobserve", "scene", "event", "callback"),
        &NetwSceneCore::unobserve
    );
    ClassDB::bind_method(
        D_METHOD("dispatch", "scene", "event", "present", "subject"),
        &NetwSceneCore::dispatch
    );
    ClassDB::bind_method(
        D_METHOD("observer_count", "scene", "event"),
        &NetwSceneCore::observer_count
    );
    ClassDB::bind_method(
        D_METHOD("forget_scene", "scene"),
        &NetwSceneCore::forget_scene
    );

    ClassDB::bind_method(
        D_METHOD("open_request"),
        &NetwSceneCore::open_request
    );
    ClassDB::bind_method(
        D_METHOD("is_current", "request_id"),
        &NetwSceneCore::is_current
    );
    ClassDB::bind_method(
        D_METHOD("close_request"),
        &NetwSceneCore::close_request
    );
    ClassDB::bind_method(
        D_METHOD("get_pending_request_id"),
        &NetwSceneCore::get_pending_request_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "pending_request_id"),
        "",
        "get_pending_request_id"
    );
    ClassDB::bind_method(
        D_METHOD("set_next_request_id", "value"),
        &NetwSceneCore::set_next_request_id
    );
    ClassDB::bind_method(
        D_METHOD("get_next_request_id"),
        &NetwSceneCore::get_next_request_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "next_request_id"),
        "set_next_request_id",
        "get_next_request_id"
    );

    ClassDB::bind_method(D_METHOD("clear"), &NetwSceneCore::clear);
}

} // namespace netw
