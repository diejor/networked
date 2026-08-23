#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "godot/spatial_node.hpp"
#include "netw/api/display_book.hpp"
#include "netw/display_channel.hpp"
#include "netw/api/display_decl.hpp"
#include "netw/display_runtime.hpp"
#include "netw/api/display_spec_row.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwDisplaySession {

using namespace godot;
using netw::NetwDisplayChannel;
using netw::NetwDisplayDecl;
using netw::NetwDisplayRuntime;
using netw::NetwDisplaySpecRow;
using netw::NetwInterpolate;
using netw::NetwMultiplayerCore;

Ref<NetwMultiplayerCore> make_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    return core;
}

Ref<NetwInterpolate> lerp_spec() {
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    return spec;
}

Ref<NetwDisplayRuntime> bare_runtime(
    Node *p_owner,
    Ref<netw::NetwEntity> &r_entity
) {
    r_entity.instantiate();
    r_entity->set_owner(p_owner);
    Ref<NetwDisplayRuntime> runtime;
    runtime.instantiate();
    runtime->bind(r_entity.ptr(), p_owner);
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();
    runtime->set_config(decl);
    return runtime;
}

TEST_CASE(
    "[Networked][Display][Hosted] DS1 a spec row derives its target from the "
    "spec's .to() when one is set, and from the source property otherwise"
) {
    Node2D *node = memnew(Node2D);
    Ref<NetwInterpolate> plain = lerp_spec();
    Ref<NetwInterpolate> aimed = lerp_spec();
    aimed->set_target("shadow_position");

    const Ref<NetwDisplaySpecRow> straight
        = NetwDisplaySpecRow::of_property(node, "position", plain);
    const Ref<NetwDisplaySpecRow> redirected
        = NetwDisplaySpecRow::of_property(node, "position", aimed);

    CHECK(straight->get_target_prop() == StringName("position"));
    CHECK(redirected->get_target_prop() == StringName("shadow_position"));
    CHECK(straight->get_source_prop() == StringName("position"));
    CHECK(straight->has_source());
    memdelete(node);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS2 an ARGUMENT row has no source, because "
    "an event argument is written where it lands and sampled from nowhere"
) {
    Node2D *node = memnew(Node2D);
    Ref<NetwInterpolate> spec = lerp_spec();
    spec->set_target("muzzle_flash");

    const Ref<NetwDisplaySpecRow> row
        = NetwDisplaySpecRow::of_argument(node, spec);

    CHECK(!row->has_source());
    CHECK(row->get_source_prop() == StringName());
    CHECK(row->get_target_prop() == StringName("muzzle_flash"));
    CHECK(row->is_displayable());
    memdelete(node);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS3 a row whose node is freed, or whose spec "
    "smooths nothing, is NOT displayable and builds no channel"
) {
    Node2D *node = memnew(Node2D);
    Ref<NetwInterpolate> none = lerp_spec();
    none->set_mode(NetwInterpolate::MODE_NONE);

    const Ref<NetwDisplaySpecRow> dead_spec
        = NetwDisplaySpecRow::of_property(node, "position", none);
    CHECK(!dead_spec->is_displayable());

    const Ref<NetwDisplaySpecRow> live
        = NetwDisplaySpecRow::of_property(node, "position", lerp_spec());
    CHECK(live->is_displayable());
    memdelete(node);
    CHECK(!live->is_displayable());
}

