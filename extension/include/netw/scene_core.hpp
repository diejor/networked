#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "netw/promise.hpp"
#include "godot/utility.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwSceneCore : public RefCounted {
    GDCLASS(NetwSceneCore, RefCounted)

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
        RID scene;
        LocalVector<Callable> callbacks;
    };

    struct LiveRow {
        RID scene;
        StringName stem;
        bool owns_its_world = false;
        LocalVector<int64_t> parked;
    };

    struct RetiringRow {
        RID scene;
        int32_t pumps_left = 0;
    };

    LocalVector<Row> rows;
    LocalVector<LiveRow> live;
    LocalVector<RetiringRow> retiring;
    HashMap<StringName, RID> named;

    bool replacing = false;
    bool transitioning = false;
    int32_t request_reach = REACH_PARTICIPANT;

    int32_t next_request_id = 1;
    int32_t pending_request_id = 0;
    bool pending_from_capture = false;
    Ref<NetwPromise> pending_request;
    Callable request_handler;
    RID current_scene;

    Row *row_for(int event, const RID &scene);
    void promote_survivors();
    void settle_pending(int code);

protected:
    static void _bind_methods();

public:
    void observe(
        const RID &scene,
        int event,
        const Callable &callback
    );

    void unobserve(
        const RID &scene,
        int event,
        const Callable &callback
    );

    int dispatch(
        const RID &scene,
        int event,
        bool present,
        const Variant &subject
    );

    int observer_count(const RID &scene, int event) const;

    bool set_request_reach(int value);
    int get_request_reach() const;
    void forget_scene(const RID &scene);

    void scene_enter(
        const RID &scene,
        const StringName &stem,
        bool owns_its_world
    );
    void scene_exit(const RID &scene);
    void scene_retire(const RID &scene, int drain_pumps);
    Array pump_retired();
    Array retiring_scenes() const;

    void set_current_scene(const RID &scene);
    RID get_current_scene() const;
    RID resolve_current(bool presents, const RID &seat) const;

    bool is_live(const RID &scene) const;
    RID scene_named(const StringName &stem) const;
    Array scenes_named(const StringName &stem) const;
    Array live_scenes() const;
    StringName stem_of(const RID &scene) const;
    int live_count() const;
    bool would_share_one_world() const;

    bool transition_open();
    void transition_close();

    int open_request();
    bool is_current(int request_id) const;
    void close_request();

    Ref<NetwPromise> request_open(bool from_capture);
    Ref<NetwPromise> get_pending_request() const;
    bool request_settle(int request_id, int code);
    void request_abandon(int code);
    bool receive_result_frame(const PackedByteArray &payload, int sender);

    static String verify_requested_path(const String &path);
    static bool admits_request(
        bool named,
        bool marked,
        bool gated,
        const String &path
    );

    void set_request_handler(const Callable &handler);
    Callable get_request_handler() const;
    bool decide_request(
        const Variant &participant,
        const Variant &destination,
        const Array &args,
        bool named,
        bool marked,
        bool gated,
        const String &path
    ) const;

    static bool native_change_strands(bool online, bool marked);

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

    bool admission_park(const RID &scene, int64_t peer);
    bool admission_unpark(const RID &scene, int64_t peer);
    bool admission_is_parked(const RID &scene, int64_t peer) const;

    bool spawn_warns_shared_world() const;
    void spawn_note(const StringName &stem) const;
    bool is_replacing() const;
    void set_replacing(bool value);

    int get_pending_request_id() const;
    int get_next_request_id() const;
    void set_next_request_id(int value);

    void clear();
};

}

VARIANT_ENUM_CAST(netw::NetwSceneCore::Capture);
