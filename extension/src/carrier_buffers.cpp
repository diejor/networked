#include "netw/carrier_buffers.hpp"

#include "netw/log.hpp"

namespace netw {

using namespace godot;

void CarrierBatch::take_frame(const PackedByteArray &p_frame) {
    held.append_array(p_frame);
    frames += 1;
}

void CarrierBatch::attach(const CarrierRow &p_row) {
    bits += p_row.bits;
    for (uint32_t at = 0; at < descriptors.size(); ++at) {
        if (descriptors[at].token != p_row.token
            || descriptors[at].route != p_row.route
            || descriptors[at].comp != p_row.comp) {
            continue;
        }
        if (p_row.revision >= descriptors[at].revision) {
            descriptors[at].copy_from(p_row);
        }
        return;
    }
    descriptors.resize(descriptors.size() + 1);
    descriptors[descriptors.size() - 1].copy_from(p_row);
}

void CarrierBatch::close_route(int64_t p_route) {
    uint32_t kept = 0;
    for (uint32_t at = 0; at < descriptors.size(); ++at) {
        if (descriptors[at].route == p_route) {
            continue;
        }
        if (kept != at) {
            descriptors[kept].copy_from(descriptors[at]);
        }
        ++kept;
    }
    descriptors.resize(kept);
}

void CarrierBatch::release() {
    held.clear();
    descriptors.clear();
    frames = 0;
    bits = 0;
}

HashMap<int64_t, CarrierBatch> &NetwCarrierBuffers::lane(bool p_reliable) {
    return p_reliable ? reliable : unreliable;
}

const HashMap<int64_t, CarrierBatch> &NetwCarrierBuffers::lane(bool p_reliable
) const {
    return p_reliable ? reliable : unreliable;
}

CarrierBatch NetwCarrierBuffers::append(
    int64_t p_peer,
    const PackedByteArray &p_frame,
    bool p_reliable,
    int64_t p_budget,
    const CarrierRow *p_row
) {
    HashMap<int64_t, CarrierBatch> &runs = lane(p_reliable);
    CarrierBatch owed;
    CarrierBatch *run = runs.getptr(p_peer);
    if (run == nullptr) {
        runs.insert(p_peer, CarrierBatch());
        run = runs.getptr(p_peer);
    }

    const bool overflows = !p_reliable && !run->is_empty()
        && run->size() + int64_t(p_frame.size()) > p_budget;
    if (overflows) {
        NETW_TRACE(
            sys::TRANSPORT,
            "peer %d flushes early: %d held + %d frame over %d",
            p_peer,
            int(run->size()),
            p_frame.size(),
            p_budget
        );
        owed = *run;
        run->release();
    }
    run->take_frame(p_frame);
    if (p_row != nullptr) {
        run->attach(*p_row);
    }
    return owed;
}

CarrierBatch NetwCarrierBuffers::take(int64_t p_peer, bool p_reliable) {
    HashMap<int64_t, CarrierBatch> &runs = lane(p_reliable);
    CarrierBatch *run = runs.getptr(p_peer);
    if (run == nullptr) {
        return CarrierBatch();
    }
    const CarrierBatch out = *run;
    runs.erase(p_peer);
    return out;
}

void NetwCarrierBuffers::close_route(int64_t p_route) {
    for (KeyValue<int64_t, CarrierBatch> &row : unreliable) {
        row.value.close_route(p_route);
    }
    for (KeyValue<int64_t, CarrierBatch> &row : reliable) {
        row.value.close_route(p_route);
    }
}

PackedInt32Array NetwCarrierBuffers::peers(bool p_reliable) const {
    PackedInt32Array out;
    for (const KeyValue<int64_t, CarrierBatch> &row : lane(p_reliable)) {
        if (!row.value.is_empty()) {
            out.push_back(int32_t(row.key));
        }
    }
    out.sort();
    return out;
}

int64_t NetwCarrierBuffers::pending(int64_t p_peer, bool p_reliable) const {
    const HashMap<int64_t, CarrierBatch>::ConstIterator found
        = lane(p_reliable).find(p_peer);
    return found != lane(p_reliable).end() ? found->value.size() : 0;
}

void NetwCarrierBuffers::clear() {
    unreliable.clear();
    reliable.clear();
}

} // namespace netw
