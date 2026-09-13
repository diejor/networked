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

class NetwInterestHandle : public godot::RefCounted {
    GDCLASS(NetwInterestHandle, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    void bind(NetwEntity *p_entity);

    static godot::Ref<NetwInterestHandle> of(godot::Node *p_node);

    godot::Ref<NetwInterestHandle> join(
        const godot::StringName &p_layer_id,
        int64_t p_leave_policy = -1,
        int64_t p_perception_policy = -1
    );
    godot::Ref<NetwInterestHandle> leave(const godot::StringName &p_layer_id);
    godot::TypedArray<godot::StringName> layer_ids() const;
    bool is_visible_to(int64_t p_peer_id) const;

    godot::Ref<NetwInterestHandle> on_enter(
        const godot::Callable &p_callback,
        const godot::StringName &p_layer_id
    );
    godot::Ref<NetwInterestHandle> on_leave(
        const godot::Callable &p_callback,
        const godot::StringName &p_layer_id
    );
    godot::Ref<NetwInterestHandle> on_observed(
        const godot::Callable &p_callback
    );
    godot::Ref<NetwInterestHandle> on_unobserved(
        const godot::Callable &p_callback
    );
    godot::Ref<NetwInterestHandle> on_leave_policy(
        const godot::StringName &p_layer_id,
        NetwMultiplayer::LeavePolicy p_policy,
        const godot::Callable &p_custom
    );
    godot::Ref<NetwInterestHandle> on_perception_policy(
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

    void client_join_label(const godot::StringName &p_layer_id);
    void dispatch_enter(const godot::StringName &p_layer_id, int64_t p_peer);
    void dispatch_leave(const godot::StringName &p_layer_id, int64_t p_peer);

    void activate();
    void deactivate();
    void on_observer_entered(
        const godot::StringName &p_layer_id,
        int64_t p_peer
    );
    void on_observer_left(const godot::StringName &p_layer_id, int64_t p_peer);

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
    godot::Ref<NetwInterestHandle> bind_layer_callback(
        const godot::Callable &p_callback,
        const godot::StringName &p_layer_id,
        bool p_on_enter
    );
};

godot::Ref<NetwInterestHandle> build_interest_handle(godot::Object *p_entity);

} // namespace netw
