#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwRateWindow : public godot::RefCounted {
    GDCLASS(NetwRateWindow, godot::RefCounted)

private:
    int32_t limit = 8;
    int32_t tracked_peers = 64;
    int64_t span_msec = 1000;
    int32_t exempt_peer = 1;

    godot::HashMap<int32_t, godot::LocalVector<int64_t>> stamps;

    void prune(int64_t window_start);

protected:
    static void _bind_methods();

public:
    void set_limit(int value);
    int get_limit() const;
    void set_tracked_peers(int value);
    int get_tracked_peers() const;
    void set_span_msec(int64_t value);
    int64_t get_span_msec() const;
    void set_exempt_peer(int value);
    int get_exempt_peer() const;

    bool exceeded(int peer_id, int64_t now_msec);

    void clear();
};

} // namespace netw
