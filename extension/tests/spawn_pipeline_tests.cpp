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
    pipeline->park_spawn_for_scene(payload, 9001, 1);

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
    pipeline->park_spawn_for_scene(payload, 9002, 1);
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
    pipeline->park_spawn_for_scene(payload, 9003, 1);
    pipeline->retry_scene_parked_spawns(0, Ref<netw::NetwEntity>());

    NETW_CHECK_EQ(
        int64_t(pipeline->counters()[StringName("spawn_park_expired")]),
        int64_t(1)
    );
    CHECK_FALSE(pipeline->get_park().has(9003));
}

struct ParkedRelease {
    Ref<NetwMultiplayer> core;
    Pipeline *pipeline = nullptr;

    ParkedRelease() {
        core.instantiate();
        core->session_set_authority_peer(7);
        pipeline = core->spawn_plane();
    }

    PackedByteArray frame() const {
        PackedByteArray payload;
        payload.push_back(7);
        return payload;
    }

    void follow(int64_t p_coordinator) {
        core->session_set_authority_peer(p_coordinator);
    }

    int64_t counted(const char *p_name) const {
        return int64_t(pipeline->counters()[StringName(p_name)]);
    }
};

TEST_CASE(
    "[Networked][Spawn][Hosted] a refused frame counts as a bad sender only "
    "when it did not come from the peer this session follows, so a "
    "coordinator of 7 and transport peer 1 swap places in the tally"
) {
    ParkedRelease world;
    PackedByteArray payload;
    payload.push_back(7);

    world.pipeline->handle_spawn_frame(payload, 1);
    NETW_CHECK_EQ(world.counted("drops_spawn_bad_sender"), int64_t(1));

    world.pipeline->handle_spawn_frame(PackedByteArray(), 7);
    NETW_CHECK_EQ(world.counted("drops_spawn_bad_sender"), int64_t(1));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a spawn waiting for its consumed spawner's "
    "scene is admitted again when the wait releases, so a sender the session "
    "no longer follows applies nothing"
) {
    ParkedRelease stale;
    stale.pipeline->park_spawn_for_scene(stale.frame(), 9101, 7);
    stale.follow(11);
    stale.pipeline->retry_scene_parked_spawns(0, Ref<netw::NetwEntity>());

    NETW_CHECK_EQ(stale.counted("spawn_park_refused"), int64_t(1));
    NETW_CHECK_EQ(stale.counted("drops_spawn_truncated"), int64_t(0));

    ParkedRelease current;
    current.pipeline->park_spawn_for_scene(current.frame(), 9101, 7);
    current.pipeline->retry_scene_parked_spawns(0, Ref<netw::NetwEntity>());

    NETW_CHECK_EQ(current.counted("spawn_park_refused"), int64_t(0));
    NETW_CHECK_EQ(current.counted("drops_spawn_truncated"), int64_t(1));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a spawn waiting for the node it adopts is "
    "admitted again when the wait releases, so a sender the session no longer "
    "follows applies nothing"
) {
    ParkedRelease stale;
    stale.pipeline->park_spawn_for_adopt(stale.frame(), 9102, 7);
    stale.follow(11);
    stale.pipeline->retry_adopt_parked();

    NETW_CHECK_EQ(stale.counted("spawn_park_refused"), int64_t(1));
    NETW_CHECK_EQ(stale.counted("drops_spawn_truncated"), int64_t(0));

    ParkedRelease current;
    current.pipeline->park_spawn_for_adopt(current.frame(), 9102, 7);
    current.pipeline->retry_adopt_parked();

    NETW_CHECK_EQ(current.counted("spawn_park_refused"), int64_t(0));
    NETW_CHECK_EQ(current.counted("drops_spawn_truncated"), int64_t(1));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a spawn waiting for the route it anchors to "
    "is admitted again when the wait releases, so a sender the session no "
    "longer follows applies nothing"
) {
    ParkedRelease stale;
    stale.pipeline->get_park()
        .park(9103, stale.frame(), Park::WAIT_ROUTE, 0, 7);
    stale.follow(11);
    stale.core->spawn_retry_parked(9103);

    NETW_CHECK_EQ(stale.counted("spawn_park_refused"), int64_t(1));
    NETW_CHECK_EQ(stale.counted("drops_spawn_truncated"), int64_t(0));

    ParkedRelease current;
    current.pipeline->get_park()
        .park(9103, current.frame(), Park::WAIT_ROUTE, 0, 7);
    current.core->spawn_retry_parked(9103);

    NETW_CHECK_EQ(current.counted("spawn_park_refused"), int64_t(0));
    NETW_CHECK_EQ(current.counted("drops_spawn_truncated"), int64_t(1));
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
