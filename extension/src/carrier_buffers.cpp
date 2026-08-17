#include "netw/carrier_buffers.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

namespace netw {

using namespace godot;

HashMap<int64_t, PackedByteArray> &NetwCarrierBuffers::lane(bool p_reliable) {
    return p_reliable ? reliable : unreliable;
}

const HashMap<int64_t, PackedByteArray> &NetwCarrierBuffers::lane(
    bool p_reliable
) const {
    return p_reliable ? reliable : unreliable;
}

PackedByteArray NetwCarrierBuffers::append(
    int64_t p_peer,
    const PackedByteArray &p_frame,
    bool p_reliable,
    int64_t p_budget
) {
    HashMap<int64_t, PackedByteArray> &runs = lane(p_reliable);
    PackedByteArray owed;
    PackedByteArray *run = runs.getptr(p_peer);
    if (run == nullptr) {
        runs.insert(p_peer, PackedByteArray());
        run = runs.getptr(p_peer);
    }

    const bool overflows = !p_reliable
        && !run->is_empty()
        && int64_t(run->size()) + int64_t(p_frame.size()) > p_budget;
    if (overflows) {
        NETW_TRACE(
            sys::TRANSPORT,
            "peer %d flushes early: %d held + %d frame over %d",
            p_peer,
            run->size(),
            p_frame.size(),
            p_budget
        );
        owed = *run;
        run->clear();
    }
    run->append_array(p_frame);
    return owed;
}

PackedByteArray NetwCarrierBuffers::take(int64_t p_peer, bool p_reliable) {
    HashMap<int64_t, PackedByteArray> &runs = lane(p_reliable);
    PackedByteArray *run = runs.getptr(p_peer);
    if (run == nullptr) {
        return PackedByteArray();
    }
    const PackedByteArray out = *run;
    runs.erase(p_peer);
    return out;
}

PackedInt32Array NetwCarrierBuffers::peers(bool p_reliable) const {
    PackedInt32Array out;
    for (const KeyValue<int64_t, PackedByteArray> &row : lane(p_reliable)) {
        if (!row.value.is_empty()) {
            out.push_back(int32_t(row.key));
        }
    }
    out.sort();
    return out;
}

int64_t NetwCarrierBuffers::pending(int64_t p_peer, bool p_reliable) const {
    const HashMap<int64_t, PackedByteArray>::ConstIterator found
        = lane(p_reliable).find(p_peer);
    return found != lane(p_reliable).end() ? int64_t(found->value.size()) : 0;
}

void NetwCarrierBuffers::clear() {
    unreliable.clear();
    reliable.clear();
}

void NetwCarrierBuffers::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("append", "peer", "frame", "reliable", "budget"),
        &NetwCarrierBuffers::append
    );
    ClassDB::bind_method(
        D_METHOD("take", "peer", "reliable"),
        &NetwCarrierBuffers::take
    );
    ClassDB::bind_method(D_METHOD("peers", "reliable"), &NetwCarrierBuffers::peers);
    ClassDB::bind_method(
        D_METHOD("pending", "peer", "reliable"),
        &NetwCarrierBuffers::pending
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwCarrierBuffers::clear);
}

} // namespace netw
