#include "support/fault_log.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestSpawnWaitRefusalLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *SPAWNER_ID = "spawn_wait_spawner";

constexpr const char *SPAWNER_SOURCE = R"(extends Node

signal never

var waits := false

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_spawn(make)

func make() -> Node:
	if waits:
		await never
	return Node.new()

func spawn_one() -> Node:
	var made: Node = Netw.spawn(make)
	if made != null:
		get_parent().add_child(made)
	return made
)";

constexpr int A = 0;
constexpr int SERVER = -1;
constexpr int TICKRATE = 30;

Script *spawner_script = nullptr;

Node *build_spawner(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(spawner_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

struct Row {
    const char *peer;
    int waiter;
    bool server_spawns;
    bool error;
    const char *message;
};

const Row ROWS[] = {
    {"server", SERVER, false, true, "Spawn function 'make' waited."},
    {"client", A, true, false, "Spawn function 'make' waited, so route"},
};

constexpr const char *FIX
    = "Load what it needs with Netw.request_all before calling Netw.spawn.";

TEST_CASE(
    "[Networked][Spawn] a spawn function that waits is refused on the server "
    "and dropped on a client, and the message says it waited"
) {
    const Ref<Script> spawner = netw_test::minted_script(SPAWNER_SOURCE);
    REQUIRE(spawner.is_valid());
    spawner_script = spawner.ptr();

    for (const Row &row : ROWS) {
        NETW_FORMAT_TEXT(peer_text, row.peer);
        CAPTURE(peer_text);

        netw_test::SimStand stand(1);
        REQUIRE(stand.ready());
        stand.teach(StringName(SPAWNER_ID), callable_mp_static(&build_spawner));
        stand.mount();
        Node *arena = stand.arena();
        REQUIRE(arena != nullptr);
        stand.arm(TICKRATE);
        stand.session(A)->session_submit_join(StringName("a"), Array());
        stand.pump(4);

        NetwMultiplayer *server = stand.session(SERVER);
        Array args;
        args.push_back(String("Spawner"));
        const RID made
            = server->spawn_registered(StringName(SPAWNER_ID), args, nullptr);
        Node *host = server->entity_get_node(made);
        REQUIRE(host != nullptr);
        arena->add_child(host);
        stand.pump(8);
        const int host_route = int(server->entity_get_route(made));
        Node *client_host = stand.node_at(A, host_route);
        REQUIRE(client_host != nullptr);
        Node *waiter = row.waiter == SERVER ? host : client_host;
        waiter->set(StringName("waits"), true);

        const netw_test::FaultWatch faults;
        Node *spawned
            = Object::cast_to<Node>(host->call(StringName("spawn_one")));
        stand.step_ticks(8);

        CHECK((spawned != nullptr) == row.server_spawns);
        if (spawned != nullptr) {
            const Ref<NetwEntity> entity = NetwEntity::of(spawned);
            REQUIRE(entity.is_valid());
            CHECK(stand.node_at(A, int(entity->get_route())) == nullptr);
        }
        for (const String message : {String(row.message), String(FIX)}) {
            NETW_CHECK_EQ(
                row.error ? faults.errors_containing(message)
                          : faults.warnings_containing(message),
                1
            );
        }
    }
    spawner_script = nullptr;
}

} // namespace TestSpawnWaitRefusalLaws

#endif
