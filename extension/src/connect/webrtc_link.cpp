#include "netw/connect/webrtc_link.hpp"

#include "godot/callable.hpp"
#include "godot/time.hpp"
#include "netw/connect/webrtc_session.hpp"

using namespace godot;

namespace netw::connect {

WebRTCPeerConnection::ConnectionState LinkBody::state() const {
    if (inner.is_null()) {
        return WebRTCPeerConnection::STATE_CLOSED;
    }
    const WebRTCPeerConnection::ConnectionState held
        = inner->get_connection_state();
    if (masking && held == WebRTCPeerConnection::STATE_DISCONNECTED) {
        return WebRTCPeerConnection::STATE_CONNECTED;
    }
    return held;
}

WebRTCPeerConnection::GatheringState LinkBody::gathering() const {
    if (inner.is_null()) {
        return WebRTCPeerConnection::GATHERING_STATE_NEW;
    }
    return inner->get_gathering_state();
}

WebRTCPeerConnection::SignalingState LinkBody::signaling() const {
    if (inner.is_null()) {
        return WebRTCPeerConnection::SIGNALING_STATE_CLOSED;
    }
    return inner->get_signaling_state();
}

Error LinkBody::initialize(const Dictionary &p_config) {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->initialize(p_config);
}

Ref<WebRTCDataChannel> LinkBody::open_channel(
    const String &p_label,
    const Dictionary &p_config
) {
    if (inner.is_null()) {
        return Ref<WebRTCDataChannel>();
    }
    return inner->create_data_channel(p_label, p_config);
}

Error LinkBody::create_offer() {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->create_offer();
}

Error LinkBody::set_remote(const String &p_type, const String &p_sdp) {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->set_remote_description(p_type, p_sdp);
}

Error LinkBody::set_local(const String &p_type, const String &p_sdp) {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->set_local_description(p_type, p_sdp);
}

Error LinkBody::add_candidate(
    const String &p_media,
    int64_t p_index,
    const String &p_name
) {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->add_ice_candidate(p_media, int(p_index), p_name);
}

Error LinkBody::poll() {
    if (inner.is_null()) {
        return ERR_UNCONFIGURED;
    }
    return inner->poll();
}

void LinkBody::listen(WebRTCLink *p_owner) {
    if (inner.is_null() || p_owner == nullptr) {
        return;
    }
    inner->connect(
        StringName("session_description_created"),
        callable_mp(p_owner, &WebRTCLink::on_description)
    );
    inner->connect(
        StringName("ice_candidate_created"),
        callable_mp(p_owner, &WebRTCLink::on_candidate)
    );
}

void LinkBody::shut(WebRTCLink *p_owner) {
    if (inner.is_null()) {
        return;
    }
    if (p_owner != nullptr) {
        const Callable on_desc
            = callable_mp(p_owner, &WebRTCLink::on_description);
        if (inner->is_connected(
                StringName("session_description_created"),
                on_desc
            )) {
            inner->disconnect(
                StringName("session_description_created"),
                on_desc
            );
        }
        const Callable on_ice = callable_mp(p_owner, &WebRTCLink::on_candidate);
        if (inner->is_connected(StringName("ice_candidate_created"), on_ice)) {
            inner->disconnect(StringName("ice_candidate_created"), on_ice);
        }
    }
    inner->close();
}

WebRTCLink::WebRTCLink() {
    body.inner = gd::new_peer_connection();
    body.listen(this);
}

void WebRTCLink::_bind_methods() {
}

void WebRTCLink::bind_session(int64_t p_handle, int64_t p_peer) {
    session_handle = p_handle;
    session_peer = p_peer;
}

void WebRTCLink::on_description(const String &p_type, const String &p_sdp) {
    emit_signal(StringName("session_description_created"), p_type, p_sdp);
    WebRTCSession *held = WebRTCSession::find(session_handle);
    if (held != nullptr) {
        held->report_description(
            session_peer,
            p_type,
            p_sdp,
            int64_t(Time::get_singleton()->get_ticks_msec())
        );
    }
}

void WebRTCLink::on_candidate(
    const String &p_media,
    int64_t p_index,
    const String &p_name
) {
    emit_signal(
        StringName("ice_candidate_created"),
        p_media,
        int64_t(p_index),
        p_name
    );
    WebRTCSession *held = WebRTCSession::find(session_handle);
    if (held != nullptr) {
        held->report_candidate(session_peer, p_media, p_index, p_name);
    }
}

#if defined(NETW_MODULE)

WebRTCPeerConnection::ConnectionState WebRTCLink::get_connection_state() const {
    return body.state();
}

WebRTCPeerConnection::GatheringState WebRTCLink::get_gathering_state() const {
    return body.gathering();
}

WebRTCPeerConnection::SignalingState WebRTCLink::get_signaling_state() const {
    return body.signaling();
}

Error WebRTCLink::initialize(const Dictionary &p_config) {
    return body.initialize(p_config);
}

Ref<WebRTCDataChannel> WebRTCLink::create_data_channel(
    const String &p_label,
    const Dictionary &p_config
) {
    return body.open_channel(p_label, p_config);
}

Error WebRTCLink::create_offer() {
    return body.create_offer();
}

Error WebRTCLink::set_remote_description(
    const String &p_type,
    const String &p_sdp
) {
    return body.set_remote(p_type, p_sdp);
}

Error WebRTCLink::set_local_description(
    const String &p_type,
    const String &p_sdp
) {
    return body.set_local(p_type, p_sdp);
}

Error WebRTCLink::add_ice_candidate(
    const String &p_media,
    int p_index,
    const String &p_name
) {
    return body.add_candidate(p_media, int64_t(p_index), p_name);
}

Error WebRTCLink::poll() {
    return body.poll();
}

void WebRTCLink::close() {
    body.shut(this);
}

#else

WebRTCPeerConnection::ConnectionState WebRTCLink::
    _get_connection_state() const {
    return body.state();
}

WebRTCPeerConnection::GatheringState WebRTCLink::_get_gathering_state() const {
    return body.gathering();
}

WebRTCPeerConnection::SignalingState WebRTCLink::_get_signaling_state() const {
    return body.signaling();
}

Error WebRTCLink::_initialize(const Dictionary &p_config) {
    return body.initialize(p_config);
}

Ref<WebRTCDataChannel> WebRTCLink::_create_data_channel(
    const String &p_label,
    const Dictionary &p_config
) {
    return body.open_channel(p_label, p_config);
}

Error WebRTCLink::_create_offer() {
    return body.create_offer();
}

Error WebRTCLink::_set_remote_description(
    const String &p_type,
    const String &p_sdp
) {
    return body.set_remote(p_type, p_sdp);
}

Error WebRTCLink::_set_local_description(
    const String &p_type,
    const String &p_sdp
) {
    return body.set_local(p_type, p_sdp);
}

Error WebRTCLink::_add_ice_candidate(
    const String &p_media,
    int32_t p_index,
    const String &p_name
) {
    return body.add_candidate(p_media, int64_t(p_index), p_name);
}

Error WebRTCLink::_poll() {
    return body.poll();
}

void WebRTCLink::_close() {
    body.shut(this);
}

#endif

} // namespace netw::connect
