#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/display/book.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/spec_row.hpp"

namespace TestNetwDisplaySession {

using namespace godot;
using netw::NetwInterpolate;
using netw::NetwMultiplayer;
using netw::display::Channel;
using netw::display::Decl;
using netw::display::Runtime;
using netw::display::SpecRow;

Ref<NetwMultiplayer> make_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

Ref<NetwInterpolate> lerp_spec() {
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    return spec;
}

struct BareRuntime {
    Runtime held;

    BareRuntime(Node *p_owner, Ref<netw::NetwEntity> &r_entity) {
        r_entity.instantiate();
        r_entity->set_owner(p_owner);
        held.bind(r_entity.ptr(), p_owner);
        held.set_config(Decl());
    }

    BareRuntime(const BareRuntime &) = delete;
    BareRuntime &operator=(const BareRuntime &) = delete;

    Runtime *operator->() {
        return &held;
    }
    operator Runtime *() {
        return &held;
    }
};

TEST_CASE(
    "[Networked][Display][Hosted] DS1 a spec row derives its target from the "
    "spec's .to() when one is set, and from the source property otherwise"
) {
    Node2D *node = memnew(Node2D);
    Ref<NetwInterpolate> plain = lerp_spec();
    Ref<NetwInterpolate> aimed = lerp_spec();
    aimed->set_target("shadow_position");

    const SpecRow straight = SpecRow::of_property(node, "position", plain);
    const SpecRow redirected = SpecRow::of_property(node, "position", aimed);

    CHECK(straight.target_prop == StringName("position"));
    CHECK(redirected.target_prop == StringName("shadow_position"));
    CHECK(straight.source_prop == StringName("position"));
    CHECK(straight.has_source());
    memdelete(node);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS2 an ARGUMENT row has no source, because "
    "an event argument is written where it lands and sampled from nowhere"
) {
    Node2D *node = memnew(Node2D);
    Ref<NetwInterpolate> spec = lerp_spec();
    spec->set_target("muzzle_flash");

    const SpecRow row = SpecRow::of_argument(node, spec);

    CHECK(!row.has_source());
    CHECK(row.source_prop == StringName());
    CHECK(row.target_prop == StringName("muzzle_flash"));
    CHECK(row.is_displayable());
    memdelete(node);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS3 a row whose node is freed, or whose spec "
    "smooths nothing, is NOT displayable and builds no channel"
) {
    Node2D *node = memnew(Node2D);
    Ref<NetwInterpolate> none = lerp_spec();
    none->set_mode(NetwInterpolate::MODE_NONE);

    const SpecRow dead_spec = SpecRow::of_property(node, "position", none);
    CHECK(!dead_spec.is_displayable());

    const SpecRow live = SpecRow::of_property(node, "position", lerp_spec());
    CHECK(live.is_displayable());
    memdelete(node);
    CHECK(!live.is_displayable());
}

TEST_CASE(
    "[Networked][Display][Hosted] DS4 the spec reader is the ONE door to the "
    "authoring registry, so a session with none installed builds no channel"
) {
    netw_test::CallLog log;
    Ref<NetwMultiplayer> core = make_core();
    Node2D *owner = memnew(Node2D);

    CHECK(!core->display_wants_runtime(owner));

    core->set_display_spec_override(LocalVector<SpecRow>());
    CHECK(!core->display_wants_runtime(owner));
    NETW_CHECK_EQ(core->display_spec_asks(), 1);

    LocalVector<SpecRow> rows;
    rows.push_back(SpecRow::of_property(owner, "position", lerp_spec()));
    core->set_display_spec_override(rows);
    CHECK(core->display_wants_runtime(owner));

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS5 a rebuild asks the registry once and "
    "declares one channel per displayable row it answers with"
) {
    netw_test::CallLog log;
    Ref<NetwMultiplayer> core = make_core();
    Node2D *owner = memnew(Node2D);
    Node2D *child = memnew(Node2D);
    owner->add_child(child);

    LocalVector<SpecRow> rows;
    rows.push_back(SpecRow::of_property(owner, "position", lerp_spec()));
    rows.push_back(SpecRow::of_property(child, "position", lerp_spec()));
    core->set_display_spec_override(rows);

    Ref<netw::NetwEntity> entity;
    BareRuntime runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    NETW_CHECK_EQ(core->display_spec_asks(), 1);
    NETW_CHECK_EQ(int(runtime->channels().size()), 2);
    NETW_CHECK_EQ(
        runtime->get_pump_mode(),
        int64_t(netw::display::PUMP_UNRESOLVED)
    );
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS6 two rows naming the same property on the "
    "same node are ONE channel, because they are one value with one history"
) {
    Ref<NetwMultiplayer> core = make_core();
    Node2D *owner = memnew(Node2D);

    LocalVector<SpecRow> rows;
    rows.push_back(SpecRow::of_property(owner, "position", lerp_spec()));
    rows.push_back(SpecRow::of_property(owner, "position", lerp_spec()));
    netw_test::CallLog log;
    core->set_display_spec_override(rows);

    Ref<netw::NetwEntity> entity;
    BareRuntime runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    NETW_CHECK_EQ(int(runtime->channels().size()), 1);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS7 a rebuild REPLACES the channel table "
    "rather than appending to it, so a re-declared entity does not accumulate"
) {
    Ref<NetwMultiplayer> core = make_core();
    Node2D *owner = memnew(Node2D);

    LocalVector<SpecRow> rows;
    rows.push_back(SpecRow::of_property(owner, "position", lerp_spec()));
    netw_test::CallLog log;
    core->set_display_spec_override(rows);

    Ref<netw::NetwEntity> entity;
    BareRuntime runtime(owner, entity);
    core->display_rebuild_runtime(runtime);
    core->display_rebuild_runtime(runtime);
    core->display_rebuild_runtime(runtime);

    NETW_CHECK_EQ(int(runtime->channels().size()), 1);
    NETW_CHECK_EQ(core->display_spec_asks(), 3);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS8 a channel built for a property SAMPLES "
    "it, and one built for an argument writes without sampling anything"
) {
    Ref<NetwMultiplayer> core = make_core();
    Node2D *owner = memnew(Node2D);
    owner->set_position(Vector2(5.0, 0.0));

    Ref<NetwInterpolate> aimed = lerp_spec();
    aimed->set_target("flash");
    LocalVector<SpecRow> rows;
    rows.push_back(SpecRow::of_property(owner, "position", lerp_spec()));
    rows.push_back(SpecRow::of_argument(owner, aimed));
    netw_test::CallLog log;
    core->set_display_spec_override(rows);

    Ref<netw::NetwEntity> entity;
    BareRuntime runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    REQUIRE(runtime->channels().size() == 2);
    Channel *sampled = runtime->channel_named("position");
    Channel *written = runtime->channel_named("flash");
    REQUIRE(sampled != nullptr);
    REQUIRE(written != nullptr);
    CHECK(sampled->get_self_feedback());
    CHECK(Object::cast_to<Node>(sampled->get_source_obj()) == owner);
    CHECK(Object::cast_to<Node>(written->get_source_obj()) == nullptr);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS9 every channel a session builds carries "
    "the session's display lane, so a write reaches the door it owns"
) {
    netw_test::CallLog log;
    Ref<NetwMultiplayer> core = make_core();
    Node2D *owner = memnew(Node2D);

    LocalVector<SpecRow> rows;
    rows.push_back(SpecRow::of_property(owner, "position", lerp_spec()));
    core->set_display_spec_override(rows);
    core->set_display_lane(log.answering("lane", int64_t(OK)));

    Ref<netw::NetwEntity> entity;
    BareRuntime runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    Channel *channel = runtime->channel_named("position");
    REQUIRE(channel != nullptr);
    channel->write(Vector2(1.0, 2.0));

    NETW_CHECK_EQ(log.count("lane"), 1);
    CHECK(StringName(log.args("lane")[1]) == StringName("position"));
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS10 a pump answers OK twice in one process "
    "frame but advances once, because a frame has one display"
) {
    Ref<NetwMultiplayer> core = make_core();

    NETW_CHECK_EQ(int(core->display_pump(1.0 / 60.0)), int(OK));
    NETW_CHECK_EQ(int(core->display_pump(1.0 / 60.0)), int(OK));
}

} // namespace TestNetwDisplaySession
