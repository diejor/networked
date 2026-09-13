#include "netw/api/nodes/multiplayer_tree.hpp"

#include "godot/class_db.hpp"
#include "godot/display_server.hpp"
#include "godot/engine.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/random.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/context.hpp"
#include "netw/api/participant.hpp"
#include "netw/log.hpp"
#include "netw/session_core.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_PEER_CONNECTED = "peer_connected";
const char *SIG_PEER_DISCONNECTED = "peer_disconnected";
const char *SIG_CONNECTED_TO_SERVER = "connected_to_server";
const char *SIG_SERVER_DISCONNECTED = "server_disconnected";
const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_ENDED = "session_ended";

const char *EMBEDDED_SERVER_NAME = "Server";

bool in_editor() {
    Engine *engine = Engine::get_singleton();
    return engine != nullptr && engine->is_editor_hint();
}

bool is_scene_infrastructure(Node *p_node) {
    return p_node->is_class("ParticipantView") || p_node->is_class("TPLayerAPI")
        || p_node->is_class("LobbyDirectory");
}

} // namespace

MultiplayerTree::MultiplayerTree() {
    if (!in_editor()) {
        api = make_api();
    }
}

int64_t MultiplayerTree::app_tag_of(const StringName &p_app_id) {
    return SessionCore::compute_app_tag(p_app_id);
}

StringName MultiplayerTree::random_app_id() {
    Ref<RandomNumberGenerator> dice;
    dice.instantiate();
    dice->randomize();
    const String glyphs = "abcdefghijklmnopqrstuvwxyz0123456789";
    String out;
    for (int at = 0; at < 15; ++at) {
        out += glyphs[dice->randi_range(0, glyphs.length() - 1)];
    }
    return StringName(out);
}

Ref<NetwMultiplayer> MultiplayerTree::make_api() const {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    return NetwMultiplayer::make(inner, api_script);
}

void MultiplayerTree::set_peer_class(const StringName &p_value) {
    peer_class = p_value;
    update_configuration_warnings();
}

void MultiplayerTree::set_link_conditions(
    const Ref<NetwLinkConditions> &p_value
) {
    if (session_settings_are_immutable("link_conditions")) {
        return;
    }
    link_conditions = p_value;
    offer_session_fallback();
}

void MultiplayerTree::set_desired_role(NetwMultiplayer::Role p_value) {
    if (session_settings_are_immutable("desired_role")) {
        return;
    }
    desired_role = p_value;
    update_configuration_warnings();
    offer_session_fallback();
}

void MultiplayerTree::set_app_id(const StringName &p_value) {
    if (session_settings_are_immutable("app_id")) {
        return;
    }
    app_id = p_value;
    offer_session_fallback();
}

void MultiplayerTree::set_api_script(const Ref<Script> &p_value) {
    NETW_ERR_COND(
        is_inside_tree(),
        sys::SESSION,
        "api_script is immutable after tree entry"
    );
    if (api_script == p_value) {
        return;
    }
    api_script = p_value;
    if (!in_editor() && api.is_valid()) {
        api->embed_dispose();
        api = make_api();
    }
}

void MultiplayerTree::generate_app_id() {
    set_app_id(random_app_id());
}

Ref<NetwSessionConfig> MultiplayerTree::build_session_config() const {
    Ref<NetwSessionConfig> config;
    config.instantiate();
    config->set_app_id(app_id);
    config->set_desired_role(int64_t(desired_role));
    config->set_link_conditions(link_conditions);
    return config;
}

bool MultiplayerTree::session_settings_are_immutable(
    const char *p_field
) const {
    if (api.is_null()
        || !api->config_is_consumed(session_decl::KIND_SESSION_CONFIG)) {
        return false;
    }
    NETW_WARN(
        sys::SESSION,
        "Netw.configure_session: '%s' on '%s' was configured after this "
        "session consumed its configuration. The running values are "
        "unchanged. Configure before startup and finish the fluent chain "
        "without awaiting.",
        p_field,
        String(get_name())
    );
    return true;
}

void MultiplayerTree::offer_session_fallback() {
    if (api.is_valid()) {
        api->session_offer_fallback(build_session_config(), this);
    }
}

void MultiplayerTree::dispose() {
    if (api.is_null()) {
        return;
    }
    api->service_clear();
    const Ref<NetwParticipant> local = api->participant_local();
    if (local.is_valid()) {
        api->participant_seat_move(local->get_peer_id(), RID());
    }
    api->clear_roster();
}

