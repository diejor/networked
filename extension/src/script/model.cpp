#include "netw/script/model.hpp"

#include "netw/call_args.hpp"

#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/utility.hpp"
#include "netw/script/cache.hpp"
#include "netw/script/registry.hpp"

using namespace godot;

namespace netw::script::model {

namespace {

struct NodeOverlay {
    Dictionary configs;
    Dictionary carry;
    Ref<NetwPersistenceConfig> persistence;
    uint64_t property_revision = 1;
};

HashMap<uint64_t, script::Cache> &cache_book() {
    static HashMap<uint64_t, script::Cache> book;
    return book;
}

HashMap<uint64_t, NodeOverlay> &overlay_book() {
    static HashMap<uint64_t, NodeOverlay> book;
    return book;
}

constexpr uint32_t OVERLAY_SWEEP_FLOOR = 64;

uint32_t &overlay_sweep_at() {
    static uint32_t at = OVERLAY_SWEEP_FLOOR;
    return at;
}

uint64_t &epoch_of_interpolation() {
    static uint64_t epoch = 1;
    return epoch;
}

script::Cache *cache_for(const Ref<Script> &p_script) {
    if (p_script.is_null()) {
        return nullptr;
    }
    const uint64_t key = uint64_t(gd::instance_id(p_script.ptr()));
    HashMap<uint64_t, script::Cache>::Iterator found = cache_book().find(key);
    if (found) {
        return &found->value;
    }
    cache_book().insert(key, script::Cache(p_script));
    return &cache_book().find(key)->value;
}

NodeOverlay *overlay_for(Node *p_node, bool p_create) {
    if (p_node == nullptr) {
        return nullptr;
    }
    const uint64_t key = uint64_t(gd::instance_id(p_node));
    HashMap<uint64_t, NodeOverlay>::Iterator found = overlay_book().find(key);
    if (found) {
        return &found->value;
    }
    if (!p_create) {
        return nullptr;
    }
    if (overlay_book().size() >= overlay_sweep_at()) {
        sweep_dead_overlays();
        const uint32_t doubled = overlay_book().size() * 2;
        overlay_sweep_at()
            = doubled > OVERLAY_SWEEP_FLOOR ? doubled : OVERLAY_SWEEP_FLOOR;
    }
    overlay_book().insert(key, NodeOverlay());
    return &overlay_book().find(key)->value;
}

Ref<Script> script_of(Node *p_node) {
    if (p_node == nullptr) {
        return Ref<Script>();
    }
    return Ref<Script>(p_node->get_script());
}

Ref<NetwInterpolate> first_interpolator(const Ref<NetwMemberConfig> &p_config) {
    if (p_config.is_null()) {
        return Ref<NetwInterpolate>();
    }
    const Array specs = p_config->get_interpolators();
    if (specs.is_empty()) {
        return Ref<NetwInterpolate>();
    }
    return Ref<NetwInterpolate>(specs[0]);
}

Array as_type_array(const LocalVector<int> &p_types) {
    Array out;
    for (uint32_t index = 0; index < p_types.size(); index++) {
        out.push_back(int64_t(p_types[index]));
    }
    return out;
}

int64_t id_of(
    const LocalVector<StringName> &p_names,
    const StringName &p_name
) {
    for (uint32_t index = 0; index < p_names.size(); index++) {
        if (p_names[index] == p_name) {
            return index < 255 ? int64_t(index) + 1 : 0;
        }
    }
    return 0;
}

StringName name_at(const LocalVector<StringName> &p_names, int64_t p_id) {
    const int64_t index = p_id - 1;
    if (index < 0 || index >= int64_t(p_names.size())) {
        return StringName();
    }
    return p_names[uint32_t(index)];
}

} // namespace

void declare_rpc_config(
    const Ref<Script> &p_script,
    const StringName &p_method,
    const Ref<NetwMemberConfig> &p_config
) {
    registry::declare_member(
        p_script,
        registry::MEMBER_RPC,
        p_method,
        p_config
    );
}

void declare_property_config(
    const Ref<Script> &p_script,
    const StringName &p_property,
    const Ref<NetwPropertyConfig> &p_config
) {
    registry::declare_member(
        p_script,
        registry::MEMBER_PROPERTY,
        p_property,
        p_config
    );
    advance_interpolation_epoch();
}

void declare_signal_config(
    const Ref<Script> &p_script,
    const StringName &p_signal,
    const Ref<NetwMemberConfig> &p_config
) {
    registry::declare_member(
        p_script,
        registry::MEMBER_SIGNAL,
        p_signal,
        p_config
    );
}

void declare_spawn_config(
    const Ref<Script> &p_script,
    const StringName &p_method,
    const Ref<NetwMemberConfig> &p_config
) {
    registry::declare_member(
        p_script,
        registry::MEMBER_SPAWN,
        p_method,
        p_config
    );
}

void declare_despawn_config(
    const Ref<Script> &p_script,
    const Ref<NetwDespawnConfig> &p_config
) {
    registry::declare_script_config(
        p_script,
        registry::SCRIPT_DESPAWN,
        p_config
    );
}

void declare_scene(const Ref<Script> &p_script, const SceneDecl &p_decl) {
    registry::declare_scene(p_script, p_decl);
}

void bind_node_property_carry(
    Node *p_node,
    const StringName &p_property,
    const Callable &p_step
) {
    NodeOverlay *overlay = overlay_for(p_node, true);
    if (overlay == nullptr) {
        return;
    }
    overlay->carry[p_property] = p_step;
}

Ref<NetwMemberConfig> get_rpc_options(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    return registry::member_config(p_script, registry::MEMBER_RPC, p_method);
}

Ref<NetwPropertyConfig> get_property_config(
    const Ref<Script> &p_script,
    const StringName &p_property
) {
    return registry::member_config(
        p_script,
        registry::MEMBER_PROPERTY,
        p_property
    );
}

Ref<NetwMemberConfig> get_signal_config(
    const Ref<Script> &p_script,
    const StringName &p_signal
) {
    return registry::member_config(p_script, registry::MEMBER_SIGNAL, p_signal);
}

Ref<NetwMemberConfig> get_spawn_config(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    return registry::member_config(p_script, registry::MEMBER_SPAWN, p_method);
}

Dictionary get_rpc_configs(const Ref<Script> &p_script) {
    return registry::member_configs(p_script, registry::MEMBER_RPC);
}

Dictionary get_property_configs(const Ref<Script> &p_script) {
    return registry::member_configs(p_script, registry::MEMBER_PROPERTY);
}

Dictionary get_signal_configs(const Ref<Script> &p_script) {
    return registry::member_configs(p_script, registry::MEMBER_SIGNAL);
}

SceneDecl get_scene_decl(const Ref<Script> &p_script) {
    return registry::scene_decl(p_script);
}

Ref<NetwDespawnConfig> get_despawn_config(const Ref<Script> &p_script) {
    return registry::script_config(p_script, registry::SCRIPT_DESPAWN);
}

Ref<NetwDespawnConfig> get_own_despawn_config(const Ref<Script> &p_script) {
    return registry::own_script_config(p_script, registry::SCRIPT_DESPAWN);
}

Ref<NetwPersistenceConfig> get_persistence_config(Node *p_node) {
    const NodeOverlay *overlay = overlay_for(p_node, false);
    return overlay != nullptr ? overlay->persistence
                              : Ref<NetwPersistenceConfig>();
}

Ref<NetwPersistenceConfig> configure_node_persistence(Node *p_node) {
    NodeOverlay *overlay = overlay_for(p_node, true);
    if (overlay == nullptr) {
        return Ref<NetwPersistenceConfig>();
    }
    if (overlay->persistence.is_null()) {
        overlay->persistence.instantiate();
    }
    return overlay->persistence;
}

Ref<NetwPropertyConfig> configure_node_property(
    Node *p_node,
    const StringName &p_property
) {
    NodeOverlay *overlay = overlay_for(p_node, true);
    if (overlay == nullptr) {
        return Ref<NetwPropertyConfig>();
    }
    const Ref<NetwPropertyConfig> existing
        = overlay->configs.get(p_property, Variant());
    if (existing.is_valid()) {
        return existing;
    }
    Ref<NetwPropertyConfig> declared;
    declared.instantiate();
    declared->set_context_script(script_of(p_node));
    declared->set_context_name(p_property);
    declared->set_context_type(1);
    declared->set_context_node_ref(gd::weak_ref(p_node));
    overlay->configs[p_property] = declared;
    advance_interpolation_epoch();
    return declared;
}

Dictionary get_node_property_configs(Node *p_node) {
    Dictionary answered;
    const Dictionary declared = get_property_configs(script_of(p_node));
    const Array declared_names = declared.keys();
    for (int index = 0; index < declared_names.size(); index++) {
        answered[declared_names[index]] = declared[declared_names[index]];
    }
    const NodeOverlay *overlay = overlay_for(p_node, false);
    if (overlay == nullptr) {
        return answered;
    }
    const Array overlay_names = overlay->configs.keys();
    for (int index = 0; index < overlay_names.size(); index++) {
        if (answered.has(overlay_names[index])) {
            continue;
        }
        answered[overlay_names[index]] = overlay->configs[overlay_names[index]];
    }
    return answered;
}

Ref<NetwInterpolate> get_node_property_interpolator(
    Node *p_node,
    const StringName &p_property
) {
    const NodeOverlay *overlay = overlay_for(p_node, false);
    if (overlay != nullptr) {
        const Ref<NetwInterpolate> per_instance = first_interpolator(
            Ref<NetwMemberConfig>(overlay->configs.get(p_property, Variant()))
        );
        if (per_instance.is_valid()) {
            return per_instance;
        }
    }
    return get_property_interpolator(script_of(p_node), p_property);
}

Callable get_node_property_carry(Node *p_node, const StringName &p_property) {
    const NodeOverlay *overlay = overlay_for(p_node, false);
    if (overlay == nullptr) {
        return Callable();
    }
    return overlay->carry.get(p_property, Callable());
}

void clear_node_overlay(Node *p_node) {
    if (p_node == nullptr) {
        return;
    }
    if (overlay_book().erase(uint64_t(gd::instance_id(p_node)))) {
        advance_interpolation_epoch();
    }
}

uint64_t interpolation_epoch() {
    return epoch_of_interpolation();
}

void advance_interpolation_epoch() {
    epoch_of_interpolation() += 1;
}

void sweep_dead_overlays() {
    LocalVector<uint64_t> dead;
    for (const KeyValue<uint64_t, NodeOverlay> &entry : overlay_book()) {
        if (gd::object_of(ObjectID(entry.key)) == nullptr) {
            dead.push_back(entry.key);
        }
    }
    for (uint32_t index = 0; index < dead.size(); index++) {
        overlay_book().erase(dead[index]);
    }
}

void clear() {
    overlay_book().clear();
    overlay_sweep_at() = OVERLAY_SWEEP_FLOOR;
    cache_book().clear();
}

#if defined(NETW_TESTS)
int64_t node_overlay_count() {
    return int64_t(overlay_book().size());
}
#endif

Array get_method_interpolators(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    const Ref<NetwMemberConfig> declared = get_rpc_options(p_script, p_method);
    return declared.is_valid() ? declared->get_interpolators() : Array();
}

Ref<NetwInterpolate> get_property_interpolator(
    const Ref<Script> &p_script,
    const StringName &p_property
) {
    return first_interpolator(get_property_config(p_script, p_property));
}

Array get_signal_interpolators(
    const Ref<Script> &p_script,
    const StringName &p_signal
) {
    const Ref<NetwMemberConfig> declared
        = get_signal_config(p_script, p_signal);
    return declared.is_valid() ? declared->get_interpolators() : Array();
}

int64_t get_method_rpc_mode(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    const script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return 0;
    }
    const Dictionary annotated = cache->rpc_config().get(p_method, Variant());
    return int64_t(annotated.get("rpc_mode", int64_t(0)));
}

