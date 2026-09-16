#pragma once

#include "godot/object.hpp"

namespace netw::persist {

struct WriteFence {
    godot::ObjectID session;
    int64_t authority = 0;
    bool armed = false;
};

bool write_fence_holds(const WriteFence &p_issued);

} // namespace netw::persist