Node *MultiplayerTree::find_bare_level() const {
    Node *only_candidate = nullptr;
    Node *only_gameplay = nullptr;
    int candidates = 0;
    int gameplay = 0;

    for (int at = 0; at < get_child_count(); ++at) {
        Node *child = get_child(at);
        if (child->get_scene_file_path().is_empty()
            || is_scene_infrastructure(child)) {
            continue;
        }
        candidates += 1;
        only_candidate = child;
        if (!child->find_children("*", "MultiplayerSpawner", true, false)
                 .is_empty()) {
            gameplay += 1;
            only_gameplay = child;
        }
    }
    if (gameplay == 1) {
        return only_gameplay;
    }
    return candidates == 1 ? only_candidate : nullptr;
}

void MultiplayerTree::enter_tree() {
    if (in_editor()) {
        return;
    }
    mount_api();
    api->embed_offer_bare_level(find_bare_level());
    callable_mp(this, &MultiplayerTree::settle_embedding).call_deferred();
}

void MultiplayerTree::settle_embedding() {
    if (api.is_valid()) {
        api->embed_settle();
    }
}

void MultiplayerTree::mount_api() {
    if (api.is_null()) {
        return;
    }
    const NodePath root_path = get_path();
    api->session_get_inner()->set_root_path(root_path);
    get_tree()->set_multiplayer(api, root_path);
    const Ref<MultiplayerAPI> mounted = get_tree()->get_multiplayer(root_path);
    NETW_ASSERT(
        mounted == api,
        sys::SESSION,
        "mounting is the only installation point on this branch"
    );
    bind_api_signals();
    offer_session_fallback();
    if (construction_role != NetwMultiplayer::ROLE_NONE) {
        api->session_constrain_role(construction_role);
    }
}

void MultiplayerTree::unmount_api() {
    if (api.is_null()) {
        return;
    }
    unbind_api_signals();
    const NodePath mounted = api->session_get_inner()->get_root_path();
    if (!mounted.is_empty()) {
        get_tree()->set_multiplayer(Ref<MultiplayerAPI>(), mounted);
    }
}

void MultiplayerTree::bind_api_signals() {
    if (api.is_null()) {
        return;
    }
    struct Wiring {
        const char *signal;
        Callable target;
    };
    const Wiring wiring[] = {
        {SIG_PEER_CONNECTED,
         callable_mp(this, &MultiplayerTree::on_peer_connected)},
        {SIG_PEER_DISCONNECTED,
         callable_mp(this, &MultiplayerTree::on_peer_disconnected)},
        {SIG_CONNECTED_TO_SERVER,
         callable_mp(this, &MultiplayerTree::on_connected_to_server)},
        {SIG_SERVER_DISCONNECTED,
         callable_mp(this, &MultiplayerTree::on_server_disconnected)},
        {SIG_SESSION_ENTERED,
         callable_mp(this, &MultiplayerTree::finalize_session)},
        {SIG_SESSION_ENDED,
         callable_mp(this, &MultiplayerTree::teardown_session)},
    };
    for (const Wiring &row : wiring) {
        const StringName name(row.signal);
        if (!api->is_connected(name, row.target)) {
            api->connect(name, row.target);
        }
    }
}

void MultiplayerTree::unbind_api_signals() {
    if (api.is_null()) {
        return;
    }
    const Callable targets[] = {
        callable_mp(this, &MultiplayerTree::on_peer_connected),
        callable_mp(this, &MultiplayerTree::on_peer_disconnected),
        callable_mp(this, &MultiplayerTree::on_connected_to_server),
        callable_mp(this, &MultiplayerTree::on_server_disconnected),
        callable_mp(this, &MultiplayerTree::finalize_session),
        callable_mp(this, &MultiplayerTree::teardown_session),
    };
    const char *names[]
        = {SIG_PEER_CONNECTED,
           SIG_PEER_DISCONNECTED,
           SIG_CONNECTED_TO_SERVER,
           SIG_SERVER_DISCONNECTED,
           SIG_SESSION_ENTERED,
           SIG_SESSION_ENDED};
    for (int at = 0; at < 6; ++at) {
        const StringName name(names[at]);
        if (api->is_connected(name, targets[at])) {
            api->disconnect(name, targets[at]);
        }
    }
}

void MultiplayerTree::poll() {
    if (in_editor()) {
        return;
    }
    if (api.is_valid()) {
        api->poll();
    }
}

void MultiplayerTree::on_peer_created(
    const Ref<MultiplayerPeer> &p_peer,
    int64_t p_error,
    const String &p_detail,
    const StringName &p_username,
    const Array &p_join_args
) {
    if (p_error != OK || p_peer.is_null()) {
        NETW_ERROR(
            sys::SESSION,
            "%s answered no peer (%s)",
            String(peer_class),
            p_detail
        );
        return;
    }
    if (!String(p_username).is_empty()) {
        api->session_prepare_join(p_username, p_join_args);
    }
    api->set_multiplayer_peer(p_peer);
}

