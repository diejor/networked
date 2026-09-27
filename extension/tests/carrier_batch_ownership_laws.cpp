#include "support/netw_test.h"

#include "support/stream_seat.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace TestNetwCarrierBatchOwnership {

using godot::LocalVector;
using netw::CarrierBatch;
using netw::CarrierRow;
using netw::NetwCarrierBuffers;
using netw::SchemaCore;
using netw::repl::RowOffer;
using netw::repl::RowSend;
using netw::repl::RowVerdict;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::table::SchemaRecord;
using netw::wire::ChannelDecl;
using netw::wire::Delivery;
using netw::wire::WireRegistry;

constexpr uint8_t CHANNEL = 43;
constexpr int PEER = 7;
constexpr int64_t BUDGET = 64;

const WireRegistry &registry() {
    static const WireRegistry made = [] {
        WireRegistry out;
        ChannelDecl decl;
        decl.id = CHANNEL;
        decl.name = godot::StringName("carrier_probe");
        decl.delivery = Delivery::FITTED;
        out.register_channel(decl);
        return out;
    }();
    return made;
}

const SchemaRecord &body() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = godot::StringName("Body");
        SchemaCore::append_column(
            &made,
            godot::StringName("x"),
            SchemaCore::I16,
            1
        );
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

RowOffer offer(int64_t p_route, int64_t p_value) {
    RowOffer out;
    out.route = p_route;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = &body();
    out.values.push_back(p_value);
    out.recipients.push_back(PEER);
    out.masked = true;
    return out;
}

PackedByteArray frame(int p_size) {
    PackedByteArray out;
    out.resize(p_size);
    return out;
}

class Link {
    NetwCarrierBuffers carrier;
    SessionSend send;
    uint16_t next_seq = 1;

public:
    bool up = true;
    int64_t last_seq = -1;

    SessionSend &sender() {
        return send;
    }

    LocalVector<RowSend> fit(
        const LocalVector<RowOffer> &p_offers,
        int64_t p_max_bits
    ) {
        netw_test::seat_streams(send, p_offers);
        const SessionResult out = send.run(registry(), p_offers, p_max_bits, 0);
        LocalVector<RowSend> sends;
        for (uint32_t at = 0; at < out.sends.size(); ++at) {
            sends.push_back(out.sends[at]);
        }
        return sends;
    }

    int64_t dispose(int p_peer, bool p_reliable, const CarrierBatch &p_batch) {
        if (p_batch.is_empty()) {
            return -1;
        }
        if (!up) {
            send.cancel(p_peer, p_batch.rows());
            return -1;
        }
        const uint16_t seq = next_seq++;
        send.commit(
            p_peer,
            seq,
            p_batch.rows(),
            p_batch.frame_count(),
            p_batch.bit_count()
        );
        last_seq = int64_t(seq);
        return int64_t(seq);
    }

    int64_t aggregate(int p_peer, const RowSend &p_send, int p_bytes) {
        CarrierRow row;
        const bool owns = send.describe(p_send, row);
        const CarrierBatch owed = carrier.append(
            p_peer,
            frame(p_bytes),
            false,
            BUDGET,
            owns ? &row : nullptr
        );
        return dispose(p_peer, false, owed);
    }

    int64_t immediate(int p_peer, const RowSend &p_send, int p_bytes) {
        CarrierRow row;
        CarrierBatch batch;
        batch.take_frame(frame(p_bytes));
        if (send.describe(p_send, row)) {
            batch.attach(row);
        }
        return dispose(p_peer, false, batch);
    }

    int64_t flush(int p_peer) {
        return dispose(p_peer, false, carrier.take(p_peer, false));
    }

    void close_route(int64_t p_route) {
        carrier.close_route(p_route);
        send.close_route(p_route);
    }

    int64_t pending(int p_peer) const {
        return carrier.pending(p_peer, false);
    }

    uint64_t exposed(int64_t p_route) {
        return send.explain(p_route, 0, PEER).exposed;
    }

    bool promoted(int64_t p_route) {
        return send.explain(p_route, 0, PEER).has_baseline;
    }

    static netw::wire::StreamLane lane_of(int64_t p_route) {
        netw::wire::StreamLane made;
        made.route = p_route;
        made.ordinal = 0;
        made.family = netw::wire::StreamFamily::VOLATILE;
        return made;
    }

    void accept(int64_t p_route) {
        const netw::wire::StreamLane lane = lane_of(p_route);
        netw::wire::StreamWriterBook &book = send.writer_book();
        netw::wire::SnapshotSender *stream = book.sender(PEER, lane);
        if (stream != nullptr) {
            book.receipt(
                PEER,
                book.token_of(PEER, lane),
                stream->exposed_high_water()
            );
        }
    }

