#pragma once

#include <cstdint>

#include "godot/multiplayer.hpp"
#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/api/timeline.hpp"
#include "netw/comp_table.hpp"
#include "netw/entity/stage.hpp"
#include "netw/session/frames.hpp"

namespace netw {

class NetwDisplayHandle;
class NetwInterestHandle;
class NetwMultiplayer;
class NetwPersistenceHandle;
class NetwPredictionHandle;
class NetwSimulationHandle;
class ReplicationCore;

class NetwEntity : public godot::RefCounted {
    GDCLASS(NetwEntity, godot::RefCounted)

public:
    enum Ownership {
        OWNERSHIP_PEER = 0,
        OWNERSHIP_SERVER = 1,
    };

    enum ControlKind {
        CONTROL_PEER_CONTROLLED = 0,
        CONTROL_SERVER_CONTROLLED = 1,
    };

    enum InitialController {
        INITIAL_SERVER = int(entity::Control::InitialController::SERVER),
        INITIAL_REPRESENTED_PEER
        = int(entity::Control::InitialController::REPRESENTED_PEER),
    };

    enum Transfer {
        TRANSFER_FIXED = int(entity::Control::Transfer::FIXED),
        TRANSFER_REQUESTABLE = int(entity::Control::Transfer::REQUESTABLE),
        TRANSFER_IMMEDIATE = int(entity::Control::Transfer::IMMEDIATE),
    };

    enum DisconnectRule {
        DISCONNECT_REVERT_TO_SERVER
        = int(entity::Control::DisconnectRule::REVERT_TO_SERVER),
        DISCONNECT_DESPAWN = int(entity::Control::DisconnectRule::DESPAWN),
    };

    enum ParentDespawnRule {
        PARENT_DESPAWN_CASCADE
        = int(entity::Control::ParentDespawnRule::CASCADE),
        PARENT_DESPAWN_DETACH = int(entity::Control::ParentDespawnRule::DETACH),
    };

    enum Lifecycle {
        LIFECYCLE_SESSION = int(entity::Control::Lifecycle::SESSION),
        LIFECYCLE_CONTROLLER = int(entity::Control::Lifecycle::CONTROLLER),
    };

    enum Stage {
        STAGE_UNBOUND = int(entity::Stage::UNBOUND),
        STAGE_TEMPLATE = int(entity::Stage::TEMPLATE),
        STAGE_ARMED = int(entity::Stage::ARMED),
        STAGE_LIVE = int(entity::Stage::LIVE),
        STAGE_DESPAWNING = int(entity::Stage::DESPAWNING),
        STAGE_LINGERING = int(entity::Stage::LINGERING),
        STAGE_FREED = int(entity::Stage::FREED),
    };

    using Hold = entity::Control::Hold;

private:
    struct ControlClaim {
        uint64_t op = 0;
        godot::Ref<NetwPromise> promise;
    };

    struct StructureOp {
        uint64_t base = 0;
        godot::Ref<NetwPromise> promise;
    };

    NetwEntityRecord *record = nullptr;
    godot::LocalVector<ControlClaim> control_claims;
    godot::LocalVector<StructureOp> structure_ops;
    godot::ObjectID owner_id;
    godot::ObjectID session_id;
    godot::ObjectID timeline_id;
    godot::TypedArray<godot::MultiplayerSynchronizer> synchronizers_cache;
    bool synchronizers_dirty = true;
    bool ready_once_fired = false;
    bool owner_exiting_tree = false;
    bool raw_move_warned = false;
    int64_t action_spawn_tick = -1;
    int64_t action_requester = 0;

