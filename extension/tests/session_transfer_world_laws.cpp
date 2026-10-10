#include "support/loopback_rig.h"
#include "support/netw_cells.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/session.hpp"
#include "netw/entity/stage.hpp"
#include "netw/schema_model.hpp"
#include "support/fault_log.h"
#include "support/minted_script.h"

#include <godot_cpp/classes/node2d.hpp>

namespace TestSessionTransferWorldLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw::NetwSession;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;

constexpr int CLIENTS = 3;
constexpr int SEATS = CLIENTS + 1;
constexpr int SETTLE_PUMPS = 6;
constexpr int TICKRATE = 30;
constexpr int DISPLAY_OFFSET = 3;
constexpr int WARM_TICKS = 12;
constexpr int SETTLE_TICKS = 24;
constexpr int RECONCILE_HOPS = 3;
constexpr int64_t COMMITTED_HP = 15;
constexpr int MANY_SPAWNS = 40;
const char *BODY_ID = "world_body";
const char *RUNNER_ID = "world_runner";
const char *LEVEL_ID = "world_level";
const char *TABLE_ID = "WorldMob";

const char *RUNNER_SOURCE = R"(extends Node2D

var beat := 0
var motion := 0


func _init() -> void:
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	Netw.configure_property(self, &"beat").state()
	Netw.configure_property(self, &"motion").input()


func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	beat += motion
)";

