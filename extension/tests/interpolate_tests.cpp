// Laws for NetwInterpolate, one channel's smoothing declaration.
//
// The builders are the authoring surface, so each one selects its field and
// answers the same spec back for chaining. Equality is by value, because a
// freshly built spec equal to a stored one must be recognized as the same
// declaration and re-applying it must change nothing.

#include "support/netw_test.h"

#include "netw/api/interpolate.hpp"

namespace TestNetwInterpolate {

using namespace godot;
using netw::NetwInterpolate;

Ref<NetwInterpolate> make_spec() {
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    return spec;
}

TEST_CASE("[Networked][Display][Hosted] a fresh spec lerps and smooths") {
    Ref<NetwInterpolate> spec = make_spec();
    NETW_CHECK_EQ(int(spec->get_mode()), int(NetwInterpolate::MODE_LERP));
    NETW_CHECK_EQ(int(spec->get_forecast_tail()), int(NetwInterpolate::TAIL_AUTO));
    NETW_CHECK_CLOSE(spec->get_smoothing(), 0.05, 0.0001);
    NETW_CHECK_CLOSE(spec->get_snap_distance(), 0.0, 0.0001);
}

TEST_CASE("[Networked][Display][Hosted] every builder answers the same spec") {
    Ref<NetwInterpolate> spec = make_spec();
    CHECK(spec->none() == spec);
    NETW_CHECK_EQ(int(spec->get_mode()), int(NetwInterpolate::MODE_NONE));
    CHECK(spec->angle() == spec);
    NETW_CHECK_EQ(int(spec->get_mode()), int(NetwInterpolate::MODE_ANGLE));
    CHECK(spec->slerp() == spec);
    NETW_CHECK_EQ(int(spec->get_mode()), int(NetwInterpolate::MODE_SLERP));
    CHECK(spec->lerp() == spec);
    NETW_CHECK_EQ(int(spec->get_mode()), int(NetwInterpolate::MODE_LERP));

    CHECK(spec->smooth(0.2) == spec);
    NETW_CHECK_CLOSE(spec->get_smoothing(), 0.2, 0.0001);
    CHECK(spec->snap_at(10.0) == spec);
    NETW_CHECK_CLOSE(spec->get_snap_distance(), 10.0, 0.0001);
    CHECK(spec->to(StringName("position")) == spec);
    CHECK(bool(spec->get_target() == StringName("position")));
}

TEST_CASE(
    "[Networked][Display][Hosted] holding refuses the tail and a derivative "
    "channel restores it"
) {
    Ref<NetwInterpolate> spec = make_spec();
    spec->hold();
    NETW_CHECK_EQ(int(spec->get_forecast_tail()), int(NetwInterpolate::TAIL_HOLD));

    spec->project_by(StringName("velocity"));
    CHECK(bool(spec->get_project_channel() == StringName("velocity")));
    NETW_CHECK_EQ(int(spec->get_forecast_tail()), int(NetwInterpolate::TAIL_AUTO));
}

TEST_CASE("[Networked][Display][Hosted] two specs are the same by value") {
    Ref<NetwInterpolate> spec = make_spec();
    spec->angle()->smooth(0.05)->snap_at(1.5)->to(StringName("rotation"));

    Ref<NetwInterpolate> twin = make_spec();
    twin->angle()->smooth(0.05)->snap_at(1.5)->to(StringName("rotation"));
    CHECK(spec->is_same_spec(twin));

    twin->smooth(0.1);
    CHECK_FALSE(spec->is_same_spec(twin));

    twin->smooth(0.05)->hold();
    CHECK_FALSE(spec->is_same_spec(twin));

    CHECK_FALSE(spec->is_same_spec(Ref<NetwInterpolate>()));
}

TEST_CASE("[Networked][Display][Hosted] a spec smooths only what it can lerp") {
    Ref<NetwInterpolate> spec = make_spec();
    CHECK(spec->supports_type(Variant::FLOAT));
    CHECK(spec->supports_type(Variant::VECTOR2));
    CHECK(spec->supports_type(Variant::VECTOR3));
    CHECK(spec->supports_type(Variant::QUATERNION));
    CHECK(spec->supports_type(Variant::COLOR));
    CHECK_FALSE(spec->supports_type(Variant::INT));
    CHECK_FALSE(spec->supports_type(Variant::STRING));
    CHECK_FALSE(spec->supports_type(Variant::TRANSFORM3D));
}

} // namespace TestNetwInterpolate
