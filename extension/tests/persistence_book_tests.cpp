#include "support/netw_test.h"

#include "godot/rid.hpp"
#include "netw/liveness_core.hpp"
#include "netw/persistence_book.hpp"

namespace TestNetwPersistenceBook {

using namespace godot;
using netw::NetwPersistenceBook;

Ref<NetwPersistenceBook> fresh() {
    Ref<NetwPersistenceBook> book;
    book.instantiate();
    return book;
}

Ref<RefCounted> engine() {
    Ref<RefCounted> out;
    out.instantiate();
    return out;
}

Ref<netw::NetwLivenessCore> minter() {
    Ref<netw::NetwLivenessCore> core;
    core.instantiate();
    return core;
}

TEST_CASE("[Networked][Database][Hosted] an enrolment answers fresh once and "
          "replaces without answering fresh again") {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3] = {
        core->entity_create(),
        core->entity_create(),
        core->entity_create()
    };
    const Ref<NetwPersistenceBook> book = fresh();
    Ref<RefCounted> first = engine();
    Ref<RefCounted> second = engine();
    const RID entity = ids[0];

    CHECK(book->enroll(entity, first));
    CHECK_FALSE(book->enroll(entity, second));
    CHECK(book->engine_of(entity) == second);
    NETW_CHECK_EQ(book->size(), 1);
}

TEST_CASE("[Networked][Database][Hosted] an invalid entity and a null engine "
          "both enroll nothing") {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3] = {
        core->entity_create(),
        core->entity_create(),
        core->entity_create()
    };
    const Ref<NetwPersistenceBook> book = fresh();

    CHECK_FALSE(book->enroll(RID(), engine()));
    CHECK_FALSE(book->enroll(ids[0], Ref<RefCounted>()));
    NETW_CHECK_EQ(book->size(), 0);
}

TEST_CASE("[Networked][Database][Hosted] an entity nothing enrolled answers "
          "no engine rather than a neighbour's") {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3] = {
        core->entity_create(),
        core->entity_create(),
        core->entity_create()
    };
    const Ref<NetwPersistenceBook> book = fresh();
    book->enroll(ids[0], engine());

    CHECK(book->engine_of(ids[1]).is_null());
    CHECK_FALSE(book->has(ids[1]));
    CHECK_FALSE(book->drop(ids[1]));
}

TEST_CASE("[Networked][Database][Hosted] the enrolment order is what the book "
          "answers in, and a drop closes its own gap") {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3] = {
        core->entity_create(),
        core->entity_create(),
        core->entity_create()
    };
    const Ref<NetwPersistenceBook> book = fresh();
    const RID first = ids[0];
    const RID second = ids[1];
    const RID third = ids[2];
    book->enroll(third, engine());
    book->enroll(first, engine());
    book->enroll(second, engine());

    TypedArray<RID> order = book->entities();
    NETW_CHECK_EQ(int(order.size()), 3);
    CHECK(RID(order[0]) == third);
    CHECK(RID(order[1]) == first);
    CHECK(RID(order[2]) == second);

    CHECK(book->drop(first));

    order = book->entities();
    NETW_CHECK_EQ(int(order.size()), 2);
    CHECK(RID(order[0]) == third);
    CHECK(RID(order[1]) == second);
}

TEST_CASE("[Networked][Database][Hosted] a re-enrolment keeps the row's place "
          "rather than moving it to the end") {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3] = {
        core->entity_create(),
        core->entity_create(),
        core->entity_create()
    };
    const Ref<NetwPersistenceBook> book = fresh();
    const RID first = ids[0];
    const RID second = ids[1];
    book->enroll(first, engine());
    book->enroll(second, engine());

    book->enroll(first, engine());

    const TypedArray<RID> order = book->entities();
    NETW_CHECK_EQ(int(order.size()), 2);
    CHECK(RID(order[0]) == first);
}

TEST_CASE("[Networked][Database][Hosted] a cleared book holds no enrolment") {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3] = {
        core->entity_create(),
        core->entity_create(),
        core->entity_create()
    };
    const Ref<NetwPersistenceBook> book = fresh();
    book->enroll(ids[0], engine());
    book->enroll(ids[1], engine());

    book->clear();

    NETW_CHECK_EQ(book->size(), 0);
    NETW_CHECK_EQ(int(book->entities().size()), 0);
}

Dictionary due_on(Object *p_database) {
    Dictionary out;
    out["db"] = p_database;
    return out;
}

TEST_CASE("[Networked][Database][Hosted] due rows batch by database, keeping "
          "both the database order and the row order they arrived in") {
    Ref<RefCounted> left = engine();
    Ref<RefCounted> right = engine();
    Array rows;
    rows.push_back(due_on(left.ptr()));
    rows.push_back(due_on(right.ptr()));
    rows.push_back(due_on(left.ptr()));

    const TypedArray<PackedInt32Array> batches
        = NetwPersistenceBook::group_by_database(rows);

    NETW_CHECK_EQ(int(batches.size()), 2);
    const PackedInt32Array first = batches[0];
    const PackedInt32Array second = batches[1];
    NETW_CHECK_EQ(int(first.size()), 2);
    NETW_CHECK_EQ(int(first[0]), 0);
    NETW_CHECK_EQ(int(first[1]), 2);
    NETW_CHECK_EQ(int(second.size()), 1);
    NETW_CHECK_EQ(int(second[0]), 1);
}

TEST_CASE("[Networked][Database][Hosted] a due row naming no database joins "
          "no batch") {
    Ref<RefCounted> only = engine();
    Array rows;
    rows.push_back(Dictionary());
    rows.push_back(due_on(only.ptr()));
    rows.push_back(due_on(nullptr));

    const TypedArray<PackedInt32Array> batches
        = NetwPersistenceBook::group_by_database(rows);

    NETW_CHECK_EQ(int(batches.size()), 1);
    const PackedInt32Array first = batches[0];
    NETW_CHECK_EQ(int(first.size()), 1);
    NETW_CHECK_EQ(int(first[0]), 1);
}

TEST_CASE("[Networked][Database][Hosted] nothing due is no batch rather than "
          "an empty one") {
    NETW_CHECK_EQ(
        int(NetwPersistenceBook::group_by_database(Array()).size()),
        0
    );
}

} // namespace TestNetwPersistenceBook
