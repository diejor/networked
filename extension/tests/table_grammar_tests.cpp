/* The TABLE frame byte grammar, hand-built and pinned.
 *
 * This suite is the port's conformance gate. It builds frames byte by byte and
 * proves the decoder reads exactly those bytes, then proves the encoder emits
 * exactly those bytes. An encoder that passes both is byte-compatible with the
 * one these bytes were written against, which is the whole point of writing
 * them down before the fast implementation exists.
 *
 * These cases were run in GDScript against the native classes before they were
 * restated here, so the restatement describes a wire that was already proven
 * rather than one it was written to match.
 */

#include "support/netw_test.h"

#include "netw/handle_ledger.hpp"
#include "netw/table/table_core.hpp"

namespace TestTableGrammar {

using namespace godot;
using netw::NetwHandleLedger;
using netw::SchemaColumn;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::TableCore;

struct Spec {
    const char *key;
    int type;
    int stride;
};

Ref<TableCore> make_core() {
    Ref<TableCore> core;
    core.instantiate();
    return core;
}

Ref<NetwHandleLedger> make_ledger() {
    Ref<NetwHandleLedger> ledger;
    ledger.instantiate();
    return ledger;
}

RID declare_table(
    const Ref<TableCore> &core,
    const Ref<NetwHandleLedger> &ledger,
    const StringName &name,
    const Spec *specs,
    int count
) {
    Ref<SchemaRecord> schema;
    schema.instantiate();
    schema->name = name;
    for (int i = 0; i < count; i++) {
        SchemaCore::append_column(
            schema,
            specs[i].key,
            specs[i].type,
            specs[i].stride
        );
    }
    SchemaCore::fix(schema);
    const RID rid = ledger->rid_create();
    core->declare(rid, schema);
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

// The schema hash rides the wire as a little-endian u16.
PackedByteArray u16_of(int value) {
    return bytes_of({value & 0xFF, (value >> 8) & 0xFF});
}

// Names the first differing byte, because a whole-frame mismatch says nothing
// about which field moved.
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
    Ref<TableCore> core = make_core();
    Ref<NetwHandleLedger> ledger = make_ledger();
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(core, ledger, "GrammarHeader", specs, 1);
    const int hash = core->schema_hash(table);

    PackedByteArray frame = bytes_of({1});
    frame.append_array(u16_of(hash));
    frame.append_array(bytes_of({5, TableCore::FLAG_SNAPSHOT, 2, 7, 9}));
    frame.append_array(bytes_of({44, 1, 255, 255}));

    const Dictionary header = TableCore::peek_header(frame);
    NETW_CHECK_EQ(int64_t(header["table_id"]), 1);
    NETW_CHECK_EQ(int64_t(header["schema_hash"]), hash);
    NETW_CHECK_EQ(int64_t(header["tick"]), 5);
    NETW_CHECK_EQ(int64_t(header["flags"]), TableCore::FLAG_SNAPSHOT);
    NETW_CHECK_EQ(int64_t(header["rows"]), 2);
}

TEST_CASE("[Networked][Table][Hosted] A hand-built frame decodes") {
    Ref<TableCore> core = make_core();
    Ref<NetwHandleLedger> ledger = make_ledger();
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(core, ledger, "GrammarHand", specs, 1);

    PackedByteArray frame = bytes_of({1});
    frame.append_array(u16_of(core->schema_hash(table)));
    frame.append_array(bytes_of({5, 0, 2, 7, 9, 44, 1, 255, 255}));

    NETW_CHECK_EQ(core->admit_header(TableCore::peek_header(frame)), OK);
    const Dictionary result = core->apply_frame(frame);

    NETW_CHECK_EQ(int64_t(result["verdict"]), OK);
    PackedInt64Array expected_routes;
    expected_routes.push_back(7);
    expected_routes.push_back(9);
    CHECK(bool(core->read_routes(table) == expected_routes));
    PackedInt32Array expected_hp;
    expected_hp.push_back(300);
    expected_hp.push_back(65535);
    CHECK(bool(PackedInt32Array(core->read_column(table, 0)) == expected_hp));
    NETW_CHECK_EQ(core->tick_of(table), 5);
    CHECK(bool(PackedInt64Array(result["bound"]) == expected_routes));
}

TEST_CASE("[Networked][Table][Hosted] The encoder emits the hand-built bytes") {
    Ref<TableCore> core = make_core();
    Ref<NetwHandleLedger> ledger = make_ledger();
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(core, ledger, "GrammarHand", specs, 1);

    PackedInt64Array routes;
    routes.push_back(7);
    routes.push_back(9);
    PackedInt32Array hp;
    hp.push_back(300);
    hp.push_back(65535);
    core->write_routes(table, routes);
    core->write_column(table, 0, hp);
    core->commit(table, 5);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(core->schema_hash(table)));
    expected.append_array(bytes_of({5, 0, 2, 7, 9, 44, 1, 255, 255}));

    const TypedArray<PackedByteArray> frames
        = core->encode_frames(table, 1200, false);
    NETW_CHECK_EQ(frames.size(), 1);
    check_bytes(frames[0], expected);
}