bool get_method_reliable(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    const script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return true;
    }
    const Dictionary annotated = cache->rpc_config().get(p_method, Variant());
    if (!annotated.has("transfer_mode")) {
        return true;
    }
    return int64_t(annotated["transfer_mode"]) == 2;
}

bool get_method_call_local(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    const script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return false;
    }
    const Dictionary annotated = cache->rpc_config().get(p_method, Variant());
    return bool(annotated.get("call_local", false));
}

bool validate_argument_count(
    const Ref<Script> &p_script,
    const StringName &p_method,
    int64_t p_arg_count
) {
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return false;
    }
    const script::MethodArity declared = cache->arity(p_method);
    if (!declared.declared) {
        return false;
    }
    return p_arg_count >= declared.minimum && p_arg_count <= declared.maximum;
}

int64_t get_method_id(const Ref<Script> &p_script, const StringName &p_method) {
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return 0;
    }
    return id_of(cache->text_ordered_methods(), p_method);
}

StringName get_method_name_by_id(
    const Ref<Script> &p_script,
    int64_t p_method_id
) {
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return StringName();
    }
    return name_at(cache->text_ordered_methods(), p_method_id);
}

Array get_method_arg_types(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return Array();
    }
    return as_type_array(cache->arg_types(p_method));
}

