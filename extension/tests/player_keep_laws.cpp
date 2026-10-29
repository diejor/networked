#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/frame_drive.h"
#include "support/loopback_rig.h"
#include "support/minted_script.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/session.hpp"
#include "netw/api/session_config.hpp"
#include "netw/entity/stage.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestPlayerKeepLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw::NetwPromise;
using netw_test::LoopbackRig;

constexpr int SETTLE = 8;
constexpr int64_t SERVER = 1;
constexpr const char *AVATAR_ID = "kept_avatar";
constexpr const char *SPAWNER_ID = "kept_spawner";

constexpr const char *ACCOUNT_GATE = R"(extends Node

func admit(request: NetwJoinRequest) -> Error:
	request.account = "test:" + request.username
	return OK
)";

constexpr const char *SPAWNER_SOURCE = R"(extends Node

signal go

var who: NetwPlayer
var built := 0

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_spawn(make)

func make():
	if multiplayer.is_server():
		await go
	var made := Node2D.new()
	made.name = &"Late"
	built = made.get_instance_id()
	if multiplayer.is_server():
		Netw.configure_entity(made).player = who
	return made

func finish() -> void:
	go.emit()

func begin() -> NetwPromise:
	return Netw.spawn_async(make)
)";

Script *spawner_script = nullptr;

