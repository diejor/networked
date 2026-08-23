#include "netw/predict/command_queue.hpp"

#include "netw/predict/drive.hpp"

using namespace godot;

namespace netw {

namespace predict {

bool CommandQueue::admit(
    int64_t p_transition,
    int64_t p_label,
    bool p_fresh,
    const Dictionary &p_command
) {
    if (cells.has(p_transition)) {
        return false;
    }
    CommandCell cell;
    cell.label = p_label;
    cell.fresh = p_fresh;
    cell.command = p_command;
    cells.insert(p_transition, cell);
    if (oldest < 0 || p_transition < oldest) {
        oldest = p_transition;
    }
    if (newest < 0 || p_transition > newest) {
        newest = p_transition;
    }
    while (int(cells.size()) > TAPE_HISTORY_LIMIT) {
        drop_oldest();
    }
    return true;
}

void CommandQueue::drop_oldest() {
    cells.erase(oldest);
    if (cells.is_empty()) {
        oldest = -1;
        newest = -1;
        return;
    }
    // The window gaps wherever a datagram was lost, so the next held
    // transition is found by walking rather than by adding one. The walk is
    // bounded by the span the ring can hold at all.
    for (int64_t at = oldest + 1; at <= newest; ++at) {
        if (cells.has(at)) {
            oldest = at;
            return;
        }
    }
    oldest = newest;
}

bool CommandQueue::has(int64_t p_transition) const {
    return cells.has(p_transition);
}

const CommandCell *CommandQueue::cell(int64_t p_transition) const {
    const HashMap<int64_t, CommandCell>::ConstIterator found
        = cells.find(p_transition);
    return found != cells.end() ? &found->value : nullptr;
}

int CommandQueue::depth_from(int64_t p_cursor) const {
    if (p_cursor < 0) {
        return 0;
    }
    int depth = 0;
    while (cells.has(p_cursor + depth)) {
        depth += 1;
    }
    return depth;
}

PackedInt64Array CommandQueue::transitions() const {
    PackedInt64Array out;
    if (cells.is_empty()) {
        return out;
    }
    for (int64_t at = oldest; at <= newest; ++at) {
        if (cells.has(at)) {
            out.push_back(at);
        }
    }
    return out;
}

void CommandQueue::clear() {
    cells.clear();
    oldest = -1;
    newest = -1;
}

} // namespace predict

} // namespace netw
