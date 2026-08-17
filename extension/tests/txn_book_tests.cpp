// Laws for NetwTxnBook.
//
// Three paths ask this book about the same transaction and never see each
// other: a reply arriving, a timeout sweep, and a peer dropping. Each case
// below states what they must agree on.

#include "support/netw_test.h"

#include "netw/txn_book.hpp"

namespace TestNetwTxnBook {

using namespace godot;
using netw::NetwTxnBook;

Ref<NetwTxnBook> make_book() {
    Ref<NetwTxnBook> book;
    book.instantiate();
    return book;
}

PackedInt64Array peers(std::initializer_list<int64_t> p_peers) {
    PackedInt64Array out;
    for (const int64_t peer : p_peers) {
        out.push_back(peer);
    }
    return out;
}

TEST_CASE(
    "[Networked][Rpc][Hosted] L1 an id names one request for the life of a "
    "session"
) {
    Ref<NetwTxnBook> book = make_book();

    const int64_t first = book->mint();
    const int64_t second = book->mint();
    NETW_CHECK_EQ(first, 1);
    NETW_CHECK_EQ(second, 2);

    SUBCASE("minting opens nothing, so a request never sent leaves no row") {
        NETW_CHECK_EQ(book->size(), 0);
        CHECK_FALSE(book->is_open(first));
    }

    SUBCASE("a closed id is not handed out again") {
        book->open(first, peers({4}), 10);
        CHECK(book->close(first));
        CHECK_FALSE(book->close(first));
        NETW_CHECK_EQ(book->mint(), 3);
    }
}

TEST_CASE(
    "[Networked][Rpc][Hosted] L2 only the peers a request addressed may "
    "answer it"
) {
    Ref<NetwTxnBook> book = make_book();
    const int64_t txn = book->mint();
    book->open(txn, peers({4, 5}), 10);

    CHECK(book->admits(txn, 4));
    CHECK(book->admits(txn, 5));
    CHECK_FALSE(book->admits(txn, 6));

    SUBCASE("a closed transaction admits nobody it once addressed") {
        book->close(txn);
        CHECK_FALSE(book->admits(txn, 4));
    }

    SUBCASE("an id that was never opened admits nobody") {
        CHECK_FALSE(book->admits(book->mint(), 4));
    }
}

TEST_CASE(
    "[Networked][Rpc][Hosted] L3 the sweep names a request once and closes it "
    "as it names it"
) {
    Ref<NetwTxnBook> book = make_book();
    const int64_t early = book->mint();
    const int64_t late = book->mint();
    book->open(early, peers({4}), 10);
    book->open(late, peers({4}), 20);

    CHECK(book->expire(9).is_empty());

    PackedInt64Array due = book->expire(10);
    NETW_CHECK_EQ(due.size(), 1);
    NETW_CHECK_EQ(due[0], early);

    SUBCASE("a second sweep at the same tick names it no second time") {
        due = book->expire(10);
        CHECK(due.is_empty());
        NETW_CHECK_EQ(book->size(), 1);
    }

    SUBCASE("a later sweep takes the rest, in minting order") {
        const int64_t third = book->mint();
        book->open(third, peers({4}), 15);
        due = book->expire(30);
        NETW_CHECK_EQ(due.size(), 2);
        NETW_CHECK_EQ(due[0], late);
        NETW_CHECK_EQ(due[1], third);
        NETW_CHECK_EQ(book->size(), 0);
    }
}

TEST_CASE(
    "[Networked][Rpc][Hosted] L4 a peer dropping names what it was owed "
    "without closing it"
) {
    Ref<NetwTxnBook> book = make_book();
    const int64_t solo = book->mint();
    const int64_t group = book->mint();
    const int64_t other = book->mint();
    book->open(solo, peers({4}), 100);
    book->open(group, peers({4, 5}), 100);
    book->open(other, peers({5}), 100);

    const PackedInt64Array owed = book->waiting_on(4);
    NETW_CHECK_EQ(owed.size(), 2);
    NETW_CHECK_EQ(owed[0], solo);
    NETW_CHECK_EQ(owed[1], group);

    // A drop is not a timeout: a group request the peer was one of may still
    // complete, so the caller decides which of these close.
    NETW_CHECK_EQ(book->size(), 3);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] L5 draining empties the book before the caller "
    "settles anything"
) {
    Ref<NetwTxnBook> book = make_book();
    const int64_t first = book->mint();
    const int64_t second = book->mint();
    book->open(first, peers({4}), 100);
    book->open(second, peers({5}), 100);

    const PackedInt64Array abandoned = book->drain();
    NETW_CHECK_EQ(abandoned.size(), 2);
    NETW_CHECK_EQ(abandoned[0], first);
    NETW_CHECK_EQ(abandoned[1], second);
    NETW_CHECK_EQ(book->size(), 0);

    // A settle that opens a fresh request cannot see the abandoned ones.
    const int64_t reopened = book->mint();
    book->open(reopened, peers({6}), 100);
    NETW_CHECK_EQ(book->drain().size(), 1);
}

} // namespace TestNetwTxnBook
