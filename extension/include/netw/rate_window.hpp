#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"

namespace netw {

class RateWindow {
public:
    int32_t limit = 8;
    int32_t tracked_peers = 64;
    int64_t span_msec = 1000;
    int32_t exempt_peer = 1;

    bool exceeded(int peer_id, int64_t now_msec);

    void clear();

private:
    godot::HashMap<int32_t, godot::LocalVector<int64_t>> ascending_stamps;

    void drop_peers_idle_since(int64_t window_start);
};

} // namespace netw
