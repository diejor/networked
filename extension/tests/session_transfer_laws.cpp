#include "support/loopback_rig.h"
#include "support/netw_recorder.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/class_db.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/packed_scene.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/session.hpp"
#include "netw/entity/stage.hpp"
#include "netw/liveness_core.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/scene_membership.hpp"
#include "netw/schema_model.hpp"
#include "netw/session_core.hpp"
#include "netw/sim/row.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/wire/registry.hpp"
#include "support/fault_log.h"
#include "support/minted_script.h"
#include "support/stock_stand.h"

#include <godot_cpp/classes/node2d.hpp>

namespace TestSessionTransferLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw::NetwPromise;
using netw::NetwSession;
using netw::sim::Mode;
using netw_test::LoopbackRig;
using netw_test::Recorder;

constexpr int CLIENTS = 3;
constexpr int SETTLE_PUMPS = 6;
constexpr int ENET_PORT = 30600;
constexpr int ENET_PORTS = 100;

const StringName &transferred() {
    static const StringName name("server_transferred");
    return name;
}

const StringName &player_left() {
    static const StringName name("player_left");
    return name;
}

Node *present(Node *p_node) {
    const bool held = p_node != nullptr;
    REQUIRE(held);
    return p_node;
}

bool same(const String &p_a, const String &p_b) {
    return p_a == p_b;
}

Mode sim_mode(NetwMultiplayer *p_seat, const RID &p_entity) {
    const netw::sim::Row *row = p_seat->sim_row_of(p_entity);
    return row != nullptr ? row->mode : Mode::NONE;
}

const char *BODY_ID = "transfer_body";

Node *build_body(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

void cycle_online(NetwMultiplayer *p_api) {
    netw::SessionCore &core = p_api->session_plane();
    core.transition(netw::SessionCore::STATE_DISCONNECTING);
    core.transition(netw::SessionCore::STATE_OFFLINE);
    core.transition(netw::SessionCore::STATE_CONNECTING);
    core.resolve_online(p_api->get_unique_id());
}

struct Table {
    LoopbackRig rig;
    Vector<int64_t> before;
    Vector<netw::LocalMultiplayerPeer *> links;
    Node *arena = nullptr;
    int waiting = -2;

    explicit Table(bool p_one_waits = false, bool p_server_cycled = false)
        : rig(p_server_cycled ? 0 : CLIENTS, true) {
        if (p_server_cycled) {
            cycle_online(rig.server());
            for (int at = 0; at < CLIENTS; ++at) {
                rig.add_client();
            }
            rig.pump(SETTLE_PUMPS);
        }
        rig.mount();
        arena = rig.mirror_child("Arena");
        for (int at = -1; at < CLIENTS; ++at) {
            before.push_back(seat(at)->get_unique_id());
            links.push_back(rig.peer(at));
        }
        if (p_one_waits) {
            waiting = (lowest_client() + 1) % CLIENTS;
        }
        rig.join(-1, StringName("host"));
        for (int at = 0; at < CLIENTS; ++at) {
            if (at != waiting) {
                rig.join(at, StringName(vformat("guest%d", at)));
            }
        }
        rig.pump(SETTLE_PUMPS);
    }

    bool rosters_match_links(int p_at) const {
        PackedInt32Array held
            = netw::gd::api_peer_ids(seat(p_at)->session_get_inner());
        PackedInt32Array linked = links[p_at + 1]->linked_peer_ids();
        held.sort();
        linked.sort();
        return held == linked;
    }

    NetwMultiplayer *seat(int p_at) const {
        return p_at < 0 ? rig.server() : rig.client(p_at);
    }

    int64_t was(int p_at) const {
        return before[p_at + 1];
    }

    int lowest_client() const {
        int lowest = 0;
        for (int at = 1; at < CLIENTS; ++at) {
            if (was(at) < was(lowest)) {
                lowest = at;
            }
        }
        return lowest;
    }

    int64_t now(int p_at, int64_t p_successor_was, int64_t p_former_now) const {
        const int64_t label = was(p_at);
        if (label == p_successor_was) {
            return 1;
        }
        return label == 1 ? p_former_now : label;
    }

    Ref<NetwPlayer> player_at(int p_at) const {
        return p_at < 0 ? rig.server()->player_local()
                        : rig.server()->peer_get_player(was(p_at));
    }

    int spawn_body(const char *p_name, int p_owner, int64_t p_controller) {
        Array types;
        types.push_back(int(Variant::STRING));
        for (int at = -1; at < CLIENTS; ++at) {
            rig.register_constructor(
                seat(at),
                StringName(BODY_ID),
                callable_mp_static(&build_body),
                types
            );
        }
        Array args;
        args.push_back(String(p_name));
        const Ref<NetwPlayer> owner
            = p_owner < -1 ? Ref<NetwPlayer>() : player_at(p_owner);
        const RID entity = rig.server()->spawn_registered(
            StringName(BODY_ID),
            args,
            owner.ptr()
        );
        REQUIRE(entity.is_valid());
        Node *node = present(rig.server()->entity_get_node(entity));
        arena->add_child(node);
        if (p_controller != 0) {
            NetwEntity::of(node)->set_controller(p_controller);
        }
        rig.pump(SETTLE_PUMPS);
        return int(rig.server()->entity_get_route(entity));
    }

    Node *body_at(int p_route, int p_at) const {
        NetwMultiplayer *api = seat(p_at);
        return api->entity_get_node(api->entity_from_route(p_route));
    }

    void keep_world() {
        for (int at = -1; at < CLIENTS; ++at) {
            seat(at)->transfer_set_mode(NetwSession::TRANSFER_CONTINUE);
        }
    }

    void forget_departures() {
        for (int at = -1; at < CLIENTS; ++at) {
            NetwMultiplayer *api = seat(at);
            api->connect(
                StringName("peer_disconnected"),
                callable_mp(api, &NetwMultiplayer::peer_forget)
            );
        }
    }
};

struct Ear {
    Vector<Recorder *> recorders;

    Ear(const Table &p_table, int p_from, Vector<StringName> p_signals) {
        for (int at = p_from; at < CLIENTS; ++at) {
            recorders.push_back(memnew(
                Recorder(p_table.seat(at)->get_session().ptr(), p_signals)
            ));
        }
    }

    ~Ear() {
        for (Recorder *recorder : recorders) {
            memdelete(recorder);
        }
    }

    Ear(const Ear &) = delete;
    Ear &operator=(const Ear &) = delete;
};

const char *PROBE_ID = "transfer_probe";

const char *PROBE_SOURCE = R"(extends Node

var asked: Array = []
var senders: Array = []
var held: Dictionary = {}


func _init() -> void:
	Netw.configure_entity(self)


@rpc("any_peer", "call_remote", "reliable")
func ask(tag: int) -> Variant:
	asked.append(tag)
	senders.append(multiplayer.get_remote_sender_id())
	if tag < 100:
		return tag * 10
	var promise := NetwPromise.new()
	held[tag] = promise
	return promise
)";

Node *build_probe(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node *made
        = Object::cast_to<Node>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    return made;
}

int spawn_probe(Table &p_table) {
    const Ref<Script> script = netw_test::minted_script(PROBE_SOURCE);
    REQUIRE(script.is_valid());
    Array types;
    types.push_back(int(Variant::STRING));
    for (int at = -1; at < CLIENTS; ++at) {
        p_table.rig.register_constructor(
            p_table.seat(at),
            StringName(PROBE_ID),
            callable_mp_static(&build_probe).bind(script),
            types
        );
    }
    Array args;
    args.push_back(String("Probe"));
    const RID entity = p_table.rig.server()->spawn_registered(
        StringName(PROBE_ID),
        args,
        nullptr
    );
    REQUIRE(entity.is_valid());
    Node *node = present(p_table.rig.server()->entity_get_node(entity));
    p_table.arena->add_child(node);
    p_table.rig.pump(SETTLE_PUMPS);
    return int(p_table.rig.server()->entity_get_route(entity));
}

Ref<NetwPromise> ask(
    Table &p_table,
    int p_from,
    int p_route,
    int64_t p_to,
    int p_tag
) {
    Node *probe = present(p_table.body_at(p_route, p_from));
    Array args;
    args.push_back(p_tag);
    const Ref<NetwPromise> asked = p_table.seat(p_from)->rpc_request_call(
        p_to,
        Callable(probe, StringName("ask")),
        args
    );
    REQUIRE(asked.is_valid());
    p_table.rig.pump(SETTLE_PUMPS);
    return asked;
}

const char *STOCK_SOURCE = R"(extends Node

var heard: Array = []


@rpc("any_peer", "call_remote", "reliable")
func poke(tag: int) -> void:
	heard.append([multiplayer.get_remote_sender_id(), tag])
)";

void seat_stock(Table &p_table) {
    const Ref<Script> script = netw_test::minted_script(STOCK_SOURCE);
    REQUIRE(script.is_valid());
    p_table.rig.mirror_child("Stock");
    for (int at = -1; at < CLIENTS; ++at) {
        Node *stock = present(
            p_table.rig.branch(at)->get_node_or_null(NodePath("Stock"))
        );
        stock->set_script(script);
    }
}

int poke_everyone(Table &p_table, int p_tag) {
    for (int at = -1; at < CLIENTS; ++at) {
        p_table.rig.branch(at)
            ->get_node_or_null(NodePath("Stock"))
            ->rpc(StringName("poke"), p_tag);
    }
    p_table.rig.pump(SETTLE_PUMPS);
    int heard = 0;
    for (int at = -1; at < CLIENTS; ++at) {
        const Array seen = p_table.rig.branch(at)
                               ->get_node_or_null(NodePath("Stock"))
                               ->get("heard");
        for (int each = 0; each < seen.size(); ++each) {
            const Array row = seen[each];
            heard += int(row[1]) == p_tag ? 1 : 0;
        }
    }
    return heard;
}

