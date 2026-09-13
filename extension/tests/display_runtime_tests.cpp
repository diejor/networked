#include "support/netw_test.h"

#include <cmath>

#include "godot/spatial_node.hpp"
#include "netw/display/history.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/runtime.hpp"

namespace TestNetwRuntime {

using namespace godot;
using netw::display::Channel;
using netw::display::Runtime;

TEST_CASE(
    "[Networked][Display][Hosted] R1 a fresh runtime is unresolved, holds its "
    "own track book and clamps no offset"
) {
    Runtime holder;
    Runtime *runtime = &holder;

    NETW_CHECK_EQ(
        runtime->get_pump_mode(),
        int64_t(netw::display::PUMP_UNRESOLVED)
    );
    NETW_CHECK_EQ(runtime->display_tracks().size(), 0);
    CHECK(std::isinf(runtime->get_display_offset_limit()));
    NETW_CHECK_EQ(int(runtime->channels().size()), 0);
    NETW_CHECK_EQ(runtime->get_pumped(), int64_t(0));
    CHECK(!runtime->get_disabled());
}

TEST_CASE(
    "[Networked][Display][Hosted] R2 a runtime whose owner was freed answers "
    "nothing, so a pass over it declines instead of following a pointer"
) {
    Runtime holder;
    Runtime *runtime = &holder;
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
    "[Networked][Display][Hosted] R3 the channel row is the runtime's, and a "
    "channel it added is the channel it hands back"
) {
    Runtime holder;
    Runtime *runtime = &holder;
    Channel *channel = runtime->add_channel();
    channel->set_name("position");

    NETW_CHECK_EQ(int(runtime->channels().size()), 1);
    CHECK(runtime->channels()[0] == channel);
    CHECK(runtime->channels()[0]->get_name() == StringName("position"));
}

TEST_CASE(
    "[Networked][Display][Hosted] R4 a track name addresses the channel that "
    "claimed it, and nothing at all when none did"
) {
    Runtime holder;
    Runtime *runtime = &holder;
    Channel *position = runtime->add_channel();
    position->set_name("position");
    Channel *rotation = runtime->add_channel();
    rotation->set_name("rotation");

    runtime->display_tracks().declare("Body:position", "position");
    runtime->display_tracks().declare("Turret:rotation", "rotation");

    CHECK(runtime->channel_named("rotation") == rotation);
    CHECK(runtime->channel_named("scale") == nullptr);
}

TEST_CASE(
    "[Networked][Display][Hosted] R5 the displayed authoring tick is the "
    "sample the playhead is standing on, and -1 when nothing authors"
) {
    Runtime holder;
    Runtime *runtime = &holder;
    netw::display::Playhead &playhead = runtime->display_playhead();

    Channel *channel = runtime->add_channel();
    channel->set_name("position");
    channel->set_authoring_ticks(true);
    netw::display::History &history = channel->display_history();
    history.record(2, Vector2(0.0, 0.0), true);
    history.record(6, Vector2(4.0, 0.0), true);
    runtime->display_tracks().declare("Body:position", "position");

    playhead.set_display_tick(4);

    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(-1));

    Ref<netw::NetwPropertySetBinding> binding;
    binding.instantiate();
    runtime->set_authoring_binding(binding);

    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(2));

    playhead.set_display_tick(-1);
    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(-1));

    playhead.set_display_tick(4);
    channel->set_authoring_ticks(false);
    NETW_CHECK_EQ(runtime->authoring_tick(), int64_t(-1));
}

TEST_CASE(
    "[Networked][Display][Hosted] R6 a track diagnostic answers from the "
    "runtime, and a chase pump has no buffer to answer with"
) {
    Runtime holder;
    Runtime *runtime = &holder;
    netw::display::Playhead &playhead = runtime->display_playhead();
    playhead.set_starvation_ticks(3);
    playhead.set_display_lag(1.5);
    runtime->set_pumped(11);

    Channel *channel = runtime->add_channel();
    channel->set_name("position");
    netw::display::History &history = channel->display_history();
    history.record(0, Vector2(), false);
    history.set_sleeping(true);
    runtime->display_tracks().declare("Body:position", "position");

    CHECK(bool(runtime->track_stat("position", "sleeping")));
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "channels")), 1);
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "ambiguous")), 0);
    NETW_CHECK_EQ(
        int64_t(runtime->track_stat("position", "pumped_frames")),
        11
    );
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
    CHECK(
        runtime->track_stat("position", "nothing_named_this").get_type()
        == Variant::NIL
    );

    runtime->set_pump_mode(netw::display::PUMP_CHASE);
    CHECK(runtime->buffer_of("position").is_null());
    NETW_CHECK_EQ(int64_t(runtime->track_stat("position", "buffer_size")), 0);
}

} // namespace TestNetwRuntime
