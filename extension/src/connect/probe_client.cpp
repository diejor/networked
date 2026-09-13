#include "netw/connect/probe_client.hpp"

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/time.hpp"
#include "godot/variant.hpp"
#include "netw/auth_protocol.hpp"

using namespace godot;

namespace netw::connect {

double ProbeClient::DEFAULT_TIMEOUT = 2.0;

static int64_t probe_now_msec() {
    Time *reading = Time::get_singleton();
    return reading != nullptr ? int64_t(reading->get_ticks_msec()) : 0;
}

void ProbeClient::open(
    const Ref<MultiplayerPeer> &p_peer,
    double p_timeout,
    const Ref<NetwPromise> &p_done,
    const ProbeHooks &p_hooks
) {
    if (p_peer.is_null()) {
        if (p_done.is_valid()) {
            p_done->reject(
                ERR_CANT_CONNECT,
                String("transport produced no probe peer")
            );
        }
        return;
    }

    peer = p_peer;
    done = p_done;
    hooks = p_hooks;
    outcome = Outcome();
    settled = false;
    inside_pump = false;
    remaining = p_timeout;
    opened_msec = probe_now_msec();

    api.instantiate();
    api->set_multiplayer_peer(peer);
    if (hooks.authenticating.is_valid()) {
        api->connect("peer_authenticating", hooks.authenticating);
    }
    if (hooks.connection_failed.is_valid()) {
        api->connect("connection_failed", hooks.connection_failed);
    }
    if (hooks.authentication_failed.is_valid()) {
        api->connect("peer_authentication_failed", hooks.authentication_failed);
    }
    if (hooks.auth_received.is_valid()) {
        api->set_auth_callback(hooks.auth_received);
    }
}

void ProbeClient::poll(double p_delta) {
    if (!is_open()) {
        return;
    }
    Ref<SceneMultiplayer> pumping = api;
    inside_pump = true;
    pumping->poll();
    inside_pump = false;
    if (settled) {
        hand_over();
        return;
    }
    remaining -= p_delta;
    if (remaining <= 0.0) {
        settle_error(ERR_TIMEOUT, String("probe expired"));
    }
}

bool ProbeClient::is_open() const {
    return api.is_valid() && !settled;
}

void ProbeClient::close() {
    if (!settled && done.is_valid()) {
        settle_error(ERR_SKIP, String("probe closed"));
        return;
    }
    release();
}

void ProbeClient::on_authenticating(int64_t p_peer_id) {
    if (api.is_null() || settled) {
        return;
    }
    if (p_peer_id != MultiplayerPeer::TARGET_PEER_SERVER) {
        return;
    }
    api->send_auth(int(p_peer_id), netw::auth::encode_probe_request(0));
}

void ProbeClient::on_auth_received(
    int64_t p_peer_id,
    const PackedByteArray &p_data
) {
    (void)p_peer_id;
    if (settled) {
        return;
    }
    const netw::auth::ProbeReply reply = netw::auth::decode_probe_reply(p_data);
    if (!reply.ok) {
        settle_error(ERR_INVALID_DATA, String("malformed NPRB reply"));
        return;
    }
    switch (netw::auth::ProbeStatus(reply.status)) {
        case netw::auth::ProbeStatus::OK: {
            Ref<NetwServerInfo> info
                = NetwServerInfo::from_payload(reply.payload);
            if (info.is_valid()) {
                settle_ok(info);
            } else {
                settle_error(
                    ERR_INVALID_DATA,
                    String("malformed NPRB info payload")
                );
            }
        } break;
        case netw::auth::ProbeStatus::BUSY:
            settle_error(ERR_BUSY, String("server reported BUSY"));
            break;
        case netw::auth::ProbeStatus::UNSUPPORTED:
            settle_error(ERR_UNAVAILABLE, String("server does not probe"));
            break;
        default:
            settle_error(
                ERR_INVALID_DATA,
                String("server reported an invalid status")
            );
            break;
    }
}

void ProbeClient::on_connection_failed() {
    if (settled) {
        return;
    }
    settle_error(ERR_CANT_CONNECT, String("connection failed"));
}

void ProbeClient::on_authentication_failed(int64_t p_peer_id) {
    (void)p_peer_id;
    if (settled) {
        return;
    }
    settle_error(ERR_UNAUTHORIZED, String("peer authentication failed"));
}

void ProbeClient::settle_ok(const Ref<NetwServerInfo> &p_info) {
    if (settled) {
        return;
    }
    settled = true;
    outcome.ok = true;
    outcome.info = p_info;
    if (!inside_pump) {
        hand_over();
    }
}

void ProbeClient::settle_error(int64_t p_code, const String &p_message) {
    if (settled) {
        return;
    }
    settled = true;
    outcome.ok = false;
    outcome.code = p_code;
    outcome.message = p_message;
    if (!inside_pump) {
        hand_over();
    }
}

void ProbeClient::hand_over() {
    const Ref<NetwPromise> settling = done;
    const Outcome reached = outcome;
    release();
    if (settling.is_null()) {
        return;
    }
    if (reached.ok) {
        settling->resolve(reached.info);
    } else {
        settling->reject(static_cast<Error>(reached.code), reached.message);
    }
}

void ProbeClient::unhook(const StringName &p_signal, const Callable &p_hook) {
    if (!p_hook.is_valid() || !api->is_connected(p_signal, p_hook)) {
        return;
    }
    api->disconnect(p_signal, p_hook);
}

void ProbeClient::release() {
    if (api.is_valid()) {
        unhook("peer_authenticating", hooks.authenticating);
        unhook("connection_failed", hooks.connection_failed);
        unhook("peer_authentication_failed", hooks.authentication_failed);
        api->set_auth_callback(Callable());
        if (peer.is_valid()) {
            peer->close();
        }
        api->set_multiplayer_peer(Ref<MultiplayerPeer>());
    }
    hooks = ProbeHooks();
    outcome = Outcome();
    api.unref();
    peer.unref();
    done.unref();
}

} // namespace netw::connect
