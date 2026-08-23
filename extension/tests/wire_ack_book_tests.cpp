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

int64_t delivered_id(Verdicts &v, uint32_t at) {
    return at < v.delivered.size() ? v.delivered[at].send_id : -1;
}

int64_t lost_id(Verdicts &v, uint32_t at) {
    return at < v.lost.size() ? v.lost[at].send_id : -1;
}

TEST_CASE("[Networked][Wire][Hosted] an ack reports the send it names") {
    AckBook book;
    Verdicts v;

    book.record_send(7, 19, 700);
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(7, v.delivered, v.lost);
    NETW_CHECK_EQ(v.delivered.size(), 1);
    NETW_CHECK_EQ(delivered_id(v, 0), 700);
    NETW_CHECK_EQ(v.lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack for a seq the book never sent reports "
    "nothing"
) {
    AckBook book;
    Verdicts v;

    book.record_send(7, 19, 700);
    book.process_ack(8, v.delivered, v.lost);

    NETW_CHECK_EQ(v.delivered.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a send falls out of the window before it falls "
    "out of the ring"
) {
    AckBook book;
    Verdicts v;

    book.record_send(1, 19, 100);
    book.process_ack(33, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(34, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 1);
    NETW_CHECK_EQ(lost_id(v, 0), 100);
    NETW_CHECK_EQ(book.active_count(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack older than the highest does not reopen "
    "the window"
) {
    AckBook book;
    Verdicts v;

    book.record_send(1, 19, 100);
    book.process_ack(40, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 1);

    book.record_send(2, 19, 200);
    book.process_ack(5, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 1);
    NETW_CHECK_EQ(lost_id(v, 0), 200);
}

TEST_CASE("[Networked][Wire][Hosted] a wrapped seq is still older") {
    AckBook book;
    Verdicts v;

    book.record_send(65500, 19, 500);
    book.process_ack(20, v.delivered, v.lost);

    NETW_CHECK_EQ(v.lost.size(), 1);
    NETW_CHECK_EQ(lost_id(v, 0), 500);
}

TEST_CASE("[Networked][Wire][Hosted] clearing forgets every send and its acks") {
    AckBook book;
    Verdicts v;

    book.record_send(1, 19, 100);
    book.process_ack(40, v.delivered, v.lost);
    book.clear();

    book.record_send(2, 19, 200);
    book.process_ack(3, v.delivered, v.lost);
    NETW_CHECK_EQ(v.lost.size(), 0);
    NETW_CHECK_EQ(book.active_count(), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a send that would evict a live one is refused "
    "rather than swallowed"
) {
    AckBook book;
    Verdicts v;

    CHECK(book.record_send(1, 19, 100));
    // 129 hashes to the same ring slot as 1. Accepting it would drop send 100
    // with nothing left to ack it or report it lost, so the caller would never
    // learn the send it was told had been recorded is gone.
    CHECK_FALSE(book.record_send(1 + AckBook::MAX_IN_FLIGHT, 19, 900));
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(1, v.delivered, v.lost);
    NETW_CHECK_EQ(v.delivered.size(), 1);
    NETW_CHECK_EQ(delivered_id(v, 0), 100);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a slot whose send is settled takes the next one"
) {
    AckBook book;
    Verdicts v;

    CHECK(book.record_send(1, 19, 100));
    book.process_ack(1, v.delivered, v.lost);
    NETW_CHECK_EQ(book.active_count(), 0);

    CHECK(book.record_send(1 + AckBook::MAX_IN_FLIGHT, 19, 900));
    NETW_CHECK_EQ(book.active_count(), 1);
}

TEST_CASE("[Networked][Wire][Hosted] re-recording one seq replaces its send") {
    AckBook book;
    Verdicts v;

    CHECK(book.record_send(7, 19, 700));
    CHECK(book.record_send(7, 19, 701));
    NETW_CHECK_EQ(book.active_count(), 1);

    book.process_ack(7, v.delivered, v.lost);
    NETW_CHECK_EQ(delivered_id(v, 0), 701);
}

} // namespace TestNetwWireAckBook
