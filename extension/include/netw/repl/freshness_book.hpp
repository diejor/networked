#pragma once

/* What a receiver has already seen of each unreliable stream.
 *
 * An unreliable datagram carries one sequence for every frame in it, and the
 * frames belong to different streams. This decides, per stream, whether an
 * arriving frame is newer than the last one accepted, so a reordered datagram
 * loses only the streams a fresher one already superseded rather than all of
 * them.
 *
 * A stream is (route, sender, channel) and nothing else. Not the node, not the
 * component, not the property set: those are resolved later and a book keyed
 * by any of them would forget what it knew whenever the resolution changed.
 *
 * Freshness is judged across a HALF WINDOW, not by magnitude. Sequences are
 * 16 bits and wrap, so `65535` then `0` is one step forward while `0` then
 * `65535` is 65535 steps forward, which no session takes: the half window is
 * what makes the first accept and the second refuse. An equal sequence is
 * stale, because a datagram is one sequence and its frames are accepted once.
 *
 * The book dies with what it describes. A route that despawns takes its
 * streams, so the next entity at that route is not judged against the last
 * one's sequences, and a peer that leaves takes its own everywhere: peer ids
 * are reused, and an inherited sequence would make a new peer's first
 * datagrams read as stale.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"

namespace netw::repl {

class FreshnessBook {
    // Route outside, stream inside, because a route's death is the common
    // lifecycle event and this makes it one erase rather than a walk.
    godot::HashMap<int64_t, godot::HashMap<uint64_t, uint16_t>> routes;

    static uint64_t stream(int p_sender, uint8_t p_channel) {
        return (uint64_t(uint32_t(p_sender)) << 8) | uint64_t(p_channel);
    }

public:
    // True when this frame is the first of its stream or newer than the last
    // accepted, recording it either way. A caller that asked without recording
    // would accept the same datagram's frames twice.
    bool accept(int p_sender, int64_t p_route, uint8_t p_channel, uint16_t p_seq);

    void clear_route(int64_t p_route);

    void clear_peer(int p_peer);

    void clear();

    uint32_t stream_count() const;
};

} // namespace netw::repl
