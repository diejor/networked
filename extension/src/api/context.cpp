#include "netw/api/context.hpp"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/utility.hpp"
#include "godot/vararg.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/connect/transport.hpp"
#include "netw/log.hpp"
#include "netw/scene_core.hpp"
#include "netw/scene_decl.hpp"
#include "netw/script/model.hpp"
#include "netw/session_decl.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *META_COMPONENT_INJECTED = "_netw_comp_injected";
constexpr const char *META_DERIVED_SCHEDULED = "_netw_derived_reg_scheduled";
constexpr const char *META_LINT_INJECTED = "_netw_property_lint_injected";
constexpr const char *META_FIRED_PREFIX = "_netw_fired_";
constexpr const char *SIG_TREE_ENTERED = "tree_entered";
constexpr const char *SIG_TREE_EXITING = "tree_exiting";

Ref<Script> script_of(Object *p_object) {
    if (p_object == nullptr) {
        return Ref<Script>();
    }
    return Ref<Script>(Object::cast_to<Script>(p_object->get_script()));
}

Node *node_behind(const Variant &p_held) {
    return Object::cast_to<Node>(gd::live_object(p_held));
}

template <typename T>
Ref<T> declare_config(
    Node *p_node,
    session_decl::Kind p_kind,
    const Ref<T> &p_preset
) {
    const char *verb = session_decl::verb_of(p_kind);
    NETW_ERR_COND_V(
        p_node == nullptr,
        Ref<T>(),
        sys::SESSION,
        "Netw.%s: a scope node is required, because the node names the "
        "multiplayer branch the declaration governs",
        verb
    );
    const Ref<T> standing = session_decl::payload_on(p_node, p_kind);
    NETW_ERR_COND_V(
        standing.is_valid(),
        Ref<T>(),
        sys::SESSION,
        "Netw.%s: '%s' already declares this configuration. Keep the Resource "
        "returned by the first call and finish authoring it before "
        "configuration settles.",
        verb,
        String(p_node->get_name())
    );
    Ref<T> draft;
    draft.instantiate();
    if (p_preset.is_valid()) {
        draft->copy_values_from(**p_preset);
    }
    if (session_decl::declare(p_node, p_kind, Callable(), draft, verb) != OK) {
        return Ref<T>();
    }
    return draft;
}

NetwMultiplayer *sole_session(const char *p_verb) {
    const TypedArray<MultiplayerAPI> live = NetwMultiplayer::session_get_all();
    if (live.is_empty()) {
        NETW_ERROR(sys::SESSION, "Netw.%s: no active session", p_verb);
        return nullptr;
    }
    if (live.size() > 1) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.%s: %d sessions are active; call the verb on the session "
            "you mean through Netw.of(node)",
            p_verb,
            int(live.size())
        );
        return nullptr;
    }
    const Ref<MultiplayerAPI> only = live[0];
    return Object::cast_to<NetwMultiplayer>(only.ptr());
}

NetwMultiplayer *rpc_interface(const Callable &p_callable) {
    Node *node = Object::cast_to<Node>(p_callable.get_object());
    if (node == nullptr) {
        NETW_ERROR(sys::SESSION, "a Netw RPC target must be a Node");
        return nullptr;
    }
    if (NetwEntity::of(node).is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "the Netw RPC target '%s' is not part of a NetwEntity",
            String(node->get_name())
        );
        return nullptr;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::SESSION,
            "no session governs the Netw RPC target '%s'",
            String(node->get_name())
        );
    }
    return api;
}

bool node_declares(Node *p_node, const StringName &p_property) {
    const Array declared = gd::property_list(p_node);
    for (int at = 0; at < declared.size(); ++at) {
        const Dictionary row = declared[at];
        if (StringName(row.get("name", StringName())) == p_property) {
            return true;
        }
    }
    return false;
}

void mark_signal_fired(const Variant &p_node, const String &p_meta_key) {
    Node *node = node_behind(p_node);
    if (node != nullptr) {
        node->set_meta(StringName(p_meta_key), true);
    }
}

