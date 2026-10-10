#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED) && defined(DEBUG_ENABLED)

#include "netw/sim/row.hpp"
#include "support/declared_nodes.h"
#include "support/display_stand.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "godot/sprite.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/diag/interpolation_view.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/runtime.hpp"
#include "netw/object_port.hpp"
#include "netw/repl/property_set.hpp"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>

using namespace godot;

namespace TestDiagInterpolationViewLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw::diag::HeldSample;
using netw::diag::InterpolationView;
using netw::diag::TimelineRow;
using netw::repl::PropertySet;
using netw_test::LoopbackRig;

struct Stand {
    Ref<NetwMultiplayer> core = netw_test::clocked_session();
    Vector<netw_test::DisplaySubject> subjects;
    InterpolationView view;

    Stand() = default;
    Stand(const Stand &) = delete;
    Stand &operator=(const Stand &) = delete;

    ~Stand() {
        for (int index = 0; index < subjects.size(); ++index) {
            netw_test::retire_subject(subjects[index]);
        }
    }

    int add(Node *p_parent = nullptr) {
        subjects.push_back(
            netw_test::stand_subject(core, subjects.size(), p_parent)
        );
        return subjects.size() - 1;
    }

    void record(int p_subject, const Vector2 &p_value, int64_t p_tick) {
        netw_test::record_sample(
            core,
            subjects[p_subject].body,
            p_value,
            p_tick
        );
    }

    netw::display::Runtime *runtime(int p_subject) const {
        netw::display::Runtime *found = NetwNativeTests::display_runtime_at(
            core.ptr(),
            subjects[p_subject].route
        );
        REQUIRE_MESSAGE(found != nullptr, "the subject has no runtime");
        return found;
    }

    netw::display::History &history(int p_subject) const {
        netw::display::Runtime *found = runtime(p_subject);
        REQUIRE_MESSAGE(!found->channels().is_empty(), "no channel");
        return found->channels()[0]->display_history();
    }

    void set_param(
        int p_subject,
        NetwMultiplayer::DisplayParam p_param,
        const Variant &p_value
    ) {
        core->display_set_param(
            subjects[p_subject].entity->get_rid_handle(),
            p_param,
            p_value
        );
    }

    void look() {
        core->diag_gather(view.sources);
        view.sample(*core.ptr());
    }

    void frame(int64_t p_tick, int p_offset, double p_factor) {
        netw_test::pump_at(core, p_tick, p_offset, p_factor);
        look();
    }

    const TimelineRow *row(int p_subject) const {
        for (const TimelineRow &each : view.rows()) {
            if (each.route == subjects[p_subject].route) {
                return &each;
            }
        }
        return nullptr;
    }

    const HeldSample &mark(const TimelineRow &p_row, uint32_t p_at) const {
        const bool held_there = p_at < p_row.sample_count;
        REQUIRE_MESSAGE(held_there, "the row holds no such mark");
        return view.marks()[p_row.first_sample + p_at];
    }
};

Vector2 moving(int64_t p_tick) {
    return Vector2(10.0 * double(p_tick), 0.0);
}

const TimelineRow &required(const TimelineRow *p_row) {
    REQUIRE_MESSAGE(p_row != nullptr, "the view sampled no row");
    return *p_row;
}

String first_line(const TimelineRow &p_row) {
    return p_row.text.get_slice("\n", 0);
}

void check_text(const String &p_seen, const String &p_wanted) {
    const bool same = p_seen == p_wanted;
    NETW_FORMAT_TEXT(seen_text, p_seen.utf8().get_data());
    CAPTURE(seen_text);
    CHECK(same);
}

void check_opening(const String &p_seen, const String &p_wanted) {
    const bool opens = p_seen.begins_with(p_wanted);
    NETW_FORMAT_TEXT(seen_text, p_seen.utf8().get_data());
    CAPTURE(seen_text);
    CHECK(opens);
}

const char *DRAWN_FLAT = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var smoothing := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(smoothing)
)";

const char *TURNING_BODY = R"(extends Node3D

func _init() -> void:
	var visual := Node3D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var moved := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(moved)
	var turned := NetwInterpolate.new().slerp().smooth(0.0).to(&"quaternion")
	Netw.configure_property(self, &"quaternion").state().interpolate(turned)
)";

const char *MESHED_BODY = R"(extends Node3D

