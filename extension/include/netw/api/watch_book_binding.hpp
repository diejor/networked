#pragma once

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
