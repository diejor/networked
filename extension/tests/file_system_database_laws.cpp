#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/file_system.hpp"
#include "netw/api/database.hpp"
#include "netw/api/file_system_database.hpp"
#include "netw/api/record.hpp"
#include "netw/api/tests.hpp"
#include "netw/api/transaction.hpp"

namespace TestFileSystemDatabaseLaws {

using namespace godot;
using netw::FileSystemDatabase;
using netw::NetwDatabase;
using netw::NetwNativeTests;
using netw::NetwPromise;
using netw::NetwTransaction;

int &run_serial() {
    static int held = 0;
    return held;
}

struct SaveDir {
    String path;

    explicit SaveDir(const char *p_stem) {
        run_serial() += 1;
        path = String("user://netw_native_saves/") + String(p_stem)
            + String::num_int64(run_serial());
        DirAccess::make_dir_recursive_absolute(path);
    }

    Ref<FileSystemDatabase> backend() const {
        Ref<FileSystemDatabase> made;
        made.instantiate();
        made->set_base_dir(path);
        return made;
    }

    ~SaveDir() {
        NetwNativeTests::file_system_database_forget_roots();
    }
};

int settled_code(const Ref<NetwPromise> &p_answer) {
    REQUIRE(p_answer.is_valid());
    REQUIRE(p_answer->get_is_settled());
    if (p_answer->get_is_failed()) {
        return p_answer->get_code();
    }
    return int(int64_t(p_answer->get_result()));
}

Dictionary settled_record(const Ref<NetwPromise> &p_answer) {
    REQUIRE(p_answer.is_valid());
    REQUIRE(p_answer->get_is_settled());
    if (p_answer->get_is_failed()) {
        return Dictionary();
    }
    return p_answer->get_result();
}

Dictionary schema_of(const char *p_table, const char *p_column) {
    Array columns;
    columns.push_back(StringName(p_column));
    Dictionary schema;
    schema[StringName(p_table)] = columns;
    return schema;
}

void queue_one(
    const Ref<NetwTransaction> &p_transaction,
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_values
) {
    p_transaction->queue_upsert(p_table, p_id, p_values);
}

Ref<NetwPromise> write_one(
    const Ref<NetwDatabase> &p_db,
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_values
) {
    return p_db->transaction(
        callable_mp_static(&queue_one).bind(p_table, p_id, p_values)
    );
}

Array one_column(const char *p_column) {
    Array held;
    held.push_back(StringName(p_column));
    return held;
}

TEST_CASE(
    "[Networked][Database] FS1 initialization creates a directory per "
    "declared table under the slot root, and answers OK again for a "
    "root that already holds one the schema no longer names"
) {
    const SaveDir dir("init");
    const Ref<FileSystemDatabase> backend = dir.backend();

    NETW_CHECK_EQ(
        settled_code(backend->initialize(schema_of("rocks", "health"))),
        int(OK)
    );
    CHECK(DirAccess::dir_exists_absolute(dir.path.path_join("rocks")));

    DirAccess::make_dir_recursive_absolute(dir.path.path_join("ghosts"));
    NETW_CHECK_EQ(
        settled_code(backend->initialize(schema_of("rocks", "health"))),
        int(OK)
    );
}

TEST_CASE(
    "[Networked][Database] FS2 a record round trips through a file, "
    "merges rather than replacing on a second write, and answers "
    "nothing once erased"
) {
    const SaveDir dir("crud");
    const Ref<FileSystemDatabase> backend = dir.backend();
    settled_code(backend->initialize(schema_of("rocks", "health")));

    Dictionary first;
    first[StringName("health")] = 100;
    first[StringName("kind")] = StringName("granite");
    backend->upsert(StringName("rocks"), StringName("r1"), first);

    const Dictionary held = settled_record(
        backend->find_by_id(StringName("rocks"), StringName("r1"))
    );
    NETW_CHECK_EQ(int(held[StringName("health")]), 100);

    Dictionary partial;
    partial[StringName("health")] = 80;
    backend->upsert(StringName("rocks"), StringName("r1"), partial);
    const Dictionary merged = settled_record(
        backend->find_by_id(StringName("rocks"), StringName("r1"))
    );
    NETW_CHECK_EQ(int(merged[StringName("health")]), 80);
    CHECK(StringName(merged[StringName("kind")]) == StringName("granite"));

    NETW_CHECK_EQ(
        settled_code(backend->erase(StringName("rocks"), StringName("r1"))),
        int(OK)
    );
    CHECK(settled_record(
              backend->find_by_id(StringName("rocks"), StringName("r1"))
    )
              .is_empty());
    NETW_CHECK_EQ(
        settled_code(backend->erase(StringName("rocks"), StringName("gone"))),
        int(OK)
    );
}

TEST_CASE(
    "[Networked][Database] FS3 a filtered read answers only the rows "
    "that match, an empty filter answers the whole table, and a table "
    "with no directory answers nothing rather than failing"
) {
    const SaveDir dir("query");
    const Ref<FileSystemDatabase> backend = dir.backend();
    settled_code(backend->initialize(schema_of("rocks", "health")));

    Dictionary granite;
    granite[StringName("health")] = 80;
    granite[StringName("kind")] = StringName("granite");
    Dictionary marble;
    marble[StringName("health")] = 50;
    marble[StringName("kind")] = StringName("marble");
    backend->upsert(StringName("rocks"), StringName("r1"), granite);
    backend->upsert(StringName("rocks"), StringName("r2"), marble);

    const Ref<NetwPromise> all
        = backend->find_all(StringName("rocks"), Dictionary());
    REQUIRE(all->get_is_settled());
    NETW_CHECK_EQ(int(Array(all->get_result()).size()), 2);

    Dictionary only_granite;
    only_granite[StringName("kind")] = StringName("granite");
    const Ref<NetwPromise> filtered
        = backend->find_all(StringName("rocks"), only_granite);
    const Array matched = filtered->get_result();
    NETW_CHECK_EQ(int(matched.size()), 1);
    NETW_CHECK_EQ(int(Dictionary(matched[0])[StringName("health")]), 80);

    const Ref<NetwPromise> absent
        = backend->find_all(StringName("nothing"), Dictionary());
    CHECK(Array(absent->get_result()).is_empty());
}

TEST_CASE(
    "[Networked][Database] FS4 the text format writes the readable "
    "extension and reads back the same record the binary one would"
) {
    const SaveDir dir("text");
    const Ref<FileSystemDatabase> backend = dir.backend();
    backend->set_use_text_format(true);
    settled_code(backend->initialize(schema_of("rocks", "health")));

    Dictionary values;
    values[StringName("health")] = 7;
    backend->upsert(StringName("rocks"), StringName("r1"), values);

    CHECK(
        netw::gd::resource_exists(
            dir.path.path_join("rocks").path_join("r1.tres")
        )
    );
    CHECK_FALSE(
        netw::gd::resource_exists(
            dir.path.path_join("rocks").path_join("r1.res")
        )
    );
    const Dictionary held = settled_record(
        backend->find_by_id(StringName("rocks"), StringName("r1"))
    );
    NETW_CHECK_EQ(int(held[StringName("health")]), 7);
}

TEST_CASE(
    "[Networked][Database] FS5 a database writes and reads under the "
    "slot it opened, and a second slot over the same directory sees "
    "none of it"
) {
    const SaveDir dir("slots");

    Ref<NetwDatabase> first;
    first.instantiate();
    first->set_backend(dir.backend());
    first->set_warm_policy(Ref<netw::WarmPolicy>());
    first->open_slot(StringName("slot_a"));
    first->declare_table(StringName("players"), one_column("hp"));
    Dictionary values;
    values[StringName("hp")] = 42;
    NETW_CHECK_EQ(
        settled_code(
            write_one(first, StringName("players"), StringName("p1"), values)
        ),
        int(OK)
    );
    NETW_CHECK_EQ(
        int(settled_record(
            first->find(StringName("players"), StringName("p1"))
        )[StringName("hp")]),
        42
    );
    first = Ref<NetwDatabase>();
    NetwNativeTests::file_system_database_forget_roots();

    Ref<NetwDatabase> second;
    second.instantiate();
    second->set_backend(dir.backend());
    second->set_warm_policy(Ref<netw::WarmPolicy>());
    second->open_slot(StringName("slot_b"));
    second->declare_table(StringName("players"), one_column("hp"));

    CHECK(settled_record(second->find(StringName("players"), StringName("p1")))
              .is_empty());
}

TEST_CASE(
    "[Networked][Database] FS6 the slot choice is startup-only: it "
    "stands after the first read locks the backend, because "
    "re-pointing a live database at another save cannot be undone"
) {
    const SaveDir dir("lock");
    Ref<NetwDatabase> db;
    db.instantiate();
    db->set_backend(dir.backend());
    db->set_warm_policy(Ref<netw::WarmPolicy>());

    CHECK(db->current_slot() == StringName("default"));
    db->open_slot(StringName("slot_a"));
    CHECK(db->current_slot() == StringName("slot_a"));

    db->declare_table(StringName("players"), one_column("hp"));
    CHECK(db->current_slot() == StringName("slot_a"));

    db->find(StringName("players"), StringName("nobody"));
    db->open_slot(StringName("slot_b"));
    CHECK(db->current_slot() == StringName("slot_a"));
}

TEST_CASE(
    "[Networked][Database] FS7 the slots a backend holds are readable "
    "and removable before any slot is open, which is what a save-select "
    "menu asks of it"
) {
    const SaveDir dir("menu");

    const Ref<FileSystemDatabase> seed_a = dir.backend();
    settled_code(seed_a->initialize(schema_of("players", "hp"), "slot_a"));
    Dictionary one;
    one[StringName("hp")] = 1;
    seed_a->upsert(StringName("players"), StringName("p1"), one);
    NetwNativeTests::file_system_database_forget_roots();

    const Ref<FileSystemDatabase> seed_b = dir.backend();
    settled_code(seed_b->initialize(schema_of("players", "hp"), "slot_b"));
    Dictionary two;
    two[StringName("hp")] = 2;
    seed_b->upsert(StringName("players"), StringName("p2"), two);
    NetwNativeTests::file_system_database_forget_roots();

    Ref<NetwDatabase> browser;
    browser.instantiate();
    browser->set_backend(dir.backend());

    const Ref<NetwPromise> listed = browser->list_slots();
    REQUIRE(listed->get_is_settled());
    const Array slots = listed->get_result();
    CHECK(slots.has(StringName("slot_a")));
    CHECK(slots.has(StringName("slot_b")));

    NETW_CHECK_EQ(
        settled_code(browser->delete_slot(StringName("slot_a"))),
        int(OK)
    );
    const Array left = browser->list_slots()->get_result();
    CHECK_FALSE(left.has(StringName("slot_a")));
    CHECK(left.has(StringName("slot_b")));

    NETW_CHECK_EQ(
        settled_code(browser->delete_slot(StringName())),
        int(ERR_INVALID_PARAMETER)
    );
}

TEST_CASE(
    "[Networked][Database] FS8 a row written through one database is "
    "read back by a cold one over the same directory, which is the "
    "whole point of a save file"
) {
    const SaveDir dir("cold");

    Ref<NetwDatabase> writer;
    writer.instantiate();
    writer->set_backend(dir.backend());
    writer->set_warm_policy(Ref<netw::WarmPolicy>());
    writer->declare_table(StringName("players"), one_column("health"));
    Dictionary values;
    values[StringName("health")] = 80;
    NETW_CHECK_EQ(
        settled_code(
            write_one(writer, StringName("players"), StringName("jose"), values)
        ),
        int(OK)
    );
    writer = Ref<NetwDatabase>();
    NetwNativeTests::file_system_database_forget_roots();

    Ref<NetwDatabase> reader;
    reader.instantiate();
    reader->set_backend(dir.backend());
    reader->set_warm_policy(Ref<netw::WarmPolicy>());
    reader->declare_table(StringName("players"), one_column("health"));

    const Dictionary cold = settled_record(
        reader->find(StringName("players"), StringName("jose"))
    );
    NETW_CHECK_EQ(int(cold[StringName("health")]), 80);
}

} // namespace TestFileSystemDatabaseLaws

#endif
