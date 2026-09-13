#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "netw/api/database.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/record.hpp"
#include "netw/api/record_table.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/transaction.hpp"
#include "netw/api/warm_policy.hpp"

namespace TestDatabaseLaws {

using namespace godot;
using netw::NetwDatabase;
using netw::NetwDatabaseBackend;
using netw::NetwPromise;
using netw::NetwRecord;
using netw::NetwRecordTable;
using netw::NetwSchema;
using netw::NetwTransaction;
using netw::WarmRequest;

class CountingBackend : public NetwDatabaseBackend {
    Dictionary rows;
    Array warm_batches;
    Array last_batch;
    int commit_calls = 0;
    int erase_calls = 0;
    int commit_outcome = 0;

public:
    Array committed_operations() const {
        return last_batch;
    }

    void refuse_commits(int p_code) {
        commit_outcome = p_code;
    }

    int commit_count() const {
        return commit_calls;
    }

    int erase_count() const {
        return erase_calls;
    }

    Array warmed() const {
        return warm_batches;
    }

    void seed(const StringName &p_id, const Dictionary &p_record) {
        rows[p_id] = p_record;
    }

    bool holds(const StringName &p_id) const {
        return rows.has(p_id);
    }

    Ref<NetwPromise> commit(const Array &p_operations) override {
        commit_calls += 1;
        last_batch = p_operations.duplicate(true);
        if (commit_outcome != int(OK)) {
            return NetwPromise::resolved(int64_t(commit_outcome));
        }
        for (int at = 0; at < p_operations.size(); at++) {
            const Dictionary row = p_operations[at];
            const StringName id = row.get("id", StringName());
            Dictionary held = rows.get(id, Dictionary());
            const Dictionary values = row.get("data", Dictionary());
            const Array keys = values.keys();
            for (int key = 0; key < keys.size(); key++) {
                held[keys[key]] = values[keys[key]];
            }
            rows[id] = held;
        }
        return NetwPromise::resolved(int64_t(OK));
    }

    Ref<NetwPromise> find_by_id(
        const StringName &,
        const StringName &p_id
    ) override {
        if (!rows.has(p_id)) {
            return NetwPromise::resolved(Dictionary());
        }
        return NetwPromise::resolved(Dictionary(rows[p_id]).duplicate());
    }

    Ref<NetwPromise> find_all(const StringName &, const Dictionary &) override {
        TypedArray<Dictionary> all;
        const Array ids = rows.keys();
        for (int at = 0; at < ids.size(); at++) {
            all.push_back(Dictionary(rows[ids[at]]).duplicate());
        }
        return NetwPromise::resolved(all);
    }

    Ref<NetwPromise> erase(
        const StringName &,
        const StringName &p_id
    ) override {
        erase_calls += 1;
        rows.erase(p_id);
        return NetwPromise::resolved(int64_t(OK));
    }

    Ref<NetwPromise> warm(const Array &p_directives) override {
        warm_batches.push_back(p_directives);
        return NetwPromise::resolved(int64_t(OK));
    }
};

struct Stand {
    Ref<NetwDatabase> db;
    Ref<CountingBackend> backend;

