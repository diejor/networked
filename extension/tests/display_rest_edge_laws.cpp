#include "support/link.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED) && defined(DEBUG_ENABLED)

#include "support/value_flow_stand.h"

#include <cmath>

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/diag/interpolation_view.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/runtime.hpp"
#include "netw/object_port.hpp"
#include "netw/repl/property_set.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace TestDisplayRestEdgeLaws {

using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw::diag::InterpolationView;
using netw::diag::TimelineRow;
using netw::repl::PropertySet;
using netw_test::LoopbackRig;

constexpr int SERVER = 1;
constexpr double STEP = 10.0;
constexpr int64_t BEAT = 6;
constexpr double LATE_MS = 200.0;

const char *GLIDER = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var glide := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(glide)
)";

const char *SPINNER = R"(extends Node2D

var spin := 0.0

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var glide := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(glide)
	Netw.configure_property(self, &"spin").state()
)";

const char *BEATING = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var glide := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	var state := Netw.configure_property(self, &"position").state()
	state.heartbeat(6).interpolate(glide)
)";

const char *SNAPPING = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var glide := NetwInterpolate.new().lerp().smooth(0.0).snap_at(100.0)
	glide.to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(glide)
)";

const TimelineRow &required(const TimelineRow *p_row) {
    REQUIRE_MESSAGE(p_row != nullptr, "the view sampled no row");
    return *p_row;
}

struct Glide {
    LoopbackRig rig;
    netw_test::FlowPair pair;
    InterpolationView view;
    double x = 0.0;

    explicit Glide(const char *p_body) : rig(1) {
        rig.declare_world(netw_test::WorldDecl().clocked(30));
        rig.mount();
        pair = netw_test::stand_flow_pair(rig, p_body, "Glider");
        rig.step_ticks(4);
    }

    Glide(const Glide &) = delete;
    Glide &operator=(const Glide &) = delete;

    NetwMultiplayer *client() const {
        return netw_test::flow_core(rig.client(0));
    }

    void place(double p_x) {
        x = p_x;
        netw::port_set(
            pair.authored,
            StringName("position"),
            Vector2(p_x, 0.0)
        );
    }

    void tick() {
        rig.step_ticks(1);
        NetwNativeTests::display_pump(client(), 0.0);
    }

    void glide(int p_ticks) {
        for (int step = 0; step < p_ticks; ++step) {
            place(x + STEP);
            tick();
        }
    }

    void rest(int p_ticks) {
        for (int step = 0; step < p_ticks; ++step) {
            tick();
        }
    }

    void drop_held() {
        rig.session()->purge_packets_from(SERVER);
        rig.release(0);
    }

    netw::display::Runtime &runtime() const {
        netw::display::Runtime *found
            = NetwNativeTests::display_runtime_at(client(), pair.route);
        REQUIRE_MESSAGE(found != nullptr, "the mirror has no runtime");
        return *found;
    }

    netw::display::History &history() const {
        return runtime().channels()[0]->display_history();
    }

    int64_t newest() const {
        return history().tick_of_age(0);
    }

    bool reached(const TimelineRow &p_row) const {
        return int64_t(std::floor(p_row.playhead)) >= newest();
    }

    bool past(const TimelineRow &p_row) const {
        return int64_t(std::floor(p_row.playhead)) > newest();
    }

    const TimelineRow *look() {
        client()->diag_gather(view.sources);
        view.sample(*client());
        for (const TimelineRow &each : view.rows()) {
            if (each.route == pair.route) {
                return &each;
            }
        }
        return nullptr;
    }

    uint64_t server_token() const {
        NetwMultiplayer *server = netw_test::flow_core(rig.server());
        const netw::repl::PropertyBindingPtr binding
            = netw_test::binding_of(pair.authored, PropertySet::RECORD_STATE);
        REQUIRE_MESSAGE(binding != nullptr, "the author holds no state set");
        const netw::store::World &world = server->store();
        const netw::store::RowAddress at = world.row_of(binding->id, 0);
        REQUIRE_MESSAGE(at.valid(), "the author holds no state row");
        const uint32_t peer = world.peer_of(rig.peer_id(0));
        return world.table(at.table).peers[peer].token[at.row];
    }

    void delay_link(double p_ms) {
        const Ref<netw::NetwLinkConditions> late = netw_test::link(1);
        late->set_latency_ms(p_ms);
        rig.conditions(0, late, SERVER);
    }

    void clear_link() {
        rig.conditions(0, netw_test::link(), SERVER);
    }

    int64_t frames_out() const {
        NetwMultiplayer *server = netw_test::flow_core(rig.server());
        return int64_t(server->stats_snapshot().get("row_frames_out", 0));
    }

    int64_t refused() const {
        return client()->attribution_book().get_refused_in();
    }

