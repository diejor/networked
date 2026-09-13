#include "netw/api/webrtc_signaler.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwWebRTCSignaler::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("receive", "from_peer", "from_address", "kind", "payload"),
        &NetwWebRTCSignaler::receive
    );
    ClassDB::bind_method(
        D_METHOD("report_ready"),
        &NetwWebRTCSignaler::report_ready
    );
    ClassDB::bind_method(
        D_METHOD("report_lost"),
        &NetwWebRTCSignaler::report_lost
    );
    ClassDB::bind_method(
        D_METHOD("report_unreachable"),
        &NetwWebRTCSignaler::report_unreachable
    );
    ClassDB::bind_method(D_METHOD("room_id"), &NetwWebRTCSignaler::room_id);
    ClassDB::bind_method(
        D_METHOD("local_signaler_id"),
        &NetwWebRTCSignaler::local_signaler_id
    );

    GDVIRTUAL_BIND(_open, "room_id", "local_peer_id");
    GDVIRTUAL_BIND(_poll, "delta");
    GDVIRTUAL_BIND(_send, "to_peer_id", "to_address", "kind", "payload");
    GDVIRTUAL_BIND(_room_id);
    GDVIRTUAL_BIND(_local_signaler_id);
    GDVIRTUAL_BIND(_on_session_connected, "peer_id");
    GDVIRTUAL_BIND(_close);
}

void NetwWebRTCSignaler::receive(
    int64_t p_from_peer,
    const String &p_from_address,
    const String &p_kind,
    const Dictionary &p_payload
) {
    connect::SignalerEvent one;
    one.kind = connect::SignalerEvent::RECEIVED;
    one.from_peer = p_from_peer;
    one.from_address = p_from_address;
    one.kind_text = p_kind;
    one.payload = p_payload;
    pending.push_back(one);
}

void NetwWebRTCSignaler::report_ready() {
    connect::SignalerEvent one;
    one.kind = connect::SignalerEvent::READY;
    pending.push_back(one);
}

void NetwWebRTCSignaler::report_lost() {
    connect::SignalerEvent one;
    one.kind = connect::SignalerEvent::LOST;
    pending.push_back(one);
}

void NetwWebRTCSignaler::report_unreachable() {
    connect::SignalerEvent one;
    one.kind = connect::SignalerEvent::UNREACHABLE;
    pending.push_back(one);
}

void NetwWebRTCSignaler::take(LocalVector<connect::SignalerEvent> &r_out) {
    for (int64_t at = 0; at < int64_t(pending.size()); at++) {
        r_out.push_back(pending[at]);
    }
    pending.clear();
}

Error NetwWebRTCSignaler::open(const String &p_room, int64_t p_local_peer) {
    Error answered = ERR_UNCONFIGURED;
    if (GDVIRTUAL_CALL(_open, p_room, p_local_peer, answered)) {
        return answered;
    }
    return ERR_UNCONFIGURED;
}

void NetwWebRTCSignaler::poll(double p_delta) {
    GDVIRTUAL_CALL(_poll, p_delta);
}

void NetwWebRTCSignaler::send(
    int64_t p_to_peer,
    const String &p_to_address,
    const String &p_kind,
    const Dictionary &p_payload
) {
    GDVIRTUAL_CALL(_send, p_to_peer, p_to_address, p_kind, p_payload);
}

String NetwWebRTCSignaler::room_id() {
    String answered;
    if (GDVIRTUAL_CALL(_room_id, answered)) {
        return answered;
    }
    return String();
}

String NetwWebRTCSignaler::local_signaler_id() {
    String answered;
    if (GDVIRTUAL_CALL(_local_signaler_id, answered)) {
        return answered;
    }
    return local_signaler_id_default();
}

String NetwWebRTCSignaler::local_signaler_id_default() {
    return String();
}

void NetwWebRTCSignaler::session_connected(int64_t p_peer) {
    if (GDVIRTUAL_CALL(_on_session_connected, p_peer)) {
        return;
    }
    session_connected_default(p_peer);
}

void NetwWebRTCSignaler::session_connected_default(int64_t p_peer) {
    (void)p_peer;
}

void NetwWebRTCSignaler::close() {
    GDVIRTUAL_CALL(_close);
}

} // namespace netw

namespace netw::connect {

ScriptSignaler::ScriptSignaler(const Ref<NetwWebRTCSignaler> &p_seam)
    : seam(p_seam) {
}

Error ScriptSignaler::open(const String &p_room, int64_t p_local_peer) {
    if (seam.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return seam->open(p_room, p_local_peer);
}

void ScriptSignaler::poll(double p_delta, int64_t p_now_usec, int64_t p_frame) {
    (void)p_now_usec;
    (void)p_frame;
    if (seam.is_null()) {
        return;
    }
    seam->poll(p_delta);
    LocalVector<SignalerEvent> taken;
    seam->take(taken);
    for (int64_t at = 0; at < int64_t(taken.size()); at++) {
        publish(taken[at]);
    }
}

void ScriptSignaler::send(
    int64_t p_to_peer,
    const String &p_to_address,
    const String &p_kind,
    const Dictionary &p_payload
) {
    if (seam.is_valid()) {
        seam->send(p_to_peer, p_to_address, p_kind, p_payload);
    }
}

String ScriptSignaler::room_id() const {
    if (seam.is_null()) {
        return String();
    }
    return const_cast<NetwWebRTCSignaler *>(seam.ptr())->room_id();
}

String ScriptSignaler::local_address() const {
    if (seam.is_null()) {
        return String();
    }
    return const_cast<NetwWebRTCSignaler *>(seam.ptr())->local_signaler_id();
}

void ScriptSignaler::on_session_connected(int64_t p_peer) {
    if (seam.is_valid()) {
        seam->session_connected(p_peer);
    }
}

void ScriptSignaler::close() {
    if (seam.is_valid()) {
        seam->close();
    }
}

} // namespace netw::connect
