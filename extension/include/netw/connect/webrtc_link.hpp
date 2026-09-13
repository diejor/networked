#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "godot/web_rtc.hpp"

namespace netw::connect {

class WebRTCLink;

struct LinkBody {
    godot::Ref<godot::WebRTCPeerConnection> inner;
    bool masking = true;

    godot::WebRTCPeerConnection::ConnectionState state() const;
    godot::WebRTCPeerConnection::GatheringState gathering() const;
    godot::WebRTCPeerConnection::SignalingState signaling() const;
    godot::Error initialize(const godot::Dictionary &p_config);
    godot::Ref<godot::WebRTCDataChannel> open_channel(
        const godot::String &p_label,
        const godot::Dictionary &p_config
    );
    godot::Error create_offer();
    godot::Error set_remote(
        const godot::String &p_type,
        const godot::String &p_sdp
    );
    godot::Error set_local(
        const godot::String &p_type,
        const godot::String &p_sdp
    );
    godot::Error add_candidate(
        const godot::String &p_media,
        int64_t p_index,
        const godot::String &p_name
    );
    godot::Error poll();
    void shut(WebRTCLink *p_owner);

    void listen(WebRTCLink *p_owner);
};

#if defined(NETW_MODULE)
#define NETW_LINK_BASE godot::WebRTCPeerConnection
#else
#define NETW_LINK_BASE godot::WebRTCPeerConnectionExtension
#endif

class WebRTCLink : public NETW_LINK_BASE {
    GDCLASS(WebRTCLink, NETW_LINK_BASE)

    LinkBody body;
    int64_t session_handle = 0;
    int64_t session_peer = 0;

protected:
    static void _bind_methods();

public:
    WebRTCLink();

    void bind_session(int64_t p_handle, int64_t p_peer);
    void set_masking(bool p_masking) {
        body.masking = p_masking;
    }

    void on_description(
        const godot::String &p_type,
        const godot::String &p_sdp
    );
    void on_candidate(
        const godot::String &p_media,
        int64_t p_index,
        const godot::String &p_name
    );

#if defined(NETW_MODULE)
    godot::WebRTCPeerConnection::
        ConnectionState get_connection_state() const override;
    godot::WebRTCPeerConnection::GatheringState
    get_gathering_state() const override;
    godot::WebRTCPeerConnection::SignalingState
    get_signaling_state() const override;
    godot::Error initialize(const godot::Dictionary &p_config) override;
    godot::Ref<godot::WebRTCDataChannel> create_data_channel(
        const godot::String &p_label,
        const godot::Dictionary &p_config
    ) override;
    godot::Error create_offer() override;
    godot::Error set_remote_description(
        const godot::String &p_type,
        const godot::String &p_sdp
    ) override;
    godot::Error set_local_description(
        const godot::String &p_type,
        const godot::String &p_sdp
    ) override;
    godot::Error add_ice_candidate(
        const godot::String &p_media,
        int p_index,
        const godot::String &p_name
    ) override;
    godot::Error poll() override;
    void close() override;
#else
    godot::WebRTCPeerConnection::
        ConnectionState _get_connection_state() const override;
    godot::WebRTCPeerConnection::GatheringState
    _get_gathering_state() const override;
    godot::WebRTCPeerConnection::SignalingState
    _get_signaling_state() const override;
    godot::Error _initialize(const godot::Dictionary &p_config) override;
    godot::Ref<godot::WebRTCDataChannel> _create_data_channel(
        const godot::String &p_label,
        const godot::Dictionary &p_config
    ) override;
    godot::Error _create_offer() override;
    godot::Error _set_remote_description(
        const godot::String &p_type,
        const godot::String &p_sdp
    ) override;
    godot::Error _set_local_description(
        const godot::String &p_type,
        const godot::String &p_sdp
    ) override;
    godot::Error _add_ice_candidate(
        const godot::String &p_media,
        int32_t p_index,
        const godot::String &p_name
    ) override;
    godot::Error _poll() override;
    void _close() override;
#endif
};

} // namespace netw::connect
