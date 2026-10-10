#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED) && defined(DEBUG_ENABLED)

#include <cmath>
#include <initializer_list>
#include <memory>

#include "godot/rendering_server.hpp"
#include "godot/world.hpp"
#include "netw/diag/interpolation_view.hpp"
#include "netw/diag/prediction_rings.hpp"
#include "netw/diag/sketch.hpp"
#include "netw/display/decl.hpp"

using namespace godot;

namespace TestDiagSketchLaws {

using netw::diag::Drawing;
using netw::diag::Ghost;
using netw::diag::HeldSample;
using netw::diag::Mark;
using netw::diag::MARK_OURS;
using netw::diag::MARK_SERVER;
using netw::diag::MARK_SHOWN;
using netw::diag::MARK_SIMULATED;
using netw::diag::PredictionRings;
using netw::diag::RingRow;
using netw::diag::Segment;
using netw::diag::Sketch;
using netw::diag::Text;
using netw::diag::TimelineRow;
using netw::diag::TimelineStyle;

constexpr int64_t SHARED_SPATIAL_RIDS = 7;
constexpr int64_t SHARED_FLAT_RIDS = 3;
constexpr int64_t SHARED_GHOST_RIDS = 2;
constexpr int64_t SPACE_RIDS = 10;
constexpr int64_t PLANE_RIDS = 4;
constexpr int64_t GHOST_BATCH_RIDS = 2;
constexpr uint32_t GHOSTS = 3;

void every_primitive_in(
    Drawing &r_drawing,
    uint64_t p_world,
    bool p_flat,
    const RID &p_ghost_base
) {
    for (uint32_t at = 0; at < GHOSTS; ++at) {
        Ghost ghost;
        ghost.world = p_world;
        ghost.flat = p_flat;
        ghost.base = p_ghost_base;
        ghost.transform.origin = Vector3(float(at), 0.0f, 0.0f);
        ghost.rect = Rect2(0.0f, 0.0f, 8.0f, 8.0f);
        ghost.source = ghost.rect;
        ghost.color = Color(0.2f, 0.8f, 0.9f, 0.5f);
        r_drawing.ghosts.push_back(ghost);
    }
    for (const netw::diag::MarkShape shape :
         {netw::diag::SHAPE_RING,
          netw::diag::SHAPE_DASHED_RING,
          netw::diag::SHAPE_DISC}) {
        Mark mark;
        mark.world = p_world;
        mark.flat = p_flat;
        mark.shape = shape;
        mark.radius = 1.0f;
        mark.color = Color(1.0f, 1.0f, 1.0f);
        r_drawing.marks.push_back(mark);
    }
    for (const bool dashed : {false, true}) {
        Segment segment;
        segment.world = p_world;
        segment.flat = p_flat;
        segment.dashed = dashed;
        segment.to = Vector3(20.0f, 0.0f, 0.0f);
        segment.color = Color(1.0f, 1.0f, 1.0f);
        r_drawing.segments.push_back(segment);
    }
}

bool same_tint(const Color &p_seen, const Color &p_wanted) {
    return Math::is_equal_approx(p_seen.r, p_wanted.r)
        && Math::is_equal_approx(p_seen.g, p_wanted.g)
        && Math::is_equal_approx(p_seen.b, p_wanted.b);
}

bool same_point(const Vector3 &p_seen, const Vector3 &p_wanted) {
    return p_seen.is_equal_approx(p_wanted);
}

TEST_CASE(
    "[Networked][Diag] S1 a sketch frees every batch and ghost it drew into "
    "a world once the world is gone, and everything else when it ends"
) {
    RenderingServer *server = RenderingServer::get_singleton();
    RID mesh = server->mesh_create();
    RID texture = server->texture_2d_placeholder_create();
    const int64_t before = Sketch::live_rids();
    auto sketch = std::make_unique<Sketch>();
    Ref<World3D> world;
    world.instantiate();
    Ref<World2D> plane;
    plane.instantiate();
    Drawing drawing;
    every_primitive_in(
        drawing,
        uint64_t(world->get_instance_id()),
        false,
        mesh
    );
    every_primitive_in(
        drawing,
        uint64_t(plane->get_instance_id()),
        true,
        texture
    );

    sketch->draw(drawing);
    sketch->draw(drawing);
    NETW_CHECK_EQ(int64_t(sketch->space_count()), int64_t(2));
    const int64_t drawing_rids = Sketch::live_rids();
    NETW_CHECK_EQ(
        drawing_rids - before,
        SHARED_SPATIAL_RIDS + SHARED_FLAT_RIDS + SHARED_GHOST_RIDS + SPACE_RIDS
            + GHOST_BATCH_RIDS + PLANE_RIDS
    );

    world.unref();
    plane.unref();
    sketch->draw(Drawing());
    NETW_CHECK_EQ(int64_t(sketch->space_count()), int64_t(0));
    NETW_CHECK_EQ(
        Sketch::live_rids(),
        drawing_rids - SPACE_RIDS - GHOST_BATCH_RIDS - PLANE_RIDS
    );

    sketch.reset();
    NETW_CHECK_EQ(Sketch::live_rids(), before);
    server->free_rid(texture);
    server->free_rid(mesh);
}

void ghosts_of(
    Drawing &r_drawing,
    uint64_t p_world,
    const RID &p_base,
    uint32_t p_count
) {
    for (uint32_t at = 0; at < p_count; ++at) {
        Ghost ghost;
        ghost.world = p_world;
        ghost.base = p_base;
        ghost.transform.origin = Vector3(float(at), 0.0f, 0.0f);
        ghost.color = Color(0.2f, 0.8f, 0.9f, 0.5f);
        r_drawing.ghosts.push_back(ghost);
    }
}

TEST_CASE(
    "[Networked][Diag] S2 ghosts sharing a mesh in one world share one "
    "batch, however many there are"
) {
    RenderingServer *server = RenderingServer::get_singleton();
    const RID truck = server->mesh_create();
    const RID wheel = server->mesh_create();
    const int64_t before = Sketch::live_rids();
    auto sketch = std::make_unique<Sketch>();
    Ref<World3D> world;
    world.instantiate();
    const uint64_t id = uint64_t(world->get_instance_id());

    Drawing few;
    ghosts_of(few, id, truck, 2);
    ghosts_of(few, id, wheel, 8);
    sketch->draw(few);
    const int64_t opened = Sketch::live_rids() - before;
    NETW_CHECK_EQ(
        opened,
        SHARED_SPATIAL_RIDS + SHARED_GHOST_RIDS + 2 * GHOST_BATCH_RIDS
    );

    Drawing many;
    ghosts_of(many, id, truck, 40);
    ghosts_of(many, id, wheel, 160);
    sketch->draw(many);
    NETW_CHECK_EQ(Sketch::live_rids() - before, opened);

    world.unref();
    sketch->draw(Drawing());
    NETW_CHECK_EQ(
        Sketch::live_rids() - before,
        SHARED_SPATIAL_RIDS + SHARED_GHOST_RIDS
    );
    sketch.reset();
    NETW_CHECK_EQ(Sketch::live_rids(), before);
    server->free_rid(wheel);
    server->free_rid(truck);
}

RingRow every_ring_row(bool p_flat) {
    RingRow row;
    row.world = 7;
    row.viewport = 9;
    row.flat = p_flat;
    row.marks = MARK_SHOWN | MARK_SIMULATED | MARK_SERVER | MARK_OURS;
    row.shown = Vector3(1.0f, 0.0f, 0.0f);
    row.simulated = Vector3(2.0f, 0.0f, 0.0f);
    row.server = Vector3(3.0f, 0.0f, 0.0f);
    row.ours = Vector3(4.0f, 0.0f, 0.0f);
    row.gap = 1.0;
    row.confirmed_alpha = PredictionRings::STALE_ALPHA;
    row.text = "OPEN";
    return row;
}

TEST_CASE(
    "[Networked][Diag] R11 a ring row draws shown, simulated and server as "
    "rings, ours dashed, the error as a segment and the text over what is "
    "simulated, in metres in 3D and pixels in 2D"
) {
    for (const bool flat : {false, true}) {
        LocalVector<RingRow> rows;
        rows.push_back(every_ring_row(flat));
        Drawing drawing;
        netw::diag::draw_ring_rows(rows, drawing);
        const float scale = flat ? PredictionRings::PIXELS_PER_METRE : 1.0f;

        NETW_CHECK_EQ(int64_t(drawing.marks.size()), int64_t(4));
        NETW_CHECK_EQ(int64_t(drawing.segments.size()), int64_t(1));
        NETW_CHECK_EQ(int64_t(drawing.texts.size()), int64_t(1));
        if (drawing.marks.size() != 4 || drawing.segments.size() != 1
            || drawing.texts.size() != 1) {
            continue;
        }
        const float radii[4] = {
            PredictionRings::RADIUS_SHOWN,
            PredictionRings::RADIUS_SIMULATED,
            PredictionRings::RADIUS_SERVER,
            PredictionRings::RADIUS_OURS,
        };
        for (uint32_t at = 0; at < 4; ++at) {
            const Mark &mark = drawing.marks[at];
            NETW_CHECK_CLOSE(mark.radius, radii[at] * scale, 0.0001);
            NETW_CHECK_CLOSE(mark.point.x, double(at + 1), 0.0001);
            NETW_CHECK_EQ(int64_t(mark.world), int64_t(7));
            NETW_CHECK_EQ(int64_t(mark.viewport), int64_t(9));
            CHECK(mark.flat == flat);
            NETW_CHECK_EQ(
                int64_t(mark.shape),
                int64_t(
                    at == 3 ? netw::diag::SHAPE_DASHED_RING
                            : netw::diag::SHAPE_RING
                )
            );
        }
        NETW_CHECK_CLOSE(drawing.marks[0].color.a, 1.0, 0.0001);
        NETW_CHECK_CLOSE(
            drawing.marks[2].color.a,
            PredictionRings::STALE_ALPHA,
            0.0001
        );
        const Segment &error = drawing.segments[0];
        CHECK_FALSE(error.dashed);
        NETW_CHECK_CLOSE(error.from.x, 3.0, 0.0001);
        NETW_CHECK_CLOSE(error.to.x, 4.0, 0.0001);
        NETW_CHECK_CLOSE(drawing.texts[0].anchor.x, 2.0, 0.0001);
    }
}

HeldSample held_at(int64_t p_tick, bool p_buffered) {
    HeldSample sample;
    sample.tick = p_tick;
    sample.point = Vector3(0.14f * float(p_tick), 0.0f, 0.0f);
    sample.buffered = p_buffered;
    return sample;
}

TimelineRow remote_row(
    double p_playhead,
    int p_phase,
    bool p_flat,
    const LocalVector<HeldSample> &p_held
) {
    TimelineRow row;
    row.world = 7;
    row.viewport = 9;
    row.flat = p_flat;
    row.pump = netw::display::PUMP_REMOTE;
    row.phase = p_phase;
    row.has_head = p_phase != netw::diag::PHASE_REST;
    row.playhead = p_playhead;
    row.first_sample = 0;
    row.sample_count = p_held.size();
    row.anchor = Vector3(0.14f * float(p_playhead), 0.0f, 0.0f);
    return row;
}

struct Timeline {
    LocalVector<TimelineRow> rows;
    LocalVector<HeldSample> held;
    Drawing drawing;