void setup_defer_latches(Node *p_node) {
    const Ref<Script> script = script_of(p_node);
    if (script.is_null()) {
        return;
    }
    const Dictionary configs = netw::script::model::get_rpc_configs(script);
    const Array methods = configs.keys();
    for (int at = 0; at < methods.size(); ++at) {
        const Ref<NetwMemberConfig> config = configs[methods[at]];
        if (config.is_null()) {
            continue;
        }
        const StringName latched = config->get_defer_signal_name();
        if (String(latched).is_empty() || latched == StringName("ready")) {
            continue;
        }
        if (!p_node->has_signal(latched)) {
            continue;
        }
        const String meta_key = String(META_FIRED_PREFIX) + String(latched);
        if (p_node->has_meta(StringName(meta_key))) {
            continue;
        }
        p_node->set_meta(StringName(meta_key), false);
        p_node->connect(
            latched,
            callable_mp_static(&mark_signal_fired)
                .bind(Variant(p_node), meta_key),
            Object::CONNECT_ONE_SHOT
        );
    }
}

void register_as_component(const Variant &p_node) {
    Node *node = node_behind(p_node);
    if (node == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_valid()) {
        entity->register_component(node);
    } else {
        NETW_WARN(
            sys::ENTITY,
            "'%s' carries networked configuration but is not part of a "
            "NetwEntity",
            String(node->get_name())
        );
    }
    setup_defer_latches(node);
}

void inject_component_registration(Node *p_node) {
    if (p_node->has_meta(StringName(META_COMPONENT_INJECTED))) {
        return;
    }
    p_node->set_meta(StringName(META_COMPONENT_INJECTED), true);
    if (p_node->is_inside_tree()) {
        register_as_component(Variant(p_node));
        return;
    }
    p_node->connect(
        StringName(SIG_TREE_ENTERED),
        callable_mp_static(&register_as_component).bind(Variant(p_node)),
        Object::CONNECT_ONE_SHOT
    );
}

SyncPipeline *pipeline_of(Node *p_node) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    return api != nullptr ? api->sync_pipeline() : nullptr;
}

void register_derived_now(const Variant &p_node) {
    Node *node = node_behind(p_node);
    if (node == nullptr) {
        return;
    }
    if (SyncPipeline *pipeline = pipeline_of(node)) {
        pipeline->register_derived(node);
    }
}

void unregister_derived_now(const Variant &p_node) {
    Node *node = node_behind(p_node);
    if (node == nullptr) {
        return;
    }
    if (SyncPipeline *pipeline = pipeline_of(node)) {
        pipeline->unregister_derived(node);
    }
}

void schedule_derived_registration(Node *p_node) {
    if (p_node->has_meta(StringName(META_DERIVED_SCHEDULED))) {
        return;
    }
    p_node->set_meta(StringName(META_DERIVED_SCHEDULED), true);
    p_node->connect(
        StringName(SIG_TREE_EXITING),
        callable_mp_static(&unregister_derived_now).bind(Variant(p_node))
    );
    if (p_node->is_inside_tree()) {
        register_derived_now(Variant(p_node));
        return;
    }
    p_node->connect(
        StringName(SIG_TREE_ENTERED),
        callable_mp_static(&register_derived_now).bind(Variant(p_node))
    );
}

bool is_persist_only(const Ref<NetwPropertyConfig> &p_config) {
    return p_config->get_is_persisted() && !p_config->is_policy_declared()
        && !p_config->is_transfer_declared()
        && p_config->get_quantizers().is_empty();
}

void lint_property_overlaps(const Variant &p_node) {
    Node *node = node_behind(p_node);
    if (node == nullptr) {
        return;
    }
    const Ref<Script> script = script_of(node);
    if (script.is_null()) {
        return;
    }
    const Dictionary configs
        = netw::script::model::get_property_configs(script);
    if (configs.is_empty()) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_null() || entity->get_owner() == nullptr) {
        return;
    }
    const Array properties = configs.keys();
    for (int at = 0; at < properties.size(); ++at) {
        const StringName property = properties[at];
        const Ref<NetwMemberConfig> config = configs[properties[at]];
        if (config.is_valid() && config->is_interpolation_only()) {
            continue;
        }
        const Ref<NetwPropertyConfig> declared = config;
        if (declared.is_valid()) {
            if (declared->get_in_state_set() || declared->get_in_input_set()) {
                continue;
            }
            if (is_persist_only(declared)) {
                continue;
            }
        }
        const NodePath real = entity->property_path(node, property, nullptr);
        if (!real.is_empty() && entity->governs_property(real, nullptr)) {
            NETW_WARN(
                sys::SESSION,
                "Netw.configure_property: '%s' on '%s' is already governed by "
                "a synchronizer; a variable and a synchronizer must not both "
                "write it (double authority)",
                String(property),
                String(node->get_name())
            );
        }
    }
}

