#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;

namespace TestNetwReplRowFrame {

using godot::PackedByteArray;
using godot::Ref;
using netw::SchemaCore;
using netw::repl::read_row_frame;
using netw::repl::read_window_frame;
using netw::repl::RowFrameHeader;
using netw::repl::WindowSample;
using netw::repl::write_row_frame;
using netw::repl::write_window_frame;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

constexpr int64_t BASE_TICK = 41;
constexpr int64_t ANY_LIFE = -1;

WirePlan triple() {
    SchemaRecord record;
    record.name = godot::StringName("Triple");
    SchemaCore::append_column(
        &record,
        godot::StringName("x"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("y"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("z"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

CodeRow filled(const WirePlan &p_plan, uint64_t a, uint64_t b, uint64_t c) {
    CodeRow row = CodeRow::for_plan(p_plan);
    row.write(p_plan.column(0), 0, a);
    row.write(p_plan.column(1), 0, b);
    row.write(p_plan.column(2), 0, c);
    return row;
}

RowFrameHeader header(uint64_t p_mask) {
    RowFrameHeader out;
    out.life = 0;
    out.reconcile_ack = -1;
    out.mask = p_mask;
    return out;
}

int64_t framed_size(const PackedByteArray &p_payload) {
    return netw::wire::frame_pack(7, 0, 39, p_payload, String()).size();
}

TEST_CASE("[Networked][Repl][Hosted] a frame round trips its header") {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    RowFrameHeader sent = header(plan.full_mask());
    sent.life = 5;
    sent.reconcile_ack = 38;
    const PackedByteArray bytes = write_row_frame(sent, BASE_TICK, plan, row);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(bytes, BASE_TICK, 5, plan, got, into));

    NETW_CHECK_EQ(got.life, 5);
    NETW_CHECK_EQ(got.reconcile_ack, 38);
    NETW_CHECK_EQ(got.mask, plan.full_mask());
    CHECK(into.equals(row));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a reconcile ack is an age below the datagram's "
    "tick, so the same ack costs the same bits at any tick"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    RowFrameHeader early = header(plan.full_mask());
    early.reconcile_ack = 38;
    RowFrameHeader late = header(plan.full_mask());
    late.reconcile_ack = 100038;

    const int64_t near_zero = write_row_frame(early, 41, plan, row).size();
    const int64_t far_out = write_row_frame(late, 100041, plan, row).size();
    NETW_CHECK_EQ(near_zero, far_out);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(
        write_row_frame(late, 100041, plan, row),
        100041,
        ANY_LIFE,
        plan,
        got,
        into
    ));
    NETW_CHECK_EQ(got.reconcile_ack, 100038);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a frame with no reconcile ack spends one bit "
    "on saying so"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    RowFrameHeader acked = header(plan.full_mask());
    acked.reconcile_ack = 40;

    const int64_t without
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row)
              .size();
    const int64_t with = write_row_frame(acked, BASE_TICK, plan, row).size();
    CHECK(without < with);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(
        write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row),
        BASE_TICK,
        ANY_LIFE,
        plan,
        got,
        into
    ));
    NETW_CHECK_EQ(got.reconcile_ack, -1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a life N row after revival is refused, because "
    "an unreliable row gathered against a dead copy can land behind the "
    "reliable verb that opened the next one"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    RowFrameHeader stale = header(plan.full_mask());
    stale.life = 2;
    const PackedByteArray bytes = write_row_frame(stale, BASE_TICK, plan, row);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    const bool refused = !read_row_frame(bytes, BASE_TICK, 3, plan, got, into);
    CHECK(refused);
    CHECK(into.equals(CodeRow::for_plan(plan)));

    CodeRow again = CodeRow::for_plan(plan);
    const bool admitted = read_row_frame(bytes, BASE_TICK, 2, plan, got, again);
    CHECK(admitted);
}

TEST_CASE(
    "[Networked][Repl][Hosted] life wraps at sixteen, so a route living past "
    "the wrap still fences on the four bits it has"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    RowFrameHeader wrapped = header(plan.full_mask());
    wrapped.life = 1;
    const PackedByteArray bytes
        = write_row_frame(wrapped, BASE_TICK, plan, row);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    const bool admitted = read_row_frame(bytes, BASE_TICK, 17, plan, got, into);
    CHECK(admitted);

    CodeRow other = CodeRow::for_plan(plan);
    const bool refused
        = !read_row_frame(bytes, BASE_TICK, 18, plan, got, other);
    CHECK(refused);
}

TEST_CASE(
    "[Networked][Repl][Hosted] only the masked columns ride, and the rest are "
    "left as the receiver had them"
) {
    const WirePlan plan = triple();
    const CodeRow fresh = filled(plan, 11, 22, 33);

    const PackedByteArray bytes
        = write_row_frame(header(0b010), BASE_TICK, plan, fresh);
    REQUIRE(bytes.size() > 0);

    CodeRow held = filled(plan, 7, 8, 9);
    RowFrameHeader got;
    REQUIRE(read_row_frame(bytes, BASE_TICK, ANY_LIFE, plan, got, held));

    NETW_CHECK_EQ(held.read(plan.column(0), 0), 7);
    NETW_CHECK_EQ(held.read(plan.column(1), 0), 22);
    NETW_CHECK_EQ(held.read(plan.column(2), 0), 9);
}

TEST_CASE("[Networked][Repl][Hosted] a narrower mask is a shorter frame") {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);

    const int64_t whole
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row)
              .size();
    const int64_t one
        = write_row_frame(header(0b001), BASE_TICK, plan, row).size();

