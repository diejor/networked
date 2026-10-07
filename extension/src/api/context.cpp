#include "netw/api/context.hpp"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/utility.hpp"
#include "godot/vararg.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/native_api.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/colors.hpp"
#include "netw/connect/transport.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/scene_core.hpp"
#include "netw/scene_decl.hpp"
#include "netw/script/model.hpp"
#include "netw/session_decl.hpp"
#include "netw/tree_hook.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *META_FIRED_PREFIX = "_netw_fired_";

Ref<Script> script_of(Object *p_object) {
    if (p_object == nullptr) {
        return Ref<Script>();
    }
    return Ref<Script>(Object::cast_to<Script>(p_object->get_script()));
}

Node *node_behind(const Variant &p_held) {
    return Object::cast_to<Node>(gd::live_object(p_held));
}

Node *handler_scope(const Callable &p_handler, const char *p_verb) {
    NETW_ERR_COND_V(
        !p_handler.is_valid(),
        nullptr,
        sys::SESSION,
        "Netw.%s needs a valid handler. Pass a method of a node inside the "
        "session's branch.",
        p_verb
    );
    Node *scope = Object::cast_to<Node>(p_handler.get_object());
    NETW_ERR_COND_V(
        scope == nullptr,
        nullptr,
        sys::SESSION,
        "Netw.%s needs a handler that is a method of a Node. Move the method "
        "onto a node inside the session's branch.",
        p_verb
    );
    return scope;
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
        "Netw.%s needs a node, and it was null. Pass a node inside the "
        "session's branch.",
        verb
    );
    const Ref<T> standing = session_decl::payload_on(p_node, p_kind);
    NETW_ERR_COND_V(
        standing.is_valid(),
        Ref<T>(),
        sys::SESSION,
        "Netw.%s was called twice on '%s'. Keep the Resource the first call "
        "returned and set its values in _init.",
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
        NETW_ERROR(
            sys::SESSION,
            "Netw.%s found no running session. Start one before calling it.",
            p_verb
        );
        return nullptr;
    }
    if (live.size() > 1) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.%s found %d running sessions and cannot choose one. Reach "
            "the one you mean with Netw.of.",
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
        NETW_ERROR(sys::SESSION, "A Netw RPC needs a method of a Node.");
        return nullptr;
    }
    if (NetwEntity::of(node).is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "'%s' is not part of a NetwEntity, so it cannot take a Netw RPC. "
            "Call Netw.configure_entity on its root.",
            String(node->get_name())
        );
        return nullptr;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::SESSION,
            "'%s' is not inside a session, so it cannot take a Netw RPC. Add "
            "it to the scene tree under a MultiplayerViewport.",
            String(node->get_name())
        );
    }
    return api;
}

bool node_declares(Node *p_node, const StringName &p_property) {
    NETW_ZONE_NC("Netw node declares", colors::SESSION);
    return gd::has_property(p_node, p_property);
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
    NETW_ZONE_NC("Netw register as component", colors::SESSION);
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
            "'%s' has networked configuration but is not part of a "
            "NetwEntity. Call Netw.configure_entity on its root.",
            String(node->get_name())
        );
    }
    setup_defer_latches(node);
}

void inject_component_registration(Node *p_node) {
    if (tree::marked(p_node, tree::MARK_COMPONENT_SEEN)) {
        return;
    }
    if (p_node->is_inside_tree()) {
        tree::mark(p_node, tree::MARK_COMPONENT_SEEN);
        register_as_component(Variant(p_node));
        return;
    }
    tree::mark(p_node, tree::MARK_COMPONENT_SEEN | tree::MARK_COMPONENT);
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
    if (tree::marked(p_node, tree::MARK_DERIVED_EXIT)) {
        return;
    }
    if (p_node->is_inside_tree()) {
        tree::mark(p_node, tree::MARK_DERIVED_EXIT);
        register_derived_now(Variant(p_node));
        return;
    }
    tree::mark(p_node, tree::MARK_DERIVED_EXIT | tree::MARK_DERIVED_ENTER);
}

bool is_persist_only(const Ref<NetwPropertyConfig> &p_config) {
    return p_config->is_persisted() && !p_config->is_policy_declared()
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
                "'%s' on '%s' is already replicated by its entity, so "
                "Netw.configure_property gives it a second writer. Declare "
                "the property only once.",
                String(property),
                String(node->get_name())
            );
        }
    }
}

void inject_property_lint(Node *p_node) {
    if (tree::marked(p_node, tree::MARK_LINT_SEEN)) {
        return;
    }
    if (p_node->is_inside_tree()) {
        tree::mark(p_node, tree::MARK_LINT_SEEN);
        lint_property_overlaps(Variant(p_node));
        return;
    }
    tree::mark(p_node, tree::MARK_LINT_SEEN | tree::MARK_LINT);
}

