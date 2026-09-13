#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "godot/spatial_node.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/port.hpp"

namespace TestNetwChannel {

using namespace godot;
using netw::NetwInterpolate;
using netw::display::Channel;

TEST_CASE(
    "[Networked][Display][Hosted] C1 a re-declared channel takes the new "
    "shape and keeps the authoring it already had"
) {
    Channel standing;
    standing.set_name("position");
    standing.set_state_key("Body:position");
    standing.set_authoring_ticks(true);
    standing.set_last_written(Vector2(3.0, 4.0));

    Channel declared;
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    declared.set_spec(spec);
    declared.set_source_prop("velocity");
    declared.set_target_prop("offset");
    declared.set_self_feedback(true);
    declared.set_authoring_ticks(false);
    declared.set_name("shadow");

    standing.copy_shape_from(declared);

    const bool spec_carried = standing.get_spec() == spec;
    CHECK(spec_carried);
    CHECK(standing.get_source_prop() == StringName("velocity"));
    CHECK(standing.get_target_prop() == StringName("offset"));
    CHECK(standing.get_self_feedback());
    CHECK(standing.get_authoring_ticks());
    CHECK(standing.get_name() == StringName("position"));
    CHECK(standing.get_state_key() == StringName("Body:position"));
    CHECK(
        Vector2(standing.get_last_written()).is_equal_approx(Vector2(3.0, 4.0))
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] C2 a channel whose source or target was "
    "freed answers nothing, rather than a pointer nobody may follow"
) {
    Channel channel;
    Node2D *source = memnew(Node2D);
    Node2D *target = memnew(Node2D);
    channel.set_source_obj(source);
    channel.set_target_obj(target);

    const bool source_bound
        = Object::cast_to<Node2D>(channel.get_source_obj()) == source;
    const bool target_bound
        = Object::cast_to<Node2D>(channel.get_target_obj()) == target;
    CHECK(source_bound);
    CHECK(target_bound);

    memdelete(source);
    const bool source_gone
        = channel.get_source_obj().get_type() == Variant::NIL;
    const bool target_stands
        = Object::cast_to<Node2D>(channel.get_target_obj()) == target;
    CHECK(source_gone);
    CHECK(target_stands);

    memdelete(target);
    const bool target_gone
        = channel.get_target_obj().get_type() == Variant::NIL;
    CHECK(target_gone);
}

TEST_CASE(
    "[Networked][Display][Hosted] C3 a channel carries its own render offset, "
    "so no caller has to remember to"
) {
    Channel channel;

    CHECK(!channel.render_offset().is_held());
    CHECK(!channel.render_offset().armed);
}

TEST_CASE(
    "[Networked][Display][Hosted] C5 a snap writes the value out and forgets "
    "everything that would have smoothed away from it"
) {
    Channel channel;
    netw::display::History &history = channel.display_history();
    history.record(0, Vector2(0.0, 0.0), false);
    history.record(4, Vector2(8.0, 0.0), false);
    channel.render_offset().absorb(Vector2(10.0, 0.0), INFINITY);
    channel.set_last_written(Vector2(1.0, 1.0));

    channel.snap(Vector2(9.0, 9.0));

    CHECK(history.is_empty());
    CHECK(!channel.render_offset().is_held());
    CHECK(
        Vector2(channel.get_last_written()).is_equal_approx(Vector2(9.0, 9.0))
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] C6 an installed output takes the write, and "
    "takes it INSTEAD of the port, because it is the whole backend"
) {
    netw_test::CallLog log;
    Channel channel;
    const netw::display::Port &port = channel.display_port();
    channel.set_output(log.callable("shown"));

    channel.write(Vector2(3.0, 4.0));

    CHECK(log.count("shown") == 1);
    CHECK(Vector2(log.args("shown")[0]).is_equal_approx(Vector2(3.0, 4.0)));
    CHECK(port.get_lost() == 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] C7 a channel with no output writes its port, "
    "so the node write is the default rather than an installed backend"
) {
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    body->add_child(visual);

    Channel channel;
    netw::display::Port &port = channel.display_port();
    port.bind(visual, body);
    port.declare("position", "position", false);

    channel.write(Vector2(3.0, 4.0));

    CHECK(visual->get_position().is_equal_approx(Vector2(3.0, 4.0)));
    memdelete(body);
}

TEST_CASE(
    "[Networked][Display][Hosted] C8 a channel bound to neither an output nor "
    "a port drops its writes rather than refusing them"
) {
    Channel channel;

    channel.write(Vector2(3.0, 4.0));
    channel.snap(Vector2(5.0, 6.0));

    CHECK(
        Vector2(channel.get_last_written()).is_equal_approx(Vector2(5.0, 6.0))
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] C9 a snap goes out through the same write "
    "path, so an installed output sees teleports as well as smoothed frames"
) {
    netw_test::CallLog log;
    Channel channel;
    channel.set_output(log.callable("shown"));

    channel.write(Vector2(1.0, 0.0));
    channel.snap(Vector2(9.0, 9.0));

    CHECK(log.count("shown") == 2);
    CHECK(Vector2(log.args("shown", 1)[0]).is_equal_approx(Vector2(9.0, 9.0)));
}

TEST_CASE(
    "[Networked][Display][Hosted] C10 a door that TAKES the value stops the "
    "write, because a lane and a port both writing is the double write"
) {
    netw_test::CallLog log;
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    body->add_child(visual);
    visual->set_position(Vector2(1.0, 1.0));

    Channel channel;
    netw::display::Port &port = channel.display_port();
    port.bind(visual, body);
    port.declare("position", "position", false);
    channel.set_door(log.answering("lane", int64_t(OK)));

    channel.write(Vector2(7.0, 7.0));

    CHECK(log.count("lane") == 1);
    CHECK(visual->get_position().is_equal_approx(Vector2(1.0, 1.0)));
    memdelete(body);
}

TEST_CASE(
    "[Networked][Display][Hosted] C11 ERR_DOES_NOT_EXIST is the ONE door "
    "verdict that falls through, because it means no lane answers this entity"
) {
    netw_test::CallLog log;
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    body->add_child(visual);

    Channel channel;
    netw::display::Port &port = channel.display_port();
    port.bind(visual, body);
    port.declare("position", "position", false);
    channel.set_door(log.answering("lane", int64_t(ERR_DOES_NOT_EXIST)));

    channel.write(Vector2(7.0, 7.0));

    CHECK(log.count("lane") == 1);
    CHECK(visual->get_position().is_equal_approx(Vector2(7.0, 7.0)));
    memdelete(body);
}

TEST_CASE(
    "[Networked][Display][Hosted] C12 a door REFUSAL stops the write as firmly "
    "as a success, so a failing lane never silently degrades to the node"
) {
    netw_test::CallLog log;
    Node2D *body = memnew(Node2D);
    Node2D *visual = memnew(Node2D);
    body->add_child(visual);
    visual->set_position(Vector2(1.0, 1.0));

    Channel channel;
    netw::display::Port &port = channel.display_port();
    port.bind(visual, body);
    port.declare("position", "position", false);
    channel.set_door(log.answering("lane", int64_t(ERR_INVALID_DATA)));

    channel.write(Vector2(7.0, 7.0));

    CHECK(log.count("lane") == 1);
    CHECK(visual->get_position().is_equal_approx(Vector2(1.0, 1.0)));
    memdelete(body);
}

TEST_CASE(
    "[Networked][Display][Hosted] C13 the door is tried ahead of an installed "
    "output as well as ahead of the port, so a lane outranks every backend"
) {
    netw_test::CallLog log;
    Channel channel;
    channel.set_output(log.callable("shown"));
    channel.set_door(log.answering("lane", int64_t(OK)));

    channel.write(Vector2(7.0, 7.0));

    CHECK(log.count("lane") == 1);
    CHECK(log.count("shown") == 0);
}

} // namespace TestNetwChannel
