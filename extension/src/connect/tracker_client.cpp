#include "netw/connect/tracker_client.hpp"

#include "godot/json.hpp"
#include "godot/project_settings.hpp"
#include "godot/variant.hpp"
#include "godot/web_socket.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

const char *TrackerClient::WARN_SETTING
    = "networked/webrtc/warn_on_tracker_failure";

bool TrackerClient::warns_on_failure() {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return false;
    }
    return bool(settings->get_setting(WARN_SETTING, false));
}

String tracker_key(const PackedStringArray &p_urls) {
    PackedStringArray sorted = p_urls.duplicate();
    sorted.sort();
    String key;
    for (int64_t at = 0; at < sorted.size(); at++) {
        if (at > 0) {
            key += "\n";
        }
        key += sorted[at];
    }
    return key;
}

Error TrackerClient::connect_to(const PackedStringArray &p_urls) {
    close();
    reported_unreachable = false;
    failures.clear();
    for (int64_t at = 0; at < p_urls.size(); at++) {
        const String url = p_urls[at];
        Ref<RefCounted> peer = gd::new_web_socket();
        if (peer.is_null()) {
            continue;
        }
        if (gd::web_socket_connect(peer, url) != OK) {
            note_failure(url, "refused to open");
            continue;
        }
        Socket opening;
        opening.id = next_socket_id++;
        opening.peer = peer;
        opening.url = url;
        sockets.push_back(opening);
    }
    if (sockets.size() == 0) {
        return ERR_CANT_CONNECT;
    }
    return OK;
}

void TrackerClient::poll(int64_t p_now_usec, int64_t p_frame) {
    if (sockets.size() == 0) {
        return;
    }
    if (p_frame >= 0 && last_poll_frame == p_frame) {
        return;
    }
    last_poll_frame = p_frame;

    for (int64_t at = 0; at < int64_t(sockets.size()); at++) {
        Socket &socket = sockets[at];
        if (socket.connect_usec == 0) {
            socket.connect_usec = p_now_usec;
        }
        gd::web_socket_poll(socket.peer);
        const int state = gd::web_socket_state(socket.peer);

        if (state == gd::WEB_SOCKET_CONNECTING) {
            if (p_now_usec - socket.connect_usec > CONNECT_TIMEOUT_USEC) {
                note_failure(socket.url, "timed out");
                gd::web_socket_close(socket.peer);
                socket.peer.unref();
            }
            continue;
        }
        if (state == gd::WEB_SOCKET_CLOSED) {
            if (socket.opened) {
                NETW_INFO(sys::TRANSPORT, "tracker closed: %s", socket.url);
            } else {
                note_failure(socket.url, "closed before opening");
            }
            socket.peer.unref();
            continue;
        }
        if (state != gd::WEB_SOCKET_OPEN) {
            continue;
        }
        if (!socket.opened) {
            socket.opened = true;
            if (!any_opened) {
                any_opened = true;
                TrackerEvent up;
                up.kind = TrackerEvent::OPENED;
                record(up);
            }
            TrackerEvent one;
            one.kind = TrackerEvent::SOCKET_OPENED;
            one.socket = socket.id;
            record(one);
        }
        while (gd::web_socket_pending(socket.peer) > 0) {
            const PackedByteArray packet = gd::web_socket_take(socket.peer);
            const String text = gd::utf8_string(packet);
            const Variant parsed = JSON::parse_string(text);
            if (parsed.get_type() != Variant::DICTIONARY) {
                continue;
            }
            const Dictionary data = parsed;
            if (data.has("warning") || data.has("failure reason")) {
                NETW_TRACE(sys::TRANSPORT, "tracker notice %s", text);
                continue;
            }
            TrackerEvent message;
            message.kind = TrackerEvent::MESSAGE;
            message.socket = socket.id;
            message.data = data;
            record(message);
        }
    }

    for (int64_t at = int64_t(sockets.size()) - 1; at >= 0; at--) {
        if (sockets[at].peer.is_null()) {
            sockets.remove_at(at);
        }
    }

    if (sockets.size() == 0 && !any_opened && !reported_unreachable) {
        reported_unreachable = true;
        NETW_WARN_ONCE(
            sys::TRANSPORT,
            "WebRTC signaling has no tracker. Tried: %s",
            String(", ").join(failures)
        );
        TrackerEvent none;
        none.kind = TrackerEvent::UNREACHABLE;
        record(none);
    }
    if (any_opened && !has_open()) {
        any_opened = false;
        TrackerEvent down;
        down.kind = TrackerEvent::CLOSED;
        record(down);
    }
}

void TrackerClient::broadcast(const Dictionary &p_message) {
    const String text = JSON::stringify(p_message, "", true, false);
    for (int64_t at = 0; at < int64_t(sockets.size()); at++) {
        if (gd::web_socket_state(sockets[at].peer) == gd::WEB_SOCKET_OPEN) {
            gd::web_socket_send_text(sockets[at].peer, text);
        }
    }
}

void TrackerClient::send(int64_t p_socket, const Dictionary &p_message) {
    const int at = index_of_socket(p_socket);
    if (at < 0) {
        return;
    }
    if (gd::web_socket_state(sockets[at].peer) != gd::WEB_SOCKET_OPEN) {
        return;
    }
    gd::web_socket_send_text(
        sockets[at].peer,
        JSON::stringify(p_message, "", true, false)
    );
}