    entity::Control *control() const;
    NetwMultiplayer *session_core() const;
    int64_t local_peer() const;
    bool ensure_server_action(const godot::StringName &p_action);
    ReplicationCore *get_replication_plane() const;
    void announce_control(int64_t p_was, int64_t p_peer);
    int64_t resolve_initial_controller() const;
    void project_control(godot::Object *p_announcer);
    void descendant_entered(godot::Node *p_node);
    void transfer_control(int64_t p_peer);
    void apply_control_change(int64_t p_peer);
    void decide_control(
        int64_t p_peer,
        int64_t p_hold,
        int64_t p_issuer,
        uint64_t p_op,
        const godot::PackedByteArray &p_final_state = godot::PackedByteArray()
    );
    entity::Control::Outcome decide_request(
        int64_t p_requester,
        uint64_t p_op,
        int64_t p_hold,
        int64_t p_source_route = 0
    );
    bool controls_source(int64_t p_requester, int64_t p_source_route) const;
    godot::Ref<NetwPromise> issue_claim(Hold p_hold, int64_t p_source_route);
    entity::Control::Outcome decide_release(
        int64_t p_requester,
        uint64_t p_op,
        int64_t p_successor,
        const godot::PackedByteArray &p_final_state
    );
    bool holds_copy(int64_t p_peer);
    godot::TypedArray<NetwPropertySetBinding> image_bindings();
    godot::PackedByteArray final_image(int64_t p_tick);
    void install_final_image(const godot::PackedByteArray &p_final_state);
    void refuse_control(
        int64_t p_requester,
        uint64_t p_op,
        entity::Control::Outcome p_outcome
    );
    bool deciding_locally() const;
    bool immediate_unavailable();
    godot::Error local_refusal(int64_t p_local, int64_t p_hold);
    godot::String refusal_detail(int64_t p_hold) const;
    void send_control_op(
        const entity::Control::Pending &p_op,
        int64_t p_successor = 0,
        const godot::PackedByteArray &p_final_state = godot::PackedByteArray(),
        int64_t p_source_route = 0
    );
    void settle_control(uint64_t p_op, entity::Control::Outcome p_outcome);
    void settle_claim(
        const godot::Ref<NetwPromise> &p_promise,
        const entity::Control::Pending &p_taken,
        entity::Control::Outcome p_outcome
    );
    godot::TypedArray<NetwPropertySetBinding> authored_bindings();
    void follow_claim(bool p_was_ahead);
    godot::Ref<NetwPromise> take_claim(uint64_t p_op);
    void transition(int64_t p_stage);
    void linger_then_free(const godot::Ref<NetwDespawnOpts> &p_opts);
    godot::Ref<NetwPropertySetBinding> derived_binding(int64_t p_record) const;

#if defined(NETW_TESTS)
public:
    godot::PackedByteArray final_image_under_test(int64_t p_tick) {
        return final_image(p_tick);
    }
    int64_t pending_ops_under_test() const {
        const entity::Control *held = control();
        return held == nullptr ? 0 : int64_t(held->pending.size());
    }

private:
#endif

protected:
    static void _bind_methods();

public:
    NetwEntity();
    ~NetwEntity();

    static NetwMultiplayer *session_core_for(godot::Node *p_node);

    static godot::Ref<NetwEntity> of(godot::Node *p_node);
    static godot::Ref<NetwEntity> ensure(godot::Node *p_root);
    static godot::Ref<NetwEntity> resolve(godot::Node *p_node);

    void attach_to(godot::Node *p_root);

    NetwEntityRecord *get_record() const {
        return record;
    }

    godot::Node *get_owner() const;
    void set_owner(godot::Node *p_owner);

    godot::StringName get_entity_id() const;
    void set_entity_id(const godot::StringName &p_entity_id);
    int64_t get_peer_id() const;
    void set_peer_id(int64_t p_peer_id);
    int64_t get_player_id() const;
    void set_player_id(int64_t p_player_id);
    int64_t get_route() const;
    void set_route(int64_t p_route);
    godot::RID get_rid_handle() const;
    void set_rid_handle(const godot::RID &p_handle);

    godot::Ref<godot::MultiplayerAPI> get_multiplayer() const;

    void stamp_multiplayer(const godot::Ref<NetwMultiplayer> &p_api);

    InitialController get_initial_controller() const;
    void set_initial_controller(InitialController p_value);
    Transfer get_transfer() const;
    void set_transfer(Transfer p_value);
    DisconnectRule get_on_controller_disconnect() const;
    void set_on_controller_disconnect(DisconnectRule p_value);
    ParentDespawnRule get_on_parent_despawn() const;
    void set_on_parent_despawn(ParentDespawnRule p_value);
    Lifecycle get_lifecycle() const;
    void set_lifecycle(Lifecycle p_value);
    bool note_raw_move_warning() {
        const bool first = !raw_move_warned;
        raw_move_warned = true;
        return first;
    }
    void structure_op_issue(
        uint64_t p_base,
        const godot::Ref<NetwPromise> &p_promise
    );
    void structure_ops_accept(uint64_t p_revision);
    bool structure_ops_refuse(
        uint64_t p_base,
        godot::Error p_code,
        const godot::String &p_detail
    );
    bool has_structure_ops() const {
        return !structure_ops.is_empty();
    }
    uint32_t structure_ops_outstanding() const {
        return structure_ops.size();
    }
    bool get_declares_scene() const;
    void set_declares_scene(bool p_value);
    godot::StringName get_scene_label() const;
    void set_scene_label(const godot::StringName &p_value);
    int64_t get_scene_isolation() const;
    void set_scene_isolation(int64_t p_value);

    int64_t get_controller() const;
    void set_controller(int64_t p_value);
    void record_controller(int64_t p_value);
    ControlKind get_control_kind() const;
    bool get_is_controlled_locally() const;
    godot::Ref<NetwPlayer> get_controller_player() const;
    int64_t get_action_spawn_tick() const {
        return action_spawn_tick;
    }
    void set_action_spawn_tick(int64_t p_tick) {
        action_spawn_tick = p_tick;
    }
    int64_t get_action_requester() const {
        return action_requester;
    }
    void set_action_requester(int64_t p_peer) {
        action_requester = p_peer;
    }

