#pragma once

/* The shell's handle on the on-change retained streams.
 *
 * A binding that exists so `NetwSyncCompat` can drive the book while it is
 * still GDScript. It holds no logic: every verb forwards to
 * `netw::repl::WatchBook`, whose header carries the law.
 *
 * Streams are keyed by an integer the caller owns rather than by the binding
 * object, because a native book holding a reference to a shell object would
 * outlive it exactly where the shell means to drop it.
 *
 * TODO: unregister this class and delete this header once NetwSyncCompat is
 * native and holds a `netw::repl::WatchBook` directly.
 */

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/repl/watch_book.hpp"

namespace netw {

class NetwWatchBook : public godot::RefCounted {
    GDCLASS(NetwWatchBook, godot::RefCounted)

    repl::WatchBook impl;

protected:
    static void _bind_methods();

public:
    void poll(
        int64_t p_key,
        const godot::Array &p_values,
        const godot::Array &p_readable
    );

    // `[mask, values]`, the pair a caller frames one retained row from.
    godot::Array mask_for(int64_t p_key, int64_t p_peer) const;

    void commit(int64_t p_key, int64_t p_peer);
    void reset(int64_t p_key);
    void clear_baselines(int64_t p_key);
    void clear_peer(int64_t p_peer);
    void retain_baselines(
        int64_t p_key,
        const godot::PackedInt32Array &p_recipients
    );
    bool is_inited(int64_t p_key) const;
    void clear();
};

} // namespace netw