void inject_property_lint(Node *p_node) {
    if (p_node->has_meta(StringName(META_LINT_INJECTED))) {
        return;
    }
    p_node->set_meta(StringName(META_LINT_INJECTED), true);
    if (p_node->is_inside_tree()) {
        lint_property_overlaps(Variant(p_node));
        return;
    }
    p_node->connect(
        StringName(SIG_TREE_ENTERED),
        callable_mp_static(&lint_property_overlaps).bind(Variant(p_node)),
        Object::CONNECT_ONE_SHOT
    );
}

void warn_late(Node *p_node, const char *p_door, const StringName &p_member) {
    if (!p_node->is_inside_tree()) {
        return;
    }
    NETW_WARN(
        sys::SESSION,
        "Netw.%s: '%s' on '%s' was configured after entering the tree; "
        "configure in _init() to prevent early packet race conditions",
        p_door,
        String(p_member),
        String(p_node->get_name())
    );
}

} // namespace

Ref<NetwMultiplayer> Netw::of(Node *p_node) {
    return NetwMultiplayer::core_of(p_node);
}

Ref<NetwConnectHandle> Netw::connection(Node *p_node) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    if (api.is_null()) {
        return Ref<NetwConnectHandle>();
    }
    return api->get_connection();
}

Ref<NetwSessionHandle> Netw::session(Node *p_node) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    if (api.is_null()) {
        return Ref<NetwSessionHandle>();
    }
    return api->get_session();
}

Ref<NetwClockHandle> Netw::clock(Node *p_node) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    if (api.is_null()) {
        return Ref<NetwClockHandle>();
    }
    return api->get_clock();
}

Variant Netw::service(Node *p_node, Object *p_type) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    return api.is_valid() ? api->service_held(p_type) : Variant();
}

void Netw::service_register(Node *p_node, Object *p_service, Object *p_type) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    NETW_ERR_COND(
        api.is_null(),
        sys::SESSION,
        "Netw.service_register: this node reaches no session, so there is "
        "nothing to register the service with. Call it from a node on the "
        "multiplayer branch."
    );
    api->service_register(p_service, p_type);
}

void Netw::service_unregister(Node *p_node, Object *p_service, Object *p_type) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    if (api.is_valid()) {
        api->service_unregister(p_service, p_type);
    }
}

Ref<NetwPromise> Netw::join(
    Node *p_node,
    const StringName &p_username,
    const Array &p_args
) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    NETW_ERR_COND_V(
        api.is_null(),
        NetwPromise::resolved(ERR_UNCONFIGURED),
        sys::SESSION,
        "Netw.join: this node reaches no session, so there is nothing to "
        "join. Call it from a node on the multiplayer branch."
    );
    if (!api->is_online()) {
        return api->session_prepare_join(p_username, p_args);
    }
    NETW_ERR_COND_V(
        String(p_username).is_empty(),
        NetwPromise::resolved(ERR_INVALID_PARAMETER),
        sys::SESSION,
        "Netw.join: username is empty."
    );
    api->session_submit_join(p_username, p_args);
    return NetwPromise::resolved(OK);
}

Ref<NetwSceneHandle> Netw::scene(Node *p_node, const StringName &p_named) {
    Node *inner = NetwMultiplayer::scene_inner_of(p_node);
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(inner);
    if (api.is_null()) {
        return Ref<NetwSceneHandle>();
    }
    if (p_named == StringName()) {
        const Ref<NetwEntity> record = NetwEntity::of(inner);
        return record.is_valid() ? record->get_scene() : Ref<NetwSceneHandle>();
    }
    const TypedArray<RID> matches = api->scene_find_all(p_named);
    NETW_ERR_COND_V(
        matches.size() > 1,
        Ref<NetwSceneHandle>(),
        sys::SCENE,
        "Netw.scene: %d live scenes are labeled %s, so the label names no "
        "one instance. Hold the handle the spawn returned instead.",
        int(matches.size()),
        String(p_named)
    );
    return matches.size() == 1 ? api->scene_handle_of(RID(matches[0]))
                               : Ref<NetwSceneHandle>();
}

Error Netw::configure_server_info(Node *p_node, const Callable &p_provider) {
    return session_decl::declare(
        p_node,
        session_decl::KIND_SERVER_INFO,
        p_provider,
        Variant(),
        "configure_server_info"
    );
}

