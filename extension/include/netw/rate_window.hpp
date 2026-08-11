#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

// A per-peer sliding rate window: how many of one kind of request a peer may
// make inside a span, and who is exempt from asking.
//
// The window slides rather than resetting, so a peer submitting steadily under
// the budget is never refused however long it keeps going, and a peer that
// spent its budget is admitted again the moment its oldest stamp ages out. The
// clock is an argument rather than a reading, which is the whole reason the
// span is testable: a wall clock cannot be asked to be a second past itself.
//
// [codeblock]
// limit 8, span 1000 ms
//   8 frames at t=0        admitted
//   the 9th at t=0         REFUSED
//   the 9th at t=1001      admitted; the first eight aged out
//   one frame every 200 ms admitted forever; five per second is under eight
// [/codeblock]
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
    // The size the table may reach before an idle peer is dropped from it, so
    // a long-lived server never accumulates every peer it ever saw.
    void set_tracked_peers(int value);
    int get_tracked_peers() const;
    void set_span_msec(int64_t value);
    int64_t get_span_msec() const;
    // The one peer the window never counts. Server authority asks itself for
    // things, and a host that could flood itself out of its own session is a
    // guard working against the peer it exists to protect.
    void set_exempt_peer(int value);
    int get_exempt_peer() const;

    // Records one request from this peer and answers whether it exceeded the
    // budget, counting the one just recorded.
    bool exceeded(int peer_id, int64_t now_msec);

    void clear();
};

} // namespace netw
