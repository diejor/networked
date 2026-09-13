#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/interp_stand.h"
#include "support/netw_cells.h"

namespace TestInterpCalculusLaws {

using namespace godot;
using namespace netw_test;

constexpr double DURATION = 5.0;
constexpr int64_t SEED = 1234;
constexpr double STEP_SLACK = 2.5;
constexpr double STEP_SLACK_DEGRADED = 3.5;
constexpr double JERK_SLACK = 3.5;
constexpr double FORECAST_SLACK = 1.5;
constexpr double FORECAST_EPS = 0.75;
constexpr int FORECAST_SEND_PERIOD = 3;

struct Cell {
    String label;
    Oracle truth;
    LinkPreset link;
    double step_slack = STEP_SLACK;
};

Cell cell(const Oracle &p_truth, const LinkPreset &p_link, double p_slack) {
    Cell out;
    out.label = String(p_truth.label) + "/" + String(p_link.label);
    out.truth = p_truth;
    out.link = p_link;
    out.step_slack = p_slack;
    return out;
}

struct Displayed {
    Cell declared;
    Metrics measured;
    double step_bound = 0.0;
    double jerk_bound = 0.0;
    double lag_max = 0.0;
    double truth_speed = 0.0;
};

Displayed display_run(const Cell &p_cell) {
    InterpHarness harness;
    harness.configure(0.05, p_cell.truth.value_at(0.0));
    harness.run(p_cell.truth, p_cell.link, DURATION, SEED);

    Displayed out;
    out.declared = p_cell;
    out.measured = analyze(harness, p_cell.truth, harness.warmup_hint());
    const double dt = harness.frame_dt();
    out.step_bound = p_cell.truth.speed_max() * dt * p_cell.step_slack;
    out.jerk_bound = p_cell.truth.accel_max() * dt * dt
        + JERK_SLACK * p_cell.truth.speed_max() * dt;
    out.lag_max = double(harness.display_offset) / harness.tickrate + 0.30;
    out.truth_speed = p_cell.truth.speed_max();
    return out;
}

typedef LawRowFor<Displayed> CalculusLaw;

LawVerdict law_sampled(const Displayed &p_run) {
    if (p_run.measured.samples <= 0) {
        return law_broken("the run displayed nothing past its warmup");
    }
    return law_held();
}

LawVerdict law_monotonic(const Displayed &p_run) {
    if (!p_run.measured.playhead_monotonic) {
        return law_broken(
            "the playhead ran backward by %.4f ticks",
            p_run.measured.max_playhead_backstep
        );
    }
    return law_held();
}

LawVerdict law_continuous(const Displayed &p_run) {
    if (p_run.measured.max_step > p_run.step_bound) {
        return law_broken(
            "one frame stepped %.3f against a %.3f motion bound",
            p_run.measured.max_step,
            p_run.step_bound
        );
    }
    return law_held();
}

LawVerdict law_steady(const Displayed &p_run) {
    if (p_run.measured.regressions != 0) {
        return law_broken(
            "%d frames moved backward",
            p_run.measured.regressions
        );
    }
    if (p_run.measured.stalls != 0) {
        return law_broken(
            "%d frames stalled while the truth moved",
            p_run.measured.stalls
        );
    }
    if (p_run.measured.moved <= 0) {
        return law_broken("the display never moved at all");
    }
    const double error = std::abs(p_run.measured.mean_speed - p_run.truth_speed)
        / p_run.truth_speed;
    if (error >= 0.05) {
        return law_broken(
            "the mean speed %.2f is %.1f%% off the truth",
            p_run.measured.mean_speed,
            error * 100.0
        );
    }
    return law_held();
}

LawVerdict law_trails(const Displayed &p_run) {
    if (p_run.measured.led_truth) {
        return law_broken(
            "the display led the truth by %.3f s",
            -p_run.measured.min_lag_sec
        );
    }
    if (p_run.measured.max_lag_sec > p_run.lag_max) {
        return law_broken(
            "the display trailed %.3f s against a %.3f s ceiling",
            p_run.measured.max_lag_sec,
            p_run.lag_max
        );
    }
    return law_held();
}

LawVerdict law_smooth(const Displayed &p_run) {
    if (p_run.measured.max_jerk > p_run.jerk_bound) {
        return law_broken(
            "the jerk %.3f exceeds the %.3f curvature bound",
            p_run.measured.max_jerk,
            p_run.jerk_bound
        );
    }
    return law_held();
}

const CalculusLaw L_SAMPLED = {
    "sampled",
    "the run produced frames past its warmup, so the laws read something",
    &law_sampled,
};

const CalculusLaw L_MONOTONIC = {
    "monotonic",
    "display time never runs backward",
    &law_monotonic,
};

const CalculusLaw L_CONTINUOUS = {
    "continuous",
    "no frame steps further than the motion allows, so nothing teleports",
    &law_continuous,
};

const CalculusLaw L_STEADY = {
    "steady",
    "the display moves forward every frame at the truth's own speed",
    &law_steady,
};

const CalculusLaw L_TRAILS = {
    "trails",
    "the display trails the truth by a bounded, non-negative delay",
    &law_trails,
};

const CalculusLaw L_SMOOTH = {
    "smooth",
    "the second difference stays under the curvature and dilation bound",
    &law_smooth,
};

void motion_cells(std::vector<Cell> &r_cells) {
    const LinkPreset LINKS[] = {perfect_link(), wifi_link(), mobile_4g_link()};
    for (const LinkPreset &link : LINKS) {
        r_cells.push_back(cell(linear_truth(), link, STEP_SLACK));
        r_cells.push_back(cell(circle_truth(), link, STEP_SLACK));
    }
}

void every_cell(std::vector<Cell> &r_cells) {
    const LinkPreset LINKS[]
        = {perfect_link(),
           wifi_link(),
           mobile_4g_link(),
           poor_3g_link(),
           satellite_link()};
    for (const LinkPreset &link : LINKS) {
        r_cells.push_back(cell(linear_truth(), link, STEP_SLACK));
        r_cells.push_back(cell(circle_truth(), link, STEP_SLACK));
    }
}

void degraded_cells(std::vector<Cell> &r_cells) {
    const LinkPreset LINKS[]
        = {poor_3g_link(), satellite_link(), heavy_loss_link()};
    for (const LinkPreset &link : LINKS) {
        r_cells.push_back(cell(linear_truth(), link, STEP_SLACK_DEGRADED));
        r_cells.push_back(cell(circle_truth(), link, STEP_SLACK_DEGRADED));
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] the display calculus holds over the "
    "clean to moderate links"
) {
    std::vector<Cell> cells;
    motion_cells(cells);
    const CalculusLaw LAWS[] = {L_SAMPLED, L_MONOTONIC, L_CONTINUOUS, L_STEADY};
    for (const Cell &declared : cells) {
        const Displayed run = display_run(declared);
        for (const CalculusLaw &law : LAWS) {
            NETW_CELL(law, declared);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] the display trails a constant-velocity "
    "truth by a bounded delay on every link"
) {
    const LinkPreset LINKS[]
        = {perfect_link(),
           wifi_link(),
           mobile_4g_link(),
           poor_3g_link(),
           satellite_link()};
    for (const LinkPreset &link : LINKS) {
        const Cell declared = cell(linear_truth(), link, STEP_SLACK);
        const Displayed run = display_run(declared);
        NETW_CELL(L_TRAILS, declared);
        NETW_LAW_HOLDS(L_TRAILS, run);
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] the displayed sequence stays under its "
    "jerk bound on every link, clean to satellite"
) {
    std::vector<Cell> cells;
    every_cell(cells);
    for (const Cell &declared : cells) {
        const Displayed run = display_run(declared);
        NETW_CELL(L_SMOOTH, declared);
        NETW_LAW_HOLDS(L_SMOOTH, run);
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] heavy loss grows the lag and never "
    "tears the output"
) {
    std::vector<Cell> cells;
    degraded_cells(cells);
    const CalculusLaw LAWS[] = {L_SAMPLED, L_MONOTONIC, L_CONTINUOUS, L_STEADY};
    for (const Cell &declared : cells) {
        const Displayed run = display_run(declared);
        for (const CalculusLaw &law : LAWS) {
            NETW_CELL(law, declared);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] the chase filter smooths a body that "
    "snaps once a tick into a hiccup-free display"
) {
    const Oracle TRUTHS[] = {linear_truth(), circle_truth()};
    for (const Oracle &truth : TRUTHS) {
        InterpHarness harness;
        harness.configure_chase(0.05, truth.value_at(0.0));
        harness.run_chase(truth, DURATION);
        const Metrics measured = analyze(harness, truth, 0.5);
        const double dt = harness.frame_dt();
        const double bound
            = truth.accel_max() * dt * dt + JERK_SLACK * truth.speed_max() * dt;
        CHECK(measured.playhead_monotonic);
        NETW_CHECK_EQ(measured.regressions, 0);
        NETW_CHECK_EQ(int(measured.max_jerk <= bound), 1);
    }
}

void settle_chase(InterpHarness &r_harness) {
    r_harness.configure_chase(0.005, Vector2(0, 0));
    r_harness.set_body(Vector2(0, 0));
    for (int at = 0; at < 30; ++at) {
        r_harness.step_chase(double(at) / r_harness.fps);
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] a recovery is absorbed as a decaying "
    "render offset, so the display glides onto the corrected body"
) {
    InterpHarness harness;
    settle_chase(harness);

    harness.set_body(Vector2(1, 0));
    harness.absorb_recovery(Vector2(1, 0), false);
    harness.step_chase(0.6);
    NETW_CHECK_EQ(int(harness.displayed.back().length() < 0.3), 1);

    for (int at = 0; at < 90; ++at) {
        harness.step_chase(0.7 + double(at) / harness.fps);
    }
    NETW_CHECK_EQ(
        int(harness.displayed.back().distance_to(Vector2(1, 0)) <= 0.02),
        1
    );
}

TEST_CASE(
    "[Networked][Display][Calculus] a correction TRAIN is absorbed onto "
    "the offset still gliding, so no correction reaches the screen"
) {
    InterpHarness harness;
    settle_chase(harness);

    Vector2 body;
    Vector2 last = harness.displayed.back();
    double worst_step = 0.0;
    for (int at = 0; at < 60; ++at) {
        if (at % 3 == 0) {
            body += Vector2(0.1, 0);
            harness.set_body(body);
            harness.absorb_recovery(Vector2(0.1, 0), false);
        }
        harness.step_chase(0.6 + double(at) / harness.fps);
        const Vector2 shown = harness.displayed.back();
        worst_step = std::max(worst_step, double(shown.distance_to(last)));
        last = shown;
    }
    NETW_CHECK_EQ(int(worst_step < 0.05), 1);
}

TEST_CASE(
    "[Networked][Display][Calculus] two runs of one scenario emit "
    "identical sequences and identical stats, so replay is a property"
) {
    const Cell declared = cell(circle_truth(), poor_3g_link(), STEP_SLACK);

    InterpHarness first;
    first.configure(0.05, declared.truth.value_at(0.0));
    first.run(declared.truth, declared.link, DURATION, SEED);

    InterpHarness second;
    second.configure(0.05, declared.truth.value_at(0.0));
    second.run(declared.truth, declared.link, DURATION, SEED);

    NETW_CHECK_EQ(first_difference(first.displayed, second.displayed), -1);
    NETW_CHECK_EQ(
        first_difference(first.written->samples, second.written->samples),
        -1
    );
    NETW_CHECK_EQ(int(same_stats(first.run_stats, second.run_stats)), 1);
}

ScheduleHarness scheduled(const std::vector<int> &p_order) {
    ScheduleHarness harness;
    harness.add_lane(LaneDecl{linear_truth(), wifi_link(), 11});
    harness.add_lane(LaneDecl{circle_truth(), poor_3g_link(), 22});
    harness.add_lane(LaneDecl{linear_truth(), mobile_4g_link(), 33});
    harness.run(DURATION, p_order);
    return harness;
}

TEST_CASE(
    "[Networked][Display][Calculus] runtimes share no state, so any "
    "partition of the pump emits the same per-lane sequences"
) {
    ScheduleHarness forward = scheduled({0, 1, 2});
    ScheduleHarness permuted = scheduled({2, 0, 1});

    for (size_t at = 0; at < forward.lanes.size(); ++at) {
        NETW_CHECK_EQ(
            first_difference(
                forward.lanes[at].displayed,
                permuted.lanes[at].displayed
            ),
            -1
        );
    }
    NETW_CHECK_EQ(int(same_stats(forward.run_stats, permuted.run_stats)), 1);
}

void forecast_run(
    InterpHarness &r_harness,
    const Oracle &p_truth,
    double p_smoothing
) {
    r_harness.send_period = FORECAST_SEND_PERIOD;
    r_harness.timeline_mode = netw::display::TIMELINE_FORECAST;
    r_harness.configure(p_smoothing, p_truth.value_at(0.0));
    r_harness.run(p_truth, perfect_link(), DURATION, SEED);
}

void buffered_run(
    InterpHarness &r_harness,
    const Oracle &p_truth,
    double p_smoothing
) {
    r_harness.send_period = FORECAST_SEND_PERIOD;
    r_harness.configure(p_smoothing, p_truth.value_at(0.0));
    r_harness.run(p_truth, perfect_link(), DURATION, SEED);
}

double max_error_at_playhead(
    const InterpHarness &p_run,
    const Oracle &p_truth
) {
    const double warm = p_run.warmup_hint();
    double worst = 0.0;
    for (size_t at = 0; at < p_run.frames.size(); ++at) {
        if (p_run.frames[at] < warm) {
            continue;
        }
        const double t = p_run.playhead_time[at] / p_run.tickrate;
        worst = std::max(
            worst,
            double(p_run.displayed[at].distance_to(p_truth.value_at(t)))
        );
    }
    return worst;
}

double mean_error_vs_live(const InterpHarness &p_run, const Oracle &p_truth) {
    const double warm = p_run.warmup_hint();
    double total = 0.0;
    int counted = 0;
    for (size_t at = 0; at < p_run.frames.size(); ++at) {
        if (p_run.frames[at] < warm) {
            continue;
        }
        total += double(
            p_run.displayed[at].distance_to(p_truth.value_at(p_run.frames[at]))
        );
        counted += 1;
    }
    return total / std::max(1.0, double(counted));
}

TEST_CASE(
    "[Networked][Display][Calculus] a FORECAST display projects past the "
    "newest sample, within the tangent bound and the tick cap"
) {
    const Oracle TRUTHS[] = {linear_truth(), circle_truth()};
    for (const Oracle &truth : TRUTHS) {
        InterpHarness run;
        forecast_run(run, truth, 0.0);
        NETW_CHECK_EQ(int(run.run_stats.projecting > 0), 1);
        NETW_CHECK_EQ(
            int(run.run_stats.max_forecast_age
                <= double(run.max_forecast_ticks) + 0.001),
            1
        );

        const double age_sec = run.run_stats.max_forecast_age / run.tickrate;
        const double span_sec = double(run.send_period) / run.tickrate;
        const double bound = truth.accel_max() * age_sec * (age_sec + span_sec)
                * FORECAST_SLACK
            + FORECAST_EPS;
        NETW_CHECK_EQ(int(max_error_at_playhead(run, truth) <= bound), 1);
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] a FORECAST display tracks live truth "
    "more closely than the same stream buffered, and never tears"
) {
    const Oracle TRUTHS[] = {linear_truth(), circle_truth()};
    for (const Oracle &truth : TRUTHS) {
        InterpHarness ahead;
        InterpHarness behind;
        forecast_run(ahead, truth, 0.05);
        buffered_run(behind, truth, 0.05);
        NETW_CHECK_EQ(
            int(mean_error_vs_live(ahead, truth)
                < mean_error_vs_live(behind, truth)),
            1
        );
        const Metrics measured = analyze(ahead, truth, ahead.warmup_hint());
        CHECK(measured.playhead_monotonic);
        NETW_CHECK_EQ(measured.regressions, 0);
    }
}

TEST_CASE(
    "[Networked][Display][Calculus] a BRACKETED runtime never forecasts, "
    "however the timeline is set, because the server holds its own "
    "newest sample"
) {
    InterpHarness harness;
    harness.send_period = FORECAST_SEND_PERIOD;
    harness.timeline_mode = netw::display::TIMELINE_FORECAST;
    harness.pump_mode = netw::display::PUMP_BRACKETED;
    const Oracle truth = linear_truth();
    harness.configure(0.0, truth.value_at(0.0));
    harness.run(truth, perfect_link(), DURATION, SEED);
    NETW_CHECK_EQ(harness.run_stats.projecting, 0);
}

TEST_CASE(
    "[Networked][Display][Calculus] a stream that stops while its truth is "
    "still sleeps rather than projecting a motionless value"
) {
    const Oracle truth = stationary_truth();
    InterpHarness run;
    forecast_run(run, truth, 0.0);
    NETW_CHECK_EQ(int(run.run_stats.sleeping > 0), 1);
    NETW_CHECK_EQ(run.run_stats.projecting, 0);

    const double warm = run.warmup_hint();
    double drift = 0.0;
    for (size_t at = 0; at < run.frames.size(); ++at) {
        if (run.frames[at] < warm) {
            continue;
        }
        drift = std::max(
            drift,
            double(run.displayed[at].distance_to(truth.origin))
        );
    }
    NETW_CHECK_EQ(int(drift < 0.001), 1);
}

TEST_CASE(
    "[Networked][Display][Calculus] a stream that stops while moving "
    "projects out to the cap and then holds instead of drifting away"
) {
    const Oracle truth = linear_truth();
    InterpHarness harness;
    harness.timeline_mode = netw::display::TIMELINE_FORECAST;
    harness.record_cutoff_sec = 1.0;
    harness.configure(0.0, truth.value_at(0.0));
    harness.run(truth, perfect_link(), 3.0, SEED);

    NETW_CHECK_EQ(int(harness.run_stats.projecting > 0), 1);
    NETW_CHECK_EQ(
        int(harness.run_stats.max_forecast_age
            >= double(harness.max_forecast_ticks) - 1.0),
        1
    );
    NETW_CHECK_EQ(
        int(harness.run_stats.max_forecast_age
            <= double(harness.max_forecast_ticks) + 0.001),
        1
    );

    const size_t n = harness.displayed.size();
    NETW_CHECK_EQ(
        int(harness.displayed[n - 1].distance_to(harness.displayed[n - 2])
            < 0.01),
        1
    );
}

} // namespace TestInterpCalculusLaws

#endif
