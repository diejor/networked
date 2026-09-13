#pragma once

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwMemberConfig : public godot::RefCounted {
    GDCLASS(NetwMemberConfig, godot::RefCounted)

public:
    enum Policy {
        POLICY_AUTHORITY = 0,
        POLICY_CONTROLLER = 1,
        POLICY_ANY_PEER = 2,
    };

    enum TransferMode {
        TRANSFER_RELIABLE = 0,
        TRANSFER_UNRELIABLE = 1,
    };

private:
    Policy write_policy = POLICY_AUTHORITY;
    TransferMode transfer_mode = TRANSFER_RELIABLE;
    bool is_call_local = false;
    godot::ObjectID context_script_id;
    godot::StringName context_name;
    int64_t context_type = 0;
    godot::Variant context_node_ref;
    godot::StringName defer_signal_name;
    bool is_controller_only = false;
    godot::Array quantizers;
    godot::Array interpolators;

    bool policy_configured = false;
    bool transfer_configured = false;
    bool local_configured = false;

    void publish_policy();
    godot::Array member_types() const;

protected:
    static void _bind_methods();

public:
    static void set_member_types_reader(const godot::Callable &p_reader);

    void set_write_policy(Policy p_write_policy);
    Policy get_write_policy() const {
        return write_policy;
    }

    void set_transfer_mode(TransferMode p_transfer_mode);
    TransferMode get_transfer_mode() const {
        return transfer_mode;
    }

    void set_is_call_local(bool p_is_call_local);
    bool get_is_call_local() const {
        return is_call_local;
    }

    void set_context_script(const godot::Ref<godot::Script> &p_script) {
        context_script_id = gd::instance_id(p_script.ptr());
    }
    godot::Ref<godot::Script> get_context_script() const {
        return godot::Ref<godot::Script>(godot::Object::cast_to<godot::Script>(
            gd::object_of(context_script_id)
        ));
    }

    void set_context_name(const godot::StringName &p_context_name) {
        context_name = p_context_name;
    }
    godot::StringName get_context_name() const {
        return context_name;
    }

    void set_context_type(int64_t p_context_type) {
        context_type = p_context_type;
    }
    int64_t get_context_type() const {
        return context_type;
    }

    void set_context_node_ref(const godot::Variant &p_context_node_ref) {
        context_node_ref = p_context_node_ref;
    }
    godot::Variant get_context_node_ref() const {
        return context_node_ref;
    }

    void set_defer_signal_name(const godot::StringName &p_defer_signal_name) {
        defer_signal_name = p_defer_signal_name;
    }
    godot::StringName get_defer_signal_name() const {
        return defer_signal_name;
    }

    void set_is_controller_only(bool p_is_controller_only) {
        is_controller_only = p_is_controller_only;
    }
    bool get_is_controller_only() const {
        return is_controller_only;
    }

    void set_quantizers(const godot::Array &p_quantizers) {
        quantizers = p_quantizers;
    }
    godot::Array get_quantizers() const {
        return quantizers;
    }

    void set_interpolators(const godot::Array &p_interpolators) {
        interpolators = p_interpolators;
    }
    godot::Array get_interpolators() const {
        return interpolators;
    }

    godot::Ref<NetwMemberConfig> authority();
    godot::Ref<NetwMemberConfig> controller();
    godot::Ref<NetwMemberConfig> any_peer();
    godot::Ref<NetwMemberConfig> reliable();
    godot::Ref<NetwMemberConfig> unreliable();
    godot::Ref<NetwMemberConfig> call_local();
    godot::Ref<NetwMemberConfig> call_remote();
    godot::Ref<NetwMemberConfig> defer_until(const godot::Signal &p_signal);
    godot::Ref<NetwMemberConfig> controller_only();
    godot::Ref<NetwMemberConfig> quantize(const godot::Array &p_quantizers);
    godot::Ref<NetwMemberConfig> interpolate(
        const godot::Array &p_interpolators
    );

    bool is_policy_declared() const {
        return policy_configured;
    }
    bool is_transfer_declared() const {
        return transfer_configured;
    }
    bool is_interpolation_only() const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwMemberConfig::Policy);
VARIANT_ENUM_CAST(netw::NetwMemberConfig::TransferMode);