    void add(const TimelineRow &p_row, const LocalVector<HeldSample> &p_held) {
        TimelineRow row = p_row;
        row.first_sample = held.size();
        row.sample_count = p_held.size();
        for (const HeldSample &each : p_held) {
            held.push_back(each);
        }
        rows.push_back(row);
    }

    void draw() {
        drawing.clear();
        netw::diag::draw_timelines(rows, held, drawing);
    }

    uint32_t discs() const {
        uint32_t found = 0;
        for (const Mark &each : drawing.marks) {
            found += each.shape == netw::diag::SHAPE_DISC ? 1 : 0;
        }
        return found;
    }
};

LocalVector<HeldSample> buffered_window() {
    LocalVector<HeldSample> held;
    held.push_back(held_at(10, true));
    held.push_back(held_at(9, true));
    held.push_back(held_at(8, false));
    held.push_back(held_at(7, false));
    held.push_back(held_at(6, false));
    return held;
}

TEST_CASE(
    "[Networked][Diag] IV13 a remote row draws one small disc per held "
    "sample, buffered tinted apart from past, faded by tick distance from "
    "the playhead"
) {
    for (const bool flat : {false, true}) {
        Timeline timeline;
        const LocalVector<HeldSample> held = buffered_window();
        timeline.add(
            remote_row(8.4, netw::diag::PHASE_BUFFERED, flat, held),
            held
        );
        timeline.draw();

        NETW_CHECK_EQ(int64_t(timeline.discs()), int64_t(held.size() + 1));
        if (timeline.drawing.marks.size() != held.size() + 1) {
            continue;
        }
        const float radius
            = flat ? TimelineStyle::MARK_PIXELS : TimelineStyle::MARK_METRES;
        const double reach = 8.4 - 6.0;
        for (uint32_t at = 0; at < held.size(); ++at) {
            const Mark &mark = timeline.drawing.marks[at + 1];
            NETW_CHECK_CLOSE(mark.radius, radius, 0.0001);
            CHECK(same_point(mark.point, held[at].point));
            const bool tinted = same_tint(
                mark.color,
                TimelineStyle::tint(
                    held[at].buffered ? netw::diag::TINT_BUFFERED
                                      : netw::diag::TINT_PAST
                )
            );
            CHECK(tinted);
            NETW_CHECK_CLOSE(
                mark.color.a,
                1.0 - std::abs(double(held[at].tick) - 8.4) / (reach + 1.0),
                0.0001
            );
        }
        CHECK_FALSE(same_tint(
            TimelineStyle::tint(netw::diag::TINT_BUFFERED),
            TimelineStyle::tint(netw::diag::TINT_PAST)
        ));
    }
}

TEST_CASE(
    "[Networked][Diag] IV14 the newest sample carries a head coloured by "
    "phase, and a resting row carries none"
) {
    const int phases[4] = {
        netw::diag::PHASE_BUFFERED,
        netw::diag::PHASE_HOLD,
        netw::diag::PHASE_STARVING,
        netw::diag::PHASE_FORECAST,
    };
    const netw::diag::TimelineTint tints[4] = {
        netw::diag::TINT_BUFFERED,
        netw::diag::TINT_HOLD,
        netw::diag::TINT_STARVING,
        netw::diag::TINT_HOLD,
    };
    for (uint32_t at = 0; at < 4; ++at) {
        Timeline timeline;
        const LocalVector<HeldSample> held = buffered_window();
        timeline.add(remote_row(8.4, phases[at], false, held), held);
        timeline.draw();
        REQUIRE_MESSAGE(!timeline.drawing.marks.is_empty(), "nothing drawn");
        const Mark &head = timeline.drawing.marks[0];
        NETW_CHECK_CLOSE(
            head.radius,
            TimelineStyle::MARK_METRES * TimelineStyle::HEAD_SCALE,
            0.0001
        );
        CHECK(same_point(head.point, held[0].point));
        const bool tinted
            = same_tint(head.color, TimelineStyle::tint(tints[at]));
        CHECK(tinted);
    }
    CHECK_FALSE(same_tint(
        TimelineStyle::tint(netw::diag::TINT_HOLD),
        TimelineStyle::tint(netw::diag::TINT_STARVING)
    ));

    Timeline resting;
    LocalVector<HeldSample> held = buffered_window();
    for (HeldSample &each : held) {
        each.buffered = false;
    }
    resting.add(remote_row(8.4, netw::diag::PHASE_REST, false, held), held);
    resting.draw();
    NETW_CHECK_EQ(int64_t(resting.discs()), int64_t(held.size()));
}

TEST_CASE(
    "[Networked][Diag] IV15 the two samples around the playhead are joined "
    "solid, a pair spanning a miss dashed, and a forecast dashed from the "
    "newest sample to the shown pose"
) {
    LocalVector<HeldSample> held;
    held.push_back(held_at(10, true));
    held.push_back(held_at(9, true));
    held.push_back(held_at(8, false));
    held.push_back(held_at(5, false));
    held[2].follows_miss = true;
    Timeline timeline;
    timeline.add(
        remote_row(8.4, netw::diag::PHASE_BUFFERED, false, held),
        held
    );
    timeline.draw();

    NETW_CHECK_EQ(int64_t(timeline.drawing.segments.size()), int64_t(2));
    if (timeline.drawing.segments.size() == 2) {
        const Segment &bracket = timeline.drawing.segments[0];
        CHECK_FALSE(bracket.dashed);
        CHECK(same_point(bracket.from, held[2].point));
        CHECK(same_point(bracket.to, held[1].point));
        const Segment &miss = timeline.drawing.segments[1];
        CHECK(miss.dashed);
        CHECK(same_point(miss.from, held[3].point));
        CHECK(same_point(miss.to, held[2].point));
    }

    LocalVector<HeldSample> ran_out;
    ran_out.push_back(held_at(10, false));
    ran_out.push_back(held_at(9, false));
    Timeline forecast;
    TimelineRow row
        = remote_row(12.0, netw::diag::PHASE_FORECAST, false, ran_out);
    forecast.add(row, ran_out);
    forecast.draw();
    NETW_CHECK_EQ(int64_t(forecast.drawing.segments.size()), int64_t(1));
    if (forecast.drawing.segments.size() == 1) {
        const Segment &ahead = forecast.drawing.segments[0];
        CHECK(ahead.dashed);
        CHECK(same_point(ahead.from, ran_out[0].point));
        CHECK(same_point(ahead.to, row.anchor));
    }
}

TEST_CASE(
    "[Networked][Diag] IV16 a turned sample draws a heading tick along its "
    "rotation, and a chasing row draws only its glide segment and text"
) {
    LocalVector<HeldSample> held = buffered_window();
    const Quaternion quarter(Vector3(0.0f, 1.0f, 0.0f), 1.5707963f);
    for (HeldSample &each : held) {
        each.turned = true;
        each.heading = quarter;
    }
    Timeline turned;
    turned.add(remote_row(8.4, netw::diag::PHASE_BUFFERED, false, held), held);
    turned.draw();
    uint32_t ticks = 0;
    for (const Segment &each : turned.drawing.segments) {
        const Vector3 span = each.to - each.from;
        if (Math::is_equal_approx(
                span.length(),
                TimelineStyle::HEADING_METRES
            )) {
            ++ticks;
            const Vector3 along = quarter.xform(Vector3(0.0f, 0.0f, -1.0f));
            CHECK(same_point(span.normalized(), along));
        }
    }
    NETW_CHECK_EQ(int64_t(ticks), int64_t(held.size()));

    Timeline chasing;
    TimelineRow glide;
    glide.world = 7;
    glide.pump = netw::display::PUMP_CHASE;
    glide.phase = netw::diag::PHASE_GLIDE;
    glide.chase_target = Vector3(1.0f, 0.0f, 0.0f);
    glide.anchor = Vector3(1.3f, 0.0f, 0.0f);
    glide.text = "CHASE  GLIDE";
    chasing.add(glide, LocalVector<HeldSample>());
    chasing.draw();
    NETW_CHECK_EQ(int64_t(chasing.drawing.marks.size()), int64_t(0));
    NETW_CHECK_EQ(int64_t(chasing.drawing.segments.size()), int64_t(1));
    NETW_CHECK_EQ(int64_t(chasing.drawing.texts.size()), int64_t(1));
    if (chasing.drawing.segments.size() == 1) {
        CHECK(same_point(chasing.drawing.segments[0].from, glide.chase_target));
        CHECK(same_point(chasing.drawing.segments[0].to, glide.anchor));
    }
}

} // namespace TestDiagSketchLaws

#endif
