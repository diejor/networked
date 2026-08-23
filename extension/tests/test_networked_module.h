#pragma once

// Only the module tier compiles this file, and only the module tier can
// answer it: the question is whether the classes reached the ENGINE's
// registry rather than one a shared library set up for itself.
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "netw/api/bit_buffer.hpp"

using namespace godot;

namespace NetwTests {

// The tests compiled into the shared library cannot answer this: whether the
// classes reached the engine's own registry, rather than one the library set
// up for itself.
TEST_CASE(
    "[Networked][Registry] The module registers its classes with the engine"
) {
    CHECK(ClassDB::class_exists("NetwBitBufferWriter"));
    CHECK(ClassDB::class_exists("NetwBitBufferReader"));
    CHECK(ClassDB::class_exists("NetwCodec"));
    CHECK(ClassDB::class_exists("NetwRingBuffer"));
    CHECK(ClassDB::class_exists("NetwLivenessCore"));
    CHECK(ClassDB::class_exists("NetwHandleLedger"));
    CHECK(
        ClassDB::get_parent_class("NetwBitBufferWriter")
        == StringName("RefCounted")
    );
    CHECK(ClassDB::can_instantiate("NetwBitBufferWriter"));

    Ref<netw::NetwBitBufferWriter> writer;
    writer.instantiate();
    writer->put_bits(0b1011, 4);
    writer->put_aligned_u32(4242);

    Ref<netw::NetwBitBufferReader> reader
        = netw::NetwBitBufferReader::create(writer->to_bytes());
    CHECK(reader->get_bits(4) == 0b1011);
    CHECK(reader->get_aligned_u32() == 4242);
}

} // namespace NetwTests
