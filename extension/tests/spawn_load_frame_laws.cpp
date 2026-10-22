#include "support/frame_drive.h"
#include "support/netw_test.h"

#include <memory>

#if defined(NETW_TIER_HOSTED)

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/spawn/pipeline.hpp"

#include <godot_cpp/classes/multiplayer_api.hpp>

using namespace godot;

namespace NetwTests {

namespace {

const char *LOAD_LEVEL = "res://tests/support/load_level.tscn";
constexpr int FRAME_BUDGET = 240;

struct LoadEvidence {
    bool driven = false;
    bool built = false;
    int frames_waiting = 0;
};

LoadEvidence &load_evidence() {
    static LoadEvidence evidence;
    return evidence;
}

class LoadPair {
    Ref<netw::LocalLoopbackSession> link;
    Ref<netw::LocalMultiplayerPeer> client_peer;
    Ref<netw::NetwMultiplayer> sides[2];
    Node *branches[2] = {nullptr, nullptr};

    static Ref<netw::NetwMultiplayer> seat(
        const Ref<netw::LocalMultiplayerPeer> &p_peer
    ) {
        Ref<netw::NetwMultiplayer> api
            = netw::NetwMultiplayer::create(Ref<Script>());
        api->session_default_join_held = true;
        api->session_get_inner()->set_root_path(NodePath("/"));
        api->set("multiplayer_peer", p_peer);
        return api;
    }

public:
    LoadPair() {
        link.instantiate();
        client_peer = link->create_client_peer();
        sides[0] = seat(link->get_server_peer());
        sides[1] = seat(client_peer);
        pump();
        const char *names[2] = {"LoadServer", "LoadClient"};
        for (int side = 0; side < 2; ++side) {
            branches[side] = memnew(Node);
            branches[side]->set_name(names[side]);
            netw::gd::scene_root()->add_child(branches[side]);
            sides[side]->session_set_root(
                Callable(branches[side], "get_node").bind(NodePath("."))
            );
            netw::gd::scene_tree()->set_multiplayer(
                Ref<MultiplayerAPI>(
                    Object::cast_to<MultiplayerAPI>(sides[side].ptr())
                ),
                branches[side]->get_path()
            );
        }
        pump();
    }

    ~LoadPair() {
        for (int side = 0; side < 2; ++side) {
            netw::gd::scene_tree()->set_multiplayer(
                Ref<MultiplayerAPI>(),
                branches[side]->get_path()
            );
            netw::gd::scene_root()->remove_child(branches[side]);
            memdelete(branches[side]);
        }
        link->reset();
    }

    LoadPair(const LoadPair &) = delete;
    LoadPair &operator=(const LoadPair &) = delete;

    void pump() {
        link->poll();
        sides[0]->poll();
        sides[1]->poll();
    }

    int open_level() {
        netw::NetwMultiplayer *server = sides[0].ptr();
        Node *root = server->scene_spawn(
            String(LOAD_LEVEL),
            netw::NetwMultiplayer::SCENE_ISOLATION_NONE
        );
        if (root == nullptr) {
            return 0;
        }
        const RID scene = server->entity_of(root);
        server->scene_watch(scene, client_peer->get_unique_id());
        return int(server->entity_get_route(scene));
    }

    bool client_waits() const {
        netw::ReplicationCore *plane = sides[1]->get_replication_plane();
        if (plane == nullptr) {
            return false;
        }
        const Dictionary counters = plane->get_spawn_pipeline()->counters();
        return int64_t(counters[StringName("spawn_deferrals")]) > 0;
    }

    Node *client_node(int p_route) const {
        netw::NetwMultiplayer *client = sides[1].ptr();
        return client->entity_get_node(client->entity_from_route(p_route));
    }
};

class LoadLevelScenario final : public netw_test::FrameScenario {
    std::unique_ptr<LoadPair> pair;
    int route = 0;
    int frames = 0;

    void close(bool p_built) {
        LoadEvidence &evidence = load_evidence();
        evidence.built = p_built;
        evidence.driven = true;
        pair.reset();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (pair == nullptr) {
            pair = std::make_unique<LoadPair>();
            route = pair->open_level();
            if (route <= 0) {
                close(false);
                return false;
            }
            return true;
        }
        pair->pump();
        const bool built = pair->client_node(route) != nullptr;
        if (!built && pair->client_waits()) {
            load_evidence().frames_waiting += 1;
        }
        if (built || ++frames >= FRAME_BUDGET) {
            close(built);
            return false;
        }
        return true;
    }
};

NETW_FRAME_SCENARIO(LoadLevelScenario, load_level_scenario);

} // namespace

TEST_CASE(
    "[Networked][Spawn][Frame] LC8 a client keeps running frames while a "
    "scene root it was sent loads in the background, then builds it"
) {
    const LoadEvidence &evidence = load_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.built);
    CHECK(evidence.frames_waiting > 0);
}

} // namespace NetwTests

#endif
