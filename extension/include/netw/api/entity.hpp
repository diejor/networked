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
#include "netw/api/display_handle.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/api/timeline.hpp"
#include "netw/comp_table.hpp"

namespace netw {

class NetwInterestHandle;
class NetwMultiplayer;
class NetwPredictionHandle;
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

private:
    NetwEntityRecord *record = nullptr;
    godot::ObjectID owner_id;
    godot::ObjectID session_id;
    godot::ObjectID timeline_id;
    godot::TypedArray<godot::MultiplayerSynchronizer> synchronizers_cache;
    bool synchronizers_dirty = true;
    bool ready_once_fired = false;
    bool owner_exiting_tree = false;
    int64_t action_spawn_tick = -1;
    int64_t action_requester = 0;

    entity::Control *control() const;
    NetwMultiplayer *session_core() const;
    int64_t local_peer() const;
    bool ensure_server_action(const godot::StringName &p_action);
    ReplicationCore *get_replication_plane() const;
    void set_controller_internal(int64_t p_value);
    int64_t resolve_initial_controller() const;
    void apply_control();
    void apply_control_change(int64_t p_peer);
    void transition(int64_t p_stage);
    void linger_then_free(const godot::Ref<NetwDespawnOpts> &p_opts);
    godot::Ref<NetwPropertySetBinding> derived_binding(int64_t p_record) const;

protected:
    static void _bind_methods();

public:
    NetwEntity();
    ~NetwEntity();

    static godot::StringName meta_key();
    static godot::StringName template_meta();

    static NetwMultiplayer *session_core_for(godot::Node *p_node);
    static godot::Ref<NetwMultiplayer> session_plane_for(godot::Node *p_node);

    static godot::Ref<NetwEntity> of(godot::Node *p_node);
    static godot::Ref<NetwEntity> ensure(godot::Node *p_root);
    static godot::Ref<NetwEntity> resolve(godot::Node *p_node);
    static godot::Ref<NetwEntity> from_rid(
        const godot::RID &p_entity,
        const godot::Ref<NetwMultiplayer> &p_api
    );
    static godot::Ref<NetwEntity> by_route(
        int64_t p_route,
        const godot::Ref<NetwMultiplayer> &p_api
    );
    static godot::StringName parse_entity(const godot::String &p_node_name);
    static int64_t parse_peer(const godot::String &p_node_name);
    static godot::String name_for(
        const godot::Ref<NetwParticipant> &p_participant
    );
    static godot::Node *find(
        godot::Node *p_root,
        const godot::Ref<NetwParticipant> &p_participant
    );
    static godot::Node *bind(
        godot::Node *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );
    static godot::Node *instantiate_from(
        godot::Node *p_template,
        const godot::Callable &p_configure
    );

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
    int64_t get_route() const;
    void set_route(int64_t p_route);
    godot::RID get_rid_handle() const;
    void set_rid_handle(const godot::RID &p_handle);

    godot::Ref<godot::MultiplayerAPI> get_multiplayer() const;

    void stamp_multiplayer(const godot::Ref<NetwMultiplayer> &p_api);

    int64_t get_initial_controller() const;
    void set_initial_controller(int64_t p_value);
    int64_t get_transfer() const;
    void set_transfer(int64_t p_value);
    int64_t get_on_controller_disconnect() const;
    void set_on_controller_disconnect(int64_t p_value);
    bool get_declares_scene() const;
    void set_declares_scene(bool p_value);
    godot::StringName get_scene_label() const;
    void set_scene_label(const godot::StringName &p_value);
    int64_t get_scene_isolation() const;
    void set_scene_isolation(int64_t p_value);

    int64_t get_controller() const;
    void set_controller(int64_t p_value);
    int64_t get_control_kind() const;
    bool get_is_controlled_locally() const;
    godot::Ref<NetwParticipant> get_controller_participant() const;
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

    void request_control();
    void grant_control(int64_t p_peer_id);
    void revoke_control();

    bool get_is_authority() const;
    godot::Ref<NetwParticipant> get_participant() const;
    int64_t get_ownership() const;
    bool get_is_player() const;

    bool get_is_template() const;
    int64_t get_stage() const;
    godot::Ref<NetwDespawnOpts> get_active_despawn_opts() const;

    void mark_template();

    void note_stage(int64_t p_from);

    void arm(const godot::Ref<NetwMultiplayer> &p_api);

    godot::Node *spawn_under(
        godot::Node *p_parent,
        const godot::StringName &p_id
    );
    godot::Node *instantiate_player(
        const godot::Ref<NetwParticipant> &p_participant
    );
    godot::Node *spawn_player(
        const godot::Ref<NetwParticipant> &p_participant,
        const godot::Ref<NetwSceneHandle> &p_scene
    );
    void reparent_to(
        godot::Node *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
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
    godot::Ref<NetwPersistenceEngine> get_persistence() const;
    godot::Ref<NetwPropertySetBinding> get_state_binding() const;
    godot::Ref<NetwPropertySetBinding> get_input_binding() const;
    godot::Ref<NetwPropertySetBinding> get_broadcast_binding() const;
    godot::Ref<NetwInterestHandle> get_interest() const;
    godot::Ref<NetwSceneHandle> get_scene() const;
    godot::Ref<NetwPredictionHandle> get_prediction() const;
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
    void _handle_control_request(int64_t p_sender);
    void _handle_control_apply(int64_t p_peer);
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