Ref<NetwJoinConfig> Netw::configure_join(
    Node *p_node,
    const Callable &p_handler
) {
    Ref<NetwJoinConfig> config;
    if (!p_handler.is_null()) {
        const Ref<Script> declaring
            = netw::script::model::declaring_script(p_handler.get_object());
        NETW_ERR_COND_V(
            NetwMultiplayer::join_declared_arg_types(p_handler).is_empty()
                && p_handler.get_argument_count() > 1,
            Ref<NetwJoinConfig>(),
            sys::SESSION,
            "Netw.configure_join: a handler taking wire arguments must be a "
            "named method, because its parameter types are the join's wire "
            "schema and a lambda publishes none. Write 'func seat(join: "
            "ResolvedJoin, at: Vector3) -> void' and declare it by name."
        );
        config.instantiate();
        config->set_context_script(declaring);
        config->set_context_name(p_handler.get_method());
    }
    if (session_decl::declare(
            p_node,
            session_decl::KIND_JOIN,
            p_handler,
            config,
            "configure_join"
        )
        != OK) {
        return Ref<NetwJoinConfig>();
    }
    return config;
}

Error Netw::configure_auth(Node *p_node, const Callable &p_factory) {
    return session_decl::declare(
        p_node,
        session_decl::KIND_AUTH,
        p_factory,
        Variant(),
        "configure_auth"
    );
}

Ref<NetwSessionConfig> Netw::configure_session(
    Node *p_node,
    const Ref<NetwSessionConfig> &p_preset
) {
    return declare_config(p_node, session_decl::KIND_SESSION_CONFIG, p_preset);
}

Ref<NetwClockConfig> Netw::configure_clock(
    Node *p_node,
    const Ref<NetwClockConfig> &p_preset
) {
    return declare_config(p_node, session_decl::KIND_CLOCK_CONFIG, p_preset);
}

Ref<NetwLagCompensationConfig> Netw::configure_lagcomp(
    Node *p_node,
    const Ref<NetwLagCompensationConfig> &p_preset
) {
    return declare_config(p_node, session_decl::KIND_LAGCOMP_CONFIG, p_preset);
}

Ref<NetwPropertyConfig> Netw::configure_property(
    Node *p_node,
    const StringName &p_property,
    bool p_warn_late
) {
    if (p_node == nullptr) {
        NETW_ERROR(sys::SESSION, "Netw.configure_property: a node is required");
        return Ref<NetwPropertyConfig>();
    }
    if (!node_declares(p_node, p_property)) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.configure_property: '%s' does not exist on '%s'",
            String(p_property),
            String(p_node->get_name())
        );
        return Ref<NetwPropertyConfig>();
    }
    if (p_warn_late) {
        warn_late(p_node, "configure_property", p_property);
    }
    const Ref<Script> script = script_of(p_node);
    if (script.is_null()) {
        return netw::script::model::configure_node_property(p_node, p_property);
    }
    inject_property_lint(p_node);
    inject_component_registration(p_node);
    schedule_derived_registration(p_node);
    const Ref<NetwPropertyConfig> existing
        = netw::script::model::get_property_config(script, p_property);
    if (existing.is_valid()) {
        return existing;
    }
    Ref<NetwPropertyConfig> config;
    config.instantiate();
    config->set_context_script(script);
    config->set_context_name(p_property);
    config->set_context_type(1);
    config->set_context_node_ref(gd::weak_ref(p_node));
    netw::script::model::declare_property_config(script, p_property, config);
    return config;
}

Ref<NetwMemberConfig> Netw::configure_rpc(const Callable &p_callable) {
    const StringName method = p_callable.get_method();
    const Ref<Script> script = script_of(p_callable.get_object());
    if (script.is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.configure_rpc: the Callable must be bound to an object with "
            "a script"
        );
        return Ref<NetwMemberConfig>();
    }
    Node *host = Object::cast_to<Node>(p_callable.get_object());
    if (host != nullptr) {
        warn_late(host, "configure_rpc", method);
        inject_component_registration(host);
    }
    const Ref<NetwMemberConfig> existing
        = netw::script::model::get_rpc_options(script, method);
    if (existing.is_valid()) {
        return existing;
    }
    Ref<NetwMemberConfig> config;
    config.instantiate();
    config->set_context_script(script);
    config->set_context_name(method);
    config->set_context_type(0);
    config->set_is_call_local(
        netw::script::model::get_method_call_local(script, method)
    );
    netw::script::model::declare_rpc_config(script, method, config);
    return config;
}

