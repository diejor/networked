#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwCarrierBuffers {
    godot::HashMap<int64_t, godot::PackedByteArray> unreliable;
    godot::HashMap<int64_t, godot::PackedByteArray> reliable;

    godot::HashMap<int64_t, godot::PackedByteArray> &lane(bool p_reliable);
    const godot::HashMap<int64_t, godot::PackedByteArray> &lane(
        bool p_reliable
    ) const;

public:
    godot::PackedByteArray append(
        int64_t p_peer,
        const godot::PackedByteArray &p_frame,
        bool p_reliable,
        int64_t p_budget
    );

    godot::PackedByteArray take(int64_t p_peer, bool p_reliable);

    godot::PackedInt32Array peers(bool p_reliable) const;

    int64_t pending(int64_t p_peer, bool p_reliable) const;
    void clear();
};

} // namespace netw
