#pragma once

#include "godot/variant.hpp"

#if defined(NETW_TESTS)

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/script.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/promise.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/timing.hpp"

namespace netw {

class NetwMultiplayer;
class NetwEntity;
class NetwInterpolate;

int run_native_tests(
    const godot::String &filter,
    const godot::String &report_path,
    const godot::String &cells_path
);

class NetwNativeTests : public godot::RefCounted {
    GDCLASS(NetwNativeTests, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::Dictionary run(
        const godot::String &filter = godot::String(),
        const godot::String &report_path = "res://reports/native/results.xml",
        const godot::String &cells_path = godot::String()
    );
    bool frame_advance();
    void instrumentation_probe(int64_t value) const;
    static void schema_model_clear();
    static void file_system_database_forget_roots();
    static void tracker_book_clear();
    static godot::TypedArray<godot::StringName> property_set_keys_of_script(
        const godot::Ref<godot::Script> &script,
        int64_t record
    );
    static void scene_enter(
        NetwMultiplayer *session,
        const godot::RID &scene,
        const godot::StringName &stem,
        bool owns_its_world
    );
    static godot::Ref<NetwPromise> scene_pending_request(
        NetwMultiplayer *session
    );
    static void scene_sync_local_participant(NetwMultiplayer *session);
    static void scene_ensure_host_view(NetwMultiplayer *session);
    static void scene_release_host_view(NetwMultiplayer *session);
    static void scene_refresh_current(NetwMultiplayer *session);
    static bool rpc_sender_admits(
        NetwMultiplayer *session,
        godot::Node *node,
        const godot::StringName &method,
        int64_t sender
    );
    static godot::StringName scene_container_meta();
    static void scene_retire(
        NetwMultiplayer *session,
        const godot::RID &scene,
        int drain_pumps
    );
    static godot::Ref<NetwGroupPromise> scene_move_participants(
        NetwMultiplayer *session,
        const godot::RID &scene,
        const godot::PackedInt32Array &peers
    );
    static display::Runtime *display_runtime_of(
        NetwMultiplayer *session,
        const godot::RID &entity
    );
    static display::Runtime *display_runtime_at(
        NetwMultiplayer *session,
        int64_t route
    );
    static godot::LocalVector<display::Runtime *> display_runtimes(
        NetwMultiplayer *session
    );
    static int64_t display_route_of(
        NetwMultiplayer *session,
        const godot::RID &entity
    );
    static void display_clock_tick(
        NetwMultiplayer *session,
        double delta,
        int64_t tick
    );
    static godot::Error display_pump(NetwMultiplayer *session, double delta);
    static void display_pump_runtime(
        NetwMultiplayer *session,
        display::Runtime *runtime,
        const display::Timing &timing
    );
    static void display_record(
        NetwMultiplayer *session,
        godot::Node *node,
        const godot::StringName &target_property,
        const godot::Variant &value,
        int64_t tick,
        const godot::Ref<NetwInterpolate> &spec,
        bool authoring_tick
    );
    static bool display_wants_runtime(
        NetwMultiplayer *session,
        godot::Node *owner
    );
    static double display_chase_smooth_time(
        NetwMultiplayer *session,
        display::Runtime *runtime,
        const display::Timing &timing
    );
    static void display_absorb_recovery(
        NetwMultiplayer *session,
        display::Runtime *runtime,
        const godot::Dictionary &deltas,
        bool teleported
    );
    static void display_mark_role_dirty(
        NetwMultiplayer *session,
        const godot::RID &entity
    );
    static void observe_node_entity_ref(
        NetwMultiplayer *session,
        const godot::Variant &node_ref
    );
    static bool interest_is_visible(NetwEntity *entity, int64_t peer_id);
    static godot::TypedArray<godot::StringName> interest_layer_ids(
        NetwEntity *entity
    );
    static int64_t interest_leave_policy_for(
        NetwEntity *entity,
        const godot::StringName &layer_id,
        int64_t fallback
    );
    static godot::Callable interest_custom_leave_for(
        NetwEntity *entity,
        const godot::StringName &layer_id
    );
    static int64_t interest_perception_policy_for(
        NetwEntity *entity,
        const godot::StringName &layer_id,
        int64_t fallback
    );
    static godot::Callable interest_custom_perception_for(
        NetwEntity *entity,
        const godot::StringName &layer_id
    );
    static void interest_set_report_observers(NetwEntity *entity, bool enabled);
    static bool interest_reports_observers(NetwEntity *entity);
    static void interest_on_observed(
        NetwEntity *entity,
        const godot::Callable &callback
    );
    static void interest_on_enter(
        NetwEntity *entity,
        const godot::StringName &layer_id,
        const godot::Callable &callback
    );
    static void interest_on_leave(
        NetwEntity *entity,
        const godot::StringName &layer_id,
        const godot::Callable &callback
    );
    static void interest_set_leave_policy(
        NetwEntity *entity,
        const godot::StringName &layer_id,
        int64_t policy,
        const godot::Callable &custom
    );
    static void interest_set_perception_policy(
        NetwEntity *entity,
        const godot::StringName &layer_id,
        int64_t policy,
        const godot::Callable &custom
    );
    static void interest_on_unobserved(
        NetwEntity *entity,
        const godot::Callable &callback
    );
    static void interest_queue_observer_awareness(
        NetwMultiplayer *session,
        const godot::StringName &layer_id,
        NetwEntity *entity,
        int64_t observer_peer,
        int kind
    );
    static godot::Array interest_awareness_drain(NetwMultiplayer *session);

    godot::Dictionary wire_spec() const;
};

} // namespace netw

#endif
