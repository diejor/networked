#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/repl/freshness_book.hpp"

namespace netw {

// TODO: unregister this class and delete this header once NetwMultiplayer is
// native and holds a netw::repl::FreshnessBook directly.
class NetwSyncProgress : public godot::RefCounted {
    GDCLASS(NetwSyncProgress, godot::RefCounted)

    repl::FreshnessBook impl;

protected:
    static void _bind_methods();

public:
    bool accept_unreliable(
        int64_t p_sender,
        int64_t p_route,
        int64_t p_channel,
        int64_t p_sequence
    );

    void clear_route(int64_t p_route);
    void clear_peer(int64_t p_peer);
    void clear();

    // Value-only book sizes, for laws and for the diagnostics panel.
    godot::Dictionary stats() const;
};

} // namespace netw
