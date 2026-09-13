#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/despawn_config.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/call_args.hpp"
#include "netw/scene_decl.hpp"

namespace netw::script::model {

void declare_rpc_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method,
    const godot::Ref<NetwMemberConfig> &p_config
);
void declare_property_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_property,
    const godot::Ref<NetwPropertyConfig> &p_config
);
void declare_signal_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_signal,
    const godot::Ref<NetwMemberConfig> &p_config
);
void declare_spawn_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method,
    const godot::Ref<NetwMemberConfig> &p_config
);
void declare_despawn_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::Ref<NetwDespawnConfig> &p_config
);
void declare_persistence_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::Ref<NetwPersistenceConfig> &p_config
);
void declare_scene(
    const godot::Ref<godot::Script> &p_script,
    const SceneDecl &p_decl
);
void bind_node_property_carry(
    godot::Node *p_node,
    const godot::StringName &p_property,
    const godot::Callable &p_step
);

godot::Ref<NetwMemberConfig> get_rpc_options(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
godot::Ref<NetwPropertyConfig> get_property_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_property
);
godot::Ref<NetwMemberConfig> get_signal_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_signal
);
godot::Ref<NetwMemberConfig> get_spawn_config(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
godot::Dictionary get_rpc_configs(const godot::Ref<godot::Script> &p_script);
godot::Dictionary get_property_configs(
    const godot::Ref<godot::Script> &p_script
);
godot::Dictionary get_signal_configs(const godot::Ref<godot::Script> &p_script);
SceneDecl get_scene_decl(const godot::Ref<godot::Script> &p_script);
godot::Ref<NetwDespawnConfig> get_despawn_config(
    const godot::Ref<godot::Script> &p_script
);
godot::Ref<NetwDespawnConfig> get_own_despawn_config(
    const godot::Ref<godot::Script> &p_script
);
godot::Ref<NetwPersistenceConfig> get_own_persistence_config(
    const godot::Ref<godot::Script> &p_script
);

godot::Ref<NetwPersistenceConfig> get_persistence_config(godot::Node *p_node);
godot::Ref<NetwPersistenceConfig> configure_node_persistence(
    godot::Node *p_node
);
godot::Ref<NetwPropertyConfig> configure_node_property(
    godot::Node *p_node,
    const godot::StringName &p_property
);
godot::Dictionary get_node_property_configs(godot::Node *p_node);
godot::Ref<NetwInterpolate> get_node_property_interpolator(
    godot::Node *p_node,
    const godot::StringName &p_property
);
godot::Callable get_node_property_carry(
    godot::Node *p_node,
    const godot::StringName &p_property
);
void clear_node_overlay(godot::Node *p_node);
void sweep_dead_overlays();

godot::Array get_method_interpolators(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
godot::Ref<NetwInterpolate> get_property_interpolator(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_property
);
godot::Array get_signal_interpolators(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_signal
);

int64_t get_method_rpc_mode(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
bool get_method_reliable(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
bool get_method_call_local(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
bool validate_argument_count(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method,
    int64_t p_arg_count
);

int64_t get_method_id(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
godot::StringName get_method_name_by_id(
    const godot::Ref<godot::Script> &p_script,
    int64_t p_method_id
);
godot::Array get_method_arg_types(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_method
);
godot::Ref<godot::Script> declaring_script(godot::Object *p_object);
int64_t get_node_property_type(
    godot::Node *p_node,
    const godot::StringName &p_property
);
godot::Array get_signal_arg_types(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_signal
);
int64_t get_property_id(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_property
);
godot::StringName get_property_name_by_id(
    const godot::Ref<godot::Script> &p_script,
    int64_t p_property_id
);
int64_t get_signal_id(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_signal
);
godot::StringName get_signal_name_by_id(
    const godot::Ref<godot::Script> &p_script,
    int64_t p_signal_id
);

bool write_token(wire::WriteStream &p_stream, const godot::Variant &p_token);
bool read_token(wire::ReadStream &p_stream, godot::Variant &r_token);
bool write_call_body(
    wire::WriteStream &p_stream,
    const godot::Variant &p_method,
    const godot::LocalVector<call_args::Slot> &p_slots,
    const godot::Array &p_quantizers,
    const godot::Array &p_arg_types
);
godot::PackedByteArray token_bytes(const godot::Variant &p_token);
godot::Variant token_of_bytes(const godot::PackedByteArray &p_bytes);

} // namespace netw::script::model
