#pragma once

#include <cstdint>

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/connect/signaler.hpp"

namespace netw {

class NetwWebRTCSignaler : public godot::RefCounted {
    GDCLASS(NetwWebRTCSignaler, godot::RefCounted)

    godot::LocalVector<connect::SignalerEvent> pending;

protected:
    static void _bind_methods();

public:
    void receive(
        int64_t p_from_peer,
        const godot::String &p_from_address,
        const godot::String &p_kind,
        const godot::Dictionary &p_payload
    );
    void report_ready();
    void report_lost();
    void report_unreachable();

    void take(godot::LocalVector<connect::SignalerEvent> &r_out);

    virtual godot::Error open(
        const godot::String &p_room,
        int64_t p_local_peer
    );
    virtual void poll(double p_delta);
    virtual void send(
        int64_t p_to_peer,
        const godot::String &p_to_address,
        const godot::String &p_kind,
        const godot::Dictionary &p_payload
    );
    virtual godot::String room_id();
    virtual godot::String local_signaler_id();
    godot::String local_signaler_id_default();
    virtual void session_connected(int64_t p_peer);
    void session_connected_default(int64_t p_peer);
    virtual void close();

    GDVIRTUAL2R(godot::Error, _open, godot::String, int64_t)
    GDVIRTUAL1(_poll, double)
    GDVIRTUAL4(_send, int64_t, godot::String, godot::String, godot::Dictionary)
    GDVIRTUAL0R(godot::String, _room_id)
    GDVIRTUAL0R(godot::String, _local_signaler_id)
    GDVIRTUAL1(_on_session_connected, int64_t)
    GDVIRTUAL0(_close)
};

} // namespace netw

namespace netw::connect {

class ScriptSignaler : public Signaler {
    godot::Ref<NetwWebRTCSignaler> seam;

public:
    explicit ScriptSignaler(const godot::Ref<NetwWebRTCSignaler> &p_seam);

    godot::Error open(
        const godot::String &p_room,
        int64_t p_local_peer
    ) override;
    void poll(double p_delta, int64_t p_now_usec, int64_t p_frame) override;
    void send(
        int64_t p_to_peer,
        const godot::String &p_to_address,
        const godot::String &p_kind,
        const godot::Dictionary &p_payload
    ) override;
    godot::String room_id() const override;
    godot::String local_address() const override;
    void on_session_connected(int64_t p_peer) override;
    void close() override;
};

} // namespace netw::connect
