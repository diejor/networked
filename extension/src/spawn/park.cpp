#include "netw/spawn/park.hpp"

#include "netw/liveness_core.hpp"

using namespace godot;

namespace netw::spawn {

bool Park::park(
    int64_t route,
    const PackedByteArray &payload,
    Wait wait,
    int64_t deadline
) {
    if (rows.has(route)) {
        return false;
    }
    Row row;
    row.payload = payload;
    row.wait = wait;
    row.deadline = deadline;
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

bool Park::cancel(int64_t route) {
    return rows.erase(route);
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
}

bool Park::anchor_parks(int64_t state) {
    return state == NetwLivenessCore::STATE_UNKNOWN
        || state == NetwLivenessCore::STATE_ABSENT;
}

} // namespace netw::spawn
