#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

#include "netw/api/context.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/ring_buffer.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/timing.hpp"
#include "netw/script/model.hpp"
#include "netw/wire/registry.hpp"

#include <godot_cpp/classes/multiplayer_synchronizer.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/rigid_body2d.hpp>

namespace TestDisplayNodeTruthLaws {

using namespace godot;
using netw::NetwDisplayHandle;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int REMOTE_PEER = 2;
const char *TARGET_BODY = netw_test::gdsrc::INTERPOLATED_RPC_AND_SIGNAL;

bool near(const Vector2 &p_held, const Vector2 &p_wanted) {
    return p_held.distance_to(p_wanted) <= 0.1;
}

Vector2 p0() {
    return Vector2(0.0, 0.0);
}

Vector2 p1() {
    return Vector2(100.0, 0.0);
}

Vector2 p2() {
    return Vector2(200.0, 0.0);
}

Ref<netw::NetwInterpolate> lerp_to(const StringName &p_target) {
    Ref<netw::NetwInterpolate> spec;
    spec.instantiate();
    return spec->lerp()->smooth(0.0)->to(p_target);
}

Ref<netw::NetwInterpolate> slerp_to(const StringName &p_target) {
    Ref<netw::NetwInterpolate> spec;
    spec.instantiate();
    return spec->slerp()->smooth(0.0)->to(p_target);
}

class Bench {
public:
    LoopbackRig rig;

    Bench() : rig(0) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE, 0);
    }

    NetwMultiplayer *core() const {
        return netw_test::flow_core(rig.server());
    }

    netw::ClockEngine &clock() const {
        return rig.clock_of(rig.server());
    }

    Node *branch() const {
        return rig.branch(-1);
    }

    void seat(Node *p_node) const {
        branch()->add_child(p_node);
    }

    void bind(const Ref<NetwEntity> &p_entity, int64_t p_route) const {
        core()->liveness_bind_route(p_route, p_entity.ptr());
    }

    void record(
        Node *p_node,
        const StringName &p_property,
        const Variant &p_value,
        int64_t p_tick
    ) const {
        NetwNativeTests::display_record(
            core(),
            p_node,
            p_property,
            p_value,
            p_tick,
            netw::script::model::get_node_property_interpolator(
                p_node,
                p_property
            ),
            false
        );
    }

    void display_at(int64_t p_tick, int p_offset, double p_factor) const {
        netw::ClockEngine &handle = clock();
        handle.set_tick(int(p_tick));
        handle.set_display_offset(p_offset);
        handle.set_tick_factor_override(p_factor);
        NetwNativeTests::display_pump(core(), 0.0);
    }

    void render(const RID &p_entity, double p_delta = 1.0 / 60.0) const {
        NetwNativeTests::display_pump_runtime(
            core(),
            NetwNativeTests::display_runtime_of(core(), p_entity),
            netw::display::capture_timing(&core()->clock_engine(), p_delta)
        );
    }

    void render_many(const RID &p_entity, int p_frames) const {
        for (int frame = 0; frame < p_frames; ++frame) {
            render(p_entity);
        }
    }
};

struct Subject {
    Node2D *body = nullptr;
    Node2D *visual = nullptr;
    Ref<NetwEntity> entity;

    RID rid() const {
        return entity->get_rid_handle();
    }

    Node2D *displayed() const {
        return visual != nullptr ? visual : body;
    }
};

void declare_prediction(Node *p_node) {
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    sync->set_name("PredictSync");
    sync->set_meta(
        StringName("netw_schedule"),
        int(netw::NetwPredict::SCHEDULE_TICK)
    );
    p_node->add_child(sync);
    sync->set_owner(p_node);
}

Subject stand_target(
    const Bench &p_bench,
    const char *p_name,
    bool p_with_visual,
    bool p_scripted = false
) {
    Subject subject;
    if (p_scripted) {
        subject.body = netw_test::flow_body(TARGET_BODY, p_name);
    } else {
        subject.body = memnew(Node2D);
        subject.body->set_name(p_name);
    }
    subject.body->set_multiplayer_authority(REMOTE_PEER);
    subject.entity = NetwEntity::ensure(subject.body);
    subject.entity->get_interpolation()->set_enable_smart_dilation(false);
    if (p_with_visual) {
        subject.visual = memnew(Node2D);
        subject.visual->set_name("Visual");
        subject.body->add_child(subject.visual);
        subject.entity->get_interpolation()->set_visual_root(
            NodePath("Visual")
        );
    }
    p_bench.seat(subject.body);
    return subject;
}

struct DisplayDecl {
    int64_t role = NetwMultiplayer::DISPLAY_ROLE_AUTO;
    int64_t predicted_mode = -1;
    double smooth_time = -1.0;
};

