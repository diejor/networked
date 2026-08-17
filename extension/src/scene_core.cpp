#include "netw/scene_core.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_NATIVE_CHANGE_SETTLED = "native_change_settled";

}

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
        surviving.push_back(callback);
        callback.callv(args);
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
    NETW_TRACE(sys::SCENE, "scene %s entered as '%s'", scene, String(stem));
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

void NetwSceneCore::scene_retire(const RID &scene, int drain_pumps) {
    if (!scene.is_valid()) {
        return;
    }
    scene_exit(scene);
    for (uint32_t index = 0; index < retiring.size(); ++index) {
        if (retiring[index].scene == scene) {
            retiring[index].pumps_left = drain_pumps > 0 ? drain_pumps : 0;
            return;
        }
    }
    RetiringRow row;
    row.scene = scene;
    row.pumps_left = drain_pumps > 0 ? drain_pumps : 0;
    retiring.push_back(row);
}

Array NetwSceneCore::pump_retired() {
    Array closed;
    LocalVector<RetiringRow> kept;
    for (uint32_t index = 0; index < retiring.size(); ++index) {
        const int32_t left = retiring[index].pumps_left - 1;
        if (left > 0) {
            RetiringRow row = retiring[index];
            row.pumps_left = left;
            kept.push_back(row);
        } else {
            closed.push_back(retiring[index].scene);
        }
    }
    retiring = kept;
    return closed;
}

Array NetwSceneCore::retiring_scenes() const {
    Array out;
    for (uint32_t index = 0; index < retiring.size(); ++index) {
        out.push_back(retiring[index].scene);
    }
    return out;
}

void NetwSceneCore::set_current_scene(const RID &scene) {
    current_scene = scene;
}

RID NetwSceneCore::get_current_scene() const {
    return current_scene;
}

