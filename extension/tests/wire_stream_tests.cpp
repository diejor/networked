// The substrate contract: one description, three modes, and a read that fails
// instead of inventing.
//
// Each format below is written once as a template over the stream and then run
// through all three, because that symmetry is the only thing that keeps the
// modes from drifting, and a test that wrote three bodies would be testing
// three different formats.
//
// Numbers compare through NETW_CHECK_EQ rather than CHECK: doctest stringifies
// both sides of a failing comparison, and streaming a number out of this shared
// object segfaults the hosted runner, which would end the run instead of
// reporting the case.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

using namespace godot;

namespace TestNetwWireStream {

using godot::PackedByteArray;
using netw::wire::MeasureStream;
using netw::wire::ReadStream;
using netw::wire::WriteStream;

struct Header {
    int64_t route = 0;
    int64_t channel = 0;
    bool reliable = false;
    uint64_t stamp = 0;
};

struct WindowHeader {
    int64_t ack = -1;
    uint64_t base = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&WindowHeader::ack>("ack", netw::wire::svarint(5)),
        netw::wire::field<&WindowHeader::base>("base", netw::wire::varuint(5))
    );
};

struct SignedBits {
    int32_t fingerprint = -1;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SignedBits::fingerprint>(
            "fingerprint",
            netw::wire::bits(32)
        )
    );
};

template <class Stream> bool serialize_header(Stream &stream, Header &value) {
    return stream.int_range(value.route, 0, 4095)
        && stream.int_range(value.channel, 0, 255)
        && stream.bool1(value.reliable) && stream.bits(value.stamp, 48);
}

TEST_CASE("[Networked][Wire][Hosted] bits round trip at arbitrary widths") {
    WriteStream writer;
    uint64_t three = 5;
    uint64_t nine = 300;
    uint64_t one = 1;
    uint64_t full = 0xFEDCBA9876543210ULL;
    REQUIRE(writer.bits(three, 3));
    REQUIRE(writer.bits(nine, 9));
    REQUIRE(writer.bits(one, 1));
    REQUIRE(writer.bits(full, 64));
    NETW_CHECK_EQ(writer.bit_length(), 77);

    ReadStream reader(writer.to_bytes());
    uint64_t back = 0;
    REQUIRE(reader.bits(back, 3));
    NETW_CHECK_EQ(back, 5);
    REQUIRE(reader.bits(back, 9));
    NETW_CHECK_EQ(back, 300);
    REQUIRE(reader.bits(back, 1));
    NETW_CHECK_EQ(back, 1);
    REQUIRE(reader.bits(back, 64));
    NETW_CHECK_EQ(back, 0xFEDCBA9876543210ULL);
    CHECK(reader.ok());
}

TEST_CASE("[Networked][Wire][Hosted] a field costs the width its range needs") {
    WriteStream writer;
    int64_t value = 300;
    REQUIRE(writer.int_range(value, 0, 4095));
    NETW_CHECK_EQ(writer.bit_length(), 12);

    // A field with one legal value costs nothing at all, which is not the same
    // as costing a zero.
    WriteStream single;
    int64_t only = 7;
    REQUIRE(single.int_range(only, 7, 7));
    NETW_CHECK_EQ(single.bit_length(), 0);
}

TEST_CASE("[Networked][Wire][Hosted] the three modes describe one format") {
    Header sent;
    sent.route = 4000;
    sent.channel = 19;
    sent.reliable = true;
    sent.stamp = 0xFFFFFFFFFFULL;

    WriteStream writer;
    REQUIRE(serialize_header(writer, sent));

    MeasureStream measurer;
    Header priced = sent;
    REQUIRE(serialize_header(measurer, priced));
    NETW_CHECK_EQ(measurer.bit_length(), writer.bit_length());
    NETW_CHECK_EQ(measurer.byte_length(), 9);

    ReadStream reader(writer.to_bytes());
    Header received;
    REQUIRE(serialize_header(reader, received));
    CHECK(reader.ok());
    NETW_CHECK_EQ(received.route, sent.route);
    NETW_CHECK_EQ(received.channel, sent.channel);
    CHECK(received.reliable);
    NETW_CHECK_EQ(received.stamp, sent.stamp);
}

