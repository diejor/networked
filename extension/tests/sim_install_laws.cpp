#include "support/link.h"
#include "support/netw_test.h"

#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include <string>

#include "support/netw_recorder.h"
#include "support/value_flow_stand.h"

#include "godot/physics_body.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction.hpp"
#include "netw/display/timing.hpp"
#include "netw/predict/arrival.hpp"
#include "netw/predict/engine.hpp"
#include "netw/prediction_core.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/sim/install.hpp"
#include "netw/sim/row.hpp"
#include "netw/store/world.hpp"

namespace TestNetwSimInstallLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::repl::PropertyBindingPtr;
using netw::repl::PropertySet;
using netw::sim::InstallStats;
using netw::sim::Mode;
using netw::sim::Restore;
using netw_test::FlowPair;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int DISPLAY_OFFSET = 3;
constexpr int BEAT_TICKS = 10;
constexpr double FRAME_DELTA = 1.0 / 60.0;

constexpr const char *FIELD = "body_position";
constexpr const char *DRAWN = "display_position";

constexpr const char *BODY = R"(extends Node2D

var body: RigidBody2D
var display_position: Vector2
var body_velocity: Vector2
var writes := 0

var body_position: Vector2:
	get:
		return body.position
	set(value):
		writes += 1
		body.position = value

func _init() -> void:
	body = RigidBody2D.new()
	body.name = &"Body"
	body.gravity_scale = 0.0
	body.collision_layer = 0
	body.collision_mask = 0
	add_child(body)
)";

const std::string STATE_SOURCE = std::string(BODY)
    + "\tNetw.configure_property(self, &\"body_position\").state()"
      ".interpolate(NetwInterpolate.new().lerp().to(&\"display_position\"))\n";

const std::string BROADCAST_SOURCE = std::string(BODY)
    + "\tNetw.configure_property(self, &\"body_position\").broadcast()\n";

const std::string CARRY_SOURCE = std::string(BODY)
    + "\tNetw.configure_property(self, &\"body_position\").state()"
      ".carry_along(&\"body_velocity\")\n"
      "\tNetw.configure_property(self, &\"body_velocity\").state()\n";

const std::string HEARTBEAT_SOURCE = std::string(BODY)
    + "\tNetw.configure_property(self, &\"body_position\").broadcast()"
      ".heartbeat(10)\n";

struct Stand {
    LoopbackRig rig;
    FlowPair pair;

    Stand(int p_clients, const std::string &p_source) : rig(p_clients) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE, DISPLAY_OFFSET);
        pair = netw_test::stand_flow_pair(rig, p_source.c_str(), "InstallBody");
    }

    Node2D *copy(int p_client) const {
        return p_client < 0 ? pair.authored : pair.mirror(p_client);
    }

    NetwMultiplayer *core(int p_client) {
        return netw_test::flow_core(
            p_client < 0 ? rig.server() : rig.client(p_client)
        );
    }

    RID rid(int p_client) const {
        return NetwEntity::of(copy(p_client))->get_rid_handle();
    }

    RigidBody2D *body(int p_client) const {
        return Object::cast_to<RigidBody2D>(
            copy(p_client)->get_node_or_null(NodePath("Body"))
        );
    }

    Vector2 held(int p_client) const {
        return copy(p_client)->get(StringName(FIELD));
    }

    int64_t writes(int p_client) const {
        return copy(p_client)->get(StringName("writes"));
    }

    void activate(int p_client, Restore p_restore) {
        NetwMultiplayer *session = core(p_client);
        netw::sim::Row &row = session->sim_row(rid(p_client));
        row.declaration.bodies.push_back(NodePath("Body"));
        row.declaration.replicas = netw::sim::Replicas::ACTIVE;
        row.declaration.restore = p_restore;
        session->sim_settle_body(NetwEntity::of(copy(p_client)));
    }

    void pump_display(int p_client, int p_frames) {
        NetwMultiplayer *session = core(p_client);
        for (int at = 0; at < p_frames; ++at) {
            session->display_pump_entity(
                rid(p_client),
                netw::display::capture_timing(
                    &session->clock_engine(),
                    FRAME_DELTA
                )
            );
        }
    }

    const netw::sim::Row *row(int p_client) {
        return core(p_client)->sim_row_of(rid(p_client));
    }

    InstallStats stats(int p_client) {
        const netw::sim::Row *seen = row(p_client);
        return seen != nullptr ? seen->installs.stats : InstallStats();
    }

    int pending(int p_client) {
        NetwMultiplayer *session = core(p_client);
        const int64_t route
            = session->liveness_route_of(NetwEntity::of(copy(p_client)).ptr());
        netw::store::World &world = session->store();
        int held = 0;
        for (uint32_t at = 0; at < world.table_count(); ++at) {
            const netw::store::Table &table = world.table(at);
            for (uint32_t index = 0; index < table.row_count(); ++index) {
                held += table.routes[index] == route
                    ? int(table.due_held[index])
                    : 0;
            }
        }
        return held;
    }

    int64_t author(int p_author, const Vector2 &p_value) {
        const int64_t tick = netw_test::clock_tick(rig, p_author) + 1;
        const PropertySet::Record record
            = NetwEntity::of(copy(p_author))->get_state_binding() != nullptr
            ? PropertySet::RECORD_STATE
            : PropertySet::RECORD_BROADCAST;
        netw_test::author_at(
            copy(p_author),
            StringName(FIELD),
            p_value,
            record,
            tick
        );
        return tick;
    }

    void author_ticks(int p_author, int p_ticks, double p_step) {
        for (int at = 0; at < p_ticks; ++at) {
            const int64_t tick = netw_test::clock_tick(rig, p_author) + 1;
            author(p_author, Vector2(p_step * double(tick), 0.0));
            rig.step_ticks(1);
        }
    }
};