Ref<NetwMemberConfig> Netw::configure_signal(const Signal &p_signal) {
    Node *node = Object::cast_to<Node>(p_signal.get_object());
    if (node == nullptr) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.configure_signal: the Signal must be bound to a Node"
        );
        return Ref<NetwMemberConfig>();
    }
    const StringName name = p_signal.get_name();
    const Ref<Script> script = script_of(node);
    if (script.is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.configure_signal: '%s' must have a script",
            String(node->get_name())
        );
        return Ref<NetwMemberConfig>();
    }
    warn_late(node, "configure_signal", name);
    const Ref<NetwMemberConfig> existing
        = netw::script::model::get_signal_config(script, name);
    if (existing.is_valid()) {
        return existing;
    }
    Ref<NetwMemberConfig> config;
    config.instantiate();
    config->set_is_call_local(true);
    config->set_context_script(script);
    config->set_context_name(name);
    config->set_context_type(2);
    netw::script::model::declare_signal_config(script, name, config);
    return config;
}

Ref<NetwMemberConfig> Netw::configure_spawn(const Callable &p_callable) {
    const StringName method = p_callable.get_method();
    const Ref<Script> script = script_of(p_callable.get_object());
    if (script.is_null()) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.configure_spawn: the Callable must be bound to an object "
            "with a script"
        );
        return Ref<NetwMemberConfig>();
    }
    const Ref<NetwMemberConfig> existing
        = netw::script::model::get_spawn_config(script, method);
    if (existing.is_valid()) {
        return existing;
    }
    Ref<NetwMemberConfig> config;
    config.instantiate();
    config->set_context_script(script);
    config->set_context_name(method);
    config->set_context_type(0);
    netw::script::model::declare_spawn_config(script, method, config);
    return config;
}

Ref<NetwEntity> Netw::configure_entity(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::resolve(p_node);
    if (entity.is_null() && p_node != nullptr && p_node->is_inside_tree()) {
        NETW_ERROR(
            sys::ENTITY,
            "Netw.configure_entity: '%s' is already in the tree and holds no "
            "entity, which is past the moment one can be provisioned for it. "
            "Declare from _init, or from NOTIFICATION_PARENTED for a node "
            "under an entity root.",
            String(p_node->get_name())
        );
    }
    return entity;
}

Ref<NetwDespawnConfig> Netw::configure_despawn(Node *p_node) {
    const Ref<Script> script = script_of(p_node);
    if (script.is_null()) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.configure_despawn: the node must have a script"
        );
        return Ref<NetwDespawnConfig>();
    }
    const Ref<NetwDespawnConfig> existing
        = netw::script::model::get_own_despawn_config(script);
    if (existing.is_valid()) {
        return existing;
    }
    Ref<NetwDespawnConfig> config;
    config.instantiate();
    netw::script::model::declare_despawn_config(script, config);
    return config;
}

Ref<NetwPersistenceConfig> Netw::configure_persistence(Node *p_node) {
    const Ref<Script> script = script_of(p_node);
    if (script.is_null()) {
        return netw::script::model::configure_node_persistence(p_node);
    }
    const Ref<NetwPersistenceConfig> existing
        = netw::script::model::get_own_persistence_config(script);
    if (existing.is_valid()) {
        return existing;
    }
    Ref<NetwPersistenceConfig> config;
    config.instantiate();
    netw::script::model::declare_persistence_config(script, config);
    return config;
}

Ref<NetwInterestHandle> Netw::configure_interest(Node *p_node) {
    return NetwInterestHandle::of(p_node);
}

Ref<NetwSchema> Netw::configure_schema(const StringName &p_name) {
    return NetwSchema::declare(p_name);
}

void Netw::rpc(const Callable &p_callable, const Array &p_args) {
    rpc_id(0, p_callable, p_args);
}

void Netw::rpc_id(
    int64_t p_peer_id,
    const Callable &p_callable,
    const Array &p_args
) {
    NetwMultiplayer *api = rpc_interface(p_callable);
    if (api != nullptr) {
        api->rpc_call(p_callable, p_args, p_peer_id);
    }
}