    bool still_staged(int64_t p_route, uint64_t p_revision) {
        const netw::wire::SnapshotSender *stream
            = send.writer_book().sender(PEER, lane_of(p_route));
        return stream != nullptr && stream->holds(p_revision);
    }
};

TEST_CASE(
    "[Networked][Carrier][Wire][Hosted] C3-1 an immediate send owns its own "
    "batch, so the row it carries stages under that datagram and nothing waits"
) {
    Link link;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10));
    const LocalVector<RowSend> sends = link.fit(offers, 1 << 20);
    REQUIRE(sends.size() == 1);

    const int64_t seq = link.immediate(PEER, sends[0], 8);
    const bool left_now = seq >= 0;
    CHECK(left_now);
    NETW_CHECK_EQ(link.pending(PEER), 0);
    NETW_CHECK_EQ(link.exposed(1), sends[0].revision);

    link.sender().acknowledge(PEER, uint16_t(seq), 0);
    const bool delivery_alone_confirms_nothing = !link.promoted(1);
    CHECK(delivery_alone_confirms_nothing);

    link.accept(1);
    const bool confirmed = link.promoted(1);
    CHECK(confirmed);
}

TEST_CASE(
    "[Networked][Carrier][Wire][Hosted] C3-2 an overflow hands back the "
    "preceding batch intact and opens the next one with the triggering row"
) {
    Link link;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10));
    offers.push_back(offer(2, 20));
    const LocalVector<RowSend> sends = link.fit(offers, 1 << 20);
    REQUIRE(sends.size() == 2);

    const int64_t held = link.aggregate(PEER, sends[0], 40);
    const bool nothing_left_yet = held < 0;
    CHECK(nothing_left_yet);

    const int64_t first = link.aggregate(PEER, sends[1], 40);
    const bool the_first_batch_went = first >= 0;
    CHECK(the_first_batch_went);
    NETW_CHECK_EQ(link.pending(PEER), 40);

    const bool the_first_lane_kept_its_row
        = link.still_staged(1, sends[0].revision);
    const bool the_second_lane_kept_its_row
        = link.still_staged(2, sends[1].revision);
    CHECK(the_first_lane_kept_its_row);
    CHECK(the_second_lane_kept_its_row);

    link.accept(1);
    const bool first_row_settled = link.promoted(1);
    const bool second_row_untouched = !link.promoted(2);
    CHECK(first_row_settled);
    CHECK(second_row_untouched);

    const int64_t second = link.flush(PEER);
    const bool the_second_batch_went = second >= 0;
    CHECK(the_second_batch_went);
    link.accept(2);
    const bool second_row_settled = link.promoted(2);
    CHECK(second_row_settled);
}

TEST_CASE(
    "[Networked][Carrier][Wire][Hosted] C3-3 a failed send cancels its own "
    "batch, so the row it carried is never staged and keeps its repair debt"
) {
    Link link;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10));
    const LocalVector<RowSend> sends = link.fit(offers, 1 << 20);
    REQUIRE(sends.size() == 1);

    link.up = false;
    const int64_t refused = link.immediate(PEER, sends[0], 8);
    const bool the_send_failed = refused < 0;
    CHECK(the_send_failed);
    const bool the_cancelled_snapshot_is_gone
        = !link.still_staged(1, sends[0].revision);
    CHECK(the_cancelled_snapshot_is_gone);
    const bool nothing_was_confirmed = !link.promoted(1);
    CHECK(nothing_was_confirmed);

    const netw::repl::RowExplain verdict = link.sender().explain(1, 0, PEER);
    const bool the_row_is_owed_again = verdict.verdict == RowVerdict::REFUSED;
    CHECK(the_row_is_owed_again);

    link.up = true;
    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10));
    const LocalVector<RowSend> retry = link.fit(again, 1 << 20);
    NETW_CHECK_EQ(retry.size(), uint32_t(1));
}

