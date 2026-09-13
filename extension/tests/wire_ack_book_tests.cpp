#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/ack_book.hpp"

using namespace godot;

namespace TestNetwWireAckBook {

using godot::LocalVector;
using netw::wire::AckBook;
using netw::wire::AckEntry;

struct Verdicts {
    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
};

int64_t delivered_bits(Verdicts &v, uint32_t at) {
    return at < v.delivered.size() ? v.delivered[at].bits : -1;
}

int64_t lost_bits(Verdicts &v, uint32_t at) {
    return at < v.lost.size() ? v.lost[at].bits : -1;
}

TEST_CASE("[Networked][Wire][Hosted] an ack reports the datagram it names") {
    AckBook book;
    Verdicts v;

    book.record_send(7, 3, 700);
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(7, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.delivered.size(), 1);
    NETW_CHECK_EQ(delivered_bits(v, 0), 700);
    NETW_CHECK_EQ(v.delivered[0].frames, 3);
    NETW_CHECK_EQ(v.lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] the history settles every datagram whose bit is "
    "set and leaves the rest in flight"
) {
    AckBook book;
    Verdicts v;

    book.record_send(8, 1, 800);
    book.record_send(9, 1, 900);
    book.record_send(10, 1, 1000);

    const uint32_t eight_arrived_nine_did_not = 0x2;
    book.process_ack(10, eight_arrived_nine_did_not, v.delivered, v.lost);

    NETW_CHECK_EQ(v.delivered.size(), 2);
    NETW_CHECK_EQ(delivered_bits(v, 0), 1000);
    NETW_CHECK_EQ(delivered_bits(v, 1), 800);
    NETW_CHECK_EQ(v.lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a history bit for a datagram already settled "
    "reports it once"
) {
    AckBook book;
    Verdicts v;

    book.record_send(8, 1, 800);
    book.process_ack(8, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.delivered.size(), 1);

    book.record_send(9, 1, 900);
    book.process_ack(9, 0x1, v.delivered, v.lost);
    NETW_CHECK_EQ(v.delivered.size(), 1);
    NETW_CHECK_EQ(delivered_bits(v, 0), 900);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack for a seq the book never sent reports "
    "nothing"
) {
    AckBook book;
    Verdicts v;

    book.record_send(7, 1, 700);
    book.process_ack(8, 0, v.delivered, v.lost);

    NETW_CHECK_EQ(v.delivered.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a send falls out of the window before it falls "
    "out of the ring"
) {
    AckBook book;
    Verdicts v;

    book.record_send(1, 1, 100);
    book.process_ack(33, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(34, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 1);
    NETW_CHECK_EQ(lost_bits(v, 0), 100);
    NETW_CHECK_EQ(book.active_count(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack older than the highest does not reopen "
    "the window"
) {
    AckBook book;
    Verdicts v;

    book.record_send(1, 1, 100);
    book.process_ack(40, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 1);

    book.record_send(2, 1, 200);
    book.process_ack(5, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 1);
    NETW_CHECK_EQ(lost_bits(v, 0), 200);
}

TEST_CASE("[Networked][Wire][Hosted] a wrapped seq is still older") {
    AckBook book;
    Verdicts v;

    book.record_send(65500, 1, 500);
    book.process_ack(20, 0, v.delivered, v.lost);

    NETW_CHECK_EQ(v.lost.size(), 1);
    NETW_CHECK_EQ(lost_bits(v, 0), 500);
}

TEST_CASE(
    "[Networked][Wire][Hosted] clearing forgets every send and its acks"
) {
    AckBook book;
    Verdicts v;

    book.record_send(1, 1, 100);
    book.process_ack(40, 0, v.delivered, v.lost);
    book.clear();

    book.record_send(2, 1, 200);
    book.process_ack(3, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a send that would evict a live one is refused "
    "rather than swallowed"
) {
    AckBook book;
    Verdicts v;

    CHECK(book.record_send(1, 1, 100));
    ERR_PRINT_OFF;
    const bool the_colliding_slot_is_refused
        = !book.record_send(1 + AckBook::MAX_IN_FLIGHT, 1, 900);
    ERR_PRINT_ON;
    CHECK(the_colliding_slot_is_refused);
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(1, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(v.delivered.size(), 1);
    NETW_CHECK_EQ(delivered_bits(v, 0), 100);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a slot whose send is settled takes the next one"
) {
    AckBook book;
    Verdicts v;

    CHECK(book.record_send(1, 1, 100));
    book.process_ack(1, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(book.active_count(), 0);

    CHECK(book.record_send(1 + AckBook::MAX_IN_FLIGHT, 1, 900));
    NETW_CHECK_EQ(book.active_count(), 1);
}

TEST_CASE("[Networked][Wire][Hosted] re-recording one seq replaces its send") {
    AckBook book;
    Verdicts v;

    CHECK(book.record_send(7, 1, 700));
    CHECK(book.record_send(7, 1, 701));
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(7, 0, v.delivered, v.lost);
    NETW_CHECK_EQ(delivered_bits(v, 0), 701);
}

} // namespace TestNetwWireAckBook
