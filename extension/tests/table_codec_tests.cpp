/* Round-trip, framing, and verdict laws for the TABLE codec.
 *
 * Two TableCore instances stand in for two peers, with no session and no
 * carrier between them. What is proven is the property the wire design rests
 * on: every frame is a self-contained statement, so any subset of them, in any
 * order, leaves membership exact.
 */

#include "support/netw_test.h"

#include "godot/math.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/quantize.hpp"
#include "netw/table/table_core.hpp"

namespace TestTableCodec {

using namespace godot;
using netw::NetwHandleLedger;
using netw::NetwQuantize;
using netw::NetwQuantizeFixed;
using netw::NetwQuantizeQuaternion;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::TableCore;

constexpr int BUDGET = 1200;

struct Spec {
    const char *key;
    int type;
    int stride = 1;
    Ref<NetwQuantize> quantizer;
};

// Two peers and one handle mint, so a table's RID is the same shape on both.
struct Peers {
    Ref<TableCore> tx;
    Ref<TableCore> rx;
    Ref<NetwHandleLedger> ledger;

    Peers() {
        tx.instantiate();
        rx.instantiate();
        ledger.instantiate();
    }

    RID declare_on(
        const Ref<TableCore> &core,
        const StringName &name,
        const Vector<Spec> &specs
    ) {
        Ref<SchemaRecord> schema;
        schema.instantiate();
        schema->name = name;
        for (int i = 0; i < specs.size(); i++) {
            const int index = SchemaCore::append_column(
                schema,
                specs[i].key,
                specs[i].type,
                specs[i].stride
            );
            if (specs[i].quantizer.is_valid()) {
                SchemaCore::assign_quantizer(schema, index, specs[i].quantizer);
            }
        }
        SchemaCore::fix(schema);
        const RID rid = ledger->rid_create();
        core->declare(rid, schema);
        return rid;
    }

    // The same declaration on both peers, which is what a shared script gives
    // two processes.
    void pair(
        const StringName &name,
        const Vector<Spec> &specs,
        RID &here,
        RID &there
    ) {
        here = declare_on(tx, name, specs);
        there = declare_on(rx, name, specs);
    }