TEST_CASE(
    "[Networked][Table][Hosted] A vector column is a little-endian memcpy"
) {
    Ref<TableCore> core = make_core();
    Ref<NetwHandleLedger> ledger = make_ledger();
    const Spec specs[] = {{"pos", SchemaCore::VECTOR3, 1}};
    const RID table = declare_table(core, ledger, "GrammarVector", specs, 1);

    PackedInt64Array routes;
    routes.push_back(1);
    PackedVector3Array pos;
    pos.push_back(Vector3(1.0, 0.0, -2.0));
    core->write_routes(table, routes);
    core->write_column(table, 0, pos);
    core->commit(table, 0);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(core->schema_hash(table)));
    expected.append_array(bytes_of({0, 0, 1, 1}));
    expected.append_array(bytes_of({0, 0, 128, 63, 0, 0, 0, 0, 0, 0, 0, 192}));

    check_bytes(core->encode_frames(table, 1200, false)[0], expected);
}

TEST_CASE(
    "[Networked][Table][Hosted] Bit-packed and varint columns stay aligned"
) {
    Ref<TableCore> core = make_core();
    Ref<NetwHandleLedger> ledger = make_ledger();
    const Spec specs[]
        = {{"flag", SchemaCore::BOOL, 1}, {"link", SchemaCore::ENTITY, 1}};
    const RID table = declare_table(core, ledger, "GrammarBits", specs, 2);

    PackedInt64Array routes;
    routes.push_back(1);
    routes.push_back(2);
    routes.push_back(3);
    PackedByteArray flags = bytes_of({1, 0, 1});
    PackedInt64Array links;
    links.push_back(200);
    links.push_back(0);
    links.push_back(1);
    core->write_routes(table, routes);
    core->write_column(table, 0, flags);
    core->write_column(table, 1, links);
    core->commit(table, 1);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(core->schema_hash(table)));
    expected.append_array(bytes_of({1, 0, 3, 1, 2, 3}));
    // The bools, packed from the low bit up, then the column after them starts
    // byte aligned so it can never straddle.
    expected.append_array(bytes_of({5}));
    expected.append_array(bytes_of({200, 1, 0, 1}));

    check_bytes(core->encode_frames(table, 1200, false)[0], expected);
}

TEST_CASE("[Networked][Table][Hosted] A removal frame is routes only") {
    Ref<TableCore> core = make_core();
    Ref<NetwHandleLedger> ledger = make_ledger();
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(core, ledger, "GrammarRemove", specs, 1);

    PackedInt64Array routes;
    routes.push_back(7);
    routes.push_back(9);
    const TypedArray<PackedByteArray> frames
        = core->encode_removal(table, routes, 4, 1200);

    PackedByteArray expected = bytes_of({1});
    expected.append_array(u16_of(core->schema_hash(table)));
    expected.append_array(bytes_of({4, TableCore::FLAG_REMOVE, 2, 7, 9}));
    NETW_CHECK_EQ(frames.size(), 1);
    check_bytes(frames[0], expected);
}

TEST_CASE("[Networked][Table][Hosted] The lifecycle stream is table id zero") {
    PackedInt64Array routes;
    routes.push_back(12);
    const TypedArray<PackedByteArray> frames
        = TableCore::encode_lifecycle(routes, 3, 1200);

    NETW_CHECK_EQ(frames.size(), 1);
    // A zero hash, so it is parseable by a peer that shares no table with the
    // sender at all.
    check_bytes(
        frames[0],
        bytes_of({0, 0, 0, 3, TableCore::FLAG_REMOVE, 1, 12})
    );

    const Dictionary header = TableCore::peek_header(frames[0]);
    NETW_CHECK_EQ(int64_t(header["table_id"]), TableCore::LIFECYCLE_STREAM);
    NETW_CHECK_EQ(int64_t(header["schema_hash"]), 0);
    NETW_CHECK_EQ(make_core()->admit_header(header), OK);
}

TEST_CASE("[Networked][Table][Hosted] The reserved flag bits stay reserved") {
    Ref<TableCore> core = make_core();
    Ref<NetwHandleLedger> ledger = make_ledger();
    const Spec specs[] = {{"hp", SchemaCore::U16, 1}};
    const RID table = declare_table(core, ledger, "GrammarFlags", specs, 1);

    NETW_CHECK_EQ(TableCore::FLAG_SNAPSHOT, 1);
    NETW_CHECK_EQ(TableCore::FLAG_REMOVE, 2);
    NETW_CHECK_EQ(TableCore::FLAG_PAIR_KEY, 4);
    NETW_CHECK_EQ(TableCore::FLAG_NO_KEY, 8);
    NETW_CHECK_EQ(TableCore::FLAGS_IMPLEMENTED, 3);

    const int reserved[]
        = {TableCore::FLAG_PAIR_KEY, TableCore::FLAG_NO_KEY, 1 << 7};
    for (const int bit : reserved) {
        PackedByteArray frame = bytes_of({1});
        frame.append_array(u16_of(core->schema_hash(table)));
        frame.append_array(bytes_of({1, bit, 0}));
        // A frame carrying a bit this peer cannot name is refused whole, since
        // it cannot know what the rest of the payload means.
        NETW_CHECK_EQ(
            core->admit_header(TableCore::peek_header(frame)),
            ERR_INVALID_DATA
        );
    }
    NETW_CHECK_EQ(int64_t(core->counters()["drops_table_unknown_flag"]), 3);
}

} // namespace TestTableGrammar
