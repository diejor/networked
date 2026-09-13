#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/interest/engine.hpp"
#include "netw/object_port.hpp"

namespace netw {

class NetwMultiplayer;

class NetwInterestLayer : public godot::RefCounted {
    GDCLASS(NetwInterestLayer, godot::RefCounted)

private:
    godot::StringName layer_id;
    interest::Engine standalone;
    interest::Engine *engine = &standalone;
    ObjectPort session;

    NetwMultiplayer *host();
    godot::Ref<NetwEntity> entity_for(int64_t p_slot);
    bool server_authority();
    int64_t local_peer_id();
    void dispatch_enter(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id
    );
    void dispatch_leave(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id
    );
    int64_t adopt_slot(const godot::Ref<NetwEntity> &p_entity);
    void track_membership(
        const godot::Ref<NetwEntity> &p_entity,
        bool p_joined
    );
    void request_flush();
    void join_label(const godot::Ref<NetwEntity> &p_entity);
    void refresh_perception(const godot::Ref<NetwEntity> &p_entity);

protected:
    static void _bind_methods();

public:
    static int64_t slot_of(const godot::Ref<NetwEntity> &p_entity);

    void bind_session(NetwMultiplayer *p_host);

    void set_layer_id(const godot::StringName &p_id);
    godot::StringName get_layer_id() const {
        return layer_id;
    }

    bool set_policy(int p_policy);
    int get_policy() const;

    void set_default_leave_policy(int p_policy);
    int get_default_leave_policy() const;

    void set_default_perception_policy(int p_policy);
    int get_default_perception_policy() const;

    godot::Dictionary get_viewers() const;
    godot::Dictionary get_entities();

    bool add_viewer(int64_t p_peer_id);
    bool remove_viewer(int64_t p_peer_id);
    bool has_viewer(int64_t p_peer_id) const;

    bool add_entity(const godot::Ref<NetwEntity> &p_entity);
    bool remove_entity(const godot::Ref<NetwEntity> &p_entity);
    bool has_entity(const godot::Ref<NetwEntity> &p_entity) const;

    void client_admit(const godot::Ref<NetwEntity> &p_entity);
    void client_revoke(const godot::Ref<NetwEntity> &p_entity);
    void client_untrack_entity(const godot::Ref<NetwEntity> &p_entity);

    void apply_server_transition(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id,
        bool p_visible
    );

    bool is_visible_to(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_peer_id
    );
    bool verdict_for(int64_t p_peer_id) const;

    godot::Array viewer_ids() const;
    godot::Dictionary monitor_snapshot() const;
    godot::Dictionary debug_dump(int64_t p_peer_id = 0);
};

} // namespace netw
