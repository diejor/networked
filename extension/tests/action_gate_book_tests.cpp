#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/action_gate_book.hpp"

namespace TestNetwActionGateBook {

using namespace godot;
using netw::ActionGateBook;

TEST_CASE(
    "[Networked][Spawn][Hosted] AG1 a gate hides its owner until the display "
    "tick reaches the action tick, then restores what the scene declared"
) {
    Node3D *owner = memnew(Node3D);
    owner->set_visible(true);
    ActionGateBook book;

    CHECK(book.arm(1, owner, 10, 4));
    CHECK_FALSE(owner->is_visible());
    NETW_CHECK_EQ(int64_t(book.size()), 1);

    CHECK(book.reveal_reached(9).is_empty());
    CHECK_FALSE(owner->is_visible());

    const PackedInt64Array woken = book.reveal_reached(10);
    NETW_CHECK_EQ(int64_t(woken.size()), 1);
    NETW_CHECK_EQ(int64_t(woken[0]), 1);
    CHECK(owner->is_visible());
    NETW_CHECK_EQ(int64_t(book.size()), 0);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] AG2 an owner the scene had already hidden is "
    "revealed back to hidden, because the gate restores rather than shows"
) {
    Node3D *owner = memnew(Node3D);
    owner->set_visible(false);
    ActionGateBook book;

    REQUIRE(book.arm(1, owner, 10, 0));
    CHECK_FALSE(owner->is_visible());

    book.reveal_reached(10);
    CHECK_FALSE(owner->is_visible());
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] AG3 a display already past the action tick "
    "arms nothing, and neither does a route with no action tick"
) {
    Node3D *owner = memnew(Node3D);
    owner->set_visible(true);
    ActionGateBook book;

    CHECK_FALSE(book.arm(1, owner, 10, 10));
    CHECK_FALSE(book.arm(1, owner, 10, 11));
    CHECK_FALSE(book.arm(1, owner, -1, 0));
    CHECK_FALSE(book.arm(1, nullptr, 10, 0));
    NETW_CHECK_EQ(int64_t(book.size()), 0);
    CHECK(owner->is_visible());
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] AG4 a non-visual owner is left alone rather "
    "than gated, because there is nothing to hide"
) {
    Node *owner = memnew(Node);
    ActionGateBook book;

    CHECK_FALSE(book.arm(1, owner, 10, 0));
    NETW_CHECK_EQ(int64_t(book.size()), 0);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] AG5 one route gates once, and a sweep wakes "
    "only the routes whose tick has come"
) {
    Node3D *early = memnew(Node3D);
    Node3D *late = memnew(Node3D);
    early->set_visible(true);
    late->set_visible(true);
    ActionGateBook book;

    REQUIRE(book.arm(1, early, 5, 0));
    REQUIRE(book.arm(2, late, 50, 0));
    CHECK_FALSE(book.arm(1, early, 5, 0));
    NETW_CHECK_EQ(int64_t(book.size()), 2);

    const PackedInt64Array woken = book.reveal_reached(5);
    NETW_CHECK_EQ(int64_t(woken.size()), 1);
    NETW_CHECK_EQ(int64_t(woken[0]), 1);
    CHECK(early->is_visible());
    CHECK_FALSE(late->is_visible());
    CHECK(book.holds(2));

    memdelete(early);
    memdelete(late);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] AG6 dropping a route reveals it, and clearing "
    "forgets every gate without touching what it hid"
) {
    Node3D *dropped = memnew(Node3D);
    Node3D *cleared = memnew(Node3D);
    dropped->set_visible(true);
    cleared->set_visible(true);
    ActionGateBook book;

    REQUIRE(book.arm(1, dropped, 50, 0));
    REQUIRE(book.arm(2, cleared, 50, 0));

    CHECK(book.reveal(1));
    CHECK(dropped->is_visible());
    CHECK_FALSE(book.reveal(1));

    book.clear();
    NETW_CHECK_EQ(int64_t(book.size()), 0);
    CHECK_FALSE(cleared->is_visible());

    memdelete(dropped);
    memdelete(cleared);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] AG7 an owner freed while gated is dropped by "
    "the sweep rather than followed into a dangling write"
) {
    Node3D *owner = memnew(Node3D);
    owner->set_visible(true);
    ActionGateBook book;

    REQUIRE(book.arm(1, owner, 10, 0));
    memdelete(owner);

    const PackedInt64Array woken = book.reveal_reached(10);
    NETW_CHECK_EQ(int64_t(woken.size()), 1);
    NETW_CHECK_EQ(int64_t(book.size()), 0);
}

} // namespace TestNetwActionGateBook