func _init() -> void:
	var part := MeshInstance3D.new()
	part.name = &"Part"
	part.mesh = BoxMesh.new()
	part.position = Vector3(0.0, 0.5, -1.0)
	add_child(part)
)";

struct TurningPair {
    Node3D *authored = nullptr;
    Node3D *mirror = nullptr;
    int64_t route = 0;
};

Node3D *turning_body(const Ref<Script> &p_script) {
    Node3D *node = Object::cast_to<Node3D>(p_script->call("new"));
    REQUIRE_MESSAGE(node != nullptr, "the body script built no Node3D");
    node->set_name("Turning");
    NetwEntity::ensure(node);
    return node;
}

TurningPair stand_turning_pair(LoopbackRig &p_rig) {
    const Ref<Script> script = netw_test::script_from(TURNING_BODY);
    REQUIRE_MESSAGE(script.is_valid(), "the body script did not load");
    TurningPair pair;
    pair.authored = turning_body(script);
    p_rig.branch(-1)->add_child(pair.authored);
    pair.mirror = turning_body(script);
    p_rig.branch(0)->add_child(pair.mirror);
    NetwMultiplayer *server = netw_test::flow_core(p_rig.server());
    const Ref<NetwEntity> authored = NetwEntity::of(pair.authored);
    pair.route = server->liveness_allocate_route(authored.ptr());
    REQUIRE_MESSAGE(pair.route > 0, "the session minted no route");
    server->liveness_bind_route(pair.route, authored.ptr());
    authored->arm(Ref<NetwMultiplayer>(server));
    netw_test::flow_core(p_rig.client(0))
        ->liveness_bind_route(pair.route, NetwEntity::of(pair.mirror).ptr());
    p_rig.pump();
    return pair;
}

void author(
    Node *p_node,
    const StringName &p_property,
    const Variant &p_value,
    int64_t p_tick
) {
    netw::port_set(p_node, p_property, p_value);
    const netw::repl::PropertyBindingPtr binding
        = netw_test::binding_of(p_node, PropertySet::RECORD_STATE);
    if (binding != nullptr) {
        binding->authored_tick = p_tick;
    }
}

const TimelineRow *row_at(const InterpolationView &p_view, int64_t p_route) {
    for (const TimelineRow &each : p_view.rows()) {
        if (each.route == p_route) {
            return &each;
        }
    }
    return nullptr;
}

TEST_CASE(
    "[Networked][Diag] IV1 a remote entity whose author moves yields one "
    "mark per held tick, newest first, at the authored pose"
) {
    LoopbackRig rig(1);
    rig.declare_world(netw_test::WorldDecl().clocked(30));
    rig.mount();
    const netw_test::FlowPair pair
        = netw_test::stand_flow_pair(rig, DRAWN_FLAT, "Followed");
    rig.step_ticks(4);
    HashMap<int64_t, double> authored_x;
    for (int step = 1; step <= 8; ++step) {
        const int64_t tick = netw_test::clock_tick(rig, -1) + 1;
        const double x = 100.0 + 10.0 * step;
        netw_test::author_at(
            pair.authored,
            StringName("position"),
            Vector2(x, 0.0),
            PropertySet::RECORD_STATE,
            tick
        );
        authored_x[tick] = x;
        rig.step_ticks(1);
    }
    rig.step_ticks(2);

    NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
    InterpolationView view;
    client->diag_gather(view.sources);
    view.sample(*client);

    const TimelineRow &row = required(row_at(view, pair.route));
    const netw::display::Runtime *runtime
        = NetwNativeTests::display_runtime_at(client, pair.route);
    REQUIRE(runtime != nullptr);
    const netw::display::History &history
        = runtime->channels()[0]->display_history();
    const bool filled = history.size() > 2;
    REQUIRE_MESSAGE(filled, "the remote holds too few rows");
    NETW_CHECK_EQ(int64_t(row.sample_count), history.size());
    int64_t newer = INT64_MAX;
    for (uint32_t at = 0; at < row.sample_count; ++at) {
        const HeldSample &mark = view.marks()[row.first_sample + at];
        NETW_CHECK_LT(mark.tick, newer);
        newer = mark.tick;
        REQUIRE_MESSAGE(
            authored_x.has(mark.tick),
            vformat("mark at tick %d was never authored", mark.tick)
        );
        NETW_CHECK_CLOSE(mark.point.x, authored_x[mark.tick], 0.001);
    }
}

