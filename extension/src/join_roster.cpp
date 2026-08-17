#include "netw/join_roster.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

LocalVector<int64_t> NetwJoinRoster::peers_in_order() const {
    LocalVector<int64_t> out;
    for (const KeyValue<int64_t, Ref<ResolvedJoin>> &entry : accepted) {
        out.push_back(entry.key);
    }
    out.sort();
    return out;
}

bool NetwJoinRoster::remember(const Ref<ResolvedJoin> &rj) {
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

Ref<ResolvedJoin> NetwJoinRoster::accepted_join(int64_t peer_id) const {
    const HashMap<int64_t, Ref<ResolvedJoin>>::ConstIterator found
        = accepted.find(peer_id);
    return found != accepted.end() ? found->value : Ref<ResolvedJoin>();
}

Array NetwJoinRoster::accepted_joins() const {
    Array out;
    for (const int64_t peer_id : peers_in_order()) {
        out.push_back(accepted[peer_id]);
    }
    return out;
}

Array NetwJoinRoster::serialize_accepted() const {
    Array out;
    for (const int64_t peer_id : peers_in_order()) {
        out.push_back(accepted[peer_id]->serialize());
    }
    return out;
}

int NetwJoinRoster::name_verdict(
    const StringName &name,
    const PackedStringArray &taken,
    bool is_debug,
    bool has_identity
) const {
    if (!taken.has(String(name))) {
        return ADMIT;
    }
    if (is_debug) {
        return RENAME;
    }
    return has_identity ? REFUSE : ADMIT;
}

StringName NetwJoinRoster::free_name(
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

void NetwJoinRoster::refuse(int64_t peer_id, const String &reason) {
    refusals[peer_id] = reason;
}

String NetwJoinRoster::refusal(int64_t peer_id) const {
    const HashMap<int64_t, String>::ConstIterator found
        = refusals.find(peer_id);
    return found != refusals.end() ? found->value : String();
}

void NetwJoinRoster::forget(int64_t peer_id) {
    accepted.erase(peer_id);
    refusals.erase(peer_id);
}

void NetwJoinRoster::clear() {
    accepted.clear();
    refusals.clear();
}

int NetwJoinRoster::size() const {
    return int(accepted.size());
}

void NetwJoinRoster::_bind_methods() {
    BIND_ENUM_CONSTANT(ADMIT);
    BIND_ENUM_CONSTANT(RENAME);
    BIND_ENUM_CONSTANT(REFUSE);

    ClassDB::bind_method(
        D_METHOD("remember", "rj"),
        &NetwJoinRoster::remember
    );
    ClassDB::bind_method(
        D_METHOD("accepted_join", "peer_id"),
        &NetwJoinRoster::accepted_join
    );
    ClassDB::bind_method(
        D_METHOD("accepted_joins"),
        &NetwJoinRoster::accepted_joins
    );
    ClassDB::bind_method(
        D_METHOD("serialize_accepted"),
        &NetwJoinRoster::serialize_accepted
    );
    ClassDB::bind_method(
        D_METHOD("name_verdict", "name", "taken", "is_debug", "has_identity"),
        &NetwJoinRoster::name_verdict
    );
    ClassDB::bind_method(
        D_METHOD("free_name", "name", "taken"),
        &NetwJoinRoster::free_name
    );
    ClassDB::bind_method(
        D_METHOD("refuse", "peer_id", "reason"),
        &NetwJoinRoster::refuse
    );
    ClassDB::bind_method(
        D_METHOD("refusal", "peer_id"),
        &NetwJoinRoster::refusal
    );
    ClassDB::bind_method(D_METHOD("forget", "peer_id"), &NetwJoinRoster::forget);
    ClassDB::bind_method(D_METHOD("clear"), &NetwJoinRoster::clear);
    ClassDB::bind_method(D_METHOD("size"), &NetwJoinRoster::size);
}

} // namespace netw
