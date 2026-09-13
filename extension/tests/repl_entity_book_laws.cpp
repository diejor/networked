#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/repl/entity_book.hpp"

using namespace godot;

namespace TestNetwReplEntityBook {

using godot::LocalVector;
using netw::repl::EntityBook;
using netw::repl::EntitySlot;
using netw::repl::EntityWrite;

EntitySlot slot_of(int64_t p_route, uint32_t p_column) {
    EntitySlot out;
    out.route = p_route;
    out.comp = 0;
    out.column = p_column;
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a reference to a bound route resolves at once"
) {
    EntityBook book;
    book.bind_route(42);

    NETW_CHECK_EQ(book.resolve(slot_of(7, 0), 42), int64_t(42));
    NETW_CHECK_EQ(book.pending_count(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a null reference resolves null and waits for "
    "nothing"
) {
    EntityBook book;

    NETW_CHECK_EQ(book.resolve(slot_of(7, 0), 0), int64_t(0));
    NETW_CHECK_EQ(book.pending_count(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a reference to a route bound later resolves on "
    "bind"
) {
    EntityBook book;

    NETW_CHECK_EQ(book.resolve(slot_of(7, 2), 42), int64_t(0));
    NETW_CHECK_EQ(book.pending_count(), 1);

    book.bind_route(42);
    LocalVector<EntityWrite> completed;
    book.release(42, completed);

    REQUIRE(completed.size() == 1);
    NETW_CHECK_EQ(completed[0].route, int64_t(42));
    NETW_CHECK_EQ(completed[0].slot.route, int64_t(7));
    NETW_CHECK_EQ(int64_t(completed[0].slot.column), int64_t(2));
    NETW_CHECK_EQ(book.pending_count(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a reference to a tombstoned route resolves null"
) {
    EntityBook book;
    NETW_CHECK_EQ(book.resolve(slot_of(7, 2), 42), int64_t(0));

    book.tombstone_route(42);
    LocalVector<EntityWrite> completed;
    book.release(42, completed);

    REQUIRE(completed.size() == 1);
    NETW_CHECK_EQ(completed[0].route, int64_t(0));
    NETW_CHECK_EQ(book.pending_count(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a route that tombstoned before it was named "
    "resolves null rather than waiting forever"
) {
    EntityBook book;
    book.bind_route(42);
    book.tombstone_route(42);

    NETW_CHECK_EQ(book.resolve(slot_of(7, 0), 42), int64_t(0));
    NETW_CHECK_EQ(book.pending_count(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] one slot waits for one route, because a column "
    "rewritten while pending owes the newer reference and not both"
) {
    EntityBook book;
    NETW_CHECK_EQ(book.resolve(slot_of(7, 0), 42), int64_t(0));
    NETW_CHECK_EQ(book.resolve(slot_of(7, 0), 43), int64_t(0));
    NETW_CHECK_EQ(book.pending_count(), 1);

    book.bind_route(42);
    LocalVector<EntityWrite> completed;
    book.release(42, completed);
    NETW_CHECK_EQ(completed.size(), 0);

    book.bind_route(43);
    book.release(43, completed);
    REQUIRE(completed.size() == 1);
    NETW_CHECK_EQ(completed[0].route, int64_t(43));
}

TEST_CASE(
    "[Networked][Repl][Hosted] two slots waiting on one route both complete "
    "on its bind"
) {
    EntityBook book;
    book.resolve(slot_of(7, 0), 42);
    book.resolve(slot_of(8, 0), 42);
    NETW_CHECK_EQ(book.pending_count(), 2);

    book.bind_route(42);
    LocalVector<EntityWrite> completed;
    book.release(42, completed);
    NETW_CHECK_EQ(completed.size(), 2);
    NETW_CHECK_EQ(book.pending_count(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a slot whose own row is gone waits for nothing"
) {
    EntityBook book;
    book.resolve(slot_of(7, 0), 42);
    book.resolve(slot_of(8, 0), 42);

    book.forget_slot(7, 0);
    NETW_CHECK_EQ(book.pending_count(), 1);

    book.bind_route(42);
    LocalVector<EntityWrite> completed;
    book.release(42, completed);
    REQUIRE(completed.size() == 1);
    NETW_CHECK_EQ(completed[0].slot.route, int64_t(8));
}

} // namespace TestNetwReplEntityBook
