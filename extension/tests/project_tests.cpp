#include "support/netw_test.h"

#include "netw/project.hpp"

namespace TestNetwProject {

using namespace godot;
namespace project = netw::project;

constexpr double HALF_TURN = 3.1415926535897932384626433833;

TEST_CASE(
    "[Networked][Display][Hosted] a linear value advances by velocity times age"
) {
    NETW_CHECK_CLOSE(double(project::forward(2.0, 3.0, 0.5)), 3.5, 0.0001);

    const Vector2 planar
        = project::forward(Vector2(1.0, 1.0), Vector2(4.0, 0.0), 0.25);
    CHECK(planar.is_equal_approx(Vector2(2.0, 1.0)));

    const Vector3 spatial
        = project::forward(Vector3(), Vector3(0.0, 10.0, 0.0), 0.1);
    CHECK(spatial.is_equal_approx(Vector3(0.0, 1.0, 0.0)));
}

TEST_CASE("[Networked][Display][Hosted] a zero velocity holds the value") {
    const Vector2 held = project::forward(Vector2(7.0, 3.0), Vector2(), 2.0);
    CHECK(held.is_equal_approx(Vector2(7.0, 3.0)));
}

TEST_CASE(
    "[Networked][Display][Hosted] a quaternion turns by its angular velocity"
) {
    const Vector3 omega(0.0, 0.0, HALF_TURN * 0.5);
    const Quaternion turned = project::forward(Quaternion(), omega, 0.25);
    const Quaternion expected(Vector3(0.0, 0.0, 1.0), HALF_TURN * 0.125);
    NETW_CHECK_CLOSE(turned.angle_to(expected), 0.0, 0.0001);
}

TEST_CASE(
    "[Networked][Display][Hosted] only a type carrying a derivative projects"
) {
    CHECK(project::supports(Variant::FLOAT));
    CHECK(project::supports(Variant::VECTOR2));
    CHECK(project::supports(Variant::VECTOR3));
    CHECK(project::supports(Variant::QUATERNION));
    CHECK_FALSE(project::supports(Variant::COLOR));
    CHECK_FALSE(project::supports(Variant::INT));
    CHECK_FALSE(project::supports(Variant::STRING));
}

TEST_CASE(
    "[Networked][Display][Hosted] an unprojectable value comes back unchanged"
) {
    CHECK(int(project::forward(7, 3, 2.0)) == 7);
    CHECK(bool(String(project::forward("held", "moved", 1.0)) == "held"));
}

} // namespace TestNetwProject