    Stand() {
        db.instantiate();
        backend = Ref<CountingBackend>(memnew(CountingBackend));
        db->set_backend(backend);
    }
};

class DeferringBackend : public NetwDatabaseBackend {
public:
    Ref<NetwPromise> upsert(
        const StringName &p_table,
        const StringName &p_id,
        const Dictionary &p_data
    ) override {
        (void)p_table;
        (void)p_id;
        (void)p_data;
        Ref<NetwPromise> in_flight;
        in_flight.instantiate();
        return in_flight;
    }
};

class PlayersOnlyPolicy : public netw::WarmPolicy {
public:
    Ref<WarmRequest> plan_table(
        const StringName &p_table,
        const TypedArray<StringName> &p_columns
    ) override {
        (void)p_columns;
        return p_table == StringName("players") ? WarmRequest::all()
                                                : WarmRequest::none();
    }
};

class NarrowedPolicy : public netw::WarmPolicy {
public:
    Ref<WarmRequest> plan_table(
        const StringName &p_table,
        const TypedArray<StringName> &p_columns
    ) override {
        (void)p_table;
        (void)p_columns;
        Array only;
        only.push_back(StringName("valeria"));
        return WarmRequest::ids(only);
    }
};

Array &announcements() {
    static Array held;
    return held;
}

void note_schema(const StringName &p_table, const Array &p_columns) {
    Dictionary row;
    row["table"] = p_table;
    row["columns"] = p_columns;
    announcements().push_back(row);
}

void note_loaded(const StringName &, const StringName &, bool p_hit) {
    announcements().push_back(p_hit);
}

void note_mismatch(
    const StringName &,
    const StringName &,
    const Array &p_missing,
    const Array &p_unknown
) {
    Dictionary row;
    row["missing"] = p_missing;
    row["unknown"] = p_unknown;
    announcements().push_back(row);
}

void note_committed(int p_tables, int p_records) {
    Dictionary row;
    row["tables"] = p_tables;
    row["records"] = p_records;
    announcements().push_back(row);
}

void queue_one(
    const Ref<NetwTransaction> &p_transaction,
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_values
) {
    p_transaction->queue_upsert(p_table, p_id, p_values);
}

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

Array names(const char *p_first, const char *p_second = nullptr) {
    Array held;
    held.push_back(StringName(p_first));
    if (p_second != nullptr) {
        held.push_back(StringName(p_second));
    }
    return held;
}

Ref<NetwSchema> typed_rocks() {
    const Ref<NetwSchema> declaration = NetwSchema::create(StringName("rocks"));
    declaration->replicated(false);
    declaration->column(
        StringName("health"),
        netw::NetwMultiplayer::COLUMN_F64,
        1,
        Ref<netw::NetwQuantize>()
    );
    declaration->column(
        StringName("origin"),
        netw::NetwMultiplayer::COLUMN_VECTOR2,
        1,
        Ref<netw::NetwQuantize>()
    );
    return declaration;
}

TEST_CASE(
    "[Networked][Database][Hosted] DB1 declaring a table twice merges "
    "the columns, and every announcement carries the whole set rather "
    "than the columns that declaration added"
) {
    Stand stand;
    announcements().clear();
    stand.db->connect(
        StringName("schema_registered"),
        callable_mp_static(&note_schema)
    );

    stand.db->declare_table(StringName("rocks"), names("health"));
    stand.db->declare_table(StringName("rocks"), names("position"));

    const TypedArray<StringName> declared
        = stand.db->get_registered_columns(StringName("rocks"));
    NETW_CHECK_EQ(int(declared.size()), 2);
    CHECK(declared.has(StringName("health")));
    CHECK(declared.has(StringName("position")));
    NETW_CHECK_EQ(int(announcements().size()), 2);
    const Dictionary last = announcements()[1];
    NETW_CHECK_EQ(int(Array(last["columns"]).size()), 2);
    announcements().clear();
}

TEST_CASE(
    "[Networked][Database][Hosted] DB2 a transaction commits every "
    "queued row as one batch and announces the tables and records it "
    "touched, once"
) {
    Stand stand;
    stand.db->declare_table(StringName("rocks"), names("health"));
    announcements().clear();
    stand.db->connect(
        StringName("transaction_committed"),
        callable_mp_static(&note_committed)
    );

    Dictionary first;
    first[StringName("health")] = 50;
    Dictionary second;
    second[StringName("health")] = 20;
    const Ref<NetwPromise> written = stand.db->transaction(
        callable_mp_static(&queue_one)
            .bind(StringName("rocks"), StringName("r1"), first)
    );
    const Ref<NetwPromise> again
        = write_one(stand.db, StringName("rocks"), StringName("r2"), second);

    NETW_CHECK_EQ(settled_code(written), int(OK));
    NETW_CHECK_EQ(settled_code(again), int(OK));
    NETW_CHECK_EQ(stand.backend->commit_count(), 2);
    NETW_CHECK_EQ(int(announcements().size()), 2);
    const Dictionary announced = announcements()[0];
    NETW_CHECK_EQ(int(announced["tables"]), 1);
    NETW_CHECK_EQ(int(announced["records"]), 1);
    announcements().clear();
}

TEST_CASE(
    "[Networked][Database][Hosted] DB3 a commit the backend refused "
    "answers its code and announces nothing, because a promise that "
    "settled is not a write that landed"
) {
    Stand stand;
    stand.db->declare_table(StringName("rocks"), names("health"));
    stand.backend->refuse_commits(int(ERR_CANT_CREATE));
    announcements().clear();
    stand.db->connect(
        StringName("transaction_committed"),
        callable_mp_static(&note_committed)
    );

    Dictionary values;
    values[StringName("health")] = 10;
    const Ref<NetwPromise> written
        = write_one(stand.db, StringName("rocks"), StringName("r1"), values);

    NETW_CHECK_EQ(settled_code(written), int(ERR_CANT_CREATE));
    CHECK(announcements().is_empty());
    announcements().clear();
}

TEST_CASE(
    "[Networked][Database][Hosted] DB4 a read answers the stored record "
    "and reports whether it hit, and an erase takes the row out"
) {
    Stand stand;
    stand.db->declare_table(StringName("rocks"), names("health"));
    Dictionary values;
    values[StringName("health")] = 99;
    write_one(stand.db, StringName("rocks"), StringName("r1"), values);
    announcements().clear();
    stand.db->connect(
        StringName("record_loaded"),
        callable_mp_static(&note_loaded)
    );

    const Dictionary held
        = settled_record(stand.db->find(StringName("rocks"), StringName("r1")));
    const Dictionary missing = settled_record(
        stand.db->find(StringName("rocks"), StringName("nobody"))
    );

    NETW_CHECK_EQ(int(held[StringName("health")]), 99);
    CHECK(missing.is_empty());
    NETW_CHECK_EQ(int(announcements().size()), 2);
    CHECK(bool(announcements()[0]));
    CHECK_FALSE(bool(announcements()[1]));

    NETW_CHECK_EQ(
        settled_code(stand.db->erase(StringName("rocks"), StringName("r1"))),
        int(OK)
    );
    NETW_CHECK_EQ(stand.backend->erase_count(), 1);
    CHECK(settled_record(stand.db->find(StringName("rocks"), StringName("r1")))
              .is_empty());
    announcements().clear();
}

TEST_CASE(
    "[Networked][Database][Hosted] DB5 a read of a table nothing "
    "declared is refused rather than guessed at, and so is a read with "
    "no backend to reach"
) {
    Stand stand;

    const Ref<NetwPromise> unread
        = stand.db->find(StringName("rocks"), StringName("r1"));
    REQUIRE(unread.is_valid());
    CHECK(unread->get_is_failed());
    NETW_CHECK_EQ(unread->get_code(), int(ERR_UNCONFIGURED));

    Ref<NetwDatabase> bare;
    bare.instantiate();
    bare->declare_table(StringName("rocks"), names("health"));
    const Ref<NetwPromise> unbacked
        = bare->find(StringName("rocks"), StringName("r1"));
    REQUIRE(unbacked.is_valid());
    CHECK(unbacked->get_is_failed());
    NETW_CHECK_EQ(unbacked->get_code(), int(ERR_UNCONFIGURED));
}

TEST_CASE(
    "[Networked][Database][Hosted] DB6 what a table put in is what it "
    "fetches back, and a table with no record script hands back a "
    "DictionaryRecord"
) {
    Stand stand;
    stand.db->declare_table(StringName("players"), names("score"));

    Ref<netw::DictionaryRecord> written;
    written.instantiate();
    written->set_value(StringName("score"), 42);
    const Ref<NetwRecordTable> players = stand.db->table(StringName("players"));
    REQUIRE(players.is_valid());

    NETW_CHECK_EQ(
        settled_code(players->put(StringName("carol"), written)),
        int(OK)
    );

    const Ref<NetwPromise> read = players->fetch(StringName("carol"));
    REQUIRE(read.is_valid());
    REQUIRE(read->get_is_settled());
    const Ref<NetwRecord> loaded = read->get_result();
    REQUIRE(loaded.is_valid());
    NETW_CHECK_EQ(int(loaded->get_value(StringName("score"))), 42);
    CHECK(loaded->get_class() == String("DictionaryRecord"));

    const Ref<NetwPromise> absent = players->fetch(StringName("nobody"));
    REQUIRE(absent.is_valid());
    CHECK(absent->get_result().get_type() == Variant::NIL);
}

TEST_CASE(
    "[Networked][Database][Hosted] DB7 a table answers every row it "
    "holds, and its column list is the database's"
) {
    Stand stand;
    stand.db->declare_table(StringName("players"), names("score"));
    Dictionary one;
    one[StringName("score")] = 1;
    Dictionary two;
    two[StringName("score")] = 2;
    write_one(stand.db, StringName("players"), StringName("a"), one);
    write_one(stand.db, StringName("players"), StringName("b"), two);

    const Ref<NetwRecordTable> players = stand.db->table(StringName("players"));
    const Ref<NetwPromise> all = players->fetch_all();
    REQUIRE(all.is_valid());
    REQUIRE(all->get_is_settled());
    const Array records = all->get_result();

    NETW_CHECK_EQ(int(records.size()), 2);
    NETW_CHECK_EQ(int(players->get_columns().size()), 1);
    CHECK(players->get_columns().has(StringName("score")));
}

TEST_CASE(
    "[Networked][Database][Hosted] DB8 a record that disagrees with the "
    "schema names what is missing and what is unknown, and a record "
    "that agrees announces nothing"
) {
    Stand stand;
    stand.db->declare_table(StringName("rocks"), names("health", "position"));
    announcements().clear();
    stand.db->connect(
        StringName("schema_mismatch"),
        callable_mp_static(&note_mismatch)
    );

    Dictionary clean;
    clean[StringName("health")] = 10;
    clean[StringName("position")] = Vector2();
    stand.backend->seed(StringName("ok"), clean);
    stand.db->find(StringName("rocks"), StringName("ok"));
    CHECK(announcements().is_empty());

    Dictionary short_row;
    short_row[StringName("health")] = 10;
    stand.backend->seed(StringName("short"), short_row);
    stand.db->find(StringName("rocks"), StringName("short"));
    NETW_CHECK_EQ(int(announcements().size()), 1);
    Dictionary noted = announcements()[0];
    CHECK(Array(noted["missing"]).has(StringName("position")));
    CHECK(Array(noted["unknown"]).is_empty());

    Dictionary strange = clean.duplicate();
    strange[StringName("gold")] = 5;
    stand.backend->seed(StringName("strange"), strange);
    stand.db->find(StringName("rocks"), StringName("strange"));
    NETW_CHECK_EQ(int(announcements().size()), 2);
    noted = announcements()[1];
    CHECK(Array(noted["unknown"]).has(StringName("gold")));
    CHECK(Array(noted["missing"]).is_empty());
    announcements().clear();
}

TEST_CASE(
    "[Networked][Database][Hosted] DB9 the purge policy deletes the row "
    "it could not read and answers the clean slate, and leaves a row "
    "that is merely short alone"
) {
    Stand stand;
    stand.db->set_mismatch_policy(NetwDatabase::PURGE);
    stand.db->declare_table(StringName("rocks"), names("health", "position"));
    Dictionary strange;
    strange[StringName("gold")] = 5;
    stand.backend->seed(StringName("r1"), strange);
    Dictionary short_row;
    short_row[StringName("health")] = 10;
    stand.backend->seed(StringName("r2"), short_row);

    const Dictionary purged
        = settled_record(stand.db->find(StringName("rocks"), StringName("r1")));
    CHECK(purged.is_empty());
    NETW_CHECK_EQ(stand.backend->erase_count(), 1);
    CHECK_FALSE(stand.backend->holds(StringName("r1")));

    const Dictionary kept
        = settled_record(stand.db->find(StringName("rocks"), StringName("r2")));
    NETW_CHECK_EQ(int(kept[StringName("health")]), 10);
    NETW_CHECK_EQ(stand.backend->erase_count(), 1);
}

TEST_CASE(
    "[Networked][Database][Hosted] DB10 the load-partial default keeps "
    "the declared columns and drops the stray one, which is what makes "
    "a schema that grew a column an ordinary save rather than damage"
) {
    Stand stand;
    stand.db->declare_table(StringName("rocks"), names("health", "position"));
    Dictionary stored;
    stored[StringName("health")] = 50;
    stored[StringName("position")] = Vector2();
    stored[StringName("gold")] = 5;
    stand.backend->seed(StringName("r1"), stored);

    const Dictionary loaded
        = settled_record(stand.db->find(StringName("rocks"), StringName("r1")));

    NETW_CHECK_EQ(
        int(stand.db->get_mismatch_policy()),
        int(NetwDatabase::LOAD_PARTIAL)
    );
    CHECK(loaded.has(StringName("health")));
    CHECK(loaded.has(StringName("position")));
    CHECK_FALSE(loaded.has(StringName("gold")));
    NETW_CHECK_EQ(stand.backend->erase_count(), 0);
}

TEST_CASE(
    "[Networked][Database][Hosted] DB11 the fail policy refuses the "
    "read and leaves the row on the backend for the caller to decide "
    "about"
) {
    Stand stand;
    stand.db->set_mismatch_policy(NetwDatabase::FAIL);
    stand.db->declare_table(StringName("rocks"), names("health"));
    Dictionary stored;
    stored[StringName("health")] = 50;
    stored[StringName("gold")] = 5;
    stand.backend->seed(StringName("r1"), stored);

    const Ref<NetwPromise> refused
        = stand.db->find(StringName("rocks"), StringName("r1"));

    REQUIRE(refused.is_valid());
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(refused->get_code(), int(ERR_UNCONFIGURED));
    CHECK(stand.backend->holds(StringName("r1")));
    NETW_CHECK_EQ(stand.backend->erase_count(), 0);
}

TEST_CASE(
    "[Networked][Database][Hosted] DB12 a schema declaration types the "
    "columns and a bare name list types nothing, which is what keeps "
    "the untyped form from judging a save it was never told the shape "
    "of"
) {
    Stand typed;
    typed.db->declare_table(StringName("rocks"), typed_rocks());

    NETW_CHECK_EQ(
        typed.db->get_column_type(StringName("rocks"), StringName("health")),
        int(netw::NetwMultiplayer::COLUMN_F64)
    );
    NETW_CHECK_EQ(
        typed.db->get_column_type(StringName("rocks"), StringName("origin")),
        int(netw::NetwMultiplayer::COLUMN_VECTOR2)
    );
    NETW_CHECK_EQ(
        typed.db->get_column_type(StringName("rocks"), StringName("never")),
        -1
    );

    Stand bare;
    bare.db->declare_table(StringName("rocks"), names("health", "origin"));
    NETW_CHECK_EQ(
        bare.db->get_column_type(StringName("rocks"), StringName("health")),
        -1
    );
}

TEST_CASE(
    "[Networked][Database][Hosted] DB13 a stored value of the wrong "
    "shape is dropped so the scene keeps its default, and the rest of "
    "the row still loads"
) {
    Stand stand;
    stand.db->declare_table(StringName("rocks"), typed_rocks());

    Dictionary sound;
    sound[StringName("health")] = 50.0;
    sound[StringName("origin")] = Vector2(1, 1);
    stand.backend->seed(StringName("r1"), sound);
    const Dictionary kept
        = settled_record(stand.db->find(StringName("rocks"), StringName("r1")));
    CHECK(kept.has(StringName("health")));
    CHECK(kept.has(StringName("origin")));

    Dictionary mistyped;
    mistyped[StringName("health")] = 50.0;
    mistyped[StringName("origin")] = String("not a vector");
    stand.backend->seed(StringName("r2"), mistyped);
    const Dictionary judged
        = settled_record(stand.db->find(StringName("rocks"), StringName("r2")));
    CHECK(judged.has(StringName("health")));
    CHECK_FALSE(judged.has(StringName("origin")));
}

TEST_CASE(
    "[Networked][Database][Hosted] DB14 an untyped table judges no "
    "shape at all, and a self-describing column is exempt even where "
    "the table is typed"
) {
    Stand untyped;
    untyped.db->declare_table(StringName("rocks"), names("health", "origin"));
    Dictionary anything;
    anything[StringName("health")] = String("fifty");
    anything[StringName("origin")] = 3;
    untyped.backend->seed(StringName("r1"), anything);

    const Dictionary kept = settled_record(
        untyped.db->find(StringName("rocks"), StringName("r1"))
    );
    NETW_CHECK_EQ(int(kept.size()), 2);

    Stand blobs;
    const Ref<NetwSchema> declaration = NetwSchema::create(StringName("blobs"));
    declaration->replicated(false);
    declaration->column(
        StringName("payload"),
        netw::NetwMultiplayer::COLUMN_VARIANT,
        1,
        Ref<netw::NetwQuantize>()
    );
    blobs.db->declare_table(StringName("blobs"), declaration);
    Dictionary payload;
    Dictionary nested;
    nested[StringName("any")] = Array();
    payload[StringName("payload")] = nested;
    blobs.backend->seed(StringName("b1"), payload);

    const Dictionary held
        = settled_record(blobs.db->find(StringName("blobs"), StringName("b1")));
    CHECK(held.has(StringName("payload")));
}

TEST_CASE(
    "[Networked][Database][Hosted] DB15 the backend initializes once, "
    "on the first read or write rather than on a declaration, so every "
    "table declared before then is warmed in one batch"
) {
    Stand stand;
    stand.db->declare_table(StringName("players"), names("hp"));
    stand.db->declare_table(StringName("items"), names("damage"));

    CHECK(stand.backend->warmed().is_empty());

    stand.db->find(StringName("players"), StringName("nobody"));
    NETW_CHECK_EQ(int(stand.backend->warmed().size()), 1);
    const Array directives = stand.backend->warmed()[0];
    NETW_CHECK_EQ(int(directives.size()), 2);
    const Dictionary first = directives[0];
    NETW_CHECK_EQ(
        int(Ref<WarmRequest>(first["request"])->get_kind()),
        int(WarmRequest::KIND_ALL)
    );

    stand.db->find(StringName("items"), StringName("nobody"));
    NETW_CHECK_EQ(int(stand.backend->warmed().size()), 1);
}

TEST_CASE(
    "[Networked][Database][Hosted] DB17 a warm request carries the "
    "scope it was built for, which is what a policy answers with and "
    "the backend reads to decide how much to pull"
) {
    NETW_CHECK_EQ(
        int(WarmRequest::none()->get_kind()),
        int(WarmRequest::KIND_NONE)
    );
    NETW_CHECK_EQ(
        int(WarmRequest::all()->get_kind()),
        int(WarmRequest::KIND_ALL)
    );

    Array wanted;
    wanted.push_back(StringName("a"));
    wanted.push_back(StringName("b"));
    const Ref<WarmRequest> by_ids = WarmRequest::ids(wanted);
    NETW_CHECK_EQ(int(by_ids->get_kind()), int(WarmRequest::KIND_IDS));
    NETW_CHECK_EQ(int(by_ids->get_id_list().size()), 2);

    Dictionary online;
    online[StringName("online")] = true;
    const Ref<WarmRequest> by_filter = WarmRequest::filter(online);
    NETW_CHECK_EQ(int(by_filter->get_kind()), int(WarmRequest::KIND_FILTER));
    CHECK(bool(by_filter->get_filter_map()[StringName("online")]));
}

TEST_CASE(
    "[Networked][Database][Hosted] DB16 clearing the warm policy warms "
    "nothing, and the runtime warm reaches the backend for one table "
    "whatever the policy said"
) {
    Stand stand;
    stand.db->set_warm_policy(Ref<netw::WarmPolicy>());
    stand.db->declare_table(StringName("players"), names("hp"));

    stand.db->find(StringName("players"), StringName("nobody"));
    NETW_CHECK_EQ(int(stand.backend->warmed().size()), 1);
    CHECK(Array(stand.backend->warmed()[0]).is_empty());

    const Ref<WarmRequest> request = WarmRequest::all();
    NETW_CHECK_EQ(
        settled_code(stand.db->warm(StringName("players"), request)),
        int(OK)
    );
    NETW_CHECK_EQ(int(stand.backend->warmed().size()), 2);
    const Array directives = stand.backend->warmed()[1];
    NETW_CHECK_EQ(int(directives.size()), 1);
    const Dictionary only = directives[0];
    CHECK(StringName(only["table"]) == StringName("players"));
}

TEST_CASE(
    "[Networked][Database][Hosted] DB18 a policy subclass answers for "
    "every table the schema locked, so a narrowed plan replaces the "
    "stock answer rather than being merged into it"
) {
    Stand stand;
    stand.db->set_warm_policy(Ref<netw::WarmPolicy>(memnew(NarrowedPolicy)));
    stand.db->declare_table(StringName("players"), names("hp"));

    stand.db->find(StringName("players"), StringName("nobody"));

    NETW_CHECK_EQ(int(stand.backend->warmed().size()), 1);
    const Array directives = stand.backend->warmed()[0];
    NETW_CHECK_EQ(int(directives.size()), 1);
    const Dictionary planned = directives[0];
    NETW_CHECK_EQ(
        int(Ref<WarmRequest>(planned["request"])->get_kind()),
        int(WarmRequest::KIND_IDS)
    );
}

TEST_CASE(
    "[Networked][Database][Hosted] DB19 a policy is asked per table and "
    "a table it plans nothing for is left out of the batch, so warming "
    "is scoped by the policy rather than by what was declared"
) {
    Stand stand;
    stand.db->set_warm_policy(Ref<netw::WarmPolicy>(memnew(PlayersOnlyPolicy)));
    stand.db->declare_table(StringName("players"), names("hp"));
    stand.db->declare_table(StringName("items"), names("damage"));

    stand.db->find(StringName("players"), StringName("nobody"));

    NETW_CHECK_EQ(int(stand.backend->warmed().size()), 1);
    const Array directives = stand.backend->warmed()[0];
    NETW_CHECK_EQ(int(directives.size()), 1);
    const Dictionary planned = directives[0];
    CHECK(StringName(planned["table"]) == StringName("players"));
}

TEST_CASE(
    "[Networked][Database][Hosted] DB20 the fallback commit refuses a "
    "backend whose writes settle later, because sequencing them would "
    "mean waiting and a backend that defers owes its own commit"
) {
    const Ref<NetwDatabaseBackend> backend
        = Ref<NetwDatabaseBackend>(memnew(DeferringBackend));

    Dictionary values;
    values["health"] = 1;
    Dictionary operation;
    operation["table"] = StringName("rocks");
    operation["id"] = StringName("r1");
    operation["data"] = values;
    Array operations;
    operations.push_back(operation);

    const Ref<NetwPromise> answer = backend->commit(operations);

    REQUIRE(answer.is_valid());
    CHECK(answer->get_is_settled());
    CHECK(answer->get_is_failed());
    NETW_CHECK_EQ(int(answer->get_code()), int(ERR_UNAVAILABLE));
}

Ref<netw::NetwMultiplayer> a_server_session() {
    Ref<netw::NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

RID a_mob_schema(const Ref<netw::NetwMultiplayer> &p_core) {
    const RID schema = p_core->schema_create(StringName("SaveMob"));
    const int pos = p_core->schema_add_column(
        schema,
        StringName("pos"),
        netw::NetwMultiplayer::COLUMN_VECTOR3,
        1
    );
    Ref<netw::NetwQuantizeScalar> half_metre;
    half_metre.instantiate();
    half_metre->set_min_limit(-512.0);
    half_metre->set_max_limit(512.0);
    half_metre->set_resolution_step(0.5);
    p_core->schema_set_column_quantizer(schema, pos, half_metre);
    p_core->schema_add_column(
        schema,
        StringName("hp"),
        netw::NetwMultiplayer::COLUMN_U16,
        1
    );
    REQUIRE(p_core->schema_seal(schema) == OK);
    return schema;
}

PackedStringArray save_keys(int p_count) {
    PackedStringArray ids;
    for (int at = 0; at < p_count; at++) {
        ids.push_back(vformat("mob_%d", at));
    }
    return ids;
}

TEST_CASE(
    "[Networked][Database][Hosted] TP1 a table saves as ONE record named for "
    "its schema, because routes must be re-minted as a set, and a fresh "
    "session hydrating that record reads every value back unquantized"
) {
    Stand stand;
    const Ref<netw::NetwMultiplayer> core = a_server_session();
    const RID schema = a_mob_schema(core);
    const RID table = core->table_create(schema);

    PackedVector3Array positions;
    positions.push_back(Vector3(1.0f / 3.0f, -2.7f, 5.25f));
    positions.push_back(Vector3());
    positions.push_back(Vector3(0, 1, 0));
    PackedInt32Array hp;
    hp.push_back(100);
    hp.push_back(90);
    hp.push_back(80);

    const PackedInt64Array routes = core->liveness_claim_routes(3);
    core->table_write_routes(table, routes);
    core->table_write_column(table, 0, positions);
    core->table_write_column(table, 1, hp);
    NETW_CHECK_EQ(int(core->table_commit(table)), int(OK));

    const PackedStringArray ids = save_keys(3);
    NETW_CHECK_EQ(
        settled_code(
            stand.db->table_flush(core, table, StringName("mobs"), ids)
        ),
        int(OK)
    );

    const Array written = stand.backend->committed_operations();
    NETW_CHECK_EQ(int(written.size()), 1);
    REQUIRE(written.size() == 1);
    const Dictionary record = written[0];
    CHECK(StringName(record["id"]) == StringName("SaveMob"));

    const Ref<netw::NetwMultiplayer> fresh = a_server_session();
    const RID reloaded = fresh->table_create(a_mob_schema(fresh));

    const Dictionary back = settled_record(
        stand.db->table_hydrate(fresh, reloaded, StringName("mobs"))
    );

    CHECK(PackedStringArray(back[StringName("ids")]) == ids);
    const PackedInt64Array fresh_routes = back[StringName("routes")];
    NETW_CHECK_EQ(fresh_routes.size(), 3);
    CHECK(fresh->table_read_routes(reloaded) == fresh_routes);
    CHECK(PackedInt32Array(fresh->table_read_column(reloaded, 1)) == hp);
    CHECK(
        PackedVector3Array(fresh->table_read_column(reloaded, 0)) == positions
    );
}

TEST_CASE(
    "[Networked][Database][Hosted] TP2 the save keys are parallel to the "
    "committed rows, so a count that does not line up is refused before "
    "anything reaches the backend rather than naming the wrong row forever"
) {
    Stand stand;
    const Ref<netw::NetwMultiplayer> core = a_server_session();
    const RID table = core->table_create(a_mob_schema(core));

    PackedVector3Array positions;
    positions.push_back(Vector3());
    positions.push_back(Vector3(1, 1, 1));
    PackedInt32Array hp;
    hp.push_back(1);
    hp.push_back(2);
    core->table_write_routes(table, core->liveness_claim_routes(2));
    core->table_write_column(table, 0, positions);
    core->table_write_column(table, 1, hp);
    core->table_commit(table);

    PackedStringArray one;
    one.push_back("only_one");

    NETW_CHECK_EQ(
        settled_code(
            stand.db->table_flush(core, table, StringName("mobs"), one)
        ),
        int(ERR_INVALID_DATA)
    );
    NETW_CHECK_EQ(stand.backend->commit_count(), 0);
}

TEST_CASE(
    "[Networked][Database][Hosted] TP3 a route column is never saved and "
    "hydrate zero-fills it, because a persisted route means nothing in the "
    "session that loads it"
) {
    Stand stand;
    const Ref<netw::NetwMultiplayer> core = a_server_session();
    const RID schema = core->schema_create(StringName("SaveEdge"));
    core->schema_add_column(
        schema,
        StringName("target"),
        netw::NetwMultiplayer::COLUMN_ENTITY,
        1
    );
    core->schema_add_column(
        schema,
        StringName("weight"),
        netw::NetwMultiplayer::COLUMN_F32,
        1
    );
    REQUIRE(core->schema_seal(schema) == OK);
    const RID table = core->table_create(schema);

    const PackedInt64Array routes = core->liveness_claim_routes(2);
    PackedInt64Array targets;
    targets.push_back(routes[1]);
    targets.push_back(routes[0]);
    PackedFloat32Array weights;
    weights.push_back(1.5f);
    weights.push_back(2.5f);
    core->table_write_routes(table, routes);
    core->table_write_column(table, 0, targets);
    core->table_write_column(table, 1, weights);
    core->table_commit(table);

    PackedStringArray ids;
    ids.push_back("e0");
    ids.push_back("e1");
    NETW_CHECK_EQ(
        settled_code(
            stand.db->table_flush(core, table, StringName("edges"), ids)
        ),
        int(OK)
    );

    const Array written = stand.backend->committed_operations();
    REQUIRE(written.size() == 1);
    const Dictionary record = written[0];
    const Dictionary saved = record["data"];
    CHECK_FALSE(saved.has(StringName("target")));
    CHECK(saved.has(StringName("weight")));

    const Ref<netw::NetwMultiplayer> fresh = a_server_session();
    const RID fresh_schema = fresh->schema_create(StringName("SaveEdge"));
    fresh->schema_add_column(
        fresh_schema,
        StringName("target"),
        netw::NetwMultiplayer::COLUMN_ENTITY,
        1
    );
    fresh->schema_add_column(
        fresh_schema,
        StringName("weight"),
        netw::NetwMultiplayer::COLUMN_F32,
        1
    );
    REQUIRE(fresh->schema_seal(fresh_schema) == OK);
    const RID reloaded = fresh->table_create(fresh_schema);

    stand.db->table_hydrate(fresh, reloaded, StringName("edges"));

    PackedInt64Array zeroed;
    zeroed.push_back(0);
    zeroed.push_back(0);
    CHECK(PackedInt64Array(fresh->table_read_column(reloaded, 0)) == zeroed);
    CHECK(PackedFloat32Array(fresh->table_read_column(reloaded, 1)) == weights);
}

TEST_CASE(
    "[Networked][Database][Hosted] TP4 a hydrate is a first play whether the "
    "record is merely absent or no storage was ever configured, so neither "
    "leaves the table half-written and neither fails the session's bring-up"
) {
    Stand stand;
    const Ref<netw::NetwMultiplayer> core = a_server_session();
    const RID table = core->table_create(a_mob_schema(core));

    const Dictionary absent = settled_record(
        stand.db->table_hydrate(core, table, StringName("mobs"))
    );

    CHECK(PackedInt64Array(absent[StringName("routes")]).is_empty());
    CHECK(PackedStringArray(absent[StringName("ids")]).is_empty());
    CHECK(core->table_read_routes(table).is_empty());

    SUBCASE(
        "a session that configured no storage answers the same first play"
    ) {
        Ref<NetwDatabase> unconfigured;
        unconfigured.instantiate();

        const Ref<NetwPromise> answer
            = unconfigured->table_hydrate(core, table, StringName("mobs"));

        REQUIRE(answer.is_valid());
        CHECK(answer->get_is_settled());
        CHECK_FALSE(answer->get_is_failed());
        const Dictionary refused = settled_record(answer);
        CHECK(PackedInt64Array(refused[StringName("routes")]).is_empty());
        CHECK(PackedStringArray(refused[StringName("ids")]).is_empty());
        CHECK(core->table_read_routes(table).is_empty());
    }
}

} // namespace TestDatabaseLaws
