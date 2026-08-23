// Unit tests for NetwBitBufferWriter / NetwBitBufferReader symmetry.
//
// Every case writes with one and reads with the other, because the pair only
// means anything together: the reader serves bits back in the order the writer
// accumulated them, and the aligned helpers have to agree on where a byte
// boundary is even when a bit-packed field left a partial one behind.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/bit_buffer.hpp"

namespace TestNetwBitBuffer {

using namespace godot;
using netw::NetwBitBufferReader;
using netw::NetwBitBufferWriter;

Ref<NetwBitBufferWriter> writer() {
    Ref<NetwBitBufferWriter> made;
    made.instantiate();
    return made;
}

TEST_CASE("[Networked][Codec][Hosted] bit round trip at arbitrary widths") {
    Ref<NetwBitBufferWriter> w = writer();
    w->put_bits(5, 3);
    w->put_bits(300, 9);
    w->put_bits(1, 1);
    w->put_bits(0, 4);

    Ref<NetwBitBufferReader> r = NetwBitBufferReader::create(w->to_bytes());
    CHECK(r->get_bits(3) == 5);
    CHECK(r->get_bits(9) == 300);
    CHECK(r->get_bits(1) == 1);
    CHECK(r->get_bits(4) == 0);
}

TEST_CASE("[Networked][Codec][Hosted] a partial final byte flushes") {
    Ref<NetwBitBufferWriter> w = writer();
    w->put_bits(0b101, 3);
    const PackedByteArray bytes = w->to_bytes();
    CHECK(bytes.size() == 1);
    CHECK(bytes[0] == 0b101);

    Ref<NetwBitBufferReader> r = NetwBitBufferReader::create(bytes);
    CHECK(r->get_bits(3) == 5);
}

TEST_CASE("[Networked][Codec][Hosted] aligned helpers round trip") {
    Ref<NetwBitBufferWriter> w = writer();
    w->put_aligned_u8(200);
    w->put_aligned_u32(0xDEADBEEF);
    PackedByteArray tail;
    tail.resize(5);
    for (int index = 0; index < 5; ++index) {
        tail.set(index, uint8_t(index + 1));
    }
    w->put_aligned_bytes(tail);

    Ref<NetwBitBufferReader> r = NetwBitBufferReader::create(w->to_bytes());
    CHECK(r->get_aligned_u8() == 200);
    CHECK(r->get_aligned_u32() == 0xDEADBEEF);
    CHECK(r->get_aligned_bytes(5) == tail);
}

TEST_CASE("[Networked][Codec][Hosted] bits then align skips the padding") {
    Ref<NetwBitBufferWriter> w = writer();
    w->put_bits(7, 3);
    w->put_aligned_u8(123);
    w->put_bits(2, 2);
    w->put_aligned_u32(99);

    Ref<NetwBitBufferReader> r = NetwBitBufferReader::create(w->to_bytes());
    CHECK(r->get_bits(3) == 7);
    CHECK(r->get_aligned_u8() == 123);
    CHECK(r->get_bits(2) == 2);
    CHECK(r->get_aligned_u32() == 99);
}

} // namespace TestNetwBitBuffer
