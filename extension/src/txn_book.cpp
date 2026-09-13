#include "netw/txn_book.hpp"

using namespace godot;

namespace netw {

LocalVector<int64_t> NetwTxnBook::ids_in_order() const {
    LocalVector<int64_t> out;
    for (const KeyValue<int64_t, Txn> &entry : open_txns) {
        out.push_back(entry.key);
    }
    out.sort();
    return out;
}

int64_t NetwTxnBook::mint() {
    const int64_t id = next_id;
    next_id += 1;
    return id;
}

void NetwTxnBook::open(
    int64_t txn_id,
    const PackedInt64Array &p_addressed,
    int64_t deadline
) {
    Txn txn;
    txn.addressed = p_addressed;
    txn.deadline = deadline;
    open_txns[txn_id] = txn;
}

bool NetwTxnBook::is_open(int64_t txn_id) const {
    return open_txns.has(txn_id);
}

bool NetwTxnBook::admits(int64_t txn_id, int64_t sender) const {
    const HashMap<int64_t, Txn>::ConstIterator found = open_txns.find(txn_id);
    if (found == open_txns.end()) {
        return false;
    }
    return found->value.addressed.has(sender);
}

PackedInt64Array NetwTxnBook::addressed(int64_t txn_id) const {
    const HashMap<int64_t, Txn>::ConstIterator found = open_txns.find(txn_id);
    return found != open_txns.end() ? found->value.addressed
                                    : PackedInt64Array();
}

bool NetwTxnBook::close(int64_t txn_id) {
    return open_txns.erase(txn_id);
}

PackedInt64Array NetwTxnBook::expire(int64_t current) {
    PackedInt64Array out;
    for (const int64_t id : ids_in_order()) {
        if (current >= open_txns[id].deadline) {
            out.push_back(id);
        }
    }
    for (int index = 0; index < out.size(); ++index) {
        open_txns.erase(out[index]);
    }
    return out;
}

PackedInt64Array NetwTxnBook::waiting_on(int64_t peer_id) const {
    PackedInt64Array out;
    for (const int64_t id : ids_in_order()) {
        if (open_txns[id].addressed.has(peer_id)) {
            out.push_back(id);
        }
    }
    return out;
}

PackedInt64Array NetwTxnBook::drain() {
    PackedInt64Array out;
    for (const int64_t id : ids_in_order()) {
        out.push_back(id);
    }
    open_txns.clear();
    return out;
}

int NetwTxnBook::size() const {
    return int(open_txns.size());
}

} // namespace netw