void Netw::rpc_controller(const Callable &p_callable, const Array &p_args) {
    const Ref<NetwEntity> entity
        = NetwEntity::of(Object::cast_to<Node>(p_callable.get_object()));
    const int64_t controller = entity.is_valid() ? entity->get_controller() : 0;
    if (controller > 0) {
        rpc_id(controller, p_callable, p_args);
    }
}

Ref<NetwPromise> Netw::request_id(
    int64_t p_peer_id,
    const Callable &p_callable,
    const Array &p_args
) {
    if (p_peer_id == 0) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.request_id: peer_id cannot be 0; use request_all for a "
            "broadcast"
        );
        return Ref<NetwPromise>();
    }
    NetwMultiplayer *api = rpc_interface(p_callable);
    if (api == nullptr) {
        return Ref<NetwPromise>();
    }
    return api->rpc_request_call(
        p_peer_id,
        p_callable,
        p_args,
        NetwMultiplayer::rpc_request_timeout_default()
    );
}

Ref<NetwPromise> Netw::request(
    const Callable &p_callable,
    const Array &p_args
) {
    return request_id(1, p_callable, p_args);
}

Ref<NetwGroupPromise> Netw::request_all(
    const Callable &p_callable,
    const Array &p_args
) {
    NetwMultiplayer *api = rpc_interface(p_callable);
    if (api == nullptr) {
        return Ref<NetwGroupPromise>();
    }
    return api->rpc_request_call_group(
        p_callable,
        p_args,
        NetwMultiplayer::rpc_request_timeout_default()
    );
}

Ref<NetwPromise> Netw::request_controller(
    const Callable &p_callable,
    const Array &p_args
) {
    const Ref<NetwEntity> entity
        = NetwEntity::of(Object::cast_to<Node>(p_callable.get_object()));
    const int64_t controller = entity.is_valid() ? entity->get_controller() : 0;
    if (controller > 0) {
        return request_id(controller, p_callable, p_args);
    }
    return Ref<NetwPromise>();
}

void Netw::sync_property(Node *p_node, const StringName &p_property) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    if (api == nullptr) {
        return;
    }
    ReplicationCore *plane = api->get_replication_plane();
    if (plane != nullptr) {
        plane->send_property(p_node, p_property);
    }
}

void Netw::emit_entity_signal(const Signal &p_signal, const Array &p_args) {
    Node *node = Object::cast_to<Node>(p_signal.get_object());
    if (node == nullptr) {
        return;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(node);
    if (api == nullptr) {
        return;
    }
    ReplicationCore *plane = api->get_replication_plane();
    if (plane != nullptr) {
        plane->send_signal(node, p_signal.get_name(), p_args);
    }
}

Ref<NetwChannel> Netw::channel(Node *p_node, int64_t p_channel_id) {
    return NetwChannel::of(p_node, p_channel_id);
}

Ref<NetwEntity> Netw::replicate(
    Node *p_node,
    const Ref<NetwParticipant> &p_owner
) {
    NetwMultiplayer *api = sole_session("replicate");
    if (api == nullptr) {
        return Ref<NetwEntity>();
    }
    ReplicationCore *plane = api->get_replication_plane();
    return plane != nullptr ? plane->replicate(p_node, p_owner)
                            : Ref<NetwEntity>();
}

Node *Netw::spawn(const Callable &p_fn, const Array &p_args) {
    return spawn_player(Ref<NetwParticipant>(), p_fn, p_args);
}

Node *Netw::spawn_player(
    const Ref<NetwParticipant> &p_player,
    const Callable &p_fn,
    const Array &p_args
) {
    Node *host = Object::cast_to<Node>(p_fn.get_object());
    if (host == nullptr) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.spawn: the spawn function must be a method on a Node host"
        );
        return nullptr;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(host);
    if (api == nullptr) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.spawn: no session governs the spawn function host '%s'",
            String(host->get_name())
        );
        return nullptr;
    }
    ReplicationCore *plane = api->get_replication_plane();
    return plane != nullptr ? plane->spawn(p_fn, p_args, p_player) : nullptr;
}

Error Netw::despawn(Node *p_node, const Ref<NetwDespawnOpts> &p_opts) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null()) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.despawn: '%s' is no entity of any session",
            p_node != nullptr ? String(p_node->get_name()) : String("<null>")
        );
        return ERR_DOES_NOT_EXIST;
    }
    NetwMultiplayer *api = NetwEntity::session_core_for(p_node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.despawn: no session governs '%s'",
            String(p_node->get_name())
        );
        return ERR_UNAVAILABLE;
    }
    return api->entity_despawn(entity->get_rid_handle(), p_opts);
}

