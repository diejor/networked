#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::predict {

struct CommandCell {
    int64_t label = -1;
    bool fresh = false;
    godot::Dictionary command;
};

class CommandQueue {
    godot::HashMap<int64_t, CommandCell> cells;
    int64_t oldest = -1;
    int64_t newest = -1;

    void drop_oldest();

public:
    bool admit(
        int64_t p_transition,
        int64_t p_label,
        bool p_fresh,
        const godot::Dictionary &p_command
    );

    bool has(int64_t p_transition) const;

    const CommandCell *cell(int64_t p_transition) const;

    int depth_from(int64_t p_cursor) const;

    int size() const {
        return int(cells.size());
    }

    int64_t oldest_transition() const {
        return oldest;
    }

    int64_t newest_transition() const {
        return newest;
    }

    godot::PackedInt64Array transitions() const;

    void clear();
};

} // namespace netw::predict
