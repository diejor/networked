#include "netw/connect/webrtc_session.hpp"

#include "godot/local_vector.hpp"
#include "netw/connect/webrtc_link.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

namespace {

struct SessionBook {
    LocalVector<WebRTCSession *> held;
    LocalVector<int64_t> handles;
    int64_t next = 1;
};

SessionBook &sessions() {
    static SessionBook book;
    return book;
}

} // namespace

String candidate_key(const Dictionary &p_payload) {
    return String(p_payload.get("sdpMid", String())) + ":"
        + String::num_int64(int64_t(p_payload.get("sdpMLineIndex", 0))) + ":"
        + String(p_payload.get("candidate", String()));
}

WebRTCSession::WebRTCSession() {
    SessionBook &book = sessions();
    book_handle = book.next++;
    book.held.push_back(this);
    book.handles.push_back(book_handle);
}

WebRTCSession::~WebRTCSession() {
    SessionBook &book = sessions();
    for (int64_t at = 0; at < int64_t(book.handles.size()); at++) {
        if (book.handles[at] == book_handle) {
            book.handles.remove_at(at);
            book.held.remove_at(at);
            break;
        }
    }
}

WebRTCSession *WebRTCSession::find(int64_t p_handle) {
    if (p_handle == 0) {
        return nullptr;
    }
    SessionBook &book = sessions();
    for (int64_t at = 0; at < int64_t(book.handles.size()); at++) {
        if (book.handles[at] == p_handle) {
            return book.held[at];
        }
    }
    return nullptr;
}

WebRTCSession::Row *WebRTCSession::row_of(int64_t p_id) {
    for (int64_t at = 0; at < int64_t(rows.size()); at++) {
        if (rows[at].peer == p_id) {
            return &rows[at];
        }
    }
    return nullptr;
}

const WebRTCSession::Row *WebRTCSession::row_of(int64_t p_id) const {
    for (int64_t at = 0; at < int64_t(rows.size()); at++) {
        if (rows[at].peer == p_id) {
            return &rows[at];
        }
    }
    return nullptr;
}

WebRTCSession::Row &WebRTCSession::open_row(
    int64_t p_id,
    bool p_is_local,
    int64_t p_now_msec
) {
    Row *found = row_of(p_id);
    if (found == nullptr) {
        Row made;
        made.peer = p_id;
        rows.push_back(made);
        found = &rows[int64_t(rows.size()) - 1];
    }
    found->is_local = p_is_local;
    found->remote_desc_set = false;
    found->bundle_sent = false;
    found->candidates_dirty = false;
    found->topups_done = false;
    found->attempt_started_msec = p_now_msec;
    found->last_send_msec = 0;
    found->gather_deadline_msec = 0;
    found->offer_msec = 0;
    found->answer_msec = 0;
    found->native_msec = 0;
    found->desc_type = String();
    found->desc_sdp = String();
    found->local_candidates.clear();
    found->pending_candidates.clear();
    found->applied_candidates.clear();
    found->counts = CandidateCounts();
    return *found;
}

Error WebRTCSession::create_server(int64_t p_now_msec) {
    is_server = true;
    Ref<MultiplayerPeer> made = gd::new_webrtc_multiplayer_peer();
    if (made.is_null()) {
        return ERR_CANT_CREATE;
    }
    const Error err = gd::webrtc_create_server(made);
    if (err != OK) {
        NETW_ERROR(
            sys::TRANSPORT,
            "the WebRTC session refused to open a server"
        );
        return err;
    }
    live = made;
    (void)p_now_msec;
    return OK;
}

Error WebRTCSession::create_client(int64_t p_id, int64_t p_now_msec) {
    is_server = false;
    Ref<MultiplayerPeer> made = gd::new_webrtc_multiplayer_peer();
    if (made.is_null()) {
        return ERR_CANT_CREATE;
    }
    const Error err = gd::webrtc_create_client(made, p_id);
    if (err != OK) {
        NETW_ERROR(
            sys::TRANSPORT,
            "the WebRTC session refused to open a client"
        );
        return err;
    }
    live = made;
    ensure_connection(1, String(), false, p_now_msec);
    return OK;
}

