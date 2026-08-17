#pragma once

/* The one call a session makes to send a pass.
 *
 * Every piece under this is composable on its own and the order they run in is
 * already law. What this adds is that a caller cannot get the order wrong,
 * because there is only one entry: offer the rows, take back what rode and
 * what waits.
 *
 * That matters more than it looks. The sequence has three places where a
 * plausible caller does the wrong thing and nothing complains. Staging a row
 * for a peer whose mask was empty tells the book that peer holds a row it was
 * never sent. Asking for a mask twice in one pass arms the sticky set twice.
 * Gathering per recipient instead of once per row is correct and quietly costs
 * a poll per peer. None of those is visible from inside the component that
 * suffers it, so the sequence is held here rather than written again at each
 * call site.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/repl/lane_set.hpp"
#include "netw/repl/send_pass.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/table/schema_core.hpp"

namespace netw::repl {

// One row a caller wants sent this pass, and who it is for.
struct RowOffer {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    godot::Ref<SchemaRecord> schema;
    godot::Array values;
    godot::LocalVector<int> recipients;
    float priority = 1.0f;
    bool masked = false;
    /* Rides an ordered, guaranteed channel, so the send is its own proof.
     *
     * A reliable offer is diffed by its own lane, which advances on send, and
     * it is NOT offered to the fitter: a fitter may defer a candidate to the
     * next pass, and a deferred row whose baseline already advanced has no
     * later frame that would carry the columns it dropped.
     */
    bool reliable = false;
    /* Repeats the recent ticks still in flight instead of sending one row.
     *
     * The samples are the lane's, not the caller's: a windowed offer hands
     * over the tick it just gathered and the lane answers with the range the
     * far side may still be owed. `window` is the ring's depth and `tick` the
     * tick this row was authored at, which is the key the ring is ordered by
     * and the only shape where a tick is core state rather than framing.
     */
    bool windowed = false;
    uint32_t window = 0;
    int64_t tick = -1;
};

// What one recipient is owed of one row, after the pass decided.
struct RowSend {
    int64_t route = 0;
    uint8_t comp = 0;
    int peer = 0;
    uint64_t mask = 0;
    bool masked = false;
    bool reliable = false;
    bool windowed = false;
    wire::CodeRow row;
    // The ticks a windowed send repeats, oldest first. Empty on every other
    // lane shape, where `row` is the whole of what rides.
    godot::LocalVector<WindowSample> samples;
};

struct SessionResult {
    godot::LocalVector<RowSend> sends;
    // Rows whose gather refused. A caller that cannot see these would read a
    // silent pass as a caught-up session.
    uint32_t ungathered = 0;
    // Peers that were already caught up, counted so a pass that sent nothing
    // can say WHY it sent nothing.
    uint32_t caught_up = 0;
    bool untrackable = false;
    int64_t sent_bits = 0;
};

class SessionSend {
    struct Pending {
        int64_t route = 0;
        uint8_t comp = 0;
        wire::CodeRow row;
    };

    LaneSet lanes;
    SendPass pass;
    godot::HashMap<int, godot::LocalVector<Pending>> pending;

    godot::LocalVector<RowSend> collect(
        const godot::LocalVector<RowOffer> &p_offers,
        godot::LocalVector<wire::FitCandidate> &r_candidates,
        SessionResult &r_out
    );

public:
    // Runs one pass. Rows are gathered once each, masked per recipient, and
    // staged only for the recipients whose mask was not empty.
    SessionResult run(
        const wire::WireRegistry &p_registry,
        const godot::LocalVector<RowOffer> &p_offers,
        int64_t p_max_bits,
        uint16_t p_seq,
        int64_t p_send_id
    );

    /* Runs one pass whose sends are stamped by someone else.
     *
     * Nothing is staged here, not even for a recipient the pass answered:
     * what a pass wanted to send and what a datagram carried are two different
     * sets, because a frame can be dropped between them and a peer's run can
     * split across two datagrams mid-pass. `defer` is how a caller says which
     * ones actually reached the carrier.
     */
    SessionResult run_deferred(const godot::LocalVector<RowOffer> &p_offers);

    /* Holds one send until the datagram carrying it is stamped.
     *
     * Called after the frame is handed to the carrier and never before, so a
     * flush the same hand-off triggered commits the rows that rode in it and
     * this one waits for the datagram it is actually in.
     */
    void defer(const RowSend &p_send);

    void commit(int p_peer, uint16_t p_seq);

    uint32_t pending_count(int p_peer) const;

    void forget_peer(int p_peer);

    void acknowledge(int p_peer, uint16_t p_acked_seq);

    // Forgets every peer outside `p_recipients`, in every lane.
    void retain(const godot::LocalVector<int> &p_recipients);

    void retain_row(
        int64_t p_route,
        uint8_t p_comp,
        const godot::LocalVector<int> &p_recipients
    ) {
        lanes.retain_row(p_route, p_comp, p_recipients);
    }

    void close_route(int64_t p_route) {
        lanes.close_route(p_route);
    }

    uint32_t lane_count() const {
        return lanes.size();
    }

    uint32_t retained_lane_count() const {
        return lanes.retained_size();
    }

    uint32_t window_lane_count() const {
        return lanes.window_size();
    }

    int outstanding() const {
        return pass.outstanding();
    }
};

} // namespace netw::repl
