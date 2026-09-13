#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/bit_stream.hpp"

namespace TestNetwBitStreamLaws {

using namespace godot;
using netw::NetwBitStream;

Ref<NetwBitStream> a_row() {
    const Ref<NetwBitStream> out = NetwBitStream::writer();
    out->bits(0b1011, 4);
    out->varuint(4242, 3);
    out->string("hero");
    out->align_verify();
    return out;
}

TEST_CASE(
    "[Networked][Wire][Hosted] BS1 one description spends the same bits in "
    "all three modes, which is what makes a writer and a reader impossible "
    "to write out of step"
) {
    const Ref<NetwBitStream> writing = a_row();
    const Ref<NetwBitStream> reading
        = NetwBitStream::reader(writing->to_bytes());
    const Ref<NetwBitStream> measuring = NetwBitStream::measurer();
    measuring->bits(0b1011, 4);
    measuring->varuint(4242, 3);
    measuring->string("hero");
    measuring->align_verify();

    const bool bits_back = reading->bits(0, 4) == 0b1011;
    const bool number_back = reading->varuint(0, 3) == 4242;
    const bool name_back = reading->string(String()) == String("hero");
    const bool aligned = reading->align_verify();

    CHECK(bits_back);
    CHECK(number_back);
    CHECK(name_back);
    CHECK(aligned);

    const bool measured_agrees
        = measuring->bit_length() == writing->bit_length();
    const bool read_agrees = reading->bit_length() == writing->bit_length();
    const bool exhausted = reading->bits_remaining() == 0;
    CHECK(measured_agrees);
    CHECK(read_agrees);
    CHECK(exhausted);
}

TEST_CASE(
    "[Networked][Wire][Hosted] BS2 a read that runs past the end poisons the "
    "stream, so every verb after it answers the caller's own default rather "
    "than bytes that were never sent"
) {
    PackedByteArray cut = a_row()->to_bytes();
    cut.resize(2);

    const Ref<NetwBitStream> reading = NetwBitStream::reader(cut);
    reading->bits(0, 4);
    reading->varuint(0, 3);
    reading->string(String());

    const bool poisoned = !reading->ok();
    const bool default_back = reading->varuint(int64_t(777), 3) == int64_t(777);
    const bool stays_poisoned = !reading->ok();

    CHECK(poisoned);
    CHECK(default_back);
    CHECK(stays_poisoned);
}

TEST_CASE(
    "[Networked][Wire][Hosted] BS3 a bound is refused on the side that can "
    "still do something about it, so a value too large to spell produces no "
    "bytes and a value the wire claims is out of range produces no value"
) {
    const Ref<NetwBitStream> writing = NetwBitStream::writer();
    writing->varuint(int64_t(1) << 40, 3);
    const bool writer_refused = !writing->ok();
    CHECK(writer_refused);

    const Ref<NetwBitStream> ranged = NetwBitStream::writer();
    ranged->int_range(9, 0, 3);
    const bool range_refused = !ranged->ok();
    CHECK(range_refused);

    const Ref<NetwBitStream> measuring = NetwBitStream::measurer();
    const bool measure_never_poisons
        = measuring->ok() && measuring->to_bytes().is_empty();
    CHECK(measure_never_poisons);
}

TEST_CASE(
    "[Networked][Wire][Hosted] BS4 alignment padding that is not zero is "
    "refused, because a tail nobody declared is a tail this description "
    "cannot account for"
) {
    const Ref<NetwBitStream> writing = NetwBitStream::writer();
    writing->bits(1, 1);
    writing->align_verify();
    PackedByteArray tampered = writing->to_bytes();
    const int last = tampered.size() - 1;
    tampered.set(last, uint8_t(tampered[last] | 0x80));

    const Ref<NetwBitStream> reading = NetwBitStream::reader(tampered);
    reading->bits(0, 1);
    const bool refused = !reading->align_verify();
    const bool poisoned = !reading->ok();

    CHECK(refused);
    CHECK(poisoned);
}

} // namespace TestNetwBitStreamLaws