    bool holds_early_edge() const {
        const netw::repl::PropertyBindingPtr binding
            = netw_test::binding_of(pair.mirror(0), PropertySet::RECORD_STATE);
        REQUIRE_MESSAGE(binding != nullptr, "the mirror holds no state set");
        const netw::store::World &world = client()->store();
        const netw::store::RowAddress at = world.row_of(binding->id, 0);
        REQUIRE_MESSAGE(at.valid(), "the mirror holds no state row");
        const netw::store::Table &table = world.table(at.table);
        const uint32_t sender = table.find_sender(SERVER);
        return sender != netw::store::Table::NO_SLOT
            && !table.senders[sender].rows[at.row].early_edge.is_empty();
    }
};

TEST_CASE(
    "[Networked][Diag] RC2 a receiver whose author stopped knows the stream "
    "finished before its playhead reaches the stop tick, and reads REST "
    "there, never HOLD or STARVING"
) {
    Glide glide(GLIDER);
    glide.glide(10);
    bool finished_early = false;
    int64_t rested = 0;
    for (int step = 0; step < 12; ++step) {
        glide.rest(1);
        const TimelineRow &row = required(glide.look());
        if (!glide.reached(row)) {
            finished_early = finished_early || row.finished;
            continue;
        }
        NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_REST));
        rested += row.phase == netw::diag::PHASE_REST ? 1 : 0;
        NETW_CHECK_GT(row.received_ahead, int64_t(0));
        const bool ahead_line = row.text.get_slice("\n", 1).begins_with(
            vformat("ahead %dt", row.received_ahead)
        );
        CHECK(ahead_line);
    }
    CHECK(finished_early);
    NETW_CHECK_GT(rested, int64_t(0));
}

TEST_CASE(
    "[Networked][Diag] RC3 a loss window while the author moves reads "
    "STARVING on every frame the playhead is past the newest row, and the "
    "runtime stays listed"
) {
    Glide glide(GLIDER);
    glide.glide(10);
    glide.rig.hold(0);
    int64_t starving = 0;
    for (int step = 0; step < 12; ++step) {
        glide.glide(1);
        const TimelineRow &row = required(glide.look());
        if (!glide.past(row)) {
            continue;
        }
        starving += 1;
        NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_STARVING));
        CHECK(glide.runtime().get_pump_listed());
    }
    glide.drop_held();
    NETW_CHECK_GT(starving, int64_t(0));
}

TEST_CASE(
    "[Networked][Diag] RC4 a lost final row reads STARVING until repair "
    "lands it, never REST, and the edge stepping from it is dropped without "
    "a stream reset"
) {
    Glide glide(GLIDER);
    glide.glide(10);
    const uint64_t token = glide.server_token();
    const int64_t refused = glide.client()->attribution_book().get_refused_in();
    glide.rig.hold(0);
    glide.glide(1);
    glide.drop_held();
    const double final_x = glide.x;
    bool landed = false;
    int64_t late = 0;
    bool rested = false;
    for (int step = 0; step < 30; ++step) {
        glide.rest(1);
        const TimelineRow &row = required(glide.look());
        landed = landed
            || Math::is_equal_approx(
                     Vector2(glide.history().value_of_age(0)).x,
                     real_t(final_x)
            );
        if (landed) {
            rested = rested || row.phase == netw::diag::PHASE_REST;
            continue;
        }
        const bool unrested = row.phase != netw::diag::PHASE_REST;
        CHECK(unrested);
        if (glide.past(row)) {
            late += 1;
            NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_STARVING));
        }
    }
    NETW_CHECK_EQ(glide.server_token(), token);
    NETW_CHECK_GT(glide.client()->attribution_book().get_refused_in(), refused);
    CHECK(landed);
    NETW_CHECK_GT(late, int64_t(0));
    CHECK(rested);
}

TEST_CASE(
    "[Networked][Diag] RC5 a row with a column that changes every tick never "
    "reads REST while its pose holds"
) {
    Glide glide(SPINNER);
    glide.glide(10);
    for (int step = 0; step < 20; ++step) {
        glide.pair.authored->set(StringName("spin"), double(step + 1));
        glide.rest(1);
        const TimelineRow &row = required(glide.look());
        const bool unrested = row.phase != netw::diag::PHASE_REST;
        CHECK(unrested);
    }
}

TEST_CASE(
    "[Networked][Diag] RC6 the first changed row after a finished stream "
    "brackets over one tick, so half a tick past the hold the sample is the "
    "midpoint"
) {
    Glide glide(GLIDER);
    glide.glide(10);
    const double held_x = glide.x;
    glide.rest(1);
    REQUIRE(glide.history().stream_finished(glide.newest()));
    glide.glide(1);
    netw::display::History &history = glide.history();
    const int64_t resume = history.tick_of_age(0);
    NETW_REQUIRE_EQ(history.tick_of_age(1), resume - 1);
    NETW_CHECK_CLOSE(Vector2(history.value_of_age(1)).x, held_x, 0.001);
    const Vector2i bracket = history.bracketing_ticks(resume - 1);
    NETW_CHECK_EQ(int64_t(bracket.x), resume - 1);
    NETW_CHECK_EQ(int64_t(bracket.y), resume);
    const Variant half = history.sample(
        resume - 1,
        0.5,
        Vector2(held_x, 0.0),
        1,
        false,
        0,
        1.0 / 30.0,
        Variant(),
        false
    );
    NETW_CHECK_CLOSE(Vector2(half).x, (held_x + glide.x) / 2.0, 0.001);
}

