#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/object_port.hpp"
#include "netw/timeline.hpp"

namespace netw {

using namespace godot;

class NetwLagCompCore : public RefCounted {
    GDCLASS(NetwLagCompCore, RefCounted)

    struct Row {
        ObjectPort port;
        Ref<NetwTimeline> history;
        LocalVector<StringName> declared;
    };

    HashMap<int64_t, Row> rows;
    int64_t next_slot = 1;

    Row *mutable_row_of(int64_t p_slot);
    const Row *row_of(int64_t p_slot) const;

    static Array touched_keys(const Row &p_row, const Dictionary &p_past);
    static Dictionary declared_only(
        const Row &p_row,
        const Dictionary &p_payload
    );

protected:
    static void _bind_methods();

public:
    int64_t timeline_open(int64_t p_history_limit);
    void timeline_close(int64_t p_slot);
    bool timeline_is_open(int64_t p_slot) const;
    bool timeline_bind_owner(int64_t p_slot, Object *p_owner);
    void timeline_unbind_owner(int64_t p_slot);
    bool timeline_owner_bound(int64_t p_slot) const;

    void timeline_declare(int64_t p_slot, const Array &p_keys);
    Array timeline_declared(int64_t p_slot) const;

    Ref<NetwTimeline> timeline_history(int64_t p_slot) const;

    void timeline_record(
        int64_t p_slot,
        int64_t p_tick,
        const Dictionary &p_payload
    );
    Dictionary timeline_sample(int64_t p_slot, int64_t p_tick) const;
    int64_t timeline_sample_tick(int64_t p_slot, int64_t p_tick) const;
    void timeline_trim_before(int64_t p_slot, int64_t p_tick);

    int rewind(
        const PackedInt64Array &p_slots,
        int64_t p_tick,
        const Callable &p_body
    );
};

} // namespace netw
