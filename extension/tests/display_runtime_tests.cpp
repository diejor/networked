#include "support/netw_test.h"

#include <cmath>

#include "godot/spatial_node.hpp"
#include "netw/display_history.hpp"
#include "netw/display_playhead.hpp"
#include "netw/display_runtime.hpp"

namespace TestNetwDisplayRuntime {

using namespace godot;
using netw::NetwDisplayChannel;
using netw::NetwDisplayDecl;
using netw::NetwDisplayRuntime;

Ref<NetwDisplayRuntime> make_runtime() {
    Ref<NetwDisplayRuntime> runtime;
    runtime.instantiate();
    return runtime;
}

TEST_CASE(
    "[Networked][Display][Hosted] R1 a fresh runtime is unresolved, holds its "
    "own track book and clamps no offset"
) {
    Ref<NetwDisplayRuntime> runtime = make_runtime();

    NETW_CHECK_EQ(
        runtime->get_pump_mode(),
        int64_t(NetwDisplayDecl::PUMP_UNRESOLVED)
    );
    REQUIRE(runtime->get_tracks().is_valid());
    NETW_CHECK_EQ(runtime->get_tracks()->size(), 0);
    CHECK(std::isinf(runtime->get_display_offset_limit()));
    NETW_CHECK_EQ(runtime->get_states().size(), 0);
    NETW_CHECK_EQ(runtime->get_pumped(), int64_t(0));
    CHECK(!runtime->get_disabled());
}

TEST_CASE(
    "[Networked][Display][Hosted] R2 a runtime whose owner was freed answers "
    "nothing, so a pass over it declines instead of following a pointer"
) {
    Ref<NetwDisplayRuntime> runtime = make_runtime();
    Node2D *owner = memnew(Node2D);
    runtime->bind(nullptr, owner);

    const bool owner_bound = runtime->owner() == owner;
    CHECK(owner_bound);
    CHECK(runtime->entity().is_null());

    memdelete(owner);

    const bool owner_gone = runtime->owner() == nullptr;
    CHECK(owner_gone);
}

TEST_CASE(
    "[Networked][Display][Hosted] R3 the channel row is the runtime's, and it "
    "keeps the element type a caller declared it with"
) {
    Ref<NetwDisplayRuntime> runtime = make_runtime();
    Ref<NetwDisplayChannel> channel;
    channel.instantiate();
    channel->set_name("position");

    runtime->get_states().push_back(channel);

    NETW_CHECK_EQ(runtime->get_states().size(), 1);
    Ref<NetwDisplayChannel> read = runtime->get_states()[0];
    CHECK(read->get_name() == StringName("position"));
    CHECK(runtime->get_states().is_typed());
}

TEST_CASE(
    "[Networked][Display][Hosted] R4 a track name addresses the channel that "
    "claimed it, and nothing at all when none did"
) {
    Ref<NetwDisplayRuntime> runtime = make_runtime();
    Ref<NetwDisplayChannel> position;
    position.instantiate();
    position->set_name("position");
    Ref<NetwDisplayChannel> rotation;
    rotation.instantiate();
    rotation->set_name("rotation");

    runtime->get_tracks()->declare("Body:position", "position");
    runtime->get_tracks()->declare("Turret:rotation", "rotation");
    runtime->get_states().push_back(position);
    runtime->get_states().push_back(rotation);

    const bool found = runtime->channel_named("rotation") == rotation;
    CHECK(found);
    CHECK(runtime->channel_named("scale").is_null());
}

TEST_CASE(
    "[Networked][Display][Hosted] R5 the displayed authoring tick is the "
    "sample the playhead is standing on, and -1 when nothing authors"
) {
    Ref<NetwDisplayRuntime> runtime = make_runtime();
    Ref<netw::NetwDisplayPlayhead> playhead;
    playhead.instantiate();
    runtime->set_playhead(playhead);

    Ref<NetwDisplayChannel> channel;
    channel.instantiate();
    channel->set_name("position");
    channel->set_authoring_ticks(true);
    Ref<netw::NetwDisplayHistory> history;
    history.instantiate();
    history->record(2, Vector2(0.0, 0.0), true);
    history->record(6, Vector2(4.0, 0.0), true);
    channel->set_history(history);
    runtime->get_tracks()->declare("Body:position", "position");
    runtime->get_states().push_back(channel);

    playhead->set_display_tick(4);

    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(-1));

    Ref<RefCounted> binding;
    binding.instantiate();
    runtime->set_authoring_binding(binding);

    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(2));

    playhead->set_display_tick(-1);
    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(-1));

    playhead->set_display_tick(4);
    channel->set_authoring_ticks(false);
    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(-1));
}

TEST_CASE(
    "[Networked][Display][Hosted] R6 a track diagnostic answers from the "
    "runtime, and a chase pump has no buffer to answer with"
) {
    Ref<NetwDisplayRuntime> runtime = make_runtime();
    Ref<netw::NetwDisplayPlayhead> playhead;
    playhead.instantiate();
    playhead->set_starvation_ticks(3);
    playhead->set_display_lag(1.5);
    runtime->set_playhead(playhead);
    runtime->set_pumped(11);

    Ref<NetwDisplayChannel> channel;
    channel.instantiate();
    channel->set_name("position");
    Ref<netw::NetwDisplayHistory> history;
    history.instantiate();
    history->record(0, Vector2(), false);
    history->set_sleeping(true);
    channel->set_history(history);
    runtime->get_tracks()->declare("Body:position", "position");
    runtime->get_states().push_back(channel);

    CHECK(bool(runtime->track_stat("position", "sleeping")));
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "channels")), 1);
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "ambiguous")), 0);
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "pumped_frames")), 11);
    NETW_CHECK_EQ(
        int64_t(runtime->track_stat("position", "starvation_ticks")),
        3
    );
    NETW_CHECK_CLOSE(
        double(runtime->track_stat("position", "display_lag")),
        1.5,
        0.000001
    );
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "buffer_size")), 1);
    CHECK(runtime->track_stat("position", "nothing_named_this").get_type()
        == Variant::NIL);

    runtime->set_pump_mode(netw::NetwDisplayDecl::PUMP_CHASE);
    CHECK(runtime->buffer_of("position").is_null());
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "buffer_size")), 0);
}

} // namespace TestNetwDisplayRuntime
