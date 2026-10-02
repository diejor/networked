#include "netw/store/world.hpp"

using namespace godot;

namespace netw::store {

void World::grow_slot_bits(uint32_t p_count) {
    live.resize(p_count);
    stale.resize(p_count);
    slot_stamps.resize(p_count);
    slot_stamps[p_count - 1] = 0;
    for (uint32_t peer = 0; peer < sides.size(); ++peer) {
        sides[peer].visible.resize(p_count);
        sides[peer].held.resize(p_count);
    }
}

void World::fresh_slot_bits(uint32_t p_slot) {
    live.set(p_slot, false);
    for (uint32_t peer = 0; peer < sides.size(); ++peer) {
        sides[peer].visible.set(p_slot, false);
        sides[peer].held.set(p_slot, false);
    }
    stale_slot(p_slot);
}

void World::write_bit(Bits &r_bits, uint32_t p_at, bool p_on) {
    if (r_bits.get(p_at) == p_on) {
        return;
    }
    r_bits.set(p_at, p_on);
#if defined(NETW_TESTS)
    interest_work.writes += 1;
#endif
}

uint32_t World::slot_of_route(int64_t p_route) const {
    const uint32_t *held = route_slot.getptr(p_route);
    return held != nullptr ? *held : Table::NO_SLOT;
}

void World::stale_slot(uint32_t p_slot) {
    if (p_slot >= stale.count || stale.get(p_slot)) {
        return;
    }
    stale.set(p_slot, true);
    stale_slots.push_back(p_slot);
}

void World::stale_route(int64_t p_route) {
    stale_slot(slot_of_route(p_route));
}

void World::stale_all() {
    for (uint32_t slot = 0; slot < slot_route.size(); ++slot) {
        if (slot_route[slot] != 0) {
            stale_slot(slot);
        }
    }
}

void World::set_live(uint32_t p_slot, bool p_live) {
    write_bit(live, p_slot, p_live);
}

void World::see(uint32_t p_slot, uint32_t p_peer, bool p_visible) {
    write_bit(sides[p_peer].visible, p_slot, p_visible);
}

void World::hold(uint32_t p_slot, uint32_t p_peer, bool p_held) {
    write_bit(sides[p_peer].held, p_slot, p_held);
}

void World::take_stale(LocalVector<uint32_t> &r_slots) {
    r_slots.clear();
    for (uint32_t at = 0; at < stale_slots.size(); ++at) {
        if (stale.get(stale_slots[at])) {
            r_slots.push_back(stale_slots[at]);
        }
    }
    stale_slots.clear();
}

void World::settle_slot(uint32_t p_slot) {
    if (p_slot < stale.count && stale.get(p_slot)) {
        stale.set(p_slot, false);
    }
    next_stamp += 1;
    slot_stamps[p_slot] = next_stamp;
#if defined(NETW_TESTS)
    interest_work.evaluations += 1;
#endif
}

bool World::reaches(uint32_t p_slot, uint32_t p_peer) const {
    return is_live(p_slot) && p_peer < sides.size() && sides[p_peer].reachable
        && sides[p_peer].visible.get(p_slot);
}

bool World::holds_copy(uint32_t p_slot, uint32_t p_peer) const {
    return p_peer < sides.size() && sides[p_peer].held.get(p_slot);
}

bool World::set_reachable(uint32_t p_peer, bool p_reachable) {
    if (p_peer >= sides.size() || sides[p_peer].reachable == p_reachable) {
        return false;
    }
    sides[p_peer].reachable = p_reachable;
    return true;
}

} // namespace netw::store
