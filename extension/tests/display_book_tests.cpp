#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "netw/display/book.hpp"
#include "netw/display/runtime.hpp"
#include "netw/liveness_core.hpp"

namespace TestDisplayBook {

using namespace godot;
using netw::display::Book;
using netw::display::Decl;

Ref<Book> make_book() {
    Ref<Book> book;
    book.instantiate();
    return book;
}

Ref<netw::NetwLivenessCore> minter() {
    Ref<netw::NetwLivenessCore> core;
    core.instantiate();
    return core;
}

TEST_CASE(
    "[Networked][Display][Hosted] B1 one entity has two names and the book "
    "answers its runtime under either"
) {
    Ref<Book> book = make_book();
    const RID entity = minter()->entity_create();
    book->enroll(entity, 7);
    netw::display::Runtime *runtime = book->open_runtime(entity);

    CHECK(book->runtime_of(entity) == runtime);
    CHECK(book->runtime_at(7) == runtime);
    CHECK(book->entity_at(7) == entity);
    NETW_CHECK_EQ(book->route_of(entity), int64_t(7));
    NETW_CHECK_EQ(book->size(), 1);
}

TEST_CASE(
    "[Networked][Display][Hosted] B2 a dropped route takes its whole row, "
    "declaration included, without asking anything outside the book"
) {
    Ref<Book> book = make_book();
    const RID entity = minter()->entity_create();
    Decl decl;

    book->enroll(entity, 7);
    book->open_runtime(entity);
    book->set_decl(entity, decl);

    book->drop_route(7);

    CHECK(book->decl_ptr(entity) == nullptr);
    CHECK(book->runtime_of(entity) == nullptr);
    CHECK(book->runtime_at(7) == nullptr);
    CHECK(!book->entity_at(7).is_valid());
    NETW_CHECK_EQ(book->size(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] B3 the pump reads runtimes in enrolment "
    "order, and a row holding none is not one"
) {
    Ref<Book> book = make_book();
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3]
        = {core->entity_create(), core->entity_create(), core->entity_create()};
    book->enroll(ids[0], 1);
    book->enroll(ids[1], 2);
    book->enroll(ids[2], 3);
    netw::display::Runtime *first = book->open_runtime(ids[0]);
    netw::display::Runtime *second = book->open_runtime(ids[2]);

    const LocalVector<netw::display::Runtime *> pumped = book->runtimes();

    NETW_CHECK_EQ(int(pumped.size()), 2);
    CHECK(pumped[0] == first);
    CHECK(pumped[1] == second);
}

TEST_CASE(
    "[Networked][Display][Hosted] B4 re-enrolling an entity releases the "
    "route it held, so a stale route answers nothing"
) {
    Ref<Book> book = make_book();
    const RID entity = minter()->entity_create();
    book->enroll(entity, 7);
    netw::display::Runtime *runtime = book->open_runtime(entity);
    book->enroll(entity, 9);

    CHECK(book->runtime_at(9) == runtime);
    CHECK(book->runtime_at(7) == nullptr);
    CHECK(!book->entity_at(7).is_valid());
    NETW_CHECK_EQ(book->size(), 1);
}

TEST_CASE(
    "[Networked][Display][Hosted] B5 a declaration outlives no route because "
    "it never needed one, so it can land before the entity goes live"
) {
    Ref<Book> book = make_book();
    const RID entity = minter()->entity_create();
    Decl decl;
    decl.trace_interval = 5;

    book->set_decl(entity, decl);

    const Decl *stored = book->decl_ptr(entity);
    REQUIRE(stored != nullptr);
    NETW_CHECK_EQ(stored->trace_interval, 5);
    NETW_CHECK_EQ(book->route_of(entity), int64_t(0));
    NETW_CHECK_EQ(int(book->runtimes().size()), 0);
    NETW_CHECK_EQ(book->size(), 1);

    book->drop(entity);
    CHECK(book->decl_ptr(entity) == nullptr);
    NETW_CHECK_EQ(book->size(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] B6 a mark reports once per entity, and the "
    "take answers every entity still marked"
) {
    Ref<Book> book = make_book();
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID one = core->entity_create();
    const RID two = core->entity_create();
    netw_test::CallLog log;
    book->set_went_dirty(log.callable("dirtied"));

    book->enroll(one, 7);
    netw::display::Runtime *runtime = book->open_runtime(one);

    book->mark_dirty(one, netw::display::DIRT_RUNTIME);
    book->mark_dirty(one, netw::display::DIRT_RUNTIME);
    book->mark_dirty(two, netw::display::DIRT_RUNTIME);

    NETW_CHECK_EQ(log.count("dirtied"), 2);
    CHECK(RID(log.args("dirtied", 0)[0]) == one);
    CHECK(RID(log.args("dirtied", 1)[0]) == two);
    CHECK(runtime->get_rebuild_queued());
    CHECK(book->is_dirty(two));

    const TypedArray<RID> taken = book->take_dirty();

    NETW_CHECK_EQ(taken.size(), 2);
    CHECK(RID(taken[0]) == one);
    CHECK(RID(taken[1]) == two);
    CHECK(!book->is_dirty(one));

    book->mark_dirty(one, netw::display::DIRT_RUNTIME);
    NETW_CHECK_EQ(log.count("dirtied"), 3);
}

TEST_CASE(
    "[Networked][Display][Hosted] B7 a retiring row takes its pending rebuild "
    "with it, because a gone entity has nothing left to repair"
) {
    Ref<Book> book = make_book();
    const RID entity = minter()->entity_create();

    book->enroll(entity, 7);
    book->open_runtime(entity);
    book->mark_dirty(entity, netw::display::DIRT_RUNTIME);

    book->drop_route(7);

    CHECK(!book->is_dirty(entity));
    NETW_CHECK_EQ(book->take_dirty().size(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] B8 an entity the book has no row for is "
    "still marked, and a listener that repaired it withdraws the mark"
) {
    Ref<Book> book = make_book();
    const RID entity = minter()->entity_create();
    netw_test::CallLog log;
    book->set_went_dirty(log.callable("dirtied"));

    book->mark_dirty(entity, netw::display::DIRT_RUNTIME);
    book->mark_dirty(RID(), netw::display::DIRT_RUNTIME);

    NETW_CHECK_EQ(log.count("dirtied"), 1);
    CHECK(book->is_dirty(entity));
    NETW_CHECK_EQ(book->size(), 0);

    book->clear_dirty(entity);

    CHECK(!book->is_dirty(entity));
    NETW_CHECK_EQ(book->take_dirty().size(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] B9 a param write publishes the declaration "
    "it was offered, and only a runtime write waits for a drain"
) {
    Ref<Book> book = make_book();
    const RID entity = minter()->entity_create();
    Decl decl;
    netw_test::CallLog log;
    book->set_went_dirty(log.callable("dirtied"));

    const int role = book->write_param(
        entity,
        decl,
        netw::display::PARAM_ROLE,
        netw::display::ROLE_REMOTE
    );

    const Decl *stored = book->decl_ptr(entity);
    REQUIRE(stored != nullptr);
    NETW_CHECK_EQ(role, int(netw::display::DIRT_ROLE));
    NETW_CHECK_EQ(log.count("dirtied"), 1);
    CHECK(!book->is_dirty(entity));
    NETW_CHECK_EQ(int(stored->display_role), int(netw::display::ROLE_REMOTE));

    Decl ignored;
    const int runtime = book->write_param(
        entity,
        ignored,
        netw::display::PARAM_VISUAL_ROOT,
        NodePath("Visual")
    );

    stored = book->decl_ptr(entity);
    REQUIRE(stored != nullptr);
    NETW_CHECK_EQ(runtime, int(netw::display::DIRT_RUNTIME));
    CHECK(book->is_dirty(entity));
    NETW_CHECK_EQ(int(stored->display_role), int(netw::display::ROLE_REMOTE));
    CHECK(stored->visual_root == NodePath("Visual"));

    const int quiet = book->write_param(
        entity,
        decl,
        netw::display::PARAM_TRACE_INTERVAL,
        4
    );
    NETW_CHECK_EQ(quiet, int(netw::display::DIRT_NONE));
    NETW_CHECK_EQ(log.count("dirtied"), 2);
}

} // namespace TestDisplayBook