TEST_CASE(
    "[Networked][Sim][SceneTree] a state body with replicas active runs "
    "unfrozen on a client that registered no prediction, and installs the "
    "rows the session authors"
) {
    Stand stand(1, STATE_SOURCE);
    stand.activate(0, Restore::BUFFERED);
    stand.author_ticks(-1, 12, 10.0);

    const netw::sim::Row *row = stand.row(0);
    REQUIRE(bool(row != nullptr));
    if (row == nullptr) {
        return;
    }
    NETW_CHECK_EQ(int(row->mode), int(Mode::ACTIVE));
    NETW_CHECK_EQ(int(row->bodies.applied), int(Mode::ACTIVE));
    CHECK_FALSE(stand.body(0)->is_freeze_enabled());
    CHECK(bool(stand.core(0)->predict_engine_for(stand.rid(0)) == nullptr));
    NETW_CHECK_GT(row->installs.stats.installed, int64_t(0));
    const double newest = stand.held(-1).x;
    const double installed = stand.held(0).x;
    NETW_CHECK_GT(installed, 0.0);
    NETW_CHECK_LT(installed, newest);
}

InstallStats jittered_ages(Restore p_restore) {
    Stand stand(1, STATE_SOURCE);
    stand.activate(0, p_restore);
    const Ref<netw::NetwLinkConditions> link = netw_test::link(29);
    link->set_latency_ms(10.0);
    link->set_jitter_ms(25.0);
    stand.rig.conditions(0, link, 1);
    stand.author_ticks(-1, 48, 10.0);
    return stand.stats(0);
}

TEST_CASE(
    "[Networked][Sim][SceneTree] under link jitter a buffered copy installs "
    "every row at the same age and an exact copy does not"
) {
    const InstallStats buffered = jittered_ages(Restore::BUFFERED);
    NETW_CHECK_GT(buffered.installed, int64_t(30));
    NETW_CHECK_EQ(buffered.youngest_age, int64_t(DISPLAY_OFFSET));
    NETW_CHECK_EQ(buffered.oldest_age, int64_t(DISPLAY_OFFSET));

    const InstallStats exact = jittered_ages(Restore::EXACT);
    NETW_CHECK_GT(exact.installed, int64_t(30));
    NETW_CHECK_LT(exact.youngest_age, exact.oldest_age);
}

