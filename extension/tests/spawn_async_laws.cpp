#include "support/fault_log.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/action.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/spawn/pipeline.hpp"

using namespace godot;

namespace TestSpawnAsyncLaws {

using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw::NetwPromise;

constexpr const char *SPAWNER_ID = "spawn_async_spawner";

constexpr const char *SPAWNER_SOURCE = R"(extends Node

signal go

static var clients_wait := false
static var made_script: Script

var outcome := "now"
var pending: NetwPromise
var late_id := 0
var calls := 0

static func client_builds_wait(waits: bool, script: Script) -> void:
	clients_wait = waits
	made_script = script

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_spawn(make)

func make():
	calls += 1
	if clients_wait and not multiplayer.is_server():
		if outcome == "promise fails":
			pending = NetwPromise.new()
			return pending
		await go
		if outcome == "null":
			return null
		return fresh()
	if outcome == "promise" or outcome == "promise fails":
		pending = NetwPromise.new()
		return pending
	if outcome != "now":
		await go
	if outcome == "null":
		return null
	return fresh()

func fresh() -> Node:
	var made: Node = made_script.new() if made_script != null else Node.new()
	made.name = &"Made"
	late_id = made.get_instance_id()
	return made

func finish() -> void:
	if outcome == "promise":
		pending.resolve(fresh())
	elif outcome == "promise fails":
		pending.reject(ERR_TIMEOUT, "slow")
	else:
		go.emit()

func begin() -> NetwPromise:
	return Netw.spawn_async(make)
)";

constexpr const char *MADE_SOURCE = R"(extends Node

var pings := 0

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_rpc(ping)

func ping() -> void:
	pings += 1

func send() -> void:
	Netw.rpc(ping)
)";

constexpr int A = 0;
constexpr int B = 1;
constexpr int SERVER = -1;
constexpr int TICKRATE = 30;
constexpr double SHORT_PARK_DEADLINE = 0.2;
constexpr int PAST_THE_DEADLINE = 30;

Script *spawner_script = nullptr;

