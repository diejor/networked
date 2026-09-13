#include "netw/scene_core.hpp"

#include "godot/resource.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/session/frames.hpp"

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
        const Variant answer = callback.callv(args);
        if (answer.get_type() == Variant::BOOL && bool(answer)) {
            NETW_TRACE(
                sys::SCENE,
                "observer of event %d retired itself",
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

void NetwSceneCore::transition_arm(
    const Variant &p_target,
    const Array &p_sources,
    const Ref<NetwPromise> &p_promise
) {
    transition = Transition();
    transition.target = p_target;
    transition.sources = p_sources;
    transition.promise = p_promise;
}

Variant NetwSceneCore::transition_target() const {
    return transition.target;
}

Array NetwSceneCore::transition_sources() const {
    return transition.sources;
}

Variant NetwSceneCore::transition_next_mover(const Callable &p_roster_of) {
    while (transition.roster_index >= int64_t(transition.roster.size())) {
        if (transition.source_index >= int64_t(transition.sources.size())) {
            return Variant();
        }
        const Variant source = transition.sources[int(transition.source_index)];
        transition.source_index += 1;
        const Variant answered
            = p_roster_of.is_valid() ? p_roster_of.call(source) : Variant();
        transition.roster = Array();
        if (answered.get_type() == Variant::ARRAY) {
            transition.roster = answered;
        }
        transition.roster_index = 0;
    }
    const Variant mover = transition.roster[int(transition.roster_index)];
    transition.roster_index += 1;
    return mover;
}

bool NetwSceneCore::transition_accept(int p_code, int64_t p_peer) {
    if (p_code != OK) {
        NETW_TRACE(
            sys::SCENE,
            "a transition mover answered %d, so the walk aborts",
            p_code
        );
        transition_fail(int(ERR_UNAVAILABLE));
        return false;
    }
    transition.moved.insert(p_peer);
    return true;
}

bool NetwSceneCore::transition_moved(int64_t p_peer) const {
    return transition.moved.has(p_peer);
}

void NetwSceneCore::transition_fail(int p_code) {
    const Ref<NetwPromise> promise = transition.promise;
    transition_close();
    if (promise.is_valid()) {
        promise->reject(static_cast<Error>(p_code), String());
    }
}

void NetwSceneCore::transition_land() {
    const Ref<NetwPromise> promise = transition.promise;
    transition_close();
    if (promise.is_valid()) {
        promise->resolve(int(OK));
    }
}

void NetwSceneCore::transition_close() {
    transitioning = false;
    transition = Transition();
}

int NetwSceneCore::open_request() {
    if (next_request_id == INT32_MAX) {
        NETW_ERROR(
            sys::SCENE,
            "this session has opened every scene request id it has, and "
            "reusing one would let an old answer settle a new request"
        );
        pending_request_id = 0;
        return 0;
    }
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
            pending_request->reject(static_cast<Error>(code), String());
        }
    }
    pending_request = Ref<NetwPromise>();
    pending_request_id = 0;
}

Ref<NetwPromise> NetwSceneCore::request_open() {
    if (pending_request.is_valid() && !pending_request->get_is_settled()) {
        settle_pending(int(ERR_SKIP));
    }
    open_request();
    pending_request.instantiate();
    NETW_TRACE(sys::SCENE, "opened scene request %d", pending_request_id);
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
    netw::session::SceneResult frame;
    if (!netw::session::frame_read(payload, frame)) {
        return false;
    }
    return request_settle(int(frame.request_id), int(frame.code));
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

String NetwSceneCore::resolve_requested_path(const String &reference) {
    if (reference.is_empty()) {
        return String();
    }
    return verify_requested_path(netw::gd::ensure_path(reference));
}

Ref<PackedScene> NetwSceneCore::packed_at(const String &reference) {
    if (reference.is_empty()) {
        return Ref<PackedScene>();
    }
    const Ref<PackedScene> packed
        = netw::gd::load_scene(netw::gd::ensure_path(reference));
    if (packed.is_null()) {
        NETW_TRACE(sys::SCENE, "no packed scene at: %s", reference);
    }
    return packed;
}

Ref<Script> NetwSceneCore::packed_root_script(const Ref<PackedScene> &packed) {
    if (packed.is_null()) {
        return Ref<Script>();
    }
    const Ref<SceneState> state = packed->get_state();
    if (state.is_null() || state->get_node_count() == 0) {
        return Ref<Script>();
    }
    const int total = state->get_node_property_count(0);
    for (int index = 0; index < total; index++) {
        if (state->get_node_property_name(0, index) != StringName("script")) {
            continue;
        }
        const Variant value = state->get_node_property_value(0, index);
        Object *object = value;
        return Ref<Script>(Object::cast_to<Script>(object));
    }
    return Ref<Script>();
}

Ref<Script> NetwSceneCore::scene_root_script_at(const String &reference) {
    return packed_root_script(packed_at(reference));
}

NetwSceneCore::Destination NetwSceneCore::destination_kind(
    const Variant &destination
) {
    switch (destination.get_type()) {
        case Variant::STRING:
        case Variant::STRING_NAME:
            return DESTINATION_NAME;
        case Variant::OBJECT:
            break;
        default:
            return DESTINATION_NONE;
    }
    Object *object = destination;
    if (object == nullptr) {
        return DESTINATION_NONE;
    }
    PackedScene *packed = Object::cast_to<PackedScene>(object);
    if (packed != nullptr) {
        return packed->get_path().is_empty() ? DESTINATION_PACKED_UNPATHED
                                             : DESTINATION_PACKED;
    }
    return Object::cast_to<Node>(object) != nullptr ? DESTINATION_NODE
                                                    : DESTINATION_NONE;
}

bool NetwSceneCore::scope_names_an_operation(int scope) {
    return scope >= SCOPE_SESSION && scope < SCOPE_MAX;
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

void NetwSceneCore::set_request_handler(const Callable &handler) {
    request_handler = handler;
}

Callable NetwSceneCore::get_request_handler() const {
    return request_handler;
}

bool NetwSceneCore::isolation_owns_world(int isolation) {
    return isolation == ISOLATION_OWN_WORLD;
}

Error NetwSceneCore::decide_request(
    const Variant &participant,
    const Variant &destination,
    int scope
) const {
    if (!request_handler.is_valid()) {
        return ERR_UNAUTHORIZED;
    }
    Array arguments;
    arguments.push_back(participant);
    arguments.push_back(destination);
    arguments.push_back(scope);
    const Variant verdict = request_handler.callv(arguments);
    const Variant::Type answered = verdict.get_type();
    if (answered != Variant::INT && answered != Variant::FLOAT) {
        NETW_TRACE(
            sys::SCENE,
            "a scene request handler answered type %d, which is no verdict",
            int(answered)
        );
        return ERR_INVALID_DATA;
    }
    const Error refusal = Error(int64_t(verdict));
    if (refusal != OK) {
        NETW_TRACE(
            sys::SCENE,
            "a scene request handler refused with %d",
            int(refusal)
        );
    }
    return refusal;
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
    pending_request_id = 0;
    pending_request = Ref<NetwPromise>();
    transition = Transition();
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

} // namespace netw
