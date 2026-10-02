#include "support/netw_test.h"

#include "netw/handle_ledger.hpp"
#include "netw/store/array.hpp"

namespace TestTableGrammar {

using namespace godot;
using netw::NetwHandleLedger;
using netw::SchemaCore;
using netw::store::ArrayFrameHead;
using netw::store::ArrayLanding;
using netw::store::Arrays;
using netw::table::SchemaRecord;

constexpr int PEER = 2;

struct Spec {
    const char *key;
    int type;
    int stride;
};

RID declare_table(
    Arrays &r_book,
    NetwHandleLedger *ledger,
    const StringName &name,
    const Spec *specs,
    int count
) {
    SchemaRecord schema;
    schema.name = name;
    for (int i = 0; i < count; i++) {
        SchemaCore::append_column(
            &schema,
            specs[i].key,
            specs[i].type,
            specs[i].stride
        );
    }
    SchemaCore::fix(&schema);
    const RID rid = ledger->rid_create();
    r_book.declare(rid, &schema);
    return rid;
}

PackedByteArray bytes_of(const std::initializer_list<int> &values) {
    PackedByteArray out;
    out.resize(static_cast<int>(values.size()));
    uint8_t *write = out.ptrw();
    int i = 0;
    for (const int value : values) {
        write[i++] = static_cast<uint8_t>(value);
    }
    return out;
}

PackedByteArray u16_of(int value) {
    return bytes_of({value & 0xFF, (value >> 8) & 0xFF});
}

void check_bytes(
    const PackedByteArray &actual,
    const PackedByteArray &expected
) {
    NETW_CHECK_EQ(actual.size(), expected.size());
    const int shared = MIN(actual.size(), expected.size());
    for (int i = 0; i < shared; i++) {
        if (actual[i] != expected[i]) {
            NETW_FORMAT_INT(at, i);
            CAPTURE(at);
            NETW_CHECK_EQ(actual[i], expected[i]);
            return;
        }
    }
}

TEST_CASE("[Networked][Table][Hosted] The header grammar is read in order") {
    Arrays book;
    NetwHandleLedger held;
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(book, &held, "GrammarHeader", specs, 1);
    const int hash = book.schema_hash(table);

    PackedByteArray frame = bytes_of({1});
    frame.append_array(u16_of(hash));
    frame.append_array(bytes_of({5, Arrays::FLAG_SNAPSHOT, 2, 7, 9}));
    frame.append_array(bytes_of({44, 1, 255, 255}));

    ArrayFrameHead header;
    REQUIRE(Arrays::peek_header(frame, header));
    NETW_CHECK_EQ(int64_t(header.table_id), 1);
    NETW_CHECK_EQ(int64_t(header.schema_hash), hash);
    NETW_CHECK_EQ(int64_t(header.tick), 5);
    NETW_CHECK_EQ(int64_t(header.flags), Arrays::FLAG_SNAPSHOT);
    NETW_CHECK_EQ(int64_t(header.rows), 2);
}

TEST_CASE("[Networked][Table][Hosted] A hand-built frame decodes") {
    Arrays book;
    NetwHandleLedger held;
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(book, &held, "GrammarHand", specs, 1);

    PackedByteArray frame = bytes_of({1});
    frame.append_array(u16_of(book.schema_hash(table)));
    frame.append_array(bytes_of({5, 0, 2, 7, 9, 44, 1, 255, 255}));

    NETW_CHECK_EQ(book.admit_header(frame), OK);
    const ArrayLanding result = book.apply_frame(frame);

    NETW_CHECK_EQ(int64_t(result.verdict), int64_t(OK));
    PackedInt64Array expected_routes;
    expected_routes.push_back(7);
    expected_routes.push_back(9);
    CHECK(bool(book.read_routes(table) == expected_routes));
    PackedInt32Array expected_hp;
    expected_hp.push_back(300);
    expected_hp.push_back(65535);
    CHECK(bool(PackedInt32Array(book.read_column(table, 0)) == expected_hp));
    NETW_CHECK_EQ(book.tick_of(table), 5);
    CHECK(bool(result.bound == expected_routes));
}

TEST_CASE("[Networked][Table][Hosted] The encoder emits the hand-built bytes") {
    Arrays book;
    NetwHandleLedger held;
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(book, &held, "GrammarHand", specs, 1);

    PackedInt64Array routes;
    routes.push_back(7);
    routes.push_back(9);
    PackedInt32Array hp;
    hp.push_back(300);
    hp.push_back(65535);
    book.write_routes(table, routes);
    book.write_column(table, 0, hp);
    book.commit(table, 5);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(book.schema_hash(table)));
    expected.append_array(bytes_of({5, 0, 2, 7, 9, 44, 1, 255, 255}));

    const TypedArray<PackedByteArray> frames
        = book.encode_owed(table, PEER, 1200);
    NETW_CHECK_EQ(frames.size(), 1);
    check_bytes(frames[0], expected);
}