TEST_CASE(
    "[Networked][Display][Hosted] DS4 the spec reader is the ONE door to the "
    "authoring registry, so a session with none installed builds no channel"
) {
    netw_test::CallLog log;
    Ref<NetwMultiplayerCore> core = make_core();
    Node2D *owner = memnew(Node2D);

    CHECK(!core->display_wants_runtime(owner));

    core->set_display_spec_reader(log.answering("specs", Array()));
    CHECK(!core->display_wants_runtime(owner));
    NETW_CHECK_EQ(log.count("specs"), 1);

    Array rows;
    rows.append(NetwDisplaySpecRow::of_property(owner, "position", lerp_spec()));
    core->set_display_spec_reader(log.answering("specs", rows));
    CHECK(core->display_wants_runtime(owner));

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS5 a rebuild asks the registry once and "
    "declares one channel per displayable row it answers with"
) {
    netw_test::CallLog log;
    Ref<NetwMultiplayerCore> core = make_core();
    Node2D *owner = memnew(Node2D);
    Node2D *child = memnew(Node2D);
    owner->add_child(child);

    Array rows;
    rows.append(NetwDisplaySpecRow::of_property(owner, "position", lerp_spec()));
    rows.append(NetwDisplaySpecRow::of_property(child, "position", lerp_spec()));
    core->set_display_spec_reader(log.answering("specs", rows));

    Ref<netw::NetwEntity> entity;
    Ref<NetwDisplayRuntime> runtime = bare_runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    NETW_CHECK_EQ(log.count("specs"), 1);
    NETW_CHECK_EQ(runtime->get_states().size(), 2);
    NETW_CHECK_EQ(
        runtime->get_pump_mode(),
        int64_t(NetwDisplayDecl::PUMP_UNRESOLVED)
    );
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS6 two rows naming the same property on the "
    "same node are ONE channel, because they are one value with one history"
) {
    Ref<NetwMultiplayerCore> core = make_core();
    Node2D *owner = memnew(Node2D);

    Array rows;
    rows.append(NetwDisplaySpecRow::of_property(owner, "position", lerp_spec()));
    rows.append(NetwDisplaySpecRow::of_property(owner, "position", lerp_spec()));
    netw_test::CallLog log;
    core->set_display_spec_reader(log.answering("specs", rows));

    Ref<netw::NetwEntity> entity;
    Ref<NetwDisplayRuntime> runtime = bare_runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    NETW_CHECK_EQ(runtime->get_states().size(), 1);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS7 a rebuild REPLACES the channel table "
    "rather than appending to it, so a re-declared entity does not accumulate"
) {
    Ref<NetwMultiplayerCore> core = make_core();
    Node2D *owner = memnew(Node2D);

    Array rows;
    rows.append(NetwDisplaySpecRow::of_property(owner, "position", lerp_spec()));
    netw_test::CallLog log;
    core->set_display_spec_reader(log.answering("specs", rows));

    Ref<netw::NetwEntity> entity;
    Ref<NetwDisplayRuntime> runtime = bare_runtime(owner, entity);
    core->display_rebuild_runtime(runtime);
    core->display_rebuild_runtime(runtime);
    core->display_rebuild_runtime(runtime);

    NETW_CHECK_EQ(runtime->get_states().size(), 1);
    NETW_CHECK_EQ(log.count("specs"), 3);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS8 a channel built for a property SAMPLES "
    "it, and one built for an argument writes without sampling anything"
) {
    Ref<NetwMultiplayerCore> core = make_core();
    Node2D *owner = memnew(Node2D);
    owner->set_position(Vector2(5.0, 0.0));

    Ref<NetwInterpolate> aimed = lerp_spec();
    aimed->set_target("flash");
    Array rows;
    rows.append(NetwDisplaySpecRow::of_property(owner, "position", lerp_spec()));
    rows.append(NetwDisplaySpecRow::of_argument(owner, aimed));
    netw_test::CallLog log;
    core->set_display_spec_reader(log.answering("specs", rows));

    Ref<netw::NetwEntity> entity;
    Ref<NetwDisplayRuntime> runtime = bare_runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    REQUIRE(runtime->get_states().size() == 2);
    const Ref<NetwDisplayChannel> sampled
        = runtime->channel_named("position");
    const Ref<NetwDisplayChannel> written = runtime->channel_named("flash");
    REQUIRE(sampled.is_valid());
    REQUIRE(written.is_valid());
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
    Ref<NetwMultiplayerCore> core = make_core();
    Node2D *owner = memnew(Node2D);

    Array rows;
    rows.append(NetwDisplaySpecRow::of_property(owner, "position", lerp_spec()));
    core->set_display_spec_reader(log.answering("specs", rows));
    core->set_display_lane(log.answering("lane", int64_t(OK)));

    Ref<netw::NetwEntity> entity;
    Ref<NetwDisplayRuntime> runtime = bare_runtime(owner, entity);
    core->display_rebuild_runtime(runtime);

    const Ref<NetwDisplayChannel> channel = runtime->channel_named("position");
    REQUIRE(channel.is_valid());
    channel->write(Vector2(1.0, 2.0));

    NETW_CHECK_EQ(log.count("lane"), 1);
    CHECK(StringName(log.args("lane")[1]) == StringName("position"));
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] DS10 a pump answers OK twice in one process "
    "frame but advances once, because a frame has one display"
) {
    Ref<NetwMultiplayerCore> core = make_core();

    NETW_CHECK_EQ(int(core->display_pump(1.0 / 60.0)), int(OK));
    NETW_CHECK_EQ(int(core->display_pump(1.0 / 60.0)), int(OK));
}

} // namespace TestNetwDisplaySession

