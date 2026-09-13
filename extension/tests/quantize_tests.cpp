#include "support/netw_test.h"

#include <cmath>
#include <cstdint>

#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/value_row.hpp"
#if defined(NETW_TIER_HOSTED)
#include "support/minted_script.h"
#endif

namespace TestNetwQuantize {

using namespace godot;
using netw::NetwQuantize;
using netw::NetwQuantizeAngle;
using netw::NetwQuantizeQuaternion;
using netw::NetwQuantizeScalar;
using netw::NetwQuantizeTransform2D;
using netw::NetwQuantizeTransform3D;

constexpr double PI_VALUE = 3.14159265358979323846;
constexpr double TAU_VALUE = PI_VALUE * 2.0;

Variant round_trip(
    const Ref<NetwQuantize> &quantizer,
    const Variant &value,
    Variant::Type type
) {
    netw::wire::WriteStream writing;
    if (!quantizer->write(writing, value)) {
        return Variant();
    }
    netw::wire::ReadStream reading(writing.to_bytes());
    Variant out;
    if (!quantizer->read(reading, type, out)) {
        return Variant();
    }
    return out;
}

Ref<NetwQuantizeScalar> bits_quantizer(int count, double low, double high) {
    Ref<NetwQuantizeScalar> made;
    made.instantiate();
    made->set_bit_count(count);
    made->set_min_limit(low);
    made->set_max_limit(high);
    return made;
}

Ref<NetwQuantizeScalar> fixed_quantizer(double step, double low, double high) {
    Ref<NetwQuantizeScalar> made;
    made.instantiate();
    made->set_min_limit(low);
    made->set_max_limit(high);
    made->set_resolution_step(step);
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

double angle_error(const Quaternion &a, const Quaternion &b) {
    const Quaternion an = a.normalized();
    const Quaternion bn = b.normalized();
    const double dot = std::abs(
        double(an.x) * double(bn.x) + double(an.y) * double(bn.y)
        + double(an.z) * double(bn.z) + double(an.w) * double(bn.w)
    );
    return 2.0 * std::acos(std::min(1.0, std::max(-1.0, dot)));
}

double shortest_arc(double from, double to) {
    double difference = std::fmod(to - from, TAU_VALUE);
    difference = std::fmod(2.0 * difference, TAU_VALUE) - difference;
    return difference;
}

TEST_CASE(
    "[Networked][Codec][Hosted] a bit quantizer packs scalars and vectors"
) {
    Ref<NetwQuantizeScalar> q = bits_quantizer(8, -1.0, 1.0);

    const double eight_bit_span_error_bound = 0.01;
    NETW_CHECK_CLOSE(
        round_trip(q, 0.5, Variant::FLOAT),
        0.5,
        eight_bit_span_error_bound
    );
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
    NETW_CHECK_EQ(q->bit_width(Variant::VECTOR3), 8);
    NETW_CHECK_EQ(q->stride(Variant::VECTOR3), 3);
    NETW_CHECK_EQ(q->total_bits(Variant::VECTOR3), 24);
    NETW_CHECK_EQ(q->total_bits(Variant::VECTOR2), 16);
    NETW_CHECK_EQ(q->total_bits(Variant::FLOAT), 8);

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

TEST_CASE(
    "[Networked][Codec][Hosted] a step is a request for a grid at least that "
    "fine, and the bits it buys are spent over the whole declared range"
) {
    Ref<NetwQuantizeScalar> q = fixed_quantizer(0.5, -100.0, 100.0);

    const double granted = q->get_resolution_step();
    CHECK(granted <= 0.5);
    NETW_CHECK_EQ(q->get_bit_count(), 9);

    const double axis_error = q->max_error(Variant::FLOAT);
    CHECK(
        Vector2(round_trip(q, Vector2(10.0, -20.5), Variant::VECTOR2))
            .distance_to(Vector2(10.0, -20.5))
        <= q->max_error(Variant::VECTOR2)
    );
    CHECK(
        Vector3(round_trip(q, Vector3(10.0, -20.5, 30.0), Variant::VECTOR3))
            .distance_to(Vector3(10.0, -20.5, 30.0))
        <= q->max_error(Variant::VECTOR3)
    );
    NETW_CHECK_CLOSE(round_trip(q, 3.3, Variant::FLOAT), 3.3, axis_error);
}

TEST_CASE(
    "[Networked][Codec][Hosted] bits and step are one grid said two ways, so "
    "asking for the step a bit count buys answers that bit count back"
) {
    Ref<NetwQuantizeScalar> q = bits_quantizer(11, -100.0, 100.0);

    const double granted = q->get_resolution_step();
    q->step(granted);

    NETW_CHECK_EQ(q->get_bit_count(), 11);
    NETW_CHECK_CLOSE(q->get_resolution_step(), granted, 1e-12);
}

TEST_CASE(
    "[Networked][Codec][Hosted] the reported max error matches the resolution"
) {
    Ref<NetwQuantizeScalar> fixed = fixed_quantizer(0.5, -2048.0, 2048.0);
    const double half = fixed->get_resolution_step() * 0.5;
    CHECK(half <= 0.25);
    NETW_CHECK_CLOSE(fixed->max_error(Variant::FLOAT), half, 0.0001);
    NETW_CHECK_CLOSE(
        fixed->max_error(Variant::VECTOR2),
        half * std::sqrt(2.0),
        0.0001
    );
    NETW_CHECK_CLOSE(
        fixed->max_error(Variant::VECTOR3),
        half * std::sqrt(3.0),
        0.0001
    );

    Ref<NetwQuantizeScalar> bits = bits_quantizer(8, -1.0, 1.0);
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

    const double off_grid = 0.3123;
    const double decoded = round_trip(bits, off_grid, Variant::FLOAT);
    const bool within_bound
        = std::abs(decoded - off_grid) <= bits->max_error(Variant::FLOAT);
    CHECK(within_bound);

    Ref<NetwQuantizeAngle> angle = angle_quantizer(8, false);
    NETW_CHECK_CLOSE(
        angle->max_error(Variant::FLOAT),
        TAU_VALUE / 256.0 * 0.5,
        0.0001
    );
}

TEST_CASE("[Networked][Codec][Hosted] an angle quantizer wraps") {
    Ref<NetwQuantizeAngle> q = angle_quantizer(8, false);

    const double eight_bit_angle_error_bound = 0.03;
    NETW_CHECK_CLOSE(
        round_trip(q, PI_VALUE, Variant::FLOAT),
        PI_VALUE,
        eight_bit_angle_error_bound
    );
    NETW_CHECK_CLOSE(
        round_trip(q, TAU_VALUE, Variant::FLOAT),
        0.0,
        eight_bit_angle_error_bound
    );
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
    CHECK(
        got.get_origin().distance_to(Vector2(3.25, -4.5))
        <= q->get_origin_quantizer()->max_error(Variant::VECTOR2)
    );
    const bool rotation_ok
        = std::abs(got.get_rotation() - value.get_rotation()) < 0.002;
    CHECK(rotation_ok);
    const bool scale_ok = got.get_scale().distance_to(value.get_scale())
        <= q->get_scale_quantizer()->max_error(Variant::VECTOR2);
    CHECK(scale_ok);
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
    const bool origin_ok = got.origin.distance_to(Vector3(1.25, -2.5, 3.75))
        <= q->get_origin_quantizer()->max_error(Variant::VECTOR3);
    CHECK(origin_ok);
    const bool rotation_ok
        = angle_error(rotation, got.basis.get_rotation_quaternion())
        <= rotation_quantizer->max_error(Variant::QUATERNION);
    CHECK(rotation_ok);
    const bool scale_ok
        = got.basis.get_scale().distance_to(value.basis.get_scale())
        <= q->get_scale_quantizer()->max_error(Variant::VECTOR3);
    CHECK(scale_ok);
}

TEST_CASE(
    "[Networked][Codec][Hosted] layout equality matches the grid, however "
    "each side was authored"
) {
    Ref<NetwQuantizeScalar> a = fixed_quantizer(0.25, -10.0, 10.0);
    Ref<NetwQuantizeScalar> b = fixed_quantizer(0.25, -10.0, 10.0);
    CHECK(a->is_same_layout(b));

    Ref<NetwQuantizeScalar> c = fixed_quantizer(0.5, -10.0, 10.0);
    CHECK_FALSE(a->is_same_layout(c));

    Ref<NetwQuantizeScalar> by_bits
        = bits_quantizer(a->get_bit_count(), -10.0, 10.0);
    CHECK(a->is_same_layout(by_bits));

    Ref<NetwQuantizeScalar> elsewhere = bits_quantizer(8, -10.0, 10.0);
    CHECK_FALSE(a->is_same_layout(elsewhere));
    CHECK_FALSE(a->is_same_layout(Ref<NetwQuantize>()));
}

TEST_CASE(
    "[Networked][Codec][Hosted] a bit quantizer round trips its limits and "
    "rest point"
) {
    for (int count : {2, 4, 8, 16}) {
        Ref<NetwQuantizeScalar> q = bits_quantizer(count, -1.0, 1.0);
        const double low = round_trip(q, -1.0, Variant::FLOAT);
        const double high = round_trip(q, 1.0, Variant::FLOAT);
        const double rest = round_trip(q, 0.0, Variant::FLOAT);

        NETW_CHECK_CLOSE(low, -1.0, 0.000001);
        NETW_CHECK_CLOSE(high, 1.0, 0.000001);
        NETW_CHECK_CLOSE(rest, 0.0, 0.000001);
    }

    Ref<NetwQuantizeScalar> thin = bits_quantizer(1, -1.0, 1.0);
    NETW_CHECK_CLOSE(round_trip(thin, -1.0, Variant::FLOAT), -1.0, 1e-6);
    NETW_CHECK_CLOSE(round_trip(thin, 1.0, Variant::FLOAT), 1.0, 1e-6);
}

TEST_CASE(
    "[Networked][Codec][Hosted] a bit quantizer round trips an asymmetric "
    "range's limits, with no rest point to protect"
) {
    Ref<NetwQuantizeScalar> q = bits_quantizer(8, 0.0, 100.0);

    NETW_CHECK_CLOSE(round_trip(q, 0.0, Variant::FLOAT), 0.0, 0.000001);
    NETW_CHECK_CLOSE(round_trip(q, 100.0, Variant::FLOAT), 100.0, 1e-4);
}

TEST_CASE(
    "[Networked][Codec][Hosted] the endpoint error shrinks across bit depths, "
    "so a defect invisible at pose depths is decisive at latch depths"
) {
    double previous = 1.0;
    for (int count : {4, 8, 16, 24}) {
        Ref<NetwQuantizeScalar> q = bits_quantizer(count, -1.0, 1.0);
        const double high = round_trip(q, 1.0, Variant::FLOAT);
        const double error = std::abs(1.0 - high);
        const bool error_shrinks = error <= previous;
        CHECK(error_shrinks);
        previous = error;
    }
}

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
        const bool same_rotation
            = std::abs(shortest_arc(wide, centered_value)) < 0.0005;
        CHECK(same_rotation);
        const bool round_trips
            = std::abs(shortest_arc(value, centered_value)) < 0.0005;
        CHECK(round_trips);
        const bool inside_euler_range = centered_value > -PI_VALUE - 0.0001
            && centered_value <= PI_VALUE + 0.0001;
        CHECK(inside_euler_range);
    }

    NETW_CHECK_CLOSE(
        round_trip(centered, PI_VALUE, Variant::FLOAT),
        PI_VALUE,
        0.0005
    );
    NETW_CHECK_CLOSE(
        round_trip(centered, -PI_VALUE, Variant::FLOAT),
        PI_VALUE,
        0.0005
    );

    const double step = TAU_VALUE / 65536.0;
    const double below = round_trip(centered, PI_VALUE - step, Variant::FLOAT);
    const double above = round_trip(centered, -PI_VALUE + step, Variant::FLOAT);
    const bool neighbours_stay_close
        = std::abs(shortest_arc(below, above)) < 3.0 * step;
    CHECK(neighbours_stay_close);
}

TEST_CASE(
    "[Networked][Codec][Hosted] Q2 a built-in quantizer answers its own codes, "
    "because only an object carrying a script enters the script hop and the "
    "unimplemented hop answers a stride of one and a code of zero"
) {
    Ref<NetwQuantizeScalar> built_in = bits_quantizer(8, -1.0, 1.0);
    NETW_CHECK_EQ(built_in->bit_width(Variant::VECTOR3), 8);
    NETW_CHECK_EQ(built_in->stride(Variant::VECTOR3), 3);
    NETW_CHECK_EQ(built_in->encode(Vector3(1.0, -1.0, 0.0), 0), 254);
    NETW_CHECK_EQ(built_in->encode(Vector3(1.0, -1.0, 0.0), 1), 0);

    Ref<NetwQuantize> bare;
    bare.instantiate();
    NETW_CHECK_EQ(bare->bit_width(Variant::VECTOR3), 0);
    NETW_CHECK_EQ(bare->stride(Variant::VECTOR3), 1);
    NETW_CHECK_EQ(bare->encode(Vector3(1.0, -1.0, 0.0), 0), 0);
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Codec] Q1 a script quantizer is asked for one code per "
    "element and is handed no stream, so the row it fills is the core's"
) {
    const Ref<Script> script = netw_test::script_from(
        "res://tests/support/chains/quantize_counting.gd"
    );
    REQUIRE(script.is_valid());

    Ref<NetwQuantize> counting;
    counting.instantiate();
    counting->set_script(script);

    NETW_CHECK_EQ(counting->bit_width(Variant::VECTOR2), 10);
    NETW_CHECK_EQ(counting->stride(Variant::VECTOR2), 2);
    NETW_CHECK_EQ(counting->total_bits(Variant::VECTOR2), 20);

    netw::table::SchemaRecord schema;
    schema.name = StringName("ScriptQuantized");
    netw::SchemaCore::append_column(
        &schema,
        StringName("move"),
        netw::SchemaCore::VECTOR2,
        1
    );
    netw::SchemaCore::assign_quantizer(&schema, 0, counting);
    REQUIRE(netw::SchemaCore::fix(&schema) == Error::OK);

    const netw::wire::WirePlan plan = netw::wire::WirePlan::compile(schema);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column(0).width, 10);
    NETW_CHECK_EQ(plan.column(0).stride, 2);

    Array values;
    values.push_back(Vector2(7.0, 9.0));
    netw::wire::CodeRow row;
    REQUIRE(netw::wire::encode_scalar_row(schema, values, row));

    NETW_CHECK_EQ(int64_t(counting->get("encodes")), 2);
    NETW_CHECK_EQ(row.read(plan.column(0), 0), 7);
    NETW_CHECK_EQ(row.read(plan.column(0), 1), 9);

    Array decoded;
    REQUIRE(netw::wire::decode_scalar_row(schema, row, decoded));
    NETW_CHECK_EQ(int64_t(counting->get("decodes")), 1);
    NETW_CHECK_EQ(int64_t(counting->get("encodes")), 2);
    CHECK(Vector2(decoded[0]) == Vector2(7.0, 9.0));
}

#endif

} // namespace TestNetwQuantize
