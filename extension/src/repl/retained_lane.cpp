#include "netw/repl/retained_lane.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

namespace netw::repl {

using namespace godot;

RetainedLane RetainedLane::open(const wire::WirePlan &p_plan) {
    RetainedLane lane;
    lane.compiled = p_plan;
    return lane;
}

RetainedLane RetainedLane::declare(const SchemaRecord &p_schema) {
    RetainedLane lane;
    lane.declaration = p_schema;
    lane.compiled = wire::WirePlan::compile(p_schema);
    return lane;
}

bool RetainedLane::gather(const Array &p_values, wire::CodeRow &r_row) const {
    NETW_ZONE_NC("Retained lane gather", colors::WIRE);
    return wire::gather_scalar_row(declaration, compiled, p_values, r_row);
}

bool RetainedLane::knows(int p_peer) const {
    const Peer *held = peers.getptr(p_peer);
    return held != nullptr && held->has_held;
}

RetainedLane::Delivery RetainedLane::send(
    int p_peer,
    const wire::CodeRow &p_row
) {
    NETW_ZONE_NC("Retained lane send", colors::WIRE);
    Delivery out;
    if (!compiled.valid() || !p_row.valid_for(compiled)) {
        return out;
    }
    Peer *peer = peers.getptr(p_peer);
    if (peer == nullptr) {
        peers.insert(p_peer, Peer());
        peer = peers.getptr(p_peer);
    }

    uint64_t mask = compiled.full_mask();
    if (peer->has_held) {
        mask = wire::CodeRow::changed_mask(compiled, peer->held, p_row);
    }
    if (mask == 0) {
        return out;
    }
    out.mask = mask;
    if (peer->has_held) {
        out.ordered_baseline.copy_from(peer->held);
        out.steps_from_baseline = true;
    }
    peer->held = p_row;
    peer->has_held = true;
    return out;
}

void RetainedLane::retain(const LocalVector<int> &p_recipients) {
    LocalVector<int> doomed;
    for (const KeyValue<int, Peer> &entry : peers) {
        bool keep = false;
        for (uint32_t at = 0; at < p_recipients.size(); ++at) {
            if (p_recipients[at] == entry.key) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            doomed.push_back(entry.key);
        }
    }
    for (uint32_t at = 0; at < doomed.size(); ++at) {
        peers.erase(doomed[at]);
    }
}

void RetainedLane::forget(int p_peer) {
    peers.erase(p_peer);
}

} // namespace netw::repl
