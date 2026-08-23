// Unit tests for NetwCodec snapshot and window framings.
//
// The wire carries no property names, so every case supplies the ordered key
// list and the parallel quantizer list a decoder is assumed to derive from the
// same synchronizer config the encoder used. What the cases pin is that a
// payload mixing quantized and raw fields survives the round trip, that the
// two framings keep their headers, and that empty input stays empty rather
// than producing a frame nobody wrote.
//
// Floating-point comparisons are spelled as a named bool rather than handed to
// CHECK directly. doctest stringifies both sides of a failing comparison, and
// streaming a double out of this shared object crashes the host process, so a
// raw double comparison reports a segfault instead of the value that missed.

#include "support/netw_test.h"

#include <cmath>
#include <cstdint>

#include "netw/api/codec.hpp"

namespace TestNetwCodec {

using namespace godot;
using netw::NetwBitBufferReader;
using netw::NetwBitBufferWriter;
using netw::NetwCodec;
using netw::NetwQuantizeBits;
using netw::NetwQuantizeFixed;

Ref<NetwQuantizeBits> bits_quantizer(int count, double low, double high) {
    Ref<NetwQuantizeBits> made;
    made.instantiate();
    made->set_bit_count(count);
    made->set_min_limit(low);
    made->set_max_limit(high);
    return made;
}

Ref<NetwQuantizeFixed> fixed_quantizer(double step, double low, double high) {
    Ref<NetwQuantizeFixed> made;
    made.instantiate();
    made->set_resolution_step(step);
    made->set_min_limit(low);
    made->set_max_limit(high);
    return made;
}

Array names(const std::initializer_list<const char *> &keys) {
    Array out;
    for (const char *key : keys) {
        out.append(StringName(key));
    }
    return out;
}

TEST_CASE("[Networked][Codec][Hosted] a raw payload round trips") {
    // No quantizers: byte-aligned, inline type tags. Mixed types.
    const Array keys = names({"flag", "count", "motion", "spin"});
    const Array quantizers;
    const Array types;
    Dictionary payload;
    payload[StringName("flag")] = true;
    payload[StringName("count")] = 7;
    payload[StringName("motion")] = Vector2(1, 2);
    payload[StringName("spin")] = 0.5;

    Ref<NetwBitBufferWriter> w;
    w.instantiate();
    NetwCodec::encode_payload(w, payload, keys, quantizers);
    Ref<NetwBitBufferReader> r = NetwBitBufferReader::create(w->to_bytes());
    const Dictionary got
        = NetwCodec::decode_payload(r, keys, quantizers, types);

    CHECK(bool(got[StringName("flag")]));
    CHECK(int64_t(got[StringName("count")]) == 7);
    CHECK(Vector2(got[StringName("motion")]) == Vector2(1, 2));
    const bool spin_ok = std::abs(double(got[StringName("spin")]) - 0.5) < 1e-6;
    CHECK(spin_ok);
}

TEST_CASE("[Networked][Codec][Hosted] a quantized window round trips") {
    const Array keys = names({"motion"});
    Array quantizers;
    quantizers.append(bits_quantizer(8, -1.0, 1.0));
    Array types;
    types.append(Variant::VECTOR2);

    Array samples;
    const Vector2 motions[3] = {
        Vector2(1, 0),
        Vector2(0.5, -0.5),
        Vector2(-1, 1),
    };
    for (int index = 0; index < 3; ++index) {
        Dictionary input;
        input[StringName("motion")] = motions[index];
        Dictionary sample;
        sample[StringName("tick")] = 13 + index;
        sample[StringName("input")] = input;
        samples.append(sample);
    }

    const PackedByteArray bytes
        = NetwCodec::encode_window(samples, keys, quantizers);
    const Array got = NetwCodec::decode_window(bytes, keys, quantizers, types);

    CHECK(got.size() == 3);
    const Dictionary first = got[0];
    const Dictionary second = got[1];
    const Dictionary third = got[2];
    CHECK(int64_t(first[StringName("tick")]) == 13);
    CHECK(int64_t(third[StringName("tick")]) == 15);
    const Dictionary second_input = second[StringName("input")];
    const Dictionary third_input = third[StringName("input")];
    const bool second_ok = Vector2(second_input[StringName("motion")])
                               .distance_to(Vector2(0.5, -0.5))
        < 0.02;
    CHECK(second_ok);
    const bool third_ok
        = Vector2(third_input[StringName("motion")]).distance_to(Vector2(-1, 1))
        < 0.02;
    CHECK(third_ok);
}

TEST_CASE(
    "[Networked][Codec][Hosted] a snapshot mixes quantized and raw fields"
) {
    const Array keys = names({"position", "velocity", "stunned"});
    Array quantizers;
    quantizers.append(fixed_quantizer(0.5, -1000.0, 1000.0));
    quantizers.append(bits_quantizer(8, -90.0, 90.0));
    quantizers.append(Variant());
    Array types;
    types.append(Variant::VECTOR2);
    types.append(Variant::VECTOR2);
    types.append(Variant::BOOL);
    Dictionary payload;
    payload[StringName("position")] = Vector2(10.0, -20.5);
    payload[StringName("velocity")] = Vector2(30.0, -45.0);
    payload[StringName("stunned")] = true;

    const PackedByteArray bytes
        = NetwCodec::encode_snapshot(42, 39, payload, keys, quantizers);
    const Dictionary frame
        = NetwCodec::decode_snapshot(bytes, keys, quantizers, types);

    CHECK(int64_t(frame[StringName("tick")]) == 42);
    CHECK(int64_t(frame[StringName("ack")]) == 39);
    const Dictionary decoded = frame[StringName("payload")];
    CHECK(Vector2(decoded[StringName("position")]) == Vector2(10.0, -20.5));
    const bool velocity_ok = Vector2(decoded[StringName("velocity")])
                                 .distance_to(Vector2(30.0, -45.0))
        < 1.5;
    CHECK(velocity_ok);
    CHECK(bool(decoded[StringName("stunned")]));
}

TEST_CASE("[Networked][Codec][Hosted] a snapshot carries an ack of minus one") {
    const Array keys = names({"position"});
    Array quantizers;
    quantizers.append(Variant());
    Array types;
    types.append(Variant::VECTOR2);
    Dictionary payload;
    payload[StringName("position")] = Vector2(3, 4);

    const PackedByteArray bytes
        = NetwCodec::encode_snapshot(7, -1, payload, keys, quantizers);
    const Dictionary frame
        = NetwCodec::decode_snapshot(bytes, keys, quantizers, types);

    CHECK(int64_t(frame[StringName("ack")]) == -1);
    const Dictionary decoded = frame[StringName("payload")];
    CHECK(Vector2(decoded[StringName("position")]) == Vector2(3, 4));
}

TEST_CASE(
    "[Networked][Codec][Hosted] the scalar and vector3 raw tags round trip"
) {
    const Array keys = names({"scalar_float", "vector3"});
    const Array quantizers;
    const Array types;
    Dictionary payload;
    payload[StringName("scalar_float")] = 12.34;
    payload[StringName("vector3")] = Vector3(1, 2, 3);

    Ref<NetwBitBufferWriter> w;
    w.instantiate();
    NetwCodec::encode_payload(w, payload, keys, quantizers);
    Ref<NetwBitBufferReader> r = NetwBitBufferReader::create(w->to_bytes());
    const Dictionary got
        = NetwCodec::decode_payload(r, keys, quantizers, types);

    const bool scalar_ok
        = std::abs(double(got[StringName("scalar_float")]) - 12.34) < 0.001;
    CHECK(scalar_ok);
    CHECK(Vector3(got[StringName("vector3")]) == Vector3(1, 2, 3));
}

TEST_CASE(
    "[Networked][Codec][Hosted] an empty window and an empty snapshot stay "
    "empty"
) {
    const Array keys = names({"motion"});
    const Array empty;
    CHECK(NetwCodec::encode_window(empty, keys, empty).size() == 0);
    CHECK(
        NetwCodec::decode_window(PackedByteArray(), keys, empty, empty).size()
        == 0
    );
    CHECK(
        NetwCodec::decode_snapshot(PackedByteArray(), keys, empty, empty)
            .is_empty()
    );
}

} // namespace TestNetwCodec
