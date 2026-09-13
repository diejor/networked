#pragma once

#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "netw/api/bit_stream.hpp"

namespace NetwTests {

TEST_CASE(
    "[Networked][Registry] The module registers its classes with the engine"
) {
    CHECK(godot::ClassDB::class_exists("NetwBitStream"));
    CHECK(godot::ClassDB::class_exists("NetwRingBuffer"));
    CHECK(godot::ClassDB::class_exists("NetwMultiplayer"));
    CHECK(
        godot::ClassDB::get_parent_class("NetwBitStream")
        == godot::StringName("RefCounted")
    );
    CHECK(godot::ClassDB::can_instantiate("NetwBitStream"));

    const godot::Ref<netw::NetwBitStream> writer
        = netw::NetwBitStream::writer();
    writer->bits(0b1011, 4);
    writer->varuint(4242, 3);
    const bool written = writer->align_verify();
    CHECK(written);

    const godot::Ref<netw::NetwBitStream> reader
        = netw::NetwBitStream::reader(writer->to_bytes());
    const bool bits_back = reader->bits(0, 4) == 0b1011;
    const bool number_back = reader->varuint(0, 3) == 4242;
    CHECK(bits_back);
    CHECK(number_back);
    const bool exhausted
        = reader->align_verify() && reader->bits_remaining() == 0;
    CHECK(exhausted);
}

} // namespace NetwTests
