#include "netw/join_roster.hpp"

#include "netw/session/frames.hpp"

using namespace godot;

namespace netw {

LocalVector<int64_t> JoinRoster::peers_in_order() const {
    LocalVector<int64_t> out;
    for (const KeyValue<int64_t, session::AcceptFrame> &entry : accepted) {
        out.push_back(entry.key);
    }
    out.sort();
    return out;
}

bool JoinRoster::remember(const session::AcceptFrame &p_entry) {
    if (p_entry.peer_id == 0 || p_entry.membership == 0) {
        return false;
    }
    const HashMap<int64_t, session::AcceptFrame>::ConstIterator found
        = accepted.find(p_entry.peer_id);
    if (found != accepted.end()
        && found->value.membership == p_entry.membership) {
        return false;
    }
    accepted[p_entry.peer_id] = p_entry;
    return true;
}

bool JoinRoster::has_accepted(int64_t p_peer) const {
    return accepted.has(p_peer);
}

session::AcceptFrame JoinRoster::accepted_join(int64_t p_peer) const {
    const HashMap<int64_t, session::AcceptFrame>::ConstIterator found
        = accepted.find(p_peer);
    return found != accepted.end() ? found->value : session::AcceptFrame();
}

LocalVector<session::AcceptFrame> JoinRoster::accepted_joins() const {
    LocalVector<session::AcceptFrame> out;
    for (const int64_t peer : peers_in_order()) {
        out.push_back(accepted[peer]);
    }
    return out;
}

PackedByteArray JoinRoster::roster_frame() const {
    return session::roster_write(accepted_joins());
}

PackedByteArray JoinRoster::accept_frame(int64_t p_peer) const {
    const HashMap<int64_t, session::AcceptFrame>::ConstIterator found
        = accepted.find(p_peer);
    if (found == accepted.end()) {
        return PackedByteArray();
    }
    return session::frame_write(found->value);
}

int JoinRoster::name_verdict(
    const StringName &p_name,
    const PackedStringArray &p_taken,
    bool p_renames_on_collision,
    bool p_has_identity
) const {
    if (!p_taken.has(String(p_name))) {
        return ADMIT;
    }
    if (p_has_identity) {
        return REFUSE;
    }
    return p_renames_on_collision ? RENAME : ADMIT;
}

StringName JoinRoster::free_name(
    const StringName &p_name,
    const PackedStringArray &p_taken
) const {
    int suffix = 1;
    String candidate = String(p_name) + String::num_int64(suffix);
    while (p_taken.has(candidate)) {
        suffix += 1;
        candidate = String(p_name) + String::num_int64(suffix);
    }
    return StringName(candidate);
}

void JoinRoster::refuse(int64_t p_peer, const String &p_reason) {
    refusals[p_peer] = p_reason;
}

String JoinRoster::refusal(int64_t p_peer) const {
    const HashMap<int64_t, String>::ConstIterator found
        = refusals.find(p_peer);
    return found != refusals.end() ? found->value : String();
}

void JoinRoster::forget(int64_t p_peer) {
    accepted.erase(p_peer);
    refusals.erase(p_peer);
}

void JoinRoster::clear() {
    accepted.clear();
    refusals.clear();
}

int JoinRoster::size() const {
    return int(accepted.size());
}

} // namespace netw
