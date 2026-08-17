#pragma once

/* The dirty-poll engine behind every on-change retained stream: values in,
 * per-recipient masks out, and nothing about nodes.
 *
 * A retained field replicates only when it changes, so the compare has to
 * remember what it last saw, and a peer that just gained the stream has to be
 * healed with the whole row rather than with what moved since a baseline it
 * never had. An ABSENT baseline is that gain edge, and it reads as zero: every
 * stamp is newer than it, so the first mask is the full row for free.
 *
 * Stamps are a monotonic counter rather than a tick, because what a peer is
 * owed is "everything that changed since I last sent to you" and no clock
 * answers that. The counter advances per changed field, never per pass, so two
 * fields changing in one pass are still ordered against a baseline taken
 * between them.
 *
 * A stored copy must not alias a container the game keeps mutating, or the
 * poll compares a value against itself forever and the field never changes
 * again.
 *
 * The book never reads a node, which is what lets one implementation serve
 * both front doors.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::repl {

class WatchBook {
    struct Stream {
        bool inited = false;
        godot::Array values;
        godot::LocalVector<int64_t> stamps;
        int64_t change_counter = 0;
        // Peer to the change counter at its last commit. Absent is the gain
        // edge and heals with the whole row.
        godot::HashMap<int64_t, int64_t> baselines;
    };

    godot::HashMap<int64_t, Stream> streams;

    Stream *stream_for(int64_t p_key);
    const Stream *stream_for(int64_t p_key) const;

public:
    // Watched fields ride a 64-bit change mask, the stock replicator's
    // ceiling. A caller asserts its field count against this before polling.
    static const int FIELD_LIMIT = 64;

    /* Stamps every field of `p_key` that changed since the last poll.
     *
     * `p_readable` is parallel to `p_values`, and a field marked unreadable
     * keeps its previous stamp, so a target that is momentarily unresolvable
     * never reads as a change. An empty `p_readable` treats every field as
     * readable. The first poll seeds the row, stamping every readable field so
     * the first recipient heals fully.
     */
    void poll(
        int64_t p_key,
        const godot::Array &p_values,
        const godot::Array &p_readable
    );

    /* Answers what `p_peer` is owed of `p_key`: the mask over the stream's
     * field order of everything stamped newer than that peer's baseline, and
     * those fields' values in set-bit order. A stream never polled owes
     * nothing.
     */
    void mask_for(
        int64_t p_key,
        int64_t p_peer,
        uint64_t &r_mask,
        godot::Array &r_values
    ) const;

    // Advances `p_peer`'s baseline to the stream's counter, once per recipient
    // per pass.
    void commit(int64_t p_key, int64_t p_peer);

    // Drops the stamps and every baseline, so the next poll reseeds and the
    // next pass heals every recipient with the whole new row.
    void reset(int64_t p_key);

    // Drops the baselines while keeping the polled row, the session-restart
    // counterpart of `reset`.
    void clear_baselines(int64_t p_key);

    void clear_peer(int64_t p_peer);

    /* Drops the baselines `p_key` holds against any peer not in
     * `p_recipients`, because a baseline outliving a peer's visibility would
     * suppress the full-row heal it is owed on re-admission.
     */
    void retain_baselines(
        int64_t p_key,
        const godot::PackedInt32Array &p_recipients
    );

    bool is_inited(int64_t p_key) const;

    void clear();
};

} // namespace netw::repl
