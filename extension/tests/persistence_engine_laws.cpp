#include "support/netw_test.h"

#include "support/persistence_stand.h"

#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/api/schema_core.hpp"

namespace TestPersistenceEngineLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwPersistenceEngine;
using netw::NetwPromise;
using netw_test::DatabaseStand;

Array &declarations() {
    static Array held;
    return held;
}

void record_declaration(
    Object *p_db,
    const StringName &p_table,
    const Array &p_columns
) {
    Dictionary row;
    row["db"] = p_db;
    row["table"] = p_table;
    row["columns"] = p_columns;
    declarations().push_back(row);
}

int &signal_count() {
    static int fired = 0;
    return fired;
}

void note_signal() {
    signal_count() += 1;
}

void check_named(const Variant &p_value, const char *p_expected) {
    const String held = StringName(p_value);
    NETW_FORMAT_TEXT(held_text, held.utf8().get_data());
    CAPTURE(held_text);
    CAPTURE(p_expected);
    CHECK(held == String(p_expected));
}

Array one_column(const char *p_property, double p_interval) {
    Dictionary entry;
    entry["property"] = StringName(p_property);
    entry["interval"] = p_interval;
    Array declared;
    declared.push_back(entry);
    return declared;
}

struct Persisted {
    Node2D *owner = nullptr;
    Ref<NetwEntity> entity;
    DatabaseStand database;
    Dictionary declaration;
    Ref<NetwPersistenceEngine> engine;

    Persisted(const char *p_id, const Array &p_columns)
        : Persisted(p_id, p_columns, true) {
    }

    Persisted(const char *p_id, const Array &p_columns, bool p_with_database) {
        owner = memnew(Node2D);
        owner->set_name("Valeria");
        owner->set_meta(NetwPersistenceEngine::meta_columns(), p_columns);
        NetwEntity::bind(owner, p_id, 0);
        entity = NetwEntity::of(owner);
        REQUIRE(entity.is_valid());

        Array declared_columns;
        for (int at = 0; at < p_columns.size(); ++at) {
            const Dictionary column = p_columns[at];
            declared_columns.push_back(column.get("property", StringName()));
        }
        database.declare(StringName("players"), declared_columns);
        if (p_with_database) {
            declaration["database"] = database.db;
        }
        declaration["table"] = StringName("players");

        declarations().clear();
        NetwPersistenceEngine::forget_claims();
        NetwPersistenceEngine::set_schema_declarer(
            callable_mp_static(&record_declaration)
        );
        NetwPersistenceEngine::set_property_configs_reader(Callable());
        engine = NetwPersistenceEngine::create(entity.ptr(), declaration);
        REQUIRE(engine.is_valid());
    }

    int settled_code(const Ref<NetwPromise> &p_answer) const {
        REQUIRE(p_answer.is_valid());
        REQUIRE(p_answer->get_is_settled());
        if (p_answer->get_is_failed()) {
            return p_answer->get_code();
        }
        return int(int64_t(p_answer->get_result()));
    }

    Dictionary last_upsert() const {
        const Array rows = database.upserts();
        REQUIRE(rows.size() > 0);
        return rows[rows.size() - 1];
    }

    ~Persisted() {
        NetwPersistenceEngine::set_schema_declarer(Callable());
        NetwPersistenceEngine::forget_claims();
        declarations().clear();
        memdelete(owner);
    }
};

TEST_CASE(
    "[Networked][Database][Hosted] PE1 the column set freezes from the "
    "declaration and gather reads the live scene through it"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(10, 20));

    const Dictionary row = saved.engine->gather(Array());

    CHECK_FALSE(saved.engine->columns_empty());
    NETW_CHECK_EQ(int(row.size()), 1);
    CHECK(Vector2(row[StringName("position")]) == Vector2(10, 20));
}

TEST_CASE(
    "[Networked][Database][Hosted] PE2 apply writes only the declared "
    "columns onto the live scene"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(0, 0));
    saved.owner->set_rotation(0.0);

    Dictionary row;
    row[StringName("position")] = Vector2(3, 4);
    row[StringName("rotation")] = 1.5;
    saved.engine->apply(row);

    CHECK(saved.owner->get_position() == Vector2(3, 4));
    NETW_CHECK_CLOSE(saved.owner->get_rotation(), 0.0, 1e-9);
}

TEST_CASE(
    "[Networked][Database][Hosted] PE3 a flush upserts the gathered row "
    "under the record id and resolves OK"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(7, 8));

    const Ref<NetwPromise> written = saved.engine->flush(Array());

    NETW_CHECK_EQ(saved.settled_code(written), int(OK));
    NETW_CHECK_EQ(saved.database.transaction_count(), 1);
    const Dictionary upsert = saved.last_upsert();
    check_named(upsert["table"], "players");
    check_named(upsert["id"], "valeria");
    const Dictionary values = upsert["data"];
    CHECK(Vector2(values[StringName("position")]) == Vector2(7, 8));
}