TEST_CASE(
    "[Networked][Sim][SceneTree] an install hands a jump inside the offset "
    "bound to the drawn value, so the visual stays where it was while the "
    "body takes the row as authored"
) {
    Stand stand(1, STATE_SOURCE);
    stand.activate(0, Restore::BUFFERED);
    const Vector2 rest(40.0, 0.0);
    const Vector2 jump(41.8, 0.0);
    for (int at = 0; at < 12; ++at) {
        stand.author(-1, rest);
        stand.rig.step_ticks(1);
    }
    REQUIRE(stand.held(0).is_equal_approx(rest));
    stand.pump_display(0, 120);
    const Vector2 drawn_before = stand.copy(0)->get(StringName(DRAWN));
    CHECK(drawn_before.is_equal_approx(rest));

    stand.author(-1, jump);
    int64_t installed = stand.stats(0).installed;
    Vector2 body_before;
    for (int at = 0; at < 8 && !stand.held(0).is_equal_approx(jump); ++at) {
        body_before = stand.held(0);
        installed = stand.stats(0).installed;
        stand.rig.step_ticks(1);
    }
    NETW_CHECK_GT(stand.stats(0).installed, installed);
    CHECK(body_before.is_equal_approx(rest));
    CHECK(stand.held(0).is_equal_approx(jump));

    stand.pump_display(0, 1);
    const Vector2 drawn_after = stand.copy(0)->get(StringName(DRAWN));
    NETW_CHECK_LT(drawn_after.distance_to(drawn_before), 0.25);
    CHECK(stand.held(0).is_equal_approx(jump));
}

TEST_CASE(
    "[Networked][Sim][SceneTree] a held row whose sender no longer authors "
    "the entity is dropped at install"
) {
    Stand stand(2, BROADCAST_SOURCE);
    netw_test::steer(stand.pair, stand.rig.peer_id(0));
    stand.activate(1, Restore::BUFFERED);
    stand.author_ticks(0, 10, 10.0);
    REQUIRE(bool(stand.row(1) != nullptr));
    if (stand.row(1) == nullptr) {
        return;
    }
    NETW_CHECK_EQ(int(stand.row(1)->mode), int(Mode::ACTIVE));
    NETW_CHECK_GT(stand.pending(1), 0);
    const int64_t dropped = stand.stats(1).dropped;
    const Vector2 before = stand.held(1);
    const Vector2 newest = stand.held(0);
    REQUIRE_FALSE(before.is_equal_approx(newest));

    netw_test::steer(stand.pair, stand.rig.peer_id(-1));
    stand.rig.hold(1);
    stand.rig.step_ticks(DISPLAY_OFFSET + 2);

    NETW_CHECK_GT(stand.stats(1).dropped, dropped);
    NETW_CHECK_EQ(stand.pending(1), 0);
    CHECK(stand.held(1).is_equal_approx(before));
    stand.rig.release(1);
}

TEST_CASE(
    "[Networked][Sim][SceneTree] a held row from its author's earlier tenure "
    "is dropped at install even when that author controls the entity again"
) {
    Stand stand(2, BROADCAST_SOURCE);
    netw_test::steer(stand.pair, stand.rig.peer_id(0));
    stand.activate(1, Restore::BUFFERED);
    stand.author_ticks(0, 10, 10.0);
    REQUIRE(bool(stand.row(1) != nullptr));
    if (stand.row(1) == nullptr) {
        return;
    }
    NETW_CHECK_GT(stand.pending(1), 0);
    const int64_t dropped = stand.stats(1).dropped;

    const Ref<NetwEntity> host = NetwEntity::of(stand.copy(-1));
    host->set_controller(stand.rig.peer_id(1));
    host->set_controller(stand.rig.peer_id(0));
    stand.rig.pump(4);
    REQUIRE(
        NetwEntity::of(stand.copy(1))->get_control_tenure()
        == host->get_control_tenure()
    );
    NETW_CHECK_EQ(
        NetwEntity::of(stand.copy(1))->get_controller(),
        int64_t(stand.rig.peer_id(0))
    );
    stand.rig.step_ticks(DISPLAY_OFFSET + 2);

    NETW_CHECK_GT(stand.stats(1).dropped, dropped);
}