Ref<NetwPromise> held(Table &p_table, int p_at, int p_route, int p_tag) {
    Node *probe = present(p_table.body_at(p_route, p_at));
    const Dictionary waiting = probe->get("held");
    return waiting.get(p_tag, Variant());
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX1 after the server is lost "
    "every survivor reads its new label, the successor serves with its own "
    "player, the session stays online, and each survivor hears "
    "server_transferred once naming the successor and no former server"
) {
    Table table;
    const int successor = table.lowest_client();
    const int64_t successor_was = table.was(successor);
    Ear ear(table, 0, {transferred()});
    const Vector<Recorder *> &heard = ear.recorders;

    netw_test::FaultWatch faults;
    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);
    NETW_CHECK_EQ(faults.errors(), 0);

    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NetwMultiplayer *seat = table.seat(at);
        NETW_CHECK_EQ(int(table.rosters_match_links(at)), 1);
        const int64_t label = table.now(at, successor_was, 0);
        NETW_CHECK_EQ(int64_t(seat->get_unique_id()), label);
        NETW_CHECK_EQ(int(seat->is_server()), int(label == 1));
        NETW_CHECK_EQ(
            int(seat->session_get_state()),
            int(NetwMultiplayer::SESSION_STATE_ONLINE)
        );
        NETW_CHECK_EQ(heard[at]->count(transferred()), 1);
        const Array args = heard[at]->args(transferred());
        REQUIRE(args.size() == 2);
        NETW_CHECK_EQ(int64_t(args[0]), successor_was);
        NETW_CHECK_EQ(int64_t(args[1]), int64_t(0));
    }
    NETW_CHECK_EQ(
        int(table.seat(successor)->session_get_role()),
        int(NetwMultiplayer::ROLE_LISTEN_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX2 a server handed on through "
    "NetwSession.transfer_server becomes a client under its new label and "
    "keeps its player there, while every other seat reads the successor as "
    "peer 1, holds the links its transport holds, and reaches peer 1 with no "
    "engine error"
) {
    Table table;
    const int successor = 1;
    const int64_t successor_was = table.was(successor);
    const int route = spawn_probe(table);
    seat_stock(table);
    NETW_REQUIRE_EQ(poke_everyone(table, 1), (CLIENTS + 1) * CLIENTS);
    NETW_REQUIRE_EQ(int(table.rig.server()->lagcomp_initialize(8, 12)), 0);
    const RID ball = table.rig.declare_entity(
        netw_test::EntityDecl()
            .named(StringName("Ball"))
            .on_schema(StringName("TransferPose"))
            .synced(StringName("position"))
            .placed_at(Vector2())
            .predicted(0.01)
    );
    table.rig.pump(SETTLE_PUMPS);
    NETW_REQUIRE_EQ(int(sim_mode(table.seat(-1), ball)), int(Mode::AUTHORITY));
    Ear ear(table, -1, {transferred()});
    const Vector<Recorder *> &heard = ear.recorders;

    netw_test::FaultWatch faults;
    NETW_REQUIRE_EQ(
        int(table.seat(-1)->get_session()->transfer_server(successor_was)),
        int(OK)
    );
    table.rig.pump(SETTLE_PUMPS);
    for (int at = -1; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(table.rosters_match_links(at)), 1);
    }
    const Ref<NetwPromise> reached = ask(table, -1, route, 1, 7);
    NETW_CHECK_EQ(int(reached->get_is_completed()), 1);
    NETW_CHECK_EQ(int64_t(reached->get_result()), int64_t(70));
    NETW_CHECK_EQ(poke_everyone(table, 2), (CLIENTS + 1) * CLIENTS);
    NETW_CHECK_EQ(faults.errors(), 0);

    NETW_REQUIRE_EQ(heard[0]->count(transferred()), 1);
    const int64_t former_now = int64_t(heard[0]->args(transferred())[1]);
    NETW_CHECK_GT(former_now, 1);
    for (int at = -1; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NetwMultiplayer *seat = table.seat(at);
        const int64_t label = table.now(at, successor_was, former_now);
        NETW_CHECK_EQ(int64_t(seat->get_unique_id()), label);
        NETW_CHECK_EQ(int(seat->is_server()), int(label == 1));
        NETW_CHECK_EQ(
            int(seat->session_get_state()),
            int(NetwMultiplayer::SESSION_STATE_ONLINE)
        );
        NETW_CHECK_EQ(heard[at + 1]->count(transferred()), 1);
        const Array args = heard[at + 1]->args(transferred());
        REQUIRE(args.size() == 2);
        NETW_CHECK_EQ(int64_t(args[0]), successor_was);
        NETW_CHECK_EQ(int64_t(args[1]), former_now);
    }
    NETW_CHECK_EQ(
        int(table.seat(-1)->session_get_role()),
        int(NetwMultiplayer::ROLE_CLIENT)
    );
    NETW_CHECK_EQ(int(sim_mode(table.seat(-1), ball)), int(Mode::PROXY));
    NETW_CHECK_EQ(
        int(table.seat(successor)->session_get_role()),
        int(NetwMultiplayer::ROLE_LISTEN_SERVER)
    );
    const Ref<NetwPlayer> kept = table.seat(-1)->player_local();
    REQUIRE(kept.is_valid());
    NETW_CHECK_EQ(kept->get_peer_id(), former_now);
    for (int at = -1; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        const Ref<NetwPlayer> seen = table.seat(at)->player_of(former_now);
        REQUIRE(seen.is_valid());
        NETW_CHECK_EQ(seen->get_peer_id(), former_now);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX3 after the server is lost "
    "with the world kept, every survivor reads the successor's player and "
    "body as peer 1, a third client's body keeps its controller, and a server "
    "body stays with the server"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    const int third = (successor + 1) % CLIENTS;
    const int64_t successor_was = table.was(successor);
    const int64_t third_label = table.was(third);
    const int carried = table.spawn_body("Carried", successor, successor_was);
    const int steered = table.spawn_body("Steered", third, third_label);
    const int served = table.spawn_body("Served", -2, 0);
    for (int at = 0; at < CLIENTS; ++at) {
        present(table.body_at(steered, at));
        present(table.body_at(served, at));
        NETW_REQUIRE_EQ(
            NetwEntity::of(present(table.body_at(carried, at)))
                ->get_controller(),
            successor_was
        );
    }

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);

    const struct {
        int route;
        int64_t controller;
        NetwEntity::ControlKind kind;
    } rows[] = {
        {carried, 1, NetwEntity::CONTROL_PEER_CONTROLLED},
        {steered, third_label, NetwEntity::CONTROL_PEER_CONTROLLED},
        {served, 1, NetwEntity::CONTROL_SERVER_CONTROLLED},
    };
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NetwMultiplayer *seat = table.seat(at);
        const Ref<NetwPlayer> host = seat->player_of(1);
        REQUIRE(host.is_valid());
        NETW_CHECK_EQ(
            int(same(host->get_username(), vformat("guest%d", successor))),
            1
        );
        const Ref<NetwPlayer> kept = seat->player_of(third_label);
        REQUIRE(kept.is_valid());
        NETW_CHECK_EQ(
            int(same(kept->get_username(), vformat("guest%d", third))),
            1
        );
        for (const auto &row : rows) {
            NETW_FORMAT_INT(route_text, row.route);
            CAPTURE(route_text);
            Node *body = present(table.body_at(row.route, at));
            NETW_CHECK_EQ(
                NetwEntity::of(body)->get_controller(),
                row.controller
            );
            NETW_CHECK_EQ(int64_t(body->get_multiplayer_authority()), 1);
            NETW_CHECK_EQ(
                int(NetwEntity::of(body)->get_control_kind()),
                int(row.kind)
            );
        }
    }
}

constexpr double REPORT_LATENCY_MS = 60000.0;

void slow_reports(Table &p_table, int p_successor) {
    Ref<netw::LocalLinkConditions> slow;
    slow.instantiate();
    slow->set_latency_ms(REPORT_LATENCY_MS);
    for (int at = 0; at < p_table.rig.count(); ++at) {
        if (at != p_successor) {
            p_table.rig.conditions(
                p_successor,
                slow,
                int(p_table.seat(at)->get_unique_id())
            );
        }
    }
}

void free_as_the_frame_would(Node *p_queued) {
    if (p_queued != nullptr && p_queued->get_parent() != nullptr) {
        p_queued->get_parent()->remove_child(p_queued);
    }
}