TEST_CASE(
    "[Networked][Diag] RC7 with a heartbeat a finished stream stays REST "
    "while beats land, and reads STARVING once its received tick is older "
    "than the heartbeat"
) {
    Glide glide(BEATING);
    glide.glide(10);
    glide.rest(6);
    int64_t rested = 0;
    for (int step = 0; step < BEAT * 4; ++step) {
        glide.rest(1);
        const TimelineRow &row = required(glide.look());
        NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_REST));
        rested += row.phase == netw::diag::PHASE_REST ? 1 : 0;
    }
    NETW_CHECK_EQ(rested, BEAT * 4);
    glide.rig.hold(0);
    int64_t stale = 0;
    for (int step = 0; step < BEAT * 3; ++step) {
        glide.rest(1);
        const TimelineRow &row = required(glide.look());
        const int64_t silent = int64_t(std::floor(row.playhead))
            - glide.history().received_tick();
        if (silent > BEAT) {
            stale += 1;
            NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_STARVING));
        } else {
            NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_REST));
        }
    }
    glide.drop_held();
    NETW_CHECK_GT(stale, int64_t(0));
}

TEST_CASE(
    "[Networked][Diag] RC8 a teleport at rest reads SNAP, and reads REST "
    "again only once a row after the teleport lands"
) {
    Glide glide(SNAPPING);
    glide.glide(10);
    glide.rest(8);
    NETW_REQUIRE_EQ(required(glide.look()).phase, int(netw::diag::PHASE_REST));
    glide.place(glide.x + 1000.0);
    bool snapped = false;
    bool rested = false;
    for (int step = 0; step < 30; ++step) {
        if (step == 1) {
            glide.rig.hold(0);
        }
        glide.rest(1);
        if (step == 1) {
            glide.drop_held();
        }
        const TimelineRow &row = required(glide.look());
        snapped = snapped || row.snapped;
        if (row.phase != netw::diag::PHASE_REST) {
            continue;
        }
        rested = true;
        NETW_CHECK_GT(glide.history().received_tick(), glide.newest());
    }
    CHECK(snapped);
    CHECK(rested);
}

TEST_CASE(
    "[Networked][Diag] RC9 a rest edge that overtakes its own final row "
    "waits for it, so the stream reads finished the tick that row lands and "
    "REST from then on, with no repair sent"
) {
    Glide glide(GLIDER);
    glide.glide(10);
    glide.delay_link(LATE_MS);
    glide.glide(1);
    glide.clear_link();
    const double final_x = glide.x;
    const int64_t sent = glide.frames_out();
    const int64_t refused = glide.refused();
    glide.rest(1);
    REQUIRE_MESSAGE(
        glide.refused() > refused,
        "the edge did not overtake its row"
    );
    bool landed = false;
    int64_t rested = 0;
    for (int step = 0; step < 16; ++step) {
        glide.rest(1);
        const bool holds_final = Math::is_equal_approx(
            Vector2(glide.history().value_of_age(0)).x,
            real_t(final_x)
        );
        if (holds_final && !landed) {
            landed = true;
            CHECK(glide.history().stream_finished(glide.newest()));
        }
        const TimelineRow &row = required(glide.look());
        if (landed && glide.reached(row)) {
            NETW_CHECK_EQ(row.phase, int(netw::diag::PHASE_REST));
            rested += row.phase == netw::diag::PHASE_REST ? 1 : 0;
        }
    }
    CHECK(landed);
    NETW_CHECK_GT(rested, int64_t(0));
    NETW_CHECK_EQ(glide.frames_out(), sent + 1);
}

TEST_CASE(
    "[Networked][Diag] RC10 a rest edge waiting for its row is discarded "
    "when a newer changed row lands, and is never offered again"
) {
    Glide glide(GLIDER);
    glide.glide(10);
    glide.delay_link(LATE_MS);
    glide.glide(1);
    glide.clear_link();
    glide.rest(1);
    REQUIRE(glide.holds_early_edge());
    const int64_t refused = glide.refused();
    glide.glide(1);
    CHECK_FALSE(glide.holds_early_edge());
    NETW_CHECK_EQ(glide.refused(), refused);
    for (int step = 0; step < 16; ++step) {
        glide.rest(1);
    }
    NETW_CHECK_EQ(glide.refused(), refused + 1);
    NETW_CHECK_CLOSE(
        Vector2(glide.history().value_of_age(0)).x,
        glide.x,
        0.001
    );
    CHECK(glide.history().stream_finished(glide.newest()));
}

} // namespace TestDisplayRestEdgeLaws

#endif