void warn_late(Node *p_node, const char *p_door, const StringName &p_member) {
    if (!p_node->is_inside_tree()) {
        return;
    }
    NETW_WARN(
        sys::SESSION,
        "Netw.%s configured '%s' on '%s' after it entered the tree, so "
        "early packets can miss it. Call it from _init.",
        p_door,
        String(p_member),
        String(p_node->get_name())
    );
}

} // namespace

void tree::declarations_entered(Node *p_node, uint16_t p_marks) {
    const Variant node(p_node);
    if ((p_marks & tree::MARK_LINT) != 0) {
        lint_property_overlaps(node);
    }
    if ((p_marks & tree::MARK_COMPONENT) != 0) {
        register_as_component(node);
    }
    if ((p_marks & tree::MARK_DERIVED_ENTER) != 0) {
        register_derived_now(node);
    }
}

void tree::declarations_exiting(Node *p_node) {
    unregister_derived_now(Variant(p_node));
}

Dictionary Netw::native_api() {
    return native_api_table();
}

Ref<NetwMultiplayer> Netw::of(Node *p_node) {
    return NetwMultiplayer::core_of(p_node);
}

Ref<NetwSession> Netw::session(Node *p_node) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    if (api.is_null()) {
        return Ref<NetwSession>();
    }
    return api->get_session();
}

Ref<NetwPromise> Netw::prepare_join(
    Node *p_node,
    const StringName &p_username,
    const Array &p_args
) {
    const Ref<NetwMultiplayer> api = NetwMultiplayer::core_of(p_node);
    NETW_ERR_COND_V(
        api.is_null(),
        NetwPromise::resolved(ERR_UNCONFIGURED),
        sys::SESSION,
        "Netw.prepare_join got a node that is not inside a session. Call it "
        "from a node in the scene tree under a MultiplayerViewport."
    );
    if (!api->is_online()) {
        return api->session_prepare_join(p_username, p_args);
    }
    NETW_ERR_COND_V(
        String(p_username).is_empty(),
        NetwPromise::resolved(ERR_INVALID_PARAMETER),
        sys::SESSION,
        "Netw.prepare_join needs a username, and it was empty."
    );
    api->session_submit_join(p_username, p_args);
    return NetwPromise::resolved(OK);
}

Ref<NetwScene> Netw::scene(Node *p_node) {
    Node *inner = NetwMultiplayer::scene_inner_of(p_node);
    if (NetwMultiplayer::core_of(inner).is_null()) {
        return Ref<NetwScene>();
    }
    const Ref<NetwEntity> record = NetwEntity::of(inner);
    return record.is_valid() ? record->get_scene() : Ref<NetwScene>();
}

