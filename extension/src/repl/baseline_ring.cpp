#include "netw/repl/baseline_ring.hpp"

using namespace godot;

namespace netw::repl {

void BaselineRing::record(uint16_t p_seq, const wire::CodeRow &p_row) {
    if (slots.is_empty()) {
        slots.resize(DEPTH);
    }
    const bool ahead = !seated || uint16_t(p_seq - newest) < 32768;
    if (!ahead && uint16_t(newest - p_seq) >= DEPTH) {
        return;
    }
    if (ahead) {
        newest = p_seq;
        seated = true;
    }
    for (uint32_t at = 0; at < slots.size(); ++at) {
        if (slots[at].held && uint16_t(newest - slots[at].seq) >= DEPTH) {
            slots[at].held = false;
        }
    }
    Slot &slot = slots[p_seq % DEPTH];
    slot.seq = p_seq;
    slot.held = true;
    slot.row.copy_from(p_row);
}

const wire::CodeRow *BaselineRing::resolve(
    uint16_t p_seq,
    uint8_t p_base_low
) const {
    if (slots.is_empty()) {
        return nullptr;
    }
    const uint8_t back = uint8_t(uint8_t(p_seq) - p_base_low);
    if (back >= DEPTH) {
        return nullptr;
    }
    const uint16_t wanted = uint16_t(p_seq - uint16_t(back));
    const Slot &slot = slots[wanted % DEPTH];
    if (!slot.held || slot.seq != wanted) {
        return nullptr;
    }
    return &slot.row;
}

uint32_t BaselineRing::count() const {
    uint32_t live = 0;
    for (uint32_t at = 0; at < slots.size(); ++at) {
        live += slots[at].held ? 1 : 0;
    }
    return live;
}

void BaselineRing::clear() {
    slots.clear();
    seated = false;
    newest = 0;
}

} // namespace netw::repl
