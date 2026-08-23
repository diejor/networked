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

    godot::Ref<netw::NetwEvent> at(int p_index) const {
        if (p_index < 0 || p_index >= int(rows.size())) {
            return godot::Ref<netw::NetwEvent>();
        }
        return godot::Ref<netw::NetwEvent>(rows[p_index]);
    }

    int64_t tick_at(int p_index) const {
        const godot::Ref<netw::NetwEvent> row = at(p_index);
        return row.is_valid() ? row->tick : -1;
    }

    int64_t event_at(int p_index) const {
        const godot::Ref<netw::NetwEvent> row = at(p_index);
        return row.is_valid() ? row->event : -1;
    }
};

} // namespace netw_test
