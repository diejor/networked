#include "netw/predict/relay_book.hpp"

#include "godot/class_db.hpp"
#include "netw/predict/frames.hpp"

using namespace godot;

namespace netw {

void NetwPredictRelayBook::set_subscribed(
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

bool NetwPredictRelayBook::subscribed(int64_t p_entity_slot, int64_t p_peer)
    const {
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

PackedInt64Array NetwPredictRelayBook::peers(int64_t p_entity_slot) const {
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

int NetwPredictRelayBook::peer_count(int64_t p_entity_slot) const {
    const LocalVector<int64_t> *row = rows.getptr(p_entity_slot);
    return row ? int(row->size()) : 0;
}

void NetwPredictRelayBook::release(int64_t p_entity_slot) {
    rows.erase(p_entity_slot);
}

int NetwPredictRelayBook::slot_count() const {
    return int(rows.size());
}

PackedByteArray NetwPredictRelayBook::request_bytes(bool p_subscribed) {
    return predict::encode_relay_request(p_subscribed);
}

int NetwPredictRelayBook::request_of(const PackedByteArray &p_bytes) {
    bool subscribed = false;
    if (!predict::decode_relay_request(p_bytes, subscribed)) {
        return -1;
    }
    return subscribed ? 1 : 0;
}

void NetwPredictRelayBook::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictRelayBook",
        D_METHOD("request_bytes", "subscribed"),
        &NetwPredictRelayBook::request_bytes
    );
    ClassDB::bind_static_method(
        "NetwPredictRelayBook",
        D_METHOD("request_of", "bytes"),
        &NetwPredictRelayBook::request_of
    );
    ClassDB::bind_method(
        D_METHOD("set_subscribed", "entity_slot", "peer", "subscribed"),
        &NetwPredictRelayBook::set_subscribed
    );
    ClassDB::bind_method(
        D_METHOD("subscribed", "entity_slot", "peer"),
        &NetwPredictRelayBook::subscribed
    );
    ClassDB::bind_method(
        D_METHOD("peers", "entity_slot"),
        &NetwPredictRelayBook::peers
    );
    ClassDB::bind_method(
        D_METHOD("peer_count", "entity_slot"),
        &NetwPredictRelayBook::peer_count
    );
    ClassDB::bind_method(
        D_METHOD("release", "entity_slot"),
        &NetwPredictRelayBook::release
    );
    ClassDB::bind_method(
        D_METHOD("slot_count"),
        &NetwPredictRelayBook::slot_count
    );
}

} // namespace netw