Node *build_spawner(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(spawner_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

enum Ending {
    RETURNS,
    SESSION_ENDS,
    HOST_FREED,
};

struct Row {
    const char *outcome;
    Ending ending;
    Error code;
};

const Row ROWS[] = {
    {"node", RETURNS, OK},
    {"promise", RETURNS, OK},
    {"null", RETURNS, ERR_INVALID_PARAMETER},
    {"promise fails", RETURNS, ERR_TIMEOUT},
    {"node", SESSION_ENDS, ERR_UNAVAILABLE},
    {"node", HOST_FREED, ERR_UNAVAILABLE},
};

int booked(NetwMultiplayer *p_api) {
    netw::spawn::Book *book = p_api->spawn_plane()->get_spawn_book();
    return book->armed_count() + book->spawned_count();
}

struct Stand {
    netw_test::SimStand sim;
    Node *arena = nullptr;
    Node *host = nullptr;
    Node *client_host = nullptr;
    int host_route = 0;

    explicit Stand(int p_clients = 1) : sim(p_clients) {
        REQUIRE(sim.ready());
        sim.teach(StringName(SPAWNER_ID), callable_mp_static(&build_spawner));
        sim.mount();
        arena = sim.arena();
        REQUIRE(arena != nullptr);
        sim.arm(TICKRATE);
        Array args;
        args.push_back(String("Spawner"));
        NetwMultiplayer *server = sim.session(SERVER);
        const RID made
            = server->spawn_registered(StringName(SPAWNER_ID), args, nullptr);
        host = server->entity_get_node(made);
        REQUIRE(host != nullptr);
        arena->add_child(host);
        sim.pump(8);
        host_route = int(server->entity_get_route(made));
        client_host = sim.node_at(A, host_route);
        REQUIRE(client_host != nullptr);
    }

    Node *made_on(int p_client) const {
        return sim.arena_of(p_client)->find_child("Made", true, false);
    }
};

TEST_CASE(
    "[Networked][Spawn] SA1 Netw.spawn_async spawns nothing until the spawn "
    "function returns, then settles with the node outside the tree, or "
    "fails with the reason it could not"
) {
    const Ref<Script> spawner = netw_test::minted_script(SPAWNER_SOURCE);
    REQUIRE(spawner.is_valid());
    spawner_script = spawner.ptr();

    for (const Row &row : ROWS) {
        NETW_FORMAT_TEXT(outcome_text, row.outcome);
        CAPTURE(outcome_text);
        CAPTURE(int(row.ending));

        Stand stand;
        NetwMultiplayer *server = stand.sim.session(SERVER);
        NetwMultiplayer *client = stand.sim.session(A);
        stand.host->set(StringName("outcome"), String(row.outcome));
        const int server_before = booked(server);
        const int client_before = booked(client);

        netw_test::FaultWatch faults;
        if (row.code == ERR_INVALID_PARAMETER) {
            faults.expect_error("*did not return a Node outside the tree*", 1);
        }
        const Ref<NetwPromise> promise = stand.host->call(StringName("begin"));
        REQUIRE(promise.is_valid());
        stand.sim.step_ticks(8);
        CHECK_FALSE(promise->get_is_settled());
        NETW_CHECK_EQ(booked(server), server_before);
        NETW_CHECK_EQ(booked(client), client_before);

        if (row.ending == SESSION_ENDS) {
            server->session_announce_ended();
        } else if (row.ending == HOST_FREED) {
            stand.arena->remove_child(stand.host);
            memdelete(stand.host);
            stand.host = nullptr;
            stand.sim.pump(2);
        }
        if (stand.host != nullptr) {
            stand.host->call(StringName("finish"));
        }
        stand.sim.step_ticks(2);

        REQUIRE(promise->get_is_settled());
        NETW_CHECK_EQ(int(promise->get_code()), int(row.code));
        if (row.code != OK) {
            if (row.ending == SESSION_ENDS) {
                const ObjectID late = ObjectID(
                    uint64_t(int64_t(stand.host->get(StringName("late_id"))))
                );
                CHECK(netw::gd::instance_from_id(late) == nullptr);
            }
            continue;
        }
        Node *node = Object::cast_to<Node>(promise->get_result());
        REQUIRE(node != nullptr);
        CHECK_FALSE(node->is_inside_tree());
        stand.arena->add_child(node);
        stand.sim.step_ticks(4);
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        REQUIRE(entity.is_valid());
        CHECK(stand.sim.node_at(A, int(entity->get_route())) != nullptr);
    }
    spawner_script = nullptr;
}

TEST_CASE(
    "[Networked][Spawn] SA1 Netw.spawn_async on a client fails "
    "ERR_UNAUTHORIZED and calls nothing"
) {
    const Ref<Script> spawner = netw_test::minted_script(SPAWNER_SOURCE);
    REQUIRE(spawner.is_valid());
    spawner_script = spawner.ptr();
    Stand stand;
    netw_test::FaultWatch faults;
    faults.expect_error("*Netw.spawn_async runs on the server.*", 1);

    const Ref<NetwPromise> promise
        = stand.client_host->call(StringName("begin"));

    REQUIRE(promise.is_valid());
    CHECK(promise->get_is_failed());
    NETW_CHECK_EQ(int(promise->get_code()), int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(int(stand.client_host->get(StringName("late_id"))), 0);
    spawner_script = nullptr;
}

struct ClientsWait {
    Ref<Script> spawner = netw_test::minted_script(SPAWNER_SOURCE);
    Ref<Script> made = netw_test::minted_script(MADE_SOURCE);

    ClientsWait() {
        REQUIRE(spawner.is_valid());
        REQUIRE(made.is_valid());
        spawner_script = spawner.ptr();
        spawner->call(StringName("client_builds_wait"), true, made);
    }

    ~ClientsWait() {
        spawner->call(StringName("client_builds_wait"), false, Variant());
        spawner_script = nullptr;
    }
};

Node *spawn_made(Stand &r_stand) {
    const Ref<NetwPromise> promise = r_stand.host->call(StringName("begin"));
    REQUIRE(promise.is_valid());
    REQUIRE(promise->get_is_completed());
    Node *made = Object::cast_to<Node>(promise->get_result());
    REQUIRE(made != nullptr);
    r_stand.arena->add_child(made);
    return made;
}

TEST_CASE(
    "[Networked][Spawn] SA2 a client whose spawn function awaits holds the "
    "route with no deadline, so a child and an RPC sent while it builds land "
    "once it returns, and the function runs once"
) {
    const ClientsWait scripts;
    for (const bool late : {false, true}) {
        CAPTURE(late);
        Stand stand(2);
        const int viewer = late ? B : A;
        NetwMultiplayer *server = stand.sim.session(SERVER);
        for (const int client : {A, B}) {
            stand.sim.session(client)->spawn_plane()->set_park_timeout_seconds(
                SHORT_PARK_DEADLINE
            );
        }

        Node *made = spawn_made(stand);
        stand.sim.pump(4);
        Array args;
        args.push_back(String("Child"));
        const RID child
            = server->spawn_registered(StringName(SPAWNER_ID), args, nullptr);
        REQUIRE(child.is_valid());
        made->add_child(server->entity_get_node(child));
        made->call(StringName("send"));
        stand.sim.step_ticks(PAST_THE_DEADLINE);
        CHECK(stand.made_on(viewer) == nullptr);

        Node *viewer_host = stand.sim.node_at(viewer, stand.host_route);
        REQUIRE(viewer_host != nullptr);
        viewer_host->call(StringName("finish"));
        stand.sim.step_ticks(4);

        Node *built = stand.made_on(viewer);
        CHECK(built != nullptr);
        if (built == nullptr) {
            continue;
        }
        CHECK(built->find_child("Child", false, false) != nullptr);
        NETW_CHECK_EQ(int(built->get(StringName("pings"))), 1);
        NETW_CHECK_EQ(int(viewer_host->get(StringName("calls"))), 1);
    }
}

TEST_CASE(
    "[Networked][Spawn] SA3 a spawn that ends while a client builds it is "
    "dropped there, and the node its spawn function returns later is freed"
) {
    const ClientsWait scripts;
    for (const char *ending : {"despawned", "hidden", "session ended"}) {
        NETW_FORMAT_TEXT(ending_text, ending);
        CAPTURE(ending_text);
        const String ended(ending);
        Stand stand;
        NetwMultiplayer *server = stand.sim.session(SERVER);
        const int64_t viewer = stand.sim.peer_id(A);

        const Ref<NetwInterestLayer> gate
            = server->interest_layer(StringName("gate"));
        REQUIRE(gate.is_valid());
        gate->set_default_leave_policy(NetwMultiplayer::LEAVE_POLICY_HIDE);
        Node *made = spawn_made(stand);
        gate->add_entity(NetwEntity::of(made));
        gate->add_viewer(viewer);
        server->interest_flush_now();
        stand.sim.step_ticks(4);
        REQUIRE(stand.made_on(A) == nullptr);

        if (ended == "despawned") {
            stand.arena->remove_child(made);
            memdelete(made);
        } else if (ended == "hidden") {
            gate->remove_viewer(viewer);
            server->interest_flush_now();
        } else {
            stand.sim.session(A)->clear_session_state();
        }
        stand.sim.step_ticks(4);
        stand.client_host->call(StringName("finish"));
        stand.sim.step_ticks(4);

        CHECK(stand.made_on(A) == nullptr);
        const ObjectID late = ObjectID(
            uint64_t(int64_t(stand.client_host->get(StringName("late_id"))))
        );
        CHECK(late.is_valid());
        CHECK(netw::gd::instance_from_id(late) == nullptr);
    }
}

struct Failure {
    const char *outcome;
    const char *warning;
    const char *reported;
};

const Failure FAILURES[] = {
    {"null",
     "*Spawn function 'make' did not return a Node*",
     "*because 'make' returned no Node on their peer*"},
    {"promise fails",
     "*Spawn function 'make' failed with 'slow'*",
     "*because 'make' failed with 'slow' on their peer*"},
    {"host freed", "*was freed before it returned*", nullptr},
};

TEST_CASE(
    "[Networked][Spawn] SA3 a client whose spawn function returns no node, "
    "fails, or loses its host drops the route with one warning"
) {
    const ClientsWait scripts;
    for (const Failure &failure : FAILURES) {
        NETW_FORMAT_TEXT(outcome_text, failure.outcome);
        CAPTURE(outcome_text);
        Stand stand;
        NetwMultiplayer *client = stand.sim.session(A);
        stand.client_host->set(StringName("outcome"), String(failure.outcome));
        netw_test::FaultWatch faults;
        faults.expect_warning(failure.warning, 1);
        if (failure.reported != nullptr) {
            faults.expect_warning(failure.reported, 1);
        }

        const int route = int(NetwEntity::of(spawn_made(stand))->get_route());
        stand.sim.step_ticks(4);
        REQUIRE(client->spawn_plane()->get_park().has(route));

        if (String(failure.outcome) == "host freed") {
            stand.client_host->get_parent()->remove_child(stand.client_host);
            memdelete(stand.client_host);
        } else {
            stand.client_host->call(StringName("finish"));
        }
        stand.sim.step_ticks(4);

        CHECK_FALSE(client->spawn_plane()->get_park().has(route));
        CHECK(stand.made_on(A) == nullptr);
    }
}

TEST_CASE(
    "[Networked][Spawn] SA4 a spawn function still waiting after "
    "park_timeout_seconds warns once on the peer running it"
) {
    const ClientsWait scripts;
    for (const bool on_client : {false, true}) {
        CAPTURE(on_client);
        scripts.spawner
            ->call(StringName("client_builds_wait"), on_client, scripts.made);
        Stand stand;
        for (const int peer : {SERVER, A}) {
            stand.sim.session(peer)->spawn_plane()->set_park_timeout_seconds(
                SHORT_PARK_DEADLINE
            );
        }
        netw_test::FaultWatch faults;
        faults.expect_warning(
            "*Spawn function 'make' has not returned after 0.2 seconds.*",
            1
        );
        Node *runner = stand.client_host;
        Ref<NetwPromise> promise;
        if (on_client) {
            spawn_made(stand);
        } else {
            runner = stand.host;
            runner->set(StringName("outcome"), String("node"));
            promise = runner->call(StringName("begin"));
        }
        stand.sim.step_ticks(PAST_THE_DEADLINE);
        runner->call(StringName("finish"));
        if (promise.is_valid()) {
            REQUIRE(promise->get_is_completed());
            Node *made = Object::cast_to<Node>(promise->get_result());
            stand.arena->add_child(made);
        }
        stand.sim.step_ticks(4);
        CHECK(stand.made_on(A) != nullptr);
    }
}

TEST_CASE(
    "[Networked][Spawn] NetwActionContext.bind_result refuses a node "
    "Netw.spawn_async built"
) {
    const Ref<Script> spawner = netw_test::minted_script(SPAWNER_SOURCE);
    REQUIRE(spawner.is_valid());
    spawner_script = spawner.ptr();
    Stand stand;
    netw_test::FaultWatch faults;
    faults.expect_error(
        "*bind_result got 'Made', which Netw.spawn_async built.*",
        1
    );

    Node *made = spawn_made(stand);
    Ref<netw::NetwActionContext> context;
    context.instantiate();
    context->bind_result(made);

    CHECK_FALSE(context->answered());
    spawner_script = nullptr;
}

} // namespace TestSpawnAsyncLaws

#endif