TEST_CASE(
    "[Networked][Database][Hosted] PE4 a flush of an archetype naming no "
    "database resolves ERR_UNCONFIGURED and writes nothing"
) {
    Persisted saved("valeria", one_column("position", 0.0), false);

    const Ref<NetwPromise> written = saved.engine->flush(Array());

    NETW_CHECK_EQ(saved.settled_code(written), int(ERR_UNCONFIGURED));
    NETW_CHECK_EQ(saved.database.transaction_count(), 0);
}

TEST_CASE(
    "[Networked][Database][Hosted] PE5 a flush that gathers nothing "
    "resolves OK without opening a transaction"
) {
    Persisted saved("valeria", Array());

    const Ref<NetwPromise> written = saved.engine->flush(Array());

    CHECK(saved.engine->columns_empty());
    NETW_CHECK_EQ(saved.settled_code(written), int(OK));
    NETW_CHECK_EQ(saved.database.transaction_count(), 0);
}

TEST_CASE(
    "[Networked][Database][Hosted] PE6 hydrate applies the stored row "
    "and leaves the engine clean against it"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    Dictionary stored;
    stored[StringName("position")] = Vector2(11, 12);
    saved.database.set_stored(stored);
    saved.owner->set_position(Vector2(0, 0));

    const Ref<NetwPromise> read = saved.engine->hydrate();

    NETW_CHECK_EQ(saved.settled_code(read), int(OK));
    CHECK(saved.owner->get_position() == Vector2(11, 12));
    CHECK_FALSE(saved.engine->is_dirty());
}

TEST_CASE(
    "[Networked][Database][Hosted] PE7 hydrating a missing row keeps the "
    "scene defaults and still answers OK"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(5, 5));

    const Ref<NetwPromise> read = saved.engine->hydrate();

    NETW_CHECK_EQ(saved.settled_code(read), int(OK));
    CHECK(saved.owner->get_position() == Vector2(5, 5));
    NETW_CHECK_EQ(saved.database.read_count(), 1);
}

TEST_CASE(
    "[Networked][Database][Hosted] PE8 a read the database refused "
    "answers its code rather than a clean hydrate"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    Dictionary stored;
    stored[StringName("position")] = Vector2(11, 12);
    stored[StringName("gold")] = 5;
    saved.database.set_stored(stored);
    saved.database.db->set_mismatch_policy(netw::NetwDatabase::FAIL);
    saved.owner->set_position(Vector2(5, 5));

    const Ref<NetwPromise> read = saved.engine->hydrate();

    NETW_CHECK_EQ(saved.settled_code(read), int(ERR_UNCONFIGURED));
    CHECK(saved.owner->get_position() == Vector2(5, 5));
}

TEST_CASE(
    "[Networked][Database][Hosted] PE10 is_dirty tracks the live scene "
    "against the last flush"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(1, 1));

    saved.engine->flush(Array());
    CHECK_FALSE(saved.engine->is_dirty());

    saved.owner->set_position(Vector2(2, 2));
    CHECK(saved.engine->is_dirty());
}

TEST_CASE(
    "[Networked][Database][Hosted] PE11 the record id is the entity id, "
    "and the node name only where there is none"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    check_named(saved.engine->record_id(), "valeria");

    saved.entity->set_entity_id(StringName());
    saved.owner->set_name("Nameless");
    check_named(saved.engine->record_id(), "Nameless");
}

TEST_CASE(
    "[Networked][Database][Hosted] PE12 a snapshot tick answers the due "
    "write, and answers nothing a second time unchanged"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(4, 4));

    const Dictionary due = saved.engine->snapshot_tick(1.0);

    check_named(due[StringName("id")], "valeria");
    const Dictionary values = due[StringName("values")];
    CHECK(Vector2(values[StringName("position")]) == Vector2(4, 4));

    saved.engine->commit_snapshot(values);
    CHECK(saved.engine->snapshot_tick(1.0).is_empty());
}

TEST_CASE(
    "[Networked][Database][Hosted] PE13 the table is declared once, "
    "before the first read or write reaches the database"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(1, 2));

    NETW_CHECK_EQ(int(declarations().size()), 0);
    saved.engine->flush(Array());
    NETW_CHECK_EQ(int(declarations().size()), 1);
    saved.engine->hydrate();
    NETW_CHECK_EQ(int(declarations().size()), 1);

    const Dictionary declared = declarations()[0];
    check_named(declared["table"], "players");
    const Array columns = declared["columns"];
    NETW_CHECK_EQ(int(columns.size()), 1);
    const Dictionary column = columns[0];
    check_named(column["property"], "position");
}