void lost_host_body_departs(bool p_keep_world) {
    Table table;
    if (p_keep_world) {
        table.keep_world();
    }
    table.forget_departures();
    const int successor = table.lowest_client();
    const int carried = table.spawn_body("HostBody", -1, 0);
    present(table.body_at(carried, successor));
    Ear ear(table, 0, {transferred(), player_left()});
    slow_reports(table, successor);

    netw_test::FaultWatch faults;
    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);
    NETW_CHECK_EQ(faults.errors(), 0);

    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        const Recorder *heard = ear.recorders[at];
        NETW_CHECK_EQ(heard->count(player_left()), 1);
        const Vector<StringName> order = heard->order();
        NETW_REQUIRE_EQ(order.size(), 2);
        NETW_CHECK_EQ(int(order[0] == transferred()), 1);
        NETW_CHECK_EQ(int(order[1] == player_left()), 1);
        const Ref<NetwPlayer> left = heard->args(player_left())[0];
        REQUIRE(left.is_valid());
        NETW_CHECK_EQ(int(same(left->get_username(), "host")), 1);
    }
    Node *body = table.body_at(carried, successor);
    const int64_t stage = body != nullptr ? NetwEntity::of(body)->get_stage()
                                          : int64_t(netw::entity::Stage::FREED);
    NETW_CHECK_GE(stage, int64_t(netw::entity::Stage::DESPAWNING));

    free_as_the_frame_would(body);
    table.rig.pump(SETTLE_PUMPS);
    table.rig.advance(REPORT_LATENCY_MS * 2.0);
    table.rig.pump(SETTLE_PUMPS);

    NETW_CHECK_EQ(int(table.seat(successor)->is_structure_halted()), 0);
    NETW_CHECK_EQ(
        int(table.rig.spawn_plane(successor)->get_spawn_book()->has_spawned(
            carried
        )),
        0
    );
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(table.body_at(carried, at) == nullptr), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX4 a lost server's player "
    "leaves every survivor once, after server_transferred, and the body it "
    "was represented by despawns on the successor and is gone from every "
    "survivor once the successor frees it, in both transfer modes"
) {
    SUBCASE("rejoin") {
        lost_host_body_departs(false);
    }
    SUBCASE("continue") {
        lost_host_body_departs(true);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX5 a call waiting on a lost "
    "server fails, a call to peer 1 afterwards reaches the successor, and a "
    "reply owed to the lost server does not settle the successor's own call"
) {
    Table table;
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    const int64_t survivor_label = table.was(survivor);
    const int route = spawn_probe(table);

    ask(table, -1, route, survivor_label, 100);
    const Ref<NetwPromise> owed = held(table, survivor, route, 100);
    REQUIRE(owed.is_valid());
    const Ref<NetwPromise> stranded = ask(table, survivor, route, 1, 101);
    REQUIRE(held(table, -1, route, 101).is_valid());
    REQUIRE_FALSE(stranded->get_is_settled());

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);
    CHECK(stranded->get_is_failed());

    const Ref<NetwPromise> fresh
        = ask(table, successor, route, survivor_label, 102);
    const Ref<NetwPromise> answering = held(table, survivor, route, 102);
    REQUIRE(answering.is_valid());
    owed->resolve(777);
    table.rig.pump(SETTLE_PUMPS);
    CHECK_FALSE(fresh->get_is_settled());
    answering->resolve(888);
    table.rig.pump(SETTLE_PUMPS);
    REQUIRE(fresh->get_is_completed());
    NETW_CHECK_EQ(int64_t(fresh->get_result()), int64_t(888));

    const Ref<NetwPromise> reached = ask(table, survivor, route, 1, 5);
    REQUIRE(reached->get_is_completed());
    NETW_CHECK_EQ(int64_t(reached->get_result()), int64_t(50));
    Node *at_successor = present(table.body_at(route, successor));
    const Array senders = at_successor->get("senders");
    REQUIRE(senders.size() > 0);
    NETW_CHECK_EQ(int64_t(senders[senders.size() - 1]), survivor_label);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX6 transfer_server answers "
    "ERR_UNAVAILABLE on a transport that cannot move the server, "
    "ERR_UNAUTHORIZED on a client, and ERR_INVALID_PARAMETER for peer 1 or a "
    "peer that is not in the session, and moves nothing"
) {
    Ref<MultiplayerPeer> enet;
    for (int port = ENET_PORT; port < ENET_PORT + ENET_PORTS; ++port) {
        const Ref<MultiplayerPeer> candidate
            = ClassDB::instantiate(StringName("ENetMultiplayerPeer"));
        REQUIRE(candidate.is_valid());
        if (Error(int(candidate->call(StringName("create_server"), port)))
            == OK) {
            enet = candidate;
            break;
        }
    }
    REQUIRE(enet.is_valid());
    Ref<NetwMultiplayer> hosted = NetwMultiplayer::create(Ref<Script>());
    hosted->NETW_API_VIRTUAL(set_multiplayer_peer)(enet);
    NETW_REQUIRE_EQ(
        int(hosted->session_get_state()),
        int(NetwMultiplayer::SESSION_STATE_ONLINE)
    );
    NETW_CHECK_EQ(
        int(hosted->get_session()->transfer_server(2)),
        int(ERR_UNAVAILABLE)
    );
    hosted->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
    enet->close();

    Table table;
    NETW_CHECK_EQ(
        int(table.seat(0)->get_session()->transfer_server(table.was(1))),
        int(ERR_UNAUTHORIZED)
    );
    int64_t stranger = 2;
    while (table.before.has(stranger)) {
        stranger += 1;
    }
    NETW_CHECK_EQ(
        int(table.seat(-1)->get_session()->transfer_server(stranger)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(
        int(table.seat(-1)->get_session()->transfer_server(1)),
        int(ERR_INVALID_PARAMETER)
    );
    table.rig.pump(SETTLE_PUMPS);
    for (int at = -1; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int64_t(table.seat(at)->get_unique_id()), table.was(at));
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX7 a relabel naming a "
    "successor the session does not know ends that seat's session with one "
    "error and renames nothing"
) {
    Table table;
    const int refusing = 0;
    int64_t stranger = 2;
    while (table.before.has(stranger)) {
        stranger += 1;
    }
    NetwMultiplayer *seat = table.seat(refusing);
    const Ref<NetwPlayer> own = seat->player_local();
    REQUIRE(own.is_valid());

    netw_test::FaultWatch faults;
    table.rig.peer(refusing)
        ->emit_signal(StringName("server_relabelled"), stranger, 0);
    NETW_CHECK_EQ(faults.errors(), 1);
    NETW_CHECK_EQ(
        int(seat->session_get_state()),
        int(NetwMultiplayer::SESSION_STATE_OFFLINE)
    );
    NETW_CHECK_EQ(own->get_peer_id(), table.was(refusing));
    NETW_CHECK_EQ(int64_t(seat->get_unique_id()), table.was(refusing));
    for (int at = -1; at < CLIENTS; ++at) {
        if (at == refusing) {
            continue;
        }
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(
            int(table.seat(at)->session_get_state()),
            int(NetwMultiplayer::SESSION_STATE_ONLINE)
        );
    }
}

constexpr int VIEWER_ROUNDS = 8;
const char *LEVEL_ID = "transfer_level";
const char *MEMBER_ID = "transfer_member";

Node *build_level(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    const Ref<NetwEntity> facet = NetwEntity::ensure(made);
    REQUIRE(facet.is_valid());
    facet->set_declares_scene(true);
    facet->set_scene_label(StringName(String(p_name)));
    return made;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array string_types() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

int open_level(Table &p_table) {
    const int route = p_table.rig.spawn_registered(
        StringName(LEVEL_ID),
        callable_mp_static(&build_level),
        named("Level"),
        string_types(),
        p_table.arena,
        Variant()
    );
    REQUIRE(route > 0);
    p_table.rig.pump(SETTLE_PUMPS);
    REQUIRE(p_table.rig.server()->scene_is_declared(
        p_table.rig.server()->entity_from_route(route)
    ));
    return route;
}

int spawn_member(Table &p_table, int p_level, const char *p_name, int p_owner) {
    const Ref<NetwPlayer> owner
        = p_owner < -1 ? Ref<NetwPlayer>() : p_table.player_at(p_owner);
    const int route = p_table.rig.spawn_registered(
        StringName(MEMBER_ID),
        callable_mp_static(&build_body),
        named(p_name),
        string_types(),
        present(p_table.body_at(p_level, -1)),
        owner
    );
    REQUIRE(route > 0);
    return route;
}

RID handle_at(const Table &p_table, int p_route, int p_at) {
    return p_table.seat(p_at)->entity_from_route(p_route);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX8 after the server is lost "
    "with the world kept, an entity a survivor subscribed to is still "
    "subscribed to relayed input on the successor"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    const int64_t survivor_label = table.was(survivor);
    netw_test::WorldDecl services;
    services.clocked(30, 3).lag_compensated();
    table.rig.declare_world(services);
    const int level = open_level(table);
    spawn_member(table, level, "Leader", successor);
    const int route = spawn_member(table, level, "Follower", survivor);
    NetwEntity::of(present(table.body_at(route, -1)))
        ->set_controller(survivor_label);
    table.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *joint = table.seat(survivor);
    const RID mirrored = handle_at(table, route, survivor);
    REQUIRE(mirrored.is_valid());
    NETW_REQUIRE_EQ(int(joint->predict_declare(mirrored)), int(OK));
    netw::NetwPredictionEngine *pool = joint->get_prediction_engine();
    Node *body = present(table.body_at(route, survivor));
    const int64_t slot = pool->slot_of(NetwEntity::of(body));
    REQUIRE(slot >= 0);
    pool->follow_relay_subscription(slot, true);
    table.rig.pump(SETTLE_PUMPS);
    const RID served = handle_at(table, route, -1);
    REQUIRE(table.rig.server()
                ->relay_peers(int64_t(served.get_id()))
                .has(survivor_label));

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *host = table.seat(successor);
    const RID relayed = handle_at(table, route, successor);
    REQUIRE(relayed.is_valid());
    NETW_CHECK_EQ(
        int(host->relay_peers(int64_t(relayed.get_id())).has(survivor_label)),
        1
    );
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX9 after the server is lost "
    "the successor files each entity under the scene it resides in and shows "
    "it to that scene's players only"
) {
    Table table;
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    const int third = (successor + 2) % CLIENTS;
    const int level = open_level(table);
    spawn_member(table, level, "Leader", successor);
    spawn_member(table, level, "Follower", survivor);
    const int crate = spawn_member(table, level, "Crate", -2);
    table.rig.pump(SETTLE_PUMPS);
    present(table.body_at(crate, successor));
    REQUIRE(table.body_at(crate, third) == nullptr);

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *host = table.seat(successor);
    const RID held = handle_at(table, crate, successor);
    REQUIRE(held.is_valid());
    const StringName filed
        = host->interest_plane().scene_membership(int64_t(held.get_id()));
    const StringName layer
        = host->scene_layer_id(handle_at(table, level, successor));
    REQUIRE_FALSE(layer.is_empty());
    NETW_CHECK_EQ(int(filed == layer), 1);
    NETW_CHECK_EQ(int(host->interest_admits(held, table.was(survivor))), 1);
    NETW_CHECK_EQ(int(host->interest_admits(held, table.was(third))), 0);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX10 after the server is lost "
    "with the world kept, the successor counts each player's body in its "
    "scene, a survivor that missed the last viewer list takes the one the "
    "successor publishes, and a scene request waiting on the lost server fails"
) {
    Table table(false, true);
    table.keep_world();
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    const int third = (successor + 2) % CLIENTS;
    const int64_t survivor_label = table.was(survivor);
    const int64_t third_label = table.was(third);
    const int level = open_level(table);
    spawn_member(table, level, "Leader", successor);
    spawn_member(table, level, "Follower", survivor);
    table.rig.pump(SETTLE_PUMPS);
    const RID served = handle_at(table, level, -1);
    for (int round = 0; round < VIEWER_ROUNDS; ++round) {
        REQUIRE(table.seat(-1)->scene_admit_peer(served, third_label));
        REQUIRE(table.seat(-1)->scene_release_peer(served, third_label));
        table.rig.pump(SETTLE_PUMPS);
    }

    table.rig.hold(survivor);
    REQUIRE(table.seat(-1)->scene_admit_peer(served, third_label));
    table.rig.pump(SETTLE_PUMPS);
    const RID seen = handle_at(table, level, survivor);
    REQUIRE(seen.is_valid());
    REQUIRE_FALSE(table.seat(survivor)->scene_watches(seen, third_label));
    REQUIRE(table.seat(successor)->scene_watches(
        handle_at(table, level, successor),
        third_label
    ));
    const Ref<NetwPromise> asked = table.seat(third)->scene_request(
        "res://transfer_missing.tscn",
        NetwMultiplayer::SCENE_CHANGE_PLAYER
    );
    REQUIRE_FALSE(asked->get_is_settled());

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.release(survivor);
    table.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *host = table.seat(successor);
    const RID scene = handle_at(table, level, successor);
    NETW_CHECK_EQ(host->membership_book().bodies_in(1, scene), 1);
    NETW_CHECK_EQ(host->membership_book().bodies_in(survivor_label, scene), 1);
    NETW_CHECK_EQ(
        int(table.seat(survivor)->scene_watches(seen, third_label)),
        1
    );
    REQUIRE(asked->get_is_failed());
    NETW_CHECK_EQ(int(asked->get_code()), int(ERR_UNAVAILABLE));
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX11 a client still waiting "
    "for admission when the server is lost is admitted by the successor with "
    "the join it sent, under a player id no other player holds"
) {
    Table table(true);
    const int successor = table.lowest_client();
    const int late = table.waiting;
    const int64_t late_label = table.was(late);
    NetwMultiplayer *waiting = table.seat(late);
    REQUIRE(waiting->player_local().is_null());
    int64_t issued = 0;
    for (int at = -1; at < CLIENTS; ++at) {
        const int64_t held = table.seat(-1)->player_incarnation(table.was(at));
        issued = held > issued ? held : issued;
    }
    REQUIRE(issued > 0);
    waiting->session_submit_join(StringName("late"), Array());

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);

    const Ref<NetwPlayer> own = waiting->player_local();
    NETW_REQUIRE_EQ(int(own.is_valid()), 1);
    NETW_CHECK_EQ(int(same(own->get_username(), "late")), 1);
    NETW_CHECK_EQ(own->get_peer_id(), late_label);
    NetwMultiplayer *host = table.seat(successor);
    NETW_CHECK_GT(host->player_incarnation(late_label), issued);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        const Ref<NetwPlayer> seen = table.seat(at)->player_of(late_label);
        NETW_REQUIRE_EQ(int(seen.is_valid()), 1);
        NETW_CHECK_EQ(int(same(seen->get_username(), "late")), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX12 transfer_server answers "
    "ERR_BUSY while a database write is unsettled and moves nothing, and "
    "once it settles the successor reports once that saving stopped"
) {
    netw::schema_model::clear();
    netw::persist::forget_stores();
    Table table;
    const int successor = 1;
    const int64_t successor_was = table.was(successor);
    const int saved = table.spawn_body("Saved", -2, 0);
    netw::Netw::configure_persistence(present(table.body_at(saved, successor)))
        ->database(StringName("saves"))
        ->table(StringName("transfer_saves"))
        ->load_on_spawn(false);

    NetwMultiplayer *server = table.seat(-1);
    const Ref<netw::NetwSchema> schema
        = netw::NetwSchema::create("transfer_saves");
    schema->replicated(false);
    schema->f64("score", Ref<netw::NetwQuantize>(), 1);
    const RID database = server->get_databases()->create("saves");
    const Ref<netw::persist::MemoryConnection> connection
        = netw::persist::MemoryConnection::opened("transfer", "slot1");
    server->get_databases()
        ->open(database, "slot1", NetwPromise::resolved(connection));
    connection->defer(true);
    Dictionary values;
    values[StringName("score")] = 3.0;
    const Ref<NetwPromise> written = server->database_write(
        database,
        server->schema_of_declaration(schema),
        StringName("hero"),
        values
    );
    REQUIRE_FALSE(written->get_is_settled());

    NETW_CHECK_EQ(
        int(server->get_session()->transfer_server(successor_was)),
        int(ERR_BUSY)
    );
    table.rig.pump(SETTLE_PUMPS);
    for (int at = -1; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int64_t(table.seat(at)->get_unique_id()), table.was(at));
    }

    connection->release();
    table.rig.pump(SETTLE_PUMPS);
    REQUIRE(written->get_is_completed());
    netw_test::FaultWatch faults;
    NETW_REQUIRE_EQ(
        int(server->get_session()->transfer_server(successor_was)),
        int(OK)
    );
    table.rig.pump(SETTLE_PUMPS);
    NETW_CHECK_EQ(int64_t(table.seat(successor)->get_unique_id()), int64_t(1));
    NETW_CHECK_EQ(faults.errors(), 1);
    NETW_CHECK_EQ(faults.errors_containing("database state does not move"), 1);
    netw::schema_model::clear();
    netw::persist::forget_stores();
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX13 a control claim waiting "
    "on a lost server fails with ERR_UNAVAILABLE and says the server moved"
) {
    Table table;
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    const int served = table.spawn_body("Claimed", -2, 0);
    const Ref<NetwEntity> claimant
        = NetwEntity::of(present(table.body_at(served, survivor)));
    const Ref<NetwPromise> claim = claimant->claim_control();
    REQUIRE_FALSE(claim->get_is_settled());

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);

    REQUIRE(claim->get_is_failed());
    NETW_CHECK_EQ(int(claim->get_code()), int(ERR_UNAVAILABLE));
    NETW_CHECK_EQ(int(claim->get_detail().contains("server moved")), 1);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX14 a survivor that missed a "
    "player's admission before the server was lost learns that player from "
    "the successor's roster"
) {
    Table table(true);
    const int successor = table.lowest_client();
    const int late = table.waiting;
    const int deaf = (late + 1) % CLIENTS == successor ? (late + 2) % CLIENTS
                                                       : (late + 1) % CLIENTS;
    const int64_t late_label = table.was(late);
    table.rig.hold(deaf);
    table.rig.join(late, StringName("late"));
    REQUIRE(table.seat(successor)->player_of(late_label).is_valid());
    REQUIRE(table.seat(deaf)->player_of(late_label).is_null());

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.release(deaf);
    table.rig.pump(SETTLE_PUMPS);

    const Ref<NetwPlayer> learned = table.seat(deaf)->player_of(late_label);
    NETW_REQUIRE_EQ(int(learned.is_valid()), 1);
    NETW_CHECK_EQ(int(same(learned->get_username(), "late")), 1);
}

const char *MAKER_SOURCE = R"(extends Node


func _init() -> void:
	Netw.configure_spawn(make)


func make(label: String) -> Node:
	var made := Node2D.new()
	made.name = label
	return made
)";