TEST_CASE(
    "[Networked][Diag] IV2 a late history that follows no stream walks "
    "BUFFERED, HOLD and STARVING, and the buffer is measured from the "
    "playhead"
) {
    Stand stand;
    const int subject = stand.add();
    for (int64_t tick = 10; tick <= 14; ++tick) {
        stand.record(subject, moving(tick), tick);
    }

    stand.frame(15, 3, 0.5);
    const TimelineRow &buffered = required(stand.row(subject));
    NETW_CHECK_EQ(buffered.phase, int(netw::diag::PHASE_BUFFERED));
    CHECK(buffered.has_head);
    NETW_CHECK_CLOSE(buffered.buffer, 14.0 - 12.5, 0.0001);

    stand.frame(17, 3, 0.5);
    const TimelineRow &hold = required(stand.row(subject));
    NETW_CHECK_EQ(hold.phase, int(netw::diag::PHASE_HOLD));
    NETW_CHECK_CLOSE(hold.buffer, 14.0 - 14.5, 0.0001);

    stand.record(subject, moving(15), 15);
    stand.frame(20, 3, 0.5);
    const TimelineRow &starving = required(stand.row(subject));
    NETW_CHECK_EQ(starving.phase, int(netw::diag::PHASE_STARVING));
    NETW_CHECK_CLOSE(starving.buffer, 15.0 - 17.5, 0.0001);
    NETW_CHECK_LT(starving.buffer, 0.0);
    check_text(first_line(starving), "REMOTE  STARVING");
}

TEST_CASE(
    "[Networked][Diag] IV3 a history that follows no stream reads REST once "
    "its display sleeps, and draws no buffer head"
) {
    Stand stand;
    const int subject = stand.add();
    for (int64_t tick = 10; tick <= 14; ++tick) {
        stand.record(subject, moving(tick), tick);
    }
    stand.frame(17, 3, 0.5);
    stand.frame(17, 3, 0.5);
    REQUIRE(stand.history(subject).is_sleeping());

    stand.frame(18, 3, 0.5);
    const TimelineRow &row = required(stand.row(subject));
    NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_REST));
    CHECK_FALSE(row.has_head);
    for (uint32_t at = 0; at < row.sample_count; ++at) {
        CHECK_FALSE(stand.mark(row, at).buffered);
    }
    check_text(row.text, "REMOTE  REST");
}

void stand_gapped(Stand &p_stand, int p_subject) {
    p_stand.record(p_subject, moving(10), 10);
    p_stand.record(p_subject, moving(11), 11);
    p_stand.record(p_subject, moving(13), 13);
    p_stand.history(p_subject).record_every_tick(16, moving(16));
    p_stand.frame(15, 3, 0.5);
}

TEST_CASE(
    "[Networked][Diag] IV4 a gap between moving rows counts its missed "
    "ticks, and a gap between equal rows counts none"
) {
    Stand stand;
    const int subject = stand.add();
    stand_gapped(stand, subject);

    const TimelineRow &row = required(stand.row(subject));
    NETW_REQUIRE_EQ(row.sample_count, 5);
    NETW_CHECK_EQ(stand.mark(row, 1).tick, 15);
    NETW_CHECK_EQ(stand.mark(row, 2).tick, 13);
    NETW_CHECK_EQ(row.misses, 1);
    CHECK_FALSE(stand.mark(row, 1).follows_miss);
    CHECK(stand.mark(row, 2).follows_miss);
    check_text(row.text.get_slice("\n", 2), "miss 1");
}

TEST_CASE(
    "[Networked][Diag] IV5 a forecast reads FORECAST with the age the "
    "history projected, capped by the entity"
) {
    Stand stand;
    const int subject = stand.add();
    stand.set_param(
        subject,
        NetwMultiplayer::DISPLAY_PARAM_TIMELINE_MODE,
        int(netw::display::TIMELINE_FORECAST)
    );
    stand.set_param(
        subject,
        NetwMultiplayer::DISPLAY_PARAM_MAX_FORECAST_TICKS,
        2
    );
    for (int64_t tick = 10; tick <= 14; ++tick) {
        stand.record(subject, moving(tick), tick);
    }
    stand.frame(21, 3, 0.5);

    const TimelineRow &row = required(stand.row(subject));
    REQUIRE(stand.history(subject).has_projected());
    NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_FORECAST));
    NETW_CHECK_CLOSE(
        row.forecast_age,
        stand.history(subject).get_project_age(),
        0.0001
    );
    NETW_CHECK_CLOSE(row.forecast_age, 2.0, 0.0001);
    NETW_CHECK_EQ(row.forecast_cap, int64_t(2));
    check_opening(row.text, "REMOTE  FORECAST\nforecast 2.0t of 2");
}