void WebRTCSession::ensure_connection(
    int64_t p_id,
    const String &p_address,
    bool p_is_local,
    int64_t p_now_msec
) {
    if (live.is_null() || gd::webrtc_has_peer(live, p_id)) {
        return;
    }
    Row &row = open_row(p_id, p_is_local, p_now_msec);
    if (!p_address.is_empty()) {
        row.address = p_address;
    }
    row.attempts = 1;

    Ref<WebRTCLink> link = memnew(WebRTCLink);
    link->set_masking(reconnect_masking);
    link->bind_session(book_handle, p_id);
    Ref<godot::WebRTCPeerConnection> held = link;

    Dictionary config;
    Array servers;
    if (!p_is_local && !is_local_session) {
        for (int64_t at = 0; at < int64_t(ice_servers.size()); at++) {
            servers.push_back(ice_servers[at]);
        }
    }
    config["iceServers"] = servers;
    held->initialize(config);

    gd::webrtc_add_peer(live, held, p_id);
    if (!is_server && p_id == 1) {
        held->create_offer();
    }
}

void WebRTCSession::poll(int64_t p_now_msec) {
    if (live.is_null()) {
        return;
    }
    live->poll();

    const Dictionary peers = gd::webrtc_peers(live);
    for (int64_t at = 0; at < int64_t(rows.size()); at++) {
        Row &row = rows[at];
        const Variant found = peers.get(row.peer, Variant());
        bool up = false;
        if (found.get_type() == Variant::DICTIONARY) {
            const Dictionary held = found;
            up = bool(held.get("connected", false));
        }
        if (up && !row.connected) {
            row.connected = true;
            row.native_msec = p_now_msec;
            SessionOutcome one;
            one.kind = SessionOutcome::NATIVE_CONNECTED;
            one.peer = row.peer;
            outcomes.push_back(one);
        } else if (!up && row.connected) {
            row.connected = false;
            SessionOutcome one;
            one.kind = SessionOutcome::NATIVE_DISCONNECTED;
            one.peer = row.peer;
            outcomes.push_back(one);
        }
    }

    drive_signaling(p_now_msec);
    if (!is_server) {
        maybe_retry(p_now_msec);
    }
}

void WebRTCSession::deliver(
    int64_t p_from_peer,
    const String &p_from_address,
    const String &p_kind,
    const Dictionary &p_payload,
    int64_t p_now_msec
) {
    if (live.is_null()) {
        return;
    }
    const bool is_local = bool(p_payload.get("is_local", false));
    ensure_connection(p_from_peer, p_from_address, is_local, p_now_msec);
    Row *row = row_of(p_from_peer);
    if (row == nullptr) {
        return;
    }
    if (!p_from_address.is_empty()) {
        row->address = p_from_address;
    }
    if (p_kind == "offer" || p_kind == "answer") {
        handle_description(*row, p_kind, p_payload);
    } else if (p_kind == "candidate") {
        handle_candidate(*row, p_payload);
    }
}

void WebRTCSession::handle_description(
    Row &r_row,
    const String &p_kind,
    const Dictionary &p_payload
) {
    if (!gd::webrtc_has_peer(live, r_row.peer)) {
        return;
    }
    const String sdp = p_payload.get("sdp", String());
    if (sdp.is_empty()) {
        NETW_DEBUG(
            sys::TRANSPORT,
            "the WebRTC session dropped an SDP-less %s for id %d",
            p_kind,
            r_row.peer
        );
        return;
    }
    if (!r_row.remote_desc_set) {
        Ref<godot::WebRTCPeerConnection> link
            = gd::webrtc_connection_of(live, r_row.peer);
        if (link.is_null()) {
            return;
        }
        if (link->set_remote_description(p_kind, sdp) != OK) {
            NETW_DEBUG(
                sys::TRANSPORT,
                "the WebRTC session ignored a stale %s for id %d",
                p_kind,
                r_row.peer
            );
            return;
        }
        r_row.remote_desc_set = true;
    }
    apply_bundled(r_row, p_payload);
    flush_pending(r_row);
}

void WebRTCSession::handle_candidate(Row &r_row, const Dictionary &p_payload) {
    if (!gd::webrtc_has_peer(live, r_row.peer)) {
        return;
    }
    if (!r_row.remote_desc_set) {
        r_row.pending_candidates.push_back(p_payload);
        return;
    }
    add_candidate(r_row, p_payload);
}

void WebRTCSession::add_candidate(Row &r_row, const Dictionary &p_payload) {
    const String key = candidate_key(p_payload);
    if (r_row.applied_candidates.has(key)) {
        return;
    }
    r_row.applied_candidates[key] = true;
    Ref<godot::WebRTCPeerConnection> link
        = gd::webrtc_connection_of(live, r_row.peer);
    if (link.is_null()) {
        return;
    }
    link->add_ice_candidate(
        String(p_payload.get("sdpMid", String())),
        int(int64_t(p_payload.get("sdpMLineIndex", 0))),
        String(p_payload.get("candidate", String()))
    );
}

