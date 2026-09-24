#include "netw/spawn/park.hpp"

#include "netw/liveness_core.hpp"

using namespace godot;

namespace netw::spawn {

bool Park::park(
    int64_t route,
    const PackedByteArray &payload,
    Wait wait,
    int64_t deadline,
    int64_t sender
) {
    if (rows.has(route)) {
        return false;
    }
    Row row;
    row.payload = payload;
    row.wait = wait;
    row.deadline = deadline;
    row.sender = sender;
    rows.insert(route, row);
    return true;
}

bool Park::has(int64_t route) const {
    return rows.has(route);
}

PackedByteArray Park::take(int64_t route) {
    const HashMap<int64_t, Row>::Iterator found = rows.find(route);
    if (!found) {
        return PackedByteArray();
    }
    const PackedByteArray payload = found->value.payload;
    rows.remove(found);
    return payload;
}

PackedByteArray Park::peek(int64_t route) const {
    const HashMap<int64_t, Row>::ConstIterator found = rows.find(route);
    return found ? found->value.payload : PackedByteArray();
}

int64_t Park::sender_of(int64_t route) const {
    const HashMap<int64_t, Row>::ConstIterator found = rows.find(route);
    return found ? found->value.sender : 0;
}

bool Park::cancel(int64_t route) {
    reparents.erase(route);
    return rows.erase(route);
}

bool Park::keep_reparent(
    int64_t route,
    const PackedByteArray &payload,
    uint64_t revision,
    int64_t sender
) {
    if (!rows.has(route)) {
        return false;
    }
    const HashMap<int64_t, Reparent>::Iterator kept = reparents.find(route);
    if (kept && kept->value.revision >= revision) {
        return false;
    }
    Reparent row;
    row.payload = payload;
    row.revision = revision;
    row.sender = sender;
    reparents.insert(route, row);
    return true;
}

PackedByteArray Park::take_reparent(int64_t route, int64_t &r_sender) {
    const HashMap<int64_t, Reparent>::Iterator found = reparents.find(route);
    if (!found) {
        return PackedByteArray();
    }
    const PackedByteArray payload = found->value.payload;
    r_sender = found->value.sender;
    reparents.remove(found);
    return payload;
}

PackedInt64Array Park::waiting_on(Wait wait) const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Row> &row : rows) {
        if (row.value.wait == wait) {
            out.push_back(row.key);
        }
    }
    return out;
}

bool Park::is_expired(int64_t route, int64_t now) const {
    const HashMap<int64_t, Row>::ConstIterator found = rows.find(route);
    if (!found || found->value.deadline <= 0) {
        return false;
    }
    return now >= found->value.deadline;
}

int Park::size() const {
    return int(rows.size());
}

void Park::clear() {
    rows.clear();
    reparents.clear();
}

bool Park::anchor_parks(int64_t state) {
    return state == NetwLivenessCore::STATE_UNKNOWN
        || state == NetwLivenessCore::STATE_ABSENT;
}

} // namespace netw::spawn