void TrackerClient::note_failure(const String &p_url, const char *p_reason) {
    failures.push_back(vformat("%s (%s)", p_url, String(p_reason)));
    if (warns_on_failure()) {
        NETW_WARN(
            sys::TRANSPORT,
            "tracker unreachable: %s (%s)",
            p_url,
            String(p_reason)
        );
        return;
    }
    NETW_INFO(
        sys::TRANSPORT,
        "tracker unreachable: %s (%s)",
        p_url,
        String(p_reason)
    );
}

int TrackerClient::index_of_socket(int64_t p_id) const {
    for (int64_t at = 0; at < int64_t(sockets.size()); at++) {
        if (sockets[at].id == p_id) {
            return int(at);
        }
    }
    return -1;
}

bool TrackerClient::has_open() const {
    for (int64_t at = 0; at < int64_t(sockets.size()); at++) {
        if (gd::web_socket_state(sockets[at].peer) == gd::WEB_SOCKET_OPEN) {
            return true;
        }
    }
    return false;
}

PackedInt64Array TrackerClient::open_sockets() const {
    PackedInt64Array out;
    for (int64_t at = 0; at < int64_t(sockets.size()); at++) {
        if (gd::web_socket_state(sockets[at].peer) == gd::WEB_SOCKET_OPEN) {
            out.push_back(sockets[at].id);
        }
    }
    return out;
}

int64_t TrackerClient::subscribe() {
    Cursor made;
    made.token = next_token++;
    made.at = drained + int64_t(events.size());
    cursors.push_back(made);
    return made.token;
}

void TrackerClient::unsubscribe(int64_t p_token) {
    for (int64_t at = 0; at < int64_t(cursors.size()); at++) {
        if (cursors[at].token == p_token) {
            cursors.remove_at(at);
            break;
        }
    }
    trim();
}

void TrackerClient::drain(
    int64_t p_token,
    LocalVector<TrackerEvent> &r_events
) {
    for (int64_t at = 0; at < int64_t(cursors.size()); at++) {
        if (cursors[at].token != p_token) {
            continue;
        }
        int64_t seq = cursors[at].at > drained ? cursors[at].at : drained;
        for (; seq < drained + int64_t(events.size()); seq++) {
            r_events.push_back(events[seq - drained]);
        }
        cursors[at].at = drained + int64_t(events.size());
        break;
    }
    trim();
}

void TrackerClient::record(const TrackerEvent &p_event) {
    events.push_back(p_event);
}

void TrackerClient::trim() {
    if (cursors.size() == 0) {
        drained += int64_t(events.size());
        events.clear();
        return;
    }
    int64_t behind = cursors[0].at;
    for (int64_t at = 1; at < int64_t(cursors.size()); at++) {
        if (cursors[at].at < behind) {
            behind = cursors[at].at;
        }
    }
    const int64_t drop = behind - drained;
    if (drop <= 0) {
        return;
    }
    for (int64_t at = 0; at < int64_t(events.size()) - drop; at++) {
        events[at] = events[at + drop];
    }
    events.resize(int64_t(events.size()) - drop);
    drained = behind;
}

void TrackerClient::close() {
    for (int64_t at = 0; at < int64_t(sockets.size()); at++) {
        gd::web_socket_close(sockets[at].peer);
    }
    sockets.clear();
    any_opened = false;
    last_poll_frame = -1;
}

TrackerBook &TrackerBook::shared() {
    static TrackerBook book;
    return book;
}

int TrackerBook::index_of(const String &p_key) const {
    for (int64_t at = 0; at < int64_t(rows.size()); at++) {
        if (rows[at].key == p_key) {
            return int(at);
        }
    }
    return -1;
}

TrackerClient *TrackerBook::acquire(
    const PackedStringArray &p_urls,
    Error &r_error
) {
    const String key = tracker_key(p_urls);
    const int at = index_of(key);
    if (at >= 0) {
        if (rows[at].client->is_active()) {
            rows[at].refs++;
            r_error = OK;
            return rows[at].client;
        }
        rows[at].client->close();
        memdelete(rows[at].client);
        rows.remove_at(at);
    }

    TrackerClient *made = memnew(TrackerClient);
    r_error = made->connect_to(p_urls);
    if (r_error != OK) {
        memdelete(made);
        return nullptr;
    }
    Row row;
    row.key = key;
    row.client = made;
    row.refs = 1;
    rows.push_back(row);
    return made;
}

void TrackerBook::release(
    const PackedStringArray &p_urls,
    TrackerClient *p_held
) {
    if (p_held == nullptr) {
        return;
    }
    const int at = index_of(tracker_key(p_urls));
    if (at < 0 || rows[at].client != p_held) {
        return;
    }
    rows[at].refs--;
    if (rows[at].refs > 0) {
        return;
    }
    rows[at].client->close();
    memdelete(rows[at].client);
    rows.remove_at(at);
}

void TrackerBook::clear() {
    for (int64_t at = 0; at < int64_t(rows.size()); at++) {
        rows[at].client->close();
        memdelete(rows[at].client);
    }
    rows.clear();
}

} // namespace netw::connect
