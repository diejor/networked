#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw::connect {

struct TrackerEvent {
    enum Kind {
        OPENED,
        SOCKET_OPENED,
        CLOSED,
        UNREACHABLE,
        MESSAGE,
    };

    Kind kind = MESSAGE;
    int64_t socket = -1;
    godot::Dictionary data;
};

class TrackerClient {
    friend struct TrackerClientProbe;

public:
    static const int64_t CONNECT_TIMEOUT_USEC = 10000000;
    static const char *WARN_SETTING;

    static bool warns_on_failure();

    godot::Error connect_to(const godot::PackedStringArray &p_urls);
    void poll(int64_t p_now_usec, int64_t p_frame);

    void broadcast(const godot::Dictionary &p_message);
    void send(int64_t p_socket, const godot::Dictionary &p_message);

    bool has_open() const;
    bool is_active() const {
        return sockets.size() > 0;
    }
    godot::PackedInt64Array open_sockets() const;

    int64_t subscribe();
    void unsubscribe(int64_t p_token);
    void drain(int64_t p_token, godot::LocalVector<TrackerEvent> &r_events);

    void close();

private:
    struct Socket {
        int64_t id = 0;
        godot::Ref<godot::RefCounted> peer;
        godot::String url;
        int64_t connect_usec = 0;
        bool opened = false;
    };

    struct Cursor {
        int64_t token = 0;
        int64_t at = 0;
    };

    godot::LocalVector<Socket> sockets;
    godot::PackedStringArray failures;
    godot::LocalVector<TrackerEvent> events;
    godot::LocalVector<Cursor> cursors;
    int64_t next_token = 1;
    int64_t next_socket_id = 1;
    int64_t drained = 0;
    int64_t last_poll_frame = -1;
    bool any_opened = false;
    bool reported_unreachable = false;

    int index_of_socket(int64_t p_id) const;
    void note_failure(const godot::String &p_url, const char *p_reason);
    void record(const TrackerEvent &p_event);
    void trim();
};

class TrackerBook {
public:
    static TrackerBook &shared();

    TrackerClient *acquire(
        const godot::PackedStringArray &p_urls,
        godot::Error &r_error
    );
    void release(const godot::PackedStringArray &p_urls, TrackerClient *p_held);
    void clear();

private:
    struct Row {
        godot::String key;
        TrackerClient *client = nullptr;
        int64_t refs = 0;
    };

    godot::LocalVector<Row> rows;

    int index_of(const godot::String &p_key) const;
};

godot::String tracker_key(const godot::PackedStringArray &p_urls);

} // namespace netw::connect
