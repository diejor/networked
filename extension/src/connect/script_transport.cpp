#include "netw/connect/script_transport.hpp"

using namespace godot;

namespace netw::connect {

ScriptTransport::ScriptTransport(const Ref<NetwTransport> &p_seam)
    : seam(p_seam) {
}

void ScriptTransport::bind_session(NetwMultiplayer *p_session) {
    Transport::bind_session(p_session);
    if (seam.is_valid()) {
        seam->bind_session(p_session);
    }
}

StringName ScriptTransport::peer_class() const {
    if (seam.is_null()) {
        return StringName();
    }
    return const_cast<NetwTransport *>(seam.ptr())->peer_class();
}

bool ScriptTransport::recognizes_peer(
    const Ref<MultiplayerPeer> &p_peer
) const {
    if (seam.is_null()) {
        return Transport::recognizes_peer(p_peer);
    }
    return const_cast<NetwTransport *>(seam.ptr())->recognizes_peer(p_peer);
}

String ScriptTransport::display_name() const {
    if (seam.is_null()) {
        return String();
    }
    return const_cast<NetwTransport *>(seam.ptr())->display_name();
}

bool ScriptTransport::is_available() const {
    if (seam.is_null()) {
        return true;
    }
    return const_cast<NetwTransport *>(seam.ptr())->is_available();
}

bool ScriptTransport::can_host_here() const {
    if (seam.is_null()) {
        return true;
    }
    return const_cast<NetwTransport *>(seam.ptr())->can_host_here();
}

bool ScriptTransport::can_probe() const {
    if (seam.is_null()) {
        return false;
    }
    return const_cast<NetwTransport *>(seam.ptr())->can_probe();
}

String ScriptTransport::address_label() const {
    if (seam.is_null()) {
        return Transport::address_label();
    }
    return const_cast<NetwTransport *>(seam.ptr())->address_label();
}

String ScriptTransport::address_placeholder() const {
    if (seam.is_null()) {
        return Transport::address_placeholder();
    }
    return const_cast<NetwTransport *>(seam.ptr())->address_placeholder();
}

String ScriptTransport::address_help() const {
    if (seam.is_null()) {
        return Transport::address_help();
    }
    return const_cast<NetwTransport *>(seam.ptr())->address_help();
}

bool ScriptTransport::accepts_empty_address() const {
    if (seam.is_null()) {
        return Transport::accepts_empty_address();
    }
    return const_cast<NetwTransport *>(seam.ptr())->accepts_empty_address();
}

Dictionary ScriptTransport::host_settings() const {
    if (seam.is_null()) {
        return Transport::host_settings();
    }
    return const_cast<NetwTransport *>(seam.ptr())->host_settings();
}

Dictionary ScriptTransport::client_settings() const {
    if (seam.is_null()) {
        return Transport::client_settings();
    }
    return const_cast<NetwTransport *>(seam.ptr())->client_settings();
}

void ScriptTransport::make_peer(
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    if (seam.is_null()) {
        fail(ERR_UNCONFIGURED, "this transport cannot create a peer");
        return;
    }
    seam->arm(armed_ticket, outcome);
    seam->make_peer(armed_ticket, p_mode, p_address, p_settings);
}

void ScriptTransport::cancel_peer_creation() {
    if (seam.is_null()) {
        return;
    }
    seam->cancel_peer_creation(armed_ticket);
}

void ScriptTransport::probe(const String &p_address) {
    if (seam.is_null()) {
        Transport::probe(p_address);
        return;
    }
    seam->arm(armed_ticket, outcome);
    seam->probe(armed_ticket, p_address);
}

Ref<MultiplayerPeer> ScriptTransport::make_probe_peer(const String &p_address) {
    if (seam.is_null()) {
        return Transport::make_probe_peer(p_address);
    }
    return seam->make_probe_peer(p_address);
}

bool ScriptTransport::can_browse() const {
    if (seam.is_null()) {
        return false;
    }
    return const_cast<NetwTransport *>(seam.ptr())->can_browse();
}

void ScriptTransport::browse() {
    if (seam.is_null()) {
        return;
    }
    seam->browse();
}

void ScriptTransport::adopt(const Ref<MultiplayerPeer> &p_peer) {
    if (seam.is_null()) {
        return;
    }
    seam->adopt(p_peer);
}

void ScriptTransport::poll(double p_delta) {
    if (seam.is_null()) {
        return;
    }
    seam->poll(p_delta);
}

String ScriptTransport::join_address() const {
    if (seam.is_null()) {
        return String();
    }
    return const_cast<NetwTransport *>(seam.ptr())->join_address();
}

Dictionary ScriptTransport::diagnostics(int64_t p_peer_id) const {
    if (seam.is_null()) {
        return Transport::diagnostics(p_peer_id);
    }
    return const_cast<NetwTransport *>(seam.ptr())->diagnostics(p_peer_id);
}

double ScriptTransport::timeout_hint() const {
    if (seam.is_null()) {
        return Transport::timeout_hint();
    }
    return const_cast<NetwTransport *>(seam.ptr())->timeout_hint();
}

void ScriptTransport::close() {
    if (seam.is_null()) {
        return;
    }
    seam->close();
}

} // namespace netw::connect