Error Netw::configure_server_info(const Callable &p_provider) {
    Node *scope = handler_scope(p_provider, "configure_server_info");
    if (scope == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    return session_decl::declare(
        scope,
        session_decl::KIND_SERVER_INFO,
        p_provider,
        Variant(),
        "configure_server_info"
    );
}

Ref<NetwJoinConfig> Netw::configure_join(const Callable &p_handler) {
    Node *scope = handler_scope(p_handler, "configure_join");
    if (scope == nullptr) {
        return Ref<NetwJoinConfig>();
    }
    const Ref<Script> declaring
        = netw::script::model::declaring_script(p_handler.get_object());
    NETW_ERR_COND_V(
        NetwMultiplayer::join_declared_arg_types(p_handler).is_empty()
            && p_handler.get_argument_count() > 1,
        Ref<NetwJoinConfig>(),
        sys::SESSION,
        "Netw.configure_join got a lambda that takes join arguments. Use a "
        "named method with typed parameters, such as "
        "'func spawn_at(who: NetwPlayer, at: Vector3) -> void'."
    );
    Ref<NetwJoinConfig> config;
    config.instantiate();
    config->set_context_script(declaring);
    config->set_context_name(p_handler.get_method());
    if (session_decl::declare(
            scope,
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

Error Netw::configure_admission(const Callable &p_handler) {
    Node *scope = handler_scope(p_handler, "configure_admission");
    if (scope == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    return session_decl::declare(
        scope,
        session_decl::KIND_ADMISSION,
        p_handler,
        Variant(),
        "configure_admission"
    );
}

Error Netw::configure_transfer(const Callable &p_keep) {
    Node *scope = handler_scope(p_keep, "configure_transfer");
    if (scope == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    return session_decl::declare(
        scope,
        session_decl::KIND_TRANSFER,
        p_keep,
        Variant(),
        "configure_transfer"
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
    NETW_ZONE_NC("Netw configure property", colors::SESSION);
    if (p_node == nullptr) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.configure_property needs a node, and it was null."
        );
        return Ref<NetwPropertyConfig>();
    }
    if (!node_declares(p_node, p_property)) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.configure_property got '%s', which '%s' does not have. "
            "Check the property name.",
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
            "Netw.configure_rpc needs a method of an object that has a "
            "script."
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
            "Netw.configure_signal needs a signal of a Node."
        );
        return Ref<NetwMemberConfig>();
    }
    const StringName name = p_signal.get_name();
    const Ref<Script> script = script_of(node);
    if (script.is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "Netw.configure_signal needs '%s' to have a script that "
            "declares the signal.",
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
            "Netw.configure_spawn needs a method of an object that has a "
            "script."
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
    NETW_ZONE_NC("Netw configure entity", colors::SESSION);
    const Ref<NetwEntity> entity = NetwEntity::resolve(p_node);
    if (entity.is_null() && p_node != nullptr && p_node->is_inside_tree()) {
        NETW_ERROR(
            sys::ENTITY,
            "Netw.configure_entity ran after '%s' entered the tree, which is "
            "too late. Call it from _init, or from NOTIFICATION_PARENTED for "
            "a node under an entity root.",
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
            "Netw.configure_despawn needs a node that has a script."
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
    NETW_ERR_COND_V(
        p_node == nullptr,
        Ref<NetwPersistenceConfig>(),
        sys::TABLE,
        "Netw.configure_persistence needs a node, and it was null."
    );
    return netw::script::model::configure_node_persistence(p_node);
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
            "Netw.request_id got peer id 0, which names no single peer. Use "
            "Netw.request_all to ask every peer."
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
    return request_id(SERVER_PEER, p_callable, p_args);
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

Ref<NetwDatabaseConfig> Netw::configure_database(
    Node *p_node,
    const StringName &p_name,
    const Ref<NetwDatabaseConfig> &p_preset
) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    NETW_ERR_COND_V(
        api == nullptr,
        Ref<NetwDatabaseConfig>(),
        sys::TABLE,
        "Netw.configure_database got '%s', which is not inside a session. "
        "Call it from a node in the scene tree under a MultiplayerViewport.",
        p_node != nullptr ? String(p_node->get_name()) : String("a freed node")
    );
    NETW_ERR_COND_V(
        String(p_name).is_empty(),
        Ref<NetwDatabaseConfig>(),
        sys::TABLE,
        "Netw.configure_database needs a database name, and it was empty."
    );
    Ref<NetwDatabaseConfig> draft;
    draft.instantiate();
    if (p_preset.is_valid()) {
        draft->copy_values_from(**p_preset);
    }
    if (!api->database_create(p_name, draft).is_valid()) {
        return Ref<NetwDatabaseConfig>();
    }
    return draft;
}

Ref<NetwEntity> Netw::replicate(Node *p_node, const Ref<NetwPlayer> &p_owner) {
    NetwMultiplayer *api = sole_session("replicate");
    if (api == nullptr) {
        return Ref<NetwEntity>();
    }
    ReplicationCore *plane = api->get_replication_plane();
    return plane != nullptr ? plane->replicate(p_node, p_owner)
                            : Ref<NetwEntity>();
}

Node *Netw::spawn(const Callable &p_fn, const Array &p_args) {
    return spawn_player(Ref<NetwPlayer>(), p_fn, p_args);
}

Node *Netw::spawn_player(
    const Ref<NetwPlayer> &p_player,
    const Callable &p_fn,
    const Array &p_args
) {
    Node *host = Object::cast_to<Node>(p_fn.get_object());
    if (host == nullptr) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.spawn needs a spawn function that is a method of a Node."
        );
        return nullptr;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(host);
    if (api == nullptr) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.spawn got a spawn function on '%s', which is not inside a "
            "session. Move it to a node in the scene tree under a "
            "MultiplayerViewport.",
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
            "Netw.despawn got '%s', which is not a NetwEntity. Despawn the "
            "entity's root node.",
            p_node != nullptr ? String(p_node->get_name()) : String("<null>")
        );
        return ERR_DOES_NOT_EXIST;
    }
    NetwMultiplayer *api = NetwEntity::session_core_for(p_node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::SPAWN,
            "Netw.despawn got '%s', which is not inside a session.",
            String(p_node->get_name())
        );
        return ERR_UNAVAILABLE;
    }
    return api->entity_despawn(entity->get_rid_handle(), p_opts);
}

Ref<NetwPromise> Netw::reparent(Node *p_node, Node *p_new_parent) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (entity.is_null()) {
        NETW_ERROR(
            sys::SCENE,
            "Netw.reparent got '%s', which is not a NetwEntity. Reparent the "
            "entity's root node.",
            p_node != nullptr ? String(p_node->get_name()) : String("<null>")
        );
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            String("Netw.reparent: the node is no entity of any session")
        );
    }
    NetwMultiplayer *api = NetwEntity::session_core_for(p_node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::SCENE,
            "Netw.reparent got '%s', which is not inside a session.",
            String(p_node->get_name())
        );
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            String("Netw.reparent: no session governs the node")
        );
    }
    return api->entity_reparent(entity->get_rid_handle(), p_new_parent);
}

