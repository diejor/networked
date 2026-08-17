#include "support/netw_test.h"

#include "netw/display_offset.hpp"
#include "netw/interpolate.hpp"

namespace TestNetwDisplayOffset {

using namespace godot;
using netw::NetwDisplayOffset;
using netw::NetwInterpolate;

Ref<NetwDisplayOffset> make_offset() {
    Ref<NetwDisplayOffset> offset;
    offset.instantiate();
    return offset;
}

TEST_CASE(
    "[Networked][Display][Hosted] O1 an armed offset seeds itself against the "
    "first target the new source produces"
) {
    Ref<NetwDisplayOffset> offset = make_offset();
    CHECK(!offset->is_held());

    offset->arm(true);
    CHECK(offset->is_armed());

    const Variant shown = offset->apply(
        Vector2(0.0, 0.0),
        1.0,
        INFINITY,
        Vector2(10.0, 0.0),
        int64_t(NetwInterpolate::MODE_LERP)
    );

    CHECK(!offset->is_armed());
    CHECK(offset->is_held());
    CHECK(Vector2(shown).is_equal_approx(Vector2(10.0, 0.0)));
}

TEST_CASE(
    "[Networked][Display][Hosted] O2 a held offset decays by the glide and is "
    "dropped once it is spent"
) {
    Ref<NetwDisplayOffset> offset = make_offset();
    offset->absorb(Vector2(10.0, 0.0), INFINITY);
    CHECK(Vector2(offset->held()).is_equal_approx(Vector2(-10.0, 0.0)));

    const Variant half = offset->apply(
        Vector2(0.0, 0.0),
        0.5,
        INFINITY,
        Vector2(),
        int64_t(NetwInterpolate::MODE_LERP)
    );
    CHECK(Vector2(half).is_equal_approx(Vector2(-5.0, 0.0)));

    for (int at = 0; at < 64; ++at) {
        offset->apply(
            Vector2(0.0, 0.0),
            0.5,
            INFINITY,
            Vector2(),
            int64_t(NetwInterpolate::MODE_LERP)
        );
    }
    CHECK(!offset->is_held());
}

TEST_CASE(
    "[Networked][Display][Hosted] O3 an absorption composes onto what has not "
    "decayed yet rather than replacing it"
) {
    Ref<NetwDisplayOffset> offset = make_offset();
    offset->absorb(Vector2(10.0, 0.0), INFINITY);
    offset->apply(
        Vector2(),
        0.5,
        INFINITY,
        Vector2(),
        int64_t(NetwInterpolate::MODE_LERP)
    );
    CHECK(Vector2(offset->held()).is_equal_approx(Vector2(-5.0, 0.0)));

    offset->absorb(Vector2(10.0, 0.0), INFINITY);

    CHECK(Vector2(offset->held()).is_equal_approx(Vector2(-15.0, 0.0)));
}

TEST_CASE(
    "[Networked][Display][Hosted] O4 the clamp is what stops an offset showing "
    "a pose a teleport was entitled to snap through"
) {
    Ref<NetwDisplayOffset> offset = make_offset();

    offset->absorb(Vector2(100.0, 0.0), 4.0);

    CHECK(Vector2(offset->held()).is_equal_approx(Vector2(-4.0, 0.0)));
}

TEST_CASE(
    "[Networked][Display][Hosted] O5 a cleared offset writes the raw value, "
    "because a genuine desync should be seen to snap"
) {
    Ref<NetwDisplayOffset> offset = make_offset();
    offset->absorb(Vector2(10.0, 0.0), INFINITY);
    offset->arm(true);

    offset->clear();

    CHECK(!offset->is_held());
    CHECK(!offset->is_armed());
    const Variant shown = offset->apply(
        Vector2(7.0, 0.0),
        0.5,
        INFINITY,
        Vector2(99.0, 0.0),
        int64_t(NetwInterpolate::MODE_LERP)
    );
    CHECK(Vector2(shown).is_equal_approx(Vector2(7.0, 0.0)));
}

TEST_CASE(
    "[Networked][Display][Hosted] O6 a circular channel takes its residual the "
    "short way around, whatever shape carries the angle"
) {
    Ref<NetwDisplayOffset> angle = make_offset();
    angle->arm(true);
    angle->apply(
        double(0.1),
        1.0,
        INFINITY,
        double(6.2),
        int64_t(NetwInterpolate::MODE_ANGLE)
    );
    NETW_CHECK_LT(double(angle->held()), 0.0);
    NETW_CHECK_GT(double(angle->held()), -0.2);

    Ref<NetwDisplayOffset> linear = make_offset();
    linear->arm(true);
    linear->apply(
        double(0.1),
        1.0,
        INFINITY,
        double(6.2),
        int64_t(NetwInterpolate::MODE_LERP)
    );
    NETW_CHECK_GT(double(linear->held()), 6.0);

    Ref<NetwDisplayOffset> spun = make_offset();
    spun->arm(true);
    const Quaternion displayed(Vector3(0.0, 1.0, 0.0), Math::deg_to_rad(170.0));
    const Quaternion target(Vector3(0.0, 1.0, 0.0), Math::deg_to_rad(-170.0));
    const Quaternion recomposed = spun->apply(
        target,
        1.0,
        INFINITY,
        displayed,
        int64_t(NetwInterpolate::MODE_SLERP)
    );
    NETW_CHECK_GT(Math::abs(double(recomposed.dot(displayed))), 0.9999);
}

TEST_CASE(
    "[Networked][Display][Hosted] O7 a value shape with no offset arithmetic "
    "holds none, so its writes pass through"
) {
    Ref<NetwDisplayOffset> offset = make_offset();

    offset->absorb(String("nudged"), INFINITY);

    CHECK(!offset->is_held());
    const Variant shown = offset->apply(
        String("shown"),
        0.5,
        INFINITY,
        String("was"),
        int64_t(NetwInterpolate::MODE_LERP)
    );
    CHECK(String(shown) == String("shown"));
}

} // namespace TestNetwDisplayOffset
