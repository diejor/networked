#include "support/fault_log.h"
#include "support/loopback_rig.h"
#include "support/scripted_loads.h"
#include "support/stock_stand.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_loads.hpp"

namespace TestSpawnLoadLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::SceneLoads;

constexpr double SHORT_PARK_DEADLINE = 0.2;
constexpr int PAST_THE_DEADLINE = 30;
constexpr int SETTLE = 4;

enum Recipe {
    SCENE_ROOT,
    FROM_SPAWNER,
};

const char *recipe_name(Recipe p_recipe) {
    switch (p_recipe) {
        case SCENE_ROOT:
            return "a scene root";
        case FROM_SPAWNER:
            return "a MultiplayerSpawner scene";
    }
    return "";
}

struct ClientBuild {
    NetwMultiplayer *client = nullptr;
    int root_route = 0;
    int built = 0;
    int under_a_ready_root = 0;
};

ClientBuild client_build;

Node *build_body(const Variant &p_name) {
    NetwMultiplayer *client = client_build.client;
    Node *root = client != nullptr
        ? client->entity_get_node(
              client->entity_from_route(client_build.root_route)
          )
        : nullptr;
    if (root != nullptr) {
        client_build.built += 1;
        client_build.under_a_ready_root += root->is_node_ready() ? 1 : 0;
    }
    Node *made = memnew(Node);
    made->set_name(String(p_name));
    return made;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Ref<PackedScene> a_level() {
    Node2D *shape = memnew(Node2D);
    shape->set_name("Level");
    const Ref<PackedScene> packed
        = pack_stock_scene(shape, mint_scene_path("LoadLevel"));
    memdelete(shape);
    return packed;
}

int route_of(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    return entity.is_valid() ? int(entity->get_route()) : 0;
}

struct Loading {
    Node *root = nullptr;
    int root_route = 0;
    int child_route = 0;
};

struct LoadStand {
    ScriptedLoads loads;
    const Ref<PackedScene> level = a_level();
    LoopbackRig rig;
    StockWorld world;
    Recipe recipe;

    explicit LoadStand(Recipe p_recipe, bool p_migrates = false)
        : rig(1, p_migrates), recipe(p_recipe) {
        rig.mount();
        rig.spawn_plane(0)->set_park_timeout_seconds(SHORT_PARK_DEADLINE);
        if (recipe == FROM_SPAWNER) {
            PackedStringArray scenes;
            scenes.push_back(level->get_path());
            world = mount_stock_world(rig, scenes);
        }
    }

    Node *spawn_root(const char *p_name) {
        NetwMultiplayer *server = rig.server();
        if (recipe == SCENE_ROOT) {
            Node *root = server->scene_spawn(
                level->get_path(),
                NetwMultiplayer::SCENE_ISOLATION_NONE
            );
            REQUIRE(root != nullptr);
            server->scene_watch(server->entity_of(root), rig.peer_id(0));
            return root;
        }
        Node *root = level->instantiate();
        root->set_name(p_name);
        world.arena(-1)->add_child(root, true);
        return root;
    }

    Loading spawn(bool p_with_child = true) {
        Loading out;
        out.root = spawn_root("Loaded");
        rig.pump(2);
        out.root_route = route_of(out.root);
        REQUIRE(out.root_route > 0);
        if (p_with_child) {
            out.child_route = rig.spawn_registered(
                StringName("child"),
                callable_mp_static(&build_body),
                named("Child"),
                one_type(),
                out.root
            );
        }
        return out;
    }

    Node *received(int p_route) const {
        NetwMultiplayer *client = rig.client(0);
        return client->entity_get_node(client->entity_from_route(p_route));
    }

    void despawn(Node *p_root) {
        p_root->get_parent()->remove_child(p_root);
        rig.pump(SETTLE);
        memdelete(p_root);
    }

    SceneLoads::Status book_status() const {
        return SceneLoads::shared().status(level->get_path());
    }
};

TEST_CASE(
    "[Networked][Spawn] LC1 LC2 LC3 a spawn whose scene is still loading "
    "waits for it past every park deadline, and its children arrive under it "
    "once it loads, built by a spawn function that finds it ready"
) {
    const Recipe RECIPES[] = {SCENE_ROOT, FROM_SPAWNER};
    for (const Recipe recipe : RECIPES) {
        INFO(doctest::String(recipe_name(recipe)));
        LoadStand stand(recipe);
        const Loading loading = stand.spawn();
        stand.rig.pump(PAST_THE_DEADLINE);
        CHECK(stand.received(loading.root_route) == nullptr);
        CHECK(stand.received(loading.child_route) == nullptr);

        client_build = ClientBuild{stand.rig.client(0), loading.root_route};
        stand.loads.finish();
        stand.rig.pump(SETTLE);
        const ClientBuild seen = client_build;
        client_build = ClientBuild{};

        Node *root = stand.received(loading.root_route);
        Node *child = stand.received(loading.child_route);
        REQUIRE(root != nullptr);
        REQUIRE(child != nullptr);
        CHECK(child->get_parent() == root);
        NETW_CHECK_EQ(seen.built, 1);
        NETW_CHECK_EQ(seen.under_a_ready_root, 1);
    }
}

TEST_CASE(
    "[Networked][Spawn] LC4 LC5 LC10 a spawn that ends while its scene loads "
    "builds nothing once the load lands, and lets go of the scene"
) {
    const char *ENDINGS[] = {"despawned", "session ended", "server lost"};
    for (const char *ending : ENDINGS) {
        INFO(doctest::String(ending));
        const bool lost = String(ending) == "server lost";
        LoadStand stand(FROM_SPAWNER, lost);
        const Loading loading = stand.spawn();
        if (String(ending) == "despawned") {
            stand.despawn(loading.root);
        } else if (lost) {
            REQUIRE(stand.rig.session()->lose_server() == OK);
            stand.rig.pump(SETTLE);
            REQUIRE(stand.rig.client(0)->is_host());
        } else {
            stand.rig.client(0)->clear_session_state();
        }

        stand.loads.finish();
        stand.rig.pump(SETTLE);

        CHECK(stand.received(loading.root_route) == nullptr);
        CHECK(stand.received(loading.child_route) == nullptr);
        NETW_CHECK_EQ(
            int(stand.book_status()),
            int(SceneLoads::STATUS_NONE)
        );
    }
}

TEST_CASE(
    "[Networked][Spawn] LC6 a scene that fails to load drops its spawn with "
    "one warning, and its children with it"
) {
    LoadStand stand(FROM_SPAWNER);
    const Loading loading = stand.spawn();
    const FaultWatch watch;

    stand.loads.fails = true;
    stand.loads.finish();
    stand.rig.pump(SETTLE);

    NETW_CHECK_EQ(
        watch.log->warning_count_containing("does not exist on this peer"),
        int64_t(1)
    );
    CHECK(stand.received(loading.root_route) == nullptr);
    CHECK(stand.received(loading.child_route) == nullptr);
    NETW_CHECK_EQ(stand.rig.spawn_plane(0)->get_park().size(), 0);
}

TEST_CASE(
    "[Networked][Spawn] LC7 a scene stays loaded while an entity built from "
    "it lives, so a second spawn of it builds at once"
) {
    LoadStand stand(FROM_SPAWNER);
    const Loading first = stand.spawn(false);
    stand.loads.finish();
    stand.rig.pump(SETTLE);
    REQUIRE(stand.received(first.root_route) != nullptr);

    stand.loads.loading = true;
    Node *second = stand.spawn_root("Again");
    stand.rig.pump(SETTLE);

    CHECK(stand.received(route_of(second)) != nullptr);
    NETW_CHECK_EQ(stand.loads.requests, 1);

    stand.despawn(first.root);
    stand.despawn(second);
    NETW_CHECK_EQ(int(stand.book_status()), int(SceneLoads::STATUS_NONE));
}

TEST_CASE(
    "[Networked][Spawn] LC9 a spawn waiting for a MultiplayerSpawner that "
    "never appears is dropped at its deadline, though nothing else spawns"
) {
    const Ref<PackedScene> level = a_level();
    LoopbackRig rig(1);
    rig.mount();
    rig.spawn_plane(0)->set_park_timeout_seconds(SHORT_PARK_DEADLINE);
    StockWorld world;
    world.scenes.push_back(level->get_path());
    seat_stock_branch(rig.branch(-1), world);
    rig.pump(2);
    const FaultWatch watch;

    world.arena(-1)->add_child(level->instantiate(), true);
    rig.pump(PAST_THE_DEADLINE);

    NETW_CHECK_EQ(
        watch.log->warning_count_containing("never appeared on this peer"),
        int64_t(1)
    );
    NETW_CHECK_EQ(rig.spawn_plane(0)->get_park().size(), 0);
}

} // namespace TestSpawnLoadLaws

#endif
