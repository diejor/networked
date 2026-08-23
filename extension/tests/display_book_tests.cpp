#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "netw/api/display_book.hpp"
#include "netw/display_runtime.hpp"
#include "netw/api/liveness_core.hpp"

namespace TestNetwDisplayBook {

using namespace godot;
using netw::NetwDisplayBook;
using netw::NetwDisplayDecl;

Ref<NetwDisplayBook> make_book() {
    Ref<NetwDisplayBook> book;
    book.instantiate();
    return book;
}

Ref<netw::NetwDisplayRuntime> make_runtime() {
    Ref<netw::NetwDisplayRuntime> runtime;
    runtime.instantiate();
    return runtime;
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
    Ref<NetwDisplayBook> book = make_book();
    const RID entity = minter()->entity_create();
    Ref<netw::NetwDisplayRuntime> runtime = make_runtime();

    book->enroll(entity, 7);
    book->set_runtime(entity, runtime);

    const bool by_entity = book->runtime_of(entity) == runtime;
    CHECK(by_entity);
    const bool by_route = book->runtime_at(7) == runtime;
    CHECK(by_route);
    CHECK(book->entity_at(7) == entity);
    NETW_CHECK_EQ(book->route_of(entity), int64_t(7));
    NETW_CHECK_EQ(book->size(), 1);
}

TEST_CASE(
    "[Networked][Display][Hosted] B2 a dropped route takes its whole row, "
    "declaration included, without asking anything outside the book"
) {
    Ref<NetwDisplayBook> book = make_book();
    const RID entity = minter()->entity_create();
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();

    book->enroll(entity, 7);
    book->set_runtime(entity, make_runtime());
    book->set_decl(entity, decl);

    book->drop_route(7);

    CHECK(book->decl_of(entity).is_null());
    CHECK(book->runtime_of(entity).is_null());
    CHECK(book->runtime_at(7).is_null());
    CHECK(!book->entity_at(7).is_valid());
    NETW_CHECK_EQ(book->size(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] B3 the pump reads runtimes in enrolment "
    "order, and a row holding none is not one"
) {
    Ref<NetwDisplayBook> book = make_book();
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3] = {
        core->entity_create(),
        core->entity_create(),
        core->entity_create()
    };
    Ref<netw::NetwDisplayRuntime> first = make_runtime();
    Ref<netw::NetwDisplayRuntime> second = make_runtime();

    book->enroll(ids[0], 1);
    book->enroll(ids[1], 2);
    book->enroll(ids[2], 3);
    book->set_runtime(ids[0], first);
    book->set_runtime(ids[2], second);

    const TypedArray<netw::NetwDisplayRuntime> pumped = book->runtimes();

    NETW_CHECK_EQ(pumped.size(), 2);
    const bool first_pumped = Ref<netw::NetwDisplayRuntime>(pumped[0]) == first;
    CHECK(first_pumped);
    const bool second_pumped = Ref<netw::NetwDisplayRuntime>(pumped[1]) == second;
    CHECK(second_pumped);
}

TEST_CASE(
    "[Networked][Display][Hosted] B4 re-enrolling an entity releases the "
    "route it held, so a stale route answers nothing"
) {
    Ref<NetwDisplayBook> book = make_book();
    const RID entity = minter()->entity_create();
    Ref<netw::NetwDisplayRuntime> runtime = make_runtime();

    book->enroll(entity, 7);
    book->set_runtime(entity, runtime);
    book->enroll(entity, 9);

    const bool moved = book->runtime_at(9) == runtime;
    CHECK(moved);
    CHECK(book->runtime_at(7).is_null());
    CHECK(!book->entity_at(7).is_valid());
    NETW_CHECK_EQ(book->size(), 1);
}

TEST_CASE(
    "[Networked][Display][Hosted] B5 a declaration outlives no route because "
    "it never needed one, so it can land before the entity goes live"
) {
    Ref<NetwDisplayBook> book = make_book();
    const RID entity = minter()->entity_create();
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();

    book->set_decl(entity, decl);

    CHECK(book->decl_of(entity) == decl);
    NETW_CHECK_EQ(book->route_of(entity), int64_t(0));
    NETW_CHECK_EQ(book->runtimes().size(), 0);
    NETW_CHECK_EQ(book->size(), 1);

    book->drop(entity);
    CHECK(book->decl_of(entity).is_null());
    NETW_CHECK_EQ(book->size(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] B6 a mark reports once per entity, and the "
    "take answers every entity still marked"
) {
    Ref<NetwDisplayBook> book = make_book();
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID one = core->entity_create();
    const RID two = core->entity_create();
    Ref<netw::NetwDisplayRuntime> runtime = make_runtime();
    netw_test::CallLog log;
    book->connect("went_dirty", log.callable("dirtied"));

    book->enroll(one, 7);
    book->set_runtime(one, runtime);

    book->mark_dirty(one, NetwDisplayDecl::DIRT_RUNTIME);
    book->mark_dirty(one, NetwDisplayDecl::DIRT_RUNTIME);
    book->mark_dirty(two, NetwDisplayDecl::DIRT_RUNTIME);

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

    book->mark_dirty(one, NetwDisplayDecl::DIRT_RUNTIME);
    NETW_CHECK_EQ(log.count("dirtied"), 3);
}

TEST_CASE(
    "[Networked][Display][Hosted] B7 a retiring row takes its pending rebuild "
    "with it, because a gone entity has nothing left to repair"
) {
    Ref<NetwDisplayBook> book = make_book();
    const RID entity = minter()->entity_create();

    book->enroll(entity, 7);
    book->set_runtime(entity, make_runtime());
    book->mark_dirty(entity, NetwDisplayDecl::DIRT_RUNTIME);

    book->drop_route(7);

    CHECK(!book->is_dirty(entity));
    NETW_CHECK_EQ(book->take_dirty().size(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] B8 an entity the book has no row for is "
    "still marked, and a listener that repaired it withdraws the mark"
) {
    Ref<NetwDisplayBook> book = make_book();
    const RID entity = minter()->entity_create();
    netw_test::CallLog log;
    book->connect("went_dirty", log.callable("dirtied"));

    book->mark_dirty(entity, NetwDisplayDecl::DIRT_RUNTIME);
    book->mark_dirty(RID(), NetwDisplayDecl::DIRT_RUNTIME);

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
    Ref<NetwDisplayBook> book = make_book();
    const RID entity = minter()->entity_create();
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();
    netw_test::CallLog log;
    book->connect("went_dirty", log.callable("dirtied"));

    const int role = book->write_param(
        entity,
        decl,
        NetwDisplayDecl::PARAM_ROLE,
        NetwDisplayDecl::ROLE_REMOTE
    );

    CHECK(book->decl_of(entity) == decl);
    NETW_CHECK_EQ(role, int(NetwDisplayDecl::DIRT_ROLE));
    NETW_CHECK_EQ(log.count("dirtied"), 1);
    CHECK(!book->is_dirty(entity));

    Ref<NetwDisplayDecl> ignored;
    ignored.instantiate();
    const int runtime = book->write_param(
        entity,
        ignored,
        NetwDisplayDecl::PARAM_VISUAL_ROOT,
        NodePath("Visual")
    );

    CHECK(book->decl_of(entity) == decl);
    NETW_CHECK_EQ(runtime, int(NetwDisplayDecl::DIRT_RUNTIME));
    CHECK(book->is_dirty(entity));
    NETW_CHECK_EQ(int(decl->get_display_role()), int(NetwDisplayDecl::ROLE_REMOTE));
    CHECK(decl->get_visual_root() == NodePath("Visual"));

    const int quiet = book->write_param(
        entity,
        decl,
        NetwDisplayDecl::PARAM_TRACE_INTERVAL,
        4
    );
    NETW_CHECK_EQ(quiet, int(NetwDisplayDecl::DIRT_NONE));
    NETW_CHECK_EQ(log.count("dirtied"), 2);
}

} // namespace TestNetwDisplayBook
