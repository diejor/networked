#pragma once

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwInterestDecl : public godot::RefCounted {
    GDCLASS(NetwInterestDecl, godot::RefCounted)

public:
    enum LeavePolicy {
        LEAVE_DESPAWN = 0,
        LEAVE_RETAIN = 1,
        LEAVE_CUSTOM = 2,
    };

    enum PerceptionPolicy {
        PERCEPTION_HIDE = 0,
        PERCEPTION_SHOW = 1,
        PERCEPTION_CUSTOM = 2,
    };

private:
    godot::LocalVector<godot::StringName> label_ids;
    godot::HashMap<godot::StringName, int32_t> leave_policies;
    godot::HashMap<godot::StringName, godot::Callable> leave_callbacks;
    godot::HashMap<godot::StringName, int32_t> perception_policies;
    godot::HashMap<godot::StringName, godot::Callable> perception_callbacks;
    bool reports_observers = false;

protected:
    static void _bind_methods();

public:
    bool join(const godot::StringName &layer_id);

    bool leave(const godot::StringName &layer_id);

    bool has_label(const godot::StringName &layer_id) const;

    godot::Array labels() const;

    bool set_leave_policy(
        const godot::StringName &layer_id,
        int policy,
        const godot::Callable &custom
    );

    bool set_perception_policy(
        const godot::StringName &layer_id,
        int policy,
        const godot::Callable &custom
    );

    int leave_policy_for(const godot::StringName &layer_id, int fallback) const;
    int perception_policy_for(const godot::StringName &layer_id, int fallback)
        const;

    godot::Callable custom_leave_for(const godot::StringName &layer_id) const;
    godot::Callable custom_perception_for(const godot::StringName &layer_id)
        const;

    void set_reports_observers(bool p_reports) {
        reports_observers = p_reports;
    }
    bool get_reports_observers() const { return reports_observers; }

    void clear();
};

} // namespace netw