TEST_CASE(
    "[Networked][Diag] IV6 a world holding more remote entities than the "
    "cap samples the cap, nearest the camera"
) {
    Stand stand;
    const uint32_t count = InterpolationView::ENTITY_CAP + 1;
    for (uint32_t index = 0; index < count; ++index) {
        const int subject = stand.add();
        const Vector2 at(10000.0 + 10.0 * index, 0.0);
        stand.record(subject, at, 10);
        stand.record(subject, at + Vector2(1.0, 0.0), 11);
    }
    stand.frame(13, 3, 0.5);

    NETW_CHECK_EQ(
        int64_t(stand.view.rows().size()),
        int64_t(InterpolationView::ENTITY_CAP)
    );
    for (const TimelineRow &row : stand.view.rows()) {
        NETW_CHECK_LT(row.anchor.x, 10000.0 + 10.0 * (count - 1) - 1.0);
    }
}

TEST_CASE(
    "[Networked][Diag] IV7 a pose local to a moved parent samples in "
    "world space"
) {
    Stand stand;
    Node2D *frame = memnew(Node2D);
    netw::gd::scene_root()->add_child(frame);
    frame->set_position(Vector2(200.0, 100.0));
    const int subject = stand.add(frame);
    stand.record(subject, Vector2(10.0, 0.0), 10);
    stand.record(subject, Vector2(20.0, 0.0), 11);
    stand.frame(13, 3, 0.5);

    const TimelineRow &row = required(stand.row(subject));
    NETW_REQUIRE_EQ(row.sample_count, 2);
    NETW_CHECK_CLOSE(stand.mark(row, 0).point.x, 220.0, 0.001);
    NETW_CHECK_CLOSE(stand.mark(row, 0).point.y, 100.0, 0.001);
    NETW_CHECK_CLOSE(stand.mark(row, 1).point.x, 210.0, 0.001);
    NETW_CHECK_CLOSE(row.anchor.y, 100.0, 0.001);

    netw_test::retire_subject(stand.subjects[subject]);
    stand.subjects.clear();
    frame->get_parent()->remove_child(frame);
    memdelete(frame);
}

constexpr int CHASED_ROWS = 8;
constexpr int CHASED_QUIET_TICKS = 3;

struct Chased {
    LoopbackRig rig{1};
    netw_test::FlowPair pair;
    NetwMultiplayer *client = nullptr;
    netw::display::Runtime *runtime = nullptr;
    InterpolationView view;

    explicit Chased(bool p_steered_here) {
        rig.declare_world(netw_test::WorldDecl().clocked(30).lag_compensated());
        rig.mount();
        pair = netw_test::stand_flow_pair(
            rig,
            netw_test::gdsrc::INTERPOLATED_STATE_AND_INPUT,
            "Chased"
        );
        netw_test::steer(pair, p_steered_here ? int(rig.peer_id(0)) : 0);
        client = netw_test::flow_core(rig.client(0));
        const Ref<NetwEntity> mirrored = NetwEntity::of(pair.mirror(0));
        mirrored->get_display()->set_live_mode(
            NetwMultiplayer::LIVE_MODE_CHASE
        );
        const RID rid = mirrored->get_rid_handle();
        if (!p_steered_here) {
            client->sim_row(rid).declaration.replicas
                = netw::sim::Replicas::ACTIVE;
        }
        NETW_REQUIRE_EQ(int(client->predict_declare(rid)), int(OK));
        rig.step_ticks(4);
        for (int step = 1; step <= CHASED_ROWS; ++step) {
            netw_test::author_at(
                pair.authored,
                StringName("position"),
                Vector2(10.0 * step, 0.0),
                PropertySet::RECORD_STATE,
                netw_test::clock_tick(rig, -1) + 1
            );
            rig.step_ticks(1);
        }
        rig.step_ticks(CHASED_QUIET_TICKS);
        REQUIRE(mirrored->get_is_controlled_locally() == p_steered_here);
        runtime = NetwNativeTests::display_runtime_at(client, pair.route);
        REQUIRE_MESSAGE(runtime != nullptr, "the chased entity has no runtime");
        NETW_REQUIRE_EQ(runtime->get_pump_mode(), netw::display::PUMP_CHASE);
        client->diag_gather(view.sources);
    }