void WebRTCSession::apply_bundled(Row &r_row, const Dictionary &p_payload) {
    const Variant found = p_payload.get("candidates", Array());
    if (found.get_type() != Variant::ARRAY) {
        return;
    }
    const Array bundled = found;
    for (int64_t at = 0; at < bundled.size(); at++) {
        if (bundled[at].get_type() == Variant::DICTIONARY) {
            add_candidate(r_row, bundled[at]);
        }
    }
}

void WebRTCSession::flush_pending(Row &r_row) {
    for (int64_t at = 0; at < r_row.pending_candidates.size(); at++) {
        add_candidate(r_row, r_row.pending_candidates[at]);
    }
    r_row.pending_candidates.clear();
}

void WebRTCSession::report_description(
    int64_t p_peer,
    const String &p_type,
    const String &p_sdp,
    int64_t p_now_msec
) {
    Row *row = row_of(p_peer);
    if (row == nullptr) {
        return;
    }
    Ref<godot::WebRTCPeerConnection> link
        = gd::webrtc_connection_of(live, p_peer);
    if (link.is_valid()) {
        link->set_local_description(p_type, p_sdp);
    }
    if (p_type == "offer") {
        row->offer_msec = p_now_msec;
    } else if (p_type == "answer") {
        row->answer_msec = p_now_msec;
    }
    row->desc_type = p_type;
    row->desc_sdp = p_sdp;
    row->bundle_sent = true;
    row->candidates_dirty = false;
    row->topups_done = false;
    row->gather_deadline_msec = p_now_msec + int64_t(gather_timeout * 1000.0);
    send_bundle(*row, p_now_msec);
}

void WebRTCSession::report_candidate(
    int64_t p_peer,
    const String &p_media,
    int64_t p_index,
    const String &p_name
) {
    Row *row = row_of(p_peer);
    if (row == nullptr) {
        return;
    }
    account_candidate(*row, p_name);
    Dictionary one;
    one["type"] = "candidate";
    one["candidate"] = p_name;
    one["sdpMid"] = p_media;
    one["sdpMLineIndex"] = p_index;
    row->local_candidates.push_back(one);
    row->candidates_dirty = true;
}

void WebRTCSession::report_native_connected(
    int64_t p_peer,
    int64_t p_now_msec
) {
    Row *row = row_of(p_peer);
    if (row == nullptr || row->connected) {
        return;
    }
    row->connected = true;
    row->native_msec = p_now_msec;
    SessionOutcome one;
    one.kind = SessionOutcome::NATIVE_CONNECTED;
    one.peer = p_peer;
    outcomes.push_back(one);
}

void WebRTCSession::report_native_disconnected(int64_t p_peer) {
    Row *row = row_of(p_peer);
    if (row == nullptr || !row->connected) {
        return;
    }
    row->connected = false;
    SessionOutcome one;
    one.kind = SessionOutcome::NATIVE_DISCONNECTED;
    one.peer = p_peer;
    outcomes.push_back(one);
}

void WebRTCSession::drive_signaling(int64_t p_now_msec) {
    for (int64_t at = 0; at < int64_t(rows.size()); at++) {
        Row &row = rows[at];
        if (row.topups_done || row.desc_sdp.is_empty()) {
            continue;
        }
        if (!gd::webrtc_has_peer(live, row.peer)) {
            continue;
        }
        Ref<godot::WebRTCPeerConnection> link
            = gd::webrtc_connection_of(live, row.peer);
        const bool complete = link.is_valid()
            && link->get_gathering_state()
                == godot::WebRTCPeerConnection::GATHERING_STATE_COMPLETE;
        const bool timed_out = p_now_msec >= row.gather_deadline_msec;
        if (complete || timed_out) {
            row.candidates_dirty = false;
            row.topups_done = true;
            send_bundle(row, p_now_msec);
            continue;
        }
        if (!row.candidates_dirty) {
            continue;
        }
        if (p_now_msec - row.last_send_msec
            < int64_t(topup_interval * 1000.0)) {
            continue;
        }
        row.candidates_dirty = false;
        send_bundle(row, p_now_msec);
    }
}

void WebRTCSession::maybe_retry(int64_t p_now_msec) {
    Row *row = row_of(1);
    if (row == nullptr || row->connected || !row->bundle_sent) {
        return;
    }
    if (p_now_msec - row->attempt_started_msec
        < int64_t(connect_retry * 1000.0)) {
        return;
    }
    if (row->attempts >= max_connect_attempts) {
        if (!retry_reported) {
            retry_reported = true;
            SessionOutcome one;
            one.kind = SessionOutcome::FAILED;
            one.peer = 1;
            one.reason = failure_reason(*row);
            outcomes.push_back(one);
        }
        return;
    }
    row->attempts++;
    send_bundle(*row, p_now_msec);
}