Node *build_avatar(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Node *build_spawner(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(spawner_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

struct World {
    Ref<Script> gate_shape = netw_test::minted_script(ACCOUNT_GATE);
    Ref<Script> spawner = netw_test::minted_script(SPAWNER_SOURCE);
    LoopbackRig rig;
    RID arena;
    bool outside_scenes = false;

    World(double p_window, int p_clients, bool p_migrates = false)
        : rig(0, p_migrates) {
        REQUIRE(gate_shape.is_valid());
        REQUIRE(spawner.is_valid());
        spawner_script = spawner.ptr();
        Ref<netw::NetwSessionConfig> config;
        config.instantiate();
        config->return_window(p_window);
        NETW_REQUIRE_EQ(int(rig.server()->session_initialize(config)), int(OK));
        rig.mount();
        arena = rig.declare_scene(StringName("Arena"));
        Node *level = rig.node_of(arena);
        rig.branch(-1)->add_child(level);
        level->set_owner(rig.branch(-1));
        rig.pump(4);
        rig.enter_scene(StringName("Arena"));
        Node *gate = Object::cast_to<Node>(gate_shape->call("new"));
        rig.branch(-1)->add_child(gate);
        REQUIRE(
            netw::Netw::configure_admission(Callable(gate, StringName("admit")))
                .is_valid()
        );
        teach(-1);
        rig.join(-1, StringName("host"));
        const char *names[] = {"ada", "bob", "cy"};
        for (int seat_at = 0; seat_at < p_clients; ++seat_at) {
            late(names[seat_at]);
        }
    }

    ~World() {
        spawner_script = nullptr;
    }

    NetwMultiplayer *at(int p_seat) const {
        return p_seat < 0 ? rig.server() : rig.client(p_seat);
    }

    void teach(int p_seat) {
        Array types;
        types.push_back(int(Variant::STRING));
        rig.register_constructor(
            at(p_seat),
            StringName(AVATAR_ID),
            callable_mp_static(&build_avatar),
            types
        );
        rig.register_constructor(
            at(p_seat),
            StringName(SPAWNER_ID),
            callable_mp_static(&build_spawner),
            types
        );
    }

    void seat(int p_seat, bool p_watch) {
        teach(p_seat);
        rig.mirror_scene(p_seat, StringName("Arena"), true);
        rig.pump(4);
        if (p_watch) {
            NETW_REQUIRE_EQ(
                int(rig.server()->scene_watch(arena, rig.peer_id(p_seat))),
                int(OK)
            );
        }
        rig.flush_interest();
        rig.pump(SETTLE);
    }

    int late(const char *p_name, bool p_watch = true) {
        const int made = rig.add_client(StringName(p_name));
        rig.mount_late(made, false);
        seat(made, p_watch);
        return made;
    }

    Node *level() const {
        return rig.node_of(arena);
    }

    int spawn(
        const StringName &p_id,
        const char *p_name,
        const Ref<NetwPlayer> &p_owner,
        bool p_pump = true
    ) {
        Array args;
        args.push_back(String(p_name));
        const RID made
            = rig.server()->spawn_registered(p_id, args, p_owner.ptr());
        REQUIRE(made.is_valid());
        Node *home = outside_scenes ? rig.branch(-1) : level();
        home->add_child(rig.server()->entity_get_node(made));
        if (p_pump) {
            rig.pump(SETTLE);
        }
        return int(rig.server()->entity_get_route(made));
    }

    int avatar(int p_client, const char *p_name) {
        return spawn(StringName(AVATAR_ID), p_name, rig.player(p_client));
    }

    Ref<NetwEntity> entity(int p_route, int p_seat) const {
        Node *found = rig.route_node(p_route, p_seat);
        return found != nullptr ? NetwEntity::of(found) : Ref<NetwEntity>();
    }

    bool live(int p_route, int p_seat) const {
        const Ref<NetwEntity> seen = entity(p_route, p_seat);
        return seen.is_valid()
            && seen->get_stage() == int64_t(netw::entity::Stage::LIVE);
    }

    void wait_msec(int64_t p_msec) {
        const int64_t until
            = int64_t(Time::get_singleton()->get_ticks_msec()) + p_msec;
        while (int64_t(Time::get_singleton()->get_ticks_msec()) < until) {
            rig.pump(1);
        }
    }
};

enum Exit {
    EXIT_DROP,
    EXIT_STEP_AWAY,
    EXIT_QUIT,
    EXIT_KICK,
};

struct ExitRow {
    const char *label;
    Exit exit;
    bool keeps;
};

void leave(World &p_world, Exit p_exit) {
    switch (p_exit) {
        case EXIT_DROP:
            p_world.rig.drop_client(0);
            break;
        case EXIT_STEP_AWAY:
            p_world.rig.client(0)->get_session()->step_away();
            break;
        case EXIT_QUIT:
            p_world.rig.client(0)->get_session()->leave();
            break;
        case EXIT_KICK:
            p_world.rig.server()->get_session()->kick(
                p_world.rig.player(0),
                String("Out.")
            );
            break;
    }
    p_world.rig.pump(SETTLE * 3);
}

struct Departure {
    int64_t server_lists = -1;
    int64_t client_lists = -1;
};

Departure heard;

void on_server_left(const Ref<NetwPlayer> &p_who, int64_t) {
    heard.server_lists = p_who->get_entities().size();
}

void on_client_left(const Ref<NetwPlayer> &p_who, int64_t) {
    heard.client_lists = p_who->get_entities().size();
}

TEST_CASE(
    "[Networked][Session][SceneTree] a player who drops or steps away "
    "leaves its entities spawned and owned by the server, and one who quits "
    "or is kicked loses them"
) {
    const ExitRow rows[] = {
        {"drop", EXIT_DROP, true},
        {"step away", EXIT_STEP_AWAY, true},
        {"quit", EXIT_QUIT, false},
        {"kick", EXIT_KICK, false},
    };
    for (const ExitRow &row : rows) {
        NETW_FORMAT_TEXT(row_text, row.label);
        CAPTURE(row_text);
        World world(60.0, 2);
        const int first = world.avatar(0, "Ada");
        const int second = world.avatar(0, "AdaPet");
        REQUIRE(world.live(first, 1));
        REQUIRE(world.live(second, 1));

        leave(world, row.exit);

        for (const int route : {first, second}) {
            NETW_CHECK_EQ(world.live(route, -1), row.keeps);
            if (!row.keeps) {
                continue;
            }
            for (const int seat_at : {-1, 1}) {
                CAPTURE(seat_at);
                REQUIRE(world.live(route, seat_at));
                const Ref<NetwEntity> kept = world.entity(route, seat_at);
                NETW_CHECK_EQ(kept->get_peer_id(), int64_t(0));
                NETW_CHECK_EQ(kept->get_player_id(), int64_t(0));
                NETW_CHECK_EQ(kept->get_controller(), SERVER);
                CHECK(kept->get_player().is_null());
            }
        }
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] an entity the dropped player only "
    "controls keeps its owner and returns to the server's control"
) {
    World world(60.0, 2);
    const int car = world.avatar(1, "BobCar");
    REQUIRE(
        world.entity(car, -1)->transfer_control(world.rig.peer_id(0)) == OK
    );
    world.rig.pump(SETTLE);
    NETW_REQUIRE_EQ(
        world.entity(car, 1)->get_controller(),
        int64_t(world.rig.peer_id(0))
    );

    world.rig.drop_client(0);
    world.rig.pump(SETTLE * 3);

    for (const int seat_at : {-1, 1}) {
        CAPTURE(seat_at);
        REQUIRE(world.live(car, seat_at));
        const Ref<NetwEntity> driven = world.entity(car, seat_at);
        NETW_CHECK_EQ(driven->get_peer_id(), int64_t(world.rig.peer_id(1)));
        NETW_CHECK_EQ(driven->get_controller(), SERVER);
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] in player_left the server lists every "
    "entity it keeps for the departed player, and a client lists none"
) {
    World world(60.0, 2);
    world.avatar(0, "Ada");
    world.avatar(0, "AdaPet");
    const int car = world.avatar(1, "BobCar");
    REQUIRE(
        world.entity(car, -1)->transfer_control(world.rig.peer_id(0)) == OK
    );
    world.rig.pump(SETTLE);
    heard = Departure();
    world.rig.server()->get_session()->connect(
        StringName("player_left"),
        callable_mp_static(&on_server_left)
    );
    world.rig.client(1)->get_session()->connect(
        StringName("player_left"),
        callable_mp_static(&on_client_left)
    );

    world.rig.drop_client(0);
    world.rig.pump(SETTLE * 3);

    NETW_CHECK_EQ(heard.server_lists, int64_t(3));
    NETW_CHECK_EQ(heard.client_lists, int64_t(0));
}

TEST_CASE(
    "[Networked][Session][SceneTree] when the return window runs out, "
    "counted in wall time through a session pause, a kept entity the player "
    "owned despawns and one it only drove stays with its owner"
) {
    for (const bool paused : {false, true}) {
        CAPTURE(paused);
        World world(0.2, 2);
        const int route = world.avatar(0, "Ada");
        const int car = world.avatar(1, "BobCar");
        REQUIRE(
            world.entity(car, -1)->transfer_control(world.rig.peer_id(0)) == OK
        );
        world.rig.pump(SETTLE);
        world.rig.drop_client(0);
        world.rig.pump(SETTLE);
        REQUIRE(world.live(route, 1));
        if (paused) {
            world.rig.server()->session_pause(String("break"));
            world.rig.pump(SETTLE);
        }

        world.wait_msec(400);

        CHECK_FALSE(world.live(route, -1));
        for (const int seat_at : {-1, 1}) {
            CAPTURE(seat_at);
            REQUIRE(world.live(car, seat_at));
            const Ref<NetwEntity> driven = world.entity(car, seat_at);
            NETW_CHECK_EQ(driven->get_peer_id(), int64_t(world.rig.peer_id(1)));
            NETW_CHECK_EQ(driven->get_controller(), SERVER);
        }
        if (paused) {
            CHECK(world.rig.server()->session_is_paused());
            world.rig.server()->session_unpause();
            world.rig.pump(SETTLE);
        }
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a stranger who connects on a dropped "
    "player's peer id claims none of the entities kept for it"
) {
    World world(60.0, 1);
    netw::gd::seed(4242);
    const int ada = world.late("ada");
    const int route = world.avatar(ada, "Ada");
    const int dropped = world.rig.peer_id(ada);
    world.rig.drop_client(ada);
    world.rig.pump(SETTLE * 3);

    netw::gd::seed(4242);
    const int stranger = world.late("cy");
    NETW_REQUIRE_EQ(world.rig.peer_id(stranger), dropped);
    REQUIRE(world.rig.player(stranger).is_valid());

    REQUIRE(world.live(route, -1));
    CHECK(world.entity(route, -1)->get_player().is_null());
    CHECK(world.rig.player(stranger)->get_entities().is_empty());
    REQUIRE(world.live(route, stranger));
    CHECK_FALSE(world.entity(route, stranger)->get_is_controlled_locally());
}

TEST_CASE(
    "[Networked][Session][SceneTree] a car set to despawn when its driver "
    "disconnects is kept while the driver is dropped, and despawns when the "
    "return window runs out or the driver quits"
) {
    const ExitRow rows[] = {
        {"drop", EXIT_DROP, true},
        {"quit", EXIT_QUIT, false},
    };
    for (const ExitRow &row : rows) {
        NETW_FORMAT_TEXT(row_text, row.label);
        CAPTURE(row_text);
        World world(0.2, 2);
        const int car = world.avatar(1, "BobCar");
        world.entity(car, -1)->set_on_controller_disconnect(
            NetwEntity::DISCONNECT_DESPAWN
        );
        REQUIRE(
            world.entity(car, -1)->transfer_control(world.rig.peer_id(0)) == OK
        );
        world.rig.pump(SETTLE);

        leave(world, row.exit);

        NETW_CHECK_EQ(world.live(car, -1), row.keeps);
        if (row.keeps) {
            CHECK(world.live(car, 1));
            NETW_CHECK_EQ(world.entity(car, 1)->get_controller(), SERVER);
        }
        world.wait_msec(400);
        CHECK_FALSE(world.live(car, -1));
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a spawn armed in the frame its player "
    "drops is discarded and not kept"
) {
    World world(60.0, 2);
    const int route
        = world.spawn(StringName(AVATAR_ID), "Ada", world.rig.player(0), false);
    world.rig.drop_client(0);
    world.rig.pump(SETTLE * 3);

    CHECK_FALSE(world.live(route, -1));
}

TEST_CASE(
    "[Networked][Session][SceneTree] a spawn_async whose player drops "
    "before it returns spawns nothing and keeps nothing"
) {
    World world(60.0, 2);
    const int made
        = world.spawn(StringName(SPAWNER_ID), "Spawner", Ref<NetwPlayer>());
    Node *spawning = world.rig.route_node(made, -1);
    REQUIRE(spawning != nullptr);
    spawning->set(StringName("who"), world.rig.player(0));
    const Ref<NetwPromise> promise = spawning->call(StringName("begin"));
    REQUIRE(promise.is_valid());
    world.rig.pump(SETTLE);
    REQUIRE_FALSE(promise->get_is_settled());

    world.rig.drop_client(0);
    world.rig.pump(SETTLE);
    spawning->call(StringName("finish"));
    world.rig.pump(SETTLE);

    CHECK(promise->get_is_failed());
    const ObjectID built
        = ObjectID(uint64_t(int64_t(spawning->get(StringName("built")))));
    CHECK(netw::gd::object_of(built) == nullptr);
    const int back = world.late("ada");
    CHECK(world.rig.player(back)->get_entities().is_empty());
}

struct Arrival {
    World *world = nullptr;
    int64_t in_join = -1;
    int64_t in_joined = -1;
};

Arrival arrival;

void spawn_when_empty(const Ref<NetwPlayer> &p_who) {
    if (p_who->get_entities().is_empty()) {
        arrival.world->spawn(StringName(AVATAR_ID), "Fresh", p_who, false);
    }
}

void on_join(const Ref<NetwPlayer> &p_who) {
    arrival.in_join = p_who->get_entities().size();
    spawn_when_empty(p_who);
}

void on_joined(const Ref<NetwPlayer> &p_who) {
    arrival.in_joined = p_who->get_entities().size();
    spawn_when_empty(p_who);
}

enum Return {
    RETURN_AFTER_DROP,
    RETURN_AFTER_STEP_AWAY,
    RETURN_BY_SECOND_LOGIN,
};

struct ReturnRow {
    const char *label;
    Return path;
    bool outside_scenes;
};

TEST_CASE(
    "[Networked][Session][SceneTree] a returning player already lists its "
    "kept entities in the join handler and in player_joined, inside a scene "
    "or not, so a game that spawns only when the list is empty spawns nothing"
) {
    const ReturnRow rows[] = {
        {"drop", RETURN_AFTER_DROP, false},
        {"step away", RETURN_AFTER_STEP_AWAY, false},
        {"second login", RETURN_BY_SECOND_LOGIN, false},
        {"drop outside any scene", RETURN_AFTER_DROP, true},
        {"second login outside any scene", RETURN_BY_SECOND_LOGIN, true},
    };
    for (const ReturnRow &row : rows) {
        NETW_FORMAT_TEXT(row_text, row.label);
        CAPTURE(row_text);
        World world(60.0, 2);
        world.outside_scenes = row.outside_scenes;
        world.avatar(0, "Ada");
        world.avatar(0, "AdaPet");
        if (row.path == RETURN_AFTER_DROP) {
            world.rig.drop_client(0);
        } else if (row.path == RETURN_AFTER_STEP_AWAY) {
            world.rig.client(0)->get_session()->step_away();
        }
        world.rig.pump(SETTLE * 3);
        arrival = Arrival();
        arrival.world = &world;
        world.rig.server()->session_set_join_override(
            callable_mp_static(&on_join),
            Array()
        );
        world.rig.server()->get_session()->connect(
            StringName("player_joined"),
            callable_mp_static(&on_joined)
        );

        const int back = world.late("ada");
        world.rig.pump(SETTLE);

        NETW_CHECK_EQ(arrival.in_join, int64_t(2));
        NETW_CHECK_EQ(arrival.in_joined, int64_t(2));
        NETW_CHECK_EQ(
            world.rig.player(back)->get_entities().size(),
            int64_t(2)
        );
        arrival = Arrival();
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a returning player gets its avatar back "
    "as the same node on a peer that stayed, owns and controls it, and late "
    "joiners and its scene follow"
) {
    World world(60.0, 2);
    const int route = world.avatar(0, "Ada");
    const ObjectID stayed
        = netw::gd::instance_id(world.rig.route_node(route, 1));
    world.rig.drop_client(0);
    world.rig.pump(SETTLE * 3);

    const int back = world.late("ada", false);
    const int64_t peer = world.rig.peer_id(back);
    const Ref<NetwPlayer> returned = world.rig.player(back);
    REQUIRE(returned.is_valid());

    REQUIRE(world.live(route, 1));
    CHECK(netw::gd::instance_id(world.rig.route_node(route, 1)) == stayed);
    const int cy = world.late("cy");
    for (const int seat_at : {-1, 1, back, cy}) {
        CAPTURE(seat_at);
        REQUIRE(world.live(route, seat_at));
        const Ref<NetwEntity> held = world.entity(route, seat_at);
        NETW_CHECK_EQ(held->get_peer_id(), peer);
        NETW_CHECK_EQ(held->get_controller(), peer);
    }
    NETW_CHECK_EQ(
        world.entity(route, -1)->get_player_id(),
        returned->player_id()
    );
    CHECK(world.entity(route, back)->get_is_controlled_locally());
    REQUIRE(returned->get_scene().is_valid());
    CHECK(world.rig.server()
              ->scene_get_player_entities(world.arena)
              .has(world.entity(route, -1)));
}

TEST_CASE(
    "[Networked][Session][SceneTree] a car the returning player only drove "
    "comes back driven by it and still owned by its owner"
) {
    World world(60.0, 2);
    const int car = world.avatar(1, "BobCar");
    REQUIRE(
        world.entity(car, -1)->transfer_control(world.rig.peer_id(0)) == OK
    );
    world.rig.pump(SETTLE);
    world.rig.drop_client(0);
    world.rig.pump(SETTLE * 3);

    const int back = world.late("ada");

    for (const int seat_at : {-1, 1, back}) {
        CAPTURE(seat_at);
        REQUIRE(world.live(car, seat_at));
        const Ref<NetwEntity> driven = world.entity(car, seat_at);
        NETW_CHECK_EQ(driven->get_peer_id(), int64_t(world.rig.peer_id(1)));
        NETW_CHECK_EQ(
            driven->get_controller(),
            int64_t(world.rig.peer_id(back))
        );
    }
    CHECK(world.entity(car, back)->get_is_controlled_locally());
    CHECK(world.rig.player(back)->get_entities().is_empty());
}

TEST_CASE(
    "[Networked][Session][SceneTree] an avatar the server was driving when "
    "its player dropped comes back owned by the player and still driven by "
    "the server"
) {
    World world(60.0, 2);
    const int route = world.avatar(0, "Ada");
    REQUIRE(world.entity(route, -1)->transfer_control(SERVER) == OK);
    world.rig.pump(SETTLE);
    NETW_REQUIRE_EQ(world.entity(route, 1)->get_controller(), SERVER);
    world.rig.drop_client(0);
    world.rig.pump(SETTLE * 3);

    const int back = world.late("ada");

    for (const int seat_at : {-1, 1, back}) {
        CAPTURE(seat_at);
        REQUIRE(world.live(route, seat_at));
        const Ref<NetwEntity> held = world.entity(route, seat_at);
        NETW_CHECK_EQ(held->get_peer_id(), int64_t(world.rig.peer_id(back)));
        NETW_CHECK_EQ(held->get_controller(), SERVER);
    }
    CHECK_FALSE(world.entity(route, back)->get_is_controlled_locally());
}

enum TransferSeat {
    SEAT_SERVER,
    SEAT_DROPPED,
    SEAT_SUCCESSOR,
    SEAT_CLIENT,
    SEAT_LATE,
    SEAT_COUNT,
};

constexpr int WATCHERS[] = {
    SEAT_SERVER,
    SEAT_SUCCESSOR,
    SEAT_CLIENT,
    SEAT_LATE,
};

struct TransferEvidence {
    bool driven = false;
    bool moved = false;
    bool kept_before = false;
    bool avatar_after[SEAT_COUNT] = {};
    bool crate_after[SEAT_COUNT] = {};
};

TransferEvidence &transfer_evidence(int p_successor) {
    static TransferEvidence seen[SEAT_COUNT];
    return seen[p_successor];
}

constexpr int TRANSFER_FRAMES = 30;

class TransferStand {
    Ref<netw::LocalLoopbackSession> link;
    Ref<NetwMultiplayer> apis[SEAT_COUNT];
    Node *branches[SEAT_COUNT] = {};
    Ref<Script> gate_shape;
    netw::NetwSession::TransferMode mode;

public:
    Node *arenas[SEAT_COUNT] = {};

    explicit TransferStand(
        netw::NetwSession::TransferMode p_mode
        = netw::NetwSession::TRANSFER_REJOIN
    )
        : mode(p_mode) {
        link.instantiate();
        link->enable_server_transfer();
        Ref<netw::NetwSessionConfig> config;
        config.instantiate();
        config->return_window(60.0);
        for (int seat_at = 0; seat_at < SEAT_LATE; ++seat_at) {
            create(seat_at);
        }
        connect(SEAT_SERVER);
        apis[SEAT_SERVER]->session_initialize(config);
        pump(1);
        for (int seat_at = 0; seat_at < SEAT_LATE; ++seat_at) {
            mount(seat_at);
        }
        gate_shape = netw_test::minted_script(ACCOUNT_GATE);
        Node *gate = Object::cast_to<Node>(gate_shape->call("new"));
        branches[SEAT_SERVER]->add_child(gate);
        netw::Netw::configure_admission(Callable(gate, StringName("admit")));
        for (int seat_at = 0; seat_at < SEAT_LATE; ++seat_at) {
            if (seat_at != SEAT_SERVER) {
                connect(seat_at);
            }
            join(seat_at);
        }
        pump(SETTLE);
    }

    void create(int p_seat) {
        const char *names[SEAT_COUNT] = {"", "ada", "bob", "cy", "dee"};
        apis[p_seat] = NetwMultiplayer::create(Ref<Script>());
        apis[p_seat]->session_get_inner()->set_root_path(NodePath("/"));
        apis[p_seat]->transfer_set_mode(mode);
        if (p_seat != SEAT_SERVER) {
            apis[p_seat]->session_prepare_join(StringName(names[p_seat]), {});
        }
    }

    void connect(int p_seat) {
        apis[p_seat]->set(
            "multiplayer_peer",
            p_seat == SEAT_SERVER ? link->get_server_peer()
                                  : link->create_client_peer()
        );
    }

    void mount(int p_seat) {
        Array types;
        types.push_back(int(Variant::STRING));
        Array quantizers;
        quantizers.resize(1);
        branches[p_seat] = memnew(Node);
        branches[p_seat]->set_name(vformat("KeptTransfer%d", p_seat));
        netw::gd::scene_root()->add_child(branches[p_seat]);
        apis[p_seat]->session_set_root(
            Callable(branches[p_seat], "get_node").bind(NodePath("."))
        );
        netw::gd::scene_tree()->set_multiplayer(
            Ref<MultiplayerAPI>(
                Object::cast_to<MultiplayerAPI>(apis[p_seat].ptr())
            ),
            branches[p_seat]->get_path()
        );
        Node *level = memnew(Node);
        level->set_name("Arena");
        branches[p_seat]->add_child(level);
        arenas[p_seat] = level;
        apis[p_seat]->spawn_register_constructor(
            StringName(AVATAR_ID),
            callable_mp_static(&build_avatar),
            types,
            quantizers
        );
        pump(1);
    }

    void join(int p_seat) {
        if (p_seat == SEAT_SERVER) {
            apis[p_seat]->session_submit_join(StringName("host"), Array());
        }
        pump(4);
    }

    void arrive(int p_seat) {
        create(p_seat);
        mount(p_seat);
        connect(p_seat);
        pump(1);
        join(p_seat);
    }

    ~TransferStand() {
        for (int seat_at = 0; seat_at < SEAT_COUNT; ++seat_at) {
            if (branches[seat_at] == nullptr) {
                continue;
            }
            netw::gd::scene_tree()->set_multiplayer(
                Ref<MultiplayerAPI>(),
                branches[seat_at]->get_path()
            );
            netw::gd::scene_root()->remove_child(branches[seat_at]);
            memdelete(branches[seat_at]);
        }
        link->reset();
    }

    void pump(int p_times) {
        for (int round = 0; round < p_times; ++round) {
            link->poll();
            for (const Ref<NetwMultiplayer> &api : apis) {
                if (api.is_valid()) {
                    api->poll();
                }
            }
        }
    }

    NetwMultiplayer *at(int p_seat) const {
        return apis[p_seat].ptr();
    }

    int64_t peer_id(int p_seat) const {
        return at(p_seat)->get_unique_id();
    }

    void drop(int p_seat) {
        link->get_server_peer()->NETW_PEER_VIRTUAL(disconnect_peer)(
            int(peer_id(p_seat)),
            false
        );
    }

    Node *spawn(const char *p_name, NetwPlayer *p_owner) {
        Array args;
        args.push_back(String(p_name));
        const RID made
            = at(SEAT_SERVER)
                  ->spawn_registered(StringName(AVATAR_ID), args, p_owner);
        Node *node = at(SEAT_SERVER)->entity_get_node(made);
        if (node != nullptr) {
            arenas[SEAT_SERVER]->add_child(node);
        }
        return node;
    }

    bool shows(int p_seat, const char *p_name) const {
        Node *found = arenas[p_seat]->get_node_or_null(NodePath(p_name));
        return found != nullptr && found->is_inside_tree()
            && !found->is_queued_for_deletion();
    }
};

class KeptTransferScenario final : public netw_test::FrameScenario {
    int successor = SEAT_SUCCESSOR;
    int frame = 0;
    TransferStand *stand = nullptr;

    void open() {
        TransferEvidence &seen = transfer_evidence(successor);
        stand = new TransferStand();
        NetwMultiplayer *server = stand->at(SEAT_SERVER);
        const Ref<NetwPlayer> ada
            = server->peer_get_player(stand->peer_id(SEAT_DROPPED));
        if (ada.is_null()) {
            return;
        }
        Node *avatar = stand->spawn("ada", ada.ptr());
        Node *crate = stand->spawn("Crate", nullptr);
        if (avatar == nullptr || crate == nullptr) {
            return;
        }
        stand->pump(SETTLE);
        stand->drop(SEAT_DROPPED);
        stand->pump(SETTLE * 3);
        stand->arrive(SEAT_LATE);
        stand->pump(SETTLE * 3);
        seen.kept_before = NetwEntity::of(avatar)->get_peer_id() == 0
            && NetwEntity::of(crate)->get_peer_id() == 0;
        for (const int seat_at : WATCHERS) {
            seen.kept_before = seen.kept_before && stand->shows(seat_at, "ada")
                && stand->shows(seat_at, "Crate");
        }
        seen.moved
            = server->get_session()->transfer_server(stand->peer_id(successor))
            == OK;
    }

    void close() {
        TransferEvidence &seen = transfer_evidence(successor);
        seen.driven = true;
        for (const int seat_at : WATCHERS) {
            seen.avatar_after[seat_at] = stand->shows(seat_at, "ada");
            seen.crate_after[seat_at] = stand->shows(seat_at, "Crate");
        }
        delete stand;
        stand = nullptr;
    }

public:
    explicit KeptTransferScenario(int p_successor) : successor(p_successor) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame > TRANSFER_FRAMES) {
            return false;
        }
        if (frame == 0) {
            open();
        } else if (frame < TRANSFER_FRAMES) {
            stand->pump(1);
        } else {
            close();
        }
        ++frame;
        return frame <= TRANSFER_FRAMES;
    }
};

NETW_FRAME_SCENARIO(
    KeptTransferScenario,
    kept_transfer_to_client,
    SEAT_SUCCESSOR
);
NETW_FRAME_SCENARIO(KeptTransferScenario, kept_transfer_to_late, SEAT_LATE);

TEST_CASE(
    "[Networked][Session][Transfer][Frame] after a server transfer, to a "
    "client that was there when the player dropped or to one that joined "
    "after, no peer shows an entity kept for the departed player, and a "
    "server entity with no player stays on every peer"
) {
    for (const int successor : {int(SEAT_SUCCESSOR), int(SEAT_LATE)}) {
        CAPTURE(successor);
        const TransferEvidence &seen = transfer_evidence(successor);
        REQUIRE(seen.driven);
        REQUIRE(seen.kept_before);
        REQUIRE(seen.moved);
        for (const int seat_at : WATCHERS) {
            CAPTURE(seat_at);
            CHECK_FALSE(seen.avatar_after[seat_at]);
            CHECK(seen.crate_after[seat_at]);
        }
    }
}

enum MoveAct {
    MOVE_DESPAWN,
    MOVE_DESPAWN_AFTER,
    MOVE_DESPAWN_SETTLED,
    MOVE_SPAWN,
};

struct MoveArm {
    const char *label;
    MoveAct act;
    netw::NetwSession::TransferMode mode;
};

constexpr MoveArm MOVE_ARMS[] = {
    {"despawn, rejoin", MOVE_DESPAWN, netw::NetwSession::TRANSFER_REJOIN},
    {"despawn, continue", MOVE_DESPAWN, netw::NetwSession::TRANSFER_CONTINUE},
    {"despawn after the call, settled before the server moves, rejoin",
     MOVE_DESPAWN_AFTER,
     netw::NetwSession::TRANSFER_REJOIN},
    {"despawn after the call, settled before the server moves, continue",
     MOVE_DESPAWN_AFTER,
     netw::NetwSession::TRANSFER_CONTINUE},
    {"despawn committed before the call in the same frame, rejoin",
     MOVE_DESPAWN_SETTLED,
     netw::NetwSession::TRANSFER_REJOIN},
    {"despawn committed before the call in the same frame, continue",
     MOVE_DESPAWN_SETTLED,
     netw::NetwSession::TRANSFER_CONTINUE},
    {"spawn, rejoin", MOVE_SPAWN, netw::NetwSession::TRANSFER_REJOIN},
    {"spawn, continue", MOVE_SPAWN, netw::NetwSession::TRANSFER_CONTINUE},
};

constexpr int MOVE_ARM_COUNT = sizeof(MOVE_ARMS) / sizeof(MOVE_ARMS[0]);

constexpr int MOVE_SEATS[] = {
    SEAT_SERVER,
    SEAT_DROPPED,
    SEAT_SUCCESSOR,
    SEAT_CLIENT,
};

struct MoveEvidence {
    bool driven = false;
    bool moved = false;
    bool crate_before[SEAT_COUNT] = {};
    bool crate_after[SEAT_COUNT] = {};
    bool keep_after[SEAT_COUNT] = {};
};

MoveEvidence &move_evidence(int p_arm) {
    static MoveEvidence seen[MOVE_ARM_COUNT];
    return seen[p_arm];
}

class MoveScenario final : public netw_test::FrameScenario {
    static constexpr int FRAMES = 40;
    int arm = 0;
    int frame = 0;
    TransferStand *stand = nullptr;

    bool move() {
        return stand->at(SEAT_SERVER)
                   ->get_session()
                   ->transfer_server(stand->peer_id(SEAT_SUCCESSOR))
            == OK;
    }

    void open() {
        MoveEvidence &seen = move_evidence(arm);
        const MoveAct act = MOVE_ARMS[arm].act;
        stand = new TransferStand(MOVE_ARMS[arm].mode);
        Node *crate = nullptr;
        if (stand->spawn("Keep", nullptr) == nullptr) {
            return;
        }
        if (act != MOVE_SPAWN) {
            crate = stand->spawn("Crate", nullptr);
            if (crate == nullptr) {
                return;
            }
        }
        stand->pump(SETTLE);
        for (const int seat_at : MOVE_SEATS) {
            seen.crate_before[seat_at] = stand->shows(seat_at, "Crate");
        }
        switch (act) {
            case MOVE_DESPAWN:
                NetwEntity::of(crate)->despawn(Ref<netw::NetwDespawnOpts>());
                seen.moved = move();
                break;
            case MOVE_DESPAWN_AFTER:
                seen.moved = move();
                NetwEntity::of(crate)->despawn(Ref<netw::NetwDespawnOpts>());
                crate->get_parent()->remove_child(crate);
                stand->at(SEAT_SERVER)->session_flush_deferred();
                break;
            case MOVE_DESPAWN_SETTLED:
                NetwEntity::of(crate)->despawn(Ref<netw::NetwDespawnOpts>());
                crate->get_parent()->remove_child(crate);
                stand->at(SEAT_SERVER)->session_flush_deferred();
                seen.moved = move();
                break;
            case MOVE_SPAWN:
                seen.moved
                    = stand->spawn("Crate", nullptr) != nullptr && move();
                break;
        }
    }

    void close() {
        MoveEvidence &seen = move_evidence(arm);
        seen.driven = true;
        for (const int seat_at : MOVE_SEATS) {
            seen.crate_after[seat_at] = stand->shows(seat_at, "Crate");
            seen.keep_after[seat_at] = stand->shows(seat_at, "Keep");
        }
        delete stand;
        stand = nullptr;
    }

public:
    explicit MoveScenario(int p_arm) : arm(p_arm) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame > FRAMES) {
            return false;
        }
        if (frame == 0) {
            open();
        } else if (frame < FRAMES) {
            stand->pump(1);
        } else {
            close();
        }
        ++frame;
        return frame <= FRAMES;
    }
};

NETW_FRAME_SCENARIO(MoveScenario, move_despawn_rejoin, 0);
NETW_FRAME_SCENARIO(MoveScenario, move_despawn_continue, 1);
NETW_FRAME_SCENARIO(MoveScenario, move_despawn_after_rejoin, 2);
NETW_FRAME_SCENARIO(MoveScenario, move_despawn_after_continue, 3);
NETW_FRAME_SCENARIO(MoveScenario, move_despawn_settled_rejoin, 4);
NETW_FRAME_SCENARIO(MoveScenario, move_despawn_settled_continue, 5);
NETW_FRAME_SCENARIO(MoveScenario, move_spawn_rejoin, 6);
NETW_FRAME_SCENARIO(MoveScenario, move_spawn_continue, 7);

TEST_CASE(
    "[Networked][Session][Transfer][Frame] a server despawn or spawn in the "
    "frame of transfer_server leaves the entity on no peer, under "
    "TRANSFER_REJOIN and TRANSFER_CONTINUE, and a server entity left alone "
    "stays on every peer"
) {
    for (int arm_at = 0; arm_at < MOVE_ARM_COUNT; ++arm_at) {
        NETW_FORMAT_TEXT(arm_text, MOVE_ARMS[arm_at].label);
        CAPTURE(arm_text);
        const MoveEvidence &seen = move_evidence(arm_at);
        REQUIRE(seen.driven);
        REQUIRE(seen.moved);
        for (const int seat_at : MOVE_SEATS) {
            CAPTURE(seat_at);
            NETW_CHECK_EQ(
                seen.crate_before[seat_at],
                MOVE_ARMS[arm_at].act != MOVE_SPAWN
            );
            CHECK_FALSE(seen.crate_after[seat_at]);
            CHECK(seen.keep_after[seat_at]);
        }
    }
}

} // namespace TestPlayerKeepLaws

#endif