    CHECK(one < whole);
}

TEST_CASE("[Networked][Repl][Hosted] an empty mask is no frame at all") {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);

    NETW_CHECK_EQ(write_row_frame(header(0), BASE_TICK, plan, row).size(), 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(
        read_row_frame(PackedByteArray(), BASE_TICK, ANY_LIFE, plan, got, into)
    );
}

TEST_CASE(
    "[Networked][Repl][Hosted] a mask naming a column the plan lacks is refused"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);

    NETW_CHECK_EQ(
        write_row_frame(header(0b1000), BASE_TICK, plan, row).size(),
        0
    );
}

TEST_CASE("[Networked][Repl][Hosted] a frame with residue is refused whole") {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    PackedByteArray bytes
        = write_row_frame(header(0b011), BASE_TICK, plan, row);
    REQUIRE(bytes.size() > 0);
    bytes.push_back(0xAB);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(read_row_frame(bytes, BASE_TICK, ANY_LIFE, plan, got, into));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a truncated frame is refused rather than read "
    "short"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    const PackedByteArray whole
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row);
    REQUIRE(whole.size() > 2);

    PackedByteArray cut;
    for (int64_t at = 0; at < whole.size() - 1; ++at) {
        cut.push_back(whole[at]);
    }

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(read_row_frame(cut, BASE_TICK, ANY_LIFE, plan, got, into));

    CHECK(into.equals(CodeRow::for_plan(plan)));
}

WirePlan eight_bools() {
    SchemaRecord record;
    record.name = godot::StringName("Eight");
    for (int at = 0; at < 8; ++at) {
        SchemaCore::append_column(
            &record,
            godot::StringName(String("f") + String::num_int64(at)),
            SchemaCore::BOOL,
            1
        );
    }
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

TEST_CASE(
    "[Networked][Repl][Hosted][D20] a row frame at route under 128 with at "
    "most eight columns costs six bytes of framing, envelope included, "
    "against the twelve v9 spent"
) {
    const WirePlan plan = eight_bools();
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(int64_t(plan.column_count()), 8);

    CodeRow row = CodeRow::for_plan(plan);
    const PackedByteArray bytes
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row);
    REQUIRE(bytes.size() > 0);

    int64_t column_bits = 0;
    for (uint32_t at = 0; at < plan.column_count(); ++at) {
        column_bits
            += int64_t(plan.column(at).stride) * int64_t(plan.column(at).width);
    }
    NETW_CHECK_EQ(column_bits, 8);

    const int64_t framing = framed_size(bytes) - (column_bits / 8);
    NETW_CHECK_EQ(framing, 6);
}

TEST_CASE(
    "[Networked][Repl][Hosted][D20] the small row, one I16 beside one BOOL, "
    "is eight bytes on the wire, exactly its budget, against the engine's "
    "eleven"
) {
    SchemaRecord record;
    record.name = godot::StringName("Small");
    SchemaCore::append_column(
        &record,
        godot::StringName("health"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("alive"),
        SchemaCore::BOOL,
        1
    );
    SchemaCore::fix(&record);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());

    CodeRow row = CodeRow::for_plan(plan);
    row.write(plan.column(0), 0, 4242);
    row.write(plan.column(1), 0, 1);

    const PackedByteArray bytes
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row);
    REQUIRE(bytes.size() > 0);
    NETW_CHECK_EQ(framed_size(bytes), 8);
}