Ref<NetwAction> Netw::action(const Callable &p_authority) {
    Node *host = Object::cast_to<Node>(p_authority.get_object());
    if (host == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.action: the authority must be a method on a Node host"
        );
        return Ref<NetwAction>();
    }
    NetwMultiplayer *api = NetwMultiplayer::of(host);
    if (api == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.action: no session governs the authority host '%s'",
            String(host->get_name())
        );
        return Ref<NetwAction>();
    }
    return api->lagcomp_action(p_authority);
}

Ref<DictionaryRecord> Netw::sample(
    const Ref<NetwEntity> &p_entity,
    int64_t p_tick
) {
    if (p_entity.is_null()) {
        NETW_ERROR(sys::LAGCOMP, "Netw.sample: an entity is required");
        return Ref<DictionaryRecord>();
    }
    NetwMultiplayer *api = NetwEntity::session_core_for(p_entity->get_owner());
    if (api == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.sample: no session governs the sampled entity"
        );
        return Ref<DictionaryRecord>();
    }
    return api->lagcomp_sample_of(p_entity, p_tick);
}

void Netw::rewind(
    const TypedArray<RID> &p_entities,
    int64_t p_tick,
    const Callable &p_body
) {
    Node *host = Object::cast_to<Node>(p_body.get_object());
    if (host == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.rewind: the body must be a method on a Node host"
        );
        return;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(host);
    if (api == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.rewind: no session governs the body host '%s'",
            String(host->get_name())
        );
        return;
    }
    api->lagcomp_rewind(p_entities, p_tick, p_body);
}

Ref<NetwPromise> Netw::change_scene_to_file(
    Node *p_node,
    const String &p_path,
    SceneChange p_scope
) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    if (api == nullptr) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            "a scene change requires an active session"
        );
    }
    return api->scene_change_to_file(
        p_node,
        p_path,
        NetwMultiplayer::SceneChange(p_scope)
    );
}

Ref<NetwPromise> Netw::change_scene_to_packed(
    Node *p_node,
    const Ref<PackedScene> &p_packed,
    SceneChange p_scope
) {
    if (p_packed.is_null() || String(p_packed->get_path()).is_empty()) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            "change_scene_to_packed needs a file-backed PackedScene, since a "
            "client can only request a scene by path"
        );
    }
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    if (api == nullptr) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            "a scene change requires an active session"
        );
    }
    return api->scene_change_to_packed(
        p_node,
        p_packed,
        NetwMultiplayer::SceneChange(p_scope)
    );
}

Ref<NetwPromise> Netw::reload_current_scene(Node *p_node, SceneChange p_scope) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    if (api == nullptr) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            "a scene change requires an active session"
        );
    }
    return api->scene_reload_current(
        p_node,
        NetwMultiplayer::SceneChange(p_scope)
    );
}

Error Netw::configure_scene_requests(Node *p_node, const Callable &p_handler) {
    const Error declared = session_decl::declare(
        p_node,
        session_decl::KIND_SCENE_REQUESTS,
        p_handler,
        Variant(),
        "configure_scene_requests"
    );
    if (declared == OK && p_handler.is_null()) {
        if (NetwMultiplayer *api = session_decl::installed_api(p_node)) {
            api->scene_set_request_handler(Callable());
        }
    }
    return declared;
}

Ref<NetwSceneConfig> Netw::configure_multiplayer_scene(Node *p_node) {
    NETW_ERR_COND_V(
        p_node == nullptr,
        Ref<NetwSceneConfig>(),
        sys::SCENE,
        "Netw.configure_multiplayer_scene: a scene root node is required"
    );
    const Ref<Script> script = script_of(p_node);
    NETW_ERR_COND_V(
        script.is_null(),
        Ref<NetwSceneConfig>(),
        sys::SCENE,
        "Netw.configure_multiplayer_scene: %s carries no script, so its "
        "declaration would belong to no scene",
        String(p_node->get_name())
    );

    SceneDecl decl = netw::script::model::get_scene_decl(script);
    if (!decl.declared) {
        decl.declared = true;
        decl.label = p_node->get_name();
        netw::script::model::declare_scene(script, decl);
    }

    const Ref<NetwEntity> entity = NetwEntity::resolve(p_node);
    if (entity.is_valid()) {
        entity->set_declares_scene(true);
    }

    Ref<NetwSceneConfig> config;
    config.instantiate();
    config->declare_against(p_node, script);
    return config;
}

