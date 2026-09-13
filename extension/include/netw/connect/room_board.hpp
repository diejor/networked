#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/random.hpp"
#include "godot/variant.hpp"
#include "netw/connect/browse_list.hpp"

namespace netw::connect {

class TrackerClient;

struct RoomCard {
    godot::String room_hash;
    godot::String room_name;
    godot::String filter_uid;
    godot::StringName app_id;
    godot::String signaling_namespace;
    int64_t players = 0;
    int64_t max_players = 0;

    godot::Dictionary to_dictionary() const;
    static bool from_dictionary(
        const godot::Dictionary &p_card,
        RoomCard &r_out
    );
    TargetRow to_row() const;
};

godot::String board_hash_of(const godot::String &p_filter_uid);

class RoomBoard {
    friend struct RoomBoardProbe;

public:
    static const double RECONNECT_COOLDOWN;

    godot::String filter_uid = "networked";
    double browse_window = 2.5;
    double advertise_interval = 2.0;
    double idle_timeout = 30.0;
    int64_t fanout = 16;

    ~RoomBoard();

    void set_trackers(const godot::PackedStringArray &p_trackers);
    void poll(double p_delta, int64_t p_now_usec, int64_t p_frame);

    void advertise(const RoomCard &p_card);
    void stop_advertising();
    void browse();
    bool take_listing(godot::LocalVector<TargetRow> &r_rows);

    void close();

private:
    godot::PackedStringArray trackers;
    TrackerClient *tracker = nullptr;
    int64_t subscription = 0;
    bool shared = false;

    godot::String board_hash;
    godot::String board_peer_id;

    bool advertising = false;
    RoomCard advertised;
    double advertise_acc = 0.0;

    bool collecting = false;
    double collect_left = 0.0;
    double query_acc = 0.0;
    godot::LocalVector<RoomCard> collected;
    bool listing_ready = false;

    double reconnect_acc = 0.0;
    double idle_acc = 0.0;
    godot::Ref<godot::RandomNumberGenerator> dice;

    void ensure_tracker();
    void release_tracker();
    void keep_warm(double p_delta);
    void maintain(double p_delta);
    void read_packet(const godot::Dictionary &p_data);
    void collect(const RoomCard &p_card);
    void answer_card(
        const godot::String &p_to,
        const godot::String &p_offer_id
    );
    godot::Dictionary announce_with_card() const;
    godot::Dictionary query_announce() const;
    godot::String random_hex();
};

} // namespace netw::connect
