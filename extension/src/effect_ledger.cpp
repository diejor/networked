#include "netw/effect_ledger.hpp"

#include "godot/class_db.hpp"
#include "godot/local_vector.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

void NetwEffectLedger::arm(
    const StringName &p_key,
    const Callable &p_revert,
    int64_t p_deadline_tick
) {
    NETW_ZONE_NC("NetwEffectLedger arm", colors::PREDICTION);
    if (p_key.is_empty()) {
        return;
    }
    Entry entry;
    entry.revert = p_revert;
    entry.deadline_tick = p_deadline_tick;
    entries[p_key] = entry;
    NETW_TRACE(
        sys::PREDICTION,
        "effect armed key=%s deadline=%d",
        String(p_key),
        p_deadline_tick
    );
}

bool NetwEffectLedger::watch(
    const StringName &p_key,
    const Callable &p_confirmed,
    const Callable &p_denied
) {
    NETW_ZONE_NC("NetwEffectLedger watch", colors::PREDICTION);
    if (p_key.is_empty() || !entries.has(p_key)) {
        NETW_TRACE(
            sys::PREDICTION,
            "effect watch refused, key not armed key=%s",
            String(p_key)
        );
        return false;
    }
    Watcher watcher;
    watcher.confirmed = p_confirmed;
    watcher.denied = p_denied;
    watchers[p_key] = watcher;
    return true;
}

void NetwEffectLedger::adopt(const StringName &p_key) {
    NETW_ZONE_NC("NetwEffectLedger adopt", colors::PREDICTION);
    resolve(p_key, true);
}

void NetwEffectLedger::discard(const StringName &p_key) {
    NETW_ZONE_NC("NetwEffectLedger discard", colors::PREDICTION);
    resolve(p_key, false);
}

void NetwEffectLedger::resolve(const StringName &p_key, bool p_keep) {
    const Entry *found = entries.getptr(p_key);
    if (found == nullptr) {
        return;
    }
    const Callable revert = found->revert;
    entries.erase(p_key);

    Callable observer;
    const Watcher *watching = watchers.getptr(p_key);
    if (watching != nullptr) {
        observer = p_keep ? watching->confirmed : watching->denied;
        watchers.erase(p_key);
    }

    NETW_TRACE(
        sys::PREDICTION,
        "effect resolved key=%s keep=%d",
        String(p_key),
        int(p_keep)
    );
    if (!p_keep && revert.is_valid()) {
        revert.call();
    }
    if (observer.is_valid()) {
        observer.call();
    }
}

bool NetwEffectLedger::pending(const StringName &p_key) const {
    return entries.has(p_key);
}

int64_t NetwEffectLedger::count() const {
    return int64_t(entries.size());
}

void NetwEffectLedger::sweep(int64_t p_tick) {
    NETW_ZONE_NC("NetwEffectLedger sweep", colors::PREDICTION);
    if (entries.is_empty()) {
        return;
    }
    LocalVector<StringName> expired;
    for (const KeyValue<StringName, Entry> &row : entries) {
        if (row.value.deadline_tick <= p_tick) {
            expired.push_back(row.key);
        }
    }
    for (const StringName &key : expired) {
        resolve(key, false);
    }
}

void NetwEffectLedger::clear() {
    entries.clear();
    watchers.clear();
}

void NetwEffectLedger::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("arm", "key", "revert", "deadline_tick"),
        &NetwEffectLedger::arm
    );
    ClassDB::bind_method(
        D_METHOD("watch", "key", "confirmed", "denied"),
        &NetwEffectLedger::watch
    );
    ClassDB::bind_method(D_METHOD("adopt", "key"), &NetwEffectLedger::adopt);
    ClassDB::bind_method(
        D_METHOD("discard", "key"),
        &NetwEffectLedger::discard
    );
    ClassDB::bind_method(
        D_METHOD("pending", "key"),
        &NetwEffectLedger::pending
    );
    ClassDB::bind_method(D_METHOD("count"), &NetwEffectLedger::count);
    ClassDB::bind_method(D_METHOD("sweep", "tick"), &NetwEffectLedger::sweep);
    ClassDB::bind_method(D_METHOD("clear"), &NetwEffectLedger::clear);
}

} // namespace netw
