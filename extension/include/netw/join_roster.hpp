#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/resolved_join.hpp"

namespace netw {

class JoinRoster {
public:
    enum Verdict {
        ADMIT = 0,
        RENAME = 1,
        REFUSE = 2,
    };

private:
    godot::HashMap<int64_t, godot::Ref<ResolvedJoin>> accepted;
    godot::HashMap<int64_t, godot::String> refusals;

    godot::LocalVector<int64_t> peers_in_order() const;

public:
    bool remember(const godot::Ref<ResolvedJoin> &rj);
    godot::Ref<ResolvedJoin> accepted_join(int64_t peer_id) const;
    godot::Array accepted_joins() const;
    godot::Array serialize_accepted() const;

    int name_verdict(
        const godot::StringName &name,
        const godot::PackedStringArray &taken,
        bool is_debug,
        bool has_identity
    ) const;
    godot::StringName free_name(
        const godot::StringName &name,
        const godot::PackedStringArray &taken
    ) const;

    void refuse(int64_t peer_id, const godot::String &reason);
    godot::String refusal(int64_t peer_id) const;

    void forget(int64_t peer_id);
    void clear();
    int size() const;
};

} // namespace netw