Ref<Script> declaring_script(Object *p_object) {
    if (p_object == nullptr) {
        return Ref<Script>();
    }
    Script *itself = Object::cast_to<Script>(p_object);
    if (itself != nullptr) {
        return Ref<Script>(itself);
    }
    return Ref<Script>(Object::cast_to<Script>(p_object->get_script()));
}

namespace {

int configured_type(Node *p_node, const StringName &p_property) {
    Ref<NetwPropertyConfig> config
        = get_property_config(script_of(p_node), p_property);
    if (config.is_null()) {
        const NodeOverlay *overlay = overlay_for(p_node, false);
        if (overlay != nullptr) {
            config = overlay->configs.get(p_property, Variant());
        }
    }
    return config.is_valid() ? config->get_declared_type() : -1;
}

void properties_changed(int64_t p_node) {
    NodeOverlay *overlay = overlay_for(
        Object::cast_to<Node>(gd::object_of(ObjectID(p_node))),
        false
    );
    if (overlay != nullptr) {
        overlay->property_revision += 1;
    }
}

} // namespace

int64_t get_node_property_type(Node *p_node, const StringName &p_property) {
    if (p_node == nullptr) {
        return int64_t(Variant::NIL);
    }
    const int declared = configured_type(p_node, p_property);
    if (declared >= 0) {
        return declared;
    }
    script::Cache *cache = cache_for(script_of(p_node));
    if (cache != nullptr) {
        return int64_t(cache->prop_type(p_node, p_property));
    }
    const gd::PropertyResolution listed
        = gd::listed_property(gd::property_list(p_node), p_property);
    return listed.type >= 0 ? listed.type : int64_t(Variant::NIL);
}