TEST_CASE(
    "[Networked][Repl][Hosted][D20] the transform row, a quantized pose with "
    "a rotation and a velocity, is twenty two bytes before the ladder against "
    "the engine's sixty"
) {
    SchemaRecord record;
    record.name = godot::StringName("Pose");
    SchemaCore::append_column(
        &record,
        godot::StringName("position"),
        SchemaCore::VECTOR3,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("rotation"),
        SchemaCore::QUATERNION,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("velocity"),
        SchemaCore::VECTOR3,
        1
    );

    Ref<netw::NetwQuantizeScalar> metres;
    metres.instantiate();
    metres->set_bit_count(16);
    metres->set_min_limit(-512.0);
    metres->set_max_limit(512.0);
    Ref<netw::NetwQuantizeQuaternion> facing;
    facing.instantiate();
    facing->set_bit_count(10);
    SchemaCore::assign_quantizer(&record, 0, metres);
    SchemaCore::assign_quantizer(&record, 1, facing);
    SchemaCore::assign_quantizer(&record, 2, metres);
    SchemaCore::fix(&record);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());

    int64_t column_bits = 0;
    for (uint32_t at = 0; at < plan.column_count(); ++at) {
        column_bits
            += int64_t(plan.column(at).stride) * int64_t(plan.column(at).width);
    }
    NETW_CHECK_EQ(column_bits, 128);

    CodeRow row = CodeRow::for_plan(plan);
    const PackedByteArray bytes
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row);
    REQUIRE(bytes.size() > 0);
    NETW_CHECK_EQ(framed_size(bytes), 22);

    record.at(0)->delta = netw::table::DeltaMode::LADDER;
    record.at(2)->delta = netw::table::DeltaMode::LADDER;
    const WirePlan stepping = WirePlan::compile(record);
    REQUIRE(stepping.valid());

    CodeRow base = CodeRow::for_plan(stepping);
    CodeRow moved = CodeRow::for_plan(stepping);
    for (int element = 0; element < 3; ++element) {
        base.write(stepping.column(0), element, 32768);
        base.write(stepping.column(2), element, 32768);
        moved.write(stepping.column(0), element, 32771);
        moved.write(stepping.column(2), element, 32765);
    }
    netw::repl::RowBaseline naming;
    naming.row = &base;
    naming.low = 12;
    const PackedByteArray stepped = write_row_frame(
        header(stepping.full_mask()),
        BASE_TICK,
        stepping,
        moved,
        naming
    );
    REQUIRE(stepped.size() > 0);
    NETW_CHECK_EQ(framed_size(stepped), 15);
}

TEST_CASE(
    "[Networked][Repl][Hosted][Conformance] the hand-built masked row "
    "transcribed from WIRE.md 9.4 is the row the writer emits, byte for byte"
) {
    const WirePlan plan = triple();
    RowFrameHeader sent;
    sent.life = 5;
    sent.mask = 0b111;
    sent.reconcile_ack = BASE_TICK - 3;
    sent.tick = BASE_TICK + 1;
    const CodeRow row = filled(plan, 11, 22, 33);

    PackedByteArray transcribed;
    transcribed.push_back(0xF5);
    transcribed.push_back(0x01);
    transcribed.push_back(0x05);
    transcribed.push_back(0x02);
    transcribed.push_back(0x0B);
    transcribed.push_back(0x00);
    transcribed.push_back(0x16);
    transcribed.push_back(0x00);
    transcribed.push_back(0x21);
    transcribed.push_back(0x00);

    const PackedByteArray written = write_row_frame(sent, BASE_TICK, plan, row);
    const bool same = written == transcribed;
    CHECK(same);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(transcribed, BASE_TICK, 5, plan, got, into));
    NETW_CHECK_EQ(got.life, 5);
    NETW_CHECK_EQ(got.mask, uint64_t(0b111));
    NETW_CHECK_EQ(got.reconcile_ack, BASE_TICK - 3);
    NETW_CHECK_EQ(got.tick, BASE_TICK + 1);
    NETW_CHECK_EQ(into.read(plan.column(0), 0), 11);
    NETW_CHECK_EQ(into.read(plan.column(1), 0), 22);
    NETW_CHECK_EQ(into.read(plan.column(2), 0), 33);
}

