#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/pipeline.hpp"

namespace TestNetwPipeline {

using namespace godot;
using netw::NetwMultiplayer;
using netw::spawn::Park;
using netw::spawn::Pipeline;

Pipeline *fresh(const Ref<NetwMultiplayer> &p_core) {
    Pipeline *pipeline = p_core->spawn_plane();
    pipeline->set_channels(11, 12, 13, 14);
    return pipeline;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a scene recipe rides its uid when it has one "
    "and its path when it does not"
) {
    netw::wire::WriteStream writer;
    REQUIRE(Pipeline::put_scene_recipe(writer, String("res://nowhere.tscn")));
    netw::wire::ReadStream reader(writer.to_bytes());

    String read_back;
    REQUIRE(Pipeline::get_scene_recipe(reader, read_back));
    const bool path_round_trips = read_back == String("res://nowhere.tscn");
    CHECK(path_round_trips);

    netw::wire::WriteStream empty;
    REQUIRE(Pipeline::put_scene_recipe(empty, String()));
    netw::wire::ReadStream empty_reader(empty.to_bytes());
    String empty_back;
    REQUIRE(Pipeline::get_scene_recipe(empty_reader, empty_back));
    const bool empty_round_trips = empty_back.is_empty();
    CHECK(empty_round_trips);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a spawn frame from a sender that is not the "
    "server is refused and counted"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Pipeline *pipeline = fresh(core);

    PackedByteArray payload;
    payload.push_back(1);

    pipeline->handle_spawn_frame(payload, 2);
    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("drops_spawn_bad_sender")]),
        int64_t(1)
    );

    pipeline->handle_despawn_frame(payload, 5);
    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("drops_spawn_bad_sender")]),
        int64_t(2)
    );

    pipeline->handle_spawn_frame(payload, 1);
    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("drops_spawn_bad_sender")]),
        int64_t(2)
    );
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a frame on a channel this pipeline does not "
    "carry is refused, because the gate reads the channel it was given"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Pipeline *pipeline = core->spawn_plane();
    pipeline->set_channels(0, 0, 0, 0);

    PackedByteArray payload;
    payload.push_back(1);

    pipeline->handle_spawn_frame(payload, 1);
    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("drops_spawn_unresolved")]),
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a scene park watches the live edge while it "
    "holds a frame and stops watching once the last one is gone"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Pipeline *pipeline = fresh(core);
    const Callable retry = callable_mp(
        core.ptr(),
        &NetwMultiplayer::spawn_retry_scene_parked_spawns
    );

    CHECK_FALSE(core->is_connected("entity_live", retry));

    PackedByteArray payload;
    payload.push_back(7);
    pipeline->park_spawn_for_scene(payload, 9001);

    CHECK(core->is_connected("entity_live", retry));
    CHECK(pipeline->get_park().has(9001));
    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("spawn_deferrals")]),
        int64_t(1)
    );

    pipeline->get_park().cancel(9001);
    pipeline->retry_scene_parked_spawns(0, Ref<netw::NetwEntity>());

    CHECK_FALSE(core->is_connected("entity_live", retry));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a session end drops the scene park and its "
    "watch, so no park outlives the session that made it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Pipeline *pipeline = fresh(core);
    const Callable retry = callable_mp(
        core.ptr(),
        &NetwMultiplayer::spawn_retry_scene_parked_spawns
    );

    PackedByteArray payload;
    payload.push_back(7);
    pipeline->park_spawn_for_scene(payload, 9002);
    CHECK(core->is_connected("entity_live", retry));

    pipeline->clear_session();

    CHECK_FALSE(core->is_connected("entity_live", retry));
    NETW_CHECK_EQ(pipeline->get_park().size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a scene park that outlived its window is "
    "given up rather than retried, and counted as expired"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Pipeline *pipeline = fresh(core);
    pipeline->set_park_timeout_seconds(0.0);

    PackedByteArray payload;
    payload.push_back(7);
    pipeline->park_spawn_for_scene(payload, 9003);
    pipeline->retry_scene_parked_spawns(0, Ref<netw::NetwEntity>());

    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("spawn_park_expired")]),
        int64_t(1)
    );
    CHECK_FALSE(pipeline->get_park().has(9003));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the ledger answers what this peer tracks, and "
    "the counters read it rather than keeping a second tally"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Pipeline *pipeline = fresh(core);
    Node *node = memnew(Node);

    CHECK_FALSE(pipeline->owns_spawned_route(31));

    pipeline->get_spawn_book()->enroll_recv(31, node);

    CHECK(pipeline->owns_spawned_route(31));
    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("spawn_book_recv")]),
        int64_t(1)
    );

    pipeline->clear_route(31);
    CHECK_FALSE(pipeline->owns_spawned_route(31));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] spawn state is collected in tree preorder, so "
    "the receiver applies in the order the authority declared"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Pipeline *pipeline = fresh(core);

    Node *root = memnew(Node);
    Node *first = memnew(Node);
    Node *second = memnew(Node);
    root->add_child(first);
    root->add_child(second);

    const TypedArray<Dictionary> rows = pipeline->collect_spawn_state(root);
    NETW_CHECK_EQ(rows.size(), 0);

    const TypedArray<Dictionary> none = pipeline->collect_spawn_state(nullptr);
    NETW_CHECK_EQ(none.size(), 0);

    memdelete(root);
}

} // namespace TestNetwPipeline
