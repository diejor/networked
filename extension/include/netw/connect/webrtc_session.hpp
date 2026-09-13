#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/variant.hpp"
#include "godot/web_rtc.hpp"

namespace netw::connect {

struct CandidateCounts {
    int64_t host = 0;
    int64_t srflx = 0;
    int64_t relay = 0;
};

struct SessionSignal {
    int64_t to_peer = 0;
    godot::String to_address;
    godot::String kind;
    godot::Dictionary payload;
};

struct SessionOutcome {
    enum Kind {
        NATIVE_CONNECTED,
        NATIVE_DISCONNECTED,
        FAILED,
    };

    Kind kind = NATIVE_CONNECTED;
    int64_t peer = 0;
    godot::String reason;
};

class WebRTCSession {
    friend struct WebRTCSessionProbe;

public:
    WebRTCSession();
    ~WebRTCSession();

    godot::LocalVector<godot::Dictionary> ice_servers;
    double connect_retry = 8.0;
    int64_t max_connect_attempts = 3;
    bool reconnect_masking = true;
    double gather_timeout = 6.0;
    double topup_interval = 0.25;
    bool is_local_session = false;

    godot::Error create_server(int64_t p_now_msec);
    godot::Error create_client(int64_t p_id, int64_t p_now_msec);

    void poll(int64_t p_now_msec);
    void deliver(
        int64_t p_from_peer,
        const godot::String &p_from_address,
        const godot::String &p_kind,
        const godot::Dictionary &p_payload,
        int64_t p_now_msec
    );

    bool has_peer(int64_t p_id) const;
    bool is_connected_to(int64_t p_id) const;
    CandidateCounts candidate_summary(int64_t p_id) const;
    godot::Dictionary connection_diagnostics(int64_t p_id) const;

    const godot::Ref<godot::MultiplayerPeer> &peer() const {
        return live;
    }

    void drain_signals(godot::LocalVector<SessionSignal> &r_out);
    void drain_outcomes(godot::LocalVector<SessionOutcome> &r_out);

    void close_channels();
    void close();

    int64_t handle() const {
        return book_handle;
    }
    static WebRTCSession *find(int64_t p_handle);

    void report_description(
        int64_t p_peer,
        const godot::String &p_type,
        const godot::String &p_sdp,
        int64_t p_now_msec
    );
    void report_candidate(
        int64_t p_peer,
        const godot::String &p_media,
        int64_t p_index,
        const godot::String &p_name
    );
    void report_native_connected(int64_t p_peer, int64_t p_now_msec);
    void report_native_disconnected(int64_t p_peer);

private:
    struct Row {
        int64_t peer = 0;
        godot::String address;
        bool is_local = false;
        bool remote_desc_set = false;
        bool connected = false;
        bool bundle_sent = false;
        bool candidates_dirty = false;
        bool topups_done = false;
        int64_t attempts = 1;
        int64_t attempt_started_msec = 0;
        int64_t last_send_msec = 0;
        int64_t gather_deadline_msec = 0;
        int64_t offer_msec = 0;
        int64_t answer_msec = 0;
        int64_t native_msec = 0;
        godot::String desc_type;
        godot::String desc_sdp;
        godot::Array local_candidates;
        godot::Array pending_candidates;
        godot::Dictionary applied_candidates;
        CandidateCounts counts;
    };

    godot::Ref<godot::MultiplayerPeer> live;
    godot::LocalVector<Row> rows;
    godot::LocalVector<SessionSignal> signals;
    godot::LocalVector<SessionOutcome> outcomes;
    int64_t book_handle = 0;
    bool is_server = false;
    bool retry_reported = false;

    Row *row_of(int64_t p_id);
    const Row *row_of(int64_t p_id) const;
    Row &open_row(int64_t p_id, bool p_is_local, int64_t p_now_msec);

    void ensure_connection(
        int64_t p_id,
        const godot::String &p_address,
        bool p_is_local,
        int64_t p_now_msec
    );
    void bind_peer(const godot::Ref<godot::MultiplayerPeer> &p_peer);

    void handle_description(
        Row &r_row,
        const godot::String &p_kind,
        const godot::Dictionary &p_payload
    );
    void handle_candidate(Row &r_row, const godot::Dictionary &p_payload);
    void add_candidate(Row &r_row, const godot::Dictionary &p_payload);
    void apply_bundled(Row &r_row, const godot::Dictionary &p_payload);
    void flush_pending(Row &r_row);

    void drive_signaling(int64_t p_now_msec);
    void maybe_retry(int64_t p_now_msec);
    void send_bundle(Row &r_row, int64_t p_now_msec);
    void account_candidate(Row &r_row, const godot::String &p_candidate);
    godot::String failure_reason(const Row &p_row) const;
};

godot::String candidate_key(const godot::Dictionary &p_payload);

} // namespace netw::connect
