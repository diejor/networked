#include "netw/repl/entity_book.hpp"

namespace netw::repl {

using namespace godot;

void EntityBook::drop(const EntitySlot &p_slot) {
    uint32_t kept = 0;
    for (uint32_t at = 0; at < waiting.size(); ++at) {
        if (waiting[at].slot.same_as(p_slot)) {
            continue;
        }
        if (kept != at) {
            waiting[kept] = waiting[at];
        }
        ++kept;
    }
    waiting.resize(kept);
}

void EntityBook::bind_route(int64_t p_route) {
    bound[p_route] = true;
}

void EntityBook::tombstone_route(int64_t p_route) {
    bound[p_route] = false;
}

bool EntityBook::is_bound(int64_t p_route) const {
    const bool *held = bound.getptr(p_route);
    return held != nullptr && *held;
}

int64_t EntityBook::resolve(const EntitySlot &p_slot, int64_t p_wanted) {
    drop(p_slot);
    if (p_wanted <= 0) {
        return 0;
    }
    if (is_bound(p_wanted)) {
        return p_wanted;
    }
    const bool *held = bound.getptr(p_wanted);
    if (held != nullptr) {
        return 0;
    }
    Entry entry;
    entry.slot = p_slot;
    entry.wanted = p_wanted;
    waiting.push_back(entry);
    return 0;
}

void EntityBook::release(
    int64_t p_route,
    LocalVector<EntityWrite> &r_completed
) {
    r_completed.clear();
    const bool live = is_bound(p_route);
    uint32_t kept = 0;
    for (uint32_t at = 0; at < waiting.size(); ++at) {
        if (waiting[at].wanted != p_route) {
            if (kept != at) {
                waiting[kept] = waiting[at];
            }
            ++kept;
            continue;
        }
        EntityWrite done;
        done.slot = waiting[at].slot;
        done.route = live ? p_route : 0;
        r_completed.push_back(done);
    }
    waiting.resize(kept);
}

void EntityBook::forget_slot(int64_t p_route, uint8_t p_comp) {
    uint32_t kept = 0;
    for (uint32_t at = 0; at < waiting.size(); ++at) {
        const EntitySlot &slot = waiting[at].slot;
        if (slot.route == p_route && slot.comp == p_comp) {
            continue;
        }
        if (kept != at) {
            waiting[kept] = waiting[at];
        }
        ++kept;
    }
    waiting.resize(kept);
}

uint32_t EntityBook::pending_count() const {
    return waiting.size();
}

void EntityBook::clear() {
    waiting.clear();
    bound.clear();
}

} // namespace netw::repl
