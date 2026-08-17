// What a receiver has already seen, and the three ways a book can lie about it.
//
// Every case here is a frame that must be accepted exactly once. The failures
// this exists to prevent are quiet in both directions: a book that accepts a
// stale frame lets an older row overwrite a newer one, and a book that refuses
// a fresh one stalls a stream forever with no error anywhere.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/repl/freshness_book.hpp"

namespace TestNetwReplFreshnessBook {

using netw::repl::FreshnessBook;

const int SENDER = 2;
const int64_t ROUTE = 7;
const uint8_t SYNC = 19;
const uint8_t DELTA = 20;

TEST_CASE("[Networked][Repl][Hosted] a stream's first datagram is accepted") {
    FreshnessBook book;
    CHECK(book.accept(SENDER, ROUTE, SYNC, 41));
    NETW_CHECK_EQ(book.stream_count(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] freshness advances monotonically across the "
    "u16 ring"
) {
    FreshnessBook book;
    CHECK(book.accept(SENDER, ROUTE, SYNC, 65535));

    // One step forward, not 65535 back. Judging by magnitude here would stall
    // every stream at the moment its sequence wrapped.
    CHECK(book.accept(SENDER, ROUTE, SYNC, 0));
    CHECK_FALSE(book.accept(SENDER, ROUTE, SYNC, 65535));
    CHECK(book.accept(SENDER, ROUTE, DELTA, 1));
}

TEST_CASE("[Networked][Repl][Hosted] an equal sequence is stale") {
    FreshnessBook book;
    CHECK(book.accept(SENDER, ROUTE, SYNC, 10));

    // A datagram is one sequence and its frames are accepted once. Accepting
    // the equal case would apply a duplicate as though it were an update.
    CHECK_FALSE(book.accept(SENDER, ROUTE, SYNC, 10));
    CHECK(book.accept(SENDER, ROUTE, DELTA, 10));
}

TEST_CASE(
    "[Networked][Repl][Hosted] the half window is where forward stops being "
    "forward"
) {
    FreshnessBook book;
    REQUIRE(book.accept(SENDER, ROUTE, SYNC, 0));

    // 32767 ahead is forward, 32768 is the far side of the ring and reads as
    // behind. The boundary is the whole definition, so it is pinned rather
    // than sampled.
    CHECK(book.accept(SENDER, ROUTE, SYNC, 32767));
    CHECK_FALSE(book.accept(SENDER, ROUTE, SYNC, 32767 + 32768));
    CHECK(book.accept(SENDER, ROUTE, SYNC, 32767 + 32767));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a stream is a route, a sender and a channel, "
    "and no two of them share a book"
) {
    FreshnessBook book;
    REQUIRE(book.accept(SENDER, ROUTE, SYNC, 100));

    CHECK(book.accept(SENDER, ROUTE, DELTA, 1));
    CHECK(book.accept(SENDER, ROUTE + 1, SYNC, 1));
    CHECK(book.accept(SENDER + 1, ROUTE, SYNC, 1));
    NETW_CHECK_EQ(book.stream_count(), 4);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a route that dies takes its streams, so the "
    "next entity at that route is judged fresh"
) {
    FreshnessBook book;
    REQUIRE(book.accept(SENDER, ROUTE, SYNC, 500));
    REQUIRE(book.accept(SENDER, ROUTE + 1, SYNC, 500));

    book.clear_route(ROUTE);
    NETW_CHECK_EQ(book.stream_count(), 1);

    // A sequence the dead route had already passed. Inheriting it would make
    // the new entity's first datagrams read as stale.
    CHECK(book.accept(SENDER, ROUTE, SYNC, 1));
    CHECK_FALSE(book.accept(SENDER, ROUTE + 1, SYNC, 1));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves is forgotten on every "
    "route, because peer ids are reused"
) {
    FreshnessBook book;
    REQUIRE(book.accept(SENDER, ROUTE, SYNC, 500));
    REQUIRE(book.accept(SENDER, ROUTE + 1, DELTA, 500));
    REQUIRE(book.accept(SENDER + 1, ROUTE, SYNC, 500));

    book.clear_peer(SENDER);
    NETW_CHECK_EQ(book.stream_count(), 1);

    CHECK(book.accept(SENDER, ROUTE, SYNC, 1));
    CHECK(book.accept(SENDER, ROUTE + 1, DELTA, 1));
    // The peer that stayed keeps what it knew.
    CHECK_FALSE(book.accept(SENDER + 1, ROUTE, SYNC, 1));
}

TEST_CASE("[Networked][Repl][Hosted] clearing forgets every stream") {
    FreshnessBook book;
    REQUIRE(book.accept(SENDER, ROUTE, SYNC, 500));
    REQUIRE(book.accept(SENDER + 1, ROUTE + 1, DELTA, 500));

    book.clear();
    NETW_CHECK_EQ(book.stream_count(), 0);
    CHECK(book.accept(SENDER, ROUTE, SYNC, 1));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a negative peer id keeps its own stream"
) {
    FreshnessBook book;
    // Godot hands out 32-bit peer ids and the shell passes them through. A
    // packing that dropped the sign would alias two live peers onto one book.
    REQUIRE(book.accept(-2, ROUTE, SYNC, 500));
    CHECK(book.accept(2, ROUTE, SYNC, 1));
    NETW_CHECK_EQ(book.stream_count(), 2);

    book.clear_peer(-2);
    NETW_CHECK_EQ(book.stream_count(), 1);
    CHECK_FALSE(book.accept(2, ROUTE, SYNC, 1));
}

} // namespace TestNetwReplFreshnessBook
