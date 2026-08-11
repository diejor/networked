#pragma once

/* Who receives an entity's authored commands, on the server.
 *
 * Membership only. Whether a subscriber may still see the entity is the
 * interest gate's answer and is re-asked on every relay rather than remembered
 * here, so a peer that leaves an entity's interest set stops receiving its
 * commands without anything having to observe the exit.
 *
 * Rows are keyed by the session's entity slot, which is the one entity
 * numbering every core reads. A slot with no subscriber holds no row, so the
 * book costs nothing for the entities nobody relays.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwPredictRelayBook : public RefCounted {
    GDCLASS(NetwPredictRelayBook, RefCounted)

    HashMap<int64_t, LocalVector<int64_t>> rows;

protected:
    static void _bind_methods();

public:
    // Admits `p_peer` to `p_entity_slot`'s relayed command lane, or drops it
    // when `p_subscribed` is false. Admitting twice is the same subscription.
    void set_subscribed(int64_t p_entity_slot, int64_t p_peer, bool p_subscribed);

    bool subscribed(int64_t p_entity_slot, int64_t p_peer) const;

    // Every subscriber of a slot, in admission order, which is what makes one
    // server's relay order a function of its own admissions rather than of a
    // hash seed.
    PackedInt64Array peers(int64_t p_entity_slot) const;

    int peer_count(int64_t p_entity_slot) const;

    void release(int64_t p_entity_slot);

    int slot_count() const;

    // The subscription request itself, which is one bit and the only thing a
    // client sends on this lane.
    static PackedByteArray request_bytes(bool p_subscribed);

    // What a request states, or -1 for bytes that are not one.
    static int request_of(const PackedByteArray &p_bytes);
};

} // namespace netw
