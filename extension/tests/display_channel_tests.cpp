#include "support/netw_test.h"

#include "godot/spatial_node.hpp"
#include "netw/display_channel.hpp"
#include "netw/display_history.hpp"

namespace TestNetwDisplayChannel {

using namespace godot;
using netw::NetwDisplayChannel;
using netw::NetwInterpolate;

Ref<NetwDisplayChannel> make_channel() {
    Ref<NetwDisplayChannel> channel;
    channel.instantiate();
    return channel;
}

TEST_CASE(
    "[Networked][Display][Hosted] C1 a re-declared channel takes the new "
    "shape and keeps the authoring it already had"
) {
    Ref<NetwDisplayChannel> standing = make_channel();
    standing->set_name("position");
    standing->set_state_key("Body:position");
    standing->set_authoring_ticks(true);
    standing->set_last_written(Vector2(3.0, 4.0));

    Ref<NetwDisplayChannel> declared = make_channel();
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    declared->set_spec(spec);
    declared->set_source_prop("velocity");
    declared->set_target_prop("offset");
    declared->set_self_feedback(true);
    declared->set_authoring_ticks(false);
    declared->set_name("shadow");

    standing->copy_shape_from(declared);

    const bool spec_carried = standing->get_spec() == spec;
    CHECK(spec_carried);
    CHECK(standing->get_source_prop() == StringName("velocity"));
    CHECK(standing->get_target_prop() == StringName("offset"));
    CHECK(standing->get_self_feedback());
    CHECK(standing->get_authoring_ticks());
    CHECK(standing->get_name() == StringName("position"));
    CHECK(standing->get_state_key() == StringName("Body:position"));
    CHECK(Vector2(standing->get_last_written()).is_equal_approx(
        Vector2(3.0, 4.0)
    ));
}

TEST_CASE(
    "[Networked][Display][Hosted] C2 a channel whose source or target was "
    "freed answers nothing, rather than a pointer nobody may follow"
) {
    Ref<NetwDisplayChannel> channel = make_channel();
    Node2D *source = memnew(Node2D);
    Node2D *target = memnew(Node2D);
    channel->set_source_obj(source);
    channel->set_target_obj(target);

    const bool source_bound = Object::cast_to<Node2D>(channel->get_source_obj()) == source;
    const bool target_bound = Object::cast_to<Node2D>(channel->get_target_obj()) == target;
    CHECK(source_bound);
    CHECK(target_bound);

    memdelete(source);
    const bool source_gone = channel->get_source_obj().get_type() == Variant::NIL;
    const bool target_stands = Object::cast_to<Node2D>(channel->get_target_obj()) == target;
    CHECK(source_gone);
    CHECK(target_stands);

    memdelete(target);
    const bool target_gone = channel->get_target_obj().get_type() == Variant::NIL;
    CHECK(target_gone);
}

TEST_CASE(
    "[Networked][Display][Hosted] C3 a channel mints its own render offset, "
    "so no caller has to remember to"
) {
    Ref<NetwDisplayChannel> channel = make_channel();

    CHECK(channel->get_offset().is_valid());
    CHECK(!channel->get_offset()->is_held());
    CHECK(!channel->get_offset()->is_armed());
}

TEST_CASE(
    "[Networked][Display][Hosted] C4 a shape copy from nothing changes "
    "nothing, because a channel with no twin is already its own shape"
) {
    Ref<NetwDisplayChannel> channel = make_channel();
    channel->set_source_prop("position");
    channel->set_authoring_ticks(true);

    channel->copy_shape_from(Ref<NetwDisplayChannel>());

    CHECK(channel->get_source_prop() == StringName("position"));
    CHECK(channel->get_authoring_ticks());
}

TEST_CASE(
    "[Networked][Display][Hosted] C5 a snap writes the value out and forgets "
    "everything that would have smoothed away from it"
) {
    Ref<NetwDisplayChannel> channel = make_channel();
    Ref<netw::NetwDisplayHistory> history;
    history.instantiate();
    history->record(0, Vector2(0.0, 0.0), false);
    history->record(4, Vector2(8.0, 0.0), false);
    channel->set_history(history);
    channel->get_offset()->absorb(Vector2(10.0, 0.0), INFINITY);
    channel->set_last_written(Vector2(1.0, 1.0));

    channel->snap(Vector2(9.0, 9.0));

    CHECK(history->is_empty());
    CHECK(!channel->get_offset()->is_held());
    CHECK(Vector2(channel->get_last_written()).is_equal_approx(
        Vector2(9.0, 9.0)
    ));
}

} // namespace TestNetwDisplayChannel
