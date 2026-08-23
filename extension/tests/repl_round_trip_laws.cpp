#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace TestNetwReplRoundTrip {

using godot::Array;
using godot::LocalVector;
using godot::PackedByteArray;
using godot::Ref;
using netw::NetwQuantizeBits;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::repl::read_row_frame;
using netw::repl::RowFrameHeader;
using netw::repl::RowOffer;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::repl::write_row_frame;
using netw::wire::ChannelDecl;
using netw::wire::CodeRow;
using netw::wire::decode_scalar_row;
using netw::wire::Delivery;
using netw::wire::WirePlan;
using netw::wire::WireRegistry;

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

Ref<SchemaRecord> mixed() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Mixed");
    SchemaCore::append_column(record, godot::StringName("x"), SchemaCore::F32, 1);
    SchemaCore::append_column(record, godot::StringName("y"), SchemaCore::F32, 1);
    SchemaCore::append_column(record, godot::StringName("n"), SchemaCore::I16, 1);
    for (int at = 0; at < 2; ++at) {
        Ref<NetwQuantizeBits> packer;
        packer.instantiate();
        packer->bits(16);
        packer->limits(-512.0, 512.0);
        record->at(at)->quantizer = packer;
    }
    SchemaCore::fix(record);
    return record;
}

Array values_of(double x, double y, int64_t n) {
    Array out;
    out.push_back(x);
    out.push_back(y);
    out.push_back(n);
    return out;
}

RowOffer offer(const Ref<SchemaRecord> &p_schema, const Array &p_values) {
    RowOffer out;
    out.route = 12;
    out.comp = 3;
    out.channel = CHANNEL;
    out.schema = p_schema;
    out.values = p_values;
    out.recipients.push_back(PEER);
    out.masked = true;
    return out;
}

Array receive(
    const Ref<SchemaRecord> &p_schema,
    const WirePlan &p_plan,
    const PackedByteArray &p_bytes,
    CodeRow &r_held
) {
    RowFrameHeader header;
    if (!read_row_frame(p_bytes, p_plan, header, r_held)) {
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
    const Ref<SchemaRecord> schema = mixed();
    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());

    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(schema, values_of(1.0 / 3.0, -7.77, 42)));

    const SessionResult sent = session.run(reg, offers, 100000, 1, 900);
    REQUIRE(sent.sends.size() == 1);

    RowFrameHeader header;
    header.route = sent.sends[0].route;
    header.comp = sent.sends[0].comp;
    header.channel = CHANNEL;
    header.tick = 41;
    header.mask = sent.sends[0].mask;
    const PackedByteArray bytes
        = write_row_frame(header, plan, sent.sends[0].row);
    REQUIRE(bytes.size() > 0);

    CodeRow held = CodeRow::for_plan(plan);
    const Array got = receive(schema, plan, bytes, held);
    REQUIRE(got.size() == 3);

    CodeRow canonical = CodeRow::for_plan(plan);
    REQUIRE(netw::wire::encode_scalar_row(
        schema,
        values_of(1.0 / 3.0, -7.77, 42),
        canonical
    ));
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
    const Ref<SchemaRecord> schema = mixed();
    const WirePlan plan = WirePlan::compile(schema);
    SessionSend session;

    LocalVector<RowOffer> first;
    first.push_back(offer(schema, values_of(1.0, 2.0, 3)));
    const SessionResult one = session.run(reg, first, 100000, 1, 900);
    REQUIRE(one.sends.size() == 1);

    RowFrameHeader head;
    head.route = 12;
    head.comp = 3;
    head.channel = CHANNEL;
    head.mask = one.sends[0].mask;
    CodeRow held = CodeRow::for_plan(plan);
    REQUIRE(receive(schema, plan, write_row_frame(head, plan, one.sends[0].row), held).size() == 3);

    session.acknowledge(PEER, 1);

    LocalVector<RowOffer> second;
    second.push_back(offer(schema, values_of(1.0, 2.0, 99)));
    const SessionResult two = session.run(reg, second, 100000, 2, 901);
    REQUIRE(two.sends.size() == 1);
    NETW_CHECK_EQ(two.sends[0].mask, uint64_t(0b100));

    head.mask = two.sends[0].mask;
    const Array got
        = receive(schema, plan, write_row_frame(head, plan, two.sends[0].row), held);
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
    const Ref<SchemaRecord> schema = mixed();
    SessionSend session;

    LocalVector<RowOffer> first;
    first.push_back(offer(schema, values_of(1.0, 2.0, 3)));
    REQUIRE(session.run(reg, first, 100000, 1, 900).sends.size() == 1);
    session.acknowledge(PEER, 1);

    LocalVector<RowOffer> same;
    same.push_back(offer(schema, values_of(1.0, 2.0, 3)));
    const SessionResult quiet = session.run(reg, same, 100000, 2, 901);

    NETW_CHECK_EQ(quiet.sends.size(), 0);
    NETW_CHECK_EQ(quiet.caught_up, 1);
}

} // namespace TestNetwReplRoundTrip
