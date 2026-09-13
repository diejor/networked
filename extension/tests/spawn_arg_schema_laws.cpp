#include "support/netw_test.h"

#include <cmath>
#include <initializer_list>

#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/call_args.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSpawnArgSchemaLaws {

using namespace godot;
using netw::NetwQuantizeScalar;
using netw::NetwSceneCore;

Array one_of(const Variant &value) {
    Array out;
    out.append(value);
    return out;
}

Array types_of(const std::initializer_list<Variant::Type> &declared) {
    Array out;
    for (const Variant::Type type : declared) {
        out.append(int(type));
    }
    return out;
}

PackedByteArray written(
    const Array &values,
    const Array &quantizers,
    const Array &types
) {
    netw::wire::WriteStream stream;
    if (!netw::call_args::values_write(stream, values, quantizers, types)
        || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

Array read_back(
    const PackedByteArray &bytes,
    const Array &quantizers,
    const Array &types
) {
    netw::wire::ReadStream stream(bytes);
    Array out;
    if (!netw::call_args::values_read(stream, quantizers, types, out)) {
        return Array();
    }
    return out;
}

Ref<NetwQuantizeScalar> an_axis_quantizer() {
    Ref<NetwQuantizeScalar> made;
    made.instantiate();
    made->set_bit_count(10);
    made->set_min_limit(-1.0);
    made->set_max_limit(1.0);
    return made;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SA1 a recipe that declares no quantizer "
    "declares nothing the bytes depend on, so a constructor whose host "
    "carries no Script encodes exactly what a reflected schema would"
) {
    Array values;
    values.append(String("arena"));
    values.append(int64_t(5));
    const Array no_quantizers;

    const PackedByteArray undeclared = written(values, no_quantizers, Array());
    const PackedByteArray declared = written(
        values,
        no_quantizers,
        types_of({Variant::STRING, Variant::INT})
    );

    const bool same_bytes = undeclared == declared;
    CHECK(same_bytes);

    const Array by_nothing = read_back(undeclared, no_quantizers, Array());
    const Array by_types = read_back(
        undeclared,
        no_quantizers,
        types_of({Variant::STRING, Variant::INT})
    );
    REQUIRE(by_nothing.size() == 2);
    REQUIRE(by_types.size() == 2);
    CHECK(String(by_nothing[0]) == String("arena"));
    CHECK(String(by_types[0]) == String("arena"));
    NETW_CHECK_EQ(int(int64_t(by_nothing[1])), 5);
    NETW_CHECK_EQ(int(int64_t(by_types[1])), 5);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SA2 a declared quantizer is the bytes, so an "
    "argument schema is a recipe's own law and two peers holding different "
    "schemas for one id read different values out of one frame"
) {
    const Vector2 sent(0.5, -0.25);
    const Array values = one_of(sent);
    const Array quantized = one_of(an_axis_quantizer());
    const Array declared = types_of({Variant::VECTOR2});

    const PackedByteArray by_schema = written(values, quantized, declared);
    const PackedByteArray by_nothing = written(values, Array(), Array());

    const bool schemas_disagree = !(by_schema == by_nothing);
    CHECK(schemas_disagree);

    const Array round_trip = read_back(by_schema, quantized, declared);
    REQUIRE(round_trip.size() == 1);
    const Vector2 got = round_trip[0];
    const bool within_a_step
        = std::abs(got.x - sent.x) < 0.01 && std::abs(got.y - sent.y) < 0.01;
    CHECK(within_a_step);

    const Array raw_trip = read_back(by_nothing, Array(), Array());
    REQUIRE(raw_trip.size() == 1);
    const Vector2 raw_got = raw_trip[0];
    const bool raw_is_exact = raw_got == sent;
    CHECK(raw_is_exact);

    const Array mismatched = read_back(by_schema, Array(), Array());
    bool mismatch_loses_the_value = mismatched.size() != 1;
    if (!mismatch_loses_the_value) {
        const Vector2 misread = mismatched[0];
        mismatch_loses_the_value = !(misread == sent);
    }
    CHECK(mismatch_loses_the_value);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SA3 a quantizer the declared type does not "
    "admit is not silently applied, which is why the type travels with the "
    "quantizer rather than being inferred from the value alone"
) {
    const Array values = one_of(Vector2(0.5, -0.25));
    const Array quantized = one_of(an_axis_quantizer());

    const PackedByteArray engaged
        = written(values, quantized, types_of({Variant::VECTOR2}));
    const PackedByteArray refused
        = written(values, quantized, types_of({Variant::STRING}));
    const PackedByteArray unquantized = written(values, Array(), Array());

    const bool refusal_is_the_raw_encoding = refused == unquantized;
    CHECK(refusal_is_the_raw_encoding);
    const bool engagement_differs = !(engaged == unquantized);
    CHECK(engagement_differs);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA4 one member of the isolation enum means a "
    "world of its own, so the viewport wrap and any policy that branches "
    "on isolation cannot disagree about which one it is"
) {
    CHECK(
        NetwSceneCore::isolation_owns_world(NetwSceneCore::ISOLATION_OWN_WORLD)
    );
    CHECK(!NetwSceneCore::isolation_owns_world(NetwSceneCore::ISOLATION_NONE));
    CHECK(!NetwSceneCore::isolation_owns_world(-1));
    CHECK(!NetwSceneCore::isolation_owns_world(7));
}

} // namespace TestNetwSpawnArgSchemaLaws
