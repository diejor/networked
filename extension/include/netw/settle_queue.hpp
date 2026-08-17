#pragma once

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw {

// Effects that run after the cascade that scheduled them, drained to a fixed
// point inside the pump the session already runs.
//
// A named key coalesces: scheduling a key already queued moves it to the back,
// which is what "run after the cascade that scheduled me" means and what a
// per-site scheduled flag used to spell for itself. An empty key never
// coalesces, so unkeyed effects run in the order they were enqueued.
//
// Each pass takes the WHOLE queue and runs it, so an effect scheduled during a
// pass runs in the next pass of the same drain rather than inside the one that
// scheduled it.
class SettleQueue {
public:
    // A cascade that has not settled in this many passes is a cycle rather
    // than a deep chain. The drain gives up and says which keys were still
    // pending, because a defect that hangs is worse than one that reports.
    static constexpr int MAX_PASSES = 8;

private:
    struct Row {
        godot::StringName key;
        godot::Callable fn;
        // Pumps still owed before this row is due. Zero is due now, which is
        // what an ordinary settle is.
        int pumps = 0;
    };

    bool has_due() const;

    godot::LocalVector<Row> rows;

public:
    void schedule(const godot::Callable &fn, const godot::StringName &key);

    /* Holds fn for p_pumps pumps before a drain will run it.
     *
     * A window counted in the session's own pumps needs no tree and no wall
     * clock, so a teardown that has to stay reachable for a while is
     * expressible where a scene tree is not. A key coalesces exactly as it does
     * for `schedule`, so re-declaring a window replaces it rather than queuing
     * a second one.
     */
    void schedule_after(
        const godot::Callable &fn,
        const godot::StringName &key,
        int p_pumps
    );

    /* Spends one pump against every held window.
     *
     * Separate from `drain`, because a clocked session drains on its polls as
     * well as its ticks and a window is counted in the cadence the session
     * sends at. Calling it twice a pump spends the window twice.
     */
    void advance_windows();

    // Withdraws a queued key, for a caller that did the work on the spot and
    // has nothing left to settle. An unqueued key is not an error.
    void cancel(const godot::StringName &key);

    bool is_empty() const;
    int size() const;
    // Whether this key is queued. A named key coalesces, so the answer is the
    // whole of "how many of it are pending".
    bool has(const godot::StringName &key) const;

    // The keys still pending when the pass bound was hit, empty when the drain
    // reached a fixed point. An unkeyed row answers "<unkeyed>", because the
    // report exists to name a cycle and an empty string names nothing. A row
    // whose window is still open is waiting rather than cycling, so it is
    // neither run nor reported nor dropped.
    godot::PackedStringArray drain();

    void clear();
};

} // namespace netw