Subject stand_interpolated(
    const Bench &p_bench,
    const char *p_name,
    bool p_with_visual,
    int64_t p_route,
    const DisplayDecl &p_declared = DisplayDecl(),
    bool p_scripted = false
) {
    Subject subject = stand_target(p_bench, p_name, p_with_visual, p_scripted);
    const Ref<NetwDisplayHandle> display = subject.entity->get_interpolation();
    if (p_declared.role != NetwMultiplayer::DISPLAY_ROLE_AUTO) {
        display->set_display_role(p_declared.role);
    }
    if (p_declared.predicted_mode >= 0) {
        display->set_predicted_mode(p_declared.predicted_mode);
    }
    if (p_declared.smooth_time >= 0.0) {
        display->set_predicted_smooth_time(p_declared.smooth_time);
    }
    netw::Netw::configure_property(subject.body, StringName("position"), false)
        ->interpolate(netw::gd::array_of(lerp_to(StringName("position"))));
    p_bench.bind(subject.entity, p_route);
    p_bench.core()->session_flush_deferred();
    return subject;
}

Subject stand_predicted(const Bench &p_bench, int64_t p_route) {
    DisplayDecl declared;
    declared.role = NetwMultiplayer::DISPLAY_ROLE_PREDICTED;
    declared.smooth_time = 0.05;
    return stand_interpolated(
        p_bench,
        "PredictedTarget",
        true,
        p_route,
        declared
    );
}

netw::ReplicationCore *plane_of(const Bench &p_bench) {
    netw::ReplicationCore *plane = p_bench.core()->get_replication_plane();
    REQUIRE_MESSAGE(plane != nullptr, "the session has no replication plane");
    return plane;
}

enum WorldShape {
    WORLD_VISUAL_CHILD,
    WORLD_OVERLAY,
    WORLD_MOVING_BODY,
    WORLD_SCALED_BODY,
    WORLD_QUATERNION,
    WORLD_REBUILT,
    WORLD_BRACKETED,
};

enum Plant {
    PLANT_NONE,
    PLANT_NOTHING_WAS_RECORDED,
    PLANT_THE_VISUAL_IS_SEVERED,
    PLANT_THE_VISUAL_ROOT_NAMES_NOTHING,
    PLANT_THE_LADDER_READS_A_STEERED_ENTITY_AS_OWNED,
};

struct WriteScenario {
    String label;
    WorldShape shape = WORLD_VISUAL_CHILD;
    int64_t route = 7;
};

WriteScenario visual_child() {
    WriteScenario scenario;
    scenario.label = "visual-child";
    return scenario;
}

WriteScenario overlay() {
    WriteScenario scenario;
    scenario.label = "overlay";
    scenario.shape = WORLD_OVERLAY;
    scenario.route = 33;
    return scenario;
}

WriteScenario moving_body() {
    WriteScenario scenario;
    scenario.label = "moving-body";
    scenario.shape = WORLD_MOVING_BODY;
    scenario.route = 11;
    return scenario;
}

WriteScenario scaled_body() {
    WriteScenario scenario;
    scenario.label = "scaled-body";
    scenario.shape = WORLD_SCALED_BODY;
    scenario.route = 12;
    return scenario;
}

WriteScenario rebuilt_runtime() {
    WriteScenario scenario;
    scenario.label = "rebuilt-runtime";
    scenario.shape = WORLD_REBUILT;
    scenario.route = 13;
    return scenario;
}

WriteScenario bracketed_predicted() {
    WriteScenario scenario;
    scenario.label = "bracketed-predicted";
    scenario.shape = WORLD_BRACKETED;
    scenario.route = 14;
    return scenario;
}

struct WriteEvidence {
    Vector2 displayed;
    Vector2 expected;
    Vector2 body;
    Vector2 scale;
    bool has_runtime = false;
};