    const TimelineRow &look() {
        view.sample(*client);
        return required(row_at(view, pair.route));
    }

    const netw::display::History &history() const {
        return runtime->channels()[0]->display_history();
    }
};

TEST_CASE(
    "[Networked][Diag] IV8 a chasing entity this peer controls yields one "
    "text row while it glides and no marks at all"
) {
    Chased chased(true);
    chased.view.sample(*chased.client);
    NETW_CHECK_EQ(chased.view.rows().size(), 0);

    chased.runtime->channels()[0]->render_offset().absorb(
        Vector2(5.0, 0.0),
        INFINITY
    );
    const TimelineRow &row = chased.look();
    NETW_CHECK_EQ(chased.view.rows().size(), 1);
    NETW_CHECK_EQ(chased.view.marks().size(), 0);
    NETW_CHECK_EQ(row.phase, netw::diag::PHASE_GLIDE);
    check_text(row.text, "CHASE  GLIDE\nglide 5 px");
}

TEST_CASE(
    "[Networked][Diag] IV22 a chasing entity this peer does not control "
    "yields one mark per received row it holds, newest first, at that row's "
    "tick and pose"
) {
    Chased chased(false);
    const TimelineRow &row = chased.look();
    const netw::display::History &history = chased.history();
    REQUIRE(history.size() > 2);
    NETW_CHECK_EQ(
        int64_t(row.sample_count),
        MIN(history.size(), int64_t(InterpolationView::SAMPLE_CAP))
    );
    for (uint32_t at = 0; at < row.sample_count; ++at) {
        const HeldSample &mark = chased.view.marks()[row.first_sample + at];
        NETW_CHECK_EQ(mark.tick, history.tick_of_age(at));
        NETW_CHECK_CLOSE(
            mark.point.x,
            Vector2(history.value_of_age(at)).x,
            0.001
        );
        CHECK_FALSE(mark.buffered);
    }
}

TEST_CASE(
    "[Networked][Diag] IV23 a chasing entity this peer does not control "
    "draws a forecast segment from its newest received row to its drawn "
    "pose, and its text gives the horizon between them in ticks"
) {
    Chased chased(false);
    const TimelineRow &row = chased.look();
    REQUIRE(row.sample_count > 0);
    const HeldSample &newest = chased.view.marks()[row.first_sample];
    const int64_t horizon = netw_test::clock_tick(chased.rig, 0) - newest.tick;
    NETW_CHECK_GE(horizon, int64_t(CHASED_QUIET_TICKS));
    NETW_CHECK_EQ(row.horizon, horizon);
    check_text(row.text, vformat("CHASE  FORECAST\nhorizon %dt", horizon));

    netw::diag::Drawing drawing;
    netw::diag::draw_timelines(
        chased.view.rows(),
        chased.view.marks(),
        drawing
    );
    int64_t forecast = 0;
    for (const netw::diag::Segment &segment : drawing.segments) {
        forecast += segment.dashed && segment.from == newest.point
                && segment.to == row.anchor
                && segment.color
                    == netw::diag::TimelineStyle::tint(netw::diag::TINT_HOLD)
            ? 1
            : 0;
    }
    NETW_CHECK_EQ(forecast, int64_t(1));
}

TEST_CASE(
    "[Networked][Diag] IV9 a heading column pairs with the pose by age in "
    "the same row"
) {
    LoopbackRig rig(1);
    rig.declare_world(netw_test::WorldDecl().clocked(30));
    rig.mount();
    const TurningPair pair = stand_turning_pair(rig);
    rig.step_ticks(4);
    HashMap<int64_t, Quaternion> authored_turn;
    HashMap<int64_t, double> authored_x;
    for (int step = 1; step <= 8; ++step) {
        const int64_t tick = netw_test::clock_tick(rig, -1) + 1;
        const Quaternion turn(Vector3(0.0, 1.0, 0.0), 0.2 * step);
        author(
            pair.authored,
            StringName("position"),
            Vector3(step, 0, 0),
            tick
        );
        author(pair.authored, StringName("quaternion"), turn, tick);
        authored_turn[tick] = turn;
        authored_x[tick] = double(step);
        rig.step_ticks(1);
    }
    rig.step_ticks(2);

    NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
    InterpolationView view;
    client->diag_gather(view.sources);
    view.sample(*client);

    const TimelineRow &row = required(row_at(view, pair.route));
    const bool filled = row.sample_count > 2;
    REQUIRE_MESSAGE(filled, "the remote holds too few rows");
    for (uint32_t at = 0; at < row.sample_count; ++at) {
        const HeldSample &mark = view.marks()[row.first_sample + at];
        REQUIRE(authored_turn.has(mark.tick));
        CHECK(mark.turned);
        NETW_CHECK_CLOSE(mark.point.x, authored_x[mark.tick], 0.001);
        NETW_CHECK_GT(
            Math::abs(mark.heading.dot(authored_turn[mark.tick])),
            0.9999
        );
    }
}

