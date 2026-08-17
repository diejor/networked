#include "netw/repl/lane_set.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

RowLane *LaneSet::open(
    int64_t p_route,
    uint8_t p_comp,
    const Ref<SchemaRecord> &p_schema
) {
    const uint64_t key = address(p_route, p_comp);
    if (RowLane *held = lanes.getptr(key)) {
        return held;
    }
    lanes.insert(key, RowLane::open(p_schema));
    return lanes.getptr(key);
}

RowLane *LaneSet::find(int64_t p_route, uint8_t p_comp) {
    return lanes.getptr(address(p_route, p_comp));
}

RetainedLane *LaneSet::open_retained(
    int64_t p_route,
    uint8_t p_comp,
    const Ref<SchemaRecord> &p_schema
) {
    const uint64_t key = address(p_route, p_comp);
    if (RetainedLane *held = retained.getptr(key)) {
        return held;
    }
    retained.insert(key, RetainedLane::declare(p_schema));
    return retained.getptr(key);
}

RetainedLane *LaneSet::find_retained(int64_t p_route, uint8_t p_comp) {
    return retained.getptr(address(p_route, p_comp));
}

WindowRing *LaneSet::open_window(
    int64_t p_route,
    uint8_t p_comp,
    const Ref<SchemaRecord> &p_schema,
    uint32_t p_depth
) {
    const uint64_t key = address(p_route, p_comp);
    if (WindowRing *held = windows.getptr(key)) {
        return held;
    }
    windows.insert(key, WindowRing::declare(p_schema, p_depth));
    return windows.getptr(key);
}

void LaneSet::close_route(int64_t p_route) {
    const uint64_t low = address(p_route, 0);
    const uint64_t high = address(p_route, 255);
    LocalVector<uint64_t> doomed;
    for (const KeyValue<uint64_t, RowLane> &entry : lanes) {
        if (entry.key >= low && entry.key <= high) {
            doomed.push_back(entry.key);
        }
    }
    for (uint32_t at = 0; at < doomed.size(); ++at) {
        lanes.erase(doomed[at]);
    }
    doomed.clear();
    for (const KeyValue<uint64_t, RetainedLane> &entry : retained) {
        if (entry.key >= low && entry.key <= high) {
            doomed.push_back(entry.key);
        }
    }
    for (uint32_t at = 0; at < doomed.size(); ++at) {
        retained.erase(doomed[at]);
    }
    doomed.clear();
    for (const KeyValue<uint64_t, WindowRing> &entry : windows) {
        if (entry.key >= low && entry.key <= high) {
            doomed.push_back(entry.key);
        }
    }
    for (uint32_t at = 0; at < doomed.size(); ++at) {
        windows.erase(doomed[at]);
    }
}

void LaneSet::retain(const LocalVector<int> &p_recipients) {
    NETW_ZONE_NC("Lane set retain", colors::WIRE);
    for (KeyValue<uint64_t, RowLane> &entry : lanes) {
        entry.value.retain(p_recipients);
    }
    for (KeyValue<uint64_t, RetainedLane> &entry : retained) {
        entry.value.retain(p_recipients);
    }
}

void LaneSet::retain_row(
    int64_t p_route,
    uint8_t p_comp,
    const LocalVector<int> &p_recipients
) {
    RowLane *lane = find(p_route, p_comp);
    if (lane != nullptr) {
        lane->retain(p_recipients);
    }
    RetainedLane *reliable = find_retained(p_route, p_comp);
    if (reliable != nullptr) {
        reliable->retain(p_recipients);
    }
}

void LaneSet::forget_peer(int p_peer) {
    NETW_ZONE_NC("Lane set forget", colors::WIRE);
    for (KeyValue<uint64_t, RowLane> &entry : lanes) {
        entry.value.forget(p_peer);
    }
    for (KeyValue<uint64_t, RetainedLane> &entry : retained) {
        entry.value.forget(p_peer);
    }
}

void LaneSet::acknowledge_peer(int p_peer, uint16_t p_acked_seq) {
    NETW_ZONE_NC("Lane set acknowledge", colors::WIRE);
    for (KeyValue<uint64_t, RowLane> &entry : lanes) {
        entry.value.acknowledge(p_peer, p_acked_seq);
    }
}

void LaneSet::clear() {
    lanes.clear();
    retained.clear();
    windows.clear();
}

} // namespace netw::repl
