#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/session/frames.hpp"

namespace netw {

class JoinRoster {
public:
    enum Verdict {
        ADMIT = 0,
        RENAME = 1,
        REFUSE = 2,
    };

private:
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

    int name_verdict(
        const godot::StringName &p_name,
        const godot::PackedStringArray &p_taken,
        bool p_renames_on_collision,
        bool p_has_identity
    ) const;
    godot::StringName free_name(
        const godot::StringName &p_name,
        const godot::PackedStringArray &p_taken
    ) const;

    void refuse(int64_t p_peer, const godot::String &p_reason);
    godot::String refusal(int64_t p_peer) const;

    void forget(int64_t p_peer);
    void clear();
    int size() const;
};

} // namespace netw
