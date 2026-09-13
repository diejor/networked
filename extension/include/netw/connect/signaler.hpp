#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/random.hpp"
#include "godot/variant.hpp"

namespace netw::connect {

class TrackerClient;

struct SignalerEvent {
    enum Kind {
        RECEIVED,
        READY,
        LOST,
        UNREACHABLE,
    };

    Kind kind = RECEIVED;
    int64_t from_peer = 0;
    godot::String from_address;
    godot::String kind_text;
    godot::Dictionary payload;
};

class Signaler {
public:
    virtual ~Signaler() = default;

    virtual godot::Error open(const godot::String &p_room, int64_t p_local_peer)
        = 0;
    virtual void poll(double p_delta, int64_t p_now_usec, int64_t p_frame) = 0;
    virtual void send(
        int64_t p_to_peer,
        const godot::String &p_to_address,
        const godot::String &p_kind,
        const godot::Dictionary &p_payload
    ) = 0;
    virtual godot::String room_id() const = 0;
    virtual godot::String local_address() const = 0;
    virtual void on_session_connected(int64_t p_peer) {
    }
    virtual void close() = 0;

    void drain(godot::LocalVector<SignalerEvent> &r_out);

protected:
    void publish(const SignalerEvent &p_event);

private:
    godot::LocalVector<SignalerEvent> inbound;
};

class TrackerSignaler : public Signaler {
    friend struct TrackerSignalerProbe;

public:
    static const double SIGNALING_CLOSE_DELAY;

    TrackerSignaler(
        const godot::PackedStringArray &p_trackers,
        const godot::String &p_namespace,
        const godot::String &p_characters
    );
    ~TrackerSignaler() override;

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
    godot::String room_id() const override {
        return room;
    }
    godot::String local_address() const override {
        return local_peer_id;
    }
    void on_session_connected(int64_t p_peer) override;
    void close() override;

    godot::String info_hash() const {
        return hash;
    }

    static godot::String host_peer_id(const godot::String &p_info_hash);
    static godot::String derived_info_hash(
        const godot::String &p_namespace,
        const godot::String &p_room
    );
    static int64_t peer_of_address(const godot::String &p_address);

private:
    struct Pending {
        godot::String key;
        godot::Dictionary message;
    };

    godot::PackedStringArray trackers;
    godot::String signaling_namespace;
    godot::String room_characters;

    TrackerClient *tracker = nullptr;
    int64_t subscription = 0;
    bool shared = false;
    bool is_server = false;
    bool native_up = false;

    godot::String room;
    godot::String hash;
    godot::String local_peer_id;
    godot::String server_peer_id;
    int64_t local_peer = 0;

    godot::Dictionary handled_offers;
    godot::Dictionary handled_answers;
    godot::LocalVector<Pending> pending;
    double announce_timer = 0.0;
    double close_delay = -1.0;
    double min_announce_period = 0.0;
    godot::Ref<godot::RandomNumberGenerator> dice;

    godot::Dictionary presence() const;
    void announce_open_sockets();
    void flush_pending();
    void read_packet(const godot::Dictionary &p_data);
    void take_inbound(
        int64_t p_peer,
        const godot::String &p_address,
        const godot::String &p_kind,
        const godot::Dictionary &p_payload,
        godot::Dictionary &r_handled
    );
    void send_directed(
        const godot::String &p_to,
        const godot::String &p_type,
        const godot::Dictionary &p_payload
    );
    void send_stop();
    void release();
    void drive_presence(double p_delta);
    double reannounce_period() const;

    godot::String random_hex(int64_t p_length);
    godot::String short_code();
    godot::String peer_id_for(int64_t p_peer);
};

godot::String candidate_digest(const godot::Dictionary &p_payload);

} // namespace netw::connect
