#include "support/netw_test.h"

#include "support/persistence_stand.h"

#include <memory>

#include "godot/rid.hpp"
#include "netw/liveness_core.hpp"
#include "netw/persist/book.hpp"
#include "netw/persist/loop.hpp"

namespace TestNetwPersistenceCore {

using namespace godot;
using netw::persist::Book;
using netw_test::DatabaseStand;
using netw_test::NetwTestPersistenceEngine;

Book fresh_book() {
    return Book();
}

Ref<netw::NetwLivenessCore> minter() {
    Ref<netw::NetwLivenessCore> core;
    core.instantiate();
    return core;
}

Ref<RefCounted> ground() {
    Ref<RefCounted> node;
    node.instantiate();
    return node;
}

std::shared_ptr<DatabaseStand> fresh_database() {
    return std::make_shared<DatabaseStand>();
}

Dictionary due_row(
    const std::shared_ptr<DatabaseStand> &p_database,
    const String &p_id,
    int p_score
) {
    Dictionary values;
    values["score"] = p_score;
    Dictionary due;
    due["db"] = p_database->db;
    due["table"] = StringName("players");
    due["id"] = StringName(p_id);
    due["values"] = values;
    return due;
}

Ref<NetwTestPersistenceEngine> engine_on(
    Object *p_owner,
    const Dictionary &p_due
) {
    Ref<NetwTestPersistenceEngine> engine;
    engine.instantiate();
    engine->set_owner(p_owner);
    engine->set_due(p_due);
    return engine;
}

int score_of(const Array &p_committed, int p_at) {
    const Dictionary values = p_committed[p_at];
    const int score = values.get("score", -1);
    return score;
}

TEST_CASE(
    "[Networked][Database][Hosted] the due rows of one database batch "
    "into a single transaction, and a second database gets its own"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[3]
        = {core->entity_create(), core->entity_create(), core->entity_create()};
    const Ref<RefCounted> owner = ground();
    const std::shared_ptr<DatabaseStand> shared = fresh_database();
    const std::shared_ptr<DatabaseStand> lone = fresh_database();
    Book book = fresh_book();
    book.enroll(ids[0], engine_on(owner.ptr(), due_row(shared, "a", 1)));
    book.enroll(ids[1], engine_on(owner.ptr(), due_row(lone, "b", 2)));
    book.enroll(ids[2], engine_on(owner.ptr(), due_row(shared, "c", 3)));

    netw::persist::snapshot_tick(book, 0.25, true);

    NETW_CHECK_EQ(shared->transaction_count(), 1);
    NETW_CHECK_EQ(int(shared->upserts().size()), 2);
    NETW_CHECK_EQ(lone->transaction_count(), 1);
    NETW_CHECK_EQ(int(lone->upserts().size()), 1);
}

TEST_CASE(
    "[Networked][Database][Hosted] an engine whose owner left the tree "
    "is dropped from the book rather than advanced"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[2] = {core->entity_create(), core->entity_create()};
    const Ref<RefCounted> owner = ground();
    const std::shared_ptr<DatabaseStand> database = fresh_database();
    const Ref<NetwTestPersistenceEngine> dead
        = engine_on(nullptr, due_row(database, "a", 1));
    const Ref<NetwTestPersistenceEngine> live
        = engine_on(owner.ptr(), due_row(database, "b", 2));
    Book book = fresh_book();
    book.enroll(ids[0], dead);
    book.enroll(ids[1], live);

    netw::persist::snapshot_tick(book, 0.5, true);

    NETW_CHECK_EQ(dead->tick_count(), 0);
    NETW_CHECK_EQ(book.size(), 1);
    CHECK_FALSE(book.has(ids[0]));
    NETW_CHECK_EQ(live->tick_count(), 1);
    NETW_CHECK_EQ(int(database->upserts().size()), 1);
}

TEST_CASE(
    "[Networked][Database][Hosted] an engine with nothing due writes "
    "nothing and keeps its enrolment"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[2] = {core->entity_create(), core->entity_create()};
    const Ref<RefCounted> owner = ground();
    const std::shared_ptr<DatabaseStand> database = fresh_database();
    const Ref<NetwTestPersistenceEngine> quiet
        = engine_on(owner.ptr(), Dictionary());
    const Ref<NetwTestPersistenceEngine> due
        = engine_on(owner.ptr(), due_row(database, "b", 7));
    Book book = fresh_book();
    book.enroll(ids[0], quiet);
    book.enroll(ids[1], due);

    netw::persist::snapshot_tick(book, 0.5, true);

    NETW_CHECK_EQ(quiet->tick_count(), 1);
    NETW_CHECK_CLOSE(quiet->advanced_by(), 0.5, 1e-9);
    NETW_CHECK_EQ(int(quiet->committed().size()), 0);
    NETW_CHECK_EQ(book.size(), 2);
    NETW_CHECK_EQ(database->transaction_count(), 1);
    NETW_CHECK_EQ(int(database->upserts().size()), 1);
    NETW_CHECK_EQ(int(due->committed().size()), 1);
    NETW_CHECK_EQ(score_of(due->committed(), 0), 7);
}

TEST_CASE(
    "[Networked][Database][Hosted] a client advances no accumulator and "
    "opens no transaction"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID entity = core->entity_create();
    const Ref<RefCounted> owner = ground();
    const std::shared_ptr<DatabaseStand> database = fresh_database();
    const Ref<NetwTestPersistenceEngine> engine
        = engine_on(owner.ptr(), due_row(database, "a", 1));
    Book book = fresh_book();
    book.enroll(entity, engine);

    netw::persist::snapshot_tick(book, 1.0, false);

    NETW_CHECK_EQ(engine->tick_count(), 0);
    NETW_CHECK_EQ(database->transaction_count(), 0);
    NETW_CHECK_EQ(book.size(), 1);
}

TEST_CASE(
    "[Networked][Database][Hosted] a committed transaction hands every "
    "row of its batch back to the engine that raised it"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[2] = {core->entity_create(), core->entity_create()};
    const Ref<RefCounted> owner = ground();
    const std::shared_ptr<DatabaseStand> database = fresh_database();
    const Ref<NetwTestPersistenceEngine> first
        = engine_on(owner.ptr(), due_row(database, "a", 11));
    const Ref<NetwTestPersistenceEngine> second
        = engine_on(owner.ptr(), due_row(database, "b", 22));
    Book book = fresh_book();
    book.enroll(ids[0], first);
    book.enroll(ids[1], second);

    netw::persist::snapshot_tick(book, 0.1, true);

    NETW_CHECK_EQ(int(first->committed().size()), 1);
    NETW_CHECK_EQ(score_of(first->committed(), 0), 11);
    NETW_CHECK_EQ(int(second->committed().size()), 1);
    NETW_CHECK_EQ(score_of(second->committed(), 0), 22);
}

TEST_CASE(
    "[Networked][Database][Hosted] a transaction that did not commit "
    "adopts nothing as the last flushed state"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID entity = core->entity_create();
    const Ref<RefCounted> owner = ground();
    const std::shared_ptr<DatabaseStand> database = fresh_database();
    database->set_outcome(int(FAILED));
    const Ref<NetwTestPersistenceEngine> engine
        = engine_on(owner.ptr(), due_row(database, "a", 5));
    Book book = fresh_book();
    book.enroll(entity, engine);

    netw::persist::snapshot_tick(book, 0.1, true);

    NETW_CHECK_EQ(int(database->upserts().size()), 1);
    NETW_CHECK_EQ(int(engine->committed().size()), 0);
}

TEST_CASE(
    "[Networked][Database][Hosted] flush_all skips an engine with no "
    "owner node and flushes the rest"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID ids[2] = {core->entity_create(), core->entity_create()};
    const Ref<RefCounted> owner = ground();
    const Ref<NetwTestPersistenceEngine> dead
        = engine_on(nullptr, Dictionary());
    const Ref<NetwTestPersistenceEngine> live
        = engine_on(owner.ptr(), Dictionary());
    Book book = fresh_book();
    book.enroll(ids[0], dead);
    book.enroll(ids[1], live);

    netw::persist::flush_all(book, true);

    NETW_CHECK_EQ(dead->flush_count(), 0);
    NETW_CHECK_EQ(live->flush_count(), 1);
    NETW_CHECK_EQ(book.size(), 2);
}

TEST_CASE(
    "[Networked][Database][Hosted] flush_all on a client flushes "
    "nothing"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID entity = core->entity_create();
    const Ref<RefCounted> owner = ground();
    const Ref<NetwTestPersistenceEngine> engine
        = engine_on(owner.ptr(), Dictionary());
    Book book = fresh_book();
    book.enroll(entity, engine);

    netw::persist::flush_all(book, false);

    NETW_CHECK_EQ(engine->flush_count(), 0);
}

TEST_CASE(
    "[Networked][Database][Hosted] a leaving owner takes its last "
    "snapshot on the server and its enrolment either way"
) {
    const Ref<netw::NetwLivenessCore> core = minter();
    const RID served = core->entity_create();
    const RID observed = core->entity_create();
    const Ref<RefCounted> owner = ground();
    const Ref<NetwTestPersistenceEngine> on_server
        = engine_on(owner.ptr(), Dictionary());
    const Ref<NetwTestPersistenceEngine> on_client
        = engine_on(owner.ptr(), Dictionary());
    Book book = fresh_book();
    book.enroll(served, on_server);
    book.enroll(observed, on_client);

    netw::persist::owner_exiting(book, served, true);
    netw::persist::owner_exiting(book, observed, false);

    NETW_CHECK_EQ(on_server->flush_count(), 1);
    NETW_CHECK_EQ(on_client->flush_count(), 0);
    NETW_CHECK_EQ(book.size(), 0);
}

} // namespace TestNetwPersistenceCore
