// The descriptor layer, against the three things it claims over a hand-written
// serialize body: the same list drives all three modes, it stages a member of
// any width through the stream's int64 vocabulary, and it can be asked what it
// describes.
//
// The frame here is the smallest real one the carrier has, which is what makes
// it a fair measure of what a description costs.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

namespace TestNetwWireDescribe {

using godot::Array;
using godot::Dictionary;
using godot::PackedByteArray;
using netw::wire::bits;
using netw::wire::bytes_capped;
using netw::wire::describe;
using netw::wire::field;
using netw::wire::int_range;
using netw::wire::MeasureStream;
using netw::wire::ReadStream;
using netw::wire::WriteStream;

struct ClockPing {
    uint32_t probe_id = 0;
    uint64_t client_time_usec = 0;

    static constexpr auto wire = describe(
        field<&ClockPing::probe_id>("probe_id", int_range(0, 4095)),
        field<&ClockPing::client_time_usec>("client_time_usec", bits(48))
    );
};

struct Mixed {
    bool flag = false;
    int16_t narrow = 0;
    PackedByteArray blob;

    static constexpr auto wire = describe(
        field<&Mixed::flag>("flag", netw::wire::bool1()),
        field<&Mixed::narrow>("narrow", int_range(-100, 100)),
        field<&Mixed::blob>("blob", bytes_capped(32))
    );
};

TEST_CASE("[Networked][Wire][Hosted] a description drives all three modes") {
    ClockPing sent;
    sent.probe_id = 4095;
    sent.client_time_usec = 0xFFFFFFFFFFULL;

    WriteStream writer;
    REQUIRE(ClockPing::wire.run(writer, sent));
    NETW_CHECK_EQ(writer.bit_length(), 60);

    MeasureStream measurer;
    ClockPing priced = sent;
    REQUIRE(ClockPing::wire.run(measurer, priced));
    NETW_CHECK_EQ(measurer.bit_length(), writer.bit_length());

    ReadStream reader(writer.to_bytes());
    ClockPing received;
    REQUIRE(ClockPing::wire.run(reader, received));
    CHECK(reader.ok());
    NETW_CHECK_EQ(received.probe_id, sent.probe_id);
    NETW_CHECK_EQ(received.client_time_usec, sent.client_time_usec);
}

TEST_CASE("[Networked][Wire][Hosted] a description stages every member width") {
    Mixed sent;
    sent.flag = true;
    sent.narrow = -100;
    sent.blob.push_back(0x11);
    sent.blob.push_back(0x22);

    WriteStream writer;
    REQUIRE(Mixed::wire.run(writer, sent));

    ReadStream reader(writer.to_bytes());
    Mixed received;
    REQUIRE(Mixed::wire.run(reader, received));
    CHECK(reader.ok());
    CHECK(received.flag);
    NETW_CHECK_EQ(received.narrow, -100);
    NETW_CHECK_EQ(received.blob.size(), 2);
    NETW_CHECK_EQ(received.blob[0], 0x11);
    NETW_CHECK_EQ(received.blob[1], 0x22);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a description stops at its first failure"
) {
    // Four bits is enough for the probe id's first nibble and nothing after it,
    // so the second field is what runs out and the first must keep its value.
    WriteStream writer;
    uint64_t nibble = 0;
    REQUIRE(writer.bits(nibble, 4));

    ReadStream reader(writer.to_bytes());
    ClockPing received;
    received.client_time_usec = 7;
    CHECK_FALSE(ClockPing::wire.run(reader, received));
    CHECK_FALSE(reader.ok());
    NETW_CHECK_EQ(received.client_time_usec, 7);
}

TEST_CASE("[Networked][Wire][Hosted] a description says what it describes") {
    const Array dumped = ClockPing::wire.spec_dump();
    REQUIRE(dumped.size() == 2);

    const Dictionary probe = dumped[0];
    NETW_FORMAT_TEXT(
        probe_name,
        godot::String(probe["name"]).utf8().get_data()
    );
    CAPTURE(probe_name);
    CHECK(godot::String(probe["name"]) == "probe_id");
    CHECK(godot::String(probe["kind"]) == "int_range");
    NETW_CHECK_EQ(int64_t(probe["low"]), 0);
    NETW_CHECK_EQ(int64_t(probe["high"]), 4095);
    NETW_CHECK_EQ(int64_t(probe["width"]), 12);

    const Dictionary stamp = dumped[1];
    CHECK(godot::String(stamp["kind"]) == "bits");
    NETW_CHECK_EQ(int64_t(stamp["width"]), 48);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a described width is the width it spends"
) {
    // The spec's own arithmetic and the stream's have to agree, or a decoder
    // built from the spec would read a frame the encoder never wrote.
    const Array dumped = ClockPing::wire.spec_dump();
    int64_t described = 0;
    for (int64_t index = 0; index < dumped.size(); ++index) {
        const Dictionary entry = dumped[index];
        described += int64_t(entry["width"]);
    }

    ClockPing sample;
    MeasureStream measurer;
    REQUIRE(ClockPing::wire.run(measurer, sample));
    NETW_CHECK_EQ(described, measurer.bit_length());
}

} // namespace TestNetwWireDescribe