const char *CHICK_SOURCE = R"(extends Node2D


func _init() -> void:
	Netw.configure_entity(self).name = &"Chick"
)";

const char *SPAWNER_SOURCE = R"(extends MultiplayerSpawner


func _ready() -> void:
	set_spawn_function(build_one)


func build_one(data: Variant) -> Node:
	var made := Node2D.new()
	made.name = str(data)
	return made
)";

const char *NEST_ID = "transfer_nest";

Node *build_nest(const Variant &p_name, const Variant &p_chick) {
    const Ref<Script> script = p_chick;
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    Node *chick
        = Object::cast_to<Node>(netw::gd::live_object(script->call("new")));
    chick->set_name("Chick");
    made->add_child(chick);
    return made;
}

struct Kinds {
    Ref<Script> maker;
    Ref<Script> chick;
    Ref<Script> spawner;
    Ref<PackedScene> crate;
    Vector<int> routes;
    Vector<int> recipes;

    Kinds() {
        maker = netw_test::minted_script(MAKER_SOURCE);
        chick = netw_test::minted_script(CHICK_SOURCE);
        spawner = netw_test::minted_script(SPAWNER_SOURCE);
        Node2D *shape = memnew(Node2D);
        shape->set_name("Crate");
        crate = netw_test::pack_stock_scene(
            shape,
            netw_test::mint_scene_path("TransferCrate")
        );
        memdelete(shape);
    }

    bool ready() const {
        return maker.is_valid() && chick.is_valid() && spawner.is_valid()
            && crate.is_valid();
    }

    void seat(Table &p_table, int p_at) const {
        p_table.rig.register_constructor(
            p_table.seat(p_at),
            StringName(NEST_ID),
            callable_mp_static(&build_nest).bind(chick),
            string_types()
        );
        Node *branch = p_table.rig.branch(p_at);
        Node *host
            = Object::cast_to<Node>(netw::gd::live_object(maker->call("new")));
        host->set_name("Maker");
        branch->add_child(host);
        MultiplayerSpawner *made = Object::cast_to<MultiplayerSpawner>(
            netw::gd::live_object(spawner->call("new"))
        );
        made->set_name("Spawner");
        made->set_spawn_path(NodePath("../Arena"));
        p_table.seat(p_at)->spawner_session_entered();
        branch->add_child(made);
    }

    void note(Node *p_node, netw::spawn::Book::Recipe p_recipe) {
        const Ref<NetwEntity> entity = NetwEntity::of(p_node);
        routes.push_back(entity.is_valid() ? int(entity->get_route()) : 0);
        recipes.push_back(int(p_recipe));
    }

