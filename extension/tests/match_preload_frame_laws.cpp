#include "support/frame_drive.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/minted_script.h"
#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestMatchPreloadFrameLaws {

using netw::NetwMultiplayer;

constexpr const char *HOST_ID = "match_host";
constexpr const char *HOST_PATH = "res://tests/support/match_host.gd";
constexpr int TICKRATE = 30;
constexpr int LAST_FRAME = 600;

Script *host_script = nullptr;

Node *build_host(const Variant &p_name) {
    if (host_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(host_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct MatchEvidence {
    bool driven = false;
    bool seated = false;
    bool loads_completed = false;
    int answers = 0;
    Variant client_answer;
    bool opened = false;
    int players = 0;
    int players_on_client = 0;
};

MatchEvidence &match_evidence() {
    static MatchEvidence evidence;
    return evidence;
}

class MatchScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    int route = 0;

    Node *host() const {
        return stand->node_at(-1, route);
    }

    int route_of(const Variant &p_node) const {
        Node *node = Object::cast_to<Node>(netw::gd::live_object(p_node));
        NetwMultiplayer *server = stand->session(-1);
        return node != nullptr
            ? int(server->entity_get_route(server->entity_of(node)))
            : 0;
    }

    bool open() {
        script = netw_test::script_from(HOST_PATH);
        if (script.is_null()) {
            return false;
        }
        host_script = script.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(HOST_ID), callable_mp_static(&build_host));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        for (int at = -1; at < 1; ++at) {
            stand->session(at)->session_submit_join(
                StringName(vformat("seat%d", at + 1)),
                Array()
            );
            stand->pump(4);
        }
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String("Host"));
        const RID made
            = server->spawn_registered(StringName(HOST_ID), args, nullptr);
        Node *built = server->entity_get_node(made);
        if (built == nullptr || arena == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(8);
        route = int(server->entity_get_route(made));
        return stand->node_at(0, route) != nullptr;
    }

    bool read() {
        MatchEvidence &seen = match_evidence();
        Object *loads = netw::gd::live_object(host()->get("loads"));
        if (loads != nullptr && bool(loads->get("is_completed"))) {
            seen.loads_completed = true;
            const Dictionary results = loads->get("results");
            seen.answers = int(results.size());
            seen.client_answer = results.get(stand->peer_id(0), Variant());
        }
        Object *arena = netw::gd::live_object(host()->get("arena"));
        seen.opened = arena != nullptr && arena->is_class("Node");
        if (!seen.opened) {
            return false;
        }
        Node *client_root = stand->node_at(0, route_of(arena));
        const Array players = host()->get("players");
        seen.players = int(players.size());
        seen.players_on_client = 0;
        for (int at = 0; at < players.size(); ++at) {
            Node *mirrored = stand->node_at(0, route_of(players[at]));
            if (client_root != nullptr && mirrored != nullptr
                && mirrored->get_parent() == client_root) {
                seen.players_on_client += 1;
            }
        }
        return seen.players > 0 && seen.players_on_client == seen.players;
    }

    void close() {
        match_evidence().driven = true;
        delete stand;
        stand = nullptr;
        host_script = nullptr;
        script = Ref<Script>();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                close();
                done = true;
                return false;
            }
            match_evidence().seated = true;
            host()->call(StringName("start_match"));
        }
        if (read() || frame >= LAST_FRAME) {
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(MatchScenario, match_preload_scenario);

TEST_CASE(
    "[Networked][Scene][Frame] MP1 a host preloads a level on every client, "
    "spawns it once each one has it, and spawns players the clients see "
    "under it"
) {
    const MatchEvidence &seen = match_evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    CHECK(seen.loads_completed);
    NETW_CHECK_EQ(seen.answers, 1);
    CHECK(seen.client_answer == Variant(true));
    CHECK(seen.opened);
    NETW_CHECK_GT(seen.players, 0);
    NETW_CHECK_EQ(seen.players_on_client, seen.players);
}

} // namespace TestMatchPreloadFrameLaws

#endif
