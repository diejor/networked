#pragma once

#include "netw_test.h"

#include "godot/variant.hpp"
#include "netw/api/event_plane.hpp"

namespace netw_test {

class EventRing {
    godot::Array rows;

public:
    EventRing() = default;
    explicit EventRing(const godot::Array &p_rows) : rows(p_rows) {
    }

    int size() const {
        return int(rows.size());
    }

    bool is_empty() const {
        return rows.is_empty();
    }

    godot::Dictionary at(int p_index) const {
        if (p_index < 0 || p_index >= int(rows.size())) {
            return godot::Dictionary();
        }
        return godot::Dictionary(rows[p_index]);
    }

    int64_t tick_at(int p_index) const {
        const godot::Dictionary row = at(p_index);
        return !row.is_empty() ? int64_t(row[netw::event_key::tick()]) : -1;
    }

    int64_t event_at(int p_index) const {
        const godot::Dictionary row = at(p_index);
        return !row.is_empty() ? int64_t(row[netw::event_key::event()]) : -1;
    }
};

} // namespace netw_test
