// What a peer is owed of an on-change row, and when it stops being owed it.
//
// Two edges carry this book. The gain edge, where a peer with no baseline is
// owed the whole row rather than nothing, and the aliasing edge, where a
// container the game mutates in place would make the poll compare a value
// against itself and never see it change again. Each has its own case here
// because each reads as correct while it is wrong.

#include "support/netw_test.h"

#include <cstdint>
#include <initializer_list>

#include "godot/variant.hpp"
#include "netw/repl/watch_book.hpp"

namespace TestNetwReplWatchBook {

using godot::Array;
using godot::Dictionary;
using godot::PackedInt32Array;
using godot::Variant;
using netw::repl::WatchBook;

const int64_t KEY = 11;
const int64_t PEER = 2;
const int64_t OTHER = 3;

Array row(std::initializer_list<Variant> p_values) {
    Array out;
    for (const Variant &value : p_values) {
        out.push_back(value);
    }
    return out;
}

PackedInt32Array peers(std::initializer_list<int32_t> p_peers) {
    PackedInt32Array out;
    for (int32_t peer : p_peers) {
        out.push_back(peer);
    }
    return out;
}

struct Owed {
    uint64_t mask = 0;
    Array values;
};

Owed owed(const WatchBook &p_book, int64_t p_peer) {
    Owed out;
    p_book.mask_for(KEY, p_peer, out.mask, out.values);
    return out;
}

TEST_CASE("[Networked][Repl][Hosted] a peer with no baseline is owed the whole "
          "row") {
    WatchBook book;
    book.poll(KEY, row({1, 2, 3}), Array());

    const Owed first = owed(book, PEER);

    NETW_CHECK_EQ(int64_t(first.mask), 0b111);
    NETW_CHECK_EQ(int64_t(first.values.size()), 3);
}

TEST_CASE("[Networked][Repl][Hosted] a committed peer is owed only what moved "
          "after") {
    WatchBook book;
    book.poll(KEY, row({1, 2, 3}), Array());
    book.commit(KEY, PEER);

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0);

    book.poll(KEY, row({1, 9, 3}), Array());
    const Owed second = owed(book, PEER);

    NETW_CHECK_EQ(int64_t(second.mask), 0b010);
    REQUIRE(second.values.size() == 1);
    NETW_CHECK_EQ(int64_t(second.values[0]), 9);
}

TEST_CASE("[Networked][Repl][Hosted] one peer's commit does not settle what "
          "another is owed") {
    WatchBook book;
    book.poll(KEY, row({1}), Array());
    book.commit(KEY, PEER);

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0);
    NETW_CHECK_EQ(int64_t(owed(book, OTHER).mask), 0b1);
}

TEST_CASE("[Networked][Repl][Hosted] an unreadable field keeps its stamp "
          "rather than reading as a change") {
    WatchBook book;
    book.poll(KEY, row({1, 2}), row({true, true}));
    book.commit(KEY, PEER);

    // The second field could not be read this pass. Its previous value stands.
    book.poll(KEY, row({1, Variant()}), row({true, false}));

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0);
}

TEST_CASE("[Networked][Repl][Hosted] a field the first poll could not read is "
          "owed as soon as it can be") {
    WatchBook book;
    book.poll(KEY, row({1, Variant()}), row({true, false}));
    book.commit(KEY, PEER);

    // The seed stamped the unreadable field zero, so a baseline taken over it
    // must not answer that the peer holds it.
    book.poll(KEY, row({1, 7}), row({true, true}));

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0b10);
}

TEST_CASE("[Networked][Repl][Hosted] a container mutated in place is still "
          "seen to change") {
    WatchBook book;
    Array live;
    live.push_back(1);
    book.poll(KEY, row({live}), Array());
    book.commit(KEY, PEER);

    // The game holds this array and appends to it. A book that stored the
    // reference would now be comparing it against itself.
    live.push_back(2);
    book.poll(KEY, row({live}), Array());

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0b1);
}

TEST_CASE("[Networked][Repl][Hosted] a dictionary mutated in place is still "
          "seen to change") {
    WatchBook book;
    Dictionary live;
    live[godot::StringName("hp")] = 10;
    book.poll(KEY, row({live}), Array());
    book.commit(KEY, PEER);

    live[godot::StringName("hp")] = 4;
    book.poll(KEY, row({live}), Array());

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0b1);
}

TEST_CASE("[Networked][Repl][Hosted] a peer that leaves the recipients is owed "
          "the whole row when it returns") {
    WatchBook book;
    book.poll(KEY, row({1, 2}), Array());
    book.commit(KEY, PEER);
    book.commit(KEY, OTHER);

    book.retain_baselines(KEY, peers({int32_t(OTHER)}));

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0b11);
    NETW_CHECK_EQ(int64_t(owed(book, OTHER).mask), 0);
}

TEST_CASE("[Networked][Repl][Hosted] a reconnecting peer is owed the whole row "
          "on every stream") {
    WatchBook book;
    book.poll(KEY, row({1}), Array());
    book.poll(KEY + 1, row({1}), Array());
    book.commit(KEY, PEER);
    book.commit(KEY + 1, PEER);

    book.clear_peer(PEER);

    uint64_t other_mask = 0;
    Array other_values;
    book.mask_for(KEY + 1, PEER, other_mask, other_values);
    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0b1);
    NETW_CHECK_EQ(int64_t(other_mask), 0b1);
}

TEST_CASE("[Networked][Repl][Hosted] a reset reseeds the row and a cleared "
          "baseline does not") {
    WatchBook book;
    book.poll(KEY, row({1, 2}), Array());
    book.commit(KEY, PEER);

    book.clear_baselines(KEY);
    CHECK(book.is_inited(KEY));
    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0b11);

    book.reset(KEY);
    CHECK(!book.is_inited(KEY));
    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0);
}

TEST_CASE("[Networked][Repl][Hosted] a stream never polled owes nothing") {
    WatchBook book;

    NETW_CHECK_EQ(int64_t(owed(book, PEER).mask), 0);
    CHECK(!book.is_inited(KEY));
}

} // namespace TestNetwReplWatchBook
