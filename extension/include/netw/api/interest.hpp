#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/interest/decl.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;

class NetwInterest : public godot::RefCounted {
    GDCLASS(NetwInterest, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    void bind(NetwEntity *p_entity);
    void announce_observer(
        const godot::StringName &p_layer_id,
        int64_t p_peer,
        bool p_entered
    );

    static godot::Ref<NetwInterest> of(godot::Node *p_node);

    godot::Ref<NetwInterest> join(
        const godot::StringName &p_layer_id,
        NetwMultiplayer::LeavePolicy p_leave_policy
        = NetwMultiplayer::LeavePolicy(-1),
        NetwMultiplayer::PerceptionPolicy p_perception_policy
        = NetwMultiplayer::PerceptionPolicy(-1)
    );
    godot::Ref<NetwInterest> leave(const godot::StringName &p_layer_id);
    godot::TypedArray<godot::StringName> layer_ids() const;
    bool is_visible_to(int64_t p_peer_id) const;

    godot::Ref<NetwInterest> on_enter(
        const godot::Callable &p_callback,
        const godot::StringName &p_layer_id
    );
    godot::Ref<NetwInterest> on_leave(
        const godot::Callable &p_callback,
        const godot::StringName &p_layer_id
    );
    godot::Ref<NetwInterest> on_observed(
        const godot::Callable &p_callback
    );
    godot::Ref<NetwInterest> on_unobserved(
        const godot::Callable &p_callback
    );
    godot::Ref<NetwInterest> on_leave_policy(
        const godot::StringName &p_layer_id,
        NetwMultiplayer::LeavePolicy p_policy,
        const godot::Callable &p_custom
    );
    godot::Ref<NetwInterest> on_perception_policy(
        const godot::StringName &p_layer_id,
        NetwMultiplayer::PerceptionPolicy p_policy,
        const godot::Callable &p_custom
    );

    void set_report_observers(bool p_enabled);
    bool reports_observers() const;
    int64_t leave_policy_for(
        const godot::StringName &p_layer_id,
        int64_t p_fallback
    ) const;
    godot::Callable custom_leave_for(const godot::StringName &p_layer_id) const;
    int64_t perception_policy_for(
        const godot::StringName &p_layer_id,
        int64_t p_fallback
    ) const;
    godot::Callable custom_perception_for(
        const godot::StringName &p_layer_id
    ) const;

    void dispatch_enter(const godot::StringName &p_layer_id, int64_t p_peer);
    void dispatch_leave(const godot::StringName &p_layer_id, int64_t p_peer);

    void activate();
    void deactivate();

    godot::Ref<NetwEntity> entity() const;
    interest::Decl *declaration();

private:
    godot::ObjectID entity_id;

    NetwMultiplayer *core() const;
    NetwMultiplayer *attached_core() const;
    interest::Facet *facet() const;
    bool is_authority() const;
    godot::RID layer_ensure(const godot::StringName &p_layer_id);
    void layer_join_live(const godot::StringName &p_layer_id);
    void layer_leave_live(const godot::StringName &p_layer_id);
    godot::Ref<NetwInterest> bind_layer_callback(
        const godot::Callable &p_callback,
        const godot::StringName &p_layer_id,
        bool p_on_enter
    );
};

godot::Ref<NetwInterest> build_interest_handle(godot::Object *p_entity);

} // namespace netw
