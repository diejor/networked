#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

// A keyed ledger of optimistic acts: a revert parked against a deadline tick.
// Adopt keeps the act and drops the revert, discard and timeout run it, so a
// timeout is a denial rather than a third outcome.
//
// The ledger never reads a clock. A deadline is measured against the tick its
// sweep is handed, which is what makes the span testable.
//
// A watcher is refused unless its key is armed, so an observer can never
// outlive the act it observes.
//
// [codeblock]
// arm("act__p__7__0", revert, deadline 9)
//   sweep(8)            kept
//   adopt               revert dropped unrun, confirmed fires
//   discard             revert runs, denied fires
//   sweep(9)            revert runs, denied fires
// [/codeblock]
class NetwEffectLedger : public godot::RefCounted {
    GDCLASS(NetwEffectLedger, godot::RefCounted)

private:
    struct Entry {
        godot::Callable revert;
        int64_t deadline_tick = 0;
    };

    struct Watcher {
        godot::Callable confirmed;
        godot::Callable denied;
    };

    godot::HashMap<godot::StringName, Entry> entries;
    godot::HashMap<godot::StringName, Watcher> watchers;

    void resolve(const godot::StringName &p_key, bool p_keep);

protected:
    static void _bind_methods();

public:
    void arm(
        const godot::StringName &p_key,
        const godot::Callable &p_revert,
        int64_t p_deadline_tick
    );
    bool watch(
        const godot::StringName &p_key,
        const godot::Callable &p_confirmed,
        const godot::Callable &p_denied
    );
    void adopt(const godot::StringName &p_key);
    void discard(const godot::StringName &p_key);
    bool pending(const godot::StringName &p_key) const;
    int64_t count() const;
    // Arms made by a mid-sweep revert are kept rather than swept, because the
    // expired set is collected before the first revert runs.
    void sweep(int64_t p_tick);
    void clear();
};

} // namespace netw
