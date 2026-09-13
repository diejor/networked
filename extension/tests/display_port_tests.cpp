#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/display/port.hpp"

namespace TestNetwPort {

using namespace godot;
using netw::display::Port;

constexpr double HALF_TURN = 3.1415926535897932384626433833;

Port make_port(
    Object *p_target,
    Object *p_host,
    const StringName &p_target_prop,
    const StringName &p_source_prop,
    bool p_global_space
) {
    Port port;
    port.bind(p_target, p_host);
    port.declare(p_target_prop, p_source_prop, p_global_space);
    return port;
}

TEST_CASE(
    "[Networked][Display][Hosted] D1 a parented visual's position is written "
    "in the host's frame"
) {
    Node2D *holder = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_position(Vector2(100.0, 0.0));

    Port port = make_port(visual, body, "position", "position", true);
    NETW_CHECK_EQ(port.write(Vector2(5.0, 0.0)), int64_t(Port::WRITE_GLOBAL));
    CHECK(visual->get_global_position().is_equal_approx(Vector2(105.0, 0.0)));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D3 2D global rotation composes the host's "
    "parent rotation"
) {
    Node2D *holder = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_rotation(HALF_TURN * 0.5);

    Port port = make_port(visual, body, "rotation", "rotation", true);
    NETW_CHECK_EQ(port.write(HALF_TURN * 0.25), int64_t(Port::WRITE_GLOBAL));
    CHECK(
        Math::is_equal_approx(
            double(visual->get_global_rotation()),
            HALF_TURN * 0.75
        )
    );

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D4 a source already in global space passes "
    "through whatever the host's parent is"
) {
    Node2D *holder = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_position(Vector2(100.0, 0.0));

    Port port = make_port(visual, body, "position", "global_position", true);
    NETW_CHECK_EQ(port.write(Vector2(5.0, 0.0)), int64_t(Port::WRITE_GLOBAL));
    CHECK(visual->get_global_position().is_equal_approx(Vector2(5.0, 0.0)));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D5 a global-space channel with no global "
    "setter here is written locally and says so"
) {
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    body->add_child(visual);

    Port port = make_port(visual, body, "scale", "scale", true);
    NETW_CHECK_EQ(port.write(Vector2(2.0, 2.0)), int64_t(Port::WRITE_REFUSED));
    CHECK(visual->get_scale().is_equal_approx(Vector2(2.0, 2.0)));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Display][Hosted] D6 a channel not in global space is a plain "
    "property write"
) {
    Node2D *visual = memnew(Node2D);

    Port port = make_port(visual, nullptr, "position", "position", false);
    NETW_CHECK_EQ(port.write(Vector2(3.0, 4.0)), int64_t(Port::WRITE_LOCAL));
    CHECK(visual->get_position().is_equal_approx(Vector2(3.0, 4.0)));

    memdelete(visual);
}

TEST_CASE(
    "[Networked][Display][Hosted][SceneTree] D7 a freed target unbinds the "
    "port instead of writing through a dangling handle"
) {
    Node2D *visual = memnew(Node2D);

    Port port = make_port(visual, nullptr, "position", "position", false);
    CHECK(port.is_bound());
    memdelete(visual);

    NETW_CHECK_EQ(port.write(Vector2(1.0, 1.0)), int64_t(Port::WRITE_UNBOUND));
    CHECK(!port.is_bound());
    NETW_CHECK_EQ(port.get_lost(), int64_t(1));
}

struct SpatialHosts {
    Node3D *holder = nullptr;
    Node3D *body = nullptr;
    Node3D *visual = nullptr;
};

SpatialHosts seat_spatial_hosts() {
    SpatialHosts hosts;
    hosts.holder = memnew(Node3D);
    hosts.body = memnew(Node3D);
    hosts.visual = memnew(Node3D);
    hosts.holder->add_child(hosts.body);
    hosts.body->add_child(hosts.visual);
    netw::gd::scene_root()->add_child(hosts.holder);
    return hosts;
}

void release_spatial_hosts(const SpatialHosts &p_hosts) {
    netw::gd::scene_root()->remove_child(p_hosts.holder);
    memdelete(p_hosts.holder);
}

TEST_CASE(
    "[Networked][Display][Hosted][SceneTree] D8 a parented visual's 3D "
    "position is written in the host's frame"
) {
    CHECK(netw::gd::scene_root() != nullptr);
    if (netw::gd::scene_root() == nullptr) {
        return;
    }
    const SpatialHosts hosts = seat_spatial_hosts();
    hosts.holder->set_position(Vector3(0.0, 10.0, 0.0));

    Port port
        = make_port(hosts.visual, hosts.body, "position", "position", true);
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, 2.0, 0.0)),
        int64_t(Port::WRITE_GLOBAL)
    );
    CHECK(hosts.visual->get_global_position().is_equal_approx(
        Vector3(0.0, 12.0, 0.0)
    ));

    release_spatial_hosts(hosts);
}

