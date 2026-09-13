#include "support/netw_recorder.h"
#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/schema_core.hpp"
#include "netw/table/core.hpp"

namespace TestTablePublish {

using namespace godot;
using netw::NetwMultiplayer;
using netw::SchemaCore;
using netw::table::Core;
using netw_test::Recorder;

struct Stand {
    Ref<NetwMultiplayer> core;
    RID schema;
    RID table;
    int pos = 0;
    int hp = 1;

    Stand(const StringName &name = StringName("PubMob"), int stride = 1) {
        core.instantiate();
        schema = core->schema_create(name);
        pos = core->schema_add_column(
            schema,
            "pos",
            NetwMultiplayer::COLUMN_VECTOR3,
            1
        );
        hp = core->schema_add_column(
            schema,
            "hp",
            NetwMultiplayer::COLUMN_I32,
            stride
        );
        core->schema_seal(schema);
        table = core->table_create(schema);
    }

    Error publish(
        const PackedInt64Array &routes,
        const PackedVector3Array &positions,
        const PackedInt32Array &hits
    ) {
        core->table_write_routes(table, routes);
        core->table_write_column(table, pos, positions);
        core->table_write_column(table, hp, hits);
        return core->table_commit(table);
    }
};

PackedInt64Array int64s(const std::initializer_list<int64_t> &values) {
    PackedInt64Array out;
    for (const int64_t value : values) {
        out.push_back(value);
    }
    return out;
}

PackedInt32Array int32s(const std::initializer_list<int32_t> &values) {
    PackedInt32Array out;
    for (const int32_t value : values) {
        out.push_back(value);
    }
    return out;
}

PackedVector3Array points(int count) {
    PackedVector3Array out;
    for (int at = 0; at < count; ++at) {
        out.push_back(Vector3(float(at), 0.0f, 0.0f));
    }
    return out;
}

TEST_CASE(
    "[Networked][Table][Hosted] TP1 a wave writes every column or none, so a "
    "half table can never reach the wire"
) {
    Stand stand;
    NETW_CHECK_EQ(int(stand.table.is_valid()), 1);
    if (!stand.table.is_valid()) {
        return;
    }

    stand.core->table_write_routes(stand.table, int64s({1, 2}));
    NETW_CHECK_EQ(stand.core->table_commit(stand.table), ERR_INVALID_DATA);

    stand.core->table_write_column(stand.table, stand.pos, points(2));
    NETW_CHECK_EQ(stand.core->table_commit(stand.table), ERR_INVALID_DATA);

    stand.core->table_write_column(stand.table, stand.hp, int32s({1}));
    NETW_CHECK_EQ(stand.core->table_commit(stand.table), ERR_INVALID_DATA);

    stand.core->table_write_column(stand.table, stand.hp, int32s({1, 2}));
    NETW_CHECK_EQ(stand.core->table_commit(stand.table), OK);
    NETW_CHECK_EQ(stand.core->table_read_routes(stand.table).size(), 2);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP2 a column refuses a buffer of the wrong "
    "storage class, which is what the wire layout is computed from"
) {
    Stand stand;
    PackedFloat32Array wrong;
    wrong.push_back(1.0f);

    NETW_CHECK_EQ(
        stand.core->table_write_column(stand.table, stand.pos, wrong),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(
        stand.core->table_write_column(stand.table, 9, int32s({1})),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(
        stand.core->table_write_column(stand.table, -1, int32s({1})),
        ERR_INVALID_DATA
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] TP3 a fixed-capacity column is a stride "
    "rather than a second type family, so its length is rows times stride"
) {
    Stand stand(StringName("PubStride"), 4);

    stand.core->table_write_routes(stand.table, int64s({1, 2}));
    stand.core->table_write_column(stand.table, stand.pos, points(2));
    stand.core->table_write_column(stand.table, stand.hp, int32s({1, 2, 3}));
    NETW_CHECK_EQ(stand.core->table_commit(stand.table), ERR_INVALID_DATA);

    stand.core->table_write_column(
        stand.table,
        stand.hp,
        int32s({1, 2, 3, 4, 5, 6, 7, 8})
    );
    NETW_CHECK_EQ(stand.core->table_commit(stand.table), OK);
    NETW_CHECK_EQ(
        PackedInt32Array(stand.core->table_read_column(stand.table, stand.hp))
            .size(),
        8
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] TP4 the row map answers a whole join in one "
    "crossing, so a hot loop never asks route by route"
) {
    Stand stand;
    NETW_CHECK_EQ(
        stand.publish(int64s({10, 20, 30}), points(3), int32s({1, 2, 3})),
        OK
    );

    NETW_CHECK_EQ(stand.core->table_get_row(stand.table, 20), 1);
    NETW_CHECK_EQ(stand.core->table_get_row(stand.table, 99), -1);

    const PackedInt32Array joined
        = stand.core->table_get_rows(stand.table, int64s({30, 99, 10}));
    NETW_CHECK_EQ(joined.size(), 3);
    if (joined.size() != 3) {
        return;
    }
    NETW_CHECK_EQ(joined[0], 2);
    NETW_CHECK_EQ(joined[1], -1);
    NETW_CHECK_EQ(joined[2], 0);
    NETW_CHECK_EQ(
        stand.core->table_get_rows(stand.table, PackedInt64Array()).size(),
        0
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] TP5 a route minted anywhere is writable, "
    "which is what lets one table key on the routes of another"
) {
    Stand stand;
    const RID entity = stand.core->entity_create();
    const int64_t spawned = stand.core->entity_admit(entity);
    const PackedInt64Array claimed = stand.core->liveness_claim_routes(1);
    NETW_CHECK_EQ(claimed.size(), 1);
    if (claimed.size() != 1) {
        return;
    }

    NETW_CHECK_EQ(
        stand.publish(int64s({spawned, claimed[0]}), points(2), int32s({5, 6})),
        OK
    );
    NETW_CHECK_EQ(stand.core->table_get_row(stand.table, spawned), 0);
    NETW_CHECK_EQ(stand.core->table_get_row(stand.table, claimed[0]), 1);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP6 a read hands back the store rather than "
    "a copy, which is what makes consuming a whole column free"
) {
    Stand stand;
    NETW_CHECK_EQ(stand.publish(int64s({1, 2}), points(2), int32s({7, 8})), OK);

    const PackedVector3Array first
        = stand.core->table_read_column(stand.table, stand.pos);
    const PackedVector3Array second
        = stand.core->table_read_column(stand.table, stand.pos);

    NETW_CHECK_EQ(first.size(), 2);
    NETW_CHECK_EQ(int(first.ptr() == second.ptr()), 1);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP7 a commit marks the table for the tick "
    "boundary, and several commits in one tick collapse to the last"
) {
    Stand stand;
    NETW_CHECK_EQ(stand.core->get_table_core()->dirty_tables().size(), 0);

    NETW_CHECK_EQ(stand.publish(int64s({1}), points(1), int32s({1})), OK);
    NETW_CHECK_EQ(stand.publish(int64s({1}), points(1), int32s({2})), OK);

    NETW_CHECK_EQ(stand.core->get_table_core()->dirty_tables().size(), 1);

    stand.core->get_table_core()->clear_dirty(stand.table);
    NETW_CHECK_EQ(stand.core->get_table_core()->dirty_tables().size(), 0);
    NETW_CHECK_EQ(
        PackedInt32Array(
            stand.core->table_read_column(stand.table, stand.hp)
        )[0],
        2
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] TP8 the intake drain tells the session once "
    "per touched table and reopens the wave behind itself"
) {
    Stand stand;
    Vector<StringName> watched;
    watched.push_back(StringName("table_received"));
    const Recorder recorder(stand.core.ptr(), watched);

    NETW_CHECK_EQ(stand.publish(int64s({1}), points(1), int32s({4})), OK);
    const Ref<Core> plane = stand.core->get_table_core();
    const TypedArray<PackedByteArray> frames
        = plane->encode_frames(stand.table, 1200, false);
    NETW_CHECK_ORDER(frames.size(), 0, >);
    if (frames.size() == 0) {
        return;
    }
    plane->begin_intake();
    for (int at = 0; at < frames.size(); ++at) {
        const PackedByteArray frame = frames[at];
        plane->admit_header(Core::peek_header(frame));
        plane->apply_frame(frame);
    }

    stand.core->table_publish_intake();
    NETW_CHECK_EQ(recorder.count(StringName("table_received")), 1);

    stand.core->table_publish_intake();
    NETW_CHECK_EQ(recorder.count(StringName("table_received")), 1);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP9 a commit snapshots, so a caller writing "
    "through its own buffer on the next line cannot tear the published state"
) {
    Stand stand;
    Variant positions = points(2);
    Variant hits = int32s({10, 20});
    stand.core->table_write_routes(stand.table, int64s({1, 2}));
    stand.core->table_write_column(stand.table, stand.pos, positions);
    stand.core->table_write_column(stand.table, stand.hp, hits);
    NETW_CHECK_EQ(stand.core->table_commit(stand.table), OK);

    positions.set(0, Vector3(99.0f, 99.0f, 99.0f));
    hits.set(1, -1);

    const PackedVector3Array stored_pos
        = stand.core->table_read_column(stand.table, stand.pos);
    const PackedInt32Array stored_hp
        = stand.core->table_read_column(stand.table, stand.hp);
    NETW_CHECK_EQ(stored_pos.size(), 2);
    NETW_CHECK_EQ(stored_hp.size(), 2);
    if (stored_pos.size() != 2 || stored_hp.size() != 2) {
        return;
    }
    NETW_CHECK_EQ(int(stored_pos[0] == Vector3(0.0f, 0.0f, 0.0f)), 1);
    NETW_CHECK_EQ(stored_hp[1], 20);
}

} // namespace TestTablePublish
