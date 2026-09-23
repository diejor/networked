#include "netw/wire/snapshot_stream.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::wire {

int32_t SnapshotSender::slot_of(uint64_t p_revision) const {
    for (uint32_t at = 0; at < staged.size(); ++at) {
        if (staged[at].revision == p_revision) {
            return int32_t(at);
        }
    }
    return -1;
}

void SnapshotSender::evict_to_depth() {
    while (staged.size() > SNAPSHOT_STAGE_DEPTH) {
        uint32_t oldest = staged.size();
        for (uint32_t at = 0; at < staged.size(); ++at) {
            if (!staged[at].pinned) {
                oldest = at;
                break;
            }
        }
        if (oldest == staged.size()) {
            return;
        }
        staged.remove_at(oldest);
    }
}

void SnapshotSender::desire(const CodeRow &p_row) {
    if (has_desired && desired_row.equals(p_row)) {
        return;
    }
    desired_row.copy_from(p_row);
    has_desired = true;
    if (repair_at != 0) {
        unpin(repair_at);
        repair_at = 0;
    }
}

uint64_t SnapshotSender::reserve() {
    const uint64_t taken = next_revision;
    if (next_revision == UINT64_MAX) {
        return 0;
    }
    ++next_revision;
    return taken;
}

uint64_t SnapshotSender::mint(int64_t p_tick) {
    const uint64_t taken = reserve();
    if (taken != 0) {
        minted_tick = p_tick;
    }
    return taken;
}

bool SnapshotSender::beat_due(int64_t p_tick, int64_t p_every) const {
    return p_every > 0 && has_confirmed && quiet()
        && p_tick - minted_tick >= p_every;
}

uint64_t SnapshotSender::distance_for(uint64_t p_revision) const {
    if (!has_confirmed || p_revision <= confirmed_revision) {
        return 0;
    }
    const uint64_t span = p_revision - confirmed_revision;
    return span <= SNAPSHOT_DELTA_SPAN ? span : 0;
}

bool SnapshotSender::expose(uint64_t p_revision, const CodeRow &p_row) {
    NETW_ZONE_NC("Snapshot expose", colors::WIRE);
    if (p_revision < SNAPSHOT_FIRST_REVISION || p_revision >= next_revision) {
        return false;
    }
    if (p_revision > highest_exposed) {
        highest_exposed = p_revision;
    }
    if (slot_of(p_revision) >= 0) {
        return true;
    }
    Staged made;
    made.revision = p_revision;
    made.row.copy_from(p_row);
    staged.push_back(made);
    evict_to_depth();
    return true;
}

bool SnapshotSender::pin(uint64_t p_revision) {
    const int32_t found = slot_of(p_revision);
    if (found < 0) {
        return false;
    }
    staged[uint32_t(found)].pinned = true;
    return true;
}

bool SnapshotSender::unpin(uint64_t p_revision) {
    const int32_t found = slot_of(p_revision);
    if (found < 0) {
        return false;
    }
    staged[uint32_t(found)].pinned = false;
    evict_to_depth();
    return true;
}

ReceiptVerdict SnapshotSender::receipt(uint64_t p_revision) {
    NETW_ZONE_NC("Snapshot receipt", colors::WIRE);
    if (p_revision < SNAPSHOT_FIRST_REVISION || p_revision > highest_exposed) {
        NETW_DEBUG(
            sys::WIRE,
            "A receipt names revision %d, past the highest this stream has "
            "exposed at %d.",
            int(p_revision),
            int(highest_exposed)
        );
        return ReceiptVerdict::UNEXPOSED;
    }
    if (p_revision <= confirmed_revision) {
        return ReceiptVerdict::IGNORED;
    }
    const int32_t found = slot_of(p_revision);
    if (found < 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A receipt names revision %d, whose snapshot this stream has "
            "already evicted.",
            int(p_revision)
        );
        return ReceiptVerdict::EVICTED;
    }
    confirmed_row.copy_from(staged[uint32_t(found)].row);
    confirmed_revision = p_revision;
    has_confirmed = true;
    uint32_t at = 0;
    while (at < staged.size()) {
        if (staged[at].revision <= confirmed_revision) {
            staged.remove_at(at);
            continue;
        }
        ++at;
    }
    return ReceiptVerdict::PROMOTED;
}

bool SnapshotSender::quiet() const {
    if (confirmed_revision < highest_exposed) {
        return false;
    }
    if (!has_desired) {
        return true;
    }
    return has_confirmed && desired_row.equals(confirmed_row);
}

bool SnapshotSender::awaiting_receipt() const {
    if (!has_desired || quiet()) {
        return false;
    }
    const int32_t found = slot_of(highest_exposed);
    return found >= 0 && staged[uint32_t(found)].row.equals(desired_row);
}

bool SnapshotSender::withdraw(uint64_t p_revision) {
    const int32_t found = slot_of(p_revision);
    if (found < 0) {
        return false;
    }
    staged.remove_at(uint32_t(found));
    if (repair_at == p_revision) {
        repair_at = 0;
    }
    return true;
}

uint64_t SnapshotSender::pinned_repair() {
    if (repair_at != 0 && slot_of(repair_at) >= 0) {
        return repair_at;
    }
    if (!has_desired) {
        return 0;
    }
    const uint64_t taken = reserve();
    if (taken == 0) {
        return 0;
    }
    expose(taken, desired_row);
    pin(taken);
    repair_at = taken;
    return taken;
}

void SnapshotSender::reset() {
    staged.clear();
    confirmed_row.clear();
    desired_row.clear();
    next_revision = SNAPSHOT_FIRST_REVISION;
    confirmed_revision = 0;
    highest_exposed = 0;
    repair_at = 0;
    minted_tick = 0;
    has_confirmed = false;
    has_desired = false;
}

RowAdmission SnapshotReceiver::admits(uint64_t p_revision) const {
    if (p_revision < SNAPSHOT_FIRST_REVISION) {
        return RowAdmission::STALE;
    }
    if (!has_accepted || p_revision > accepted_revision) {
        return RowAdmission::ACCEPTABLE;
    }
    return p_revision == accepted_revision ? RowAdmission::DUPLICATE
                                           : RowAdmission::STALE;
}

const CodeRow *SnapshotReceiver::baseline(uint64_t p_revision) const {
    for (uint32_t at = 0; at < ring.size(); ++at) {
        if (ring[at].revision == p_revision) {
            return &ring[at].row;
        }
    }
    return nullptr;
}

bool SnapshotReceiver::commit(uint64_t p_revision, const CodeRow &p_row) {
    NETW_ZONE_NC("Snapshot commit", colors::WIRE);
    if (admits(p_revision) != RowAdmission::ACCEPTABLE) {
        return false;
    }
    Held made;
    made.revision = p_revision;
    made.row.copy_from(p_row);
    ring.push_back(made);
    while (ring.size() > SNAPSHOT_RING_DEPTH) {
        ring.remove_at(0);
    }
    accepted_row.copy_from(p_row);
    accepted_revision = p_revision;
    has_accepted = true;
    return true;
}

void SnapshotReceiver::reset() {
    ring.clear();
    accepted_row.clear();
    accepted_revision = 0;
    has_accepted = false;
}

} // namespace netw::wire