void WebRTCSession::send_bundle(Row &r_row, int64_t p_now_msec) {
    if (r_row.desc_sdp.is_empty()) {
        return;
    }
    r_row.attempt_started_msec = p_now_msec;
    r_row.last_send_msec = p_now_msec;

    Dictionary payload;
    payload["type"] = r_row.desc_type;
    payload["sdp"] = r_row.desc_sdp;
    payload["candidates"] = r_row.local_candidates.duplicate();
    payload["is_local"] = is_local_session || r_row.is_local;

    SessionSignal out;
    out.to_peer = r_row.peer;
    out.to_address = r_row.address;
    out.kind = r_row.desc_type;
    out.payload = payload;
    signals.push_back(out);
}

void WebRTCSession::account_candidate(Row &r_row, const String &p_candidate) {
    if (p_candidate.contains(" typ relay")) {
        r_row.counts.relay++;
    } else if (p_candidate.contains(" typ srflx")) {
        r_row.counts.srflx++;
    } else if (p_candidate.contains(" typ host")) {
        r_row.counts.host++;
    }
}

String WebRTCSession::failure_reason(const Row &p_row) const {
    if (!p_row.remote_desc_set) {
        return String("HOST_UNRESPONSIVE");
    }
    if (p_row.counts.relay == 0) {
        return String("TURN_UNREACHABLE");
    }
    return String("NAT_TRAVERSAL_FAILED");
}

bool WebRTCSession::has_peer(int64_t p_id) const {
    return live.is_valid() && gd::webrtc_has_peer(live, p_id);
}

bool WebRTCSession::is_connected_to(int64_t p_id) const {
    const Row *row = row_of(p_id);
    return row != nullptr && row->connected;
}

CandidateCounts WebRTCSession::candidate_summary(int64_t p_id) const {
    const Row *row = row_of(p_id);
    return row != nullptr ? row->counts : CandidateCounts();
}

Dictionary WebRTCSession::connection_diagnostics(int64_t p_id) const {
    const Row *row = row_of(p_id);
    const CandidateCounts counts
        = row != nullptr ? row->counts : CandidateCounts();

    Dictionary phases;
    phases["offer_ms"] = row != nullptr ? row->offer_msec : 0;
    phases["answer_ms"] = row != nullptr ? row->answer_msec : 0;
    phases["native_ms"] = row != nullptr ? row->native_msec : 0;

    Dictionary candidates;
    candidates["host"] = counts.host;
    candidates["srflx"] = counts.srflx;
    candidates["relay"] = counts.relay;

    Dictionary out;
    out["phases"] = phases;
    out["candidates"] = candidates;
    out["relay_used"]
        = counts.relay > 0 && counts.host == 0 && counts.srflx == 0;
    return out;
}

void WebRTCSession::drain_signals(LocalVector<SessionSignal> &r_out) {
    for (int64_t at = 0; at < int64_t(signals.size()); at++) {
        r_out.push_back(signals[at]);
    }
    signals.clear();
}

void WebRTCSession::drain_outcomes(LocalVector<SessionOutcome> &r_out) {
    for (int64_t at = 0; at < int64_t(outcomes.size()); at++) {
        r_out.push_back(outcomes[at]);
    }
    outcomes.clear();
}

void WebRTCSession::close_channels() {
    if (live.is_null()) {
        return;
    }
    const Dictionary peers = gd::webrtc_peers(live);
    const Array held = peers.values();
    for (int64_t at = 0; at < held.size(); at++) {
        if (held[at].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary row = held[at];
        const Variant found = row.get("channels", Array());
        if (found.get_type() != Variant::ARRAY) {
            continue;
        }
        const Array channels = found;
        for (int64_t seat = 0; seat < channels.size(); seat++) {
            Object *made = channels[seat];
            godot::WebRTCDataChannel *channel
                = Object::cast_to<godot::WebRTCDataChannel>(made);
            if (channel != nullptr
                && channel->get_ready_state()
                    < godot::WebRTCDataChannel::STATE_CLOSING) {
                channel->close();
            }
        }
    }
}

void WebRTCSession::close() {
    if (live.is_valid()) {
        close_channels();
        live->close();
    }
    live.unref();
    is_local_session = false;
    retry_reported = false;
    rows.clear();
    signals.clear();
    outcomes.clear();
}

} // namespace netw::connect