TEST_CASE(
    "[Networked][Diag] IV10 an arrival age is taken when the newest row "
    "lands and held until the next one"
) {
    Stand stand;
    const int subject = stand.add();
    stand.record(subject, moving(10), 10);
    stand.frame(12, 1, 0.5);
    CHECK_FALSE(required(stand.row(subject)).has_age);

    stand.record(subject, moving(11), 11);
    stand.frame(14, 1, 0.5);
    const TimelineRow &landed = required(stand.row(subject));
    REQUIRE(landed.has_age);
    NETW_CHECK_CLOSE(landed.arrival_age, 13.5 - 11.0, 0.0001);

    stand.frame(15, 1, 0.5);
    stand.frame(16, 1, 0.5);
    const TimelineRow &held = required(stand.row(subject));
    REQUIRE(held.has_age);
    NETW_CHECK_CLOSE(held.arrival_age, 2.5, 0.0001);
}

TEST_CASE(
    "[Networked][Diag] IV12 a snap reads SNAP on line one for one second, "
    "then the phase again"
) {
    Stand stand;
    const int subject = stand.add();
    stand.record(subject, Vector2(0.0, 0.0), 10);
    stand.record(subject, Vector2(10.0, 0.0), 11);
    stand.history(subject).set_snap_distance(50.0);
    stand.record(subject, Vector2(500.0, 0.0), 12);
    stand.record(subject, Vector2(510.0, 0.0), 13);
    stand.record(subject, Vector2(520.0, 0.0), 14);

    stand.frame(15, 4, 0.5);
    REQUIRE(stand.history(subject).has_snapped());
    const TimelineRow &snapped = required(stand.row(subject));
    CHECK(snapped.snapped);
    check_text(first_line(snapped), "REMOTE  SNAP");

    stand.frame(16, 4, 0.5);
    check_text(first_line(required(stand.row(subject))), "REMOTE  SNAP");

    stand.frame(44, 4, 0.5);
    CHECK(required(stand.row(subject)).snapped);

    stand.frame(45, 4, 0.5);
    const TimelineRow &after = required(stand.row(subject));
    CHECK_FALSE(after.snapped);
    check_text(first_line(after), "REMOTE  REST");
}

Node3D *meshed_subject(const Ref<NetwMultiplayer> &p_core, int64_t p_route) {
    const Ref<Script> script = netw_test::script_from(MESHED_BODY);
    REQUIRE_MESSAGE(script.is_valid(), "the body script did not load");
    Node3D *body = Object::cast_to<Node3D>(script->call("new"));
    REQUIRE_MESSAGE(body != nullptr, "the body script built no Node3D");
    body->set_name("Meshed");
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    const Ref<netw::NetwDisplay> display = entity->get_display();
    display->set_enable_smart_dilation(false);
    display->set_display_role(NetwMultiplayer::DISPLAY_ROLE_REMOTE);
    Ref<netw::NetwInterpolate> spec;
    spec.instantiate();
    netw::Netw::configure_property(body, StringName("transform"), true)
        ->interpolate(
            netw::gd::array_of(
                spec->lerp()->smooth(0.0)->to(StringName("transform"))
            )
        );
    netw::gd::scene_root()->add_child(body);
    p_core->liveness_bind_route(p_route, entity.ptr());
    return body;
}

Transform3D turned_pose(int64_t p_tick) {
    return Transform3D(
        Basis(Vector3(0.0, 1.0, 0.0), 0.2 * double(p_tick)),
        Vector3(double(p_tick), 0.0, 0.0)
    );
}