gd::PropertyResolution resolve_property(
    Node *p_node,
    const StringName &p_property,
    const Array &p_infos
) {
    gd::PropertyResolution resolved
        = gd::resolve_property(p_node, p_property, p_infos);
    const int declared = configured_type(p_node, p_property);
    if (declared >= 0) {
        if (resolved.type >= 0 && declared != resolved.type) {
            return {};
        }
        resolved.type = declared;
    }
    return resolved;
}

void watch_properties(Node *p_node) {
    NodeOverlay *overlay = overlay_for(p_node, true);
    if (overlay == nullptr) {
        return;
    }
    const Callable changed = callable_mp_static(&properties_changed)
                                 .bind(int64_t(p_node->get_instance_id()));
    if (!p_node->is_connected(StringName("property_list_changed"), changed)) {
        p_node->connect(StringName("property_list_changed"), changed);
    }
}

uint64_t property_revision(Node *p_node) {
    const NodeOverlay *overlay = overlay_for(p_node, false);
    return overlay != nullptr ? overlay->property_revision : 0;
}

Array get_signal_arg_types(
    const Ref<Script> &p_script,
    const StringName &p_signal
) {
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return Array();
    }
    return as_type_array(cache->signal_arg_types(p_signal));
}

int64_t get_property_id(
    const Ref<Script> &p_script,
    const StringName &p_property
) {
    if (p_script.is_valid()
        && p_script->get_class() == StringName("CSharpScript")) {
        return 0;
    }
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return 0;
    }
    return id_of(cache->text_ordered_properties(), p_property);
}

StringName get_property_name_by_id(
    const Ref<Script> &p_script,
    int64_t p_property_id
) {
    if (p_script.is_valid()
        && p_script->get_class() == StringName("CSharpScript")) {
        return StringName();
    }
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return StringName();
    }
    return name_at(cache->text_ordered_properties(), p_property_id);
}

int64_t get_signal_id(const Ref<Script> &p_script, const StringName &p_signal) {
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return 0;
    }
    return id_of(cache->text_ordered_signals(), p_signal);
}

StringName get_signal_name_by_id(
    const Ref<Script> &p_script,
    int64_t p_signal_id
) {
    script::Cache *cache = cache_for(p_script);
    if (cache == nullptr) {
        return StringName();
    }
    return name_at(cache->text_ordered_signals(), p_signal_id);
}

bool write_token(wire::WriteStream &p_stream, const Variant &p_token) {
    bool by_id = p_token.get_type() == Variant::INT;
    if (!p_stream.bool1(by_id)) {
        return false;
    }
    if (by_id) {
        uint64_t id = uint64_t(int64_t(p_token)) & 0xFF;
        return p_stream.bits(id, 8);
    }
    String name = String(p_token);
    return wire::string_field(p_stream, name);
}

bool read_token(wire::ReadStream &p_stream, Variant &r_token) {
    bool by_id = false;
    if (!p_stream.bool1(by_id)) {
        return false;
    }
    if (by_id) {
        uint64_t id = 0;
        if (!p_stream.bits(id, 8)) {
            return false;
        }
        r_token = int64_t(id);
        return true;
    }
    String name;
    if (!wire::string_field(p_stream, name)) {
        return false;
    }
    r_token = StringName(name);
    return true;
}

bool write_call_body(
    wire::WriteStream &p_stream,
    const Variant &p_method,
    const LocalVector<call_args::Slot> &p_slots,
    const Array &p_quantizers,
    const Array &p_arg_types
) {
    return write_token(p_stream, p_method)
        && call_args::write(p_stream, p_slots, p_quantizers, p_arg_types);
}

PackedByteArray token_bytes(const Variant &p_token) {
    wire::WriteStream stream;
    if (!write_token(stream, p_token) || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

Variant token_of_bytes(const PackedByteArray &p_bytes) {
    wire::ReadStream stream(p_bytes);
    Variant token;
    if (!read_token(stream, token) || !stream.align_verify()
        || stream.bits_remaining() != 0) {
        return StringName();
    }
    return token;
}

} // namespace netw::script::model