godot::LocalVector<WindowSample> window(
    const WirePlan &p_plan,
    int64_t p_oldest,
    uint32_t p_count
) {
    godot::LocalVector<WindowSample> out;
    for (uint32_t at = 0; at < p_count; ++at) {
        WindowSample sample;
        sample.tick = p_oldest + int64_t(at);
        sample.row = filled(p_plan, 10 + at, 20 + at, 30 + at);
        out.push_back(sample);
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame round trips every tick it "
    "repeats, and the ticks come back absolute against the datagram's"
) {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> samples = window(plan, 39, 3);
    const PackedByteArray bytes
        = write_window_frame(header(0), BASE_TICK, plan, samples);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;
    REQUIRE(read_window_frame(bytes, BASE_TICK, ANY_LIFE, plan, got, read));

    NETW_CHECK_EQ(got.mask, plan.full_mask());
    REQUIRE(read.size() == 3);
    for (uint32_t at = 0; at < read.size(); ++at) {
        NETW_CHECK_EQ(read[at].tick, samples[at].tick);
        CHECK(read[at].row.equals(samples[at].row));
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame costs one row per tick it "
    "carries"
) {
    const WirePlan plan = triple();
    const PackedByteArray one
        = write_window_frame(header(0), BASE_TICK, plan, window(plan, 41, 1));
    const PackedByteArray three
        = write_window_frame(header(0), BASE_TICK, plan, window(plan, 39, 3));
    REQUIRE(one.size() > 0);
    CHECK(three.size() > one.size());
}

TEST_CASE("[Networked][Repl][Hosted] an empty window is no frame at all") {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> none;
    CHECK(write_window_frame(header(0), BASE_TICK, plan, none).is_empty());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window authored ahead of the datagram keeps "
    "its own ticks, because a stamped lane writes for the tick that applies "
    "the value rather than for the tick that carries it"
) {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> ahead
        = window(plan, BASE_TICK + 1, 2);
    const PackedByteArray bytes
        = write_window_frame(header(0), BASE_TICK, plan, ahead);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;
    REQUIRE(read_window_frame(bytes, BASE_TICK, ANY_LIFE, plan, got, read));

    NETW_CHECK_EQ(got.tick, BASE_TICK + 2);
    REQUIRE(read.size() == 2);
    for (uint32_t at = 0; at < read.size(); ++at) {
        NETW_CHECK_EQ(read[at].tick, ahead[at].tick);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame on a datagram with no tick is "
    "no frame at all, because every sample it holds is an age below one"
) {
    const WirePlan plan = triple();
    CHECK(
        write_window_frame(header(0), -1, plan, window(plan, 39, 3)).is_empty()
    );
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame with residue or a short tail is "
    "refused whole"
) {
    const WirePlan plan = triple();
    const PackedByteArray bytes
        = write_window_frame(header(0), BASE_TICK, plan, window(plan, 39, 3));
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;

    PackedByteArray longer = bytes;
    longer.push_back(0);
    CHECK_FALSE(
        read_window_frame(longer, BASE_TICK, ANY_LIFE, plan, got, read)
    );

    PackedByteArray shorter = bytes;
    shorter.resize(bytes.size() - 1);
    CHECK_FALSE(
        read_window_frame(shorter, BASE_TICK, ANY_LIFE, plan, got, read)
    );
    NETW_CHECK_EQ(read.size(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an unquantized composite column rides as a "
    "frame, because three strided elements reach a stream one wide run cannot"
) {
    SchemaRecord record;
    record.name = godot::StringName("Placed");
    SchemaCore::append_column(
        &record,
        godot::StringName("pos"),
        SchemaCore::VECTOR3,
        1
    );
    SchemaCore::fix(&record);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());

    CodeRow row = CodeRow::for_plan(plan);
    REQUIRE(row.write_bits(0, 32, 0x3f800000ULL));
    REQUIRE(row.write_bits(32, 32, 0x40000000ULL));
    REQUIRE(row.write_bits(64, 32, 0x40400000ULL));

    const PackedByteArray bytes
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(bytes, BASE_TICK, ANY_LIFE, plan, got, into));
    CHECK(into.equals(row));
}

} // namespace TestNetwReplRowFrame