    void spawn(Table &p_table) {
        NetwMultiplayer *server = p_table.rig.server();
        netw::spawn::Pipeline *pipeline = p_table.rig.spawn_plane(-1);
        Node *arena = p_table.arena;

        const RID nest_entity = server->spawn_registered(
            StringName(NEST_ID),
            named("Nest"),
            nullptr
        );
        Node *nest = server->entity_get_node(nest_entity);
        NETW_REQUIRE_EQ(int(nest != nullptr), 1);
        arena->add_child(nest);

        Node *crate_node = crate->instantiate();
        crate_node->set_name("Crate");
        REQUIRE(pipeline->replicate(crate_node, Ref<NetwPlayer>()).is_valid());
        arena->add_child(crate_node);

        Node *host
            = p_table.rig.branch(-1)->get_node_or_null(NodePath("Maker"));
        NETW_REQUIRE_EQ(int(host != nullptr), 1);
        Node *made = pipeline->spawn(
            Callable(host, StringName("make")),
            named("Made"),
            Ref<NetwPlayer>()
        );
        NETW_REQUIRE_EQ(int(made != nullptr), 1);
        arena->add_child(made);

        MultiplayerSpawner *spawner = Object::cast_to<MultiplayerSpawner>(
            p_table.rig.branch(-1)->get_node_or_null(NodePath("Spawner"))
        );
        NETW_REQUIRE_EQ(int(spawner != nullptr), 1);
        Node *spawned = spawner->spawn(String("Spawned"));
        NETW_REQUIRE_EQ(int(spawned != nullptr), 1);
        p_table.rig.pump(SETTLE_PUMPS);

        note(nest, netw::spawn::Book::RECIPE_FN_REGISTRY);
        note(
            nest->get_node_or_null(NodePath("Chick")),
            netw::spawn::Book::RECIPE_ADOPT
        );
        note(crate_node, netw::spawn::Book::RECIPE_SCENE);
        note(made, netw::spawn::Book::RECIPE_FN);
        note(spawned, netw::spawn::Book::RECIPE_SPAWNER);
    }
};

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX15 a peer that joins after "
    "the server is lost receives every entity the successor held, one of "
    "each recipe kind"
) {
    Table table;
    const int successor = table.lowest_client();
    Kinds kinds;
    NETW_REQUIRE_EQ(int(kinds.ready()), 1);
    for (int at = -1; at < CLIENTS; ++at) {
        kinds.seat(table, at);
    }
    table.rig.pump(SETTLE_PUMPS);
    kinds.spawn(table);
    NETW_REQUIRE_EQ(kinds.routes.size(), 5);
    for (int at = 0; at < kinds.routes.size(); ++at) {
        NETW_FORMAT_INT(recipe_text, kinds.recipes[at]);
        CAPTURE(recipe_text);
        NETW_REQUIRE_EQ(
            int(table.body_at(kinds.routes[at], successor) != nullptr),
            1
        );
    }

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);

    const int late = table.rig.add_client();
    table.rig.hold(late);
    table.rig.mount_late(late);
    table.rig.mirror_late(late, "Arena");
    kinds.seat(table, late);
    table.rig.release(late);
    table.rig.pump(SETTLE_PUMPS);

    const netw::spawn::Book *book
        = table.rig.spawn_plane(successor)->get_spawn_book();
    for (int at = 0; at < kinds.routes.size(); ++at) {
        NETW_FORMAT_INT(recipe_text, kinds.recipes[at]);
        CAPTURE(recipe_text);
        const netw::spawn::Record *record = book->spawned_of(kinds.routes[at]);
        NETW_CHECK_EQ(
            record != nullptr ? record->get_recipe() : -1,
            kinds.recipes[at]
        );
        NETW_CHECK_EQ(int(table.body_at(kinds.routes[at], late) != nullptr), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX16 after the server is lost "
    "with the world kept, the successor's despawn of an entity it held as a "
    "client reaches a survivor, also when that entity is on its second life"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    for (int at = -1; at < CLIENTS; ++at) {
        table.rig.register_constructor(
            table.seat(at),
            StringName(BODY_ID),
            callable_mp_static(&build_body),
            string_types()
        );
    }
    Node *pen = table.rig.mirror_child("Pen");
    NetwMultiplayer *server = table.seat(-1);
    const RID entity = server->spawn_registered(
        StringName(BODY_ID),
        named("Doomed"),
        nullptr
    );
    Node *served = server->entity_get_node(entity);
    NETW_REQUIRE_EQ(int(served != nullptr), 1);
    table.arena->add_child(served);
    table.rig.pump(SETTLE_PUMPS);
    const int route = int(server->entity_get_route(entity));
    const Ref<netw::NetwLivenessCore> lives = server->get_liveness_core();
    const RID life = lives->rid_from_route(route);
    lives->set_state(life, netw::NetwLivenessCore::STATE_DEAD);
    NETW_REQUIRE_EQ(int(lives->bind_route(life, route)), 1);
    served->reparent(pen);
    table.rig.pump(SETTLE_PUMPS);
    NETW_REQUIRE_EQ(
        int(table.seat(survivor)->liveness_route_wire_life(route)),
        1
    );
    NETW_REQUIRE_EQ(int(table.body_at(route, survivor) != nullptr), 1);

    REQUIRE(int(table.rig.session()->lose_server()) == int(OK));
    table.rig.pump(SETTLE_PUMPS);

    Node *held = table.body_at(route, successor);
    NETW_REQUIRE_EQ(int(held != nullptr), 1);
    held->get_parent()->remove_child(held);
    table.rig.pump(SETTLE_PUMPS);
    memdelete(held);

    NETW_CHECK_EQ(int(table.body_at(route, survivor) == nullptr), 1);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX17 after a server hands "
    "itself on with the world kept, the successor's despawn of an entity the "
    "former server spawned reaches the former server and every other client"
) {
    Table table;
    table.keep_world();
    const int successor = 1;
    const int64_t successor_was = table.was(successor);
    const int route = table.spawn_body("Handed", -2, 0);
    NETW_REQUIRE_EQ(
        int(table.seat(-1)->get_session()->transfer_server(successor_was)),
        int(OK)
    );
    table.rig.pump(SETTLE_PUMPS);

    Node *held = table.body_at(route, successor);
    NETW_REQUIRE_EQ(int(held != nullptr), 1);
    held->get_parent()->remove_child(held);
    table.rig.pump(SETTLE_PUMPS);
    memdelete(held);

    for (int at = -1; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(table.body_at(route, at) == nullptr), 1);
    }
}

constexpr int TICKRATE = 30;
constexpr int SETTLE_TICKS = 24;
constexpr int QUIET_ENTITIES = 6;
const char *VITAL_ID = "transfer_vital";

const char *VITAL_SOURCE = R"(extends Node2D

var hp := 0


func _init() -> void:
	Netw.configure_property(self, &"hp").broadcast()
)";

const char *KEEPER_SOURCE = R"(extends Node


func _init() -> void:
	Netw.configure_transfer(keep)


func keep(node: Node) -> bool:
	return node.name != &"Doomed"
)";

Node *build_leased(const Variant &p_name) {
    Node *made = build_body(p_name);
    NetwEntity::ensure(made)->set_lifecycle(NetwEntity::LIFECYCLE_CONTROLLER);
    return made;
}

bool alive(const Table &p_table, int p_route, int p_at) {
    Node *node = p_table.body_at(p_route, p_at);
    if (node == nullptr || !node->is_inside_tree()) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    return entity.is_valid()
        && entity->get_stage() < int64_t(netw::entity::Stage::DESPAWNING);
}

bool left_tree(ObjectID p_node) {
    Node *node = Object::cast_to<Node>(netw::gd::object_of(p_node));
    return node == nullptr || !node->is_inside_tree();
}

int64_t spawn_frames_in(NetwMultiplayer *p_api, const char *p_channel) {
    const Dictionary rows
        = p_api->attribution_snapshot()["frames_in_by_channel"];
    const Dictionary channels = rows.get(int64_t(1), Dictionary());
    return int64_t(channels.get(
        int64_t(netw::wire::builtin_channel(StringName(p_channel))),
        0
    ));
}

int64_t recv_count(Table &p_table, int p_at) {
    const Dictionary counted = p_table.rig.spawn_plane(p_at)->counters();
    return int64_t(counted.get(StringName("spawn_book_recv"), -1));
}

int64_t refused_duplicates(Table &p_table, int p_at) {
    const Dictionary counted = p_table.rig.spawn_plane(p_at)->counters();
    return int64_t(counted.get(StringName("drops_spawn_duplicate"), -1));
}

struct Vitals {
    Ref<Script> script;

    explicit Vitals(Table &p_table) {
        script = netw_test::minted_script(VITAL_SOURCE);
        REQUIRE(script.is_valid());
        for (int at = -1; at < CLIENTS; ++at) {
            p_table.rig.register_constructor(
                p_table.seat(at),
                StringName(VITAL_ID),
                callable_mp_static(&build_probe).bind(script),
                string_types()
            );
        }
    }

    int spawn(Table &p_table, const char *p_name) const {
        NetwMultiplayer *server = p_table.rig.server();
        const RID entity = server->spawn_registered(
            StringName(VITAL_ID),
            named(p_name),
            nullptr
        );
        Node *node = server->entity_get_node(entity);
        if (node == nullptr) {
            return 0;
        }
        p_table.arena->add_child(node);
        p_table.rig.pump(SETTLE_PUMPS);
        return int(server->entity_get_route(entity));
    }
};

