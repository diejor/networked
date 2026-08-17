#include "support/netw_test.h"

#include "godot/spatial_node.hpp"
#include "netw/display_port.hpp"

namespace TestNetwDisplayPort {

using namespace godot;
using netw::NetwDisplayPort;

constexpr double HALF_TURN = 3.1415926535897932384626433833;

Ref<NetwDisplayPort> make_port(
    Object *p_target,
    Object *p_host,
    const StringName &p_target_prop,
    const StringName &p_source_prop,
    bool p_global_space
) {
    Ref<NetwDisplayPort> port;
    port.instantiate();
    port->bind(p_target, p_host);
    port->declare(p_target_prop, p_source_prop, p_global_space);
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

    Ref<NetwDisplayPort> port
        = make_port(visual, body, "position", "position", true);
    NETW_CHECK_EQ(
        port->write(Vector2(5.0, 0.0)),
        int64_t(NetwDisplayPort::WRITE_GLOBAL)
    );
    CHECK(visual->get_global_position().is_equal_approx(Vector2(105.0, 0.0)));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D3 2D global rotation composes "
    "the host's parent rotation"
) {
    Node2D *holder = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_rotation(HALF_TURN * 0.5);

    Ref<NetwDisplayPort> port
        = make_port(visual, body, "rotation", "rotation", true);
    NETW_CHECK_EQ(
        port->write(HALF_TURN * 0.25),
        int64_t(NetwDisplayPort::WRITE_GLOBAL)
    );
    CHECK(Math::is_equal_approx(
        double(visual->get_global_rotation()),
        HALF_TURN * 0.75
    ));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D4 a source already in global "
    "space passes through whatever the host's parent is"
) {
    Node2D *holder = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    holder->add_child(body);
    body->add_child(visual);
    holder->set_position(Vector2(100.0, 0.0));

    Ref<NetwDisplayPort> port
        = make_port(visual, body, "position", "global_position", true);
    NETW_CHECK_EQ(
        port->write(Vector2(5.0, 0.0)),
        int64_t(NetwDisplayPort::WRITE_GLOBAL)
    );
    CHECK(visual->get_global_position().is_equal_approx(Vector2(5.0, 0.0)));

    memdelete(holder);
}

TEST_CASE(
    "[Networked][Display][Hosted] D5 a global-space channel with "
    "no global setter here is written locally and says so"
) {
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    body->add_child(visual);

    Ref<NetwDisplayPort> port
        = make_port(visual, body, "scale", "scale", true);
    NETW_CHECK_EQ(
        port->write(Vector2(2.0, 2.0)),
        int64_t(NetwDisplayPort::WRITE_REFUSED)
    );
    CHECK(visual->get_scale().is_equal_approx(Vector2(2.0, 2.0)));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Display][Hosted] D6 a channel not in global space "
    "is a plain property write"
) {
    Node2D *visual = memnew(Node2D);

    Ref<NetwDisplayPort> port
        = make_port(visual, nullptr, "position", "position", false);
    NETW_CHECK_EQ(
        port->write(Vector2(3.0, 4.0)),
        int64_t(NetwDisplayPort::WRITE_LOCAL)
    );
    CHECK(visual->get_position().is_equal_approx(Vector2(3.0, 4.0)));

    memdelete(visual);
}

TEST_CASE(
    "[Networked][Display][Hosted] D7 a freed target unbinds the "
    "port instead of writing through a dangling handle"
) {
    Node2D *visual = memnew(Node2D);

    Ref<NetwDisplayPort> port
        = make_port(visual, nullptr, "position", "position", false);
    CHECK(port->is_bound());
    memdelete(visual);

    NETW_CHECK_EQ(
        port->write(Vector2(1.0, 1.0)),
        int64_t(NetwDisplayPort::WRITE_UNBOUND)
    );
    CHECK(!port->is_bound());
    NETW_CHECK_EQ(port->get_lost(), int64_t(1));
}

} // namespace TestNetwDisplayPort
