#include "support/netw_test.h"

#include "netw/call_args.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"

namespace TestRpcFrameWire {

using namespace godot;
using netw::call_args::Slot;

PackedByteArray slots_bytes(const LocalVector<Slot> &p_slots) {
    netw::wire::WriteStream stream;
    REQUIRE(netw::call_args::write(stream, p_slots, Array(), Array()));
    REQUIRE(stream.align_verify());
    return stream.to_bytes();
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RW1 a token transcribed from WIRE.md 9.13 "
    "spends one bit and a byte for an ordinal and carries a name otherwise"
) {
    const PackedByteArray by_id = netw::script::model::token_bytes(int64_t(7));
    REQUIRE(by_id.size() == 2);
    NETW_CHECK_EQ(by_id[0], 0x0f);
    NETW_CHECK_EQ(by_id[1], 0x00);
    NETW_CHECK_EQ(int64_t(netw::script::model::token_of_bytes(by_id)), 7);

    const PackedByteArray by_name
        = netw::script::model::token_bytes(StringName("fire"));
    const Variant read = netw::script::model::token_of_bytes(by_name);
    const bool named = String(read) == String("fire");
    CHECK(named);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RW2 an argument row cut short yields no "
    "arguments at all, because a method invoked with the arguments that did "
    "parse and a zero for the rest is a call the game was never sent"
) {
    LocalVector<Slot> slots;
    slots.push_back(netw::call_args::of_value(int64_t(5)));
    slots.push_back(netw::call_args::of_value(true));
    const PackedByteArray whole = slots_bytes(slots);
    REQUIRE(whole.size() > 2);

    netw::wire::ReadStream reader(whole.slice(0, whole.size() - 2));
    LocalVector<Slot> read;
    const bool refused = !netw::call_args::read(reader, Array(), Array(), read);
    CHECK(refused);
    NETW_CHECK_EQ(int64_t(read.size()), int64_t(0));

    netw::wire::ReadStream sound(whole);
    LocalVector<Slot> admitted;
    const bool taken = netw::call_args::read(sound, Array(), Array(), admitted);
    REQUIRE(taken);
    NETW_CHECK_EQ(int64_t(admitted.size()), int64_t(2));
    NETW_CHECK_EQ(int64_t(admitted[0].value), int64_t(5));
    CHECK(bool(admitted[1].value));
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RW3 a node argument rides as an address and "
    "keeps its relative path, because an Object has no value encoding and a "
    "call that passed one by value would carry nothing the receiver can bind"
) {
    LocalVector<Slot> slots;
    slots.push_back(netw::call_args::of_node(11, 255, String("Body/Turret")));
    slots.push_back(netw::call_args::of_node(12, 3, String()));

    netw::wire::ReadStream reader(slots_bytes(slots));
    LocalVector<Slot> read;
    REQUIRE(netw::call_args::read(reader, Array(), Array(), read));
    REQUIRE(read.size() == 2);
    CHECK(read[0].addresses_node);
    NETW_CHECK_EQ(read[0].node.route, int64_t(11));
    NETW_CHECK_EQ(read[0].node.comp, int64_t(255));
    const bool pathed = read[0].node.path == String("Body/Turret");
    CHECK(pathed);
    NETW_CHECK_EQ(read[1].node.comp, int64_t(3));
    const bool unpathed = read[1].node.path.is_empty();
    CHECK(unpathed);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RW4 the action request transcribed from "
    "WIRE.md 9.13 packs to the bytes tools/wire_decode.py holds it to"
) {
    netw::session::ActionRequest body;
    body.method = StringName("fire");
    body.view_tick = 9;
    body.key = StringName("k");
    body.timing = 1;
    const PackedByteArray bytes = netw::session::frame_write(body);

    REQUIRE(bytes.size() == 13);
    NETW_CHECK_EQ(bytes[0], 0x04);
    NETW_CHECK_EQ(bytes[2], uint8_t('f'));
    NETW_CHECK_EQ(bytes[6], 0x12);
    NETW_CHECK_EQ(bytes[11], uint8_t('k'));
    NETW_CHECK_EQ(bytes[12], 0x01);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RW5 a raw float crosses at 32 bits and a raw "
    "int at 64, so a value row states what it spends rather than what the "
    "engine happens to hold"
) {
    LocalVector<Slot> floats;
    floats.push_back(netw::call_args::of_value(0.5));
    const PackedByteArray float_row = slots_bytes(floats);

    LocalVector<Slot> ints;
    ints.push_back(netw::call_args::of_value(int64_t(-1)));
    const PackedByteArray int_row = slots_bytes(ints);

    NETW_CHECK_EQ(int64_t(int_row.size() - float_row.size()), int64_t(4));

    netw::wire::ReadStream reader(int_row);
    LocalVector<Slot> read;
    REQUIRE(netw::call_args::read(reader, Array(), Array(), read));
    REQUIRE(read.size() == 1);
    NETW_CHECK_EQ(int64_t(read[0].value), int64_t(-1));
}

} // namespace TestRpcFrameWire
