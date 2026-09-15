#pragma once

#include "netw_test.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw_test {

struct Membership {
    godot::StringName scene_name;
    godot::Vector<godot::StringName> enclosed;
    godot::Vector<int> subscribed;
    int member_count = 0;
    bool boundary = false;
    bool asked = false;

    bool encloses(const godot::StringName &p_name) const {
        return enclosed.find(p_name) >= 0;
    }

    int members() const {
        return member_count;
    }

    bool subscribes(int p_client) const {
        return subscribed.find(p_client) >= 0;
    }

    int viewers() const {
        return subscribed.size();
    }

    bool has_boundary() const {
        return boundary;
    }

    bool taken() const {
        return asked;
    }
};

} // namespace netw_test