Ref<NetwAction> Netw::action(const Callable &p_authority) {
    Node *host = Object::cast_to<Node>(p_authority.get_object());
    if (host == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.action needs an authority callback that is a method of a "
            "Node."
        );
        return Ref<NetwAction>();
    }
    NetwMultiplayer *api = NetwMultiplayer::of(host);
    if (api == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.action got a callback on '%s', which is not inside a "
            "session. Move it to a node in the scene tree under a "
            "MultiplayerViewport.",
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
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.sample needs an entity, and it was null."
        );
        return Ref<DictionaryRecord>();
    }
    NetwMultiplayer *api = NetwEntity::session_core_for(p_entity->get_owner());
    if (api == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.sample got an entity that is not inside a session."
        );
        return Ref<DictionaryRecord>();
    }
    return api->lagcomp_sample_of(p_entity, p_tick);
}

void Netw::rewind(
    const TypedArray<NetwEntity> &p_entities,
    int64_t p_tick,
    const Callable &p_body
) {
    Node *host = Object::cast_to<Node>(p_body.get_object());
    if (host == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.rewind needs a body that is a method of a Node."
        );
        return;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(host);
    if (api == nullptr) {
        NETW_ERROR(
            sys::LAGCOMP,
            "Netw.rewind got a body on '%s', which is not inside a session. "
            "Move it to a node in the scene tree under a MultiplayerViewport.",
            String(host->get_name())
        );
        return;
    }
    api->lagcomp_rewind_of(p_entities, p_tick, p_body);
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

Error Netw::configure_scene_requests(const Callable &p_handler) {
    Node *scope = handler_scope(p_handler, "configure_scene_requests");
    if (scope == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    return session_decl::declare(
        scope,
        session_decl::KIND_SCENE_REQUESTS,
        p_handler,
        Variant(),
        "configure_scene_requests"
    );
}

Ref<NetwSceneConfig> Netw::configure_scene(Node *p_node) {
    NETW_ERR_COND_V(
        p_node == nullptr,
        Ref<NetwSceneConfig>(),
        sys::SCENE,
        "Netw.configure_scene needs the scene's root node, and it was null."
    );
    const Ref<Script> script = script_of(p_node);
    NETW_ERR_COND_V(
        script.is_null(),
        Ref<NetwSceneConfig>(),
        sys::SCENE,
        "Netw.configure_scene needs '%s' to have a script. "
        "Call it from the script on the scene's root.",
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
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("_native_api"),
        &Netw::native_api
    );
    ClassDB::bind_static_method("Netw", D_METHOD("of", "node"), &Netw::of);
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("session", "node"),
        &Netw::session
    );
    gd::bind_static_vararg(
        "Netw",
        D_METHOD("prepare_join", "node", "username"),
        &Netw::prepare_join
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("scene", "node"),
        &Netw::scene
    );

    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_server_info", "provider"),
        &Netw::configure_server_info
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_join", "handler"),
        &Netw::configure_join
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_admission", "handler"),
        &Netw::configure_admission
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_transfer", "keep"),
        &Netw::configure_transfer
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
        D_METHOD("configure_database", "node", "name", "preset"),
        &Netw::configure_database,
        DEFVAL(Ref<NetwDatabaseConfig>())
    );

    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("replicate", "node", "owner"),
        &Netw::replicate,
        DEFVAL(Ref<NetwPlayer>())
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
        D_METHOD("reparent", "node", "new_parent"),
        &Netw::reparent
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
        DEFVAL(enums::NetwScene::SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("change_scene_to_packed", "node", "packed", "scope"),
        &Netw::change_scene_to_packed,
        DEFVAL(enums::NetwScene::SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("reload_current_scene", "node", "scope"),
        &Netw::reload_current_scene,
        DEFVAL(enums::NetwScene::SCENE_CHANGE_SESSION)
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_scene", "node"),
        &Netw::configure_scene
    );
    ClassDB::bind_static_method(
        "Netw",
        D_METHOD("configure_scene_requests", "handler"),
        &Netw::configure_scene_requests
    );
}

} // namespace netw
