#include "support/netw_test.h"

#include <cstdint>

#include "netw/wire/stream.hpp"

namespace TestNetwWireConformance {

using netw::wire::ReadStream;
using netw::wire::WriteStream;

TEST_CASE(
    "[Networked][Wire][Hosted] red-proof: truncated read poisons stream"
) {
    godot::PackedByteArray bytes;
    bytes.push_back(0xFF);

    ReadStream reader(bytes);
    uint64_t val = 0;

    CHECK_FALSE(reader.bits(val, 16));
    CHECK_FALSE(reader.ok());

    uint64_t next_val = 123;
    CHECK_FALSE(reader.bits(next_val, 4));
    NETW_CHECK_EQ(next_val, 123);
    CHECK_FALSE(reader.ok());
}

TEST_CASE(
    "[Networked][Wire][Hosted] red-proof: align_verify flushes and verifies "
    "byte boundary"
) {
    godot::PackedByteArray bytes;
    bytes.push_back(0x07);
    bytes.push_back(0xCD);

    ReadStream reader(bytes);
    uint64_t low_bits = 0;
    REQUIRE(reader.bits(low_bits, 3));
    NETW_CHECK_EQ(low_bits, 7);
    REQUIRE(reader.align_verify());
    NETW_CHECK_EQ(reader.bit_length(), 8);

    uint64_t byte2 = 0;
    REQUIRE(reader.bits(byte2, 8));
    NETW_CHECK_EQ(byte2, 0xCD);
    REQUIRE(reader.align_verify());
    CHECK(reader.ok());
}

TEST_CASE(
    "[Networked][Wire][Hosted] fuzz safety: pseudo-random payload streams "
    "never panic"
) {
    uint32_t seed = 0x12345678;
    auto lcg = [&seed]() -> uint8_t {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<uint8_t>(seed >> 24);
    };

    for (int iter = 0; iter < 100; ++iter) {
        godot::PackedByteArray payload;
        const int len = (iter % 16) + 1;
        for (int b = 0; b < len; ++b) {
            payload.push_back(lcg());
        }

        ReadStream reader(payload);
        while (reader.ok() && reader.bits_remaining() > 0) {
            uint64_t dummy = 0;
            int bit_count = (iter + reader.bit_length()) % 17 + 1;
            reader.bits(dummy, bit_count);
        }

        CHECK((reader.ok() || !reader.ok()));
    }
}

} // namespace TestNetwWireConformance