TEST_CASE(
    "[Networked][Sim][SceneTree] a heartbeat that agrees with a sleeping "
    "active copy writes nothing, and one that finds it perturbed heals it"
) {
    Stand stand(1, HEARTBEAT_SOURCE);
    netw_test::steer(stand.pair, 0);
    stand.activate(0, Restore::BUFFERED);
    const Vector2 rest(25.0, -5.0);
    stand.author(-1, rest);
    stand.rig.step_ticks(BEAT_TICKS * 2);
    REQUIRE(stand.held(0).is_equal_approx(rest));
    NETW_CHECK_EQ(int(stand.row(0)->mode), int(Mode::ACTIVE));

    stand.body(0)->set_sleeping(true);
    const int64_t writes = stand.writes(0);
    const InstallStats quiet = stand.stats(0);
    stand.rig.step_ticks(BEAT_TICKS * 2 + DISPLAY_OFFSET);
    NETW_CHECK_GT(stand.stats(0).skipped, quiet.skipped);
    NETW_CHECK_EQ(stand.stats(0).installed, quiet.installed);
    NETW_CHECK_EQ(stand.writes(0), writes);
    CHECK(stand.body(0)->is_sleeping());

    stand.body(0)->set_position(Vector2(-60.0, 12.0));
    stand.body(0)->set_sleeping(true);
    stand.rig.step_ticks(BEAT_TICKS + DISPLAY_OFFSET + 2);
    CHECK(stand.held(0).is_equal_approx(rest));
    NETW_CHECK_GT(stand.stats(0).installed, quiet.installed);
}

netw::predict::Arrival simulated_arrival(
    NetwMultiplayer *p_core,
    const PropertyBindingPtr &p_binding,
    int64_t p_tick,
    bool p_whole
) {
    netw::store::World &world = p_core->store();
    const netw::store::RowAddress at = world.row_of(p_binding->id, 0);
    REQUIRE(at.valid());
    netw::store::Table &table = world.table(at.table);
    for (int column = 0; column < table.schema.column_count(); ++column) {
        if (table.schema.at(column)->key == StringName(FIELD)) {
            table.write_value(at.row, uint32_t(column), Vector2(2.0, 0.0));
        }
    }
    netw::predict::Arrival arrival = netw::predict::Arrival::of(table, at.row);
    arrival.tick = p_tick;
    arrival.whole = p_whole;
    return arrival;
}

TEST_CASE(
    "[Networked][Sim][SceneTree] a simulated member installs through the "
    "same function as an active copy, so it accepts no row before its "
    "stream has reconstructed and reports the divergence it installed"
) {
    Stand stand(1, STATE_SOURCE);
    const Ref<NetwEntity> seated = NetwEntity::of(stand.copy(0));
    REQUIRE(seated.is_valid());
    netw::NetwPredictionEngine *const pool
        = stand.core(0)->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    pool->adopt_declaration(
        seated,
        seated->get_state_binding(),
        PropertyBindingPtr(),
        int(netw::Schedule::TICK),
        int(netw::Role::SIMULATE),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT),
        6,
        0,
        false
    );
    pool->bind_property_sets(
        slot,
        seated->get_state_binding(),
        PropertyBindingPtr()
    );
    netw::NetwPrediction *handle = Object::cast_to<netw::NetwPrediction>(
        netw::gd::live_object(seated->get_prediction())
    );
    REQUIRE(handle != nullptr);
    Vector<StringName> watched;
    watched.push_back(StringName("state_evaluated"));
    netw_test::Recorder judged(handle, watched);
    stand.body(0)->set_position(Vector2());
    const int64_t writes = stand.writes(0);

    const PropertyBindingPtr state = seated->get_state_binding();
    pool->admit_simulated_state(
        slot,
        simulated_arrival(stand.core(0), state, 7, false)
    );
    NETW_CHECK_EQ(judged.count(StringName("state_evaluated")), 0);
    NETW_CHECK_EQ(stand.writes(0), writes);
    NETW_CHECK_EQ(stand.stats(0).installed, int64_t(0));
    NETW_CHECK_EQ(pool->stream_reconstructed_of(slot), 0);

    pool->admit_simulated_state(
        slot,
        simulated_arrival(stand.core(0), state, 8, true)
    );
    NETW_CHECK_EQ(pool->stream_reconstructed_of(slot), 1);
    NETW_CHECK_EQ(stand.stats(0).installed, int64_t(1));
    CHECK(stand.held(0).is_equal_approx(Vector2(2.0, 0.0)));
    NETW_CHECK_EQ(judged.count(StringName("state_evaluated")), 1);
    const Array said = judged.args(StringName("state_evaluated"));
    REQUIRE(said.size() == 4);
    NETW_CHECK_EQ(int64_t(said[0]), int64_t(8));
    NETW_CHECK_EQ(int64_t(said[1]), int64_t(-1));
    NETW_CHECK_GT(double(said[2]), 0.0);
    NETW_CHECK_EQ(
        int64_t(pool->verdict_reason_of(slot)),
        int64_t(netw::predict::VERDICT_REASON_NONE)
    );
}

