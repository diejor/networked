#include "netw/join_roster.hpp"

#include "netw/session/frames.hpp"

using namespace godot;

namespace netw {

LocalVector<int64_t> JoinRoster::peers_in_order() const {
    LocalVector<int64_t> out;
    for (const KeyValue<int64_t, Ref<ResolvedJoin>> &entry : accepted) {
        out.push_back(entry.key);
    }
    out.sort();
    return out;
}

bool JoinRoster::remember(const Ref<ResolvedJoin> &rj) {
    if (rj.is_null()) {
        return false;
    }
    const int64_t peer_id = rj->get_peer_id();
    const HashMap<int64_t, Ref<ResolvedJoin>>::ConstIterator found
        = accepted.find(peer_id);
    if (found != accepted.end()) {
        const bool enriches = found->value->get_arg_values().is_empty()
            && !rj->get_arg_values().is_empty();
        if (!enriches) {
            return false;
        }
    }
    accepted[peer_id] = rj;
    return true;
}

Ref<ResolvedJoin> JoinRoster::accepted_join(int64_t peer_id) const {
    const HashMap<int64_t, Ref<ResolvedJoin>>::ConstIterator found
        = accepted.find(peer_id);
    return found != accepted.end() ? found->value : Ref<ResolvedJoin>();
}

Array JoinRoster::accepted_joins() const {
    Array out;
    for (const int64_t peer_id : peers_in_order()) {
        out.push_back(accepted[peer_id]);
    }
    return out;
}

PackedByteArray JoinRoster::roster_frame() const {
    LocalVector<AcceptFrame> rows;
    for (const int64_t peer_id : peers_in_order()) {
        rows.push_back(accepted[peer_id]->accept_frame());
    }
    return session::roster_write(rows);
}

int JoinRoster::name_verdict(
    const StringName &name,
    const PackedStringArray &taken,
    bool renames_on_collision,
    bool has_identity
) const {
    if (!taken.has(String(name))) {
        return ADMIT;
    }
    if (has_identity) {
        return REFUSE;
    }
    return renames_on_collision ? RENAME : ADMIT;
}

StringName JoinRoster::free_name(
    const StringName &name,
    const PackedStringArray &taken
) const {
    int suffix = 1;
    String candidate = String(name) + String::num_int64(suffix);
    while (taken.has(candidate)) {
        suffix += 1;
        candidate = String(name) + String::num_int64(suffix);
    }
    return StringName(candidate);
}

void JoinRoster::refuse(int64_t peer_id, const String &reason) {
    refusals[peer_id] = reason;
}

String JoinRoster::refusal(int64_t peer_id) const {
    const HashMap<int64_t, String>::ConstIterator found
        = refusals.find(peer_id);
    return found != refusals.end() ? found->value : String();
}

void JoinRoster::forget(int64_t peer_id) {
    accepted.erase(peer_id);
    refusals.erase(peer_id);
}

void JoinRoster::clear() {
    accepted.clear();
    refusals.clear();
}

int JoinRoster::size() const {
    return int(accepted.size());
}

} // namespace netw
