#pragma once

/* Authoritative history per entity, as a slot-minted store.
 *
 * Lag compensation is a query rather than a system: with the server recording
 * one authoritative row per tick, "where was this entity when the shooter saw
 * it" is a read. A row therefore never produces state, and the one write it
 * performs, the rewind, is bracketed so the live world is restored whole
 * whatever the body does.
 *
 * [codeblock]
 * const int64_t slot = core->timeline_open(64);
 * core->timeline_bind_owner(slot, body);
 * core->timeline_record(slot, tick, payload);
 * const Dictionary past = core->timeline_sample(slot, view_tick);
 * [/codeblock]
 */

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
    };

    HashMap<int64_t, Row> rows;
    int64_t next_slot = 1;

    Row *mutable_row_of(int64_t p_slot);
    const Row *row_of(int64_t p_slot) const;

protected:
    static void _bind_methods();

public:
    int64_t timeline_open(int64_t p_history_limit);
    void timeline_close(int64_t p_slot);
    bool timeline_is_open(int64_t p_slot) const;
    bool timeline_bind_owner(int64_t p_slot, Object *p_owner);
    void timeline_unbind_owner(int64_t p_slot);
    bool timeline_owner_bound(int64_t p_slot) const;

    /* The one store this slot's history lives in.
     *
     * Handed out rather than copied, because the prediction plane reads the
     * same rows the recorder writes. Two stores would be two histories that
     * agree only by accident.
     */
    Ref<NetwTimeline> timeline_history(int64_t p_slot) const;

    void timeline_record(
        int64_t p_slot,
        int64_t p_tick,
        const Dictionary &p_payload
    );
    // The newest row at or before p_tick, which is what makes a view tick
    // between two recordings read the state that was standing at it.
    Dictionary timeline_sample(int64_t p_slot, int64_t p_tick) const;
    int64_t timeline_sample_tick(int64_t p_slot, int64_t p_tick) const;
    void timeline_trim_before(int64_t p_slot, int64_t p_tick);

    /* Applies each named slot's state at p_tick to its owner, runs p_body, and
     * restores what it overwrote, unconditionally, on return.
     *
     * A slot with no owner or no retained row at p_tick is left at its live
     * state and skipped, so the restore touches exactly the objects the rewind
     * moved. Only the fields the retained row names are captured and put back:
     * a field history never held is a field the rewind never wrote.
     */
    int rewind(
        const PackedInt64Array &p_slots,
        int64_t p_tick,
        const Callable &p_body
    );
};

} // namespace netw