TEST_CASE(
    "[Networked][Carrier][Wire][Hosted] C3-4 a failed flush discards its rows "
    "with its bytes, so a later unrelated datagram cannot settle them"
) {
    Link link;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10));
    const LocalVector<RowSend> first = link.fit(offers, 1 << 20);
    REQUIRE(first.size() == 1);
    link.aggregate(PEER, first[0], 8);

    link.up = false;
    const int64_t lost = link.flush(PEER);
    const bool the_flush_failed = lost < 0;
    CHECK(the_flush_failed);
    NETW_CHECK_EQ(link.pending(PEER), 0);
    const bool the_lost_snapshot_is_gone
        = !link.still_staged(1, first[0].revision);
    CHECK(the_lost_snapshot_is_gone);

    link.up = true;
    LocalVector<RowOffer> other;
    other.push_back(offer(2, 20));
    const LocalVector<RowSend> later = link.fit(other, 1 << 20);
    REQUIRE(later.size() == 1);
    link.aggregate(PEER, later[0], 8);
    const int64_t seq = link.flush(PEER);

    const bool the_datagram_went = seq >= 0;
    CHECK(the_datagram_went);
    const bool the_carried_row_is_staged
        = link.still_staged(2, later[0].revision);
    CHECK(the_carried_row_is_staged);

    link.accept(2);
    const bool the_discarded_row_is_not_confirmed = !link.promoted(1);
    const bool the_carried_row_is_confirmed = link.promoted(2);
    CHECK(the_discarded_row_is_not_confirmed);
    CHECK(the_carried_row_is_confirmed);
}

TEST_CASE(
    "[Networked][Carrier][Wire][Hosted] C3-5 a row the fitter refuses never "
    "reaches a batch, so no datagram can stage it"
) {
    Link link;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10));
    offers.push_back(offer(2, 20));
    const LocalVector<RowSend> sends = link.fit(offers, 1);
    NETW_CHECK_EQ(sends.size(), uint32_t(0));

    const int64_t seq = link.flush(PEER);
    const bool nothing_was_carried = seq < 0;
    CHECK(nothing_was_carried);
    NETW_CHECK_EQ(link.exposed(1), 0);
    NETW_CHECK_EQ(link.exposed(2), 0);

    const bool the_fitter_deferred_it
        = link.sender().explain(1, 0, PEER).verdict == RowVerdict::DEFERRED;
    CHECK(the_fitter_deferred_it);
}

TEST_CASE(
    "[Networked][Carrier][Wire][Hosted] C3-6 closing a route cancels every "
    "descriptor it owns in an open batch and leaves the others standing"
) {
    Link link;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10));
    offers.push_back(offer(2, 20));
    const LocalVector<RowSend> sends = link.fit(offers, 1 << 20);
    REQUIRE(sends.size() == 2);
    link.aggregate(PEER, sends[0], 8);
    link.aggregate(PEER, sends[1], 8);

    link.close_route(1);
    const int64_t seq = link.flush(PEER);
    const bool the_datagram_still_went = seq >= 0;
    CHECK(the_datagram_still_went);

    const bool the_closed_route_kept_no_snapshot
        = !link.still_staged(1, sends[0].revision);
    CHECK(the_closed_route_kept_no_snapshot);
    const bool the_living_lane_kept_its_row
        = link.still_staged(2, sends[1].revision);
    CHECK(the_living_lane_kept_its_row);

    link.accept(2);
    const bool the_closed_route_confirmed_nothing = !link.promoted(1);
    const bool the_living_lane_is_confirmed = link.promoted(2);
    CHECK(the_closed_route_confirmed_nothing);
    CHECK(the_living_lane_is_confirmed);
}

TEST_CASE(
    "[Networked][Carrier][Wire][Hosted] C3-7 two rows of one lane in one "
    "datagram name one staged snapshot, and it is the one the receiver ends at"
) {
    Link link;
    LocalVector<RowOffer> first;
    first.push_back(offer(1, 10));
    const LocalVector<RowSend> early = link.fit(first, 1 << 20);
    REQUIRE(early.size() == 1);

    LocalVector<RowOffer> second;
    second.push_back(offer(1, 20));
    const LocalVector<RowSend> late = link.fit(second, 1 << 20);
    REQUIRE(late.size() == 1);

    link.aggregate(PEER, early[0], 8);
    link.aggregate(PEER, late[0], 8);
    const int64_t seq = link.flush(PEER);
    const bool one_datagram_carried_both = seq >= 0;
    CHECK(one_datagram_carried_both);

    NETW_CHECK_EQ(link.exposed(1), late[0].revision);

    link.accept(1);
    const bool the_lane_is_confirmed = link.promoted(1);
    CHECK(the_lane_is_confirmed);

    LocalVector<RowOffer> repeat_last;
    repeat_last.push_back(offer(1, 20));
    const LocalVector<RowSend> quiet = link.fit(repeat_last, 1 << 20);
    const bool the_last_row_is_the_baseline = quiet.is_empty();
    CHECK(the_last_row_is_the_baseline);

    LocalVector<RowOffer> repeat_first;
    repeat_first.push_back(offer(1, 10));
    const LocalVector<RowSend> owed = link.fit(repeat_first, 1 << 20);
    const bool the_first_row_is_not_the_baseline = owed.size() == 1;
    CHECK(the_first_row_is_not_the_baseline);
}

} // namespace TestNetwCarrierBatchOwnership
