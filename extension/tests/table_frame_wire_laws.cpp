#include "support/netw_test.h"

#include "netw/handle_ledger.hpp"
#include "netw/table/core.hpp"

namespace TestTableFrameWire {

using namespace godot;
using netw::NetwHandleLedger;
using netw::NetwQuantize;
using netw::SchemaCore;
using netw::table::Core;
using netw::table::SchemaRecord;

constexpr int BUDGET = 1200;

RID declare_pair(const Ref<Core> &p_core, NetwHandleLedger &p_ledger) {
    SchemaRecord schema;
    schema.name = StringName("hp_table");
    SchemaCore::append_column(&schema, StringName("hp"), SchemaCore::U16, 1);
    SchemaCore::fix(&schema);
    const RID rid = p_ledger.rid_create();
    p_core->declare(rid, &schema);
    return rid;
}

PackedByteArray one_frame(const Ref<Core> &p_tx, const RID &p_table) {
    PackedInt64Array routes;
    routes.push_back(11);
    routes.push_back(12);
    PackedInt32Array hp;
    hp.push_back(70);
    hp.push_back(80);
    p_tx->write_routes(p_table, routes);
    p_tx->write_column(p_table, 0, hp);
    p_tx->commit(p_table, 4);
    const TypedArray<PackedByteArray> frames
        = p_tx->encode_frames(p_table, BUDGET, true);
    REQUIRE(frames.size() == 1);
    return frames[0];
}

TEST_CASE(
    "[Networked][Table][Hosted] TW1 the header transcribed from WIRE.md 9.12 "
    "packs to the bytes tools/wire_decode.py holds it to"
) {
    netw::wire::WriteStream stream;
    Core::FrameHead head;
    head.table_id = 3;
    head.schema_hash = 0xBEEF;
    head.tick = 300;
    head.flags = 1;
    head.rows = 2;
    REQUIRE(Core::FrameHead::wire.run(stream, head));
    REQUIRE(stream.align_verify());
    const PackedByteArray bytes = stream.to_bytes();

    REQUIRE(bytes.size() == 7);
    NETW_CHECK_EQ(bytes[0], 0x03);
    NETW_CHECK_EQ(bytes[1], 0xef);
    NETW_CHECK_EQ(bytes[2], 0xbe);
    NETW_CHECK_EQ(bytes[3], 0xac);
    NETW_CHECK_EQ(bytes[4], 0x02);
    NETW_CHECK_EQ(bytes[5], 0x01);
    NETW_CHECK_EQ(bytes[6], 0x02);
}

TEST_CASE(
    "[Networked][Table][Hosted] TW2 a frame that runs out inside its columns "
    "binds no row at all, because a table applied up to the truncation point "
    "holds rows the sender never claimed were whole"
) {
    NetwHandleLedger ledger;
    Ref<Core> tx;
    tx.instantiate();
    Ref<Core> rx;
    rx.instantiate();
    const RID here = declare_pair(tx, ledger);
    const RID there = declare_pair(rx, ledger);
    const PackedByteArray whole = one_frame(tx, here);
    REQUIRE(whole.size() > 2);

    rx->begin_intake();
    const Dictionary verdict
        = rx->apply_frame(whole.slice(0, whole.size() - 1));

    NETW_CHECK_EQ(int64_t(verdict["verdict"]), int64_t(ERR_INVALID_DATA));
    NETW_CHECK_EQ(rx->read_routes(there).size(), 0);
    NETW_CHECK_EQ(
        int64_t(rx->counters().get(StringName("drops_table_truncated"), 0)),
        int64_t(1)
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] TW3 a frame carrying one byte past its last "
    "column binds no row either, because residue means the sender and this "
    "reader disagree about the shape they are both holding"
) {
    NetwHandleLedger ledger;
    Ref<Core> tx;
    tx.instantiate();
    Ref<Core> rx;
    rx.instantiate();
    const RID here = declare_pair(tx, ledger);
    const RID there = declare_pair(rx, ledger);
    PackedByteArray extended = one_frame(tx, here);
    extended.push_back(0x00);

    rx->begin_intake();
    const Dictionary verdict = rx->apply_frame(extended);

    NETW_CHECK_EQ(int64_t(verdict["verdict"]), int64_t(ERR_INVALID_DATA));
    NETW_CHECK_EQ(rx->read_routes(there).size(), 0);
}

} // namespace TestTableFrameWire
