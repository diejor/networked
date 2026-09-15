#include "support/netw_test.h"
#include "support/send_drive.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace TestNetwReplRoundTrip {

using godot::Array;
using godot::LocalVector;
using godot::PackedByteArray;
using godot::Ref;
using netw::NetwQuantizeScalar;
using netw::SchemaCore;
using netw::repl::read_snapshot_row;
using netw::repl::RowOffer;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::repl::SnapshotHeader;
using netw::table::SchemaRecord;
using netw::wire::ChannelDecl;
using netw::wire::CodeRow;
using netw::wire::decode_scalar_row;
using netw::wire::Delivery;
using netw::wire::WirePlan;
using netw::wire::WireRegistry;
using netw_test::drive_send;

const uint8_t CHANNEL = 45;
const int PEER = 7;

WireRegistry registry() {
    WireRegistry out;
    ChannelDecl decl;
    decl.id = CHANNEL;
    decl.name = godot::StringName("round_trip");
    decl.delivery = Delivery::FITTED;
    out.register_channel(decl);
    return out;
}

SchemaRecord mixed() {
    SchemaRecord record;
    record.name = godot::StringName("Mixed");
    SchemaCore::append_column(
        &record,
        godot::StringName("x"),
        SchemaCore::F32,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("y"),
        SchemaCore::F32,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("n"),
        SchemaCore::I16,
        1
    );
    for (int at = 0; at < 2; ++at) {
        Ref<NetwQuantizeScalar> packer;
        packer.instantiate();
        packer->bits(16);
        packer->limits(-512.0, 512.0);
        record.at(at)->quantizer = packer;
    }
    SchemaCore::fix(&record);
    return record;
}

Array values_of(double x, double y, int64_t n) {
    Array out;
    out.push_back(x);
    out.push_back(y);
    out.push_back(n);
    return out;
}

RowOffer offer(const SchemaRecord &p_schema, const Array &p_values) {
    RowOffer out;
    out.route = 12;
    out.comp = 3;
    out.channel = CHANNEL;
    out.schema = &p_schema;
    out.values = p_values;
    out.recipients.push_back(PEER);
    out.masked = true;
    return out;
}

Array receive(
    const SchemaRecord &p_schema,
    const WirePlan &p_plan,
    const PackedByteArray &p_bytes,
    const CodeRow *p_baseline,
    CodeRow &r_held
) {
    SnapshotHeader header;
    if (!read_snapshot_row(
            p_bytes,
            41,
            p_plan,
            header,
            r_held,
            p_baseline,
            nullptr
        )) {
        return Array();
    }
    Array out;
    if (!decode_scalar_row(p_schema, r_held, out)) {
        return Array();
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a value handed to the send side arrives as the "
    "value the grid makes of it"
) {
    const WireRegistry reg = registry();
    const SchemaRecord schema = mixed();
    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());

    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(schema, values_of(1.0 / 3.0, -7.77, 42)));

    const SessionResult sent = drive_send(session, reg, offers, 100000, 1);
    REQUIRE(sent.sends.size() == 1);

    const PackedByteArray bytes = sent.sends[0].bytes;
    REQUIRE(bytes.size() > 0);

    CodeRow held = CodeRow::for_plan(plan);
    const Array got = receive(schema, plan, bytes, nullptr, held);
    REQUIRE(got.size() == 3);

    CodeRow canonical = CodeRow::for_plan(plan);
    REQUIRE(
        netw::wire::encode_scalar_row(
            schema,
            values_of(1.0 / 3.0, -7.77, 42),
            canonical
        )
    );
    Array expected;
    REQUIRE(decode_scalar_row(schema, canonical, expected));

    for (int at = 0; at < 3; ++at) {
        CHECK(godot::Variant(got[at]) == godot::Variant(expected[at]));
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a second pass carries only what moved, and the "
    "receiver still holds the whole row"
) {
    const WireRegistry reg = registry();
    const SchemaRecord schema = mixed();
    const WirePlan plan = WirePlan::compile(schema);
    SessionSend session;

    LocalVector<RowOffer> first;
    first.push_back(offer(schema, values_of(1.0, 2.0, 3)));
    const SessionResult one = drive_send(session, reg, first, 100000, 1);
    REQUIRE(one.sends.size() == 1);

    CodeRow held = CodeRow::for_plan(plan);
    REQUIRE(
        receive(schema, plan, one.sends[0].bytes, nullptr, held).size() == 3
    );

    const CodeRow confirmed = one.sends[0].row;
    netw_test::accept_streams(session, first, PEER);

    LocalVector<RowOffer> second;
    second.push_back(offer(schema, values_of(1.0, 2.0, 99)));
    const SessionResult two = drive_send(session, reg, second, 100000, 2);
    REQUIRE(two.sends.size() == 1);
    NETW_CHECK_EQ(two.sends[0].mask, uint64_t(0b100));

    const Array got
        = receive(schema, plan, two.sends[0].bytes, &confirmed, held);
    REQUIRE(got.size() == 3);

    NETW_CHECK_EQ(int64_t(got[2]), 99);
    CHECK(double(got[0]) != 0.0);
    CHECK(double(got[1]) != 0.0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a caught-up peer produces no frame, and the "
    "receiver keeps what it had"
) {
    const WireRegistry reg = registry();
    const SchemaRecord schema = mixed();
    SessionSend session;

    LocalVector<RowOffer> first;
    first.push_back(offer(schema, values_of(1.0, 2.0, 3)));
    REQUIRE(drive_send(session, reg, first, 100000, 1).sends.size() == 1);
    netw_test::accept_streams(session, first, PEER);

    LocalVector<RowOffer> same;
    same.push_back(offer(schema, values_of(1.0, 2.0, 3)));
    const SessionResult quiet = drive_send(session, reg, same, 100000, 2);

    NETW_CHECK_EQ(quiet.sends.size(), 0);
    NETW_CHECK_EQ(quiet.caught_up, 1);
}

} // namespace TestNetwReplRoundTrip
