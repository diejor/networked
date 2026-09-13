#include "godot/callable.hpp"
#include "godot/engine.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/scene_tree.hpp"
#include "godot/utility.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"
#include "netw/session_decl.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *SIG_PHYSICS_FRAME = "physics_frame";
constexpr const char *SIG_CLOCK_CONFIGURED = "clock_configured";
constexpr const char *SIG_SERVER_DISCONNECTED = "server_disconnected";
constexpr const char *SIG_CONNECTION_FAILED = "connection_failed";
constexpr const char *SIG_CONNECTED_TO_SERVER = "connected_to_server";
constexpr const char *SIG_SESSION_ENTERED = "session_entered";

constexpr double AUTO_CONFIG_SECONDS = 5.0;

bool authoring_in_editor() {
    Engine *engine = Engine::get_singleton();
    return engine != nullptr && engine->is_editor_hint();
}

} // namespace

Error NetwMultiplayer::clock_initialize(const Ref<NetwClockConfig> &p_from) {
    const int slot = int(session_decl::KIND_CLOCK_CONFIG)
        - int(session_decl::KIND_SESSION_CONFIG);
    if (config_consumption[slot].consumed) {
        NETW_WARN(
            sys::CLOCK,
            "Netw.configure_clock: 'clock configuration' on '%s' was "
            "configured after this session consumed its configuration. The "
            "running values are unchanged. Configure before startup and "
            "finish the fluent chain without awaiting.",
            String("this session")
        );
        return ERR_ALREADY_IN_USE;
    }
    NETW_ERR_COND_V(
        p_from.is_null(),
        ERR_INVALID_PARAMETER,
        sys::CLOCK,
        "the clock refused an initialization carrying no values"
    );
    config_consumption[slot].consumed = true;
    const Error configured = clock_configure(p_from);
    if (configured != OK) {
        config_consumption[slot].consumed = false;
        return configured;
    }
    clock_set_mismatch_action(
        MismatchAction(p_from->get_tickrate_mismatch_action())
    );
    connect_once(
        Signal(this, SIG_SERVER_DISCONNECTED),
        callable_mp(this, &NetwMultiplayer::clock_drop_synchronization)
    );
    connect_once(
        Signal(this, SIG_CONNECTION_FAILED),
        callable_mp(this, &NetwMultiplayer::clock_drop_synchronization)
    );
    connect_once(
        Signal(this, SIG_SESSION_ENTERED),
        callable_mp(this, &NetwMultiplayer::clock_start_handshake)
    );
    connect_once(
        Signal(this, SIG_CONNECTED_TO_SERVER),
        callable_mp(this, &NetwMultiplayer::clock_start_handshake)
    );
    clock_attach_pump();
    emit_signal(StringName(SIG_CLOCK_CONFIGURED));
    clock_start_handshake();
    return OK;
}

void NetwMultiplayer::clock_offer_fallback(
    const Ref<NetwClockConfig> &p_draft,
    Object *p_source
) {
    const ObjectID source = gd::instance_id(p_source);
    NETW_ERR_COND(
        clock_fallback.is_valid() && clock_fallback_source != source
            && gd::object_of(clock_fallback_source) != nullptr,
        sys::CLOCK,
        "two mounts export clock settings for one session, so this session "
        "has no single configuration. Keep one declaration on the session's "
        "branch."
    );
    clock_fallback = p_draft;
    clock_fallback_source = source;
    declarations_changed();
}

void NetwMultiplayer::clock_attach_pump() {
    if (clock_pump_attached) {
        return;
    }
    SceneTree *tree = gd::scene_tree();
    if (tree == nullptr || session_root() == nullptr) {
        return;
    }
    clock_pump_attached = true;
    connect_once(
        Signal(tree, SIG_PHYSICS_FRAME),
        callable_mp(this, &NetwMultiplayer::clock_on_physics_frame)
    );
}

