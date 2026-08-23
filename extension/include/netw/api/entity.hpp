#pragma once

#include <cstdint>

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"

namespace netw {

class NetwMultiplayerCore;

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
    godot::Ref<NetwEntityRecord> record;
    godot::ObjectID owner_id;
    godot::ObjectID session_id;
    godot::ObjectID timeline_id;
    godot::TypedArray<godot::MultiplayerSynchronizer> synchronizers_cache;
    bool synchronizers_dirty = true;
    bool ready_once_fired = false;
    bool owner_exiting_tree = false;
    int64_t action_spawn_tick = -1;
    int64_t action_requester = 0;

    godot::Ref<NetwEntityControl> control() const;
    NetwMultiplayerCore *session_core() const;
    godot::Object *session() const;
    int64_t local_peer() const;
    bool ensure_server_action(const godot::StringName &p_action);
    godot::Object *replication_plane() const;
    void set_controller_internal(int64_t p_value);
    void apply_control();
    void apply_control_change(int64_t p_peer);
    void transition(int64_t p_stage);
    void linger_then_free(const godot::Ref<NetwDespawnOpts> &p_opts);
    godot::Variant derived_binding(int64_t p_record) const;

protected:
    static void _bind_methods();

public:
    NetwEntity();
    ~NetwEntity();

    static godot::StringName meta_key();
    static godot::StringName template_meta();

    static void set_session_lookup(const godot::Callable &p_lookup);
    static NetwMultiplayerCore *session_core_for(godot::Object *p_node);
    static godot::Ref<NetwMultiplayerCore> session_plane_for(
        godot::Object *p_node
    );

    static godot::Ref<NetwEntity> of(godot::Object *p_node);
    static godot::Ref<NetwEntity> ensure(godot::Object *p_root);
    static godot::Ref<NetwEntity> resolve(godot::Object *p_node);
    static godot::Ref<NetwEntity> from_rid(
        const godot::RID &p_entity,
        godot::Object *p_api
    );
    static godot::Ref<NetwEntity> by_route(
        int64_t p_route,
        godot::Object *p_api
    );
    static godot::StringName parse_entity(const godot::String &p_node_name);
    static int64_t parse_peer(const godot::String &p_node_name);
    static godot::String name_for(godot::Object *p_join);
    static godot::Node *find(godot::Object *p_root, godot::Object *p_join);
    static godot::Node *bind(
        godot::Object *p_node,
        const godot::StringName &p_entity_id,
        int64_t p_peer_id
    );
    static godot::Node *instantiate_from(
        godot::Object *p_template,
        const godot::Callable &p_configure
    );

    void attach_to(godot::Object *p_root);

    godot::Ref<NetwEntityRecord> get_record() const { return record; }

    godot::Node *get_owner() const;
    void set_owner(godot::Object *p_owner);

    godot::StringName get_entity_id() const;
    void set_entity_id(const godot::StringName &p_entity_id);
    int64_t get_peer_id() const;
    void set_peer_id(int64_t p_peer_id);
    int64_t get_route() const;
    void set_route(int64_t p_route);
    godot::RID get_rid_handle() const;
    void set_rid_handle(const godot::RID &p_handle);

    godot::Variant get_multiplayer() const;

    void stamp_multiplayer(godot::Object *p_api);

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
    godot::Variant get_controller_participant() const;
    int64_t get_action_spawn_tick() const { return action_spawn_tick; }
    void set_action_spawn_tick(int64_t p_tick) { action_spawn_tick = p_tick; }
    int64_t get_action_requester() const { return action_requester; }
    void set_action_requester(int64_t p_peer) { action_requester = p_peer; }

    void request_control();
    void grant_control(int64_t p_peer_id);
    void revoke_control();

    bool get_is_authority() const;
    godot::Variant get_participant() const;
    int64_t get_ownership() const;
    bool get_is_player() const;

    godot::Ref<NetwReparentOpts> get_reparenting() const;
    void set_reparenting(const godot::Ref<NetwReparentOpts> &p_opts);
    bool get_is_template() const;
    int64_t get_stage() const;
    godot::Ref<NetwDespawnOpts> get_active_despawn_opts() const;

    void mark_template();

    void note_stage(int64_t p_from);

    void arm(godot::Object *p_api);

    godot::Node *spawn_under(godot::Object *p_parent, const godot::StringName &p_id);
    godot::Node *instantiate_player(godot::Object *p_participant);
    godot::Node *spawn_player(
        godot::Object *p_participant,
        godot::Object *p_scene
    );
    void reparent_to(
        godot::Object *p_new_parent,
        const godot::Ref<NetwReparentOpts> &p_opts
    );
    void despawn(const godot::Ref<NetwDespawnOpts> &p_opts);

    godot::Variant get_components() const;
    void register_component(godot::Object *p_component);
    godot::NodePath relative_path(
        godot::Object *p_source,
        godot::Object *p_target
    ) const;
    godot::NodePath property_path(
        godot::Object *p_source,
        const godot::StringName &p_property,
        godot::Object *p_base
    ) const;
    godot::Variant get_persistence() const;
    godot::Variant get_state_binding() const;
    godot::Variant get_input_binding() const;
    godot::Variant get_broadcast_binding() const;
    godot::Variant get_interest() const;
    godot::Variant get_scene() const;
    godot::Variant get_prediction() const;
    godot::Variant get_interpolation() const;
    godot::Variant get_timeline() const;
    void set_timeline(godot::Object *p_timeline);

    godot::TypedArray<godot::MultiplayerSynchronizer> synchronizers();
    bool governs_property(
        const godot::NodePath &p_real_path,
        godot::Object *p_exclude
    ) const;
    void invalidate_synchronizers_cache();
    godot::Ref<NetwEntity> parent_entity() const;

    godot::Variant own_scene();

    void _on_identity_hydrated();
    void _handle_control_request(int64_t p_sender);
    void _handle_control_apply(int64_t p_peer);
    void _go_live_if_armed();
    void _remote_despawn(
        const godot::StringName &p_reason,
        double p_linger_seconds
    );
    void _handle_tree_entered();
    void _handle_tree_exiting();
    void _on_owner_ready();
    void _on_peer_disconnected(int64_t p_peer_id);
    void _settle_emit_reparented(const godot::Ref<NetwReparentOpts> &p_opts);
    void _do_emit_reparented(const godot::Ref<NetwReparentOpts> &p_opts);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwEntity::Ownership);
VARIANT_ENUM_CAST(netw::NetwEntity::ControlKind);