RID NetwSceneCore::resolve_current(bool presents, const RID &seat) const {
    if (!presents) {
        return RID();
    }
    if (seat.is_valid()) {
        return seat;
    }
    for (uint32_t at = 0; at < live.size(); at++) {
        const RID *canonical = named.getptr(live[at].stem);
        if (canonical != nullptr && *canonical == live[at].scene) {
            return live[at].scene;
        }
    }
    return RID();
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

bool NetwSceneCore::would_share_one_world() const {
    if (live.is_empty()) {
        return false;
    }
    for (uint32_t index = 0; index < live.size(); ++index) {
        if (live[index].owns_its_world) {
            return false;
        }
    }
    return true;
}

bool NetwSceneCore::transition_open() {
    if (transitioning) {
        return false;
    }
    transitioning = true;
    return true;
}

void NetwSceneCore::transition_close() {
    transitioning = false;
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

void NetwSceneCore::settle_pending(int code) {
    if (pending_request.is_valid()) {
        if (code == OK) {
            pending_request->resolve(int(OK));
        } else {
            pending_request->reject(code, String());
        }
    }
    pending_request = Ref<NetwPromise>();
    pending_request_id = 0;
    const bool captured = pending_from_capture;
    pending_from_capture = false;
    if (captured) {
        emit_signal(StringName(SIG_NATIVE_CHANGE_SETTLED), code);
    }
}

Ref<NetwPromise> NetwSceneCore::request_open(bool from_capture) {
    if (pending_request.is_valid() && !pending_request->get_is_settled()) {
        settle_pending(int(ERR_SKIP));
    }
    pending_from_capture = from_capture;
    open_request();
    pending_request.instantiate();
    NETW_TRACE(
        sys::SCENE,
        "opened scene request %d captured=%d",
        pending_request_id,
        from_capture
    );
    return pending_request;
}

Ref<NetwPromise> NetwSceneCore::get_pending_request() const {
    return pending_request;
}

bool NetwSceneCore::request_settle(int request_id, int code) {
    if (pending_request.is_null() || !is_current(request_id)) {
        NETW_TRACE(
            sys::SCENE,
            "request %d answered while %d is in flight",
            request_id,
            pending_request_id
        );
        return false;
    }
    settle_pending(code);
    return true;
}

void NetwSceneCore::request_abandon(int code) {
    settle_pending(code);
}

bool NetwSceneCore::receive_result_frame(
    const PackedByteArray &payload,
    int sender
) {
    if (sender != 1) {
        NETW_WARN(sys::SCENE, "rejected a scene result from peer %d", sender);
        return false;
    }
    const Variant decoded = netw::gd::bytes_to_var(payload);
    if (decoded.get_type() != Variant::ARRAY) {
        return false;
    }
    const Array row = decoded;
    if (row.size() != 2) {
        return false;
    }
    return request_settle(int(row[0]), int(row[1]));
}

String NetwSceneCore::verify_requested_path(const String &path) {
    const bool bounded = !path.is_empty()
        && path.length() <= MAX_REQUESTED_PATH_LENGTH
        && path.begins_with("res://");
    const String extension = bounded ? path.get_extension() : String();
    if (!bounded || (extension != "tscn" && extension != "scn")) {
        NETW_TRACE(sys::SCENE, "refused a requested path: %s", path);
        return String();
    }
    return path;
}

bool NetwSceneCore::set_request_reach(int value) {
    NETW_ERR_COND_V(
        value < REACH_PARTICIPANT || value >= REACH_MAX,
        false,
        sys::SCENE,
        "Scene request reach %d names no reach.",
        value
    );
    request_reach = value;
    return true;
}

int NetwSceneCore::get_request_reach() const {
    return request_reach;
}

NetwSceneCore::Capture NetwSceneCore::capture_verdict(
    const NativeEntry &entry
) {
    if (!entry.has_path) {
        return CAPTURE_REFUSED;
    }
    if (!entry.is_server) {
        return CAPTURE_REQUEST;
    }
    if (entry.session_reach && entry.a_scene_is_active) {
        return CAPTURE_CHANGE_SESSION;
    }
    if (entry.listen_host && entry.has_local_participant
        && entry.here_owns_its_world) {
        return CAPTURE_MOVE_ME;
    }
    return CAPTURE_ACTIVATE;
}

bool NetwSceneCore::native_change_strands(bool p_online, bool p_marked) {
    return p_online && !p_marked;
}

int NetwSceneCore::native_entry_verdict(
    bool has_path,
    bool is_server,
    bool session_reach,
    bool a_scene_is_active,
    bool listen_host,
    bool has_local_participant,
    bool here_owns_its_world
) {
    NativeEntry entry;
    entry.has_path = has_path;
    entry.is_server = is_server;
    entry.session_reach = session_reach;
    entry.a_scene_is_active = a_scene_is_active;
    entry.listen_host = listen_host;
    entry.has_local_participant = has_local_participant;
    entry.here_owns_its_world = here_owns_its_world;
    const Capture verdict = capture_verdict(entry);
    NETW_TRACE(sys::SCENE, "a captured native entry reads %d", int(verdict));
    return int(verdict);
}

bool NetwSceneCore::admits_request(
    bool named,
    bool marked,
    bool gated,
    const String &path
) {
    const bool admitted = !path.is_empty() && (named || marked) && !gated;
    if (!admitted) {
        NETW_TRACE(
            sys::SCENE,
            "denied a scene request: named=%d marked=%d gated=%d path=%s",
            named,
            marked,
            gated,
            path
        );
    }
    return admitted;
}

void NetwSceneCore::set_request_handler(const Callable &handler) {
    request_handler = handler;
}

Callable NetwSceneCore::get_request_handler() const {
    return request_handler;
}

bool NetwSceneCore::decide_request(
    const Variant &participant,
    const Variant &destination,
    const Array &args,
    bool named,
    bool marked,
    bool gated,
    const String &path
) const {
    if (!request_handler.is_valid()) {
        return admits_request(named, marked, gated, path);
    }
    Array arguments;
    arguments.push_back(participant);
    arguments.push_back(destination);
    arguments.push_back(args);
    const Variant verdict = request_handler.callv(arguments);
    const Variant::Type answered = verdict.get_type();
    if (answered != Variant::INT && answered != Variant::FLOAT) {
        NETW_TRACE(
            sys::SCENE,
            "a scene request handler answered type %d, which is no verdict",
            int(answered)
        );
        return false;
    }
    const bool admitted = int64_t(verdict) == int64_t(OK);
    if (!admitted) {
        NETW_TRACE(
            sys::SCENE,
            "a scene request handler refused with %d",
            int(int64_t(verdict))
        );
    }
    return admitted;
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
    live.clear();
    retiring.clear();
    named.clear();
    next_request_id = 1;
    pending_request_id = 0;
    pending_request = Ref<NetwPromise>();
    pending_from_capture = false;
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
            "peer %d is parked against scene %s until its roster row lands",
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

bool NetwSceneCore::admission_is_parked(const RID &p_scene, int64_t p_peer)
    const {
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

bool NetwSceneCore::spawn_warns_shared_world() const {
    return !replacing && would_share_one_world();
}

void NetwSceneCore::spawn_note(const StringName &p_stem) const {
    if (!spawn_warns_shared_world()) {
        return;
    }
    const String subject = String(p_stem).is_empty()
        ? String("another scene")
        : vformat("scene '%s'", String(p_stem));
    NETW_WARN(
        sys::SCENE,
        "Activating %s while a shared-world scene is active. Both levels share "
        "one physics space. Declare isolation with "
        "Netw.configure_multiplayer_scene(self).isolated() if that is wrong.",
        subject
    );
}

bool NetwSceneCore::is_replacing() const {
    return replacing;
}

void NetwSceneCore::set_replacing(bool p_value) {
    replacing = p_value;
}

void NetwSceneCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_request_reach", "value"),
        &NetwSceneCore::set_request_reach
    );
    ClassDB::bind_method(
        D_METHOD("get_request_reach"),
        &NetwSceneCore::get_request_reach
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "request_reach"),
        "set_request_reach",
        "get_request_reach"
    );
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
        D_METHOD("scene_enter", "scene", "stem", "owns_its_world"),
        &NetwSceneCore::scene_enter
    );
    ClassDB::bind_method(
        D_METHOD("scene_exit", "scene"),
        &NetwSceneCore::scene_exit
    );
    ClassDB::bind_method(
        D_METHOD("scene_retire", "scene", "drain_pumps"),
        &NetwSceneCore::scene_retire
    );
    ClassDB::bind_method(
        D_METHOD("pump_retired"),
        &NetwSceneCore::pump_retired
    );
    ClassDB::bind_method(
        D_METHOD("retiring_scenes"),
        &NetwSceneCore::retiring_scenes
    );
    ClassDB::bind_method(D_METHOD("is_live", "scene"), &NetwSceneCore::is_live);
    ClassDB::bind_method(
        D_METHOD("scene_named", "stem"),
        &NetwSceneCore::scene_named
    );
    ClassDB::bind_method(
        D_METHOD("scenes_named", "stem"),
        &NetwSceneCore::scenes_named
    );
    ClassDB::bind_method(
        D_METHOD("live_scenes"),
        &NetwSceneCore::live_scenes
    );
    ClassDB::bind_method(
        D_METHOD("stem_of", "scene"),
        &NetwSceneCore::stem_of
    );
    ClassDB::bind_method(
        D_METHOD("live_count"),
        &NetwSceneCore::live_count
    );
    ClassDB::bind_method(
        D_METHOD("would_share_one_world"),
        &NetwSceneCore::would_share_one_world
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
        D_METHOD("transition_open"),
        &NetwSceneCore::transition_open
    );
    ClassDB::bind_method(
        D_METHOD("transition_close"),
        &NetwSceneCore::transition_close
    );
    ClassDB::bind_method(
        D_METHOD("request_open", "from_capture"),
        &NetwSceneCore::request_open
    );
    ClassDB::bind_method(
        D_METHOD("get_pending_request"),
        &NetwSceneCore::get_pending_request
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "pending_request",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPromise"
        ),
        "",
        "get_pending_request"
    );
    ClassDB::bind_method(
        D_METHOD("request_settle", "request_id", "code"),
        &NetwSceneCore::request_settle
    );
    ClassDB::bind_method(
        D_METHOD("request_abandon", "code"),
        &NetwSceneCore::request_abandon
    );
    ClassDB::bind_method(
        D_METHOD("receive_result_frame", "payload", "sender"),
        &NetwSceneCore::receive_result_frame
    );
    ADD_SIGNAL(MethodInfo(
        SIG_NATIVE_CHANGE_SETTLED,
        PropertyInfo(Variant::INT, "result")
    ));
    ClassDB::bind_static_method(
        "NetwSceneCore",
        D_METHOD("verify_requested_path", "path"),
        &NetwSceneCore::verify_requested_path
    );
    ClassDB::bind_static_method(
        "NetwSceneCore",
        D_METHOD("admits_request", "named", "marked", "gated", "path"),
        &NetwSceneCore::admits_request
    );
    ClassDB::bind_method(
        D_METHOD("set_request_handler", "handler"),
        &NetwSceneCore::set_request_handler
    );
    ClassDB::bind_method(
        D_METHOD("get_request_handler"),
        &NetwSceneCore::get_request_handler
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "request_handler"),
        "set_request_handler",
        "get_request_handler"
    );
    ClassDB::bind_method(
        D_METHOD(
            "decide_request",
            "participant",
            "destination",
            "args",
            "named",
            "marked",
            "gated",
            "path"
        ),
        &NetwSceneCore::decide_request
    );
    ClassDB::bind_static_method(
        "NetwSceneCore",
        D_METHOD(
            "native_entry_verdict",
            "has_path",
            "is_server",
            "session_reach",
            "a_scene_is_active",
            "listen_host",
            "has_local_participant",
            "here_owns_its_world"
        ),
        &NetwSceneCore::native_entry_verdict
    );
    ClassDB::bind_static_method(
        "NetwSceneCore",
        D_METHOD("native_change_strands", "online", "marked"),
        &NetwSceneCore::native_change_strands
    );
    BIND_CONSTANT(MAX_REQUESTED_PATH_LENGTH);
    BIND_ENUM_CONSTANT(CAPTURE_REFUSED);
    BIND_ENUM_CONSTANT(CAPTURE_REQUEST);
    BIND_ENUM_CONSTANT(CAPTURE_CHANGE_SESSION);
    BIND_ENUM_CONSTANT(CAPTURE_MOVE_ME);
    BIND_ENUM_CONSTANT(CAPTURE_ACTIVATE);
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

    ClassDB::bind_method(
        D_METHOD("set_current_scene", "scene"),
        &NetwSceneCore::set_current_scene
    );
    ClassDB::bind_method(
        D_METHOD("get_current_scene"),
        &NetwSceneCore::get_current_scene
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::RID, "current_scene"),
        "set_current_scene",
        "get_current_scene"
    );
    ClassDB::bind_method(
        D_METHOD("resolve_current", "presents", "seat"),
        &NetwSceneCore::resolve_current
    );

    ClassDB::bind_method(D_METHOD("clear"), &NetwSceneCore::clear);

    ClassDB::bind_method(
        D_METHOD("admission_park", "scene", "peer"),
        &NetwSceneCore::admission_park
    );
    ClassDB::bind_method(
        D_METHOD("admission_unpark", "scene", "peer"),
        &NetwSceneCore::admission_unpark
    );
    ClassDB::bind_method(
        D_METHOD("admission_is_parked", "scene", "peer"),
        &NetwSceneCore::admission_is_parked
    );

    ClassDB::bind_method(
        D_METHOD("spawn_warns_shared_world"),
        &NetwSceneCore::spawn_warns_shared_world
    );
    ClassDB::bind_method(
        D_METHOD("spawn_note", "stem"),
        &NetwSceneCore::spawn_note
    );
    ClassDB::bind_method(
        D_METHOD("is_replacing"),
        &NetwSceneCore::is_replacing
    );
    ClassDB::bind_method(
        D_METHOD("set_replacing", "value"),
        &NetwSceneCore::set_replacing
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "replacing"),
        "set_replacing",
        "is_replacing"
    );
}

}
