#include "support/fault_log.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestSpawnWaitRefusalLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *SPAWNER_ID = "spawn_wait_spawner";
constexpr const char *WAITING_ID = "waiting";

constexpr const char *SPAWNER_SOURCE = R"(extends Node

signal go

var waits := false
var promised := false
var pending: NetwPromise
var late_id := 0

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_spawn(make)

func make():
	if promised:
		return promise()
	if waits:
		await go
	return fresh()

func construct(_name: String):
	if promised:
		return promise()
	if waits:
		await go
	return fresh()

func promise() -> NetwPromise:
	pending = NetwPromise.new()
	return pending

func fresh() -> Node:
	var made := Node.new()
	late_id = made.get_instance_id()
	return made

func finish() -> void:
	if pending != null:
		pending.resolve(fresh())
	else:
		go.emit()

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

enum Wait {
    COROUTINE,
    PROMISE,
    REGISTRY,
};

struct Row {
    const char *name;
    int waiter;
    Wait wait;
    bool error;
    const char *message;
};

constexpr const char *FN_FIX
    = "Use Netw.spawn_async for a spawn function that awaits.";
constexpr const char *REGISTRY_FIX = "Return the node without waiting.";

const Row ROWS[] = {
    {"server coroutine",
     SERVER,
     COROUTINE,
     true,
     "Spawn function 'make' waited."},
    {"client coroutine",
     A,
     COROUTINE,
     false,
     "Spawn function 'make' waited, so route"},
    {"server promise", SERVER, PROMISE, true, "Spawn function 'make' waited."},
    {"client promise",
     A,
     PROMISE,
     false,
     "Spawn function 'make' waited, so route"},
    {"server registry",
     SERVER,
     REGISTRY,
     true,
     "Spawn constructor 'waiting' waited."},
    {"client registry",
     A,
     REGISTRY,
     false,
     "Spawn constructor 'waiting' waited, so route"},
};

void register_waiting(NetwMultiplayer *p_api, Node *p_host) {
    Array types;
    types.push_back(int(Variant::STRING));
    Array quantizers;
    quantizers.push_back(Variant());
    p_api->spawn_register_constructor(
        StringName(WAITING_ID),
        Callable(p_host, StringName("construct")),
        types,
        quantizers
    );
}

Node *spawn_waiting(NetwMultiplayer *p_server, Node *p_arena) {
    Array args;
    args.push_back(String("Waited"));
    const RID made
        = p_server->spawn_registered(StringName(WAITING_ID), args, nullptr);
    Node *node = made.is_valid() ? p_server->entity_get_node(made) : nullptr;
    if (node != nullptr) {
        p_arena->add_child(node);
    }
    return node;
}

TEST_CASE(
    "[Networked][Spawn] a spawn function or constructor that waits is "
    "refused on the server and dropped on a client, the message says it "
    "waited, and the node it returns later is freed"
) {
    const Ref<Script> spawner = netw_test::minted_script(SPAWNER_SOURCE);
    REQUIRE(spawner.is_valid());
    spawner_script = spawner.ptr();

    for (const Row &row : ROWS) {
        NETW_FORMAT_TEXT(row_text, row.name);
        CAPTURE(row_text);

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
        register_waiting(server, host);
        register_waiting(stand.session(A), client_host);
        Node *waiter = row.waiter == SERVER ? host : client_host;
        waiter->set(StringName("waits"), true);
        waiter->set(StringName("promised"), row.wait == PROMISE);

        const netw_test::FaultWatch faults;
        Node *spawned = row.wait == REGISTRY
            ? spawn_waiting(server, arena)
            : Object::cast_to<Node>(host->call(StringName("spawn_one")));
        stand.step_ticks(8);

        CHECK((spawned != nullptr) == (row.waiter != SERVER));
        if (spawned != nullptr) {
            const Ref<NetwEntity> entity = NetwEntity::of(spawned);
            REQUIRE(entity.is_valid());
            CHECK(stand.node_at(A, int(entity->get_route())) == nullptr);
        }
        const String fix = row.wait == REGISTRY ? REGISTRY_FIX : FN_FIX;
        for (const String message : {String(row.message), fix}) {
            NETW_CHECK_EQ(
                row.error ? faults.errors_containing(message)
                          : faults.warnings_containing(message),
                1
            );
        }

        waiter->call(StringName("finish"));
        const ObjectID late
            = ObjectID(uint64_t(int64_t(waiter->get(StringName("late_id")))));
        CHECK(late.is_valid());
        CHECK(netw::gd::instance_from_id(late) == nullptr);
    }
    spawner_script = nullptr;
}

} // namespace TestSpawnWaitRefusalLaws

#endif