TEST_CASE(
    "[Networked][Sim][SceneTree] an extrapolated install is carried along "
    "its carry channel by the row's age, capped at the restore horizon, and "
    "an exact or same-tick install lands as authored"
) {
    Stand stand(1, CARRY_SOURCE);
    stand.rig.step_ticks(4);
    const PropertyBindingPtr binding
        = NetwEntity::of(stand.copy(0))->get_state_binding();
    REQUIRE(binding != nullptr);
    NetwMultiplayer *client = stand.core(0);
    const int64_t now = netw_test::clock_tick(stand.rig, 0);
    const double step = client->clock_engine().ticktime();

    netw::sim::Sample sample;
    sample.binding = binding->id;
    sample.comp = binding->comp;
    sample.tick = now - 3;
    sample.keys.push_back(StringName(FIELD));
    sample.keys.push_back(StringName("body_velocity"));
    sample.values.push_back(Vector2(10.0, 0.0));
    sample.values.push_back(Vector2(30.0, 0.0));

    int64_t age = -1;
    const Array exact
        = client->sim_install_target(binding, sample, Restore::EXACT, 6, age);
    NETW_CHECK_EQ(age, int64_t(3));
    CHECK(Vector2(exact[0]).is_equal_approx(Vector2(10.0, 0.0)));

    const Array carried = client->sim_install_target(
        binding,
        sample,
        Restore::EXTRAPOLATED,
        6,
        age
    );
    NETW_CHECK_CLOSE(Vector2(carried[0]).x, 10.0 + 30.0 * 3.0 * step, 1e-4);

    const Array capped = client->sim_install_target(
        binding,
        sample,
        Restore::EXTRAPOLATED,
        1,
        age
    );
    NETW_CHECK_CLOSE(Vector2(capped[0]).x, 10.0 + 30.0 * step, 1e-4);

    sample.tick = now;
    const Array fresh = client->sim_install_target(
        binding,
        sample,
        Restore::EXTRAPOLATED,
        6,
        age
    );
    NETW_CHECK_EQ(age, int64_t(0));
    CHECK(Vector2(fresh[0]).is_equal_approx(Vector2(10.0, 0.0)));
}

TEST_CASE(
    "[Networked][Sim][SceneTree] O5 a predicted copy that leaves proxy for "
    "active starts from the newest row it received, not from where its body "
    "was left"
) {
    Stand stand(1, STATE_SOURCE);
    NetwMultiplayer *session = stand.core(0);
    const RID copy = stand.rid(0);
    netw::sim::Row &seated = session->sim_row(copy);
    seated.declaration.bodies.push_back(NodePath("Body"));
    seated.facts.predicted = true;
    seated.mode = Mode::PROXY;
    session->sim_settle_body(NetwEntity::of(stand.copy(0)));
    stand.author_ticks(-1, 12, 10.0);
    REQUIRE(stand.held(0).x > 0.0);

    stand.copy(0)->set(StringName(FIELD), Vector2(-500.0, 0.0));
    session->sim_row(copy).mode = Mode::ACTIVE;
    session->sim_settle_body(NetwEntity::of(stand.copy(0)));
    NETW_CHECK_EQ(
        int(session->sim_row_of(copy)->bodies.applied),
        int(Mode::ACTIVE)
    );
    NETW_CHECK_GT(stand.held(0).x, 0.0);
}

} // namespace TestNetwSimInstallLaws

#endif
