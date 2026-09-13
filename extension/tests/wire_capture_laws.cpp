#include "support/netw_test.h"

#include "netw/carrier_frame.hpp"
#include "netw/wire/capture.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/spec.hpp"

namespace TestWireCapture {

using namespace godot;
using netw::wire::CaptureDirection;
using netw::wire::CaptureRecord;
using netw::wire::ReadStream;
using netw::wire::WriteStream;

PackedByteArray blob(std::initializer_list<uint8_t> p_bytes) {
    PackedByteArray out;
    for (const uint8_t byte : p_bytes) {
        out.push_back(byte);
    }
    return out;
}

CaptureRecord transcribed() {
    CaptureRecord record;
    record.dir = 1;
    record.peer = 2;
    record.wall_ms = 300;
    record.tick = 43;
    record.bytes = blob({0x57, 0x01});
    return record;
}

TEST_CASE(
    "[Networked][Wire][Hosted] C1 the capture record transcribed from WIRE.md "
    "9.15 packs to the bytes tools/wire_decode.py holds it to"
) {
    const PackedByteArray bytes
        = netw::wire::capture_record_pack(transcribed());

    REQUIRE(bytes.size() == 9);
    NETW_CHECK_EQ(bytes[0], 0x01);
    NETW_CHECK_EQ(bytes[1], 0x02);
    NETW_CHECK_EQ(bytes[2], 0xAC);
    NETW_CHECK_EQ(bytes[3], 0x02);
    NETW_CHECK_EQ(bytes[4], 0x2B);
    NETW_CHECK_EQ(bytes[5], 0x02);
    NETW_CHECK_EQ(bytes[6], 0x00);
    NETW_CHECK_EQ(bytes[7], 0x57);
    NETW_CHECK_EQ(bytes[8], 0x01);
}

TEST_CASE(
    "[Networked][Wire][Hosted] C2 a capture record measures to the length it "
    "writes, so a capture's size is known before it is spent"
) {
    CaptureRecord record = transcribed();
    netw::wire::MeasureStream measured;
    REQUIRE(CaptureRecord::wire.run(measured, record));

    WriteStream written;
    REQUIRE(CaptureRecord::wire.run(written, record));

    NETW_CHECK_EQ(measured.bit_length(), written.bit_length());
    NETW_CHECK_EQ(measured.byte_length(), int64_t(written.to_bytes().size()));
}

TEST_CASE(
    "[Networked][Wire][Hosted] C3 a capture record round trips every field it "
    "carried and leaves the stream on the next record"
) {
    WriteStream stream;
    CaptureRecord first = transcribed();
    CaptureRecord second = transcribed();
    second.dir = 0;
    second.peer = 5;
    second.bytes = blob({0x77, 0x09, 0x00, 0x00});
    REQUIRE(CaptureRecord::wire.run(stream, first));
    REQUIRE(CaptureRecord::wire.run(stream, second));

    ReadStream read(stream.to_bytes());
    CaptureRecord back;
    REQUIRE(netw::wire::capture_record_next(read, back));
    NETW_CHECK_EQ(int64_t(back.dir), int64_t(1));
    NETW_CHECK_EQ(int64_t(back.peer), int64_t(2));
    NETW_CHECK_EQ(int64_t(back.wall_ms), int64_t(300));
    NETW_CHECK_EQ(int64_t(back.tick), int64_t(43));
    NETW_CHECK_EQ(back.bytes.size(), 2);

    CaptureRecord next;
    REQUIRE(netw::wire::capture_record_next(read, next));
    NETW_CHECK_EQ(int64_t(next.dir), int64_t(0));
    NETW_CHECK_EQ(int64_t(next.peer), int64_t(5));
    NETW_CHECK_EQ(next.bytes.size(), 4);
    NETW_CHECK_EQ(read.bits_remaining(), int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][Hosted] C4 a capture record cut short inside its blob "
    "yields nothing, because a datagram read from fewer bytes than it claims "
    "decodes to a lie"
) {
    const PackedByteArray whole
        = netw::wire::capture_record_pack(transcribed());
    const PackedByteArray cut = whole.slice(0, whole.size() - 1);

    ReadStream read(cut);
    CaptureRecord back;
    const bool admitted = netw::wire::capture_record_next(read, back);
    const bool refused = !admitted;
    CHECK(refused);
}

TEST_CASE(
    "[Networked][Wire][Hosted] C7 the capture header carries the same record "
    "sheet the spec artifact does, so a capture decodes with no build beside "
    "it"
) {
    const Dictionary document = netw::wire::spec_document();
    const Dictionary records = document[StringName("records")];

    const bool holds_capture = records.has("CaptureRecord");
    const bool holds_frame = records.has("FrameHeader");
    const bool holds_reliable = records.has("DatagramReliable");
    const bool holds_unreliable = records.has("DatagramUnreliable");
    const bool holds_acked = records.has("DatagramAcked");
    CHECK(holds_capture);
    CHECK(holds_frame);
    CHECK(holds_reliable);
    CHECK(holds_unreliable);
    CHECK(holds_acked);
    NETW_CHECK_EQ(
        int64_t(document[StringName("format")]),
        int64_t(netw::wire::FORMAT_VERSION)
    );
}

} // namespace TestWireCapture