TEST_CASE(
    "[Networked][Display][Hosted][SceneTree] D9 3D global rotation is "
    "converted rather than refused"
) {
    CHECK(netw::gd::scene_root() != nullptr);
    if (netw::gd::scene_root() == nullptr) {
        return;
    }
    const SpatialHosts hosts = seat_spatial_hosts();
    hosts.holder->set_rotation(Vector3(0.0, HALF_TURN * 0.5, 0.0));

    Port port
        = make_port(hosts.visual, hosts.body, "rotation", "rotation", true);
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, HALF_TURN * 0.25, 0.0)),
        int64_t(Port::WRITE_GLOBAL)
    );
    CHECK(hosts.visual->get_global_rotation().is_equal_approx(
        Vector3(0.0, HALF_TURN * 0.75, 0.0)
    ));

    release_spatial_hosts(hosts);
}

TEST_CASE(
    "[Networked][Display][Hosted][SceneTree] D10 a 3D source already in global "
    "space passes through whatever the host's parent is"
) {
    CHECK(netw::gd::scene_root() != nullptr);
    if (netw::gd::scene_root() == nullptr) {
        return;
    }
    const SpatialHosts hosts = seat_spatial_hosts();
    hosts.holder->set_position(Vector3(0.0, 10.0, 0.0));

    Port port = make_port(
        hosts.visual,
        hosts.body,
        "position",
        "global_position",
        true
    );
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, 2.0, 0.0)),
        int64_t(Port::WRITE_GLOBAL)
    );
    CHECK(hosts.visual->get_global_position().is_equal_approx(
        Vector3(0.0, 2.0, 0.0)
    ));

    release_spatial_hosts(hosts);
}

TEST_CASE(
    "[Networked][Display][Hosted][SceneTree] D11 a 3D global-space channel "
    "with no global setter is written locally and says so"
) {
    CHECK(netw::gd::scene_root() != nullptr);
    if (netw::gd::scene_root() == nullptr) {
        return;
    }
    const SpatialHosts hosts = seat_spatial_hosts();

    Port port = make_port(hosts.visual, hosts.body, "scale", "scale", true);
    NETW_CHECK_EQ(
        port.write(Vector3(2.0, 2.0, 2.0)),
        int64_t(Port::WRITE_REFUSED)
    );
    CHECK(hosts.visual->get_scale().is_equal_approx(Vector3(2.0, 2.0, 2.0)));

    release_spatial_hosts(hosts);
}

TEST_CASE(
    "[Networked][Display][Hosted] D12 an unmounted 3D visual takes its "
    "position in the only frame it has"
) {
    Node3D *holder = memnew(Node3D);
    Node3D *body = memnew(Node3D);
    Node3D *visual = memnew(Node3D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_position(Vector3(0.0, 10.0, 0.0));

    Port port = make_port(visual, body, "position", "position", true);
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, 2.0, 0.0)),
        int64_t(Port::WRITE_LOCAL)
    );
    CHECK(visual->get_position().is_equal_approx(Vector3(0.0, 2.0, 0.0)));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D13 an unmounted 3D visual takes its "
    "rotation in the only frame it has"
) {
    Node3D *holder = memnew(Node3D);
    Node3D *body = memnew(Node3D);
    Node3D *visual = memnew(Node3D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_rotation(Vector3(0.0, HALF_TURN * 0.5, 0.0));

    Port port = make_port(visual, body, "rotation", "rotation", true);
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, HALF_TURN * 0.25, 0.0)),
        int64_t(Port::WRITE_LOCAL)
    );
    CHECK(visual->get_rotation().is_equal_approx(
        Vector3(0.0, HALF_TURN * 0.25, 0.0)
    ));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D14 a channel naming the global property "
    "writes the same frame as one naming the local property"
) {
    Node3D *holder = memnew(Node3D);
    Node3D *body = memnew(Node3D);
    Node3D *visual = memnew(Node3D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_position(Vector3(0.0, 10.0, 0.0));

    Port port = make_port(visual, body, "global_position", "position", true);
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, 2.0, 0.0)),
        int64_t(Port::WRITE_LOCAL)
    );
    CHECK(visual->get_position().is_equal_approx(Vector3(0.0, 2.0, 0.0)));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D15 a channel naming the global rotation "
    "writes the same frame as one naming the local rotation"
) {
    Node3D *holder = memnew(Node3D);
    Node3D *body = memnew(Node3D);
    Node3D *visual = memnew(Node3D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_rotation(Vector3(0.0, HALF_TURN * 0.5, 0.0));

    Port port = make_port(visual, body, "global_rotation", "rotation", true);
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, HALF_TURN * 0.25, 0.0)),
        int64_t(Port::WRITE_LOCAL)
    );
    CHECK(visual->get_rotation().is_equal_approx(
        Vector3(0.0, HALF_TURN * 0.25, 0.0)
    ));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted][SceneTree] D16 a mounted channel naming the "
    "global property composes the host's frame and says it wrote globally"
) {
    CHECK(netw::gd::scene_root() != nullptr);
    if (netw::gd::scene_root() == nullptr) {
        return;
    }
    const SpatialHosts hosts = seat_spatial_hosts();
    hosts.holder->set_position(Vector3(0.0, 10.0, 0.0));

    Port port = make_port(
        hosts.visual,
        hosts.body,
        "global_position",
        "position",
        true
    );
    NETW_CHECK_EQ(
        port.write(Vector3(0.0, 2.0, 0.0)),
        int64_t(Port::WRITE_GLOBAL)
    );
    CHECK(hosts.visual->get_global_position().is_equal_approx(
        Vector3(0.0, 12.0, 0.0)
    ));

    release_spatial_hosts(hosts);
}

} // namespace TestNetwPort
