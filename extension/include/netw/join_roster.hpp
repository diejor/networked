#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/resolved_join.hpp"

namespace netw {

using namespace godot;

class NetwJoinRoster : public RefCounted {
    GDCLASS(NetwJoinRoster, RefCounted)

public:
    enum Verdict {
        ADMIT = 0,
        RENAME = 1,
        REFUSE = 2,
    };

private:
    HashMap<int64_t, Ref<ResolvedJoin>> accepted;
    HashMap<int64_t, String> refusals;

    LocalVector<int64_t> peers_in_order() const;

protected:
    static void _bind_methods();

public:
    bool remember(const Ref<ResolvedJoin> &rj);
    Ref<ResolvedJoin> accepted_join(int64_t peer_id) const;
    Array accepted_joins() const;
    Array serialize_accepted() const;

    int name_verdict(
        const StringName &name,
        const PackedStringArray &taken,
        bool is_debug,
        bool has_identity
    ) const;
    StringName free_name(
        const StringName &name,
        const PackedStringArray &taken
    ) const;

    void refuse(int64_t peer_id, const String &reason);
    String refusal(int64_t peer_id) const;

    void forget(int64_t peer_id);
    void clear();
    int size() const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwJoinRoster::Verdict);