class WriteRun {
    WriteScenario declared;
    Plant planted = PLANT_NONE;
    WriteEvidence seen;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

public:
    explicit WriteRun(
        const WriteScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        Bench bench;
        const bool with_visual = p_scenario.shape != WORLD_OVERLAY
            && !plant_is(PLANT_THE_VISUAL_IS_SEVERED)
            && !plant_is(PLANT_THE_VISUAL_ROOT_NAMES_NOTHING);
        DisplayDecl declared;
        if (p_scenario.shape == WORLD_BRACKETED) {
            declared.role = NetwMultiplayer::DISPLAY_ROLE_PREDICTED;
            declared.predicted_mode = NetwMultiplayer::PREDICTED_MODE_BRACKETED;
        }
        Subject subject = stand_interpolated(
            bench,
            "WriteTarget",
            with_visual,
            p_scenario.route,
            declared
        );
        if (plant_is(PLANT_THE_VISUAL_ROOT_NAMES_NOTHING)) {
            subject.entity->get_interpolation()->set_visual_root(
                NodePath("Visual")
            );
        }
        if (p_scenario.shape == WORLD_REBUILT
            || plant_is(PLANT_THE_VISUAL_ROOT_NAMES_NOTHING)) {
            bench.core()->display_mark_dirty(
                subject.rid(),
                netw::display::DIRT_RUNTIME
            );
            bench.core()->session_flush_deferred();
        }
        if (p_scenario.shape == WORLD_BRACKETED) {
            subject.body->set_position(p0());
            NetwNativeTests::display_clock_tick(bench.core(), 0.0, 0);
            subject.body->set_position(p1());
            NetwNativeTests::display_clock_tick(bench.core(), 0.0, 1);
        } else if (!plant_is(PLANT_NOTHING_WAS_RECORDED)) {
            bench.record(subject.body, StringName("position"), p0(), 0);
            bench.record(subject.body, StringName("position"), p1(), 1);
        }
        if (p_scenario.shape == WORLD_MOVING_BODY) {
            subject.body->set_position(Vector2(500.0, 500.0));
        }
        if (p_scenario.shape == WORLD_SCALED_BODY) {
            subject.body->set_scale(Vector2(2.0, 2.0));
        }
        bench.display_at(1, p_scenario.shape == WORLD_BRACKETED ? 0 : 1, 0.5);

        seen.displayed = subject.displayed()->get_global_position();
        seen.expected = p0().lerp(p1(), 0.5);
        seen.body = subject.body->get_position();
        seen.scale = subject.displayed()->get_global_scale();
        seen.has_runtime
            = NetwNativeTests::display_runtime_of(bench.core(), subject.rid())
            != nullptr;
    }

    const WriteScenario &scenario() const {
        return declared;
    }