void seat_keepers(Table &p_table) {
    const Ref<Script> script = netw_test::minted_script(KEEPER_SOURCE);
    REQUIRE(script.is_valid());
    for (int at = -1; at < CLIENTS; ++at) {
        Node *keeper
            = Object::cast_to<Node>(netw::gd::live_object(script->call("new")));
        keeper->set_name("Keeper");
        p_table.rig.branch(at)->add_child(keeper);
    }
    p_table.rig.pump(SETTLE_PUMPS);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX18 after the server is lost "
    "every survivor holds exactly the entities the successor serves it, each "
    "as a fresh node, drops one the successor never held, and receives state "
    "again"
) {
    Table table;
    table.rig.declare_world(netw_test::WorldDecl().clocked(TICKRATE, 3));
    const int successor = table.lowest_client();
    const Vitals vitals(table);
    const int shared = vitals.spawn(table, "Shared");
    NETW_REQUIRE_EQ(int(shared > 0), 1);
    table.rig.hold(successor);
    const int orphan = vitals.spawn(table, "Orphan");
    NETW_REQUIRE_EQ(int(orphan > 0), 1);
    NETW_REQUIRE_EQ(int(table.body_at(orphan, successor) == nullptr), 1);
    ObjectID before[CLIENTS];
    for (int at = 0; at < CLIENTS; ++at) {
        Node *held = table.body_at(shared, at);
        NETW_REQUIRE_EQ(int(held != nullptr), 1);
        before[at] = ObjectID(held->get_instance_id());
        if (at != successor) {
            NETW_REQUIRE_EQ(int(table.body_at(orphan, at) != nullptr), 1);
        }
    }
    const int watched = (successor + 1) % CLIENTS;
    const int64_t duplicates = refused_duplicates(table, watched);

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.release(successor);
    table.rig.pump(SETTLE_PUMPS);

    const PackedInt64Array served
        = table.rig.spawn_plane(successor)->get_spawn_book()->spawned_routes();
    NETW_CHECK_EQ(int(served.has(shared)), 1);
    NETW_CHECK_EQ(int(served.has(orphan)), 0);
    for (int at = 0; at < CLIENTS; ++at) {
        if (at == successor) {
            continue;
        }
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(recv_count(table, at), int64_t(served.size()));
        for (int which = 0; which < served.size(); ++which) {
            NETW_CHECK_EQ(int(alive(table, int(served[which]), at)), 1);
        }
        NETW_CHECK_EQ(int(table.body_at(orphan, at) == nullptr), 1);
        Node *fresh = table.body_at(shared, at);
        NETW_REQUIRE_EQ(int(fresh != nullptr), 1);
        const ObjectID now(fresh->get_instance_id());
        NETW_CHECK_EQ(int(now != before[at]), 1);
        NETW_CHECK_EQ(int(left_tree(before[at])), 1);
    }
    NETW_CHECK_EQ(refused_duplicates(table, watched), duplicates);

    Node *source = table.body_at(shared, successor);
    NETW_REQUIRE_EQ(int(source != nullptr), 1);
    source->set(StringName("hp"), 42);
    table.rig.step_ticks(SETTLE_TICKS);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        Node *held = table.body_at(shared, at);
        NETW_REQUIRE_EQ(int(held != nullptr), 1);
        NETW_CHECK_EQ(int64_t(held->get(StringName("hp"))), int64_t(42));
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX19 an entity the "
    "Netw.configure_transfer handler refuses after the server is lost "
    "despawns on the successor and reaches no survivor, while the rest stay "
    "everywhere"
) {
    Table table;
    const int successor = table.lowest_client();
    seat_keepers(table);
    const int kept = table.spawn_body("Kept", -2, 0);
    const int doomed = table.spawn_body("Doomed", -2, 0);
    int64_t heard[CLIENTS];
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_REQUIRE_EQ(int(alive(table, doomed, at)), 1);
        heard[at] = spawn_frames_in(table.seat(at), "SPAWN");
    }

    netw_test::FaultWatch faults;
    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);
    NETW_CHECK_EQ(faults.errors(), 0);

    const int64_t served
        = table.rig.spawn_plane(successor)->get_spawn_book()->spawned_count();
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(alive(table, doomed, at)), 0);
        NETW_CHECK_EQ(int(alive(table, kept, at)), 1);
        if (at != successor) {
            NETW_CHECK_EQ(
                spawn_frames_in(table.seat(at), "SPAWN") - heard[at],
                served
            );
        }
    }
    NETW_CHECK_EQ(
        int(table.rig.spawn_plane(successor)->get_spawn_book()->has_spawned(
            doomed
        )),
        0
    );
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX20 an entity a survivor "
    "spawned as its controller is gone from every peer after the server is "
    "lost, while one the server spawned for that survivor to steer stays"
) {
    Table table;
    table.rig.declare_world(netw_test::WorldDecl().clocked(TICKRATE, 3));
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    for (int at = -1; at < CLIENTS; ++at) {
        table.rig.register_constructor(
            table.seat(at),
            StringName("transfer_leased"),
            callable_mp_static(&build_leased),
            string_types()
        );
    }
    const RID served = table.rig.server()->spawn_registered(
        StringName("transfer_leased"),
        named("Steady"),
        nullptr
    );
    Node *steered = table.rig.server()->entity_get_node(served);
    NETW_REQUIRE_EQ(int(steered != nullptr), 1);
    table.arena->add_child(steered);
    NetwEntity::of(steered)->set_controller(table.was(survivor));
    const int steady = int(table.rig.server()->entity_get_route(served));
    table.rig.step_ticks(4);
    NetwMultiplayer *author = table.seat(survivor);
    const RID entity = author->spawn_registered(
        StringName("transfer_leased"),
        named("Leased"),
        nullptr
    );
    Node *made = author->entity_get_node(entity);
    NETW_REQUIRE_EQ(int(made != nullptr), 1);
    table.rig.branch(survivor)
        ->get_node_or_null(NodePath("Arena"))
        ->add_child(made);
    table.rig.step_ticks(SETTLE_TICKS);
    const int route = int(author->entity_get_route(entity));
    for (int at = -1; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_REQUIRE_EQ(int(alive(table, route, at)), 1);
    }

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);
    table.rig.step_ticks(SETTLE_TICKS);

    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(alive(table, route, at)), 0);
        NETW_CHECK_EQ(int(alive(table, steady, at)), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX21 after the server is lost "
    "a survivor's clock holds until the successor's roster reaches it and "
    "ticks again after, while the successor's clock keeps ticking"
) {
    Table table;
    table.rig.declare_world(netw_test::WorldDecl().clocked(TICKRATE, 3));
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    table.rig.step_ticks(4);
    table.rig.hold(survivor);

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);
    const netw::ClockEngine &held = table.seat(survivor)->clock_engine();
    const netw::ClockEngine &serving = table.seat(successor)->clock_engine();
    const int stopped = held.get_tick();
    const int serving_from = serving.get_tick();
    table.rig.step_ticks(5);
    NETW_CHECK_EQ(held.get_tick(), stopped);
    NETW_CHECK_EQ(int(held.is_held()), 1);
    NETW_CHECK_EQ(serving.get_tick() - serving_from, 5);

    table.rig.release(survivor);
    table.rig.pump(SETTLE_PUMPS);
    NETW_CHECK_EQ(int(held.is_held()), 0);
    const int resumed = held.get_tick();
    table.rig.step_ticks(5);
    NETW_CHECK_EQ(held.get_tick() - resumed, 5);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX22 a server that hands "
    "itself on frees the world it spawned and receives the successor's as "
    "fresh nodes"
) {
    Table table;
    const int successor = 1;
    const int64_t successor_was = table.was(successor);
    const int route = table.spawn_body("Handed", -2, 0);
    Node *held = table.body_at(route, -1);
    NETW_REQUIRE_EQ(int(held != nullptr), 1);
    const ObjectID before(held->get_instance_id());

    NETW_REQUIRE_EQ(
        int(table.seat(-1)->get_session()->transfer_server(successor_was)),
        int(OK)
    );
    table.rig.pump(SETTLE_PUMPS);

    const netw::spawn::Book *book = table.rig.spawn_plane(-1)->get_spawn_book();
    NETW_CHECK_EQ(book->spawned_count(), 0);
    NETW_CHECK_EQ(
        recv_count(table, -1),
        int64_t(
            table.rig.spawn_plane(successor)->get_spawn_book()->spawned_count()
        )
    );
    NETW_CHECK_EQ(int(alive(table, route, -1)), 1);
    Node *fresh = table.body_at(route, -1);
    NETW_REQUIRE_EQ(int(fresh != nullptr), 1);
    NETW_CHECK_EQ(int(ObjectID(fresh->get_instance_id()) != before), 1);
    NETW_CHECK_EQ(int(left_tree(before)), 1);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX23 a quiet rejoin sends each "
    "survivor one spawn per entity the successor serves it and no despawn, "
    "hide or reparent"
) {
    Table table;
    const int successor = table.lowest_client();
    for (int at = 0; at < QUIET_ENTITIES; ++at) {
        NETW_FORMAT_INT(quiet_name, at);
        NETW_REQUIRE_EQ(int(table.spawn_body(quiet_name, -2, 0) > 0), 1);
    }
    int64_t spawns[CLIENTS];
    int64_t others[CLIENTS];
    for (int at = 0; at < CLIENTS; ++at) {
        NetwMultiplayer *seat = table.seat(at);
        spawns[at] = spawn_frames_in(seat, "SPAWN");
        others[at] = spawn_frames_in(seat, "DESPAWN")
            + spawn_frames_in(seat, "HIDE") + spawn_frames_in(seat, "REPARENT");
    }

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);

    const int64_t served
        = table.rig.spawn_plane(successor)->get_spawn_book()->spawned_count();
    NETW_CHECK_EQ(served, int64_t(QUIET_ENTITIES));
    for (int at = 0; at < CLIENTS; ++at) {
        if (at == successor) {
            continue;
        }
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NetwMultiplayer *seat = table.seat(at);
        NETW_CHECK_EQ(spawn_frames_in(seat, "SPAWN") - spawns[at], served);
        NETW_CHECK_EQ(
            spawn_frames_in(seat, "DESPAWN") + spawn_frames_in(seat, "HIDE")
                + spawn_frames_in(seat, "REPARENT") - others[at],
            int64_t(0)
        );
    }
}

constexpr int MANY_ENTITIES = 900;
constexpr int SPLIT_ENTITIES = 400;
constexpr int ORPHANS = 3;
constexpr int MISSED_JOINS = 3;

int64_t channel_in(
    NetwMultiplayer *p_api,
    const char *p_key,
    int64_t p_peer,
    const char *p_channel
) {
    const Dictionary rows = p_api->attribution_snapshot()[StringName(p_key)];
    const Dictionary channels = rows.get(p_peer, Dictionary());
    return int64_t(channels.get(
        int64_t(netw::wire::builtin_channel(StringName(p_channel))),
        0
    ));
}

int64_t verbs_in(NetwMultiplayer *p_api) {
    return spawn_frames_in(p_api, "SPAWN") + spawn_frames_in(p_api, "DESPAWN")
        + spawn_frames_in(p_api, "CONTROL_APPLY");
}

int spawn_bodies(Table &p_table, int p_count) {
    for (int at = -1; at < CLIENTS; ++at) {
        p_table.rig.register_constructor(
            p_table.seat(at),
            StringName(BODY_ID),
            callable_mp_static(&build_body),
            string_types()
        );
    }
    NetwMultiplayer *server = p_table.rig.server();
    int spawned = 0;
    for (int at = 0; at < p_count; ++at) {
        NETW_FORMAT_INT(body_name, at);
        const RID entity = server->spawn_registered(
            StringName(BODY_ID),
            named(body_name),
            nullptr
        );
        Node *node = server->entity_get_node(entity);
        if (node != nullptr) {
            p_table.arena->add_child(node);
            spawned += 1;
        }
    }
    p_table.rig.pump(SETTLE_PUMPS);
    return spawned;
}