    void deliver(const TypedArray<PackedByteArray> &frames) {
        rx->begin_intake();
        for (int i = 0; i < frames.size(); i++) {
            const PackedByteArray frame = frames[i];
            NETW_CHECK_EQ(rx->admit_header(TableCore::peek_header(frame)), OK);
            NETW_CHECK_EQ(int64_t(rx->apply_frame(frame)["verdict"]), OK);
        }
    }
};

Vector<Spec> one_u16() {
    Vector<Spec> specs;
    specs.push_back(Spec{"hp", SchemaCore::U16, 1, Ref<NetwQuantize>()});
    return specs;
}

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

void publish(
    const Ref<TableCore> &tx,
    const RID &table,
    const PackedInt64Array &routes,
    const PackedInt32Array &hp,
    int64_t tick
) {
    tx->write_routes(table, routes);
    tx->write_column(table, 0, hp);
    tx->commit(table, tick);
}

TEST_CASE("[Networked][Table][Hosted] Every column type round trips") {
    Peers peers;
    Vector<Spec> specs;
    specs.push_back(Spec{"f32", SchemaCore::F32, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"f64", SchemaCore::F64, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"i8", SchemaCore::I8, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"u8", SchemaCore::U8, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"i16", SchemaCore::I16, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"u16", SchemaCore::U16, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"i32", SchemaCore::I32, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"i64", SchemaCore::I64, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"flag", SchemaCore::BOOL, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"v2", SchemaCore::VECTOR2, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"v3", SchemaCore::VECTOR3, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"v4", SchemaCore::VECTOR4, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"col", SchemaCore::COLOR, 1, Ref<NetwQuantize>()});
    specs.push_back(
        Spec{"quat", SchemaCore::QUATERNION, 1, Ref<NetwQuantize>()}
    );
    specs.push_back(Spec{"link", SchemaCore::ENTITY, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"cooldown", SchemaCore::F32, 3, Ref<NetwQuantize>()});
    RID here;
    RID there;
    peers.pair("CodecAll", specs, here, there);
    NETW_CHECK_EQ(peers.tx->schema_hash(here), peers.rx->schema_hash(there));
    NETW_CHECK_EQ(peers.tx->wire_id(here), peers.rx->wire_id(there));

    const PackedInt64Array routes = int64s({11, 12});
    peers.tx->write_routes(here, routes);

    Array written;
    PackedFloat32Array f32;
    f32.push_back(1.5f);
    f32.push_back(-2.5f);
    written.push_back(f32);
    PackedFloat64Array f64;
    f64.push_back(1.0 / 3.0);
    f64.push_back(-1.0e18);
    written.push_back(f64);
    written.push_back(int32s({-128, 127}));
    written.push_back(int32s({0, 255}));
    written.push_back(int32s({-32768, 32767}));
    written.push_back(int32s({0, 65535}));
    written.push_back(int32s({-2147483647 - 1, 2147483647}));
    written.push_back(int64s({-9007199254740991LL, 9007199254740991LL}));
    PackedByteArray flags;
    flags.push_back(1);
    flags.push_back(0);
    written.push_back(flags);
    PackedVector2Array v2;
    v2.push_back(Vector2(1, -1));
    v2.push_back(Vector2());
    written.push_back(v2);
    PackedVector3Array v3;
    v3.push_back(Vector3(1, 2, 3));
    v3.push_back(Vector3(-4, -5, -6));
    written.push_back(v3);
    PackedVector4Array v4;
    v4.push_back(Vector4(1, 2, 3, 4));
    v4.push_back(Vector4());
    written.push_back(v4);
    PackedColorArray colors;
    colors.push_back(Color(0.25, 0.5, 0.75, 1.0));
    colors.push_back(Color(0, 0, 0, 1));
    written.push_back(colors);
    PackedVector4Array quats;
    quats.push_back(Vector4(0, 0, 0, 1));
    quats.push_back(Vector4(0, 1, 0, 0));
    written.push_back(quats);
    written.push_back(int64s({1000, 0}));
    PackedFloat32Array cooldown;
    for (int i = 1; i <= 6; i++) {
        cooldown.push_back(static_cast<float>(i));
    }
    written.push_back(cooldown);

    for (int c = 0; c < written.size(); c++) {
        peers.tx->write_column(here, c, written[c]);
    }
    NETW_CHECK_EQ(peers.tx->commit(here, 7), OK);

    peers.deliver(peers.tx->encode_frames(here, BUDGET, false));

    CHECK(bool(peers.rx->read_routes(there) == routes));
    NETW_CHECK_EQ(peers.rx->tick_of(there), 7);
    CHECK(bool(peers.rx->read_births(there) == routes));
    for (int c = 0; c < written.size(); c++) {
        NETW_FORMAT_INT(column, c);
        CAPTURE(column);
        CHECK(bool(peers.rx->read_column(there, c) == written[c]));
    }
}

TEST_CASE(
    "[Networked][Table][Hosted] Wire ids are name-sorted and order independent"
) {
    Peers peers;
    const char *names[] = {"zulu", "alpha", "mike", "Bravo", "alpha2"};
    const int count = 5;
    for (int i = 0; i < count; i++) {
        peers.declare_on(peers.tx, names[i], one_u16());
    }
    // Declared in the opposite order on the other peer, and they still agree.
    for (int i = count - 1; i >= 0; i--) {
        peers.declare_on(peers.rx, names[i], one_u16());
    }

    PackedStringArray seen;
    for (int id = 1; id <= count; id++) {
        const RID here = peers.tx->table_from_wire_id(id);
        const RID there = peers.rx->table_from_wire_id(id);
        CHECK(here.is_valid());
        NETW_FORMAT_INT(wire_id, id);
        CAPTURE(wire_id);
        CHECK(bool(peers.tx->name_of(here) == peers.rx->name_of(there)));
        seen.push_back(String(peers.tx->name_of(here)));
    }

    PackedStringArray sorted = seen.duplicate();
    sorted.sort();
    // String order, not StringName pointer order, or two peers renumber.
    CHECK(bool(seen == sorted));
    CHECK_FALSE(peers.tx->table_from_wire_id(0).is_valid());
    CHECK_FALSE(peers.tx->table_from_wire_id(99).is_valid());
}

TEST_CASE(
    "[Networked][Table][Hosted] A quantized column round trips within its step"
) {
    Peers peers;
    Ref<NetwQuantizeFixed> fixed;
    fixed.instantiate();
    fixed->step(0.03);
    fixed->limits(-512.0, 512.0);
    Ref<NetwQuantizeQuaternion> smallest_three;
    smallest_three.instantiate();
    smallest_three->bits(10);

    Vector<Spec> specs;
    specs.push_back(Spec{"pos", SchemaCore::VECTOR3, 1, fixed});
    specs.push_back(Spec{"rot", SchemaCore::QUATERNION, 1, smallest_three});
    RID here;
    RID there;
    peers.pair("CodecQuant", specs, here, there);

    const Quaternion turn(Vector3(0, 1, 0), Math::deg_to_rad(90.0));
    PackedVector3Array positions;
    positions.push_back(Vector3(1.5, -2.25, 3.0));
    positions.push_back(Vector3(100.0, 0.0, -100.0));
    PackedVector4Array rotations;
    rotations.push_back(Vector4(0, 0, 0, 1));
    rotations.push_back(Vector4(turn.x, turn.y, turn.z, turn.w));

    peers.tx->write_routes(here, int64s({1, 2}));
    peers.tx->write_column(here, 0, positions);
    peers.tx->write_column(here, 1, rotations);
    peers.tx->commit(here, 2);

    const TypedArray<PackedByteArray> frames
        = peers.tx->encode_frames(here, BUDGET, false);
    const PackedByteArray first = frames[0];
    // Smaller than the raw column alone, which is the only reason to pack.
    NETW_CHECK_EQ(
        first.size() < positions.size() * int(sizeof(Vector3)) + 32,
        true
    );
    peers.deliver(frames);

    const PackedVector3Array got = peers.rx->read_column(there, 0);
    for (int i = 0; i < 2; i++) {
        NETW_CHECK_CLOSE(got[i].distance_to(positions[i]), 0.0, 0.05);
    }
    const PackedVector4Array back = peers.rx->read_column(there, 1);
    const Quaternion decoded(back[1].x, back[1].y, back[1].z, back[1].w);
    NETW_CHECK_CLOSE(Math::abs(decoded.dot(turn)), 1.0, 0.001);
}

TEST_CASE("[Networked][Table][Hosted] Frames are sized to the budget") {
    Peers peers;
    Vector<Spec> specs;
    specs.push_back(Spec{"pos", SchemaCore::VECTOR3, 1, Ref<NetwQuantize>()});
    specs.push_back(Spec{"hp", SchemaCore::U16, 1, Ref<NetwQuantize>()});
    RID here;
    RID there;
    peers.pair("CodecMtu", specs, here, there);

    PackedInt64Array routes;
    PackedVector3Array pos;
    PackedInt32Array hp;
    for (int i = 0; i < 500; i++) {
        routes.push_back(100 + i);
        pos.push_back(Vector3(i, 0, -i));
        hp.push_back(i % 65535);
    }
    peers.tx->write_routes(here, routes);
    peers.tx->write_column(here, 0, pos);
    peers.tx->write_column(here, 1, hp);
    peers.tx->commit(here, 3);

    const TypedArray<PackedByteArray> frames
        = peers.tx->encode_frames(here, BUDGET, false);
    CHECK(frames.size() > 1);
    for (int i = 0; i < frames.size(); i++) {
        const PackedByteArray frame = frames[i];
        NETW_FORMAT_INT(index, i);
        CAPTURE(index);
        CHECK(frame.size() <= BUDGET);
    }

    peers.deliver(frames);
    NETW_CHECK_EQ(peers.rx->read_routes(there).size(), 500);
    const PackedVector3Array arrived = peers.rx->read_column(there, 0);
    CHECK(bool(arrived[499] == Vector3(499, 0, -499)));
}

TEST_CASE("[Networked][Table][Hosted] Frames apply in any order") {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecOrder", one_u16(), here, there);

    PackedInt64Array routes;
    PackedInt32Array hp;
    for (int i = 0; i < 200; i++) {
        routes.push_back(500 + i);
        hp.push_back(i);
    }
    publish(peers.tx, here, routes, hp, 9);

    const TypedArray<PackedByteArray> frames
        = peers.tx->encode_frames(here, 200, false);
    CHECK(frames.size() >= 3);
    TypedArray<PackedByteArray> reversed;
    for (int i = frames.size() - 1; i >= 0; i--) {
        reversed.push_back(frames[i]);
    }
    peers.deliver(reversed);

    NETW_CHECK_EQ(peers.rx->read_routes(there).size(), 200);
    const PackedInt32Array arrived = peers.rx->read_column(there, 0);
    for (int i = 0; i < 200; i++) {
        const int row = peers.rx->row_of(there, 500 + i);
        CHECK(row >= 0);
        NETW_CHECK_EQ(arrived[row], i);
    }
}

TEST_CASE(
    "[Networked][Table][Hosted] Losing frames never corrupts membership"
) {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecLoss", one_u16(), here, there);

    PackedInt64Array routes;
    PackedInt32Array hp;
    for (int i = 0; i < 200; i++) {
        routes.push_back(500 + i);
        hp.push_back(i);
    }
    publish(peers.tx, here, routes, hp, 9);

    const TypedArray<PackedByteArray> frames
        = peers.tx->encode_frames(here, 200, false);
    TypedArray<PackedByteArray> kept;
    for (int i = 0; i < frames.size(); i++) {
        if (i % 2 == 0) {
            kept.push_back(frames[i]);
        }
    }
    peers.deliver(kept);

    const PackedInt64Array arrived = peers.rx->read_routes(there);
    CHECK(arrived.size() > 0);
    CHECK(arrived.size() < 200);
    NETW_CHECK_EQ(peers.rx->read_births(there).size(), arrived.size());
    const PackedInt32Array values = peers.rx->read_column(there, 0);
    for (int i = 0; i < arrived.size(); i++) {
        const int row = peers.rx->row_of(there, arrived[i]);
        NETW_CHECK_EQ(values[row], arrived[i] - 500);
    }
}

TEST_CASE(
    "[Networked][Table][Hosted] A removal erases the row and leaves a memo"
) {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecRemove", one_u16(), here, there);
    publish(peers.tx, here, int64s({1, 2, 3}), int32s({10, 20, 30}), 1);
    peers.deliver(peers.tx->encode_frames(here, BUDGET, false));

    peers.deliver(peers.tx->encode_removal(here, int64s({2}), 2, BUDGET));

    NETW_CHECK_EQ(peers.rx->row_of(there, 2), -1);
    CHECK(bool(peers.rx->read_deaths(there) == int64s({2})));
    NETW_CHECK_EQ(peers.rx->read_routes(there).size(), 2);
    const PackedInt32Array values = peers.rx->read_column(there, 0);
    NETW_CHECK_EQ(values[peers.rx->row_of(there, 1)], 10);
    NETW_CHECK_EQ(values[peers.rx->row_of(there, 3)], 30);

    // The upsert the reliable removal outran must not put the row back.
    publish(peers.tx, here, int64s({1, 2, 3}), int32s({10, 20, 30}), 1);
    peers.deliver(peers.tx->encode_frames(here, BUDGET, false));
    NETW_CHECK_EQ(peers.rx->row_of(there, 2), -1);
    CHECK(int64_t(peers.rx->counters()["table_drops_stale_row"]) > 0);
}

TEST_CASE("[Networked][Table][Hosted] A snapshot clears then applies") {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecSnap", one_u16(), here, there);
    publish(peers.tx, here, int64s({1, 2, 3}), int32s({1, 2, 3}), 1);
    peers.deliver(peers.tx->encode_frames(here, BUDGET, false));

    publish(peers.tx, here, int64s({4}), int32s({9}), 5);
    const TypedArray<PackedByteArray> frames
        = peers.tx->encode_frames(here, BUDGET, true);
    const PackedByteArray first = frames[0];
    NETW_CHECK_EQ(
        int64_t(TableCore::peek_header(first)["flags"])
            & TableCore::FLAG_SNAPSHOT,
        TableCore::FLAG_SNAPSHOT
    );
    peers.deliver(frames);

    CHECK(bool(peers.rx->read_routes(there) == int64s({4})));
    CHECK(bool(peers.rx->read_births(there) == int64s({4})));

    // An empty table still owes a joiner the statement that it is empty.
    publish(peers.tx, here, PackedInt64Array(), PackedInt32Array(), 6);
    const TypedArray<PackedByteArray> empty
        = peers.tx->encode_frames(here, BUDGET, true);
    NETW_CHECK_EQ(empty.size(), 1);
    peers.deliver(empty);
    CHECK(peers.rx->read_routes(there).is_empty());
}

TEST_CASE("[Networked][Table][Hosted] Only the first snapshot frame clears") {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecSnapChunk", one_u16(), here, there);

    PackedInt64Array routes;
    PackedInt32Array hp;
    for (int i = 0; i < 200; i++) {
        routes.push_back(700 + i);
        hp.push_back(i);
    }
    publish(peers.tx, here, routes, hp, 4);

    const TypedArray<PackedByteArray> frames
        = peers.tx->encode_frames(here, 200, true);
    CHECK(frames.size() > 1);
    // Ordered reliable delivery is the whole assembly protocol.
    const PackedByteArray second = frames[1];
    NETW_CHECK_EQ(int64_t(TableCore::peek_header(second)["flags"]), 0);
    peers.deliver(frames);
    NETW_CHECK_EQ(peers.rx->read_routes(there).size(), 200);
}

TEST_CASE(
    "[Networked][Table][Hosted] The lifecycle stream retires a route everywhere"
) {
    Peers peers;
    RID a_here;
    RID a_there;
    RID b_here;
    RID b_there;
    peers.pair("CodecLifeA", one_u16(), a_here, a_there);
    peers.pair("CodecLifeB", one_u16(), b_here, b_there);
    publish(peers.tx, a_here, int64s({1, 2}), int32s({5, 6}), 1);
    peers.deliver(peers.tx->encode_frames(a_here, BUDGET, false));
    publish(peers.tx, b_here, int64s({1, 2}), int32s({5, 6}), 1);
    peers.deliver(peers.tx->encode_frames(b_here, BUDGET, false));

    peers.deliver(TableCore::encode_lifecycle(int64s({2}), 2, BUDGET));

    NETW_CHECK_EQ(peers.rx->row_of(a_there, 2), -1);
    NETW_CHECK_EQ(peers.rx->row_of(b_there, 2), -1);
    CHECK(bool(peers.rx->read_deaths(a_there) == int64s({2})));
    CHECK(bool(peers.rx->read_deaths(b_there) == int64s({2})));
    CHECK(peers.rx->is_tombstoned(2));

    // A tombstone is permanent without keeping the row it described.
    publish(peers.tx, a_here, int64s({1, 2}), int32s({5, 6}), 9);
    peers.deliver(peers.tx->encode_frames(a_here, BUDGET, false));
    NETW_CHECK_EQ(peers.rx->row_of(a_there, 2), -1);
    CHECK(int64_t(peers.rx->counters()["table_drops_tombstone"]) > 0);
}

TEST_CASE("[Networked][Table][Hosted] Cohorts describe a wave, not a frame") {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecWave", one_u16(), here, there);

    PackedInt64Array routes;
    PackedInt32Array hp;
    for (int i = 0; i < 120; i++) {
        routes.push_back(900 + i);
        hp.push_back(i);
    }
    publish(peers.tx, here, routes, hp, 1);
    peers.deliver(peers.tx->encode_frames(here, 200, false));

    NETW_CHECK_EQ(peers.rx->read_births(there).size(), 120);
    CHECK(peers.rx->touched_tables().has(there));

    // A wave that changes nothing reports nothing.
    publish(peers.tx, here, routes, hp, 2);
    peers.deliver(peers.tx->encode_frames(here, 200, false));
    CHECK(peers.rx->read_births(there).is_empty());
    CHECK(peers.rx->read_deaths(there).is_empty());
}

TEST_CASE(
    "[Networked][Table][Hosted] Every malformed frame sorts into its verdict"
) {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecVerdict", one_u16(), here, there);
    publish(peers.tx, here, int64s({1, 2}), int32s({7, 8}), 10);
    const PackedByteArray good
        = peers.tx->encode_frames(here, BUDGET, false)[0];

    PackedByteArray unknown = good.duplicate();
    unknown.set(0, 99);
    NETW_CHECK_EQ(
        peers.rx->admit_header(TableCore::peek_header(unknown)),
        ERR_DOES_NOT_EXIST
    );
    NETW_CHECK_EQ(int64_t(peers.rx->counters()["drops_table_unknown"]), 1);

    PackedByteArray skewed = good.duplicate();
    skewed.set(1, (skewed[1] + 1) % 256);
    skewed.set(2, (skewed[2] + 1) % 256);
    NETW_CHECK_EQ(
        peers.rx->admit_header(TableCore::peek_header(skewed)),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(int64_t(peers.rx->counters()["drops_table_schema"]), 1);

    PackedByteArray flagged = good.duplicate();
    flagged.set(4, TableCore::FLAG_PAIR_KEY);
    NETW_CHECK_EQ(
        peers.rx->admit_header(TableCore::peek_header(flagged)),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(int64_t(peers.rx->counters()["drops_table_unknown_flag"]), 1);

    const PackedByteArray truncated = good.slice(0, good.size() - 2);
    NETW_CHECK_EQ(
        peers.rx->admit_header(TableCore::peek_header(truncated)),
        OK
    );
    NETW_CHECK_EQ(
        int64_t(peers.rx->apply_frame(truncated)["verdict"]),
        ERR_INVALID_DATA
    );
    CHECK(int64_t(peers.rx->counters()["drops_table_truncated"]) >= 1);
    // A refused frame must leave the store exactly as it was.
    CHECK(peers.rx->read_routes(there).is_empty());

    TypedArray<PackedByteArray> only_good;
    only_good.push_back(good);
    peers.deliver(only_good);
    CHECK(bool(peers.rx->read_routes(there) == int64s({1, 2})));
}

TEST_CASE(
    "[Networked][Table][Hosted] Stale frames are refused whole and per row"
) {
    Peers peers;
    RID here;
    RID there;
    peers.pair("CodecStale", one_u16(), here, there);
    publish(peers.tx, here, int64s({1, 2}), int32s({1, 1}), 100);
    peers.deliver(peers.tx->encode_frames(here, BUDGET, false));

    publish(peers.tx, here, int64s({1, 2}), int32s({2, 2}), 3);
    const PackedByteArray ancient
        = peers.tx->encode_frames(here, BUDGET, false)[0];
    NETW_CHECK_EQ(int64_t(peers.rx->apply_frame(ancient)["verdict"]), ERR_SKIP);
    NETW_CHECK_EQ(int64_t(peers.rx->counters()["table_drops_stale"]), 1);
    NETW_CHECK_EQ(PackedInt32Array(peers.rx->read_column(there, 0))[0], 1);

    // Inside the reorder window the frame is admitted and refused per row,
    // which is where freshness lives for a route-0 carrier.
    publish(peers.tx, here, int64s({1, 2}), int32s({3, 3}), 96);
    peers.deliver(peers.tx->encode_frames(here, BUDGET, false));
    NETW_CHECK_EQ(PackedInt32Array(peers.rx->read_column(there, 0))[0], 1);
    CHECK(int64_t(peers.rx->counters()["table_drops_stale_row"]) >= 2);
}

TEST_CASE("[Networked][Table][Hosted] A zero route is refused") {
    Peers peers;
    const RID table = peers.declare_on(peers.tx, "CodecZero", one_u16());

    PackedByteArray frame;
    frame.push_back(1);
    frame.push_back(peers.tx->schema_hash(table) & 0xFF);
    frame.push_back((peers.tx->schema_hash(table) >> 8) & 0xFF);
    // Row two names route zero, which no sender could have issued, so the
    // frame reads as truncation rather than as a row.
    const uint8_t tail[] = {1, 0, 2, 7, 0, 1, 0, 1, 0};
    for (const uint8_t byte : tail) {
        frame.push_back(byte);
    }

    NETW_CHECK_EQ(
        int64_t(peers.tx->apply_frame(frame)["verdict"]),
        ERR_INVALID_DATA
    );
    CHECK(peers.tx->read_routes(table).is_empty());
}

} // namespace TestTableCodec
