#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/session/frames.hpp"

namespace netw {

class JoinRoster {
    godot::HashMap<int64_t, session::AcceptFrame> accepted;
    godot::HashMap<int64_t, godot::String> refusals;

    godot::LocalVector<int64_t> peers_in_order() const;

public:
    bool remember(const session::AcceptFrame &p_entry);
    bool has_accepted(int64_t p_peer) const;
    session::AcceptFrame accepted_join(int64_t p_peer) const;
    godot::LocalVector<session::AcceptFrame> accepted_joins() const;
    godot::PackedByteArray roster_frame() const;
    godot::PackedByteArray accept_frame(int64_t p_peer) const;

    void refuse(int64_t p_peer, const godot::String &p_reason);
    godot::String refusal(int64_t p_peer) const;

    void forget(int64_t p_peer);
    void clear();
    int size() const;
};

} // namespace netw
