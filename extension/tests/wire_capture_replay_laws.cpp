#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/wire/capture.hpp"
#include "netw/wire/frame.hpp"

namespace TestWireCaptureReplay {

using namespace godot;
using netw::NetwCarrierFrame;
using netw::SchemaCore;
using netw::repl::RowFrameHeader;
using netw::repl::RowRefusal;
using netw::table::SchemaRecord;
using netw::wire::CaptureDirection;
using netw::wire::CaptureRecord;
using netw::wire::CodeRow;
using netw::wire::ReadStream;
using netw::wire::WirePlan;
using netw::wire::WriteStream;

constexpr int64_t BASE_TICK = 41;
constexpr uint8_t ROW_CHANNEL = 39;

WirePlan triple() {
    SchemaRecord record;
    record.name = StringName("Triple");
    SchemaCore::append_column(&record, StringName("x"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("y"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("z"), SchemaCore::I16, 1);
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

PackedByteArray row_payload(const WirePlan &p_plan, uint64_t p_seed) {
    CodeRow row = CodeRow::for_plan(p_plan);
    row.write(p_plan.column(0), 0, p_seed);
    row.write(p_plan.column(1), 0, p_seed + 11);
    row.write(p_plan.column(2), 0, p_seed + 22);
    RowFrameHeader head;
    head.life = 0;
    head.reconcile_ack = -1;
    head.mask = p_plan.full_mask();
    return netw::repl::write_row_frame(head, BASE_TICK, p_plan, row);
}

PackedByteArray corpus_capture(const WirePlan &p_plan) {
    WriteStream stream;
    for (int at = 0; at < 6; ++at) {
        PackedByteArray framed = netw::wire::frame_pack(
            7 + at,
            0,
            ROW_CHANNEL,
            row_payload(p_plan, uint64_t(100 * at)),
            String()
        );
        if (at % 2 == 0) {
            framed.append_array(
                netw::wire::frame_pack(
                    90 + at,
                    0,
                    ROW_CHANNEL,
                    row_payload(p_plan, uint64_t(7 * at)),
                    String()
                )
            );
        }
        CaptureRecord record;
        record.dir = uint64_t(
            at % 3 == 0 ? CaptureDirection::OUT : CaptureDirection::IN
        );
        record.peer = 1;
        record.wall_ms = uint64_t(at) * 16;
        record.tick = uint64_t(BASE_TICK) + 1;
        record.bytes = NetwCarrierFrame::build(
            framed,
            at % 2 == 0,
            at,
            at - 1,
            0xA5A5A5A5,
            BASE_TICK
        );
        CaptureRecord staged = record;
        if (!CaptureRecord::wire.run(stream, staged)) {
            return PackedByteArray();
        }
    }
    return stream.to_bytes();
}

struct Verdicts {
    int64_t records = 0;
    int64_t foreign = 0;
    int64_t malformed = 0;
    int64_t walked = 0;
    int64_t stopped = 0;
    int64_t frames = 0;
    int64_t rows_applied = 0;
    int64_t rows_refused = 0;
    int64_t frames_over_bytes = 0;

    int64_t accounted() const {
        return foreign + malformed + walked + stopped;
    }
};

void replay_datagram(
    const PackedByteArray &p_datagram,
    const WirePlan &p_plan,
    Verdicts &r_out
) {
    const NetwCarrierFrame head = NetwCarrierFrame::read(p_datagram);
    if (head.kind == NetwCarrierFrame::FOREIGN) {
        r_out.foreign += 1;
        return;
    }
    if (head.kind == NetwCarrierFrame::MALFORMED) {
        r_out.malformed += 1;
        return;
    }
    const netw::wire::FrameWalk walk
        = netw::wire::frame_unpack_all(p_datagram, head.payload_offset);
    if (walk.frames.size() > uint32_t(p_datagram.size())) {
        r_out.frames_over_bytes += 1;
    }
    r_out.frames += int64_t(walk.frames.size());
    for (uint32_t at = 0; at < walk.frames.size(); ++at) {
        const netw::wire::Frame &frame = walk.frames[at];
        if (frame.channel != ROW_CHANNEL) {
            continue;
        }
        RowFrameHeader read_head;
        CodeRow held = CodeRow::for_plan(p_plan);
        RowRefusal refusal = RowRefusal::NONE;
        const bool admitted = netw::repl::read_row_frame(
            frame.payload,
            BASE_TICK,
            -1,
            p_plan,
            read_head,
            held,
            netw::repl::RowBaselineSource(),
            &refusal
        );
        if (admitted) {
            r_out.rows_applied += 1;
        } else {
            r_out.rows_refused += 1;
        }
    }
    if (walk.whole) {
        r_out.walked += 1;
    } else {
        r_out.stopped += 1;
    }
}

Verdicts replay(const PackedByteArray &p_capture, const WirePlan &p_plan) {
    Verdicts out;
    ReadStream stream(p_capture);
    CaptureRecord record;
    while (stream.bits_remaining() >= 8) {
        if (!netw::wire::capture_record_next(stream, record)) {
            break;
        }
        out.records += 1;
        replay_datagram(record.bytes, p_plan, out);
    }
    return out;
}

PackedByteArray flip_bit(const PackedByteArray &p_bytes, int64_t p_bit) {
    PackedByteArray out = p_bytes;
    out.set(int(p_bit / 8), uint8_t(out[int(p_bit / 8)] ^ (1 << (p_bit % 8))));
    return out;
}

PackedByteArray swap_pair(const PackedByteArray &p_bytes, int64_t p_at) {
    PackedByteArray out = p_bytes;
    const uint8_t held = out[int(p_at)];
    out.set(int(p_at), out[int(p_at) + 1]);
    out.set(int(p_at) + 1, held);
    return out;
}

TEST_CASE(
    "[Networked][Wire][Hosted] R1 a capture replays every datagram it "
    "recorded, and the verdicts the replay reaches are the ones the corpus "
    "was written with"
) {
    const WirePlan plan = triple();
    const PackedByteArray capture = corpus_capture(plan);
    REQUIRE(capture.size() > 0);

    const Verdicts seen = replay(capture, plan);
    NETW_CHECK_EQ(seen.records, int64_t(6));
    NETW_CHECK_EQ(seen.foreign, int64_t(0));
    NETW_CHECK_EQ(seen.malformed, int64_t(0));
    NETW_CHECK_EQ(seen.stopped, int64_t(0));
    NETW_CHECK_EQ(seen.walked, int64_t(6));
    NETW_CHECK_EQ(seen.frames, int64_t(9));
    NETW_CHECK_EQ(seen.rows_applied, int64_t(9));
    NETW_CHECK_EQ(seen.rows_refused, int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][Hosted] R2 every bit of a captured datagram flipped in "
    "turn reaches a counted verdict, because a mutation the replay cannot "
    "account for is a decoder reading bytes nobody wrote"
) {
    const WirePlan plan = triple();
    const PackedByteArray capture = corpus_capture(plan);
    ReadStream stream(capture);
    CaptureRecord record;

    Verdicts seen;
    int64_t mutations = 0;
    while (stream.bits_remaining() >= 8
           && netw::wire::capture_record_next(stream, record)) {
        const int64_t bits = int64_t(record.bytes.size()) * 8;
        for (int64_t bit = 0; bit < bits; ++bit) {
            mutations += 1;
            seen.records += 1;
            replay_datagram(flip_bit(record.bytes, bit), plan, seen);
        }
    }

    const bool every_bit_of_every_record = mutations > 1000;
    CHECK(every_bit_of_every_record);
    NETW_CHECK_EQ(seen.records, mutations);
    NETW_CHECK_EQ(seen.accounted(), mutations);
    NETW_CHECK_EQ(seen.frames_over_bytes, int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][Hosted] R3 a captured datagram truncated at every "
    "length reaches a counted verdict and yields no frame the bytes cannot "
    "hold"
) {
    const WirePlan plan = triple();
    const PackedByteArray capture = corpus_capture(plan);
    ReadStream stream(capture);
    CaptureRecord first;
    REQUIRE(netw::wire::capture_record_next(stream, first));

    Verdicts seen;
    for (int64_t length = 0; length <= int64_t(first.bytes.size()); ++length) {
        seen.records += 1;
        replay_datagram(first.bytes.slice(0, int(length)), plan, seen);
    }

    NETW_CHECK_EQ(seen.accounted(), seen.records);
    NETW_CHECK_EQ(seen.frames_over_bytes, int64_t(0));
    const bool some_stopped = seen.stopped + seen.malformed + seen.foreign > 0;
    CHECK(some_stopped);
}

TEST_CASE(
    "[Networked][Wire][Hosted] R4 a captured datagram with any two adjacent "
    "bytes swapped reaches a counted verdict, which is the reorder a link "
    "does to a run of bytes rather than to a datagram"
) {
    const WirePlan plan = triple();
    const PackedByteArray capture = corpus_capture(plan);
    ReadStream stream(capture);
    CaptureRecord first;
    REQUIRE(netw::wire::capture_record_next(stream, first));
    REQUIRE(first.bytes.size() >= 2);

    Verdicts seen;
    for (int64_t at = 0; at + 1 < int64_t(first.bytes.size()); ++at) {
        seen.records += 1;
        replay_datagram(swap_pair(first.bytes, at), plan, seen);
    }

    NETW_CHECK_EQ(seen.accounted(), seen.records);
    NETW_CHECK_EQ(seen.frames_over_bytes, int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][Hosted] R5 a captured datagram replayed twice and a "
    "capture whose records are duplicated reach the same verdicts twice "
    "over, because a replay holds no state a duplicate can move"
) {
    const WirePlan plan = triple();
    const PackedByteArray capture = corpus_capture(plan);
    const Verdicts once = replay(capture, plan);

    PackedByteArray twice = capture;
    twice.append_array(capture);
    const Verdicts doubled = replay(twice, plan);

    NETW_CHECK_EQ(doubled.records, once.records * 2);
    NETW_CHECK_EQ(doubled.frames, once.frames * 2);
    NETW_CHECK_EQ(doubled.rows_applied, once.rows_applied * 2);
    NETW_CHECK_EQ(doubled.accounted(), once.accounted() * 2);
}

TEST_CASE(
    "[Networked][Wire][Hosted] R6 a capture whose record stream is cut short "
    "replays the records it holds whole and none of the one it does not"
) {
    const WirePlan plan = triple();
    const PackedByteArray capture = corpus_capture(plan);
    const Verdicts whole = replay(capture, plan);

    const Verdicts cut = replay(capture.slice(0, capture.size() - 4), plan);
    const bool fewer_records = cut.records < whole.records;
    const bool accounted = cut.accounted() == cut.records;
    CHECK(fewer_records);
    CHECK(accounted);
    NETW_CHECK_EQ(cut.frames_over_bytes, int64_t(0));
}

} // namespace TestWireCaptureReplay