TEST_CASE(
    "[Networked][Diag] IV17 a buffered remote casts one ghost per mesh at "
    "the newest sample's pose, each mesh keeping its offset from the body"
) {
    const Ref<NetwMultiplayer> core = netw_test::clocked_session();
    constexpr int64_t route = 7;
    Node3D *body = meshed_subject(core, route);
    const Ref<netw::NetwInterpolate> spec
        = netw::script::model::get_node_property_interpolator(
            body,
            StringName("transform")
        );
    for (int64_t tick = 10; tick <= 14; ++tick) {
        NetwNativeTests::display_record(
            core.ptr(),
            body,
            StringName("transform"),
            turned_pose(tick),
            tick,
            spec,
            false
        );
    }
    netw_test::pump_at(core, 15, 3, 0.5);
    InterpolationView view;
    core->diag_gather(view.sources);
    view.sample(*core.ptr());

    const TimelineRow &row = required(row_at(view, route));
    NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_BUFFERED));
    const bool held_some = row.sample_count > 0;
    REQUIRE(held_some);
    const HeldSample &newest = view.marks()[row.first_sample];
    NETW_CHECK_EQ(newest.tick, 14);
    Node3D *part
        = Object::cast_to<Node3D>(body->get_node_or_null(NodePath("Part")));
    REQUIRE(part != nullptr);
    const Transform3D offset = body->get_global_transform().affine_inverse()
        * part->get_global_transform();
    const Transform3D wanted = turned_pose(14) * offset;

    NETW_CHECK_EQ(view.ghosts().size(), 1);
    if (view.ghosts().size() == 1) {
        const netw::diag::Ghost &ghost = view.ghosts()[0];
        CHECK_FALSE(ghost.flat);
        CHECK(ghost.base.is_valid());
        NETW_CHECK_EQ(int64_t(ghost.world), int64_t(row.world));
        CHECK(ghost.transform.origin.is_equal_approx(wanted.origin));
        for (int axis = 0; axis < 3; ++axis) {
            CHECK(ghost.transform.basis.get_column(axis).is_equal_approx(
                wanted.basis.get_column(axis)
            ));
        }
        const bool apart = !ghost.transform.origin.is_equal_approx(
            part->get_global_position()
        );
        CHECK(apart);
    }

    body->get_parent()->remove_child(body);
    memdelete(body);
}

TEST_CASE(
    "[Networked][Diag] IV18 a mark is a thirty-second of its entity, never "
    "smaller than today's radius and never more than six times it"
) {
    const Ref<NetwMultiplayer> core = netw_test::clocked_session();
    const double sizes[3] = {0.5, 5.0, 50.0};
    Node3D *bodies[3] = {};
    for (int at = 0; at < 3; ++at) {
        const int64_t route = 7 + at;
        bodies[at] = meshed_subject(core, route);
        Object::cast_to<Node3D>(bodies[at]->get_node_or_null(NodePath("Part")))
            ->set_scale(Vector3(1.0f, 1.0f, 1.0f) * float(sizes[at]));
        const Ref<netw::NetwInterpolate> spec
            = netw::script::model::get_node_property_interpolator(
                bodies[at],
                StringName("transform")
            );
        for (int64_t tick = 10; tick <= 14; ++tick) {
            NetwNativeTests::display_record(
                core.ptr(),
                bodies[at],
                StringName("transform"),
                Transform3D(
                    turned_pose(tick).basis,
                    Vector3(double(tick), 100.0 * double(at), 0.0)
                ),
                tick,
                spec,
                false
            );
        }
    }
    netw_test::pump_at(core, 15, 3, 0.5);
    InterpolationView view;
    core->diag_gather(view.sources);
    view.sample(*core.ptr());

    netw::diag::Drawing drawing;
    netw::diag::draw_timelines(view.rows(), view.marks(), drawing);
    const TimelineRow &small = required(row_at(view, 7));
    const TimelineRow &large = required(row_at(view, 8));
    const TimelineRow &huge = required(row_at(view, 9));
    float radii[3] = {};
    const TimelineRow *rows[3] = {&small, &large, &huge};
    for (int at = 0; at < 3; ++at) {
        for (const netw::diag::Mark &mark : drawing.marks) {
            if (mark.world == rows[at]->world
                && mark.point.is_equal_approx(
                    view.marks()[rows[at]->first_sample + 1].point
                )) {
                radii[at] = mark.radius;
            }
        }
    }
    NETW_CHECK_CLOSE(radii[0], netw::diag::TimelineStyle::MARK_METRES, 0.0001);
    NETW_CHECK_CLOSE(radii[1], 5.0 / 32.0, 0.0001);
    NETW_CHECK_CLOSE(
        radii[2],
        netw::diag::TimelineStyle::MARK_METRES
            * netw::diag::TimelineStyle::MAX_SCALE,
        0.0001
    );

    for (Node3D *body : bodies) {
        body->get_parent()->remove_child(body);
        memdelete(body);
    }
}

