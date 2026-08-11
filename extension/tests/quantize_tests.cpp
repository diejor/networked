// Unit tests for the NetwQuantize family: round trip plus bounded error.
//
// A quantizer is a wire contract, so every case round-trips a value through a
// writer and a reader rather than inspecting the code it produced. Two groups
// of cases carry more weight than the rest. The endpoint laws pin that a
// bounded quantizer does not move its declared limits or the rest point of a
// symmetric range, because a ceiling that decodes to something else is
// invisible to every comparison while the two games read values a whole
// quantum apart. The centered-angle laws pin which half-open range an angle
// decodes into, because a recovery writes that number straight into the game.
//
// Closeness is asserted with NETW_CHECK_CLOSE rather than handed to CHECK
// directly. doctest stringifies both sides of a failing comparison, and
// streaming a double out of this shared object crashes the host process, so a
// raw double comparison reports a segfault instead of the value that missed.
// An inequality against a bound is still a named bool, because it has one
// operand rather than two.

#include "support/netw_test.h"

#include <cmath>
#include <cstdint>

#include "netw/quantize.hpp"

namespace TestNetwQuantize {

using namespace godot;
using netw::NetwBitBufferReader;
using netw::NetwBitBufferWriter;
using netw::NetwQuantize;
using netw::NetwQuantizeAngle;
using netw::NetwQuantizeBits;
using netw::NetwQuantizeFixed;
using netw::NetwQuantizeQuaternion;
using netw::NetwQuantizeTransform2D;
using netw::NetwQuantizeTransform3D;

constexpr double PI_VALUE = 3.14159265358979323846;
constexpr double TAU_VALUE = PI_VALUE * 2.0;

Variant round_trip(
    const Ref<NetwQuantize> &quantizer,
    const Variant &value,
    int type
) {
    Ref<NetwBitBufferWriter> w;
    w.instantiate();
    quantizer->write(w, value);
    Ref<NetwBitBufferReader> r = NetwBitBufferReader::create(w->to_bytes());
    return quantizer->read(r, type);
}

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

Ref<NetwQuantizeAngle> angle_quantizer(int count, bool centered) {
    Ref<NetwQuantizeAngle> made;
    made.instantiate();
    made->set_bit_count(count);
    made->set_centered_on_zero(centered);
    return made;
}

Ref<NetwQuantizeQuaternion> quaternion_quantizer(int count) {
    Ref<NetwQuantizeQuaternion> made;
    made.instantiate();
    made->set_bit_count(count);
    return made;
}

// The dot product accumulates in double on purpose. Two nearly equal rotations
// put it within a float epsilon of one, and acos amplifies that epsilon into an
// angle wider than the bound a twelve-bit quantizer promises, so a float
// accumulator would fail this file's own laws on an exact codec.
double angle_error(const Quaternion &a, const Quaternion &b) {
    const Quaternion an = a.normalized();
    const Quaternion bn = b.normalized();
    const double dot = std::abs(
        double(an.x) * double(bn.x) + double(an.y) * double(bn.y)
        + double(an.z) * double(bn.z) + double(an.w) * double(bn.w)
    );
    return 2.0 * std::acos(std::min(1.0, std::max(-1.0, dot)));
}

// The shortest signed arc between two angles, which is how a law asks whether
// two decodes name the same rotation rather than the same number.
double shortest_arc(double from, double to) {
    double difference = std::fmod(to - from, TAU_VALUE);
    difference = std::fmod(2.0 * difference, TAU_VALUE) - difference;
    return difference;
}

TEST_CASE(
    "[Networked][Codec][Hosted] a bit quantizer packs scalars and vectors"
) {
    Ref<NetwQuantizeBits> q = bits_quantizer(8, -1.0, 1.0);

    // error bound is span / (2^bits - 1) ~= 0.0078
    NETW_CHECK_CLOSE(round_trip(q, 0.5, Variant::FLOAT), 0.5, 0.01);
    CHECK(
        Vector2(round_trip(q, Vector2(0.25, -0.75), Variant::VECTOR2))
            .distance_to(Vector2(0.25, -0.75))
        < 0.02
    );
    CHECK(
        Vector3(round_trip(q, Vector3(0.25, -0.75, 0.5), Variant::VECTOR3))
            .distance_to(Vector3(0.25, -0.75, 0.5))
        < 0.02
    );
    CHECK(q->bit_width(Variant::VECTOR3) == 24);
    CHECK(q->bit_width(Variant::VECTOR2) == 16);
    CHECK(q->bit_width(Variant::FLOAT) == 8);

    // A symmetric range round-trips its center exactly, so a value at rest on
    // one axis (pure horizontal motion) does not pick up quant noise on the
    // other.
    const bool rest_is_exact
        = double(round_trip(q, 0.0, Variant::FLOAT)) == 0.0;
    CHECK(rest_is_exact);
    CHECK(
        Vector2(round_trip(q, Vector2(-1.0, 0.0), Variant::VECTOR2))
        == Vector2(-1.0, 0.0)
    );
    CHECK(
        Vector3(round_trip(q, Vector3(-1.0, 0.0, -1.0), Variant::VECTOR3))
        == Vector3(-1.0, 0.0, -1.0)
    );
}

TEST_CASE("[Networked][Codec][Hosted] a fixed quantizer is exact on its grid") {
    Ref<NetwQuantizeFixed> q = fixed_quantizer(0.5, -100.0, 100.0);

    // multiples of step round-trip exactly
    CHECK(
        Vector2(round_trip(q, Vector2(10.0, -20.5), Variant::VECTOR2))
        == Vector2(10.0, -20.5)
    );
    CHECK(
        Vector3(round_trip(q, Vector3(10.0, -20.5, 30.0), Variant::VECTOR3))
        == Vector3(10.0, -20.5, 30.0)
    );
    // an off-grid value snaps within half a step
    NETW_CHECK_CLOSE(round_trip(q, 3.3, Variant::FLOAT), 3.3, 0.26);
}

TEST_CASE(
    "[Networked][Codec][Hosted] the reported max error matches the resolution"
) {
    // Fixed: half a step per axis, magnitude across a vector.
    Ref<NetwQuantizeFixed> fixed = fixed_quantizer(0.5, -2048.0, 2048.0);
    NETW_CHECK_CLOSE(fixed->max_error(Variant::FLOAT), 0.25, 0.0001);
    NETW_CHECK_CLOSE(
        fixed->max_error(Variant::VECTOR2),
        0.25 * std::sqrt(2.0),
        0.0001
    );
    NETW_CHECK_CLOSE(
        fixed->max_error(Variant::VECTOR3),
        0.25 * std::sqrt(3.0),
        0.0001
    );

    // Bits: half the grid spacing (span / (2^bits - 2)) per axis. The tolerance
    // here is wider than the difference between that and span / 2^bits, so the
    // arithmetic itself is pinned by the round-trip laws rather than here.
    Ref<NetwQuantizeBits> bits = bits_quantizer(8, -1.0, 1.0);
    const double axis_error = 2.0 / 256.0 * 0.5;
    NETW_CHECK_CLOSE(bits->max_error(Variant::FLOAT), axis_error, 0.0001);
    NETW_CHECK_CLOSE(
        bits->max_error(Variant::VECTOR2),
        axis_error * std::sqrt(2.0),
        0.0001
    );
    NETW_CHECK_CLOSE(
        bits->max_error(Variant::VECTOR3),
        axis_error * std::sqrt(3.0),
        0.0001
    );

    // The round-trip error of a worst-case value never exceeds the bound.
    const double off_grid = 0.3123;
    const double decoded = round_trip(bits, off_grid, Variant::FLOAT);
    const bool within_bound
        = std::abs(decoded - off_grid) <= bits->max_error(Variant::FLOAT);
    CHECK(within_bound);

    // Angle: half the angular resolution in radians.
    Ref<NetwQuantizeAngle> angle = angle_quantizer(8, false);
    NETW_CHECK_CLOSE(
        angle->max_error(Variant::FLOAT),
        TAU_VALUE / 256.0 * 0.5,
        0.0001
    );
}

TEST_CASE("[Networked][Codec][Hosted] an angle quantizer wraps") {
    Ref<NetwQuantizeAngle> q = angle_quantizer(8, false);

    // error bound TAU / 256 ~= 0.0245
    NETW_CHECK_CLOSE(round_trip(q, PI_VALUE, Variant::FLOAT), PI_VALUE, 0.03);
    // TAU wraps back to ~0
    NETW_CHECK_CLOSE(round_trip(q, TAU_VALUE, Variant::FLOAT), 0.0, 0.03);
    CHECK(q->bit_width(Variant::FLOAT) == 8);
}

TEST_CASE(
    "[Networked][Codec][Hosted] a quaternion uses the smallest-three layout"
) {
    Ref<NetwQuantizeQuaternion> q = quaternion_quantizer(12);

    const Vector3 axis = Vector3(0.3, 1.0, 0.2).normalized();
    const Quaternion value(axis, 1.234);
    const Quaternion got = round_trip(q, value, Variant::QUATERNION);

    CHECK(q->bit_width(Variant::QUATERNION) == 38);
    const bool rotation_ok
        = angle_error(value, got) <= q->max_error(Variant::QUATERNION);
    CHECK(rotation_ok);
}

TEST_CASE(
    "[Networked][Codec][Hosted] a 2D transform composes origin, rotation and "
    "scale"
) {
    Ref<NetwQuantizeTransform2D> q;
    q.instantiate();
    q->set_origin_quantizer(fixed_quantizer(0.25, -10.0, 10.0));
    q->set_rotation_quantizer(angle_quantizer(12, false));
    q->set_scale_quantizer(fixed_quantizer(0.125, 0.0, 4.0));

    const Transform2D
        value(PI_VALUE * 0.25, Vector2(2.0, 0.5), 0.0, Vector2(3.25, -4.5));
    const Transform2D got = round_trip(q, value, Variant::TRANSFORM2D);

    CHECK(q->bit_width(Variant::TRANSFORM2D) == 38);
    CHECK(got.get_origin() == Vector2(3.25, -4.5));
    const bool rotation_ok
        = std::abs(got.get_rotation() - value.get_rotation()) < 0.002;
    CHECK(rotation_ok);
    CHECK(got.get_scale().distance_to(value.get_scale()) < 0.002);
}

TEST_CASE(
    "[Networked][Codec][Hosted] a 3D transform composes origin, rotation and "
    "scale"
) {
    const Quaternion rotation(Vector3(0.2, 1.0, 0.4).normalized(), 0.9);
    const Transform3D value(
        Basis(rotation).scaled_local(Vector3(1.5, 0.75, 2.0)),
        Vector3(1.25, -2.5, 3.75)
    );
    Ref<NetwQuantizeQuaternion> rotation_quantizer = quaternion_quantizer(12);

    Ref<NetwQuantizeTransform3D> q;
    q.instantiate();
    q->set_origin_quantizer(fixed_quantizer(0.25, -10.0, 10.0));
    q->set_rotation_quantizer(rotation_quantizer);
    q->set_scale_quantizer(fixed_quantizer(0.125, 0.0, 4.0));

    const Transform3D got = round_trip(q, value, Variant::TRANSFORM3D);

    const bool width_ok = q->bit_width(Variant::TRANSFORM3D) == 77;
    CHECK(width_ok);
    const bool origin_ok = got.origin == Vector3(1.25, -2.5, 3.75);
    CHECK(origin_ok);
    const bool rotation_ok
        = angle_error(rotation, got.basis.get_rotation_quaternion())
        <= rotation_quantizer->max_error(Variant::QUATERNION);
    CHECK(rotation_ok);
    const bool scale_ok
        = got.basis.get_scale().distance_to(value.basis.get_scale()) < 0.002;
    CHECK(scale_ok);
}

TEST_CASE(
    "[Networked][Codec][Hosted] layout equality matches class and parameters"
) {
    // Two fresh instances with identical parameters are the same schema, the
    // case a per-instance re-declaration produces every spawn.
    Ref<NetwQuantizeFixed> a = fixed_quantizer(0.25, -10.0, 10.0);
    Ref<NetwQuantizeFixed> b = fixed_quantizer(0.25, -10.0, 10.0);
    CHECK(a->is_same_layout(b));

    // A changed parameter is a different bit layout.
    Ref<NetwQuantizeFixed> c = fixed_quantizer(0.5, -10.0, 10.0);
    CHECK_FALSE(a->is_same_layout(c));

    // A different quantizer class is a different layout even when field names
    // overlap, and null never matches an instance.
    Ref<NetwQuantizeBits> bits = bits_quantizer(8, -10.0, 10.0);
    CHECK_FALSE(a->is_same_layout(bits));
    CHECK_FALSE(a->is_same_layout(Ref<NetwQuantize>()));
}

// The three values a bounded quantizer must not move: both declared limits, and
// the rest point of a symmetric range.
//
// Endpoints matter because canonical form is what a predicted transition is
// compared on, and a maximum that decodes to something else is invisible to
// every comparison -- both peers encode the same code -- while the two games
// read values a whole quantum apart. That is how racing's four-bit steer latch
// became the seed of a heading drift no column could see.
//
// The rest point matters for the opposite reason: it is read directly. A stick
// at neutral or a body at rest that decodes to a small non-zero drives the game
// rather than any comparison.
//
// They cannot all sit on a 2^bits grid, so the grid holds 2^bits - 1 levels and
// spends one bit pattern. Both halves are asserted here because a scheme that
// buys either one by selling the other has been proposed twice.
TEST_CASE(
    "[Networked][Codec][Hosted] a bit quantizer round trips its limits and "
    "rest point"
) {
    for (int count : {2, 4, 8, 16}) {
        Ref<NetwQuantizeBits> q = bits_quantizer(count, -1.0, 1.0);
        const double low = round_trip(q, -1.0, Variant::FLOAT);
        const double high = round_trip(q, 1.0, Variant::FLOAT);
        const double rest = round_trip(q, 0.0, Variant::FLOAT);

        // The declared minimum decodes to itself.
        NETW_CHECK_CLOSE(low, -1.0, 0.000001);
        // A range whose ceiling is unrepresentable is a lie no comparison can
        // catch.
        NETW_CHECK_CLOSE(high, 1.0, 0.000001);
        // Spreading the range over 2^bits levels instead of an odd count would
        // buy the ceiling by selling this.
        NETW_CHECK_CLOSE(rest, 0.0, 0.000001);
    }

    // One bit cannot hold three distinct values, so it degenerates to the two
    // endpoints rather than dividing by zero.
    Ref<NetwQuantizeBits> thin = bits_quantizer(1, -1.0, 1.0);
    NETW_CHECK_CLOSE(round_trip(thin, -1.0, Variant::FLOAT), -1.0, 1e-6);
    NETW_CHECK_CLOSE(round_trip(thin, 1.0, Variant::FLOAT), 1.0, 1e-6);
}

// An asymmetric range has no rest point to protect, and both limits still hold.
TEST_CASE(
    "[Networked][Codec][Hosted] a bit quantizer round trips an asymmetric range"
) {
    Ref<NetwQuantizeBits> q = bits_quantizer(8, 0.0, 100.0);

    NETW_CHECK_CLOSE(round_trip(q, 0.0, Variant::FLOAT), 0.0, 0.000001);
    // The declared maximum decodes to itself on an asymmetric range too.
    NETW_CHECK_CLOSE(round_trip(q, 100.0, Variant::FLOAT), 100.0, 1e-4);
}

// The error scales with coarseness, so the same defect is invisible at the bit
// depths a pose uses and decisive at the depth a latch uses.
TEST_CASE(
    "[Networked][Codec][Hosted] the endpoint error shrinks across bit depths"
) {
    double previous = 1.0;
    for (int count : {4, 8, 16, 24}) {
        Ref<NetwQuantizeBits> q = bits_quantizer(count, -1.0, 1.0);
        const double high = round_trip(q, 1.0, Variant::FLOAT);
        const double error = std::abs(1.0 - high);
        const bool error_shrinks = error <= previous;
        CHECK(error_shrinks);
        previous = error;
    }
}

// An angle codec decodes into the range the field it writes back into stores
// angles in, because a recovery writes that value into the game.
//
// Both ranges name the same rotation, so nothing downstream that treats the
// field as an angle can tell them apart. What can tell them apart is the game
// reading the number: a Godot Euler component carries -PI < angle <= PI, and
// leaving TAU-relative values in it means the field holds something outside the
// range the game declared it in.
TEST_CASE(
    "[Networked][Codec][Hosted] a centered angle decodes into the Godot Euler "
    "range"
) {
    Ref<NetwQuantizeAngle> plain = angle_quantizer(16, false);
    Ref<NetwQuantizeAngle> centered = angle_quantizer(16, true);

    for (double value : {-3.0, -0.5, 0.0, 0.5, 3.0}) {
        const double wide = round_trip(plain, value, Variant::FLOAT);
        const double centered_value
            = round_trip(centered, value, Variant::FLOAT);
        // Both name the same rotation, which is what makes this a range choice
        // rather than a correctness one.
        const bool same_rotation
            = std::abs(shortest_arc(wide, centered_value)) < 0.0005;
        CHECK(same_rotation);
        const bool round_trips
            = std::abs(shortest_arc(value, centered_value)) < 0.0005;
        CHECK(round_trips);
        // Only the centered one stays in the range a Godot Euler uses.
        const bool inside_euler_range = centered_value > -PI_VALUE - 0.0001
            && centered_value <= PI_VALUE + 0.0001;
        CHECK(inside_euler_range);
    }

    // The boundary resolves positive, so both peers agree on the one code that
    // sits exactly on it rather than splitting it between +PI and -PI.
    NETW_CHECK_CLOSE(
        round_trip(centered, PI_VALUE, Variant::FLOAT),
        PI_VALUE,
        0.0005
    );
    // -PI names the same rotation as +PI and lands on its code.
    NETW_CHECK_CLOSE(
        round_trip(centered, -PI_VALUE, Variant::FLOAT),
        PI_VALUE,
        0.0005
    );

    // Wrap costs nothing either side of the boundary: neighbouring angles stay
    // neighbours, which is the property a linear codec on an angle would lose.
    const double step = TAU_VALUE / 65536.0;
    const double below = round_trip(centered, PI_VALUE - step, Variant::FLOAT);
    const double above = round_trip(centered, -PI_VALUE + step, Variant::FLOAT);
    const bool neighbours_stay_close
        = std::abs(shortest_arc(below, above)) < 3.0 * step;
    CHECK(neighbours_stay_close);
}

} // namespace TestNetwQuantize
