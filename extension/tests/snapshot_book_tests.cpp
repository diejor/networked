#include "support/netw_test.h"

#include "netw/snapshot_book.hpp"

namespace TestNetwSnapshotBook {

using namespace godot;
using netw::SnapshotBook;

SnapshotBook make_book(double default_interval = 0.0) {
    SnapshotBook book;
    book.set_default_interval(default_interval);
    return book;
}

Dictionary row(const StringName &property, const Variant &value) {
    Dictionary out;
    out[property] = value;
    return out;
}

TEST_CASE(
    "[Networked][Persistence][Hosted] L1 a column comes up on its own cadence"
) {
    const double GOLD_INTERVAL = 0.5;
    const double DEFAULT_INTERVAL = 1.0;
    SnapshotBook book = make_book(DEFAULT_INTERVAL);
    book.declare("gold", GOLD_INTERVAL);
    book.declare("name", 0.0);

    Array due = book.advance(GOLD_INTERVAL);
    NETW_CHECK_EQ(due.size(), 1);
    CHECK(due.has(StringName("gold")));

    SUBCASE("a column with no interval of its own falls back to the default") {
        due = book.advance(0.5);
        NETW_CHECK_EQ(due.size(), 2);
        CHECK(due.has(StringName("name")));
    }

    SUBCASE("coming up rearms the column rather than leaving it due") {
        due = book.advance(0.25);
        NETW_CHECK_EQ(due.size(), 0);
    }

    SUBCASE("a later declaration replaces the cadence rather than the column") {
        book.declare("gold", 4.0);
        NETW_CHECK_EQ(book.properties().size(), 2);
        due = book.advance(1.0);
        CHECK_FALSE(due.has(StringName("gold")));
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] L2 a column that came up and did not "
    "move is owed nothing"
) {
    SnapshotBook book = make_book();
    book.declare("gold", 0.0);

    Dictionary owed_before_any_write = book.changed(row("gold", 10));
    NETW_CHECK_EQ(owed_before_any_write.size(), 1);
    Dictionary owed = owed_before_any_write;

    book.commit(owed);

    SUBCASE("the same value is not owed twice") {
        owed = book.changed(row("gold", 10));
        NETW_CHECK_EQ(owed.size(), 0);
    }

    SUBCASE("a moved value is owed again") {
        owed = book.changed(row("gold", 11));
        NETW_CHECK_EQ(owed.size(), 1);
        NETW_CHECK_EQ(int(owed["gold"]), 11);
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] L3 committing speaks only for the "
    "columns it names, and adopting for all of them"
) {
    SnapshotBook book = make_book();
    book.declare("gold", 0.0);
    book.declare("name", 0.0);

    Dictionary whole;
    whole["gold"] = 10;
    whole["name"] = "ana";
    book.adopt(whole);
    CHECK_FALSE(book.differs(whole));

    SUBCASE("a subset commit leaves the columns it left out believed") {
        book.commit(row("gold", 11));
        Dictionary moved;
        moved["gold"] = 11;
        moved["name"] = "ana";
        CHECK_FALSE(book.differs(moved));
    }

    SUBCASE("adopting a subset forgets the columns it left out") {
        book.adopt(row("gold", 10));
        CHECK(book.differs(whole));
    }

    SUBCASE("a value that moved makes the entity dirty") {
        Dictionary moved;
        moved["gold"] = 10;
        moved["name"] = "bo";
        CHECK(book.differs(moved));
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] an archetype that marked no field "
    "declares nothing"
) {
    SnapshotBook book = make_book();
    CHECK(book.is_empty());

    book.declare("gold", 0.0);
    CHECK_FALSE(book.is_empty());
}

} // namespace TestNetwSnapshotBook
