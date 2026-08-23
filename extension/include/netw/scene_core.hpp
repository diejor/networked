#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "netw/api/promise.hpp"
#include "godot/utility.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwSceneCore : public godot::RefCounted {
    GDCLASS(NetwSceneCore, godot::RefCounted)

public:
    static constexpr int MAX_REQUESTED_PATH_LENGTH = 512;

    enum Capture {
        CAPTURE_REFUSED,
        CAPTURE_REQUEST,
        CAPTURE_CHANGE_SESSION,
        CAPTURE_MOVE_ME,
        CAPTURE_ACTIVATE,
    };

    enum Reach {
        REACH_PARTICIPANT = 0,
        REACH_SESSION = 1,
        REACH_MAX = 2,
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
        EVENT_PARTICIPANT = 0,
        EVENT_PLAYER = 1,
        EVENT_ENTITY = 2,
    };

    enum Destination {
        DESTINATION_NONE,
        DESTINATION_NAME,
        DESTINATION_NODE,
        DESTINATION_PACKED,
        DESTINATION_PACKED_UNPATHED,
    };

    struct NativeEntry {
        bool has_path = false;
        bool is_server = false;
        bool session_reach = false;
        bool a_scene_is_active = false;
        bool listen_host = false;
        bool has_local_participant = false;
        bool here_owns_its_world = false;
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

    struct DeclaredRow {
        godot::StringName stem;
        godot::String path;
        godot::Variant spawn_data;
        bool initial = false;
    };

    struct Declaration {
        bool published = false;
        int32_t isolation = 0;
        godot::Callable level_spawn_function;
        godot::LocalVector<DeclaredRow> rows;
    };

    struct Transition {
        godot::Variant target;
        godot::Array sources;
        int64_t source_index = 0;
        godot::Array roster;
        int64_t roster_index = 0;
        godot::HashSet<int64_t> moved;
        godot::Ref<NetwPromise> promise;
    };

    Declaration declared;
    Declaration drafted;
    bool drafting = false;
    Transition transition;

    const DeclaredRow *declared_row(const godot::StringName &stem) const;

    godot::LocalVector<Row> rows;
    godot::LocalVector<LiveRow> live;
    godot::LocalVector<RetiringRow> retiring;
    godot::HashMap<godot::StringName, godot::RID> named;

    bool replacing = false;
    bool transitioning = false;
    int32_t request_reach = REACH_PARTICIPANT;

    int32_t next_request_id = 1;
    int32_t pending_request_id = 0;
    bool pending_from_capture = false;
    godot::Ref<NetwPromise> pending_request;
    godot::Callable request_handler;
    godot::Callable container_entered;
    godot::Callable container_exited;
    godot::RID current_scene;
    godot::ObjectID scene_anchor;

    Row *row_for(int event, const godot::RID &scene);
    void promote_survivors();
    void settle_pending(int code);

protected:
    static void _bind_methods();

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

    bool set_request_reach(int value);
    int get_request_reach() const;
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

    void set_scene_anchor(godot::Node *anchor);
    godot::Node *get_scene_anchor();
    godot::Node *spawn_anchor(godot::Node *fallback);

    void set_current_scene(const godot::RID &scene);
    godot::RID get_current_scene() const;
    godot::RID resolve_current(bool presents, const godot::RID &seat) const;

    bool is_live(const godot::RID &scene) const;
    godot::RID scene_named(const godot::StringName &stem) const;
    godot::Array scenes_named(const godot::StringName &stem) const;
    godot::Array live_scenes() const;
    godot::StringName stem_of(const godot::RID &scene) const;
    int live_count() const;
    bool would_share_one_world() const;

    void declaration_open(
        int isolation,
        const godot::Callable &level_spawn_function
    );
    void declaration_row(
        const godot::StringName &stem,
        const godot::String &path,
        const godot::Variant &spawn_data,
        bool initial
    );
    void declaration_publish();
    void declaration_drop();

    bool declaration_is_published() const;
    int declared_isolation() const;
    godot::Callable declared_level_spawn_function() const;
    bool declares_scene(const godot::StringName &stem) const;
    godot::String declared_scene_path(const godot::StringName &stem) const;
    godot::Variant declared_spawn_data(
        const godot::StringName &stem,
        const godot::Variant &fallback
    ) const;
    godot::Array declared_initial_stems() const;
    int declared_scene_count() const;

    bool transition_open();
    void transition_arm(
        const godot::Variant &target,
        const godot::Array &sources,
        const godot::Ref<NetwPromise> &promise
    );
    godot::Variant transition_target() const;
    godot::Array transition_sources() const;
    godot::Variant transition_next_mover(const godot::Callable &roster_of);
    bool transition_accept(int code, int64_t peer);
    bool transition_moved(int64_t peer) const;
    void transition_fail(int code);
    void transition_land();
    void transition_close();

    int open_request();
    bool is_current(int request_id) const;
    void close_request();

    godot::Ref<NetwPromise> request_open(bool from_capture);
    godot::Ref<NetwPromise> get_pending_request() const;
    bool request_settle(int request_id, int code);
    void request_abandon(int code);
    bool receive_result_frame(
        const godot::PackedByteArray &payload,
        int sender
    );

    static godot::String verify_requested_path(const godot::String &path);
    static godot::String resolve_requested_path(
        const godot::String &reference
    );
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
    static bool admits_request(
        bool named,
        bool marked,
        bool gated,
        const godot::String &path
    );

    void set_request_handler(const godot::Callable &handler);
    godot::Callable get_request_handler() const;

    void set_container_lifecycle(
        const godot::Callable &entered,
        const godot::Callable &exited
    );
    godot::Callable get_container_entered() const;
    godot::Callable get_container_exited() const;

    static bool isolation_owns_world(int isolation);
    bool decide_request(
        const godot::Variant &participant,
        const godot::Variant &destination,
        const godot::Array &args,
        bool named,
        bool marked,
        bool gated,
        const godot::String &path
    ) const;

    bool change_replaces_session(
        bool declared_session_wide,
        bool has_participant
    ) const;

    static bool native_change_strands(bool online, bool marked);

    static Move move_verdict(
        bool mover_live,
        bool target_live,
        bool same_scene
    );

    static Capture capture_verdict(const NativeEntry &entry);
    static int native_entry_verdict(
        bool has_path,
        bool is_server,
        bool session_reach,
        bool a_scene_is_active,
        bool listen_host,
        bool has_local_participant,
        bool here_owns_its_world
    );

    bool admission_park(const godot::RID &scene, int64_t peer);
    bool admission_unpark(const godot::RID &scene, int64_t peer);
    bool admission_is_parked(const godot::RID &scene, int64_t peer) const;

    bool spawn_warns_shared_world() const;
    void spawn_note(const godot::StringName &stem) const;
    bool is_replacing() const;
    void set_replacing(bool value);

    int get_pending_request_id() const;
    int get_next_request_id() const;
    void set_next_request_id(int value);

    void clear();
};

}

VARIANT_ENUM_CAST(netw::NetwSceneCore::Capture);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Event);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Isolation);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Move);
VARIANT_ENUM_CAST(netw::NetwSceneCore::Destination);
