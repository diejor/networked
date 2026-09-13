#include "netw/predict/relay_book.hpp"

#include "netw/predict/frames.hpp"

using namespace godot;

namespace netw::predict {

void RelayBook::set_subscribed(
    int64_t p_entity_slot,
    int64_t p_peer,
    bool p_subscribed
) {
    LocalVector<int64_t> *row = rows.getptr(p_entity_slot);
    if (!p_subscribed) {
        if (!row) {
            return;
        }
        for (uint32_t at = 0; at < row->size(); ++at) {
            if ((*row)[at] == p_peer) {
                row->remove_at(at);
                break;
            }
        }
        if (row->is_empty()) {
            rows.erase(p_entity_slot);
        }
        return;
    }
    if (!row) {
        rows.insert(p_entity_slot, LocalVector<int64_t>());
        row = rows.getptr(p_entity_slot);
    }
    for (uint32_t at = 0; at < row->size(); ++at) {
        if ((*row)[at] == p_peer) {
            return;
        }
    }
    row->push_back(p_peer);
}

bool RelayBook::subscribed(int64_t p_entity_slot, int64_t p_peer) const {
    const LocalVector<int64_t> *row = rows.getptr(p_entity_slot);
    if (!row) {
        return false;
    }
    for (uint32_t at = 0; at < row->size(); ++at) {
        if ((*row)[at] == p_peer) {
            return true;
        }
    }
    return false;
}

PackedInt64Array RelayBook::peers(int64_t p_entity_slot) const {
    PackedInt64Array out;
    const LocalVector<int64_t> *row = rows.getptr(p_entity_slot);
    if (!row) {
        return out;
    }
    for (uint32_t at = 0; at < row->size(); ++at) {
        out.push_back((*row)[at]);
    }
    return out;
}

int RelayBook::peer_count(int64_t p_entity_slot) const {
    const LocalVector<int64_t> *row = rows.getptr(p_entity_slot);
    return row ? int(row->size()) : 0;
}

void RelayBook::release(int64_t p_entity_slot) {
    rows.erase(p_entity_slot);
}

int RelayBook::slot_count() const {
    return int(rows.size());
}

PackedByteArray RelayBook::request_bytes(bool p_subscribed) {
    return encode_relay_request(p_subscribed);
}

int RelayBook::request_of(const PackedByteArray &p_bytes) {
    bool subscribed = false;
    if (!decode_relay_request(p_bytes, subscribed)) {
        return -1;
    }
    return subscribed ? 1 : 0;
}

} // namespace netw::predict
