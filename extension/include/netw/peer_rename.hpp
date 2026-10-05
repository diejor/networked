#pragma once

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/session_core.hpp"

namespace netw {

struct PeerRename {
    int64_t successor_was = 0;
    int64_t former_now = 0;

    static PeerRename of_transfer(
        int64_t p_successor_was,
        int64_t p_former_now
    ) {
        PeerRename made;
        made.successor_was = p_successor_was;
        made.former_now = p_former_now != 0 ? p_former_now : p_successor_was;
        return made;
    }

    int64_t of(int64_t p_peer) const {
        if (p_peer == successor_was) {
            return SERVER_PEER;
        }
        return p_peer == SERVER_PEER ? former_now : p_peer;
    }

    bool changes(int64_t p_peer) const {
        return p_peer == successor_was || p_peer == SERVER_PEER;
    }

    template <typename V> void rekey(godot::HashMap<int64_t, V> &r_map) const {
        godot::HashMap<int64_t, V> renamed;
        for (const godot::KeyValue<int64_t, V> &entry : r_map) {
            renamed[of(entry.key)] = entry.value;
        }
        r_map = renamed;
    }

    void rekey(godot::HashSet<int64_t> &r_set) const {
        godot::HashSet<int64_t> renamed;
        for (const int64_t peer : r_set) {
            renamed.insert(of(peer));
        }
        r_set = renamed;
    }
};

} // namespace netw