    godot::Ref<NetwPromise> claim_authority(
        Hold p_hold = entity::Control::HOLD_EXCLUSIVE
    );
    godot::Ref<NetwPromise> claim_by_contact(
        const godot::Ref<NetwEntity> &p_source
    );
    godot::Ref<NetwPromise> release_authority(int64_t p_successor = 0);
    bool is_controller_here() const;
    bool is_claim_pending() const;
    bool is_claim_running_ahead() const;
    void follow_session(const godot::NodePath &p_path);
    void apply_control();
    Hold get_hold() const;
    bool get_is_control_pending() const;
    uint64_t get_control_revision() const;
    uint64_t get_control_tenure() const;
    void seed_control(uint64_t p_revision, uint64_t p_tenure, int64_t p_hold);
    bool expire_control(int64_t p_tick, int64_t p_deadline);
    void abandon_claims();

    bool get_is_session_authority() const;
    godot::Ref<NetwPlayer> get_player() const;
    Ownership get_ownership() const;
    bool get_is_player() const;

    bool get_is_template() const;
    void set_is_template(bool p_value);
    Stage get_stage() const;
    godot::Ref<NetwDespawnOpts> get_active_despawn_opts() const;

    void note_stage(int64_t p_from);

    void arm(const godot::Ref<NetwMultiplayer> &p_api);

    godot::Node *spawn_under(
        godot::Node *p_parent,
        const godot::StringName &p_id,
        const godot::Callable &p_configure = godot::Callable()
    );
    void despawn(const godot::Ref<NetwDespawnOpts> &p_opts);

    void register_component(godot::Node *p_component);
    godot::NodePath relative_path(
        godot::Node *p_source,
        godot::Node *p_target
    ) const;
    int64_t comp_of(godot::Node *p_node) const;
    godot::String comp_path_of(godot::Node *p_node) const;
    godot::Node *comp_node_of(int64_t p_comp) const;
    bool has_comp_path(const godot::NodePath &p_path) const;
    bool get_comps_poisoned() const;

    NetwCompTable &comp_table();
    const NetwCompTable &comp_table() const;
    godot::NodePath property_path(
        godot::Node *p_source,
        const godot::StringName &p_property,
        godot::Node *p_base
    ) const;
    godot::Ref<NetwPropertySetBinding> get_state_binding() const;
    godot::Ref<NetwPropertySetBinding> get_input_binding() const;
    godot::Ref<NetwPropertySetBinding> get_broadcast_binding() const;
    godot::Ref<NetwInterestHandle> get_interest() const;
    godot::Ref<NetwSceneHandle> get_scene() const;
    godot::Ref<NetwPredictionHandle> get_prediction() const;
    godot::Ref<NetwSimulationHandle> get_simulation() const;
    godot::Ref<NetwPersistenceHandle> get_persistence() const;
    godot::Ref<NetwDisplayHandle> get_interpolation() const;
    godot::Ref<NetwTimeline> get_timeline() const;
    void set_timeline(const godot::Ref<NetwTimeline> &p_timeline);

    godot::TypedArray<godot::MultiplayerSynchronizer> synchronizers();
    bool governs_property(
        const godot::NodePath &p_real_path,
        godot::Node *p_exclude
    ) const;
    void invalidate_synchronizers_cache();
    godot::Ref<NetwEntity> parent_entity() const;

    godot::Ref<NetwSceneHandle> own_scene();

    void hydrate_components();
    int64_t comp_structure_hash(const godot::PackedStringArray &p_paths) const;
    void _handle_control_request(
        int64_t p_sender,
        const session::ControlRequest &p_request
    );
    void _handle_control_apply(const session::ControlApply &p_applied);
    void _go_live_if_armed();
    void _remote_despawn(
        const godot::StringName &p_reason,
        double p_linger_seconds
    );
    void _remote_hide();
    void _handle_tree_entered();
    void _handle_tree_exiting();
    void _on_owner_ready();
    void _on_peer_disconnected(int64_t p_peer_id);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwEntity::Ownership);
VARIANT_ENUM_CAST(netw::NetwEntity::ControlKind);
VARIANT_ENUM_CAST(netw::NetwEntity::Hold);
VARIANT_ENUM_CAST(netw::NetwEntity::InitialController);
VARIANT_ENUM_CAST(netw::NetwEntity::Transfer);
VARIANT_ENUM_CAST(netw::NetwEntity::DisconnectRule);
VARIANT_ENUM_CAST(netw::NetwEntity::ParentDespawnRule);
VARIANT_ENUM_CAST(netw::NetwEntity::Lifecycle);
VARIANT_ENUM_CAST(netw::NetwEntity::Stage);