TEST_CASE(
    "[Networked][Diag] IV19 a sprite is measured by one frame, not its whole "
    "sheet"
) {
    Stand stand;
    const int subject = stand.add();
    Ref<Image> image
        = Image::create_empty(200, 200, false, Image::FORMAT_RGBA8);
    Sprite2D *sprite = memnew(Sprite2D);
    sprite->set_texture(ImageTexture::create_from_image(image));
    sprite->set_hframes(2);
    sprite->set_vframes(2);
    stand.subjects[subject].first->add_child(sprite);
    for (int64_t tick = 10; tick <= 14; ++tick) {
        stand.record(subject, moving(tick), tick);
    }
    stand.frame(15, 3, 0.5);

    netw::diag::Drawing drawing;
    netw::diag::draw_timelines(stand.view.rows(), stand.view.marks(), drawing);
    float radius = 1.0e9f;
    for (const netw::diag::Mark &mark : drawing.marks) {
        radius = MIN(radius, mark.radius);
    }
    NETW_CHECK_CLOSE(radius, 100.0 / 32.0, 0.0001);
}

TEST_CASE(
    "[Networked][Diag] IV20 an entity's meshes are found once and kept "
    "across frames, and a mesh freed meanwhile casts no ghost"
) {
    const Ref<NetwMultiplayer> core = netw_test::clocked_session();
    constexpr int64_t route = 7;
    Node3D *body = meshed_subject(core, route);
    const Ref<netw::NetwInterpolate> spec
        = netw::script::model::get_node_property_interpolator(
            body,
            StringName("transform")
        );
    InterpolationView view;
    for (int64_t tick = 10; tick <= 16; ++tick) {
        NetwNativeTests::display_record(
            core.ptr(),
            body,
            StringName("transform"),
            turned_pose(tick),
            tick,
            spec,
            false
        );
        netw_test::pump_at(core, tick + 1, 3, 0.5);
        core->diag_gather(view.sources);
        view.sample(*core.ptr());
        NETW_CHECK_EQ(view.ghosts().size(), 1);
    }
    NETW_CHECK_EQ(view.part_walks, uint64_t(1));

    Node *part = body->get_node_or_null(NodePath("Part"));
    REQUIRE(part != nullptr);
    body->remove_child(part);
    memdelete(part);
    netw_test::pump_at(core, 18, 3, 0.5);
    core->diag_gather(view.sources);
    view.sample(*core.ptr());
    NETW_CHECK_EQ(view.ghosts().size(), 0);

    body->get_parent()->remove_child(body);
    memdelete(body);
}

TEST_CASE(
    "[Networked][Diag] IV21 only the rows a label can show carry text, the "
    "first eight of a world"
) {
    Stand stand;
    const uint32_t count = netw::diag::Sketch::TEXT_CAP + 3;
    for (uint32_t index = 0; index < count; ++index) {
        const int subject = stand.add();
        const Vector2 at(10.0 * index, 0.0);
        stand.record(subject, at, 10);
        stand.record(subject, at + Vector2(1.0, 0.0), 11);
    }
    stand.frame(13, 3, 0.5);

    NETW_REQUIRE_EQ(int64_t(stand.view.rows().size()), int64_t(count));
    int64_t texted = 0;
    int64_t texted_first = 0;
    for (uint32_t at = 0; at < count; ++at) {
        if (!stand.view.rows()[at].text.is_empty()) {
            ++texted;
            texted_first += at < netw::diag::Sketch::TEXT_CAP ? 1 : 0;
        }
    }
    NETW_CHECK_EQ(texted, int64_t(netw::diag::Sketch::TEXT_CAP));
    NETW_CHECK_EQ(texted_first, int64_t(netw::diag::Sketch::TEXT_CAP));
}

} // namespace TestDiagInterpolationViewLaws

#endif