void MultiplayerTree::bring_up_here(
    NetwMultiplayer::TransportMode p_mode,
    const String &p_address,
    const StringName &p_username,
    const Array &p_join_args
) {
    const Ref<NetwConnectHandle> connection = Netw::connection(this);
    if (connection.is_null()) {
        return;
    }
    const RID ticket = connection->create_peer(
        peer_class,
        int64_t(p_mode),
        p_address,
        transport_settings,
        callable_mp(this, &MultiplayerTree::on_peer_created)
            .bind(p_username, p_join_args),
        Callable()
    );
    NETW_ERR_COND(
        !ticket.is_valid(),
        sys::SESSION,
        netw::log::format("no transport answers '%s'", String(peer_class))
    );
}

void MultiplayerTree::ready() {
    if (in_editor()) {
        return;
    }
    callable_mp(this, &MultiplayerTree::decide_bring_up).call_deferred();
}

void MultiplayerTree::decide_bring_up() {
    if (api.is_null() || !is_inside_tree()) {
        return;
    }
    const NetwMultiplayer::Role effective_role
        = api->session_get_authored_role();
    DisplayServer *display = DisplayServer::get_singleton();
    const bool headless
        = display != nullptr && display->get_name() == String("headless");

    if (auto_host_headless && !String(peer_class).is_empty() && headless) {
        if (effective_role == NetwMultiplayer::ROLE_LISTEN_SERVER
            || effective_role == NetwMultiplayer::ROLE_DEDICATED_SERVER) {
            bring_up_here(
                NetwMultiplayer::TRANSPORT_MODE_HOST,
                String(),
                StringName(),
                Array()
            );
        }
        return;
    }

    OS *os = OS::get_singleton();
    const bool debug_build = os != nullptr && os->has_feature("debug");
    if (debug_join.is_valid() && !String(peer_class).is_empty()
        && effective_role != NetwMultiplayer::ROLE_DEDICATED_SERVER
        && debug_build) {
        debug_autoconnect();
    }
}

void MultiplayerTree::debug_autoconnect() {
    if (api->session_get_state() != NetwMultiplayer::SESSION_STATE_OFFLINE) {
        return;
    }
    NETW_INFO(
        sys::SESSION,
        "debug auto-connect as '%s'",
        String(debug_join->get_username())
    );
    bring_up_here(
        NetwMultiplayer::TRANSPORT_MODE_HOST,
        String(),
        debug_join->get_username(),
        debug_join->get_join_args()
    );
}

MultiplayerTree *MultiplayerTree::raise_embedded_server() {
    Node *parent = get_parent();
    if (parent == nullptr) {
        return nullptr;
    }
    MultiplayerTree *server = Object::cast_to<MultiplayerTree>(duplicate());
    if (server == nullptr) {
        return nullptr;
    }
    server->set_desired_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    server->construction_role = NetwMultiplayer::ROLE_DEDICATED_SERVER;
    server->set_name(EMBEDDED_SERVER_NAME);
    server->set_auto_host_headless(false);
    parent->add_child(server);
    return server;
}

void MultiplayerTree::finalize_session() {
    NETW_TRACE(sys::SESSION, "finalizing session");
    NETW_DEBUG(
        sys::SESSION,
        "session app_id='%s' app_tag=%s",
        String(app_id),
        String::num_uint64(app_tag_of(app_id), 16)
    );
}

void MultiplayerTree::teardown_session() {
    if (api.is_valid()) {
        api->clear_roster();
        api->session_set_role(NetwMultiplayer::ROLE_NONE);
    }
    Node *parent = get_parent();
    if (parent == nullptr) {
        return;
    }
    MultiplayerTree *server = Object::cast_to<MultiplayerTree>(
        parent->get_node_or_null(NodePath(EMBEDDED_SERVER_NAME))
    );
    if (server != nullptr && server != this) {
        server->queue_free();
    }
}

void MultiplayerTree::release_peer() {
    if (api.is_valid() && api->has_multiplayer_peer()) {
        api->get_multiplayer_peer()->close();
        api->set_multiplayer_peer(Ref<MultiplayerPeer>());
    }
}

void MultiplayerTree::exiting() {
    NETW_TRACE(sys::SESSION, "exiting");
    if (!is_queued_for_deletion()) {
        unmount_api();
        return;
    }
    release_peer();
    unmount_api();
    dispose();
    deletion_finalized = true;
}

void MultiplayerTree::close_peer_on_delete() {
    if (deletion_finalized || in_editor() || is_inside_tree()) {
        return;
    }
    deletion_finalized = true;
    release_peer();
    dispose();
}

void MultiplayerTree::on_peer_connected(int64_t p_peer_id) {
    NETW_INFO(sys::SESSION, "peer connected: %d", p_peer_id);
}