Node *build_body(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Node *build_scripted(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node *made
        = Object::cast_to<Node>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    return made;
}

Node *build_level(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    const Ref<NetwEntity> facet = NetwEntity::ensure(made);
    facet->set_declares_scene(true);
    facet->set_scene_label(StringName(String(p_name)));
    return made;
}

Array named(const String &p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array string_types() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

PackedInt32Array hit_points(int p_first, int p_second) {
    PackedInt32Array out;
    out.push_back(p_first);
    out.push_back(p_second);
    return out;
}

struct Session {
    LoopbackRig rig;
    Vector<int64_t> before;
    Ref<Script> runner;

    explicit Session(NetwSession::TransferMode p_mode) : rig(CLIENTS, true) {
        runner = netw_test::minted_script(RUNNER_SOURCE);
        rig.mount();
        rig.mirror_child("Arena");
        for (int at = -1; at < CLIENTS; ++at) {
            before.push_back(seat(at)->get_unique_id());
            seat(at)->transfer_set_mode(p_mode);
            teach(at);
        }
        rig.declare_world(
            netw_test::WorldDecl().clocked(TICKRATE, DISPLAY_OFFSET)
        );
        rig.join(-1, StringName("host"));
        for (int at = 0; at < CLIENTS; ++at) {
            rig.join(at, StringName(vformat("guest%d", at)));
        }
        rig.pump(SETTLE_PUMPS);
    }

    void teach(int p_at) {
        const Array types = string_types();
        rig.register_constructor(
            seat(p_at),
            StringName(BODY_ID),
            callable_mp_static(&build_body),
            types
        );
        rig.register_constructor(
            seat(p_at),
            StringName(LEVEL_ID),
            callable_mp_static(&build_level),
            types
        );
        rig.register_constructor(
            seat(p_at),
            StringName(RUNNER_ID),
            callable_mp_static(&build_scripted).bind(runner),
            types
        );
    }

    void clock(int p_at) {
        Ref<netw::NetwClockConfig> config;
        config.instantiate();
        config->set("tickrate", TICKRATE);
        config->set("display_offset", DISPLAY_OFFSET);
        seat(p_at)->clock_initialize(config);
        seat(p_at)->clock_engine().set_manual_tick(true);
    }

    NetwMultiplayer *seat(int p_at) const {
        return p_at < 0 ? rig.server() : rig.client(p_at);
    }

    Node *arena_of(int p_at) const {
        return rig.branch(p_at)->get_node_or_null(NodePath("Arena"));
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

    int spawn(
        int p_server,
        const char *p_id,
        const String &p_name,
        Node *p_parent,
        const Ref<NetwPlayer> &p_owner
    ) {
        NetwMultiplayer *server = seat(p_server);
        const RID entity = server->spawn_registered(
            StringName(p_id),
            named(p_name),
            p_owner.ptr()
        );
        Node *node = server->entity_get_node(entity);
        if (node == nullptr || p_parent == nullptr) {
            return 0;
        }
        p_parent->add_child(node);
        return int(server->entity_get_route(entity));
    }

    Node *body_at(int p_route, int p_at) const {
        NetwMultiplayer *api = seat(p_at);
        return api->entity_get_node(api->entity_from_route(p_route));
    }

    bool alive(int p_route, int p_at) const {
        Node *node = body_at(p_route, p_at);
        if (node == nullptr || !node->is_inside_tree()) {
            return false;
        }
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        return entity.is_valid()
            && entity->get_stage() < int64_t(netw::entity::Stage::DESPAWNING);
    }

    int64_t beat(int p_route, int p_at) const {
        Node *node = body_at(p_route, p_at);
        return node != nullptr ? int64_t(node->get(StringName("beat")))
                               : int64_t(-1);
    }

    int64_t corrections(int p_route, int p_at) const {
        Node *node = body_at(p_route, p_at);
        if (node == nullptr) {
            return -1;
        }
        NetwMultiplayer *api = seat(p_at);
        const Ref<netw::NetwPredictStats> stats
            = api->predict_get_stats(api->entity_of(node));
        return stats.is_valid() ? stats->get_corrections() : int64_t(-1);
    }
};

void ticks_after_loss_keep_bodies(NetwSession::TransferMode p_mode) {
    Session session(p_mode);
    const int owned = session.spawn(
        -1,
        BODY_ID,
        "Owned",
        session.arena_of(-1),
        session.rig.server()->peer_get_player(session.was(0))
    );
    const int shared
        = session.spawn(-1, BODY_ID, "Shared", session.arena_of(-1), {});
    session.rig.pump(SETTLE_PUMPS);
    NETW_REQUIRE_EQ(int(owned > 0 && shared > 0), 1);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_REQUIRE_EQ(int(session.alive(owned, at)), 1);
        NETW_REQUIRE_EQ(int(session.alive(shared, at)), 1);
    }

    NETW_REQUIRE_EQ(int(session.rig.session()->lose_server()), int(OK));
    session.rig.step_ticks(SETTLE_TICKS);

    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(session.alive(owned, at)), 1);
        NETW_CHECK_EQ(int(session.alive(shared, at)), 1);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TW1 a session that only ticks "
    "after the server is lost keeps every server-spawned body on every "
    "client, rejoining"
) {
    ticks_after_loss_keep_bodies(NetwSession::TRANSFER_REJOIN);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TW1 a session that only ticks "
    "after the server is lost keeps every server-spawned body on every "
    "client, continuing"
) {
    ticks_after_loss_keep_bodies(NetwSession::TRANSFER_CONTINUE);
}

void lost_body_leaves_nothing_waiting(NetwSession::TransferMode p_mode) {
    Session session(p_mode);
    NetwMultiplayer *server = session.rig.server();
    const int plaza
        = session.spawn(-1, LEVEL_ID, "Plaza", session.arena_of(-1), {});
    session.rig.pump(SETTLE_PUMPS);
    const int lost = session.rig.spawn_registered(
        StringName("lost_body"),
        callable_mp_static(&build_body),
        named("Lost"),
        string_types(),
        session.body_at(plaza, -1),
        server->peer_get_player(session.was(-1)),
        true
    );
    const RID plaza_scene = server->entity_from_route(plaza);
    bool admitted = true;
    for (int at = 0; at < CLIENTS; ++at) {
        admitted = server->scene_admit_peer(plaza_scene, session.was(at))
            && admitted;
    }
    session.rig.pump(SETTLE_PUMPS);
    NETW_REQUIRE_EQ(int(plaza > 0 && lost > 0 && admitted), 1);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_REQUIRE_EQ(int(session.alive(lost, at)), 1);
    }
    const int successor = session.lowest_client();
    const netw_test::FaultWatch faults;
    session.rig.hold(successor);
    NETW_REQUIRE_EQ(int(session.rig.session()->lose_server()), int(OK));
    session.rig.step_ticks(DISPLAY_OFFSET);
    Node *doomed = session.body_at(lost, successor);
    if (doomed != nullptr) {
        memdelete(doomed);
    }
    session.rig.release(successor);
    session.rig.step_ticks(TICKRATE * 3);
    NETW_CHECK_EQ(faults.warnings_containing("never became live"), 0);
    for (int at = 0; at < CLIENTS; ++at) {
        NETW_FORMAT_INT(seat_text, at);
        CAPTURE(seat_text);
        NETW_CHECK_EQ(int(session.alive(lost, at)), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TW3 a body the lost server's "
    "player owned leaves no client waiting on it, rejoining"
) {
    lost_body_leaves_nothing_waiting(NetwSession::TRANSFER_REJOIN);
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TW3 a body the lost server's "
    "player owned leaves no client waiting on it, continuing"
) {
    lost_body_leaves_nothing_waiting(NetwSession::TRANSFER_CONTINUE);
}

enum Exit {
    EXIT_LOSS,
    EXIT_GRACEFUL,
};

struct TransferScenario {
    String label;
    Exit exit = EXIT_LOSS;
    NetwSession::TransferMode mode = NetwSession::TRANSFER_REJOIN;
};

struct Seen {
    bool survives = false;
    bool shared = false;
    bool fresh = false;
    bool runner = false;
    bool loot = false;
    bool vault = false;
    bool crate = false;
    bool plaza = false;
    int table_rows = -1;
    int64_t table_hp = -1;
    int64_t viewers = -1;
};

struct Evidence {
    bool stood = false;
    char unstood[128] = {0};
    int holder = -1;
    int64_t lead_before = 0;
    int readmitted = -1;
    bool continues = false;
    int64_t backlog = -1;
    int consume_per_tick = 0;
    int64_t leads[SETTLE_TICKS] = {0};
    int64_t corrections_before = -1;
    int64_t corrections_after = -1;
    Seen seats[SEATS];
    Seen late;
    int64_t successor_viewers = -1;
    int minted = -1;
    int repeated = -1;
    int held_elsewhere = -1;
    int64_t unwritten = -1;
    int64_t sends_refused = -1;
};

enum Viewer {
    VIEWER_SUCCESSOR = 1,
    VIEWER_HOLDER = 2,
    VIEWER_THIRD = 4,
    VIEWER_FORMER = 8,
};

class WorldRun {
    Session session;
    Evidence evidence;
    int successor = 0;
    int holder = 0;
    int third = 0;
    int shared = 0;
    int runner = 0;
    int plaza = 0;
    int vault = 0;
    int crate = 0;
    int loot = 0;
    int fresh = 0;
    int64_t former_now = 0;
    ObjectID steered_before;
    PackedInt64Array rows;

    void unstood(const char *p_why) {
        if (evidence.unstood[0] == '\0') {
            snprintf(evidence.unstood, sizeof(evidence.unstood), "%s", p_why);
        }
    }

    int64_t label_now(int p_at) const {
        if (p_at == successor) {
            return 1;
        }
        return p_at < 0 ? former_now : session.was(p_at);
    }

    int64_t viewers_on(int p_at) const {
        NetwMultiplayer *api = session.seat(p_at);
        const RID scene = api->entity_from_route(plaza);
        if (!scene.is_valid()) {
            return -1;
        }
        int64_t mask = 0;
        mask |= api->scene_watches(scene, 1) ? VIEWER_SUCCESSOR : 0;
        mask
            |= api->scene_watches(scene, label_now(holder)) ? VIEWER_HOLDER : 0;
        mask |= api->scene_watches(scene, label_now(third)) ? VIEWER_THIRD : 0;
        if (former_now != 0) {
            mask |= api->scene_watches(scene, former_now) ? VIEWER_FORMER : 0;
        }
        return mask;
    }

    void steer_and_step() {
        Node *steered = session.body_at(runner, holder);
        if (steered != nullptr) {
            steered->set(StringName("motion"), 1);
        }
        session.rig.step_ticks(1);
    }

    int64_t lead(int p_authority) const {
        return session.beat(runner, holder) - session.beat(runner, p_authority);
    }

    ObjectID steered_node() const {
        Node *node = session.body_at(runner, holder);
        return node != nullptr ? ObjectID(node->get_instance_id()) : ObjectID();
    }

    Seen seen_at(int p_at) const {
        Seen seen;
        seen.shared = session.alive(shared, p_at);
        seen.fresh = fresh > 0 && session.alive(fresh, p_at);
        seen.runner = session.alive(runner, p_at);
        seen.loot = session.alive(loot, p_at);
        seen.vault = session.alive(vault, p_at);
        seen.crate = session.alive(crate, p_at);
        seen.plaza = session.alive(plaza, p_at);
        NetwMultiplayer *api = session.seat(p_at);
        const RID table = api->table_find(StringName(TABLE_ID));
        if (table.is_valid()) {
            seen.table_rows = api->table_read_routes(table).size();
            const PackedInt32Array hps = api->table_read_column(table, 0);
            int64_t sum = 0;
            for (int at = 0; at < hps.size(); ++at) {
                sum += hps[at];
            }
            seen.table_hp = sum;
        }
        seen.viewers = viewers_on(p_at);
        return seen;
    }

    void stand_world() {
        NetwMultiplayer *server = session.seat(-1);
        Node *arena = session.arena_of(-1);
        successor = session.lowest_client();
        holder = (successor + 1) % CLIENTS;
        third = (successor + 2) % CLIENTS;
        evidence.holder = holder;
        shared = session.spawn(-1, BODY_ID, "Shared", arena, {});
        runner = session.spawn(
            -1,
            RUNNER_ID,
            "Runner",
            arena,
            server->peer_get_player(session.was(holder))
        );
        plaza = session.spawn(-1, LEVEL_ID, "Plaza", arena, {});
        vault = session.spawn(-1, LEVEL_ID, "Vault", arena, {});
        session.rig.pump(SETTLE_PUMPS);
        crate
            = session
                  .spawn(-1, BODY_ID, "Crate", session.body_at(plaza, -1), {});
        loot = session
                   .spawn(-1, BODY_ID, "Loot", session.body_at(vault, -1), {});
        const RID plaza_scene = server->entity_from_route(plaza);
        const RID vault_scene = server->entity_from_route(vault);
        const bool admitted
            = server->scene_admit_peer(plaza_scene, session.was(successor))
            && server->scene_admit_peer(plaza_scene, session.was(holder))
            && server->scene_admit_peer(vault_scene, session.was(holder));
        if (!admitted) {
            unstood("a viewer was not admitted");
        }
        const RID table = server->table_find_or_adopt(StringName(TABLE_ID));
        rows = server->liveness_claim_routes(2);
        server->table_write_routes(table, rows);
        server->table_write_column(table, 0, hit_points(5, 6));
        if (server->table_commit(table) != OK) {
            unstood("the table did not commit");
        }
        session.rig.pump(SETTLE_PUMPS);
        for (int tick = 0; tick < WARM_TICKS; ++tick) {
            steer_and_step();
        }
        evidence.lead_before = lead(-1);
        evidence.corrections_before = session.corrections(runner, holder);
        steered_before = steered_node();
    }

    void check_world() {
        if (shared <= 0 || runner <= 0 || crate <= 0 || loot <= 0) {
            unstood("a spawn reached no node");
            return;
        }
        for (int at = 0; at < CLIENTS; ++at) {
            const Seen seen = seen_at(at);
            if (!seen.shared || !seen.runner || seen.table_rows != 2) {
                unstood("a client misses the shared world");
            }
            if (seen.crate != (at != third)) {
                unstood("the plaza's crate is not where its viewers are");
            }
            if (seen.loot != (at == holder)) {
                unstood("the vault's loot is not with its one viewer");
            }
        }
    }

    void hand_on(Exit p_exit) {
        if (p_exit == EXIT_LOSS) {
            if (session.rig.session()->lose_server() != OK) {
                unstood("the loopback did not lose its server");
            }
            return;
        }
        const Error handed = session.seat(-1)->get_session()->transfer_server(
            session.was(successor)
        );
        if (handed != OK) {
            unstood("transfer_server refused");
        }
    }

    void note_backlog() {
        Node *node = session.body_at(runner, successor);
        if (node == nullptr) {
            return;
        }
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        if (entity.is_null() || entity->get_prediction().is_null()) {
            return;
        }
        netw::NetwPredictionEngine *pool
            = session.seat(successor)->get_prediction_engine();
        const int64_t slot = pool->slot_of(entity);
        const int64_t cursor = pool->next_input_tick_of(slot);
        if (cursor < 0) {
            return;
        }
        evidence.backlog = pool->newest_authored_input(slot) - cursor;
        evidence.consume_per_tick
            = entity->get_prediction()->get_max_consume_per_tick();
    }

    void settle(Exit p_exit) {
        for (int tick = 0; tick < SETTLE_TICKS; ++tick) {
            steer_and_step();
            const bool held = session.seat(holder)->clock_engine().is_held();
            if (evidence.readmitted < 0 && !held
                && session.body_at(runner, holder) != nullptr) {
                evidence.readmitted = tick;
            }
            evidence.leads[tick] = lead(successor);
            if (evidence.backlog < 0) {
                note_backlog();
            }
        }
        evidence.corrections_after = session.corrections(runner, holder);
        if (steered_node() != steered_before) {
            evidence.corrections_before = 0;
        }
        if (p_exit == EXIT_GRACEFUL) {
            former_now = session.seat(-1)->get_unique_id();
        }
    }

    void play_on() {
        NetwMultiplayer *host = session.seat(successor);
        const RID table = host->table_find_or_adopt(StringName(TABLE_ID));
        host->table_write_routes(table, rows);
        host->table_write_column(table, 0, hit_points(7, 8));
        host->table_commit(table);
        fresh = session.spawn(
            successor,
            BODY_ID,
            "Fresh",
            session.arena_of(successor),
            {}
        );
        for (int tick = 0; tick < SETTLE_TICKS; ++tick) {
            steer_and_step();
        }
    }

    void judge_survivors(Exit p_exit) {
        for (int at = -1; at < CLIENTS; ++at) {
            Seen seen = seen_at(at);
            seen.survives = at >= 0 ? at != successor : p_exit == EXIT_GRACEFUL;
            evidence.seats[at + 1] = seen;
        }
        evidence.successor_viewers = viewers_on(successor);
    }

    bool held_by_survivor(int p_route) const {
        for (int at = -1; at < CLIENTS; ++at) {
            if (evidence.seats[at + 1].survives
                && session.body_at(p_route, at) != nullptr) {
                return true;
            }
        }
        return false;
    }

    void spawn_many() {
        const netw_test::FaultWatch faults;
        Vector<int> routes;
        evidence.repeated = 0;
        evidence.held_elsewhere = 0;
        for (int at = 0; at < MANY_SPAWNS; ++at) {
            const int route = session.spawn(
                successor,
                BODY_ID,
                vformat("Many%d", at),
                session.arena_of(successor),
                {}
            );
            if (route <= 0) {
                continue;
            }
            evidence.repeated += routes.has(route) ? 1 : 0;
            evidence.held_elsewhere += held_by_survivor(route) ? 1 : 0;
            routes.push_back(route);
        }
        for (int tick = 0; tick < SETTLE_TICKS; ++tick) {
            steer_and_step();
        }
        evidence.minted = routes.size();
        evidence.unwritten = faults.errors_containing("could not be written");
    }

    void join_late() {
        const int late = session.rig.add_client();
        session.rig.hold(late);
        session.rig.mount_late(late);
        session.rig.mirror_late(late, "Arena");
        session.teach(late);
        session.clock(late);
        session.rig.release(late);
        session.seat(late)->session_submit_join(StringName("late"), Array());
        for (int tick = 0; tick < SETTLE_TICKS; ++tick) {
            steer_and_step();
        }
        evidence.late = seen_at(late);
        evidence.late.survives = true;
    }

public:
    explicit WorldRun(const TransferScenario &p_scenario)
        : session(p_scenario.mode) {
        evidence.continues = p_scenario.mode == NetwSession::TRANSFER_CONTINUE;
        stand_world();
        check_world();
        {
            const netw_test::FaultWatch faults;
            hand_on(p_scenario.exit);
            settle(p_scenario.exit);
            evidence.sends_refused = faults.errors_containing(
                "via a multiplayer peer which is not connected"
            );
        }
        play_on();
        judge_survivors(p_scenario.exit);
        spawn_many();
        join_late();
        evidence.stood = evidence.unstood[0] == '\0';
    }

    const Evidence &read() const {
        return evidence;
    }
};

typedef LawRowFor<Evidence> WorldLaw;

LawVerdict law_spawns(const Evidence &p_run) {
    for (int at = 0; at < SEATS; ++at) {
        const Seen &seen = p_run.seats[at];
        if (seen.survives && !seen.fresh) {
            return law_broken(
                "seat %d misses the body the new server spawned",
                at - 1
            );
        }
    }
    return law_held();
}

LawVerdict law_mints(const Evidence &p_run) {
    if (p_run.minted != MANY_SPAWNS || p_run.repeated != 0
        || p_run.held_elsewhere != 0 || p_run.unwritten != 0) {
        return law_broken(
            "the new server minted %d of %d routes, %d repeated, %d held by "
            "a survivor, %d spawns unwritten",
            p_run.minted,
            MANY_SPAWNS,
            p_run.repeated,
            p_run.held_elsewhere,
            int(p_run.unwritten)
        );
    }
    return law_held();
}

LawVerdict law_tables(const Evidence &p_run) {
    for (int at = 0; at < SEATS; ++at) {
        const Seen &seen = p_run.seats[at];
        if (seen.survives
            && (seen.table_rows != 2 || seen.table_hp != COMMITTED_HP)) {
            return law_broken(
                "seat %d holds %d table rows summing %d after the new "
                "server committed 2 rows summing %d",
                at - 1,
                seen.table_rows,
                int(seen.table_hp),
                int(COMMITTED_HP)
            );
        }
    }
    const Seen &late = p_run.late;
    if (late.table_rows != 2 || late.table_hp != COMMITTED_HP) {
        return law_broken(
            "the late joiner holds %d table rows summing %d",
            late.table_rows,
            int(late.table_hp)
        );
    }
    return law_held();
}

LawVerdict law_viewers(const Evidence &p_run) {
    const int64_t listed = p_run.successor_viewers;
    if (listed != (VIEWER_SUCCESSOR | VIEWER_HOLDER)) {
        return law_broken(
            "the new server lists plaza viewers %d where the old one "
            "admitted the successor and the holder",
            int(listed)
        );
    }
    for (int at = 0; at < SEATS; ++at) {
        const Seen &seen = p_run.seats[at];
        if (!seen.survives) {
            continue;
        }
        const bool views = at - 1 == p_run.holder;
        if (seen.plaza != views || seen.crate != views) {
            return law_broken(
                "seat %d holds plaza %d crate %d where it views %d",
                at - 1,
                int(seen.plaza),
                int(seen.crate),
                int(views)
            );
        }
        if (views && seen.viewers != listed) {
            return law_broken(
                "seat %d reads plaza viewers %d where the new server lists %d",
                at - 1,
                int(seen.viewers),
                int(listed)
            );
        }
    }
    const Seen &late = p_run.late;
    if (late.plaza || late.crate) {
        return law_broken(
            "the late joiner holds plaza %d crate %d without viewing it",
            int(late.plaza),
            int(late.crate)
        );
    }
    return law_held();
}

LawVerdict law_missed(const Evidence &p_run) {
    for (int at = 0; at < SEATS; ++at) {
        const Seen &seen = p_run.seats[at];
        if (seen.survives && (seen.vault || seen.loot)) {
            return law_broken(
                "seat %d holds vault %d loot %d, which the new server never "
                "held",
                at - 1,
                int(seen.vault),
                int(seen.loot)
            );
        }
    }
    return law_held();
}

LawVerdict law_reconciles(const Evidence &p_run) {
    if (p_run.readmitted < 0) {
        return law_broken("the controller was never readmitted");
    }
    int hops = RECONCILE_HOPS;
    if (p_run.continues) {
        if (p_run.backlog < 0 || p_run.consume_per_tick < 2) {
            return law_broken(
                "the new server's input backlog was not measured, or it may "
                "run only %d input per tick",
                p_run.consume_per_tick
            );
        }
        const int64_t gain = p_run.consume_per_tick - 1;
        hops = 1 + int((p_run.backlog + gain - 1) / gain);
    }
    const int from = p_run.readmitted + hops;
    if (from >= SETTLE_TICKS) {
        return law_broken(
            "the controller may take %d ticks to reach its lead, past the %d "
            "ticks this run settles",
            hops,
            SETTLE_TICKS
        );
    }
    for (int tick = from; tick < SETTLE_TICKS; ++tick) {
        if (p_run.leads[tick] != p_run.lead_before) {
            return law_broken(
                "%d ticks after readmission the controller leads by %d "
                "where it led by %d before the transfer",
                tick - p_run.readmitted,
                int(p_run.leads[tick]),
                int(p_run.lead_before)
            );
        }
    }
    if (p_run.corrections_after - p_run.corrections_before > 1) {
        return law_broken(
            "the controller corrected %d times across the transfer",
            int(p_run.corrections_after - p_run.corrections_before)
        );
    }
    return law_held();
}

LawVerdict law_closes_quietly(const Evidence &p_run) {
    if (p_run.sends_refused != 0) {
        return law_broken(
            "the engine refused %d sends on a link that had closed",
            int(p_run.sends_refused)
        );
    }
    return law_held();
}

const WorldLaw L_SPAWNS = {
    "spawns",
    "a body the new server spawns reaches every survivor",
    &law_spawns,
};

const WorldLaw L_MINTS = {
    "mints",
    "a new server spawns 40 bodies on distinct routes no survivor holds",
    &law_mints,
};

const WorldLaw L_TABLES = {
    "tables",
    "a table the new server commits reaches every survivor and a joiner",
    &law_tables,
};

const WorldLaw L_VIEWERS = {
    "viewers",
    "the new server keeps a scene's viewers and shows the scene to them only",
    &law_viewers,
};

const WorldLaw L_MISSED = {
    "missed",
    "a scene the new server never held is gone from every survivor",
    &law_missed,
};

const WorldLaw L_RECONCILES = {
    "reconciles",
    "a controller's prediction runs at its old lead within three ticks of "
    "readmission, or when continuing within one tick plus the new server's "
    "catch-up over its input backlog, after one correction at most",
    &law_reconciles,
};

const WorldLaw L_CLOSES = {
    "closes",
    "a server that hands on sends nothing over its link once the link has "
    "closed, so the engine refuses no send",
    &law_closes_quietly,
};

const WorldLaw *const EVERY_LAW[] = {
    &L_CLOSES,
    &L_SPAWNS,
    &L_MINTS,
    &L_TABLES,
    &L_VIEWERS,
    &L_MISSED,
    &L_RECONCILES,
};

template <int N> constexpr int count_of(const WorldLaw *const (&)[N]) {
    return N;
}

struct MatrixRow {
    const char *label;
    Exit exit;
    NetwSession::TransferMode mode;
    const WorldLaw *const *laws;
    int count;
};

const MatrixRow MATRIX[] = {
    {"loss x rejoin",
     EXIT_LOSS,
     NetwSession::TRANSFER_REJOIN,
     EVERY_LAW,
     count_of(EVERY_LAW)},
    {"graceful x rejoin",
     EXIT_GRACEFUL,
     NetwSession::TRANSFER_REJOIN,
     EVERY_LAW,
     count_of(EVERY_LAW)},
    {"loss x continue",
     EXIT_LOSS,
     NetwSession::TRANSFER_CONTINUE,
     EVERY_LAW,
     count_of(EVERY_LAW)},
    {"graceful x continue",
     EXIT_GRACEFUL,
     NetwSession::TRANSFER_CONTINUE,
     EVERY_LAW,
     count_of(EVERY_LAW)},
};

void judge(const MatrixRow &p_row) {
    TransferScenario scenario;
    scenario.label = p_row.label;
    scenario.exit = p_row.exit;
    scenario.mode = p_row.mode;
    netw::schema_model::clear();
    const Ref<netw::NetwSchema> schema
        = netw::NetwSchema::declare(StringName(TABLE_ID));
    schema->i32("hp", 1);
    {
        const WorldRun run(scenario);
        const Evidence &evidence = run.read();
        NETW_FORMAT_TEXT(row_text, p_row.label);
        CAPTURE(row_text);
        NETW_FORMAT_TEXT(unstood_text, evidence.unstood);
        CAPTURE(unstood_text);
        NETW_CHECK_EQ(int(evidence.stood), 1);
        for (int at = 0; evidence.stood && at < p_row.count; ++at) {
            const WorldLaw &law = *p_row.laws[at];
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, evidence);
        }
    }
    netw::schema_model::clear();
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] TW2 a server and three clients "
    "with a filtered, predicted, tabled and viewed world carry on after the "
    "server leaves, by loss or by hand, in each transfer mode"
) {
    for (const MatrixRow &row : MATRIX) {
        judge(row);
    }
}

} // namespace TestSessionTransferWorldLaws

#endif