void NetwMultiplayer::clock_detach_pump() {
    if (!clock_pump_attached) {
        return;
    }
    clock_pump_attached = false;
    SceneTree *tree = gd::scene_tree();
    if (tree == nullptr) {
        return;
    }
    disconnect_once(
        Signal(tree, SIG_PHYSICS_FRAME),
        callable_mp(this, &NetwMultiplayer::clock_on_physics_frame)
    );
}

void NetwMultiplayer::clock_on_physics_frame() {
    if (authoring_in_editor() || !clock_engine().get_configured()) {
        return;
    }
    SceneTree *tree = gd::scene_tree();
    if (tree == nullptr || tree->is_paused() || session_root() == nullptr) {
        return;
    }
    if (clock_engine().get_manual_tick()) {
        return;
    }
    const Ref<MultiplayerPeer> peer = NETW_API_VIRTUAL(get_multiplayer_peer)();
    if (peer.is_null()
        || peer->get_connection_status()
            != MultiplayerPeer::CONNECTION_CONNECTED) {
        return;
    }
    Node *root = tree->get_root();
    if (root == nullptr) {
        return;
    }
    const double delta = root->get_physics_process_delta_time();
    const bool hosting = is_host();
    if (delta > clock_engine().get_stall_threshold() && !hosting) {
        clock_request_handshake();
    }
    clock_physics_step(delta);
    clock_step_auto_config(delta);
    if (!hosting && clock_is_synchronized() && clock_consume_ping_due(delta)) {
        clock_send_ping();
    }
}

void NetwMultiplayer::clock_start_handshake() {
    if (is_host() || !clock_engine().get_configured()) {
        return;
    }
    const Ref<MultiplayerPeer> peer = NETW_API_VIRTUAL(get_multiplayer_peer)();
    const bool connected = peer.is_valid()
        && peer->get_connection_status()
            == MultiplayerPeer::CONNECTION_CONNECTED;
    if (!connected) {
        return;
    }
    const uint64_t generation = uint64_t(gd::instance_id(peer.ptr()));
    if (clock_handshake_generation == generation) {
        return;
    }
    clock_handshake_generation = generation;
    clock_request_handshake();
}

void NetwMultiplayer::clock_drop_synchronization() {
    clock_set_synchronized(false);
    clock_auto_config_left = 0.0;
}

void NetwMultiplayer::clock_auto_configure_offset() {
    NETW_ERR_COND(
        !clock_engine().get_configured(),
        sys::CLOCK,
        "measuring a display offset needs a configured clock, so this "
        "session has nothing to measure against yet"
    );
    if (is_host()) {
        return;
    }
    NETW_INFO(sys::CLOCK, "the clock opened a 5s display-offset measurement");
    clock_auto_config_left = AUTO_CONFIG_SECONDS;
    clock_auto_config_best = 0;
}

void NetwMultiplayer::clock_step_auto_config(double p_delta) {
    if (clock_auto_config_left <= 0.0) {
        return;
    }
    const int64_t seen
        = int64_t(clock_get_monitor(CLOCK_MONITOR_RECOMMENDED_DISPLAY_OFFSET));
    clock_auto_config_best
        = seen > clock_auto_config_best ? seen : clock_auto_config_best;
    clock_auto_config_left -= p_delta;
    if (clock_auto_config_left > 0.0) {
        return;
    }
    clock_auto_config_left = 0.0;
    clock_set_param(CLOCK_PARAM_DISPLAY_OFFSET, clock_auto_config_best);
    NETW_INFO(
        sys::CLOCK,
        "the clock settled its measurement at display_offset %d",
        int(clock_auto_config_best)
    );
}

void NetwMultiplayer::clock_release() {
    clock_detach_pump();
    clock_auto_config_left = 0.0;
    clock_handshake_generation = 0;
    clock_fallback = Ref<NetwClockConfig>();
}

} // namespace netw