void MultiplayerTree::on_peer_disconnected(int64_t p_peer_id) {
    NETW_INFO(sys::SESSION, "peer disconnected: %d", p_peer_id);
    if (api.is_null()) {
        return;
    }
    const Ref<NetwParticipant> participant
        = api->peer_get_participant(p_peer_id);
    if (participant.is_valid()) {
        api->participant_seat_move(participant->get_peer_id(), RID());
    }
    api->peer_forget(p_peer_id);
}

void MultiplayerTree::on_connected_to_server() {
    const int64_t peer_id = api->get_multiplayer_peer()->get_unique_id();
    NETW_INFO(sys::SESSION, "connected to server as peer %d", peer_id);
    set_multiplayer_authority(int(peer_id), false);
}

void MultiplayerTree::on_server_disconnected() {
    NETW_INFO(sys::SESSION, "disconnected from server");
}

void MultiplayerTree::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE:
            enter_tree();
            break;
        case NOTIFICATION_READY:
            set_process(true);
            ready();
            break;
        case NOTIFICATION_PROCESS:
            poll();
            break;
        case NOTIFICATION_EXIT_TREE:
            exiting();
            break;
        case NOTIFICATION_PREDELETE:
            close_peer_on_delete();
            if (api.is_valid()) {
                api->embed_dispose();
            }
            break;
        case NOTIFICATION_WM_CLOSE_REQUEST:
            if (!in_editor() && api.is_valid()) {
                api->persist_shutdown();
            }
            break;
        default:
            break;
    }
}

void MultiplayerTree::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_peer_class", "value"),
        &MultiplayerTree::set_peer_class
    );
    ClassDB::bind_method(
        D_METHOD("get_peer_class"),
        &MultiplayerTree::get_peer_class
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "peer_class"),
        "set_peer_class",
        "get_peer_class"
    );

    ClassDB::bind_method(
        D_METHOD("set_transport_settings", "value"),
        &MultiplayerTree::set_transport_settings
    );
    ClassDB::bind_method(
        D_METHOD("get_transport_settings"),
        &MultiplayerTree::get_transport_settings
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "transport_settings"),
        "set_transport_settings",
        "get_transport_settings"
    );

    ClassDB::bind_method(
        D_METHOD("set_link_conditions", "value"),
        &MultiplayerTree::set_link_conditions
    );
    ClassDB::bind_method(
        D_METHOD("get_link_conditions"),
        &MultiplayerTree::get_link_conditions
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "link_conditions",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwLinkConditions"
        ),
        "set_link_conditions",
        "get_link_conditions"
    );

    ClassDB::bind_method(
        D_METHOD("set_auto_host_headless", "value"),
        &MultiplayerTree::set_auto_host_headless
    );
    ClassDB::bind_method(
        D_METHOD("get_auto_host_headless"),
        &MultiplayerTree::get_auto_host_headless
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "auto_host_headless"),
        "set_auto_host_headless",
        "get_auto_host_headless"
    );

    ClassDB::bind_method(
        D_METHOD("set_desired_role", "value"),
        &MultiplayerTree::set_desired_role
    );
    ClassDB::bind_method(
        D_METHOD("get_desired_role"),
        &MultiplayerTree::get_desired_role
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "desired_role",
            PROPERTY_HINT_ENUM,
            "None,Dedicated Server,Listen Server,Client"
        ),
        "set_desired_role",
        "get_desired_role"
    );

    ClassDB::bind_method(
        D_METHOD("set_app_id", "value"),
        &MultiplayerTree::set_app_id
    );
    ClassDB::bind_method(D_METHOD("get_app_id"), &MultiplayerTree::get_app_id);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "app_id"),
        "set_app_id",
        "get_app_id"
    );

    ClassDB::bind_method(
        D_METHOD("set_debug_join", "value"),
        &MultiplayerTree::set_debug_join
    );
    ClassDB::bind_method(
        D_METHOD("get_debug_join"),
        &MultiplayerTree::get_debug_join
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "debug_join",
            PROPERTY_HINT_RESOURCE_TYPE,
            "DebugJoinConfig"
        ),
        "set_debug_join",
        "get_debug_join"
    );

    ClassDB::bind_method(
        D_METHOD("set_api_script", "value"),
        &MultiplayerTree::set_api_script
    );
    ClassDB::bind_method(
        D_METHOD("get_api_script"),
        &MultiplayerTree::get_api_script
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "api_script",
            PROPERTY_HINT_RESOURCE_TYPE,
            "Script"
        ),
        "set_api_script",
        "get_api_script"
    );

    ClassDB::bind_method(D_METHOD("get_api"), &MultiplayerTree::get_api);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "api",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwMultiplayer",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_api"
    );

    ClassDB::bind_method(D_METHOD("dispose"), &MultiplayerTree::dispose);
    ClassDB::bind_method(
        D_METHOD("raise_embedded_server"),
        &MultiplayerTree::raise_embedded_server
    );
}

} // namespace netw