TEST_CASE(
    "[Networked][Database][Hosted] PE14 an engine built without an "
    "entity or without a declaration is refused rather than minted"
) {
    Dictionary declaration;
    declaration["table"] = StringName("players");

    ERR_PRINT_OFF;
    CHECK(NetwPersistenceEngine::create(nullptr, declaration).is_null());
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][Database][Hosted] PE15 a hydrate emits hydrated even "
    "where no row existed, so a first play still places its player"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    signal_count() = 0;
    saved.engine->connect("hydrated", callable_mp_static(&note_signal));

    saved.engine->hydrate();

    NETW_CHECK_EQ(signal_count(), 1);
}

TEST_CASE(
    "[Networked][Database][Hosted] PE16 a flush emits flushed only where "
    "the write landed"
) {
    Persisted saved("valeria", one_column("position", 0.0));
    saved.owner->set_position(Vector2(6, 6));
    signal_count() = 0;
    saved.engine->connect("flushed", callable_mp_static(&note_signal));

    saved.engine->flush(Array());
    NETW_CHECK_EQ(signal_count(), 1);

    saved.database.set_outcome(int(ERR_UNAVAILABLE));
    saved.owner->set_position(Vector2(7, 7));
    const Ref<NetwPromise> refused = saved.engine->flush(Array());

    NETW_CHECK_EQ(saved.settled_code(refused), int(ERR_UNAVAILABLE));
    NETW_CHECK_EQ(signal_count(), 1);
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PTC1 committing a loaded table claims "
    "one fresh route per saved row and writes every declared column beneath "
    "them, defaulting a column the save cannot supply rather than refusing "
    "the table"
) {
    Ref<netw::NetwMultiplayer> core;
    core.instantiate();
    const RID schema = core->schema_create(StringName("SavedRow"));
    core->schema_add_column(
        schema,
        StringName("hp"),
        netw::NetwMultiplayer::COLUMN_I32,
        1
    );
    core->schema_add_column(
        schema,
        StringName("owner"),
        netw::NetwMultiplayer::COLUMN_ENTITY,
        1
    );
    REQUIRE(core->schema_seal(schema) == OK);
    const RID table = core->table_create(schema);

    const Dictionary nothing
        = core->persist_table_commit(table, schema, Dictionary());
    CHECK(PackedInt64Array(nothing[StringName("routes")]).is_empty());
    CHECK(PackedStringArray(nothing[StringName("ids")]).is_empty());

    PackedStringArray ids;
    ids.push_back("save-a");
    ids.push_back("save-b");
    PackedInt32Array hp;
    hp.push_back(11);
    hp.push_back(22);
    PackedInt64Array stale_owner;
    stale_owner.push_back(9999);
    stale_owner.push_back(8888);
    Dictionary data;
    data[StringName("ids")] = ids;
    data[StringName("hp")] = hp;
    data[StringName("owner")] = stale_owner;

    const Dictionary out = core->persist_table_commit(table, schema, data);

    const PackedInt64Array routes = out[StringName("routes")];
    NETW_CHECK_EQ(routes.size(), 2);
    NETW_CHECK_EQ(PackedStringArray(out[StringName("ids")]).size(), 2);
    const PackedInt32Array written = core->table_read_column(table, 0);
    NETW_CHECK_EQ(written.size(), 2);
    const PackedInt64Array owners = core->table_read_column(table, 1);
    NETW_CHECK_EQ(owners.size(), 2);
    if (routes.size() == 2 && written.size() == 2 && owners.size() == 2) {
        CHECK(routes[0] != routes[1]);
        CHECK(core->table_read_routes(table) == routes);
        NETW_CHECK_EQ(written[0], 11);
        NETW_CHECK_EQ(written[1], 22);
        NETW_CHECK_EQ(owners[0], 0);
        NETW_CHECK_EQ(owners[1], 0);
    }

    SUBCASE("a column the save carries at the wrong length is defaulted") {
        PackedInt32Array short_hp;
        short_hp.push_back(5);
        Dictionary ragged;
        ragged[StringName("ids")] = ids;
        ragged[StringName("hp")] = short_hp;

        core->persist_table_commit(table, schema, ragged);

        const PackedInt32Array defaulted = core->table_read_column(table, 0);
        NETW_CHECK_EQ(defaulted.size(), 2);
        if (defaulted.size() == 2) {
            NETW_CHECK_EQ(defaulted[0], 0);
            NETW_CHECK_EQ(defaulted[1], 0);
        }
    }
}

} // namespace TestPersistenceEngineLaws