void Netw::_bind_methods() {
    ClassDB::bind_static_method("Netw", D_METHOD("of", "node"), &Netw::of);
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("connection", "node"),
        &Netw::connection
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("session", "node"),
        &Netw::session
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("clock", "node"),
        &Netw::clock
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("service", "node", "type"),
        &Netw::service
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("service_register", "node", "service", "type"),
        &Netw::service_register,
        DEFVAL(static_cast<Object *>(nullptr))
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("service_unregister", "node", "service", "type"),
        &Netw::service_unregister,
        DEFVAL(static_cast<Object *>(nullptr))
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("join", "node", "username", "args"),
        &Netw::join,
        DEFVAL(Array())
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("scene", "node", "named"),
        &Netw::scene,
        DEFVAL(StringName())
    );

    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_server_info", "node", "provider"),
        &Netw::configure_server_info
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_join", "node", "handler"),
        &Netw::configure_join
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_auth", "node", "factory"),
        &Netw::configure_auth
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_session", "node", "preset"),
        &Netw::configure_session,
        DEFVAL(Ref<NetwSessionConfig>())
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_clock", "node", "preset"),
        &Netw::configure_clock,
        DEFVAL(Ref<NetwClockConfig>())
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_lagcomp", "node", "preset"),
        &Netw::configure_lagcomp,
        DEFVAL(Ref<NetwLagCompensationConfig>())
    );

    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_property", "node", "property", "warn_late"),
        &Netw::configure_property,
        DEFVAL(true)
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_rpc", "callable"),
        &Netw::configure_rpc
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_signal", "sig"),
        &Netw::configure_signal
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_spawn", "callable"),
        &Netw::configure_spawn
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_entity", "node"),
        &Netw::configure_entity
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_despawn", "node"),
        &Netw::configure_despawn
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_persistence", "node"),
        &Netw::configure_persistence
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_interest", "node"),
        &Netw::configure_interest
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_schema", "name"),
        &Netw::configure_schema
    );

    gd::bind_static_vararg("Netw", D_METHOD("rpc", "callable"), &Netw::rpc);
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("rpc_id", "peer_id", "callable"),
        &Netw::rpc_id
    );
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("rpc_controller", "callable"),
        &Netw::rpc_controller
    );
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("request", "callable"),
        &Netw::request
    );
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("request_id", "peer_id", "callable"),
        &Netw::request_id
    );
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("request_all", "callable"),
        &Netw::request_all
    );
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("request_controller", "callable"),
        &Netw::request_controller
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("sync_property", "node", "property"),
        &Netw::sync_property
    );
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("emit_entity_signal", "sig"),
        &Netw::emit_entity_signal
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("channel", "node", "channel_id"),
        &Netw::channel
    );

    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("replicate", "node", "owner"),
        &Netw::replicate,
        DEFVAL(Ref<NetwParticipant>())
    );
    gd::bind_static_vararg("Netw", D_METHOD("spawn", "fn"), &Netw::spawn);
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("spawn_player", "player", "fn"),
        &Netw::spawn_player
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("despawn", "node", "opts"),
        &Netw::despawn,
        DEFVAL(Ref<NetwDespawnOpts>())
    );

    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("action", "authority"),
        &Netw::action
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("sample", "entity", "tick"),
        &Netw::sample
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("rewind", "entities", "tick", "body"),
        &Netw::rewind
    );

    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("change_scene_to_file", "node", "path", "scope"),
        &Netw::change_scene_to_file,
        DEFVAL(SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("change_scene_to_packed", "node", "packed", "scope"),
        &Netw::change_scene_to_packed,
        DEFVAL(SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("reload_current_scene", "node", "scope"),
        &Netw::reload_current_scene,
        DEFVAL(SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_multiplayer_scene", "node"),
        &Netw::configure_multiplayer_scene
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_scene_requests", "node", "handler"),
        &Netw::configure_scene_requests
    );

    BIND_ENUM_CONSTANT(SCENE_CHANGE_SESSION);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_PARTICIPANT);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_SCENE);
    BIND_ENUM_CONSTANT(SCENE_ISOLATION_NONE);
    BIND_ENUM_CONSTANT(SCENE_ISOLATION_OWN_WORLD);
}

} // namespace netw