    const WriteEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<WriteRun> WriteLaw;

LawVerdict law_bracketed(const WriteRun &p_run) {
    const WriteEvidence &seen = p_run.evidence();
    if (!near(seen.displayed, seen.expected)) {
        return law_broken(
            "the display holds %d where the bracket reads %d",
            int(seen.displayed.x),
            int(seen.expected.x)
        );
    }
    return law_held();
}

const WriteLaw L_BRACKETED = {
    "bracketed",
    "the display writes the bracketed value of the two recorded rows to the "
    "configured visual, in global space",
    &law_bracketed,
};

LawVerdict law_inherits(const WriteRun &p_run) {
    const WriteEvidence &seen = p_run.evidence();
    if (p_run.scenario().shape != WORLD_SCALED_BODY) {
        return law_held();
    }
    if (!seen.scale.is_equal_approx(Vector2(2.0, 2.0))) {
        return law_broken(
            "a parented visual reads scale %d rather than its body's",
            int(seen.scale.x * 100.0)
        );
    }
    return law_held();
}

const WriteLaw L_INHERITS = {
    "inherits",
    "an unsmoothed channel still inherits from the body, so writing the "
    "smoothed one in global space never severs the visual",
    &law_inherits,
};

LawVerdict law_leaves_the_body(const WriteRun &p_run) {
    const WriteScenario &scenario = p_run.scenario();
    const WriteEvidence &seen = p_run.evidence();
    if (scenario.shape == WORLD_OVERLAY || scenario.shape == WORLD_BRACKETED) {
        return law_held();
    }
    const Vector2 held
        = scenario.shape == WORLD_MOVING_BODY ? Vector2(500.0, 500.0) : p0();
    if (!near(seen.body, held)) {
        return law_broken(
            "the body reads %d where it was left at %d",
            int(seen.body.x),
            int(held.x)
        );
    }
    return law_held();
}

const WriteLaw L_LEAVES_THE_BODY = {
    "leaves-the-body",
    "a display over a visual writes the visual and never the body, so moving "
    "the body does not drag the smoothed channel",
    &law_leaves_the_body,
};

const WriteLaw WRITE_LAWS[] = {L_BRACKETED, L_INHERITS, L_LEAVES_THE_BODY};

TEST_CASE("[Networked][Display][SceneTree] the display write laws hold") {
    const WriteScenario CORPUS[] = {
        visual_child(),
        overlay(),
        moving_body(),
        scaled_body(),
        rebuilt_runtime(),
        bracketed_predicted(),
    };
    for (const WriteScenario &scenario : CORPUS) {
        const WriteRun run(scenario);
        for (const WriteLaw &law : WRITE_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a world that records nothing reds "
    "bracketed"
) {
    const WriteScenario scenario = visual_child();
    const WriteRun run(scenario, PLANT_NOTHING_WAS_RECORDED);
    NETW_CELL(L_BRACKETED, scenario);
    NETW_LAW_BREAKS(L_BRACKETED, run);
}

TEST_CASE(
    "[Networked][Display][SceneTree] a body that is its own visual reds "
    "leaves-the-body"
) {
    const WriteScenario scenario = moving_body();
    const WriteRun run(scenario, PLANT_THE_VISUAL_IS_SEVERED);
    NETW_CELL(L_LEAVES_THE_BODY, scenario);
    NETW_LAW_BREAKS(L_LEAVES_THE_BODY, run);
}

TEST_CASE(
    "[Networked][Display][SceneTree] a visual root naming no node still "
    "holds leaves-the-body"
) {
    const WriteScenario scenario = moving_body();
    const WriteRun run(scenario, PLANT_THE_VISUAL_ROOT_NAMES_NOTHING);
    NETW_CELL(L_LEAVES_THE_BODY, scenario);
    NETW_LAW_HOLDS(L_LEAVES_THE_BODY, run);
}

enum Rung {
    RUNG_AUTHORITY,
    RUNG_SIMULATED,
    RUNG_STEERED,
    RUNG_REMOTE,
};

struct RoleScenario {
    String label;
    Rung rung = RUNG_AUTHORITY;
    int64_t route = 51;
    int64_t expected = NetwMultiplayer::DISPLAY_ROLE_DISABLED;
};

RoleScenario locally_owned() {
    RoleScenario scenario;
    scenario.label = "locally-owned";
    return scenario;
}

RoleScenario locally_simulated() {
    RoleScenario scenario;
    scenario.label = "locally-simulated";
    scenario.rung = RUNG_SIMULATED;
    scenario.route = 52;
    scenario.expected = NetwMultiplayer::DISPLAY_ROLE_PREDICTED;
    return scenario;
}

RoleScenario locally_steered() {
    RoleScenario scenario;
    scenario.label = "locally-steered";
    scenario.rung = RUNG_STEERED;
    scenario.route = 53;
    scenario.expected = NetwMultiplayer::DISPLAY_ROLE_PREDICTED;
    return scenario;
}

RoleScenario streamed_from_elsewhere() {
    RoleScenario scenario;
    scenario.label = "streamed-from-elsewhere";
    scenario.rung = RUNG_REMOTE;
    scenario.route = 54;
    scenario.expected = NetwMultiplayer::DISPLAY_ROLE_REMOTE;
    return scenario;
}

class RoleRun {
    RoleScenario declared;
    int64_t resolved = -1;
    bool steers_locally = false;

public:
    explicit RoleRun(const RoleScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario) {
        Bench bench;
        Node2D *node = memnew(Node2D);
        node->set_name("LadderTarget");
        if (p_scenario.rung == RUNG_SIMULATED
            || p_scenario.rung == RUNG_STEERED) {
            declare_prediction(node);
        }
        if (p_scenario.rung == RUNG_REMOTE) {
            node->set_multiplayer_authority(REMOTE_PEER);
        }
        const Ref<NetwEntity> entity = NetwEntity::ensure(node);
        entity->get_interpolation()->set_enable_smart_dilation(false);
        if (p_scenario.rung == RUNG_SIMULATED) {
            entity->get_prediction()->set_input_source(
                netw::NetwPredict::INPUT_SOURCE_PREDICTED
            );
        }
        if (p_scenario.rung == RUNG_SIMULATED
            || p_scenario.rung == RUNG_STEERED) {
            entity->get_prediction()->set_sim_mode(
                netw::NetwPredict::SIM_MODE_SPECULATIVE
            );
        }
        netw::Netw::configure_property(node, StringName("position"), false)
            ->interpolate(netw::gd::array_of(lerp_to(StringName("position"))));
        bench.seat(node);
        bench.bind(entity, p_scenario.route);
        if (p_scenario.rung == RUNG_STEERED
            && p_plant != PLANT_THE_LADDER_READS_A_STEERED_ENTITY_AS_OWNED) {
            entity->set_controller(1);
        }
        bench.record(node, StringName("position"), p0(), 0);
        NetwNativeTests::display_mark_role_dirty(
            bench.core(),
            entity->get_rid_handle()
        );
        steers_locally = entity->get_is_controlled_locally();
        resolved = int64_t(bench.core()->display_get_track_stat(
            entity->get_rid_handle(),
            StringName(),
            StringName("role")
        ));
    }

    const RoleScenario &scenario() const {
        return declared;
    }

    int64_t role() const {
        return resolved;
    }

    bool local_control() const {
        return steers_locally;
    }
};

typedef LawRowFor<RoleRun> RoleLaw;

LawVerdict law_ladder(const RoleRun &p_run) {
    if (p_run.role() != p_run.scenario().expected) {
        return law_broken(
            "the ladder resolved role %d where the world declares %d",
            int(p_run.role()),
            int(p_run.scenario().expected)
        );
    }
    return law_held();
}

const RoleLaw L_LADDER = {
    "ladder",
    "the auto role reads the entity's own facts: an entity this peer holds "
    "authority over displays nothing, one this peer simulates or steers is "
    "predicted, and one streamed from elsewhere is remote",
    &law_ladder,
};

LawVerdict law_steering(const RoleRun &p_run) {
    if (p_run.scenario().rung != RUNG_STEERED) {
        return law_held();
    }
    if (!p_run.local_control()) {
        return law_broken("the entity this peer steers reads as remote-held");
    }
    return law_held();
}

const RoleLaw L_STEERING = {
    "steering",
    "the prediction rung is reached through local control alone, with no "
    "declared input source",
    &law_steering,
};

const RoleLaw ROLE_LAWS[] = {L_LADDER, L_STEERING};

TEST_CASE("[Networked][Display][SceneTree] the auto display role laws hold") {
    const RoleScenario CORPUS[] = {
        locally_owned(),
        locally_simulated(),
        locally_steered(),
        streamed_from_elsewhere(),
    };
    for (const RoleScenario &scenario : CORPUS) {
        const RoleRun run(scenario);
        for (const RoleLaw &law : ROLE_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a steered entity nobody steers reds "
    "ladder"
) {
    const RoleScenario scenario = locally_steered();
    const RoleRun run(
        scenario,
        PLANT_THE_LADDER_READS_A_STEERED_ENTITY_AS_OWNED
    );
    NETW_CELL(L_LADDER, scenario);
    NETW_LAW_BREAKS(L_LADDER, run);
}

TEST_CASE(
    "[Networked][Display][SceneTree] an unconfigured record builds no "
    "runtime, so a bare node costs nothing"
) {
    Bench bench;
    Node2D *node = memnew(Node2D);
    node->set_name("BareTarget");
    node->set_multiplayer_authority(REMOTE_PEER);
    const Ref<NetwEntity> entity = NetwEntity::ensure(node);
    bench.seat(node);
    bench.bind(entity, 31);

    bench.record(node, StringName("position"), p1(), 0);

    CHECK(
        NetwNativeTests::display_runtime_of(
            bench.core(),
            entity->get_rid_handle()
        )
        == nullptr
    );
}

TEST_CASE(
    "[Networked][Display][SceneTree] two visuals interpolating the same "
    "property keep their own source, so neither collides with the "
    "other"
) {
    Bench bench;
    Node2D *body = memnew(Node2D);
    body->set_name("TwoTrackTarget");
    body->set_multiplayer_authority(REMOTE_PEER);
    Node2D *first = memnew(Node2D);
    first->set_name("A");
    body->add_child(first);
    Node2D *second = memnew(Node2D);
    second->set_name("B");
    body->add_child(second);
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    entity->get_interpolation()->set_enable_smart_dilation(false);
    netw::Netw::configure_property(first, StringName("position"), false)
        ->interpolate(netw::gd::array_of(lerp_to(StringName("position"))));
    netw::Netw::configure_property(second, StringName("position"), false)
        ->interpolate(netw::gd::array_of(lerp_to(StringName("position"))));
    bench.seat(body);
    bench.bind(entity, 21);

    bench.record(first, StringName("position"), p0(), 0);
    bench.record(second, StringName("position"), p2(), 0);

    const netw::display::Runtime *runtime = NetwNativeTests::display_runtime_of(
        bench.core(),
        entity->get_rid_handle()
    );
    REQUIRE(runtime != nullptr);
    int sources = 0;
    bool holds_first = false;
    bool holds_second = false;
    for (netw::display::Channel *channel : runtime->channels()) {
        if (channel->get_name() != StringName("position")) {
            continue;
        }
        sources += 1;
        Object *source = channel->get_source_obj();
        holds_first = holds_first || source == first;
        holds_second = holds_second || source == second;
    }
    NETW_CHECK_EQ(sources, 2);
    CHECK(holds_first);
    CHECK(holds_second);
}

TEST_CASE(
    "[Networked][Display][SceneTree] a slerp channel brackets a "
    "quaternion, which no component-wise lerp would reach"
) {
    Bench bench;
    Node3D *node = memnew(Node3D);
    node->set_name("RotationTarget");
    node->set_multiplayer_authority(REMOTE_PEER);
    const Ref<NetwEntity> entity = NetwEntity::ensure(node);
    entity->get_interpolation()->set_enable_smart_dilation(false);
    netw::Netw::configure_property(node, StringName("quaternion"), false)
        ->interpolate(netw::gd::array_of(slerp_to(StringName("quaternion"))));
    bench.seat(node);
    bench.bind(entity, 41);

    const Quaternion held;
    const Quaternion turned(Vector3(0.0, 1.0, 0.0), Math_PI / 2.0);
    bench.record(node, StringName("quaternion"), held, 0);
    bench.record(node, StringName("quaternion"), turned, 1);
    bench.display_at(1, 1, 0.5);

    CHECK(node->get_quaternion().is_equal_approx(held.slerp(turned, 0.5)));
}

TEST_CASE(
    "[Networked][Display][SceneTree] a remote rigid body freezes "
    "kinematically while a display owns it and returns to its own "
    "freeze when the display is disabled"
) {
    Bench bench;
    RigidBody2D *body = memnew(RigidBody2D);
    body->set_name("RemoteBody");
    body->set_freeze_enabled(false);
    body->set_freeze_mode(RigidBody2D::FREEZE_MODE_STATIC);
    body->set_multiplayer_authority(REMOTE_PEER);
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    entity->get_interpolation()->set_enable_smart_dilation(false);
    netw::Netw::configure_property(body, StringName("position"), false)
        ->interpolate(netw::gd::array_of(lerp_to(StringName("position"))));
    bench.seat(body);
    bench.bind(entity, 44);

    CHECK(body->is_freeze_enabled());
    NETW_CHECK_EQ(
        int(body->get_freeze_mode()),
        int(RigidBody2D::FREEZE_MODE_KINEMATIC)
    );

    entity->get_interpolation()->set_display_role(
        NetwMultiplayer::DISPLAY_ROLE_DISABLED
    );
    bench.core()->display_mark_dirty(
        entity->get_rid_handle(),
        netw::display::DIRT_RUNTIME
    );
    bench.core()->session_flush_deferred();

    CHECK(!body->is_freeze_enabled());
    NETW_CHECK_EQ(
        int(body->get_freeze_mode()),
        int(RigidBody2D::FREEZE_MODE_STATIC)
    );
}

TEST_CASE(
    "[Networked][Display][SceneTree] a predicted chase moves the visual "
    "toward the live source without moving the source"
) {
    Bench bench;
    const Subject subject = stand_predicted(bench, 7);
    subject.body->set_position(p1());
    bench.render(subject.rid());

    NETW_CHECK_GT(subject.visual->get_global_position().x, 0.0);
    NETW_CHECK_LT(subject.visual->get_global_position().x, p1().x);
    CHECK(near(subject.body->get_position(), p1()));
}

TEST_CASE(
    "[Networked][Display][SceneTree] a reset snaps the chase to the live "
    "source"
) {
    Bench bench;
    const Subject subject = stand_predicted(bench, 7);
    subject.body->set_position(p1());
    bench.render(subject.rid());
    NETW_CHECK_LT(subject.visual->get_global_position().x, p1().x);

    subject.entity->get_interpolation()->reset();
    CHECK(near(subject.visual->get_global_position(), p1()));
}

TEST_CASE(
    "[Networked][Display][SceneTree] a snap writes the visual for a "
    "track that has no history"
) {
    Bench bench;
    const Subject subject = stand_predicted(bench, 7);

    bench.core()->display_snap(subject.rid(), StringName("position"), p2());

    CHECK(near(subject.visual->get_global_position(), p2()));
    CHECK(subject.entity->get_interpolation()
              ->get_buffer(StringName("position"))
              .is_null());
}

TEST_CASE(
    "[Networked][Display][SceneTree] a correction is absorbed over "
    "frames rather than landing in one"
) {
    Bench bench;
    const Subject subject = stand_predicted(bench, 7);
    subject.body->set_position(p1());
    bench.render(subject.rid());
    const double before = subject.visual->get_global_position().x;

    subject.body->set_position(p2());
    bench.render(subject.rid());
    const double after = subject.visual->get_global_position().x;
    NETW_CHECK_GT(after, before);
    NETW_CHECK_LT(after, p2().x);

    bench.render_many(subject.rid(), 12);
    NETW_CHECK_GT(subject.visual->get_global_position().x, after);
    NETW_CHECK_LE(subject.visual->get_global_position().x, p2().x);
}

TEST_CASE(
    "[Networked][Display][SceneTree] an automatic chase smooth time "
    "tracks the clock's ticktime at every tickrate"
) {
    Bench bench;
    const Subject subject = stand_predicted(bench, 7);
    subject.entity->get_interpolation()->set_predicted_smooth_time(0.0);
    netw::display::Runtime *runtime
        = NetwNativeTests::display_runtime_of(bench.core(), subject.rid());
    REQUIRE(runtime != nullptr);
    netw::ClockEngine &clock = bench.core()->clock_engine();

    const int RATES[] = {15, 60};
    for (const int rate : RATES) {
        clock.set_tickrate(rate);
        NETW_CHECK_CLOSE(
            NetwNativeTests::display_chase_smooth_time(
                bench.core(),
                runtime,
                netw::display::capture_timing(&clock, 0.0)
            ),
            clock.ticktime() * 0.85,
            0.0001
        );
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a promote to predicted clears the "
    "remote tick domain"
) {
    Bench bench;
    const Subject subject = stand_interpolated(bench, "PromoteTarget", true, 7);
    bench.record(subject.body, StringName("position"), p0(), 0);
    bench.record(subject.body, StringName("position"), p1(), 1);
    bench.display_at(1, 1, 0.5);

    subject.entity->get_interpolation()->set_display_role(
        NetwMultiplayer::DISPLAY_ROLE_PREDICTED
    );
    const netw::display::Runtime *runtime
        = NetwNativeTests::display_runtime_of(bench.core(), subject.rid());
    REQUIRE(runtime != nullptr);
    for (const netw::display::Channel *channel : runtime->channels()) {
        CHECK(channel->display_history().is_empty());
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a demote to remote resumes from the "
    "rows recorded while predicted and seeds its role offset on the "
    "first frame"
) {
    Bench bench;
    const Subject subject = stand_predicted(bench, 7);
    subject.entity->get_prediction()->set_teleport_threshold(250.0);
    subject.body->set_position(p0());
    bench.render_many(subject.rid(), 30);

    bench.record(subject.body, StringName("position"), p1(), 1);
    bench.record(subject.body, StringName("position"), p2(), 2);
    const netw::display::Runtime *runtime
        = NetwNativeTests::display_runtime_of(bench.core(), subject.rid());
    REQUIRE(runtime != nullptr);
    REQUIRE(runtime->channels().size() > 0);
    const netw::display::Channel *track = runtime->channels()[0];
    CHECK(!track->display_history().is_empty());
    bench.render(subject.rid());
    NETW_CHECK_LT(subject.visual->get_global_position().distance_to(p0()), 0.1);

    subject.entity->get_interpolation()->set_display_role(
        NetwMultiplayer::DISPLAY_ROLE_REMOTE
    );
    CHECK(
        bool(runtime->track_stat(track->get_name(), StringName("offset_armed")))
    );
    bench.display_at(2, 0, 0.0);
    CHECK(!bool(
        runtime->track_stat(track->get_name(), StringName("offset_armed"))
    ));
    CHECK(
        bool(runtime->track_stat(track->get_name(), StringName("offset_held")))
    );
    NETW_CHECK_LT(subject.visual->get_global_position().distance_to(p0()), 0.1);

    bench.render_many(subject.rid(), 90);
    CHECK(near(subject.visual->get_global_position(), p2()));
}

TEST_CASE(
    "[Networked][Display][SceneTree] a disabled role declines every pass "
    "while a running one counts its own"
) {
    Bench bench;
    const Subject subject = stand_predicted(bench, 7);
    bench.render_many(subject.rid(), 10);
    const int64_t ran = int64_t(bench.core()->display_get_track_stat(
        subject.rid(),
        StringName(),
        StringName("pumped_frames")
    ));
    bench.render_many(subject.rid(), 10);
    NETW_CHECK_GT(
        int64_t(bench.core()->display_get_track_stat(
            subject.rid(),
            StringName(),
            StringName("pumped_frames")
        )),
        ran
    );

    subject.entity->get_interpolation()->set_display_role(
        NetwMultiplayer::DISPLAY_ROLE_DISABLED
    );
    bench.render(subject.rid());
    const int64_t declined = int64_t(bench.core()->display_get_track_stat(
        subject.rid(),
        StringName(),
        StringName("pumped_frames")
    ));
    bench.render_many(subject.rid(), 10);
    NETW_CHECK_EQ(
        int64_t(bench.core()->display_get_track_stat(
            subject.rid(),
            StringName(),
            StringName("pumped_frames")
        )),
        declined
    );
}

void dispatch(
    const Bench &p_bench,
    const Subject &p_subject,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    plane_of(p_bench)->dispatch(
        p_subject.entity->get_route(),
        0,
        p_channel,
        p_payload,
        String(),
        1,
        true,
        0
    );
}

TEST_CASE(
    "[Networked][Display][SceneTree] a property arriving with no "
    "synchronizer still feeds the display"
) {
    Bench bench;
    const Subject subject = stand_interpolated(
        bench,
        "DispatchTarget",
        true,
        7,
        DisplayDecl(),
        true
    );

    netw::wire::WriteStream writer;
    REQUIRE(netw::script::model::write_token(writer, StringName("position")));
    Array values;
    values.push_back(p0());
    Array quantizers;
    quantizers.push_back(Variant());
    Array types;
    types.push_back(int(Variant::VECTOR2));
    REQUIRE(netw::call_args::values_write(writer, values, quantizers, types));
    REQUIRE(writer.align_verify());
    bench.clock().set_tick(0);
    dispatch(
        bench,
        subject,
        netw::wire::builtin_channel("PROPERTY_SYNC"),
        writer.to_bytes()
    );

    netw::wire::WriteStream second;
    REQUIRE(netw::script::model::write_token(second, StringName("position")));
    Array later;
    later.push_back(p1());
    REQUIRE(netw::call_args::values_write(second, later, quantizers, types));
    REQUIRE(second.align_verify());
    bench.clock().set_tick(1);
    dispatch(
        bench,
        subject,
        netw::wire::builtin_channel("PROPERTY_SYNC"),
        second.to_bytes()
    );

    bench.display_at(1, 1, 0.5);

    CHECK(near(subject.body->get_position(), p1()));
    CHECK(near(subject.visual->get_global_position(), p0().lerp(p1(), 0.5)));
}

TEST_CASE(
    "[Networked][Display][SceneTree] an arriving rpc argument drives an "
    "interpolated target the method itself never writes"
) {
    Bench bench;
    const Subject subject
        = stand_interpolated(bench, "RpcTarget", false, 7, DisplayDecl(), true);
    Array specs;
    specs.push_back(lerp_to(StringName("rpc_target")));
    netw::Netw::configure_rpc(Callable(subject.body, StringName("apply_rpc")))
        ->interpolate(specs);

    netw::wire::WriteStream writer;
    uint64_t flags = 0;
    REQUIRE(writer.bits(flags, 8));
    LocalVector<netw::call_args::Slot> args;
    args.push_back(netw::call_args::of_value(p1()));
    REQUIRE(
        netw::script::model::write_call_body(
            writer,
            StringName("apply_rpc"),
            args,
            Array(),
            netw::script::model::get_method_arg_types(
                subject.body->get_script(),
                StringName("apply_rpc")
            )
        )
    );
    REQUIRE(writer.align_verify());
    bench.clock().set_tick(4);
    dispatch(
        bench,
        subject,
        netw::wire::builtin_channel("CALL"),
        writer.to_bytes()
    );
    bench.display_at(4, 0, 0.0);

    CHECK(near(subject.body->get(StringName("last_rpc_arg")), p1()));
    CHECK(near(subject.body->get(StringName("rpc_target")), p1()));
}

TEST_CASE(
    "[Networked][Display][SceneTree] an arriving signal argument drives "
    "an interpolated target and still reaches the signal's listeners"
) {
    Bench bench;
    const Subject subject = stand_interpolated(
        bench,
        "SignalTarget",
        false,
        7,
        DisplayDecl(),
        true
    );
    Array specs;
    specs.push_back(lerp_to(StringName("signal_target")));
    netw::Netw::configure_signal(Signal(subject.body, StringName("nudged")))
        ->interpolate(specs);

    netw::wire::WriteStream writer;
    REQUIRE(netw::script::model::write_token(writer, StringName("nudged")));
    Array args;
    args.push_back(p2());
    REQUIRE(
        netw::call_args::values_write(
            writer,
            args,
            Array(),
            netw::script::model::get_signal_arg_types(
                subject.body->get_script(),
                StringName("nudged")
            )
        )
    );
    REQUIRE(writer.align_verify());
    bench.clock().set_tick(8);
    dispatch(
        bench,
        subject,
        netw::wire::builtin_channel("SIGNAL"),
        writer.to_bytes()
    );
    bench.display_at(8, 0, 0.0);

    CHECK(near(subject.body->get(StringName("signal_target")), p2()));
}

TEST_CASE(
    "[Networked][Display][SceneTree] a promote to predicted adopts the "
    "display the remote role left, then glides to the live source"
) {
    Bench bench;
    DisplayDecl declared;
    declared.role = NetwMultiplayer::DISPLAY_ROLE_REMOTE;
    const Subject subject
        = stand_interpolated(bench, "GlideTarget", true, 7, declared);
    subject.entity->get_prediction()->set_teleport_threshold(250.0);
    bench.core()->display_set_param(
        subject.rid(),
        NetwMultiplayer::DISPLAY_PARAM_CHASE_GLIDE_TIME,
        0.15
    );

    bench.record(subject.body, StringName("position"), p1(), 1);
    bench.display_at(1, 0, 0.0);
    bench.render_many(subject.rid(), 90);
    CHECK(near(subject.visual->get_global_position(), p1()));

    subject.body->set_position(p0());
    subject.entity->get_interpolation()->set_display_role(
        NetwMultiplayer::DISPLAY_ROLE_PREDICTED
    );
    bench.render(subject.rid());
    NETW_CHECK_LT(
        subject.visual->get_global_position().distance_to(p1()),
        20.0
    );
    bench.render_many(subject.rid(), 90);
    CHECK(near(subject.visual->get_global_position(), p0()));
}

} // namespace TestDisplayNodeTruthLaws

#endif