int held_by(Table &p_table, int p_server, int64_t p_peer) {
    netw::spawn::Pipeline *plane = p_table.rig.spawn_plane(p_server);
    const PackedInt64Array served = plane->get_spawn_book()->spawned_routes();
    int held = 0;
    for (int at = 0; at < served.size(); ++at) {
        const netw::spawn::Record *record = plane->spawned_record(served[at]);
        held += record != nullptr && record->has_recipient(int(p_peer)) ? 1 : 0;
    }
    return held;
}

int spawn_on(Table &p_table, int p_at, const char *p_id, const char *p_name) {
    NetwMultiplayer *api = p_table.seat(p_at);
    const RID entity
        = api->spawn_registered(StringName(p_id), named(p_name), nullptr);
    Node *node = api->entity_get_node(entity);
    if (node == nullptr) {
        return 0;
    }
    p_table.rig.branch(p_at)
        ->get_node_or_null(NodePath("Arena"))
        ->add_child(node);
    return int(api->entity_get_route(entity));
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX24 after the server is lost "
    "with the world kept, a spawn on the successor takes a route no survivor "
    "held and reaches every survivor"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    const Vitals vitals(table);
    table.rig.hold(successor);
    Vector<int> orphans;
    for (int at = 0; at < ORPHANS; ++at) {
        orphans.push_back(vitals.spawn(table, "Orphan"));
    }

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.release(successor);
    table.rig.pump(SETTLE_PUMPS);
    const int fresh = spawn_on(table, successor, VITAL_ID, "Fresh");
    table.rig.pump(SETTLE_PUMPS);

    NETW_REQUIRE_EQ(int(fresh > 0), 1);
    NETW_CHECK_EQ(int(orphans.has(fresh)), 0);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(alive(table, fresh, at)), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX25 after the server is lost "
    "with the world kept, a survivor spawns as controller from the lease the "
    "old server granted it and every peer holds the body"
) {
    Table table;
    table.keep_world();
    table.rig.declare_world(netw_test::WorldDecl().clocked(TICKRATE, 3));
    const int successor = table.lowest_client();
    const int survivor = (successor + 1) % CLIENTS;
    for (int at = -1; at < CLIENTS; ++at) {
        table.rig.register_constructor(
            table.seat(at),
            StringName("transfer_leased"),
            callable_mp_static(&build_leased),
            string_types()
        );
    }
    table.rig.step_ticks(4);

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);
    const int route = spawn_on(table, survivor, "transfer_leased", "Leased");
    table.rig.step_ticks(SETTLE_TICKS);

    NETW_REQUIRE_EQ(int(route > 0), 1);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(alive(table, route, at)), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX26 after the server is lost "
    "with the world kept, an entity the successor never held is gone from "
    "every survivor"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    const Vitals vitals(table);
    const int shared = vitals.spawn(table, "Shared");
    table.rig.hold(successor);
    const int orphan = vitals.spawn(table, "Orphan");
    for (int at = 0; at < CLIENTS; ++at) {
        if (at != successor) {
            NETW_REQUIRE_EQ(int(alive(table, orphan, at)), 1);
        }
    }

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.release(successor);
    table.rig.pump(SETTLE_PUMPS);

    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(alive(table, orphan, at)), 0);
        NETW_CHECK_EQ(int(alive(table, shared, at)), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX27 a survivor that saw a "
    "move the successor missed keeps receiving that entity's state after the "
    "server is lost with the world kept"
) {
    Table table;
    table.keep_world();
    table.rig.declare_world(netw_test::WorldDecl().clocked(TICKRATE, 3));
    const int successor = table.lowest_client();
    const Vitals vitals(table);
    Node *shelf = table.rig.mirror_child("Shelf");
    const int route = vitals.spawn(table, "Moved");
    NETW_REQUIRE_EQ(int(route > 0), 1);
    table.rig.hold(successor);
    present(table.body_at(route, -1))->reparent(shelf);
    table.rig.pump(SETTLE_PUMPS);
    for (int at = 0; at < CLIENTS; ++at) {
        if (at != successor) {
            NETW_REQUIRE_EQ(
                int(table.seat(at)->liveness_route_anchor(route)
                    > table.seat(successor)->liveness_route_anchor(route)),
                1
            );
        }
    }

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.release(successor);
    table.rig.pump(SETTLE_PUMPS);
    present(table.body_at(route, successor))->set(StringName("hp"), 42);
    table.rig.step_ticks(SETTLE_TICKS);

    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        Node *held = table.body_at(route, at);
        NETW_REQUIRE_EQ(int(held != nullptr), 1);
        NETW_CHECK_EQ(int64_t(held->get(StringName("hp"))), int64_t(42));
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX28 a join that reaches the "
    "successor before every survivor reported waits, and is then admitted "
    "under a player id no peer held"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    table.rig.hold(successor);
    for (int at = 0; at < MISSED_JOINS; ++at) {
        const int missed = table.rig.add_client();
        table.rig.mount_late(missed);
        table.seat(missed)->session_submit_join(StringName("missed"), Array());
    }
    table.rig.pump(SETTLE_PUMPS);
    const int seats = table.rig.count();
    slow_reports(table, successor);

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.release(successor);
    table.rig.pump(SETTLE_PUMPS);
    Vector<int64_t> taken;
    for (int at = 0; at < seats; ++at) {
        const TypedArray<NetwPlayer> seen = table.seat(at)->player_all();
        for (int which = 0; which < seen.size(); ++which) {
            const Ref<NetwPlayer> player = seen[which];
            if (player.is_valid() && !taken.has(player->player_id())) {
                taken.push_back(player->player_id());
            }
        }
    }
    const int late = table.rig.add_client();
    table.rig.mount_late(late);
    table.seat(late)->session_submit_join(StringName("late"), Array());
    table.rig.pump(SETTLE_PUMPS);
    NetwMultiplayer *host = table.seat(successor);
    NETW_CHECK_EQ(int(host->is_structure_halted()), 1);
    NETW_CHECK_EQ(int(table.seat(late)->player_local().is_valid()), 0);

    table.rig.advance(REPORT_LATENCY_MS * 2.0);
    table.rig.pump(SETTLE_PUMPS);

    NETW_CHECK_EQ(int(host->is_structure_halted()), 0);
    const Ref<NetwPlayer> admitted = table.seat(late)->player_local();
    NETW_REQUIRE_EQ(int(admitted.is_valid()), 1);
    NETW_CHECK_EQ(int(taken.has(admitted->player_id())), 0);
    const int64_t joiner = table.seat(late)->get_unique_id();
    const TypedArray<NetwPlayer> issued = host->player_all();
    for (int at = 0; at < issued.size(); ++at) {
        const Ref<NetwPlayer> player = issued[at];
        if (player.is_valid() && player->get_peer_id() == joiner) {
            NETW_CHECK_EQ(int(taken.has(player->player_id())), 0);
        }
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX29 after the server is lost "
    "with 900 entities kept and nobody disagreeing, each survivor sends its "
    "report and receives no spawn, despawn or control change"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    NETW_REQUIRE_EQ(spawn_bodies(table, MANY_ENTITIES), MANY_ENTITIES);
    int64_t verbs[CLIENTS];
    for (int at = 0; at < CLIENTS; ++at) {
        verbs[at] = verbs_in(table.seat(at));
    }
    slow_reports(table, successor);

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);
    table.rig.advance(REPORT_LATENCY_MS * 2.0);
    table.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *host = table.seat(successor);
    NETW_CHECK_EQ(int(host->is_structure_halted()), 0);
    for (int at = 0; at < CLIENTS; ++at) {
        if (at == successor) {
            continue;
        }
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(verbs_in(table.seat(at)) - verbs[at], int64_t(0));
        NETW_CHECK_GT(
            channel_in(host, "bytes_in", table.was(at), "SESSION_RESYNC"),
            int64_t(0)
        );
        NETW_CHECK_EQ(held_by(table, successor, table.was(at)), MANY_ENTITIES);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX30 a survivor's report "
    "larger than one message reaches the successor whole, in frames that "
    "each fit one message"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    NETW_REQUIRE_EQ(spawn_bodies(table, SPLIT_ENTITIES), SPLIT_ENTITIES);

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *host = table.seat(successor);
    const int64_t limit = host->get_max_sync_packet_size();
    for (int at = 0; at < CLIENTS; ++at) {
        if (at == successor) {
            continue;
        }
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        const int64_t peer = table.was(at);
        const int64_t bytes
            = channel_in(host, "bytes_in", peer, "SESSION_RESYNC");
        const int64_t frames
            = channel_in(host, "frames_in_by_channel", peer, "SESSION_RESYNC");
        NETW_CHECK_GT(bytes, limit);
        NETW_CHECK_GE(frames * limit, bytes);
        NETW_CHECK_EQ(held_by(table, successor, peer), SPLIT_ENTITIES);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX31 after the server is lost "
    "with the world kept, a control change only the survivors saw leaves "
    "every peer on the successor's controller under a revision above every "
    "report, published to each survivor once"
) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    const int first = (successor + 1) % CLIENTS;
    const int second = (successor + 2) % CLIENTS;
    const int route = table.spawn_body("Steered", -2, table.was(first));
    table.rig.hold(successor);
    NetwEntity::of(present(table.body_at(route, -1)))
        ->set_controller(table.was(second));
    table.rig.pump(SETTLE_PUMPS);
    const uint64_t missed
        = NetwEntity::of(present(table.body_at(route, successor)))
              ->get_control_revision();
    uint64_t reported = 0;
    int64_t applies[CLIENTS];
    for (int at = 0; at < CLIENTS; ++at) {
        applies[at] = spawn_frames_in(table.seat(at), "CONTROL_APPLY");
        if (at == successor) {
            continue;
        }
        const uint64_t seen = NetwEntity::of(present(table.body_at(route, at)))
                                  ->get_control_revision();
        NETW_REQUIRE_EQ(int(seen > missed), 1);
        reported = MAX(reported, seen);
    }

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.release(successor);
    table.rig.pump(SETTLE_PUMPS);

    const Ref<NetwEntity> served
        = NetwEntity::of(present(table.body_at(route, successor)));
    NETW_CHECK_EQ(int(table.seat(successor)->is_structure_halted()), 0);
    NETW_CHECK_EQ(int(served->get_control_revision() > reported), 1);
    for (int at = 0; at < CLIENTS; ++at) {
        if (at == successor) {
            continue;
        }
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        const Ref<NetwEntity> held
            = NetwEntity::of(present(table.body_at(route, at)));
        NETW_CHECK_EQ(held->get_controller(), served->get_controller());
        NETW_CHECK_EQ(
            held->get_control_revision(),
            served->get_control_revision()
        );
        NETW_CHECK_EQ(
            spawn_frames_in(table.seat(at), "CONTROL_APPLY") - applies[at],
            int64_t(1)
        );
    }
}

void modes_disagree(bool p_server_keeps) {
    Table table;
    const int successor = table.lowest_client();
    for (int at = -1; at < CLIENTS; ++at) {
        const bool keeps = (at == successor) == p_server_keeps;
        table.seat(at)->transfer_set_mode(
            keeps ? NetwSession::TRANSFER_CONTINUE
                  : NetwSession::TRANSFER_REJOIN
        );
    }
    const int shared = table.spawn_body("Shared", -2, 0);
    table.rig.hold(successor);
    const int orphan = table.spawn_body("Orphan", -2, 0);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_REQUIRE_EQ(int(alive(table, orphan, at)), int(at != successor));
    }

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.release(successor);
    table.rig.pump(SETTLE_PUMPS);

    NETW_CHECK_EQ(int(table.seat(successor)->is_structure_halted()), 0);
    PackedInt64Array served
        = table.rig.spawn_plane(successor)->get_spawn_book()->spawned_routes();
    served.sort();
    NETW_CHECK_EQ(int(served.has(shared)), 1);
    NETW_CHECK_EQ(int(served.has(orphan)), 0);
    for (int at = 0; at < CLIENTS; ++at) {
        if (at == successor) {
            continue;
        }
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        PackedInt64Array held
            = table.rig.spawn_plane(at)->get_spawn_book()->recv_routes();
        held.sort();
        NETW_CHECK_EQ(int(held == served), 1);
        for (int which = 0; which < served.size(); ++which) {
            NETW_CHECK_EQ(int(alive(table, int(served[which]), at)), 1);
        }
        NETW_CHECK_EQ(int(alive(table, orphan, at)), 0);
        NETW_CHECK_EQ(int(table.seat(at)->clock_engine().is_held()), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX32 when the successor and "
    "the survivors were set to different transfer modes, the successor still "
    "finishes the move and every survivor holds exactly the world it serves"
) {
    SUBCASE("the successor keeps the world and the survivors rejoin") {
        modes_disagree(true);
    }
    SUBCASE("the successor rejoins and the survivors keep the world") {
        modes_disagree(false);
    }
}

using Tamper = void (*)(LocalVector<PackedByteArray> &, int64_t);

constexpr uint64_t SWELL_ROUTES = 400;
constexpr uint64_t NEARBY_RAISE = 50000;
constexpr uint64_t ABSURD_ID = uint64_t(1) << 34;
constexpr int64_t SANE_ID = 1000;

struct Report {
    netw::session::ResyncHead head;
    LocalVector<netw::session::RouteLease> leases;
    LocalVector<uint64_t> routes;
    LocalVector<netw::session::ResyncRoute> rows;
};

Report report_read(const LocalVector<PackedByteArray> &p_outbox) {
    Report report;
    for (uint32_t at = 0; at < p_outbox.size(); ++at) {
        netw::session::Resync chunk;
        if (!netw::session::resync_read(p_outbox[at], chunk)) {
            continue;
        }
        if (at == 0) {
            report.head = chunk.head;
            for (const netw::session::RouteLease &lease : chunk.leases) {
                report.leases.push_back(lease);
            }
        }
        uint64_t route = 0;
        for (const netw::session::ResyncRoute &row : chunk.routes) {
            route += row.route_delta;
            report.routes.push_back(route);
            report.rows.push_back(row);
        }
    }
    return report;
}

void report_write(
    Report &p_report,
    int64_t p_budget,
    LocalVector<PackedByteArray> &r_outbox
) {
    uint64_t previous = 0;
    for (uint32_t at = 0; at < p_report.rows.size(); ++at) {
        p_report.rows[at].route_delta = p_report.routes[at] - previous;
        previous = p_report.routes[at];
    }
    r_outbox.clear();
    netw::session::resync_chunks(
        p_report.head,
        p_report.leases,
        p_report.rows,
        p_budget,
        r_outbox
    );
}

void withhold_last(LocalVector<PackedByteArray> &r_outbox, int64_t) {
    if (!r_outbox.is_empty()) {
        r_outbox.remove_at(r_outbox.size() - 1);
    }
}

void swell(LocalVector<PackedByteArray> &r_outbox, int64_t p_budget) {
    Report report = report_read(r_outbox);
    uint64_t route = report.routes.is_empty()
        ? 0
        : report.routes[report.routes.size() - 1];
    for (uint64_t at = 0; at < SWELL_ROUTES; ++at) {
        route += 1;
        report.routes.push_back(route);
        report.rows.push_back(netw::session::ResyncRoute());
    }
    report.head.route_floor = route + NEARBY_RAISE;
    report.head.player_high = NEARBY_RAISE;
    report_write(report, p_budget, r_outbox);
}

void lift_floor(LocalVector<PackedByteArray> &r_outbox, int64_t p_budget) {
    Report report = report_read(r_outbox);
    report.head.route_floor = ABSURD_ID;
    report_write(report, p_budget, r_outbox);
}

void lift_players(LocalVector<PackedByteArray> &r_outbox, int64_t p_budget) {
    Report report = report_read(r_outbox);
    report.head.player_high = ABSURD_ID;
    report_write(report, p_budget, r_outbox);
}

bool linked_to(NetwMultiplayer *p_host, int64_t p_peer) {
    return netw::gd::api_peer_ids(p_host->session_get_inner())
        .has(int32_t(p_peer));
}

int join_late(Table &p_table) {
    const int late = p_table.rig.add_client();
    p_table.rig.mount_late(late);
    p_table.seat(late)->session_submit_join(StringName("late"), Array());
    return late;
}

void survivors_resume(
    Table &p_table,
    int p_successor,
    int p_honest,
    int p_late
) {
    const int fresh = spawn_on(p_table, p_successor, BODY_ID, "Fresh");
    p_table.rig.pump(SETTLE_PUMPS);
    NETW_CHECK_GT(fresh, 0);
    NETW_CHECK_LT(int64_t(fresh), SANE_ID);
    NETW_CHECK_EQ(int(alive(p_table, fresh, p_honest)), 1);
    const Ref<NetwPlayer> admitted = p_table.seat(p_late)->player_local();
    NETW_REQUIRE_EQ(int(admitted.is_valid()), 1);
    NETW_CHECK_LT(admitted->player_id(), SANE_ID);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX33 a survivor that never "
    "finishes its report is disconnected once the successor's deadline "
    "passes, with one warning, and the successor admits joins and spawns "
    "again"
) {
    Table table;
    table.keep_world();
    table.rig.declare_world(netw_test::WorldDecl().clocked(TICKRATE, 3));
    const int successor = table.lowest_client();
    const int silent = (successor + 1) % CLIENTS;
    const int honest = (successor + 2) % CLIENTS;
    const int shared = table.spawn_body("Shared", -2, 0);
    table.seat(silent)->resync_tamper = &withhold_last;
    netw_test::FaultWatch faults;

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);
    NetwMultiplayer *host = table.seat(successor);
    const int64_t deadline
        = host->session_get_config()->get_transfer_deadline_ticks();
    NETW_REQUIRE_EQ(int(deadline > SETTLE_TICKS), 1);
    const int late = join_late(table);
    table.rig.step_ticks(int(deadline) - SETTLE_TICKS);

    NETW_CHECK_EQ(int(host->is_structure_halted()), 1);
    NETW_CHECK_EQ(int(table.seat(late)->player_local().is_valid()), 0);
    NETW_CHECK_EQ(int(linked_to(host, table.was(silent))), 1);

    table.rig.step_ticks(2 * SETTLE_TICKS);

    NETW_CHECK_EQ(int(host->is_structure_halted()), 0);
    NETW_CHECK_EQ(int(linked_to(host, table.was(silent))), 0);
    NETW_CHECK_EQ(int(linked_to(host, table.was(honest))), 1);
    NETW_CHECK_EQ(int(alive(table, shared, honest)), 1);
    NETW_CHECK_EQ(faults.warnings_containing("did not finish"), int64_t(1));
    NETW_CHECK_EQ(
        faults.warnings_containing(
            vformat("Peer %d did not finish", table.was(silent))
        ),
        int64_t(1)
    );
    survivors_resume(table, successor, honest, late);
}

void report_refused(Tamper p_tamper) {
    Table table;
    table.keep_world();
    const int successor = table.lowest_client();
    const int liar = (successor + 1) % CLIENTS;
    const int honest = (successor + 2) % CLIENTS;
    const int shared = table.spawn_body("Shared", -2, 0);
    table.seat(liar)->resync_tamper = p_tamper;
    netw_test::FaultWatch faults;

    NETW_REQUIRE_EQ(int(table.rig.session()->lose_server()), int(OK));
    table.rig.pump(SETTLE_PUMPS);
    const int late = join_late(table);
    table.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *host = table.seat(successor);
    NETW_CHECK_EQ(int(host->is_structure_halted()), 0);
    NETW_CHECK_EQ(int(linked_to(host, table.was(liar))), 0);
    NETW_CHECK_EQ(int(linked_to(host, table.was(honest))), 1);
    NETW_CHECK_EQ(int(alive(table, shared, honest)), 1);
    NETW_CHECK_EQ(faults.warnings_containing("world report"), int64_t(1));
    NETW_CHECK_EQ(
        faults.warnings_containing(
            vformat("Peer %d sent a world report", table.was(liar))
        ),
        int64_t(1)
    );
    survivors_resume(table, successor, honest, late);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX34 a report naming far more "
    "routes than the successor holds is refused with one warning, its sender "
    "is disconnected, and the successor's route and player ids do not move"
) {
    report_refused(&swell);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TX35 a report raising route or "
    "player ids far above the successor's is refused with one warning, its "
    "sender is disconnected, and the successor mints sane ids"
) {
    SUBCASE("route floor") {
        report_refused(&lift_floor);
    }
    SUBCASE("player ids") {
        report_refused(&lift_players);
    }
}

} // namespace TestSessionTransferLaws

#endif
