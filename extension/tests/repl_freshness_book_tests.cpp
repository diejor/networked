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

    CHECK(book.accept(SENDER, ROUTE, SYNC, 0));
    CHECK_FALSE(book.accept(SENDER, ROUTE, SYNC, 65535));
    CHECK(book.accept(SENDER, ROUTE, DELTA, 1));
}

TEST_CASE("[Networked][Repl][Hosted] an equal sequence is stale") {
    FreshnessBook book;
    CHECK(book.accept(SENDER, ROUTE, SYNC, 10));

    CHECK_FALSE(book.accept(SENDER, ROUTE, SYNC, 10));
    CHECK(book.accept(SENDER, ROUTE, DELTA, 10));
}

TEST_CASE(
    "[Networked][Repl][Hosted] the half window is where forward stops being "
    "forward"
) {
    FreshnessBook book;
    REQUIRE(book.accept(SENDER, ROUTE, SYNC, 0));

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

TEST_CASE("[Networked][Repl][Hosted] a negative peer id keeps its own stream") {
    FreshnessBook book;
    REQUIRE(book.accept(-2, ROUTE, SYNC, 500));
    CHECK(book.accept(2, ROUTE, SYNC, 1));
    NETW_CHECK_EQ(book.stream_count(), 2);

    book.clear_peer(-2);
    NETW_CHECK_EQ(book.stream_count(), 1);
    CHECK_FALSE(book.accept(2, ROUTE, SYNC, 1));
}

TEST_CASE(
    "[Networked][Repl][Hosted] one datagram judges a stream once, and every "
    "later frame of it repeats that verdict"
) {
    FreshnessBook book;
    book.open_datagram();
    REQUIRE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 10));

    CHECK(book.accept_in_datagram(SENDER, ROUTE, SYNC, 10));
    CHECK(book.accept_in_datagram(SENDER, ROUTE, SYNC, 10));
    NETW_CHECK_EQ(book.stale_count(), 0);

    CHECK(book.accept_in_datagram(SENDER, ROUTE, DELTA, 10));
    CHECK(book.accept_in_datagram(SENDER, ROUTE + 1, SYNC, 10));
    CHECK(book.accept_in_datagram(SENDER + 1, ROUTE, SYNC, 10));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a refused stream stays refused for the rest "
    "of its datagram, and is counted once"
) {
    FreshnessBook book;
    book.open_datagram();
    REQUIRE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 500));

    book.open_datagram();
    CHECK_FALSE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 400));
    CHECK_FALSE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 400));
    CHECK_FALSE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 400));
    NETW_CHECK_EQ(book.stale_count(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a duplicated datagram re-evaluates against "
    "the book and drops whole"
) {
    FreshnessBook book;
    book.open_datagram();
    REQUIRE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 10));
    REQUIRE(book.accept_in_datagram(SENDER, ROUTE, DELTA, 10));

    book.open_datagram();
    CHECK_FALSE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 10));
    CHECK_FALSE(book.accept_in_datagram(SENDER, ROUTE, DELTA, 10));
    NETW_CHECK_EQ(book.stale_count(), 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] opening a datagram forgets the memo but "
    "keeps the book"
) {
    FreshnessBook book;
    book.open_datagram();
    REQUIRE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 10));

    book.open_datagram();
    CHECK(book.accept_in_datagram(SENDER, ROUTE, SYNC, 11));
    NETW_CHECK_EQ(book.stream_count(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] clearing forgets the stale count and the "
    "open datagram's memo"
) {
    FreshnessBook book;
    book.open_datagram();
    REQUIRE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 500));

    book.open_datagram();
    REQUIRE_FALSE(book.accept_in_datagram(SENDER, ROUTE, SYNC, 400));
    NETW_CHECK_EQ(book.stale_count(), 1);

    book.clear();
    NETW_CHECK_EQ(book.stale_count(), 0);
    CHECK(book.accept_in_datagram(SENDER, ROUTE, SYNC, 400));
}

} // namespace TestNetwReplFreshnessBook