TEST_CASE(
    "[Networked][Table][Hosted] A vector column is a little-endian memcpy"
) {
    Arrays book;
    NetwHandleLedger held;
    const Spec specs[] = {{"pos", SchemaCore::VECTOR3, 1}};
    const RID table = declare_table(book, &held, "GrammarVector", specs, 1);

    PackedInt64Array routes;
    routes.push_back(1);
    PackedVector3Array pos;
    pos.push_back(Vector3(1.0, 0.0, -2.0));
    book.write_routes(table, routes);
    book.write_column(table, 0, pos);
    book.commit(table, 0);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(book.schema_hash(table)));
    expected.append_array(bytes_of({0, 0, 1, 1}));
    expected.append_array(bytes_of({0, 0, 128, 63, 0, 0, 0, 0, 0, 0, 0, 192}));

    check_bytes(book.encode_owed(table, PEER, 1200)[0], expected);
}

TEST_CASE(
    "[Networked][Table][Hosted] Bit-packed and varint columns stay aligned"
) {
    Arrays book;
    NetwHandleLedger held;
    const Spec specs[]
        = {{"flag", SchemaCore::BOOL, 1}, {"link", SchemaCore::ENTITY, 1}};
    const RID table = declare_table(book, &held, "GrammarBits", specs, 2);

    PackedInt64Array routes;
    routes.push_back(1);
    routes.push_back(2);
    routes.push_back(3);
    PackedByteArray flags = bytes_of({1, 0, 1});
    PackedInt64Array links;
    links.push_back(200);
    links.push_back(0);
    links.push_back(1);
    book.write_routes(table, routes);
    book.write_column(table, 0, flags);
    book.write_column(table, 1, links);
    book.commit(table, 1);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(book.schema_hash(table)));
    expected.append_array(bytes_of({1, 0, 3, 1, 2, 3}));
    expected.append_array(bytes_of({5}));
    expected.append_array(bytes_of({200, 1, 0, 1}));

    check_bytes(book.encode_owed(table, PEER, 1200)[0], expected);
}

TEST_CASE("[Networked][Table][Hosted] A removal frame is routes only") {
    Arrays book;
    NetwHandleLedger held;
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(book, &held, "GrammarRemove", specs, 1);

    PackedInt64Array routes;
    routes.push_back(7);
    routes.push_back(9);
    const TypedArray<PackedByteArray> frames
        = book.encode_removal(table, routes, 4, 1200);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(book.schema_hash(table)));
    expected.append_array(bytes_of({4, Arrays::FLAG_REMOVE, 2, 7, 9}));
    NETW_CHECK_EQ(frames.size(), 1);
    check_bytes(frames[0], expected);
}

TEST_CASE("[Networked][Table][Hosted] The lifecycle stream is table id zero") {
    PackedInt64Array routes;
    routes.push_back(12);
    const TypedArray<PackedByteArray> frames
        = Arrays::encode_lifecycle(routes, 3, 1200);

    NETW_CHECK_EQ(frames.size(), 1);
    check_bytes(frames[0], bytes_of({0, 0, 0, 3, Arrays::FLAG_REMOVE, 1, 12}));

    ArrayFrameHead header;
    REQUIRE(Arrays::peek_header(frames[0], header));
    NETW_CHECK_EQ(int64_t(header.table_id), int64_t(Arrays::LIFECYCLE_STREAM));
    NETW_CHECK_EQ(int64_t(header.schema_hash), int64_t(0));
    Arrays bare;
    NETW_CHECK_EQ(bare.admit_header(frames[0]), OK);
}

TEST_CASE("[Networked][Table][Hosted] The reserved flag bits stay reserved") {
    Arrays book;
    NetwHandleLedger held;
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(book, &held, "GrammarFlags", specs, 1);

    NETW_CHECK_EQ(int(Arrays::FLAG_SNAPSHOT), 1);
    NETW_CHECK_EQ(int(Arrays::FLAG_REMOVE), 2);
    NETW_CHECK_EQ(int(Arrays::FLAG_PAIR_KEY), 4);
    NETW_CHECK_EQ(int(Arrays::FLAG_NO_KEY), 8);
    NETW_CHECK_EQ(int(Arrays::FLAGS_IMPLEMENTED), 3);

    const int reserved[] = {Arrays::FLAG_PAIR_KEY, Arrays::FLAG_NO_KEY, 1 << 7};
    for (const int bit : reserved) {
        PackedByteArray frame = bytes_of({1});
        frame.append_array(u16_of(book.schema_hash(table)));
        frame.append_array(bytes_of({1, bit, 0}));
        NETW_CHECK_EQ(book.admit_header(frame), ERR_INVALID_DATA);
    }
    NETW_CHECK_EQ(int64_t(book.counters()["drops_table_unknown_flag"]), 3);
}

} // namespace TestTableGrammar