TEST_CASE(
    "[Networked][Wire][Hosted] aligned varints preserve the prediction "
    "window bytes"
) {
    WindowHeader sent;
    sent.ack = -1;
    sent.base = 300;
    WriteStream writer;
    REQUIRE(WindowHeader::wire.run(writer, sent));
    const PackedByteArray bytes = writer.to_bytes();
    NETW_CHECK_EQ(bytes.size(), 3);
    NETW_CHECK_EQ(bytes[0], 1);
    NETW_CHECK_EQ(bytes[1], 0xac);
    NETW_CHECK_EQ(bytes[2], 0x02);

    MeasureStream measurer;
    WindowHeader priced = sent;
    REQUIRE(WindowHeader::wire.run(measurer, priced));
    NETW_CHECK_EQ(measurer.byte_length(), bytes.size());

    ReadStream reader(bytes);
    WindowHeader received;
    REQUIRE(WindowHeader::wire.run(reader, received));
    NETW_CHECK_EQ(received.ack, -1);
    NETW_CHECK_EQ(received.base, 300);
}

TEST_CASE("[Networked][Wire][Hosted] signed bits preserve their bit pattern") {
    SignedBits sent;
    WriteStream writer;
    REQUIRE(SignedBits::wire.run(writer, sent));

    ReadStream reader(writer.to_bytes());
    SignedBits received;
    received.fingerprint = 0;
    REQUIRE(SignedBits::wire.run(reader, received));
    NETW_CHECK_EQ(received.fingerprint, -1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a truncated or overlong varint poisons once"
) {
    PackedByteArray truncated;
    truncated.push_back(0x80);
    ReadStream short_read(truncated);
    uint64_t value = 77;
    CHECK_FALSE(short_read.varuint(value, 5));
    CHECK_FALSE(short_read.ok());
    NETW_CHECK_EQ(value, 77);

    PackedByteArray overlong;
    for (int at = 0; at < 5; ++at) {
        overlong.push_back(0x80);
    }
    ReadStream long_read(overlong);
    CHECK_FALSE(long_read.varuint(value, 5));
    CHECK_FALSE(long_read.ok());
    NETW_CHECK_EQ(value, 77);

    PackedByteArray overflow;
    for (int at = 0; at < 9; ++at) {
        overflow.push_back(0x80);
    }
    overflow.push_back(0x02);
    ReadStream wide_read(overflow);
    CHECK_FALSE(wide_read.varuint(value));
    CHECK_FALSE(wide_read.ok());
    NETW_CHECK_EQ(value, 77);

    PackedByteArray redundant;
    redundant.push_back(0x80);
    redundant.push_back(0x00);
    ReadStream noncanonical_read(redundant);
    CHECK_FALSE(noncanonical_read.varuint(value));
    CHECK_FALSE(noncanonical_read.ok());
    NETW_CHECK_EQ(value, 77);
}

TEST_CASE("[Networked][Wire][Hosted] a measure never touches the value") {
    MeasureStream measurer;
    Header untouched;
    untouched.route = 11;
    untouched.stamp = 7;
    REQUIRE(serialize_header(measurer, untouched));
    NETW_CHECK_EQ(untouched.route, 11);
    NETW_CHECK_EQ(untouched.stamp, 7);
}

TEST_CASE("[Networked][Wire][Hosted] a read past the end poisons") {
    WriteStream writer;
    uint64_t small = 1;
    REQUIRE(writer.bits(small, 4));

    ReadStream reader(writer.to_bytes());
    uint64_t back = 99;
    REQUIRE(reader.bits(back, 4));
    NETW_CHECK_EQ(back, 1);
    CHECK_FALSE(reader.bits(back, 32));
    CHECK_FALSE(reader.ok());
    NETW_CHECK_EQ(back, 1);
}

TEST_CASE("[Networked][Wire][Hosted] an empty datagram decodes to nothing") {
    const PackedByteArray nothing;
    ReadStream reader(nothing);
    Header received;
    received.route = 3;
    CHECK_FALSE(serialize_header(reader, received));
    CHECK_FALSE(reader.ok());
    NETW_CHECK_EQ(received.route, 3);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a value above its range poisons the read"
) {
    // A 0..2000 field spends the 11 bits that hold 2000, and 11 bits can name
    // 2047, so the width does not confine the value and the read has to carry
    // the bound itself.
    WriteStream writer;
    uint64_t top_of_width = 2047;
    REQUIRE(writer.bits(top_of_width, 11));

    ReadStream reader(writer.to_bytes());
    int64_t ranged = 1;
    CHECK_FALSE(reader.int_range(ranged, 0, 2000));
    CHECK_FALSE(reader.ok());
    NETW_CHECK_EQ(ranged, 1);
}

TEST_CASE("[Networked][Wire][Hosted] a poisoned stream stops answering") {
    const PackedByteArray nothing;
    ReadStream reader(nothing);
    uint64_t value = 42;
    CHECK_FALSE(reader.bits(value, 8));
    CHECK_FALSE(reader.ok());

    bool flag = true;
    int64_t ranged = 5;
    PackedByteArray blob;
    CHECK_FALSE(reader.bool1(flag));
    CHECK_FALSE(reader.int_range(ranged, 0, 7));
    CHECK_FALSE(reader.bytes_capped(blob, 4));
    CHECK_FALSE(reader.align_verify());
    NETW_CHECK_EQ(value, 42);
    CHECK(flag);
    NETW_CHECK_EQ(ranged, 5);
    CHECK(blob.is_empty());
}

TEST_CASE("[Networked][Wire][Hosted] alignment pads, and a dirty pad poisons") {
    WriteStream writer;
    uint64_t three = 5;
    REQUIRE(writer.bits(three, 3));
    REQUIRE(writer.align_verify());
    NETW_CHECK_EQ(writer.bit_length(), 8);

    ReadStream clean(writer.to_bytes());
    uint64_t back = 0;
    REQUIRE(clean.bits(back, 3));
    CHECK(clean.align_verify());
    CHECK(clean.ok());

    PackedByteArray dirty = writer.to_bytes();
    dirty.set(0, dirty[0] | 0x80);
    ReadStream poisoned(dirty);
    REQUIRE(poisoned.bits(back, 3));
    CHECK_FALSE(poisoned.align_verify());
    CHECK_FALSE(poisoned.ok());
}

TEST_CASE("[Networked][Wire][Hosted] a capped blob round trips") {
    PackedByteArray payload;
    payload.push_back(0xDE);
    payload.push_back(0xAD);
    payload.push_back(0xBE);

    WriteStream writer;
    uint64_t lead = 1;
    REQUIRE(writer.bits(lead, 1));
    REQUIRE(writer.bytes_capped(payload, 64));

    ReadStream reader(writer.to_bytes());
    uint64_t back = 0;
    PackedByteArray received;
    REQUIRE(reader.bits(back, 1));
    REQUIRE(reader.bytes_capped(received, 64));
    CHECK(reader.ok());
    NETW_CHECK_EQ(received.size(), payload.size());
    for (int64_t index = 0; index < payload.size(); ++index) {
        NETW_CHECK_EQ(received[index], payload[index]);
    }
}

TEST_CASE("[Networked][Wire][Hosted] a blob longer than the datagram poisons") {
    // The length is legal against its cap and a lie about the bytes behind it,
    // which is the shape a truncating carrier produces.
    WriteStream writer;
    int64_t length = 60;
    REQUIRE(writer.int_range(length, 0, 64));
    REQUIRE(writer.align_verify());

    ReadStream reader(writer.to_bytes());
    PackedByteArray received;
    CHECK_FALSE(reader.bytes_capped(received, 64));
    CHECK_FALSE(reader.ok());
    CHECK(received.is_empty());
}

TEST_CASE("[Networked][Wire][Hosted] a frame that decodes leaves no residue") {
    Header sent;
    sent.route = 12;
    sent.channel = 19;
    sent.reliable = false;
    sent.stamp = 900;

    WriteStream writer;
    REQUIRE(serialize_header(writer, sent));
    REQUIRE(writer.align_verify());

    ReadStream reader(writer.to_bytes());
    Header received;
    REQUIRE(serialize_header(reader, received));
    REQUIRE(reader.align_verify());
    CHECK(reader.ok());
    NETW_CHECK_EQ(reader.bits_remaining(), 0);
}

} // namespace TestNetwWireStream
